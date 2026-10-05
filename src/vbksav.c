#define	__MODULE__	"VBKSAV"
#define	__IDENT__	"X01-07"
#define	__REV__		"1.7.0"

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
**	X01-07		 5-OCT-2026	RRL
**		SAVESUMM always; SYSTEM made by FAO.
**
**	X01-06		 5-OCT-2026	RRL
**		/ENCRYPT: the passphrase, the SALT and the keys of the saveset;
**		a short SUMMARY in the clear for the VHDR, the crypto tags in
**		both SUMMARYs; the writer seals the blocks.
**
**	X01-04		 4-OCT-2026	RRL
**		/DATA_FORMAT=COMPRESSED: a DATAZ record where LZ4 pays, DATA
**		elsewhere; the SUMMARY says COMPRESS.  /PHYSICAL: a device
**		saved as one file, the runs of zeros left out (S_VBK$PHYSICAL).
**		/IMAGE: the identity of the volume in the SUMMARY.  /DELETE:
**		the files saved and verified, unchanged since, deleted.  The
**		compression on several cores (VBKZPL.C): the records through
**		its ring, a catalog entry waits there for its FILE record.
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
	uint8_t *	zbuf;			/* /DATA_FORMAT=COMPRESSED: a DATAZ record body	*/
	uint64_t	nzin, nzout;		/* ... octets in, octets out			*/
	uint64_t	physdata;		/* Octets of data written: /PHYSICAL, but zeros	*/
	struct vbk_zp_t *zp;			/* Compression on several cores, NULL - none	*/
	struct vbk_pcat_t *pend;		/* ... the catalog entry of the file in hand	*/
	VBK$LOC		loc;			/* Where the FILE record in hand went		*/
	VBK$HLINK *	hlink [VBK$K_HLHASH];
	uint32_t	fileno;
	uint64_t	nfiles, nbytes, nentries;
	uint32_t	nerrors;
	uint64_t	npresent;		/* Covered, not saved: PRESENT entries		*/
	VBK$HASH	record;			/* /RECORD: absolute name -> VBK$FSTATE		*/
	VBK$TIME	created;		/* Of the SUMMARY: the RECORDED of the journal	*/
	int		failed;			/* The saveset itself could not be written	*/

	int		crypt;			/* /ENCRYPT (format.md 6.10)			*/
	VBK$KEYS	keys;			/* ... the keys of this saveset			*/
	uint8_t		salt [VBK$K_SALTSZ];	/* ... its SALT					*/
	uint32_t	kdfiter;		/* ... its KDFITER				*/
} VBK$SAVE;


/*
**  The tags of an encrypted saveset: in its VHDR and in its SUMMARY
*/
static	int	s_vbk$crypttags	(
		VBK$SAVE *	a_sav,
		VBK$TLVB *	a_tlvb
			)
{
int	l_ok = 1;

	l_ok &= vbk$tlv_u8(a_tlvb, VBK$K_TAG_CIPHER, VBK$K_CIPHER_CC20HS);
	l_ok &= vbk$tlv_u8(a_tlvb, VBK$K_TAG_KDF, VBK$K_KDF_PBKDF2);
	l_ok &= vbk$tlv_u32(a_tlvb, VBK$K_TAG_KDFITER, a_sav->kdfiter);
	l_ok &= vbk$tlv_put(a_tlvb, VBK$K_TAG_SALT, VBK$K_SALTSZ, a_sav->salt);
	l_ok &= vbk$tlv_put(a_tlvb, VBK$K_TAG_KEYCHECK, VBK$K_KEYSZ, a_sav->keys.check);

	return	l_ok;
}


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
**  A catalog entry that waits in the ring of the compression for its FILE
**  record to be written - the place of it goes into the entry then
*/
typedef struct vbk_pcat_t
{
	VBK$SAVE *	sav;
	VBK$LOC		loc;			/* Filled when the FILE record is written	*/
	int		hasloc;
	uint32_t	locat;			/* Offset of the LOCVOL item in BUF		*/
	uint32_t	len;
	uint8_t *	buf;			/* The entry - apart: the ring holds &LOC	*/
} VBK$PCAT;


