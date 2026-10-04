#define	__MODULE__	"VBKSAV"
#define	__IDENT__	"X01-03"
#define	__REV__		"1.3.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbksav.c
**
**  ABSTRACT:	The save: input specifications -> saveset, and /VERIFY.
**
**  DESCRIPTION: Every entry the walk hands over becomes a FILE record,
**		the DATA records of its contents and an FEND record with the
**		checksum and the status; a catalog entry is spooled for it
**		into an unnamed temporary file.  At the end the spooled
**		entries become the CATALOG records, then END and TRAILER.
**
**		A regular file is read region by region (SEEK_DATA and
**		SEEK_HOLE), so the holes of a sparse file cost nothing.  It
**		is read up to the size it had when it was opened; the
**		attributes are asked again after the read, and a file whose
**		size, mtime or ctime moved meanwhile is reported FILCHANGED
**		and gets the status CHANGED.
**
**		The second and every later name of a file with several hard
**		links is saved as a HARDLINK to the first one.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-03		 3-OCT-2026	RRL
**		The files are read SEQUENTIAL; a cold one is dropped from the
**		cache behind the read.  VBACKUP_PIPELINE=0 - no writer thread.
**		The read-ahead of files runs while the inputs are walked.
**
**	X01-02		 3-OCT-2026	RRL
**		Stage 2.  A time filter makes the saveset INCREMENTAL: the files
**		it does not choose get a catalog entry PRESENT and no records.
**		The catalog entries carry CTIME and DEVINO.  The bases go into
**		the SUMMARY as absolute names.  /RECORD: the state of every file
**		saved OK goes into the journal - after /VERIFY, and only when
**		that found no difference.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<pwd.h>
#include	<sys/stat.h>
#include	<sys/utsname.h>
#include	<sys/ioctl.h>
#include	<linux/fs.h>

#include	"vbkdef.h"

#define	VBK$K_HLHASH	4096			/* Buckets of the hard link table		*/

/*
**  A file with more than one name, already saved under <name>
*/
typedef struct vbk_hlink_t
{
	struct vbk_hlink_t *next;
	uint64_t	dev, ino;
	char		name [];
} VBK$HLINK;

/*
**  The state of a save
*/
typedef struct vbk_save_t
{
	VBK$OPTS *	opts;
	VBK$WCTX	wctx;
	VBK$TLVB	rec;			/* The record body being built			*/
	VBK$TLVB	xbuf;			/* Extended attributes of the current file	*/
	VBK$TLVB	cat;			/* A catalog entry				*/
	FILE *		spool;			/* The catalog entries, until the end		*/
	uint8_t *	iobuf;			/* VBK$K_DATAHDR + VBACKUP$K_IOBUF		*/
	VBK$HLINK *	hlink [VBK$K_HLHASH];
	uint32_t	fileno;
	uint64_t	nfiles, nbytes, nentries;
	uint32_t	nerrors;
	uint64_t	npresent;		/* Covered, not saved: PRESENT entries		*/
	VBK$HASH	record;			/* /RECORD: absolute name -> VBK$FSTATE		*/
	VBK$TIME	created;		/* Of the SUMMARY: the RECORDED of the journal	*/
	int		failed;			/* The saveset itself could not be written	*/
} VBK$SAVE;


static	int	s_vbk$volcb	(
		void *		a_arg,
		uint32_t	a_volno,
	const	char *		a_spec
			)
{
VBK$SAVE *	l_sav = (VBK$SAVE *) a_arg;

	if ( l_sav->opts->log || (a_volno > 1) )
		$VBKMSG(VBACKUP$_CREATED, a_spec);

	return	STS$K_SUCCESS;
}


/*
**  A failure of the saveset itself: reported once, the save is to stop
*/
static	int	s_vbk$wrterr	(
		VBK$SAVE *	a_sav
			)
{
	if ( !a_sav->failed )
		$VBKMSG(VBACKUP$_WRITERR, a_sav->wctx.volspec, a_sav->wctx.err, strerror(a_sav->wctx.err));

	a_sav->failed	= 1;

	return	STS$K_FATAL;
}


