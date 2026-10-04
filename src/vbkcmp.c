#define	__MODULE__	"VBKCMP"
#define	__IDENT__	"X01-01"
#define	__REV__		"1.1.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkcmp.c
**
**  ABSTRACT:	/COMPARE and /VERIFY: the files of a saveset against the
**		files on the disk.
**
**  DESCRIPTION: The saveset is read in sequential mode.  Each file is
**		looked for under the directory of the command, or - under
**		/VERIFY, or /COMPARE without one - where it was saved from:
**		the base of its input specification, which the SUMMARY keeps.
**
**		Compared are the type, the contents of every DATA record
**		against the same octets on the disk, the size, and the target
**		of a symbolic link.  The holes of a sparse file are not read:
**		a region the saveset has no data for is not looked at.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
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
#include	<sys/stat.h>

#include	"vbkdef.h"

#define	VBK$K_MAXBASE	VBACKUP$K_MAXSPEC

typedef struct vbk_cmp_t
{
	VBK$OPTS *	opts;
	VBK$RCTX	rctx;
	const char *	dir;			/* The directory of the command, NULL - the bases	*/
	char *		base [VBK$K_MAXBASE];
	unsigned	nbase;

	char		path [VBACKUP$K_SZ_PATH];
	char		name [VBACKUP$K_SZ_PATH];
	uint32_t	fileno;			/* The file being compared, 0 - none		*/
	int		fd;
	int		differs;		/* Reported already: one report a file		*/
	uint8_t *	buf;

	uint64_t	nfiles, ndiff;
} VBK$CMP;


static	void	s_vbk$differs	(
		VBK$CMP *	a_cmp,
	const	char *		a_what
			)
{
	if ( a_cmp->differs )
		return;

	a_cmp->differs	= 1;
	a_cmp->ndiff++;

	$VBKMSG(VBACKUP$_COMPARERR, a_cmp->path, a_what);
}


static	void	s_vbk$done	(
		VBK$CMP *	a_cmp
			)
{
	if ( !a_cmp->fileno )
		return;

	if ( a_cmp->fd >= 0 )
		close(a_cmp->fd);

	a_cmp->fd	= -1;
	a_cmp->fileno	= 0;
	a_cmp->nfiles++;

	if ( a_cmp->opts->log && !a_cmp->differs )
		$VBKMSG(VBACKUP$_COMPARED, a_cmp->path);
}


/*
**  A FILE record: find the file on the disk and compare what can be told
**  at once - the type, the target of a link
*/
static	void	s_vbk$file	(
		VBK$CMP *	a_cmp,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
VBK$OPTS *	l_o = a_cmp->opts;
VBK$ATTR	l_attr;
struct stat	l_st;
const char *	l_dir;
char		l_lnk [VBACKUP$K_SZ_PATH];
ssize_t		l_n;

	if ( !(1 & vbk$atr_parse(a_body, a_len, &l_attr)) )
		return;

	snprintf(a_cmp->name, sizeof(a_cmp->name), "%.*s", (int) l_attr.pathlen, l_attr.path);

	if ( l_o->nexclude && (1 & vbk$match(a_cmp->name, l_o->exclude, l_o->nexclude)) )
		return;

	if ( l_o->nselect && !(1 & vbk$match(a_cmp->name, l_o->select, l_o->nselect)) )
		return;

	l_dir	= a_cmp->dir ? a_cmp->dir : ((l_attr.baseidx < a_cmp->nbase) ? a_cmp->base [l_attr.baseidx] : ".");

	if ( !(1 & vbk$mkpath(l_dir, l_attr.path, l_attr.pathlen, a_cmp->path, sizeof(a_cmp->path))) )
		return;

	a_cmp->fileno	= l_attr.fileno;
	a_cmp->differs	= 0;
	a_cmp->fd	= -1;

	if ( lstat(a_cmp->path, &l_st) )
		{
		s_vbk$differs(a_cmp, "not found on the disk");
		return;
		}

	switch ( l_attr.ftype )
		{
		case	VBK$K_FT_REG:
		case	VBK$K_FT_HARDLINK:
			if ( !S_ISREG(l_st.st_mode) )
				s_vbk$differs(a_cmp, "not a regular file on the disk");
			else if ( (l_attr.ftype == VBK$K_FT_REG) && (0 > (a_cmp->fd = open(a_cmp->path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC))) )
				$VBKMSG(VBACKUP$_OPENIN, a_cmp->path, errno, strerror(errno)), a_cmp->differs = 1, a_cmp->ndiff++;
			break;

		case	VBK$K_FT_DIR:
			if ( !S_ISDIR(l_st.st_mode) )
				s_vbk$differs(a_cmp, "not a directory on the disk");
			break;

		case	VBK$K_FT_SYMLINK:
			if ( !S_ISLNK(l_st.st_mode) )
				s_vbk$differs(a_cmp, "not a symbolic link on the disk");
			else if ( (0 > (l_n = readlink(a_cmp->path, l_lnk, sizeof(l_lnk)))) || ((uint32_t) l_n != l_attr.linklen)
				|| memcmp(l_lnk, l_attr.link, (size_t) l_n) )
				s_vbk$differs(a_cmp, "the link points elsewhere");
			break;

		case	VBK$K_FT_FIFO:
			if ( !S_ISFIFO(l_st.st_mode) )
				s_vbk$differs(a_cmp, "not a FIFO on the disk");
			break;

		case	VBK$K_FT_CHR:
		case	VBK$K_FT_BLK:
			if ( !S_ISCHR(l_st.st_mode) && !S_ISBLK(l_st.st_mode) )
				s_vbk$differs(a_cmp, "not a device on the disk");
			break;
		}
}