/*
**  Where the FILE record about to be written is to say it went: without
**  the ring at once, with it once the ring writes it
*/
static	VBK$LOC *	s_vbk$fileloc	(
		VBK$SAVE *	a_sav
			)
{
	if ( !a_sav->zp )
		return	&a_sav->loc;

	/* An entry never made (the file failed after its FILE record) is simply left to its own */
	if ( !(a_sav->pend = calloc(1, sizeof(VBK$PCAT))) )
		return	&a_sav->loc;

	return	&a_sav->pend->loc;
}


/*
**  A record other than DATA: through the ring when there is one, so that
**  the order of the stream is kept
*/
static	int	s_vbk$rec	(
		VBK$SAVE *	a_sav,
		uint16_t	a_type,
	const	void *		a_body,
		uint32_t	a_len,
		VBK$LOC *	a_loc
			)
{
	if ( a_sav->zp )
		return	vbk$zp_record(a_sav->zp, a_type, a_body, a_len, a_loc);

	return	vbk$wrt_record(&a_sav->wctx, a_type, a_body, a_len, a_loc);
}


/*
**  The ring has written the FILE record: the place into the entry, the
**  entry into the spool
*/
static	void	s_vbk$catcb	(
		void *		a_arg
			)
{
VBK$PCAT *	l_pc = (VBK$PCAT *) a_arg;
uint8_t		l_len [4];

	if ( l_pc->hasloc )
		{
		vbk$put32(l_pc->buf + l_pc->locat + 6, l_pc->loc.vol);
		vbk$put64(l_pc->buf + l_pc->locat + 6 + 4 + 6, l_pc->loc.blk);
		vbk$put32(l_pc->buf + l_pc->locat + 6 + 4 + 6 + 8 + 6, l_pc->loc.off);
		}

	vbk$put32(l_len, l_pc->len);

	if ( (1 != fwrite(l_len, sizeof(l_len), 1, l_pc->sav->spool)) || (1 != fwrite(l_pc->buf, l_pc->len, 1, l_pc->sav->spool)) )
		{
		$VBKMSG(VBACKUP$_WRITERR, "the catalog spool", errno, strerror(errno));
		l_pc->sav->failed = 1;
		}

	free(l_pc->buf);
	free(l_pc);
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
uint32_t	l_locat = 0;
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

	/* A device, not a file called so */
	if ( a_sav->opts->physical )
		l_ok &= vbk$tlv_u8(l_c, VBK$K_TAG_PHYSICAL, 1);

	/* A PRESENT entry has no records in the stream: nothing to point at, nothing summed */
	if ( a_status != VBK$K_FS_PRESENT )
		{
		l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_CRC, a_crc);
		l_locat	= l_c->len;
		l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_LOCVOL, a_loc->vol);
		l_ok &= vbk$tlv_u64(l_c, VBK$K_TAG_LOCBLK, a_loc->blk);
		l_ok &= vbk$tlv_u32(l_c, VBK$K_TAG_LOCOFF, a_loc->off);
		}

	if ( !l_ok )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));

	/* The ring: the entry waits for its FILE record, the place is put in when that is written */
	if ( a_sav->zp )
		{
		VBK$PCAT *	l_pc = a_sav->pend;

		/* The entry of the FILE record just handed over - or one of its own (PRESENT: no record) */
		if ( l_pc && (a_loc == &l_pc->loc) )
			a_sav->pend = NULL;
		else if ( !(l_pc = calloc(1, sizeof(VBK$PCAT))) )
			return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));
		else if ( a_status != VBK$K_FS_PRESENT )
			l_pc->loc = *a_loc;

		if ( !(l_pc->buf = malloc(l_c->len ? l_c->len : 1)) )
			{
			free(l_pc);

			return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));
			}

		l_pc->sav	= a_sav;
		l_pc->hasloc	= (a_status != VBK$K_FS_PRESENT);
		l_pc->locat	= l_locat;
		l_pc->len	= l_c->len;
		memcpy(l_pc->buf, l_c->buf, l_c->len);

		a_sav->nentries++;

		return	(1 & vbk$zp_call(a_sav->zp, s_vbk$catcb, l_pc)) ? STS$K_SUCCESS : STS$K_ERROR;
		}

	vbk$put32(l_len, l_c->len);

	if ( (1 != fwrite(l_len, sizeof(l_len), 1, a_sav->spool)) || (1 != fwrite(l_c->buf, l_c->len, 1, a_sav->spool)) )
		return	$VBKMSG(VBACKUP$_WRITERR, "the catalog spool", errno, strerror(errno));

	a_sav->nentries++;

	return	STS$K_SUCCESS;
}