/*
**  Look a file up in the hard link table; not there - it is entered
**  under <a_name> and NULL is returned
*/
static	const char *	s_vbk$hlink	(
		VBK$SAVE *	a_sav,
		uint64_t	a_dev,
		uint64_t	a_ino,
	const	char *		a_name
			)
{
unsigned	l_h = (unsigned) ((a_ino ^ (a_dev << 7)) % VBK$K_HLHASH);
VBK$HLINK *	l_e;
size_t		l_len = strlen(a_name);

	for ( l_e = a_sav->hlink [l_h]; l_e; l_e = l_e->next )
		if ( (l_e->dev == a_dev) && (l_e->ino == a_ino) )
			return	l_e->name;

	if ( (l_e = malloc(sizeof(*l_e) + l_len + 1)) )
		{
		l_e->dev	= a_dev;
		l_e->ino	= a_ino;
		memcpy(l_e->name, a_name, l_len + 1);
		l_e->next	= a_sav->hlink [l_h];
		a_sav->hlink [l_h] = l_e;
		}

	return	NULL;
}


/*
**  Spool the catalog entry of a file that has been saved: u32 length,
**  then the TLV items
*/
static	int	s_vbk$catent	(
		VBK$SAVE *	a_sav,
	const	VBK$ATTR *	a_attr,
	const	VBK$LOC *	a_loc,
		uint32_t	a_crc,
		uint8_t		a_status
			)
{
VBK$TLVB *	l_c = &a_sav->cat;
uint8_t		l_len [4];
int		l_ok = 1;

	vbk$tlv_reset(l_c);

	l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_FILENO, a_attr->fileno);
	l_ok &= vbk$tlv_put(l_c, VBK$K_TAG_PATH, a_attr->pathlen, a_attr->path);
	l_ok &= vbk$tlv_u8(l_c, VBK$K_TAG_FTYPE, a_attr->ftype);
	l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_MODE, a_attr->mode);
	l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_UID, a_attr->uid);
	l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_GID, a_attr->gid);

	if ( a_attr->uname )
		l_ok &= vbk$tlv_str(l_c, VBK$K_TAG_UNAME, a_attr->uname);

	if ( a_attr->gname )
		l_ok &= vbk$tlv_str(l_c, VBK$K_TAG_GNAME, a_attr->gname);

	l_ok &= vbk$tlv_u64(l_c, VBK$K_TAG_SIZE, a_attr->size);
	l_ok &= vbk$tlv_time(l_c, VBK$K_TAG_MTIME, &a_attr->mtime);
	l_ok &= vbk$tlv_time(l_c, VBK$K_TAG_CTIME, &a_attr->ctime);

	if ( a_attr->link )
		l_ok &= vbk$tlv_put(l_c, VBK$K_TAG_LINK, a_attr->linklen, a_attr->link);

	l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_NLINK, a_attr->nlink);
	l_ok &= vbk$tlv_u64x2(l_c, VBK$K_TAG_DEVINO, a_attr->dev, a_attr->ino);
	l_ok &= vbk$tlv_u16(l_c, VBK$K_TAG_BASEIDX, a_attr->baseidx);
	l_ok &= vbk$tlv_u8(l_c, VBK$K_TAG_STATUS, a_status);

	/* A PRESENT entry has no records in the stream: nothing to point at, nothing summed */
	if ( a_status != VBK$K_FS_PRESENT )
		{
		l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_CRC, a_crc);
		l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_LOCVOL, a_loc->vol);
		l_ok &= vbk$tlv_u64(l_c, VBK$K_TAG_LOCBLK, a_loc->blk);
		l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_LOCOFF, a_loc->off);
		}

	if ( !l_ok )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));

	vbk$put32(l_len, l_c->len);

	if ( (1 != fwrite(l_len, sizeof(l_len), 1, a_sav->spool)) || (1 != fwrite(l_c->buf, l_c->len, 1, a_sav->spool)) )
		return	$VBKMSG(VBACKUP$_WRITERR, "the catalog spool", errno, strerror(errno));

	a_sav->nentries++;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Save the contents of a regular file: DATA records for its regions