/*
**  A DATA record: the same octets are read from the disk
*/
static	void	s_vbk$data	(
		VBK$CMP *	a_cmp,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
uint32_t	l_n = a_len - VBK$K_DATAHDR;
uint64_t	l_off;
ssize_t		l_rc;
char		l_what [96];

	if ( !a_cmp->fileno || (a_cmp->fd < 0) || a_cmp->differs || (a_len < VBK$K_DATAHDR) || (vbk$get32(a_body) != a_cmp->fileno) )
		return;

	l_off	= vbk$get64(a_body + 8);

	if ( (l_rc = pread(a_cmp->fd, a_cmp->buf, l_n, (off_t) l_off)) != (ssize_t) l_n )
		{
		if ( l_rc < 0 )
			$VBKMSG(VBACKUP$_READERR, a_cmp->path, errno, strerror(errno)), a_cmp->differs = 1, a_cmp->ndiff++;
		else	s_vbk$differs(a_cmp, "the file on the disk is shorter");

		return;
		}

	if ( memcmp(a_cmp->buf, a_body + VBK$K_DATAHDR, l_n) )
		{
		uint32_t	i = 0;

		while ( (i < l_n) && (a_cmp->buf [i] == a_body [VBK$K_DATAHDR + i]) )
			i++;

		snprintf(l_what, sizeof(l_what), "the contents differ at octet %llu", (unsigned long long) (l_off + i));
		s_vbk$differs(a_cmp, l_what);
		}
}


/*
**  The FEND: the size
*/
static	void	s_vbk$fend	(
		VBK$CMP *	a_cmp,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
uint32_t	l_pos = 0, l_vlen;
uint64_t	l_size = 0;
uint16_t	l_tag;
const uint8_t *	l_val;
struct stat	l_st;

	if ( !a_cmp->fileno )
		return;

	while ( 1 & vbk$tlv_next(a_body, a_len, &l_pos, &l_tag, &l_vlen, &l_val) )
		if ( l_tag == VBK$K_TAG_SIZE )
			l_size	= vbk$tlv_getu(l_vlen, l_val);

	if ( (a_cmp->fd >= 0) && !fstat(a_cmp->fd, &l_st) && ((uint64_t) l_st.st_size != l_size) )
		s_vbk$differs(a_cmp, "the size differs");

	s_vbk$done(a_cmp);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The /COMPARE operation, and the second pass of /VERIFY.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command
**	a_saveset	The saveset
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- no difference;
**	STS$K_WARN	- differences, all reported;
**	STS$K_ERROR	- the saveset cannot be read.
**--
*/
int	vbk$compare	(
		VBK$OPTS *	a_opts,
	const	char *		a_saveset
			)
{
VBK$CMP *	l_cmp;
const uint8_t *	l_body, *l_val;
uint32_t	l_len, l_pos = 0, l_vlen;
uint16_t	l_type, l_tag;
int		l_status;

	if ( !(l_cmp = calloc(1, sizeof(*l_cmp))) || !(l_cmp->buf = malloc(VBK$K_MAXDATA)) )
		return	$VBKMSG(VBACKUP$_NOMEM, errno, strerror(errno));

	l_cmp->opts	= a_opts;
	l_cmp->fd	= -1;
	l_cmp->dir	= ((a_opts->op == VBACKUP$K_OP_COMPARE) && a_opts->output [0]) ? a_opts->output : NULL;

	if ( !(1 & (l_status = vbk$rd_open(&l_cmp->rctx, a_saveset, vbk$rdevent, (void *) a_saveset))) )
		{
		l_status = (l_status == STS$K_WARN) ? $VBKMSG(VBACKUP$_NOTSAVESET, a_saveset)
						    : $VBKMSG(VBACKUP$_OPENIN, a_saveset, l_cmp->rctx.err, strerror(l_cmp->rctx.err));
		free(l_cmp->buf);
		free(l_cmp);

		return	l_status;
		}

	/* Where the files came from, by the index their FILE records carry */
	while ( (1 & vbk$tlv_next(l_cmp->rctx.summary, l_cmp->rctx.sumlen, &l_pos, &l_tag, &l_vlen, &l_val)) && (l_cmp->nbase < VBK$K_MAXBASE) )
		if ( (l_tag == VBK$K_TAG_BASE) && (l_cmp->base [l_cmp->nbase] = strndup((const char *) l_val, l_vlen)) )
			l_cmp->nbase++;

	while ( 1 & vbk$rd_next(&l_cmp->rctx, &l_type, &l_body, &l_len, NULL) )
		{
		if ( l_cmp->rctx.resync && l_cmp->fileno )
			{
			$VBKMSG(VBACKUP$_FILDAMAGED, l_cmp->path);
			s_vbk$done(l_cmp);
			}

		if ( l_type == VBK$K_RT_FILE )
			{
			s_vbk$done(l_cmp);
			s_vbk$file(l_cmp, l_body, l_len);
			}
		else if ( l_type == VBK$K_RT_DATA )
			s_vbk$data(l_cmp, l_body, l_len);
		else if ( l_type == VBK$K_RT_FEND )
			s_vbk$fend(l_cmp, l_body, l_len);
		else if ( (l_type == VBK$K_RT_CATALOG) || (l_type == VBK$K_RT_END) )
			break;
		}

	s_vbk$done(l_cmp);

	if ( a_opts->log || l_cmp->ndiff )
		$VBKMSG(VBACKUP$_CMPSUMM, l_cmp->nfiles, l_cmp->ndiff);

	l_status = l_cmp->ndiff ? STS$K_WARN : STS$K_SUCCESS;

	vbk$rd_close(&l_cmp->rctx);

	for ( unsigned i = 0; i < l_cmp->nbase; i++ )
		free(l_cmp->base [i]);

	free(l_cmp->buf);
	free(l_cmp);

	return	l_status;
}
