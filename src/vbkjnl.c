#define	__MODULE__	"VBKJNL"
#define	__IDENT__	"X01-03"
#define	__REV__		"1.3.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkjnl.c
**
**  ABSTRACT:	The journal of /RECORD (format.md, section 9): which
**		savesets were made, and the state every file had when it was
**		saved, so that /SINCE=BACKUP can tell what has changed since.
**		Also the listing of the journal and its rebuild from catalogs.
**
**  DESCRIPTION: OpenVMS keeps the date of the last backup in the header
**		of every file; Linux has no such place, and writing one - an
**		xattr - would itself change the ctime of the file.  So the
**		state is kept here, keyed by the absolute name of the file.
**
**		The file is read whole and rewritten whole: <journal>.tmp,
**		fsync, rename.  A writer holds an exclusive flock on
**		<journal>.lock from the read to the rename, so two saves
**		recording at once take their turns rather than lose an update.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-03		 3-OCT-2026	RRL
**		The FSTATE records are written in the order of their names.
**
**	X01-02		 3-OCT-2026	RRL
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
#include	<libgen.h>
#include	<sys/file.h>
#include	<sys/stat.h>

#include	"vbkdef.h"

#define	VBK$K_JNLDIR_ROOT	"/var/lib/vbackup"
#define	VBK$K_JNLDIR_USER	".vbackup"
#define	VBK$K_JNLNAME		"vbackup.jnl"


/*
**  Where the journal is: /JOURNAL=file, or the default of the user
*/
static	int	s_vbk$jnlspec	(
	const	char *		a_spec,
		char *		a_out,
		size_t		a_outsz
			)
{
const char *	l_home;

	if ( a_spec && *a_spec )
		return	vbk$strcpy(a_outsz, a_out, a_spec);

	if ( !geteuid() )
		snprintf(a_out, a_outsz, "%s/%s", VBK$K_JNLDIR_ROOT, VBK$K_JNLNAME);
	else if ( (l_home = getenv("HOME")) && *l_home )
		snprintf(a_out, a_outsz, "%s/%s/%s", l_home, VBK$K_JNLDIR_USER, VBK$K_JNLNAME);
	else	return	STS$K_ERROR;

	return	STS$K_SUCCESS;
}


/*
**  The directory of the journal, made when it is not there
*/
static	int	s_vbk$jnldir	(
	const	char *		a_spec
			)
{
char	l_dir [VBACKUP$K_SZ_PATH];

	vbk$strcpy(sizeof(l_dir), l_dir, a_spec);

	for ( char *l_p = l_dir + 1; *l_p; l_p++ )
		{
		if ( *l_p != '/' )
			continue;

		*l_p	= '\0';

		if ( mkdir(l_dir, 0700) && (errno != EEXIST) )
			return	STS$K_ERROR;

		*l_p	= '/';
		}

	return	STS$K_SUCCESS;
}