**	of data, up to the size it had when it was opened.
**
**  FORMAL PARAMETERS:
**
**	a_sav		The save
**	a_fd		The file, open
**	a_path		Its specification, for the diagnostics
**	a_size		Its size when it was opened
**	a_crc		Receives the checksum of the data saved
**	a_saved		Receives the logical size saved: the end of the last
**			region read, or <a_size>
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- saved;
**	STS$K_ERROR	- a read failed and has been reported, the rest of
**			  the file is not in the saveset;
**	STS$K_FATAL	- the saveset could not be written.
**--
*/
static	int	s_vbk$data	(
		VBK$SAVE *	a_sav,
		int		a_fd,
		uint32_t	a_fileno,
	const	char *		a_path,
		uint64_t	a_size,
		uint32_t *	a_crc,
		uint64_t *	a_saved
			)
{
uint8_t *	l_hdr = a_sav->iobuf, *l_data = a_sav->iobuf + VBK$K_DATAHDR;
off_t		l_beg, l_end, l_off;
ssize_t		l_n;
uint32_t	l_crc = 0;
size_t		l_want;
int		l_cold;

	*a_saved	= a_size;

	/* Cold before it was read: the read-ahead knows, if it has read it */
	if ( 0 > (l_cold = vbk$pre_take(a_sav->opts->pre, a_path)) )
		l_cold	= vbk$os_cold(a_fd, a_size);

	vbk$os_seq(a_fd);

	for ( l_off = 0; (uint64_t) l_off < a_size; )
		{
		/* The next region of data; none, or no support for the question - the file is all data */
		if ( 0 > (l_beg = lseek(a_fd, l_off, SEEK_DATA)) )
			{
			if ( errno == ENXIO )
				break;

			l_beg	= l_off;
			l_end	= (off_t) a_size;
			}
		else if ( 0 > (l_end = lseek(a_fd, l_beg, SEEK_HOLE)) )
			l_end	= (off_t) a_size;

		if ( (uint64_t) l_beg >= a_size )
			break;

		if ( (uint64_t) l_end > a_size )
			l_end	= (off_t) a_size;

		for ( l_off = l_beg; l_off < l_end; )
			{
			l_want	= (size_t) (((l_end - l_off) < VBACKUP$K_IOBUF) ? (l_end - l_off) : VBACKUP$K_IOBUF);

			if ( 0 > (l_n = pread(a_fd, l_data, l_want, l_off)) )
				{
				if ( errno == EINTR )
					continue;

				$VBKMSG(VBACKUP$_READERR, a_path, errno, strerror(errno));

				*a_crc	 = l_crc;
				*a_saved = (uint64_t) l_off;

				return	STS$K_ERROR;
				}

			/* The file has shrunk under us: what there is, is saved */
			if ( !l_n )
				{
				*a_crc	 = l_crc;
				*a_saved = (uint64_t) l_off;

				return	STS$K_SUCCESS;
				}

			vbk$put32(l_hdr, a_fileno);
			vbk$put32(l_hdr + 4, 0);
			vbk$put64(l_hdr + 8, (uint64_t) l_off);

			if ( !(1 & vbk$wrt_record(&a_sav->wctx, VBK$K_RT_DATA, l_hdr, VBK$K_DATAHDR + (uint32_t) l_n, NULL)) )
				return	s_vbk$wrterr(a_sav);

			l_crc	= $VBK_CRC(l_crc, l_data, l_n);

			if ( l_cold )
				vbk$os_drop(a_fd, (uint64_t) l_off, (uint64_t) l_n);

			l_off	+= l_n;
			a_sav->nbytes += (uint64_t) l_n;
			}
		}

	*a_crc	= l_crc;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Open an entry the walk has handed over and collect its attributes;
**	the save and the copy share it.  A regular file and a directory are
**	opened - the flags and the data are read through the descriptor -
**	without following a link that might have taken the name meanwhile,
**	and without moving atime.  A regular file flagged nodump is left out
**	here (directories are judged by the walk), unless /IGNORE=NOBACKUP.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command
**	a_ent		The entry
**	a_fd		Receives the descriptor, -1 - none was opened
**	a_attr		Receives the attributes
**	a_xbuf		Receives the extended attributes
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- open, attributes collected;
**	STS$K_WARN	- left out (nodump), reported under /LOG;
**	STS$K_ERROR	- it cannot be looked at, reported;
**	STS$K_FATAL	- no memory, reported.
**--
*/
int	vbk$sav_open	(
	const	VBK$OPTS *	a_opts,
	const	VBK$ENT *	a_ent,
		int *		a_fd,
		VBK$ATTR *	a_attr,
		VBK$TLVB *	a_xbuf
			)
{
struct stat	l_st;
int		l_fd = -1, l_flags = 0, l_status;

	*a_fd	= -1;

	if ( lstat(a_ent->path, &l_st) )
		return	$VBKMSG(VBACKUP$_OPENIN, a_ent->path, errno, strerror(errno));

	if ( S_ISREG(l_st.st_mode) || S_ISDIR(l_st.st_mode) )
		{
		int	l_oflags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | (S_ISDIR(l_st.st_mode) ? O_DIRECTORY : 0);

		if ( (0 > (l_fd = open(a_ent->path, l_oflags | O_NOATIME))) && (errno == EPERM) )
			l_fd	= open(a_ent->path, l_oflags);

		if ( l_fd < 0 )
			return	$VBKMSG(VBACKUP$_OPENIN, a_ent->path, errno, strerror(errno));

		if ( S_ISREG(l_st.st_mode) && !a_opts->nobackup && !ioctl(l_fd, FS_IOC_GETFLAGS, &l_flags) && (l_flags & FS_NODUMP_FL) )
			{
			close(l_fd);

			if ( a_opts->log )
				$VBKMSG(VBACKUP$_SKIPPED, a_ent->name, "nodump flag set");

			return	STS$K_WARN;
			}
		}

	if ( STS$K_ERROR == (l_status = vbk$atr_get(a_ent->path, l_fd, a_opts->xattrs, a_attr, a_xbuf)) )
		{
		l_status = $VBKMSG(VBACKUP$_OPENIN, a_ent->path, errno, strerror(errno));

		if ( l_fd >= 0 )
			close(l_fd);

		return	l_status;
		}

	if ( l_status == STS$K_FATAL )
		{
		if ( l_fd >= 0 )
			close(l_fd);

		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));
		}

	*a_fd	= l_fd;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The action routine of the walk: save one entry.