/*
**  One DATA or DATAZ record: compressed when it pays - by more than the
**  4 octets the DATAZ header costs over DATA - else stored as it is.  A
**  saveset so mixes both kinds, and a reader takes either.
*/
static	int	s_vbk$put	(
		VBK$SAVE *	a_sav,
		uint32_t	a_fileno,
		uint64_t	a_off,
	const	uint8_t *	a_data,
		uint32_t	a_n
			)
{
uint8_t		l_hdr [VBK$K_DATAHDR];
uint32_t	l_zlen = 0;

	a_sav->nzin += a_n;

	/* Several cores: the compression goes to the workers, the records come out in order all the same */
	if ( a_sav->zp )
		return	vbk$zp_data(a_sav->zp, a_fileno, a_off, a_data, a_n);

	if ( a_sav->zbuf && (1 & vbk$lz4_pack(a_data, a_n, a_sav->zbuf + VBK$K_DATAZHDR, VBK$LZ4_BOUND(a_n), &l_zlen)) )
		{
		vbk$put32(a_sav->zbuf, a_fileno);
		vbk$put32(a_sav->zbuf + 4, VBK$K_CODEC_LZ4);
		vbk$put64(a_sav->zbuf + 8, a_off);
		vbk$put32(a_sav->zbuf + 16, a_n);

		a_sav->nzout += VBK$K_DATAZHDR + l_zlen;

		return	vbk$wrt_record(&a_sav->wctx, VBK$K_RT_DATAZ, a_sav->zbuf, VBK$K_DATAZHDR + l_zlen, NULL);
		}

	vbk$put32(l_hdr, a_fileno);
	vbk$put32(l_hdr + 4, 0);
	vbk$put64(l_hdr + 8, a_off);

	a_sav->nzout += VBK$K_DATAHDR + (uint64_t) a_n;

	if ( !(1 & vbk$wrt_rechdr(&a_sav->wctx, VBK$K_RT_DATA, VBK$K_DATAHDR + a_n, NULL)) || !(1 & vbk$wrt_bytes(&a_sav->wctx, l_hdr, sizeof(l_hdr))) )
		return	STS$K_ERROR;

	return	vbk$wrt_bytes(&a_sav->wctx, a_data, a_n);
}


/*
**  /PHYSICAL looks for zeros a piece at a time: 64 KB, the last one shorter
*/
#define	VBK$K_PHYPIECE	65536

static	size_t	s_vbk$piece	(
		size_t		a_at,
		size_t		a_len
			)
{
	return	((a_len - a_at) < VBK$K_PHYPIECE) ? (a_len - a_at) : VBK$K_PHYPIECE;
}