/*
**  Decode one FSTATE record into the table
*/
static	int	s_vbk$fstate	(
		VBK$JNL *	a_jnl,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
VBK$FSTATE	l_st = {0};
char		l_path [VBACKUP$K_SZ_PATH] = {0};
uint32_t	l_pos = 0, l_vlen;
uint16_t	l_tag;
const uint8_t *	l_val;

	while ( 1 & vbk$tlv_next(a_body, a_len, &l_pos, &l_tag, &l_vlen, &l_val) )
		{
		switch ( l_tag )
			{
			case	VBK$K_TAG_PATH:
				if ( l_vlen < sizeof(l_path) )
					{
					memcpy(l_path, l_val, l_vlen);
					l_path [l_vlen] = '\0';
					}
				break;

			case	VBK$K_TAG_DEVINO:
				if ( l_vlen == 16 )
					{
					l_st.dev	= vbk$get64(l_val);
					l_st.ino	= vbk$get64(l_val + 8);
					}
				break;

			case	VBK$K_TAG_SSUUID:
				if ( l_vlen == VBK$K_UUIDSZ )
					memcpy(l_st.ssuuid, l_val, VBK$K_UUIDSZ);
				break;

			case	VBK$K_TAG_CTIME:	vbk$tlv_gettime(l_vlen, l_val, &l_st.ctime);		break;
			case	VBK$K_TAG_MTIME:	vbk$tlv_gettime(l_vlen, l_val, &l_st.mtime);		break;
			case	VBK$K_TAG_RECORDED:	vbk$tlv_gettime(l_vlen, l_val, &l_st.recorded);		break;
			case	VBK$K_TAG_SIZE:		l_st.size  = vbk$tlv_getu(l_vlen, l_val);		break;
			case	VBK$K_TAG_FTYPE:	l_st.ftype = (uint8_t) vbk$tlv_getu(l_vlen, l_val);	break;
			}
		}

	if ( !l_path [0] )
		return	STS$K_ERROR;

	return	vbk$jnl_put(a_jnl, l_path, &l_st);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Open the journal and read it in.  A journal that is not there yet
**	is an empty one.  For a writer the directory is made, and the lock
**	is taken and held up to VBK$JNL_CLOSE.
**
**  FORMAL PARAMETERS:
**
**	a_jnl		The journal, zeroed by the caller beforehand
**	a_spec		/JOURNAL=file, "" or NULL - the default one
**	a_write		Non-zero - it is to be written
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- read, or empty;
**	STS$K_ERROR	- it cannot be opened, locked or read, or it is no
**			  journal, or it is damaged - reported.
**--
*/
int	vbk$jnl_open	(
		VBK$JNL *	a_jnl,
	const	char *		a_spec,
		int		a_write
			)
{
char		l_lock [VBACKUP$K_SZ_PATH + 8];
uint8_t *	l_buf = NULL;
struct stat	l_st;
uint64_t	l_nrec;
size_t		l_off;
int		l_fd;

	a_jnl->lockfd	= -1;

	if ( !(1 & s_vbk$jnlspec(a_spec, a_jnl->spec, sizeof(a_jnl->spec))) )
		return	$VBKMSG(VBACKUP$_JNLERR, "(none)", "no journal given, and no home directory", EINVAL, strerror(EINVAL));

	if ( a_write )
		{
		if ( !(1 & s_vbk$jnldir(a_jnl->spec)) )
			return	$VBKMSG(VBACKUP$_JNLERR, a_jnl->spec, "cannot make its directory", errno, strerror(errno));

		snprintf(l_lock, sizeof(l_lock), "%s.lock", a_jnl->spec);

		if ( 0 > (a_jnl->lockfd = open(l_lock, O_RDWR | O_CREAT | O_CLOEXEC, 0600)) )
			return	$VBKMSG(VBACKUP$_JNLERR, l_lock, "cannot be opened", errno, strerror(errno));

		if ( flock(a_jnl->lockfd, LOCK_EX) )
			return	$VBKMSG(VBACKUP$_JNLERR, l_lock, "cannot be locked", errno, strerror(errno));
		}

	if ( 0 > (l_fd = open(a_jnl->spec, O_RDONLY | O_CLOEXEC)) )
		{
		if ( errno == ENOENT )
			return	STS$K_SUCCESS;

		return	$VBKMSG(VBACKUP$_JNLERR, a_jnl->spec, "cannot be opened", errno, strerror(errno));
		}

	a_jnl->exists	= 1;

	if ( fstat(l_fd, &l_st) || (l_st.st_size < (VBK$K_JNLHDR + 4)) || !(l_buf = malloc((size_t) l_st.st_size))
		|| (read(l_fd, l_buf, (size_t) l_st.st_size) != l_st.st_size) )
		{
		close(l_fd);
		free(l_buf);

		return	$VBKMSG(VBACKUP$_JNLERR, a_jnl->spec, "cannot be read, or is too short", errno, strerror(errno));
		}

	close(l_fd);

	if ( memcmp(l_buf, vbk$t_jmagic, sizeof(vbk$t_jmagic)) || (vbk$get16(l_buf + 4) != 1)
		|| ($VBK_CRC(0, l_buf, (size_t) l_st.st_size - 4) != vbk$get32(l_buf + l_st.st_size - 4)) )
		{
		free(l_buf);

		return	$VBKMSG(VBACKUP$_JNLERR, a_jnl->spec, "is no journal, or is damaged", EINVAL, strerror(EINVAL));
		}

	l_nrec	= vbk$get64(l_buf + 8);
	l_off	= VBK$K_JNLHDR;

	for ( uint64_t i = 0; i < l_nrec; i++ )
		{
		uint16_t	l_type;
		uint32_t	l_len;

		if ( (l_off + VBK$K_RECHDR) > ((size_t) l_st.st_size - 4) )
			break;

		l_type	= vbk$get16(l_buf + l_off);
		l_len	= vbk$get32(l_buf + l_off + 4);
		l_off	+= VBK$K_RECHDR;

		if ( (l_off + l_len) > ((size_t) l_st.st_size - 4) )
			break;

		if ( l_type == VBK$K_RT_SSET )
			vbk$jnl_sset(a_jnl, l_buf + l_off, l_len);
		else if ( l_type == VBK$K_RT_FSTATE )
			s_vbk$fstate(a_jnl, l_buf + l_off, l_len);

		l_off	+= l_len;
		}

	free(l_buf);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Tell /SINCE=BACKUP whether a file is to be saved: it is, when the
**	journal does not know it, or when its inode, ctime, mtime or size
**	differ from what was recorded (format.md, 9, rule 4).  The device is
**	not compared: btrfs and NFS give another one at every mount.
**
**  FORMAL PARAMETERS:
**
**	a_jnl		The journal
**	a_abspath	The absolute name of the file
**	a_stx		Its attributes now
**
**  RETURN VALUE:
**	1 - save it; 0 - it has not changed.
**--
*/
int	vbk$jnl_changed	(
	const	VBK$JNL *	a_jnl,
	const	char *		a_abspath,
	const	struct statx *	a_stx
			)
{
const VBK$FSTATE *l_st = (const VBK$FSTATE *) vbk$hash_get(&a_jnl->files, a_abspath);

	if ( !l_st )
		return	1;

	return	(l_st->ino != a_stx->stx_ino) || (l_st->size != a_stx->stx_size)
		|| (l_st->ctime.sec != a_stx->stx_ctime.tv_sec) || (l_st->ctime.nsec != a_stx->stx_ctime.tv_nsec)
		|| (l_st->mtime.sec != a_stx->stx_mtime.tv_sec) || (l_st->mtime.nsec != a_stx->stx_mtime.tv_nsec);
}


/*
**  Record the state of a file; an older record of it is replaced
*/
int	vbk$jnl_put	(
		VBK$JNL *	a_jnl,
	const	char *		a_abspath,
	const	VBK$FSTATE *	a_st
			)
{
VBK$FSTATE *	l_st;
void *		l_old = NULL;

	if ( !(l_st = malloc(sizeof(*l_st))) )
		return	STS$K_FATAL;

	*l_st	= *a_st;

	if ( !(1 & vbk$hash_put(&a_jnl->files, a_abspath, l_st, &l_old)) )
		{
		free(l_st);

		return	STS$K_FATAL;
		}

	free(l_old);

	return	STS$K_SUCCESS;
}


/*
**  The SSUUID of an SSET body, NULL - none
*/
static	const uint8_t *	s_vbk$ssuuid	(
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
uint32_t	l_pos = 0, l_vlen;
uint16_t	l_tag;
const uint8_t *	l_val;

	while ( 1 & vbk$tlv_next(a_body, a_len, &l_pos, &l_tag, &l_vlen, &l_val) )
		if ( (l_tag == VBK$K_TAG_SSUUID) && (l_vlen == VBK$K_UUIDSZ) )
			return	l_val;

	return	NULL;
}


/*
**  Record a saveset; one already recorded under the same UUID is replaced
*/
int	vbk$jnl_sset	(
		VBK$JNL *	a_jnl,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
const uint8_t *	l_uuid = s_vbk$ssuuid(a_body, a_len);
VBK$TLVB *	l_t = NULL;

	for ( size_t i = 0; l_uuid && (i < a_jnl->nsset); i++ )
		{
		const uint8_t *	l_u = s_vbk$ssuuid(a_jnl->sset [i].buf, a_jnl->sset [i].len);

		if ( l_u && !memcmp(l_u, l_uuid, VBK$K_UUIDSZ) )
			{
			l_t	= &a_jnl->sset [i];
			vbk$tlv_reset(l_t);
			break;
			}
		}

	if ( !l_t )
		{
		VBK$TLVB *	l_p;

		if ( !(l_p = realloc(a_jnl->sset, (a_jnl->nsset + 1) * sizeof(VBK$TLVB))) )
			return	STS$K_FATAL;

		a_jnl->sset	= l_p;
		l_t		= &a_jnl->sset [a_jnl->nsset++];
		memset(l_t, 0, sizeof(*l_t));
		}

	/* The body is copied as it stands: one item, the whole of it, then taken apart again */
	if ( !(l_t->buf = realloc(l_t->buf, a_len ? a_len : 1)) )
		return	STS$K_FATAL;

	memcpy(l_t->buf, a_body, a_len);
	l_t->len	= l_t->sz = a_len;

	return	STS$K_SUCCESS;
}


/*
**  Append a record - header and body - to a growing buffer
*/
static	int	s_vbk$putrec	(
		VBK$TLVB *	a_out,
		uint16_t	a_type,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
uint8_t *	l_p;
uint32_t	l_need = a_out->len + VBK$K_RECHDR + a_len;

	if ( l_need > a_out->sz )
		{
		uint32_t	l_sz = a_out->sz ? a_out->sz : 65536;

		while ( l_sz < l_need )
			l_sz *= 2;

		if ( !(l_p = realloc(a_out->buf, l_sz)) )
			return	STS$K_FATAL;

		a_out->buf	= l_p;
		a_out->sz	= l_sz;
		}

	l_p	= a_out->buf + a_out->len;

	vbk$put16(l_p, a_type);
	vbk$put16(l_p + 2, 0);
	vbk$put32(l_p + 4, a_len);
	memcpy(l_p + VBK$K_RECHDR, a_body, a_len);

	a_out->len	= l_need;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Write the journal: everything in memory, to <journal>.tmp, synced,
**	renamed over the journal, the directory synced.
**
**  FORMAL PARAMETERS:
**
**	a_jnl		The journal, opened for writing
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_ERROR - reported; the old journal stands.
**--
*/
int	vbk$jnl_commit	(
		VBK$JNL *	a_jnl
			)
{
const VBK$HENT **	l_sorted = NULL;
VBK$TLVB	l_out = {0}, l_rec = {0};
char		l_tmp [VBACKUP$K_SZ_PATH + 8], l_dir [VBACKUP$K_SZ_PATH];
uint64_t	l_nrec = 0;
uint8_t		l_hdr [VBK$K_JNLHDR] = {0}, l_crc [4];
int		l_fd, l_ok = 1;

	/* The header first; its count of records is filled in at the end */
	memcpy(l_hdr, vbk$t_jmagic, sizeof(vbk$t_jmagic));
	vbk$put16(l_hdr + 4, 1);

	if ( !(l_out.buf = malloc(65536)) )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));

	l_out.sz	= 65536;
	memcpy(l_out.buf, l_hdr, VBK$K_JNLHDR);
	l_out.len	= VBK$K_JNLHDR;

	for ( size_t i = 0; i < a_jnl->nsset; i++, l_nrec++ )
		l_ok &= s_vbk$putrec(&l_out, VBK$K_RT_SSET, a_jnl->sset [i].buf, a_jnl->sset [i].len);

	/* In the order of the names: the same contents make the same journal, whatever order they came in */
	if ( a_jnl->files.cnt && !(l_sorted = vbk$hash_sorted(&a_jnl->files)) )
		l_ok	= 0;

	for ( size_t i = 0; l_sorted && (i < a_jnl->files.cnt); i++ )
		{
		const VBK$HENT *	l_e = l_sorted [i];
		const VBK$FSTATE *	l_st = (const VBK$FSTATE *) l_e->val;

		vbk$tlv_reset(&l_rec);
		l_ok &= vbk$tlv_str(&l_rec, VBK$K_TAG_PATH, l_e->key);
		l_ok &= vbk$tlv_u64x2(&l_rec, VBK$K_TAG_DEVINO, l_st->dev, l_st->ino);
		l_ok &= vbk$tlv_time(&l_rec, VBK$K_TAG_CTIME, &l_st->ctime);
		l_ok &= vbk$tlv_time(&l_rec, VBK$K_TAG_MTIME, &l_st->mtime);
		l_ok &= vbk$tlv_u64(&l_rec, VBK$K_TAG_SIZE, l_st->size);
		l_ok &= vbk$tlv_u8(&l_rec, VBK$K_TAG_FTYPE, l_st->ftype);
		l_ok &= vbk$tlv_put(&l_rec, VBK$K_TAG_SSUUID, VBK$K_UUIDSZ, l_st->ssuuid);
		l_ok &= vbk$tlv_time(&l_rec, VBK$K_TAG_RECORDED, &l_st->recorded);
		l_ok &= s_vbk$putrec(&l_out, VBK$K_RT_FSTATE, l_rec.buf, l_rec.len);
		l_nrec++;
		}

	vbk$tlv_free(&l_rec);
	free(l_sorted);

	if ( !l_ok )
		{
		vbk$tlv_free(&l_out);

		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));
		}

	vbk$put64(l_out.buf + 8, l_nrec);
	vbk$put32(l_crc, $VBK_CRC(0, l_out.buf, l_out.len));

	snprintf(l_tmp, sizeof(l_tmp), "%s.tmp", a_jnl->spec);

	if ( 0 > (l_fd = open(l_tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600)) )
		{
		vbk$tlv_free(&l_out);

		return	$VBKMSG(VBACKUP$_JNLERR, l_tmp, "cannot be created", errno, strerror(errno));
		}

	if ( (write(l_fd, l_out.buf, l_out.len) != (ssize_t) l_out.len) || (write(l_fd, l_crc, 4) != 4) || fsync(l_fd) )
		{
		int	l_err = errno;

		close(l_fd);
		unlink(l_tmp);
		vbk$tlv_free(&l_out);

		return	$VBKMSG(VBACKUP$_JNLERR, l_tmp, "cannot be written", l_err, strerror(l_err));
		}

	close(l_fd);
	vbk$tlv_free(&l_out);

	if ( rename(l_tmp, a_jnl->spec) )
		{
		int	l_err = errno;

		unlink(l_tmp);

		return	$VBKMSG(VBACKUP$_JNLERR, a_jnl->spec, "cannot be replaced", l_err, strerror(l_err));
		}

	/* The rename is durable only once the directory is */
	vbk$strcpy(sizeof(l_dir), l_dir, a_jnl->spec);

	if ( 0 <= (l_fd = open(dirname(l_dir), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) )
		{
		fsync(l_fd);
		close(l_fd);
		}

	return	STS$K_SUCCESS;
}