**
**  FORMAL PARAMETERS:
**
**	a_ent		The entry
**	a_arg		The save
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- saved, or skipped and reported;
**	STS$K_FATAL	- the save is to stop: the saveset failed, or /CONFIRM
**			  was answered QUIT.
**--
*/
static	int	s_vbk$entry	(
	const	VBK$ENT *	a_ent,
		void *		a_arg
			)
{
VBK$SAVE *	l_sav = (VBK$SAVE *) a_arg;
VBK$OPTS *	l_o = l_sav->opts;
VBK$ATTR	l_attr, l_after;
VBK$TLVB	l_xafter = {0};
VBK$LOC		l_loc;
const char *	l_first = NULL;
uint64_t	l_saved = 0;
uint32_t	l_crc = 0;
uint8_t		l_fstat = VBK$K_FS_OK;
int		l_fd = -1, l_status;

	/* Covered, not chosen by the time filter: a catalog entry, nothing else */
	if ( a_ent->present )
		{
		if ( !(1 & vbk$atr_get(a_ent->path, -1, 0, &l_attr, &l_sav->xbuf)) )
			return	STS$K_SUCCESS;

		l_attr.fileno	= ++l_sav->fileno;
		l_attr.path	= a_ent->name;
		l_attr.pathlen	= (uint32_t) strlen(a_ent->name);
		l_attr.baseidx	= a_ent->baseidx;
		l_sav->npresent++;

		return	(1 & s_vbk$catent(l_sav, &l_attr, &l_loc, 0, VBK$K_FS_PRESENT)) ? STS$K_SUCCESS : STS$K_FATAL;
		}

	if ( l_o->confirm )
		{
		if ( STS$K_FATAL == (l_status = vbk$confirm("Save", a_ent->name)) )
			return	STS$K_FATAL;

		if ( !(1 & l_status) )
			return	STS$K_SUCCESS;
		}

	if ( STS$K_SUCCESS != (l_status = vbk$sav_open(l_o, a_ent, &l_fd, &l_attr, &l_sav->xbuf)) )
		{
		if ( l_status == STS$K_ERROR )
			l_sav->nerrors++;

		return	(l_status == STS$K_FATAL) ? STS$K_FATAL : STS$K_SUCCESS;
		}

	l_attr.fileno	= ++l_sav->fileno;
	l_attr.path	= a_ent->name;
	l_attr.pathlen	= (uint32_t) strlen(a_ent->name);
	l_attr.baseidx	= a_ent->baseidx;

	/* A further name of a file already saved: a link to the first one, no data */
	if ( (l_attr.ftype == VBK$K_FT_REG) && (l_attr.nlink > 1) && (l_first = s_vbk$hlink(l_sav, l_attr.dev, l_attr.ino, a_ent->name)) )
		{
		l_attr.ftype	= VBK$K_FT_HARDLINK;
		l_attr.link	= l_first;
		l_attr.linklen	= (uint32_t) strlen(l_first);
		}

	vbk$tlv_reset(&l_sav->rec);

	if ( !(1 & vbk$atr_tlv(&l_attr, &l_sav->rec)) )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM)), STS$K_FATAL;

	if ( !(1 & vbk$wrt_record(&l_sav->wctx, VBK$K_RT_FILE, l_sav->rec.buf, l_sav->rec.len, &l_loc)) )
		{
		if ( l_fd >= 0 )
			close(l_fd);

		return	s_vbk$wrterr(l_sav);
		}

	if ( l_attr.ftype == VBK$K_FT_REG )
		{
		l_status = s_vbk$data(l_sav, l_fd, l_attr.fileno, a_ent->path, l_attr.size, &l_crc, &l_saved);

		if ( l_status == STS$K_FATAL )
			{
			close(l_fd);

			return	STS$K_FATAL;
			}

		if ( l_status == STS$K_ERROR )
			l_fstat	= VBK$K_FS_READERR;
		else if ( (1 & vbk$atr_get(a_ent->path, l_fd, 0, &l_after, &l_xafter))
			&& ((l_after.size != l_attr.size) || (l_after.mtime.sec != l_attr.mtime.sec) || (l_after.mtime.nsec != l_attr.mtime.nsec)
			|| (l_after.ctime.sec != l_attr.ctime.sec) || (l_after.ctime.nsec != l_attr.ctime.nsec)) )
			{
			$VBKMSG(VBACKUP$_FILCHANGED, a_ent->path);
			l_fstat	= VBK$K_FS_CHANGED;
			}

		vbk$tlv_free(&l_xafter);
		}

	if ( l_fd >= 0 )
		close(l_fd);

	/* FEND: the checksum and how the file fared */
	vbk$tlv_reset(&l_sav->rec);
	vbk$tlv_u32(&l_sav->rec, VBK$K_TAG_FILENO, l_attr.fileno);
	vbk$tlv_u64(&l_sav->rec, VBK$K_TAG_SIZE, (l_attr.ftype == VBK$K_FT_REG) ? l_saved : l_attr.size);
	vbk$tlv_u32(&l_sav->rec, VBK$K_TAG_CRC, l_crc);
	vbk$tlv_u8(&l_sav->rec, VBK$K_TAG_STATUS, l_fstat);

	if ( !(1 & vbk$wrt_record(&l_sav->wctx, VBK$K_RT_FEND, l_sav->rec.buf, l_sav->rec.len, NULL)) )
		return	s_vbk$wrterr(l_sav);

	if ( l_fstat != VBK$K_FS_OK )
		l_sav->nerrors++;

	l_sav->nfiles++;

	if ( l_attr.ftype == VBK$K_FT_REG )
		l_attr.size	= l_saved;

	if ( !(1 & s_vbk$catent(l_sav, &l_attr, &l_loc, l_crc, l_fstat)) )
		return	STS$K_FATAL;

	/* /RECORD: what the journal is to know of it, once the saveset is complete (format.md, 9, rule 3) */
	if ( l_o->record && (l_fstat == VBK$K_FS_OK) && (l_attr.ftype != VBK$K_FT_DIR) )
		{
		VBK$FSTATE *	l_st;
		void *		l_old = NULL;

		if ( !(l_st = calloc(1, sizeof(*l_st))) || !(1 & vbk$hash_put(&l_sav->record, a_ent->abspath, l_st, &l_old)) )
			return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM)), STS$K_FATAL;

		free(l_old);

		l_st->dev	= l_attr.dev;
		l_st->ino	= l_attr.ino;
		l_st->size	= l_attr.size;
		l_st->ctime	= l_attr.ctime;
		l_st->mtime	= l_attr.mtime;
		l_st->ftype	= l_attr.ftype;
		}

	if ( l_o->log )
		$VBKMSG(VBACKUP$_SAVED, a_ent->name);

	return	STS$K_SUCCESS;
}