static	int	s_vbk$zeros	(
	const	uint8_t *	a_p,
		size_t		a_len
			)
{
static	const uint8_t	l_zero [VBK$K_PHYPIECE];

	return	!memcmp(a_p, l_zero, a_len);
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
uint8_t *	l_data = a_sav->iobuf + VBK$K_DATAHDR;
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

			/*
			**  /PHYSICAL: the runs of zeros - 64 KB at a time - are left
			**  out, holes as in a sparse file; the checksum covers what
			**  is written, as it does for a sparse file
			*/
			for ( size_t l_s = 0, l_e; l_s < (size_t) l_n; l_s = l_e )
				{
				l_e	= (size_t) l_n;

				if ( a_sav->opts->physical )
					{
					while ( (l_s < (size_t) l_n) && s_vbk$zeros(l_data + l_s, s_vbk$piece(l_s, (size_t) l_n)) )
						l_s += s_vbk$piece(l_s, (size_t) l_n);

					for ( l_e = l_s; (l_e < (size_t) l_n) && !s_vbk$zeros(l_data + l_e, s_vbk$piece(l_e, (size_t) l_n)); )
						l_e += s_vbk$piece(l_e, (size_t) l_n);

					if ( l_s >= l_e )
						break;
					}

				if ( !(1 & s_vbk$put(a_sav, a_fileno, (uint64_t) l_off + l_s, l_data + l_s, (uint32_t) (l_e - l_s))) )
					return	s_vbk$wrterr(a_sav);

				l_crc	= $VBK_CRC(l_crc, l_data + l_s, l_e - l_s);
				a_sav->physdata += (uint64_t) (l_e - l_s);
				}

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
**	/PHYSICAL: the device opened by VBK$PHY_OPEN saved as one regular
**	file (format.md, 6.8) - its size the size of the device, its data
**	at their offsets, the runs of zeros left out.
**
**  FORMAL PARAMETERS:
**
**	a_sav		The save; OPTS->PHYSFD is the device
**	a_spec		Its specification
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_FATAL - the saveset could not be written.
**--
*/
static	int	s_vbk$physical	(
		VBK$SAVE *	a_sav,
	const	char *		a_spec
			)
{
VBK$OPTS *	l_o = a_sav->opts;
VBK$ATTR	l_attr;
VBK$LOC *	l_ploc;
struct stat	l_st;
const char *	l_name = strrchr(a_spec, '/') ? (strrchr(a_spec, '/') + 1) : a_spec;
uint64_t	l_saved = 0, l_now = l_o->physsize;
uint32_t	l_crc = 0;
uint8_t		l_fstat = VBK$K_FS_OK;
int		l_fd = l_o->physfd, l_status;

	if ( !(1 & vbk$atr_get(a_spec, l_fd, 0, &l_attr, &a_sav->xbuf)) )
		memset(&l_attr, 0, sizeof(l_attr));

	/* The node of the device gives the owner, the mode, the times; the rest is that of a file */
	l_attr.ftype	= VBK$K_FT_REG;
	l_attr.size	= l_o->physsize;
	l_attr.rdev	= 0;
	l_attr.nlink	= 1;
	l_attr.hasflags	= 0;
	l_attr.fsflags	= 0;
	l_attr.link	= NULL;
	l_attr.linklen	= 0;
	l_attr.xattr	= NULL;
	l_attr.xattrlen	= 0;
	l_attr.fileno	= ++a_sav->fileno;
	l_attr.path	= l_name;
	l_attr.pathlen	= (uint32_t) strlen(l_name);
	l_attr.baseidx	= 0;

	vbk$tlv_reset(&a_sav->rec);

	if ( !(1 & vbk$atr_tlv(&l_attr, &a_sav->rec)) || !(1 & vbk$tlv_u8(&a_sav->rec, VBK$K_TAG_PHYSICAL, 1)) )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM)), STS$K_FATAL;

	l_ploc	= s_vbk$fileloc(a_sav);

	if ( !(1 & s_vbk$rec(a_sav, VBK$K_RT_FILE, a_sav->rec.buf, a_sav->rec.len, l_ploc)) )
		return	s_vbk$wrterr(a_sav);

	l_status = s_vbk$data(a_sav, l_fd, l_attr.fileno, a_spec, l_o->physsize, &l_crc, &l_saved);

	if ( l_status == STS$K_FATAL )
		return	STS$K_FATAL;

	if ( l_status == STS$K_ERROR )
		l_fstat	= VBK$K_FS_READERR;

	/* Removable media: the size must be what it was - else the copy is a mix */
	if ( !fstat(l_fd, &l_st) )
		{
		if ( !S_ISBLK(l_st.st_mode) )
			l_now	= (uint64_t) l_st.st_size;
		else if ( ioctl(l_fd, BLKGETSIZE64, &l_now) )
			l_now	= l_o->physsize;

		if ( l_now != l_o->physsize )
			{
			$VBKMSG(VBACKUP$_PHYSSIZE, a_spec, l_o->physsize, l_now);
			l_fstat	= VBK$K_FS_CHANGED;
			}
		}

	close(l_fd);
	l_o->physfd	= -1;

	vbk$tlv_reset(&a_sav->rec);
	vbk$tlv_u32(&a_sav->rec, VBK$K_TAG_FILENO, l_attr.fileno);
	vbk$tlv_u64(&a_sav->rec, VBK$K_TAG_SIZE, l_saved);
	vbk$tlv_u32(&a_sav->rec, VBK$K_TAG_CRC, l_crc);
	vbk$tlv_u8(&a_sav->rec, VBK$K_TAG_STATUS, l_fstat);

	if ( !(1 & s_vbk$rec(a_sav, VBK$K_RT_FEND, a_sav->rec.buf, a_sav->rec.len, NULL)) )
		return	s_vbk$wrterr(a_sav);

	if ( l_fstat != VBK$K_FS_OK )
		a_sav->nerrors++;

	a_sav->nfiles++;
	l_attr.size	= l_saved;

	if ( !(1 & s_vbk$catent(a_sav, &l_attr, l_ploc, l_crc, l_fstat)) )
		return	STS$K_FATAL;

	$VBKMSG(VBACKUP$_PHYSSUMM, a_spec, l_o->physsize, a_sav->physdata);

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
VBK$LOC		l_loc = {0}, *l_ploc = &l_loc;
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

	l_ploc	= s_vbk$fileloc(l_sav);

	if ( !(1 & s_vbk$rec(l_sav, VBK$K_RT_FILE, l_sav->rec.buf, l_sav->rec.len, l_ploc)) )
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

	if ( !(1 & s_vbk$rec(l_sav, VBK$K_RT_FEND, l_sav->rec.buf, l_sav->rec.len, NULL)) )
		return	s_vbk$wrterr(l_sav);

	if ( l_fstat != VBK$K_FS_OK )
		l_sav->nerrors++;

	l_sav->nfiles++;

	if ( l_attr.ftype == VBK$K_FT_REG )
		l_attr.size	= l_saved;

	if ( !(1 & s_vbk$catent(l_sav, &l_attr, l_ploc, l_crc, l_fstat)) )
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
	$VBKFAOB(l_sys, sizeof(l_sys), "!AZ !AZ !AZ", l_uts.sysname, l_uts.release, l_uts.machine);

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

	/* Information only: a reader goes by the record types, not by this */
	if ( l_o->compress )
		l_ok &= vbk$tlv_u8(a_tlvb, VBK$K_TAG_COMPRESS, VBK$K_CODEC_LZ4);

	/* /IMAGE: what makes the volume that volume - a restore makes it again */
	if ( l_o->image )
		{
		l_ok &= vbk$tlv_u8(a_tlvb, VBK$K_TAG_IMAGE, 1);
		l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_FSTYPE, l_o->imgfstype);

		if ( l_o->imglabel [0] )
			l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_FSLABEL, l_o->imglabel);

		if ( l_o->imguuid [0] )
			l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_FSUUID, l_o->imguuid);

		l_ok &= vbk$tlv_u64(a_tlvb, VBK$K_TAG_FSUSED, l_o->imgused);
		l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_MOUNTOPTS, l_o->imgopts);

		if ( l_o->imgroot.len )
			l_ok &= vbk$tlv_put(a_tlvb, VBK$K_TAG_ROOTATTR, l_o->imgroot.len, l_o->imgroot.buf);
		}

	/* /PHYSICAL: what a restore onto a device must know, and a file cannot say */
	if ( l_o->physical )
		{
		l_ok &= vbk$tlv_u8(a_tlvb, VBK$K_TAG_PHYSICAL, 1);
		l_ok &= vbk$tlv_u64(a_tlvb, VBK$K_TAG_DEVSIZE, l_o->physsize);
		l_ok &= vbk$tlv_u32(a_tlvb, VBK$K_TAG_SECTORSIZE, l_o->physsector);
		}
	l_ok &= vbk$tlv_u64(a_tlvb, VBK$K_TAG_VOLSIZE, l_o->volsize);

	if ( l_o->comment [0] )
		l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_COMMENT, l_o->comment);

	l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_SYSTEM, l_sys);

	if ( a_sav->crypt )
		l_ok &= s_vbk$crypttags(a_sav, a_tlvb);

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
**	/DELETE: the files of the saveset deleted from the disk, once it
**	has been written and verified.  Only the entries saved whole
**	(STATUS OK), only regular files and links - directories stay - and
**	only a file that is still the one saved: the same inode, size,
**	modification and change time as its catalog entry.  A file changed
**	since is kept and said so.  /CONFIRM asks for each.
**
**  FORMAL PARAMETERS:
**
**	a_sav		The save: its catalog spool
**	a_base		The bases of the inputs
**	a_nbase		How many
**
**  RETURN VALUE:
**	None; DELSUMM says what was done.
**--
*/
static	void	s_vbk$delete	(
		VBK$SAVE *	a_sav,
		char		a_base [][VBACKUP$K_SZ_PATH],
		unsigned	a_nbase
			)
{
VBK$OPTS *	l_o = a_sav->opts;
VBK$ATTR	l_attr;
struct stat	l_st;
uint8_t		l_len [4], *l_buf = NULL;
uint32_t	l_elen, l_bufsz = 0;
uint64_t	l_ndel = 0, l_nkept = 0;
char		l_path [VBACKUP$K_SZ_PATH];
int		l_status;

	rewind(a_sav->spool);

	while ( 1 == fread(l_len, sizeof(l_len), 1, a_sav->spool) )
		{
		l_elen	= vbk$get32(l_len);

		if ( l_elen > l_bufsz )
			{
			uint8_t *	l_p = realloc(l_buf, l_elen);

			if ( !l_p )
				break;

			l_buf	= l_p;
			l_bufsz	= l_elen;
			}

		if ( (1 != fread(l_buf, l_elen, 1, a_sav->spool)) || !(1 & vbk$atr_parse(l_buf, l_elen, &l_attr)) )
			break;

		if ( (l_attr.status != VBK$K_FS_OK) || ((l_attr.ftype != VBK$K_FT_REG) && (l_attr.ftype != VBK$K_FT_SYMLINK) && (l_attr.ftype != VBK$K_FT_HARDLINK)) )
			continue;

		if ( (l_attr.baseidx >= a_nbase) || !(1 & vbk$mkpath(a_base [l_attr.baseidx], l_attr.path, l_attr.pathlen, l_path, sizeof(l_path))) )
			continue;

		if ( lstat(l_path, &l_st) )
			continue;

		/* Still the file that was saved?  The inode, and - but for a further name - its size and times */
		if ( (l_attr.hasdevino && (((uint64_t) l_st.st_ino != l_attr.ino) || ((uint64_t) l_st.st_dev != l_attr.dev)))
			|| ((l_attr.ftype != VBK$K_FT_HARDLINK) && (((uint64_t) l_st.st_size != l_attr.size)
			|| (l_st.st_mtim.tv_sec != l_attr.mtime.sec) || ((uint32_t) l_st.st_mtim.tv_nsec != l_attr.mtime.nsec)
			|| (l_attr.hasctime && ((l_st.st_ctim.tv_sec != l_attr.ctime.sec) || ((uint32_t) l_st.st_ctim.tv_nsec != l_attr.ctime.nsec))))) )
			{
			$VBKMSG(VBACKUP$_SRCKEPT, l_path, "it changed after it was saved");
			l_nkept++;
			continue;
			}

		if ( l_o->confirm )
			{
			if ( STS$K_FATAL == (l_status = vbk$confirm("Delete", l_path)) )
				break;

			if ( !(1 & l_status) )
				{
				l_nkept++;
				continue;
				}
			}

		if ( unlink(l_path) )
			{
			$VBKMSG(VBACKUP$_SRCKEPT, l_path, strerror(errno));
			l_nkept++;
			continue;
			}

		l_ndel++;

		if ( l_o->log )
			$VBKMSG(VBACKUP$_SRCDELETED, l_path);
		}

	free(l_buf);

	$VBKMSG(VBACKUP$_DELSUMM, l_ndel, l_nkept);
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
int		l_verified = 0;
VBK$SAVE *	l_sav;
VBK$TLVB	l_sum = {0}, l_trl = {0}, l_vhdr = {0};
VBK$LOC		l_catloc = {0};
char		(*l_base) [VBACKUP$K_SZ_PATH];
uint64_t	l_nblocks;
uint32_t	l_nvols;
int		l_status = STS$K_SUCCESS;

	if ( !(l_sav = calloc(1, sizeof(*l_sav))) || !(l_base = calloc(a_opts->ninput, VBACKUP$K_SZ_PATH))
		|| !(l_sav->iobuf = malloc(VBK$K_DATAHDR + VBACKUP$K_IOBUF))
		|| (a_opts->compress && !(l_sav->zbuf = malloc(VBK$K_DATAZHDR + VBK$LZ4_BOUND(VBACKUP$K_IOBUF)))) )
		return	$VBKMSG(VBACKUP$_NOMEM, errno, strerror(errno)), STS$K_FATAL;

	l_sav->opts	= a_opts;

	if ( !(l_sav->spool = tmpfile()) )
		return	$VBKMSG(VBACKUP$_OPENOUT, "the catalog spool", errno, strerror(errno)), STS$K_FATAL;

	/* The bases go into the SUMMARY, so they are worked out before anything is written */
	for ( unsigned i = 0; i < a_opts->ninput; i++ )
		vbk$base(a_opts->input [i], l_base [i], VBACKUP$K_SZ_PATH);

	/* /PHYSICAL: the device is checked and opened first - its size goes into the SUMMARY too */
	if ( a_opts->physical && !(1 & vbk$phy_open(a_opts, a_opts->input [0])) )
		return	STS$K_FATAL;

	/*
	**  /ENCRYPT: the passphrase - asked twice - the SALT of this saveset,
	**  the keys.  The VHDR carries a short SUMMARY in the clear: nothing
	**  in it names the system or the files.
	*/
	if ( a_opts->encrypt )
		{
		const char *	l_pass;
		size_t		l_plen;

		if ( !(1 & vbk$key_get(a_opts, a_opts->output, 1, &l_pass, &l_plen)) )
			return	STS$K_FATAL;

		if ( !(1 & vbk$os_random(l_sav->salt, sizeof(l_sav->salt))) )
			return	$VBKMSG(VBACKUP$_OPENOUT, "getrandom", errno, strerror(errno)), STS$K_FATAL;

		l_sav->kdfiter	= vbk$key_iter();
		vbk$crp_derive(&l_sav->keys, l_pass, l_plen, l_sav->salt, l_sav->kdfiter);
		l_sav->crypt	= 1;
		l_sav->wctx.keys = &l_sav->keys;
		}

	if ( !(1 & s_vbk$summary(l_sav, &l_sum, l_base, a_opts->ninput)) )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM)), STS$K_FATAL;

	if ( l_sav->crypt )
		{
		int	l_ok = 1;

		l_ok &= vbk$tlv_str(&l_vhdr, VBK$K_TAG_PRODUCT, "VBACKUP " VBACKUP_K_IDENT);
		l_ok &= vbk$tlv_u32(&l_vhdr, VBK$K_TAG_BLOCKSIZE, a_opts->bsize);
		l_ok &= vbk$tlv_u32(&l_vhdr, VBK$K_TAG_GROUPSIZE, a_opts->grpsz);
		l_ok &= vbk$tlv_u64(&l_vhdr, VBK$K_TAG_VOLSIZE, a_opts->volsize);
		l_ok &= s_vbk$crypttags(l_sav, &l_vhdr);

		if ( !l_ok )
			return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM)), STS$K_FATAL;
		}

	l_sav->wctx.volcb	= s_vbk$volcb;
	l_sav->wctx.volarg	= l_sav;

	if ( !(1 & vbk$wrt_open(&l_sav->wctx, a_opts->output, a_opts->bsize, a_opts->grpsz, a_opts->volsize,
				(a_opts->replace ? VBK$M_WRT_REPLACE : 0) | (a_opts->nopipe ? VBK$M_WRT_SYNC : 0),
				l_sav->crypt ? l_vhdr.buf : l_sum.buf, l_sav->crypt ? l_vhdr.len : l_sum.len)) )
		return	$VBKMSG(VBACKUP$_OPENOUT, l_sav->wctx.volspec[0] ? l_sav->wctx.volspec : a_opts->output,
				l_sav->wctx.err, strerror(l_sav->wctx.err)), STS$K_FATAL;

	if ( !(1 & vbk$wrt_record(&l_sav->wctx, VBK$K_RT_SUMMARY, l_sum.buf, l_sum.len, NULL)) )
		s_vbk$wrterr(l_sav);

	if ( l_sav->crypt && a_opts->log )
		$VBKMSG(VBACKUP$_ENCRYPTED, a_opts->output, l_sav->kdfiter);

	/* Compression on several cores: the ring between the reading of the files and the writer */
	if ( a_opts->compress )
		l_sav->zp = vbk$zp_start(&l_sav->wctx);

	/* A device is not a tree: no walk, no read-ahead - one file, block by block */
	if ( a_opts->physical )
		{
		if ( !l_sav->failed )
			s_vbk$physical(l_sav, a_opts->input [0]);
		}
	else	vbk$pre_start(a_opts);

	for ( unsigned i = 0; !a_opts->physical && !l_sav->failed && (i < a_opts->ninput); i++ )
		{
		l_status = vbk$walk(a_opts, a_opts->input [i], (uint16_t) i, l_base [i], VBACKUP$K_SZ_PATH, s_vbk$entry, l_sav);

		if ( l_status == STS$K_WARN )
			$VBKMSG(VBACKUP$_NOFILES, a_opts->input [i]);

		if ( l_status == STS$K_FATAL )
			break;
		}

	vbk$pre_stop(a_opts);

	/* The ring empty - everything written, every catalog entry spooled - before the catalog */
	if ( l_sav->zp )
		{
		if ( !l_sav->failed && !(1 & vbk$zp_flush(l_sav->zp)) )
			s_vbk$wrterr(l_sav);

		vbk$zp_stop(l_sav->zp, &l_sav->nzin, &l_sav->nzout);
		l_sav->zp = NULL;
		}

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

	/* Always: a command says what it did, not only under /LOG */
	$VBKMSG(VBACKUP$_SAVESUMM, l_sav->nfiles, l_sav->nbytes, l_nblocks, l_nvols);

	if ( a_opts->timefilter )
		$VBKMSG(VBACKUP$_INCRSUMM, l_sav->npresent);

	vbk$tlv_free(&l_sum);
	vbk$tlv_free(&l_trl);
	vbk$tlv_free(&l_vhdr);
	vbk$crp_wipe(&l_sav->keys, sizeof(l_sav->keys));
	vbk$tlv_free(&l_sav->xbuf);
	vbk$tlv_free(&l_sav->cat);

	for ( unsigned i = 0; i < VBK$K_HLHASH; i++ )
		for ( VBK$HLINK *l_e = l_sav->hlink [i], *l_n; l_e; l_e = l_n )
			{
			l_n	= l_e->next;
			free(l_e);
			}

	free(l_sav->iobuf);
	free(l_sav->zbuf);

	/* /VERIFY: the saveset just written, read back and compared with the disk */
	if ( a_opts->verify && strcmp(a_opts->output, "-") )
		{
		$VBKMSG(VBACKUP$_VERIFYING, a_opts->output);

		l_status = vbk$compare(a_opts, a_opts->output);
		l_verified = (l_status == STS$K_SUCCESS);
		}
	else	l_status = STS$K_SUCCESS;

	/* /DELETE: only what a verify that found nothing has seen - and nothing that changed since */
	if ( a_opts->delete )
		{
		if ( l_verified )
			s_vbk$delete(l_sav, l_base, a_opts->ninput);
		else	$VBKMSG(VBACKUP$_SRCKEPT, "every file", strcmp(a_opts->output, "-") ? "the saveset did not verify" : "a saveset on the standard output cannot be verified");
		}

	fclose(l_sav->spool);
	free(l_base);

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