void	vbk$jnl_close	(
		VBK$JNL *	a_jnl
			)
{
	vbk$hash_free(&a_jnl->files, 1);

	for ( size_t i = 0; i < a_jnl->nsset; i++ )
		vbk$tlv_free(&a_jnl->sset [i]);

	free(a_jnl->sset);

	if ( a_jnl->lockfd >= 0 )
		close(a_jnl->lockfd);

	a_jnl->sset	= NULL;
	a_jnl->nsset	= 0;
	a_jnl->lockfd	= -1;
}


/*
**  A time as OpenVMS shows it, local
*/
static	void	s_vbk$fmttim	(
		int64_t		a_sec,
		char *		a_buf,
		size_t		a_bufsz
			)
{
fao_desc_t	l_dsc = { (unsigned short) (a_bufsz - 1), 0, 0, a_buf };
unsigned short	l_len = 0;
struct tm	l_tm;
time_t		l_t = (time_t) a_sec;

	if ( localtime_r(&l_t, &l_tm) )
		a_sec	+= l_tm.tm_gmtoff;

	if ( !(1 & __util$fao("!%D", &l_len, &l_dsc, (fao_prm_t) a_sec)) )
		l_len	= 0;

	a_buf [l_len] = '\0';
}


static	int	s_vbk$cmpkey	(const void *a_a, const void *a_b)
{
	return	strcmp((*(const VBK$HENT * const *) a_a)->key, (*(const VBK$HENT * const *) a_b)->key);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The listing of the journal: the savesets, and under /FULL the files
**	- /SELECT applied to their absolute names - with the saveset that
**	holds their last copy.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: /JOURNAL, /FULL, /SELECT, /LIST=file
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the condition reported.
**--
*/
int	vbk$jnl_list	(
		VBK$OPTS *	a_opts
			)
{
VBK$JNL		l_jnl = {0};
FILE *		l_out = stdout;
const VBK$HENT **l_sorted = NULL;
size_t		l_n = 0;
char		l_tim [64];

	if ( !(1 & vbk$jnl_open(&l_jnl, a_opts->jnlspec, 0)) )
		return	STS$K_ERROR;

	if ( a_opts->lstfile [0] && !(l_out = fopen(a_opts->lstfile, "w")) )
		{
		vbk$jnl_close(&l_jnl);

		return	$VBKMSG(VBACKUP$_OPENOUT, a_opts->lstfile, errno, strerror(errno));
		}

	fprintf(l_out, "Journal:           %s%s\n\n", l_jnl.spec, l_jnl.exists ? "" : "  (not there yet: empty)");

	for ( size_t i = 0; i < l_jnl.nsset; i++ )
		{
		uint32_t	l_pos = 0, l_vlen;
		uint16_t	l_tag;
		const uint8_t *	l_val;
		VBK$TIME	l_t = {0};
		const char *	l_kind = "full";
		char		l_spec [VBACKUP$K_SZ_PATH] = "?", l_filter [VBACKUP$K_SZ_STR] = "";
		uint64_t	l_nf = 0, l_nb = 0;

		while ( 1 & vbk$tlv_next(l_jnl.sset [i].buf, l_jnl.sset [i].len, &l_pos, &l_tag, &l_vlen, &l_val) )
			{
			switch ( l_tag )
				{
				case	VBK$K_TAG_CREATED:	vbk$tlv_gettime(l_vlen, l_val, &l_t);				break;
				case	VBK$K_TAG_KIND:		l_kind = vbk$tlv_getu(l_vlen, l_val) ? "incremental" : "full";	break;
				case	VBK$K_TAG_NFILES:	l_nf = vbk$tlv_getu(l_vlen, l_val);				break;
				case	VBK$K_TAG_NBYTES:	l_nb = vbk$tlv_getu(l_vlen, l_val);				break;
				case	VBK$K_TAG_SPEC:		snprintf(l_spec, sizeof(l_spec), "%.*s", (int) l_vlen, l_val);	break;
				case	VBK$K_TAG_FILTER:	snprintf(l_filter, sizeof(l_filter), " %.*s", (int) l_vlen, l_val); break;
				}
			}

		s_vbk$fmttim(l_t.sec, l_tim, sizeof(l_tim));
		fprintf(l_out, "%s  %-11s %8llu files %14llu bytes  %s%s\n", l_tim, l_kind, (unsigned long long) l_nf,
			(unsigned long long) l_nb, l_spec, l_filter);
		}

	fprintf(l_out, "\nTotal of %zu saveset%s, %zu file%s recorded\n", l_jnl.nsset, (l_jnl.nsset == 1) ? "" : "s",
		l_jnl.files.cnt, (l_jnl.files.cnt == 1) ? "" : "s");

	/* The files, in the order of their names */
	if ( (a_opts->lstfmt == VBACKUP$K_LST_FULL) && l_jnl.files.cnt && (l_sorted = malloc(l_jnl.files.cnt * sizeof(*l_sorted))) )
		{
		for ( size_t i = 0; i < l_jnl.files.sz; i++ )
			if ( l_jnl.files.ent [i].key && (!a_opts->nselect || (1 & vbk$match(l_jnl.files.ent [i].key, a_opts->select, a_opts->nselect))) )
				l_sorted [l_n++] = &l_jnl.files.ent [i];

		qsort(l_sorted, l_n, sizeof(*l_sorted), s_vbk$cmpkey);

		fputc('\n', l_out);

		for ( size_t i = 0; i < l_n; i++ )
			{
			const VBK$FSTATE *	l_st = (const VBK$FSTATE *) l_sorted [i]->val;
			char			l_spec [VBACKUP$K_SZ_PATH] = "?";

			/* The saveset of the last copy, by its UUID */
			for ( size_t j = 0; j < l_jnl.nsset; j++ )
				{
				const uint8_t *	l_u = s_vbk$ssuuid(l_jnl.sset [j].buf, l_jnl.sset [j].len);
				uint32_t	l_pos = 0, l_vlen;
				uint16_t	l_tag;
				const uint8_t *	l_val;

				if ( !l_u || memcmp(l_u, l_st->ssuuid, VBK$K_UUIDSZ) )
					continue;

				while ( 1 & vbk$tlv_next(l_jnl.sset [j].buf, l_jnl.sset [j].len, &l_pos, &l_tag, &l_vlen, &l_val) )
					if ( l_tag == VBK$K_TAG_SPEC )
						snprintf(l_spec, sizeof(l_spec), "%.*s", (int) l_vlen, l_val);
				}

			s_vbk$fmttim(l_st->recorded.sec, l_tim, sizeof(l_tim));
			fprintf(l_out, "%-50s %12llu  %s  %s\n", l_sorted [i]->key, (unsigned long long) l_st->size, l_tim, l_spec);
			}

		free(l_sorted);
		}

	if ( l_out != stdout )
		fclose(l_out);

	vbk$jnl_close(&l_jnl);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Rebuild the journal from the catalogs of savesets (format.md, 9,
**	rule 5): each one is recorded, and every file it saved with status
**	OK, unless the journal knows a later copy of it already.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: the savesets in INPUT, /JOURNAL
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the condition reported.
**--
*/
int	vbk$jnl_rebuild	(
		VBK$OPTS *	a_opts
			)
{
VBK$JNL		l_jnl = {0};
uint64_t	l_nrec = 0;

	if ( !(1 & vbk$jnl_open(&l_jnl, a_opts->jnlspec, 1)) )
		return	STS$K_ERROR;

	for ( unsigned l_i = 0; l_i < a_opts->ninput; l_i++ )
		{
		VBK$RCTX	l_rctx = {0};
		VBK$TLVB	l_sset = {0};
		VBK$LOC		l_loc = {0};
		VBK$TIME	l_created = {0};
		char *		l_base [VBACKUP$K_MAXSPEC] = {0};
		char		l_abs [VBACKUP$K_SZ_PATH];
		const char *	l_spec = a_opts->input [l_i];
		const uint8_t *	l_body, *l_val;
		uint32_t	l_len, l_pos = 0, l_vlen, l_nbase = 0, l_bare = 0;
		uint16_t	l_type, l_tag;
		int		l_status;

		if ( !(1 & (l_status = vbk$rd_open(&l_rctx, l_spec, vbk$rdevent, (void *) l_spec))) )
			{
			if ( l_status == STS$K_WARN )
				$VBKMSG(VBACKUP$_NOTSAVESET, l_spec);
			else	$VBKMSG(VBACKUP$_OPENIN, l_spec, l_rctx.err, strerror(l_rctx.err));

			continue;
			}

		if ( !l_rctx.trailer )
			{
			$VBKMSG(VBACKUP$_NOTRAILER, l_spec);
			vbk$rd_close(&l_rctx);
			continue;
			}

		/* The SSET: what the SUMMARY and the TRAILER say of the saveset */
		vbk$tlv_put(&l_sset, VBK$K_TAG_SSUUID, VBK$K_UUIDSZ, l_rctx.ssuuid);
		vbk$tlv_str(&l_sset, VBK$K_TAG_SPEC, realpath(l_spec, l_abs) ? l_abs : l_spec);

		while ( 1 & vbk$tlv_next(l_rctx.summary, l_rctx.sumlen, &l_pos, &l_tag, &l_vlen, &l_val) )
			{
			switch ( l_tag )
				{
				case	VBK$K_TAG_CREATED:
					vbk$tlv_gettime(l_vlen, l_val, &l_created);
					vbk$tlv_put(&l_sset, l_tag, l_vlen, l_val);
					break;

				case	VBK$K_TAG_BASE:
					if ( l_nbase < VBACKUP$K_MAXSPEC )
						l_base [l_nbase++] = strndup((const char *) l_val, l_vlen);
					/* Fall through */

				case	VBK$K_TAG_KIND:
				case	VBK$K_TAG_FILTER:
					vbk$tlv_put(&l_sset, l_tag, l_vlen, l_val);
					break;
				}
			}

		for ( l_pos = 0; 1 & vbk$tlv_next(l_rctx.trailer, l_rctx.trllen, &l_pos, &l_tag, &l_vlen, &l_val); )
			{
			switch ( l_tag )
				{
				case	VBK$K_TAG_NFILES:
				case	VBK$K_TAG_NBYTES:	vbk$tlv_put(&l_sset, l_tag, l_vlen, l_val);			break;
				case	VBK$K_TAG_CATVOL:	l_loc.vol = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
				case	VBK$K_TAG_CATBLK:	l_loc.blk = vbk$tlv_getu(l_vlen, l_val);		break;
				case	VBK$K_TAG_CATOFF:	l_loc.off = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
				}
			}

		vbk$jnl_sset(&l_jnl, l_sset.buf, l_sset.len);
		vbk$tlv_free(&l_sset);

		/* The files: those saved OK, with what the journal needs of them */
		if ( 1 & vbk$rd_seek(&l_rctx, &l_loc) )
			while ( (1 & vbk$rd_next(&l_rctx, &l_type, &l_body, &l_len, NULL)) && (l_type != VBK$K_RT_END) )
				{
				if ( l_type != VBK$K_RT_CATALOG )
					continue;

				for ( uint32_t l_off = 0; (l_off + 4) <= l_len; )
					{
					uint32_t	l_elen = vbk$get32(l_body + l_off);
					VBK$ATTR	l_attr;
					VBK$FSTATE	l_st = {0};
					char		l_path [VBACKUP$K_SZ_PATH];
					const VBK$FSTATE *l_old;

					if ( (l_off + 4 + l_elen) > l_len )
						break;

					if ( (1 & vbk$atr_parse(l_body + l_off + 4, l_elen, &l_attr)) && (l_attr.status == VBK$K_FS_OK)
						&& (l_attr.ftype != VBK$K_FT_DIR) && (l_attr.baseidx < l_nbase) && l_base [l_attr.baseidx] )
						{
						/* A catalog of X01-01 has no CTIME and DEVINO: such an entry is of no use */
						if ( !l_attr.hasctime || !l_attr.hasdevino || (l_base [l_attr.baseidx][0] != '/') )
							l_bare++;
						else if ( snprintf(l_path, sizeof(l_path), "%s%s%.*s", l_base [l_attr.baseidx],
								strcmp(l_base [l_attr.baseidx], "/") ? "/" : "", (int) l_attr.pathlen, l_attr.path) < (int) sizeof(l_path) )
							{
							l_old	= (const VBK$FSTATE *) vbk$hash_get(&l_jnl.files, l_path);

							l_st.dev	= l_attr.dev;
							l_st.ino	= l_attr.ino;
							l_st.size	= l_attr.size;
							l_st.ctime	= l_attr.ctime;
							l_st.mtime	= l_attr.mtime;
							l_st.ftype	= l_attr.ftype;
							l_st.recorded	= l_created;
							memcpy(l_st.ssuuid, l_rctx.ssuuid, VBK$K_UUIDSZ);

							/* The later saveset wins, whatever order they were given in */
							if ( !l_old || (l_old->recorded.sec <= l_created.sec) )
								{
								vbk$jnl_put(&l_jnl, l_path, &l_st);
								l_nrec++;
								}
							}
						}

					l_off	+= 4 + l_elen;
					}
				}

		if ( l_bare )
			$VBKMSG(VBACKUP$_NOINODE, l_spec, l_bare);

		for ( uint32_t i = 0; i < l_nbase; i++ )
			free(l_base [i]);

		vbk$rd_close(&l_rctx);
		}

	if ( 1 & vbk$jnl_commit(&l_jnl) )
		$VBKMSG(VBACKUP$_RECORDED, l_nrec, l_jnl.spec);

	vbk$jnl_close(&l_jnl);

	return	STS$K_SUCCESS;
}