/*
**  The SUMMARY record: who, where, when, how
*/
static	int	s_vbk$summary	(
		VBK$SAVE *	a_sav,
		VBK$TLVB *	a_tlvb,
		char		a_base [][VBACKUP$K_SZ_PATH],
		unsigned	a_nbase
			)
{
VBK$OPTS *	l_o = a_sav->opts;
struct utsname	l_uts;
struct timespec	l_now;
VBK$TIME	l_tim;
char		l_sys [512];
const char *	l_user = vbk$atr_uname(geteuid());
int		l_ok = 1;

	clock_gettime(CLOCK_REALTIME, &l_now);
	l_tim.sec	= l_now.tv_sec;
	l_tim.nsec	= (uint32_t) l_now.tv_nsec;
	a_sav->created	= l_tim;

	uname(&l_uts);
	snprintf(l_sys, sizeof(l_sys), "%s %s %s", l_uts.sysname, l_uts.release, l_uts.machine);

	l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_PRODUCT, "VBACKUP " VBACKUP_K_IDENT);
	l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_HOST, l_uts.nodename);
	l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_USER, l_user ? l_user : "?");
	l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_CMDLINE, l_o->cmdline);
	l_ok &= vbk$tlv_time(a_tlvb, VBK$K_TAG_CREATED, &l_tim);

	/* Absolute: a compare without a directory, and the journal, find the files by it from anywhere */
	for ( unsigned i = 0; i < a_nbase; i++ )
		{
		char	l_abs [VBACKUP$K_SZ_PATH];

		l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_BASE, realpath(a_base [i], l_abs) ? l_abs : a_base [i]);
		}

	l_ok &= vbk$tlv_u8(a_tlvb, VBK$K_TAG_KIND, l_o->timefilter ? VBK$K_KIND_INCR : VBK$K_KIND_FULL);

	if ( l_o->timefilter )
		l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_FILTER, l_o->filter);

	l_ok &= vbk$tlv_u32(a_tlvb, VBK$K_TAG_BLOCKSIZE, l_o->bsize);
	l_ok &= vbk$tlv_u32(a_tlvb, VBK$K_TAG_GROUPSIZE, l_o->grpsz);
	l_ok &= vbk$tlv_u64(a_tlvb, VBK$K_TAG_VOLSIZE, l_o->volsize);

	if ( l_o->comment [0] )
		l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_COMMENT, l_o->comment);

	l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_SYSTEM, l_sys);

	return	l_ok ? STS$K_SUCCESS : STS$K_FATAL;
}


/*
**  The spooled catalog entries become CATALOG records of at most
**  VBK$K_MAXCAT octets of body each
*/
static	int	s_vbk$catalog	(
		VBK$SAVE *	a_sav,
		VBK$LOC *	a_catloc
			)
{
uint8_t *	l_buf, l_len [4];
uint32_t	l_fill = 0, l_elen;
int		l_first = 1, l_status = STS$K_SUCCESS;

	if ( !(l_buf = malloc(VBK$K_MAXCAT)) )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));

	rewind(a_sav->spool);

	for ( ;; )
		{
		int	l_eof = (1 != fread(l_len, sizeof(l_len), 1, a_sav->spool));

		l_elen	= l_eof ? 0 : vbk$get32(l_len);

		/* The record is full, or this is the end: out with it */
		if ( l_eof || ((l_fill + 4 + l_elen) > VBK$K_MAXCAT) )
			{
			if ( l_fill || l_first )
				{
				if ( !(1 & vbk$wrt_record(&a_sav->wctx, VBK$K_RT_CATALOG, l_buf, l_fill, l_first ? a_catloc : NULL)) )
					{
					l_status = s_vbk$wrterr(a_sav);
					break;
					}

				l_first	= 0;
				l_fill	= 0;
				}

			if ( l_eof )
				break;
			}

		if ( (l_elen + 4) > VBK$K_MAXCAT )
			{
			l_status = $VBKMSG(VBACKUP$_READERR, "the catalog spool", EOVERFLOW, strerror(EOVERFLOW));
			break;
			}

		memcpy(l_buf + l_fill, l_len, 4);

		if ( 1 != fread(l_buf + l_fill + 4, l_elen, 1, a_sav->spool) )
			{
			l_status = $VBKMSG(VBACKUP$_READERR, "the catalog spool", errno, strerror(errno));
			break;
			}

		l_fill	+= 4 + l_elen;
		}

	free(l_buf);

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The save operation.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: the inputs, the saveset in OUTPUT
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the saveset is complete (files may have been reported);
**	STS$K_FATAL	- it is not.
**--
*/
int	vbk$save	(
		VBK$OPTS *	a_opts
			)
{
VBK$SAVE *	l_sav;
VBK$TLVB	l_sum = {0}, l_trl = {0};
VBK$LOC		l_catloc = {0};
char		(*l_base) [VBACKUP$K_SZ_PATH];
uint64_t	l_nblocks;
uint32_t	l_nvols;
int		l_status = STS$K_SUCCESS;

	if ( !(l_sav = calloc(1, sizeof(*l_sav))) || !(l_base = calloc(a_opts->ninput, VBACKUP$K_SZ_PATH))
		|| !(l_sav->iobuf = malloc(VBK$K_DATAHDR + VBACKUP$K_IOBUF)) )
		return	$VBKMSG(VBACKUP$_NOMEM, errno, strerror(errno)), STS$K_FATAL;

	l_sav->opts	= a_opts;

	if ( !(l_sav->spool = tmpfile()) )
		return	$VBKMSG(VBACKUP$_OPENOUT, "the catalog spool", errno, strerror(errno)), STS$K_FATAL;

	/* The bases go into the SUMMARY, so they are worked out before anything is written */
	for ( unsigned i = 0; i < a_opts->ninput; i++ )
		vbk$base(a_opts->input [i], l_base [i], VBACKUP$K_SZ_PATH);

	if ( !(1 & s_vbk$summary(l_sav, &l_sum, l_base, a_opts->ninput)) )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM)), STS$K_FATAL;

	l_sav->wctx.volcb	= s_vbk$volcb;
	l_sav->wctx.volarg	= l_sav;

	if ( !(1 & vbk$wrt_open(&l_sav->wctx, a_opts->output, a_opts->bsize, a_opts->grpsz, a_opts->volsize,
				(a_opts->replace ? VBK$M_WRT_REPLACE : 0) | (a_opts->nopipe ? VBK$M_WRT_SYNC : 0), l_sum.buf, l_sum.len)) )
		return	$VBKMSG(VBACKUP$_OPENOUT, l_sav->wctx.volspec[0] ? l_sav->wctx.volspec : a_opts->output,
				l_sav->wctx.err, strerror(l_sav->wctx.err)), STS$K_FATAL;

	if ( !(1 & vbk$wrt_record(&l_sav->wctx, VBK$K_RT_SUMMARY, l_sum.buf, l_sum.len, NULL)) )
		s_vbk$wrterr(l_sav);

	vbk$pre_start(a_opts);

	for ( unsigned i = 0; !l_sav->failed && (i < a_opts->ninput); i++ )
		{
		l_status = vbk$walk(a_opts, a_opts->input [i], (uint16_t) i, l_base [i], VBACKUP$K_SZ_PATH, s_vbk$entry, l_sav);

		if ( l_status == STS$K_WARN )
			$VBKMSG(VBACKUP$_NOFILES, a_opts->input [i]);

		if ( l_status == STS$K_FATAL )
			break;
		}

	vbk$pre_stop(a_opts);

	/* QUIT to /CONFIRM still closes the saveset properly; a failed one is left as it is */
	if ( l_sav->failed )
		{
		vbk$wrt_abort(&l_sav->wctx);
		$VBKMSG(VBACKUP$_FATALSAVE, a_opts->output);

		return	STS$K_FATAL;
		}

	if ( !(1 & s_vbk$catalog(l_sav, &l_catloc)) )
		{
		vbk$wrt_abort(&l_sav->wctx);
		$VBKMSG(VBACKUP$_FATALSAVE, a_opts->output);

		return	STS$K_FATAL;
		}

	vbk$tlv_reset(&l_sav->rec);
	vbk$tlv_u64(&l_sav->rec, VBK$K_TAG_NFILES, l_sav->nfiles);
	vbk$tlv_u64(&l_sav->rec, VBK$K_TAG_NBYTES, l_sav->nbytes);
	vbk$tlv_u32(&l_sav->rec, VBK$K_TAG_NERRORS, l_sav->nerrors);

	if ( !(1 & vbk$wrt_record(&l_sav->wctx, VBK$K_RT_END, l_sav->rec.buf, l_sav->rec.len, NULL))
		|| !(1 & vbk$wrt_finish(&l_sav->wctx, &l_nblocks, &l_nvols)) )
		{
		s_vbk$wrterr(l_sav);
		vbk$wrt_abort(&l_sav->wctx);
		$VBKMSG(VBACKUP$_FATALSAVE, a_opts->output);

		return	STS$K_FATAL;
		}

	vbk$tlv_u64(&l_trl, VBK$K_TAG_NFILES, l_sav->nfiles);
	vbk$tlv_u64(&l_trl, VBK$K_TAG_NBYTES, l_sav->nbytes);
	vbk$tlv_u32(&l_trl, VBK$K_TAG_NERRORS, l_sav->nerrors);
	vbk$tlv_u64(&l_trl, VBK$K_TAG_NBLOCKS, l_nblocks);
	vbk$tlv_u32(&l_trl, VBK$K_TAG_CATVOL, l_catloc.vol);
	vbk$tlv_u64(&l_trl, VBK$K_TAG_CATBLK, l_catloc.blk);
	vbk$tlv_u32(&l_trl, VBK$K_TAG_CATOFF, l_catloc.off);
	vbk$tlv_u32(&l_trl, VBK$K_TAG_NVOLS, l_nvols);
	vbk$tlv_u64(&l_trl, VBK$K_TAG_NENTRIES, l_sav->nentries);

	if ( !(1 & vbk$wrt_close(&l_sav->wctx, l_trl.buf, l_trl.len)) )
		{
		s_vbk$wrterr(l_sav);
		$VBKMSG(VBACKUP$_FATALSAVE, a_opts->output);

		return	STS$K_FATAL;
		}

	if ( a_opts->log )
		{
		$VBKMSG(VBACKUP$_SAVESUMM, l_sav->nfiles, l_sav->nbytes, l_nblocks, l_nvols);

		if ( a_opts->timefilter )
			$VBKMSG(VBACKUP$_INCRSUMM, l_sav->npresent);
		}

	fclose(l_sav->spool);
	vbk$tlv_free(&l_sum);
	vbk$tlv_free(&l_trl);
	vbk$tlv_free(&l_sav->xbuf);
	vbk$tlv_free(&l_sav->cat);

	for ( unsigned i = 0; i < VBK$K_HLHASH; i++ )
		for ( VBK$HLINK *l_e = l_sav->hlink [i], *l_n; l_e; l_e = l_n )
			{
			l_n	= l_e->next;
			free(l_e);
			}

	free(l_sav->iobuf);
	free(l_base);

	/* /VERIFY: the saveset just written, read back and compared with the disk */
	if ( a_opts->verify && strcmp(a_opts->output, "-") )
		{
		$VBKMSG(VBACKUP$_VERIFYING, a_opts->output);

		l_status = vbk$compare(a_opts, a_opts->output);
		}
	else	l_status = STS$K_SUCCESS;

	/*
	**  /RECORD: the saveset and the state of its files into the journal -
	**  only when the saveset is complete and, under /VERIFY, agreed with
	**  the disk; otherwise the next /SINCE=BACKUP saves them all again.
	*/
	if ( a_opts->record && a_opts->jnl && (l_status == STS$K_SUCCESS) )
		{
		char	l_abs [VBACKUP$K_SZ_PATH];

		vbk$tlv_reset(&l_sav->rec);
		vbk$tlv_put(&l_sav->rec, VBK$K_TAG_SSUUID, VBK$K_UUIDSZ, l_sav->wctx.ssuuid);
		vbk$tlv_str(&l_sav->rec, VBK$K_TAG_SPEC, !strcmp(a_opts->output, "-") ? "(standard output)"
				: (realpath(a_opts->output, l_abs) ? l_abs : a_opts->output));
		vbk$tlv_time(&l_sav->rec, VBK$K_TAG_CREATED, &l_sav->created);
		vbk$tlv_u8(&l_sav->rec, VBK$K_TAG_KIND, a_opts->timefilter ? VBK$K_KIND_INCR : VBK$K_KIND_FULL);

		if ( a_opts->timefilter )
			vbk$tlv_str(&l_sav->rec, VBK$K_TAG_FILTER, a_opts->filter);

		vbk$tlv_u64(&l_sav->rec, VBK$K_TAG_NFILES, l_sav->nfiles);
		vbk$tlv_u64(&l_sav->rec, VBK$K_TAG_NBYTES, l_sav->nbytes);
		vbk$jnl_sset(a_opts->jnl, l_sav->rec.buf, l_sav->rec.len);

		for ( size_t i = 0; i < l_sav->record.sz; i++ )
			{
			VBK$FSTATE *	l_st = (VBK$FSTATE *) l_sav->record.ent [i].val;

			if ( !l_sav->record.ent [i].key )
				continue;

			l_st->recorded	= l_sav->created;
			memcpy(l_st->ssuuid, l_sav->wctx.ssuuid, VBK$K_UUIDSZ);
			vbk$jnl_put(a_opts->jnl, l_sav->record.ent [i].key, l_st);
			}

		if ( 1 & vbk$jnl_commit(a_opts->jnl) )
			$VBKMSG(VBACKUP$_RECORDED, (uint64_t) l_sav->record.cnt, a_opts->jnl->spec);
		}

	vbk$hash_free(&l_sav->record, 1);
	vbk$tlv_free(&l_sav->rec);
	free(l_sav);

	return	l_status;
}
