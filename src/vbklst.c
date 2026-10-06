#define	__MODULE__	"VBKLST"
#define	__IDENT__	"X01-17"
#define	__REV__		"1.17.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbklst.c
**
**  ABSTRACT:	/LIST: the summary of a saveset and its files, in the
**		manner of BACKUP/LIST - /BRIEF (default), /FULL - or as an
**		ls -l listing (/FORMAT=LS) for a program to read.
**
**  DESCRIPTION: The files are listed from the catalog: the TRAILER says
**		where it is, and nothing else of the saveset is read.  A
**		saveset without a TRAILER - a save that did not complete -
**		is listed by reading it whole, from its FILE and FEND records.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-17		 6-OCT-2026	RRL
**		Windows: no tm_gmtoff there, VBK$W_GMTOFF.
**
**	X01-14		 5-OCT-2026	RRL
**		The heading says the parity of a saveset of version 2.
**
**	X01-08		 5-OCT-2026	RRL
**		A pipe is listed as it is read, without NOTRAILER or NOCATALOG.
**
**	X01-07		 5-OCT-2026	RRL
**		The listing made by FAO, byte for byte the one of printf: the widths
**		of the columns by # (a wide number or name is never cut nor
**		starred), the newline as LF (!/ is CR LF).
**
**	X01-06		 5-OCT-2026	RRL
**		The heading says how a saveset is encrypted.
**
**	X01-04		 4-OCT-2026	RRL
**		The heading says PHYSICAL, the device size and sector, and
**		whether the data is compressed; of /IMAGE the file system type,
**		label, UUID, the bytes in use and the mount options.
**
**	X01-02		 3-OCT-2026	RRL
**		The PRESENT entries of an incremental catalog are counted, not
**		listed; the heading tells the kind of the saveset and its filter.
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
#include	<time.h>

#include	"vbkdef.h"

typedef struct vbk_list_t
{
	VBK$OPTS *	opts;
	FILE *		out;
	uint64_t	nfiles, nbytes;
	uint64_t	npresent;		/* PRESENT entries: covered, not in the saveset	*/
} VBK$LIST;

static	const char *	s_ftname [] = { "?", "file", "directory", "symlink", "hard link", "character device", "block device", "FIFO", "socket" };
static	const char *	s_fsname [] = { "OK", "changed while saved", "read error" };


/*
**  A time as OpenVMS shows it: dd-MMM-yyyy hh:mm:ss.cc
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

	/* !%D shows the time it is given as UTC: the local one is handed over, as OpenVMS shows it */
#ifdef	_WIN32
	(void) l_tm;
	a_sec	+= vbk$w_gmtoff(l_t);
#else
	if ( localtime_r(&l_t, &l_tm) )
		a_sec	+= l_tm.tm_gmtoff;
#endif

	if ( !(1 & __util$fao("!%D", &l_len, &l_dsc, (fao_prm_t) a_sec)) )
		l_len	= 0;

	a_buf [l_len] = '\0';
}


/*
**  The ls -l permission string of an entry
*/
static	void	s_vbk$perms	(
	const	VBK$ATTR *	a_attr,
		char		a_out [11]
			)
{
static	const char	l_type [] = "?-dl-cbps";
uint32_t		l_m = a_attr->mode;

	a_out [0] = (a_attr->ftype < sizeof(l_type) - 1) ? l_type [a_attr->ftype] : '?';
	a_out [1] = (l_m & 0400) ? 'r' : '-';
	a_out [2] = (l_m & 0200) ? 'w' : '-';
	a_out [3] = (l_m & 04000) ? ((l_m & 0100) ? 's' : 'S') : ((l_m & 0100) ? 'x' : '-');
	a_out [4] = (l_m & 040) ? 'r' : '-';
	a_out [5] = (l_m & 020) ? 'w' : '-';
	a_out [6] = (l_m & 02000) ? ((l_m & 010) ? 's' : 'S') : ((l_m & 010) ? 'x' : '-');
	a_out [7] = (l_m & 04) ? 'r' : '-';
	a_out [8] = (l_m & 02) ? 'w' : '-';
	a_out [9] = (l_m & 01000) ? ((l_m & 01) ? 't' : 'T') : ((l_m & 01) ? 'x' : '-');
	a_out [10] = '\0';
}


/*
**  One file of the listing
*/
static	void	s_vbk$entry	(
		VBK$LIST *	a_lst,
	const	VBK$ATTR *	a_attr
			)
{
char		l_tim [64], l_perm [11];
struct tm	l_tm;
time_t		l_t = (time_t) a_attr->mtime.sec;
const char *	l_ft = (a_attr->ftype < $ARRSZ(s_ftname)) ? s_ftname [a_attr->ftype] : "?";

	/* /SELECT and /EXCLUDE choose what is listed, as they choose what is restored */
	if ( a_lst->opts->nselect || a_lst->opts->nexclude )
		{
		char	l_name [VBACKUP$K_SZ_PATH];

		$VBKFAOB(l_name, sizeof(l_name), "!AD", a_attr->pathlen, a_attr->path);

		if ( a_lst->opts->nexclude && (1 & vbk$match(l_name, a_lst->opts->exclude, a_lst->opts->nexclude)) )
			return;

		if ( a_lst->opts->nselect && !(1 & vbk$match(l_name, a_lst->opts->select, a_lst->opts->nselect)) )
			return;
		}

	if ( a_attr->status == VBK$K_FS_PRESENT )
		{
		a_lst->npresent++;

		return;
		}

	a_lst->nfiles++;

	if ( (a_attr->ftype == VBK$K_FT_REG) )
		a_lst->nbytes += a_attr->size;

	switch ( a_lst->opts->lstfmt )
		{
		case	VBACKUP$K_LST_LS:
			/* What the extfs of MC and MultiArc of Far read: MM-DD-YYYY hh:mm:ss */
			localtime_r(&l_t, &l_tm);
			$VBKFAOB(l_tim, sizeof(l_tim), "!2ZL-!2ZL-!4ZL !2ZL:!2ZL:!2ZL", l_tm.tm_mon + 1, l_tm.tm_mday, l_tm.tm_year + 1900,
				l_tm.tm_hour, l_tm.tm_min, l_tm.tm_sec);

			s_vbk$perms(a_attr, l_perm);

			/* A hard link: a regular file whose further name points at the first */
			if ( a_attr->ftype == VBK$K_FT_HARDLINK )
				l_perm [0] = '-';

			{
			uint32_t	l_nl = a_attr->nlink ? a_attr->nlink : 1;
			const char *	l_un = a_attr->uname ? a_attr->uname : "?", *l_gn = a_attr->gname ? a_attr->gname : "?";

			$VBKFAOP(a_lst->out, "!AZ !#UL !AZ!#*  !AZ!#*  !#UQ !AZ !AD", l_perm, VBK$NUMW(3, l_nl), l_nl,
				l_un, VBK$PADW(8, strlen(l_un)), l_gn, VBK$PADW(8, strlen(l_gn)),
				VBK$NUMW(10, a_attr->size), a_attr->size, l_tim, a_attr->pathlen, a_attr->path);
			}

			if ( a_attr->link )
				$VBKFAOP(a_lst->out, " -> !AD", a_attr->linklen, a_attr->link);

			$VBKFAOP(a_lst->out, "\n");
			break;

		case	VBACKUP$K_LST_FULL:
			s_vbk$fmttim(a_attr->mtime.sec, l_tim, sizeof(l_tim));
			s_vbk$perms(a_attr, l_perm);

			{
			char	l_size [32];
			int	l_sl = $VBKFAOB(l_size, sizeof(l_size), "!UQ", a_attr->size);

			$VBKFAOP(a_lst->out, "!AD\n", a_attr->pathlen, a_attr->path);
			$VBKFAOP(a_lst->out, "    Type: !AZ!#*  Size: !AZ!#*  Owner: !AZ:!AZ (!UL,!UL)  Protection: !AZ\n",
				l_ft, VBK$PADW(12, strlen(l_ft)), l_size, VBK$PADW(14, l_sl), a_attr->uname ? a_attr->uname : "?",
				a_attr->gname ? a_attr->gname : "?", a_attr->uid, a_attr->gid, l_perm);
			$VBKFAOP(a_lst->out, "    Modified: !AZ  Checksum: !XL  Status: !AZ\n", l_tim, a_attr->crc,
				(a_attr->status < $ARRSZ(s_fsname)) ? s_fsname [a_attr->status] : "?");
			}

			if ( a_attr->link )
				$VBKFAOP(a_lst->out, "    Link to: !AD\n", a_attr->linklen, a_attr->link);

			break;

		default:
			s_vbk$fmttim(a_attr->mtime.sec, l_tim, sizeof(l_tim));

			/* hh:mm, as BACKUP/LIST: the seconds and hundredths go */
			if ( strlen(l_tim) > 6 )
				l_tim [strlen(l_tim) - 6] = '\0';

			if ( a_attr->ftype == VBK$K_FT_DIR )
				$VBKFAOP(a_lst->out, "!AD/\n", a_attr->pathlen, a_attr->path);
			else	$VBKFAOP(a_lst->out, "!AD!#*  !#UQ  !AZ\n", a_attr->pathlen, a_attr->path, VBK$PADW(46, a_attr->pathlen),
					VBK$NUMW(12, a_attr->size), a_attr->size, l_tim);
		}
}


/*
**  The heading of the listing, out of the SUMMARY
*/
static	void	s_vbk$heading	(
		VBK$LIST *	a_lst,
		VBK$RCTX *	a_rctx
			)
{
uint32_t	l_pos = 0, l_vlen;
uint16_t	l_tag;
const uint8_t *	l_val;
char		l_tim [64];
VBK$TIME	l_t;

	$VBKFAOP(a_lst->out, "Listing of save set(s)\n\n");
	$VBKFAOP(a_lst->out, "Save set:          !AZ\n", a_rctx->spec);
	$VBKFAOP(a_lst->out, "Volumes:           !UL\n", a_rctx->nvols);

	while ( 1 & vbk$tlv_next(a_rctx->summary, a_rctx->sumlen, &l_pos, &l_tag, &l_vlen, &l_val) )
		{
		switch ( l_tag )
			{
			case	VBK$K_TAG_USER:		$VBKFAOP(a_lst->out, "Written by:        !AD\n", l_vlen, l_val);	break;
			case	VBK$K_TAG_CMDLINE:	$VBKFAOP(a_lst->out, "Command:           !AD\n", l_vlen, l_val);	break;
			case	VBK$K_TAG_SYSTEM:	$VBKFAOP(a_lst->out, "Operating system:  !AD\n", l_vlen, l_val);	break;
			case	VBK$K_TAG_PRODUCT:	$VBKFAOP(a_lst->out, "Written with:      !AD\n", l_vlen, l_val);	break;
			case	VBK$K_TAG_HOST:		$VBKFAOP(a_lst->out, "Node name:         !AD\n", l_vlen, l_val);	break;
			case	VBK$K_TAG_BASE:		$VBKFAOP(a_lst->out, "Base:              !AD\n", l_vlen, l_val);	break;
			case	VBK$K_TAG_COMMENT:	$VBKFAOP(a_lst->out, "Comment:           !AD\n", l_vlen, l_val);	break;
			case	VBK$K_TAG_FILTER:	$VBKFAOP(a_lst->out, "Filter:            !AD\n", l_vlen, l_val);	break;
			case	VBK$K_TAG_KIND:		$VBKFAOP(a_lst->out, "Kind:              !AZ\n", vbk$tlv_getu(l_vlen, l_val) ? "incremental" : "full"); break;
			case	VBK$K_TAG_PHYSICAL:	$VBKFAOP(a_lst->out, "Physical:          a device, block by block (/PHYSICAL)\n");	break;
			case	VBK$K_TAG_DEVSIZE:	$VBKFAOP(a_lst->out, "Device size:       !UQ bytes\n", vbk$tlv_getu(l_vlen, l_val)); break;
			case	VBK$K_TAG_SECTORSIZE:	$VBKFAOP(a_lst->out, "Sector size:       !UQ bytes\n", vbk$tlv_getu(l_vlen, l_val)); break;
			case	VBK$K_TAG_COMPRESS:	$VBKFAOP(a_lst->out, "Data format:       compressed (LZ4)\n");				break;
			case	VBK$K_TAG_KDFITER:	$VBKFAOP(a_lst->out, "Encryption:        ChaCha20, HMAC-SHA256; PBKDF2-HMAC-SHA256, !UQ iterations\n",
							vbk$tlv_getu(l_vlen, l_val));					break;
			case	VBK$K_TAG_IMAGE:	$VBKFAOP(a_lst->out, "Image:             a whole file system (/IMAGE)\n");			break;
			case	VBK$K_TAG_FSTYPE:	$VBKFAOP(a_lst->out, "File system:       !AD\n", l_vlen, l_val);			break;
			case	VBK$K_TAG_FSLABEL:	$VBKFAOP(a_lst->out, "Label:             !AD\n", l_vlen, l_val);			break;
			case	VBK$K_TAG_FSUUID:	$VBKFAOP(a_lst->out, "UUID:              !AD\n", l_vlen, l_val);			break;
			case	VBK$K_TAG_FSUSED:	$VBKFAOP(a_lst->out, "In use:            !UQ bytes\n", vbk$tlv_getu(l_vlen, l_val)); break;
			case	VBK$K_TAG_MOUNTOPTS:	$VBKFAOP(a_lst->out, "Mounted with:      !AD\n", l_vlen, l_val);			break;
			case	VBK$K_TAG_BLOCKSIZE:	$VBKFAOP(a_lst->out, "Block size:        !UQ\n", vbk$tlv_getu(l_vlen, l_val));	break;
			case	VBK$K_TAG_GROUPSIZE:	$VBKFAOP(a_lst->out, "Group size:        !UQ\n", vbk$tlv_getu(l_vlen, l_val));	break;
			case	VBK$K_TAG_PARITY:	$VBKFAOP(a_lst->out, "Parity:            !UQ blocks a group\n", vbk$tlv_getu(l_vlen, l_val)); break;

			case	VBK$K_TAG_VOLSIZE:
				if ( vbk$tlv_getu(l_vlen, l_val) )
					$VBKFAOP(a_lst->out, "Volume size:       !UQ\n", vbk$tlv_getu(l_vlen, l_val));
				break;

			case	VBK$K_TAG_CREATED:
				vbk$tlv_gettime(l_vlen, l_val, &l_t);
				s_vbk$fmttim(l_t.sec, l_tim, sizeof(l_tim));
				$VBKFAOP(a_lst->out, "Date:              !AZ\n", l_tim);
				break;
			}
		}

	$VBKFAOP(a_lst->out, "\n");
}


/*
**  The files, out of the catalog
*/
static	int	s_vbk$fromcat	(
		VBK$LIST *	a_lst,
		VBK$RCTX *	a_rctx
			)
{
VBK$LOC		l_loc = {0};
VBK$ATTR	l_attr;
uint32_t	l_pos = 0, l_vlen, l_len;
uint16_t	l_tag, l_type;
const uint8_t *	l_val, *l_body;

	while ( 1 & vbk$tlv_next(a_rctx->trailer, a_rctx->trllen, &l_pos, &l_tag, &l_vlen, &l_val) )
		{
		switch ( l_tag )
			{
			case	VBK$K_TAG_CATVOL:	l_loc.vol = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_CATBLK:	l_loc.blk = vbk$tlv_getu(l_vlen, l_val);		break;
			case	VBK$K_TAG_CATOFF:	l_loc.off = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			}
		}

	if ( !(1 & vbk$rd_seek(a_rctx, &l_loc)) )
		return	STS$K_WARN;

	while ( 1 & vbk$rd_next(a_rctx, &l_type, &l_body, &l_len, NULL) )
		{
		if ( l_type == VBK$K_RT_END )
			break;

		if ( l_type != VBK$K_RT_CATALOG )
			continue;

		for ( uint32_t l_off = 0; (l_off + 4) <= l_len; )
			{
			uint32_t	l_elen = vbk$get32(l_body + l_off);

			if ( (l_off + 4 + l_elen) > l_len )
				break;

			if ( 1 & vbk$atr_parse(l_body + l_off + 4, l_elen, &l_attr) )
				s_vbk$entry(a_lst, &l_attr);

			l_off	+= 4 + l_elen;
			}
		}

	return	STS$K_SUCCESS;
}


/*
**  The files, out of the record stream: a FILE record is remembered, its
**  FEND says how big it came out and how it fared
*/
static	int	s_vbk$fromstream	(
		VBK$LIST *	a_lst,
		VBK$RCTX *	a_rctx
			)
{
VBK$ATTR	l_attr;
uint8_t *	l_file = NULL;
uint32_t	l_len, l_flen = 0, l_pos, l_vlen;
uint16_t	l_type, l_tag;
const uint8_t *	l_body, *l_val;
int		l_have = 0;

	vbk$rd_rewind(a_rctx);

	while ( 1 & vbk$rd_next(a_rctx, &l_type, &l_body, &l_len, NULL) )
		{
		if ( a_rctx->resync )
			l_have	= 0;

		if ( l_type == VBK$K_RT_FILE )
			{
			uint8_t *	l_p;

			if ( !(l_p = realloc(l_file, l_len ? l_len : 1)) )
				break;

			l_file	= l_p;
			memcpy(l_file, l_body, l_len);
			l_flen	= l_len;
			l_have	= 1;
			}
		else if ( (l_type == VBK$K_RT_FEND) && l_have && (1 & vbk$atr_parse(l_file, l_flen, &l_attr)) )
			{
			for ( l_pos = 0; 1 & vbk$tlv_next(l_body, l_len, &l_pos, &l_tag, &l_vlen, &l_val); )
				{
				if ( l_tag == VBK$K_TAG_SIZE )
					l_attr.size	= vbk$tlv_getu(l_vlen, l_val);
				else if ( l_tag == VBK$K_TAG_CRC )
					l_attr.crc	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);
				else if ( l_tag == VBK$K_TAG_STATUS )
					l_attr.status	= (uint8_t) vbk$tlv_getu(l_vlen, l_val);
				}

			s_vbk$entry(a_lst, &l_attr);
			l_have	= 0;
			}
		else if ( (l_type == VBK$K_RT_CATALOG) || (l_type == VBK$K_RT_END) )
			break;
		}

	free(l_file);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The /LIST operation.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: the saveset is INPUT [0], /LIST=file in
**			LSTFILE, the format in LSTFMT
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- listed;
**	STS$K_ERROR	- the saveset cannot be read, or the listing written.
**--
*/
int	vbk$list	(
		VBK$OPTS *	a_opts
			)
{
VBK$RCTX	l_rctx = {0};
VBK$LIST	l_lst = { .opts = a_opts, .out = stdout };
const char *	l_spec = a_opts->input [0];
int		l_status;

	if ( !(1 & (l_status = vbk$rd_open(&l_rctx, l_spec, vbk$rdevent, (void *) l_spec))) )
		{
		if ( l_status == STS$K_WARN )
			return	$VBKMSG(VBACKUP$_NOTSAVESET, l_spec);

		return	$VBKMSG(VBACKUP$_OPENIN, l_spec, l_rctx.err, strerror(l_rctx.err));
		}

	/* Encrypted: the passphrase first - nothing of it can be read before */
	if ( !(1 & vbk$key_unlock(a_opts, &l_rctx, l_spec)) )
		{
		vbk$rd_close(&l_rctx);

		return	STS$K_ERROR;
		}


	if ( a_opts->lstfile [0] && !(l_lst.out = fopen(a_opts->lstfile, "w")) )
		{
		vbk$rd_close(&l_rctx);

		return	$VBKMSG(VBACKUP$_OPENOUT, a_opts->lstfile, errno, strerror(errno));
		}

	if ( a_opts->lstfmt != VBACKUP$K_LST_LS )
		s_vbk$heading(&l_lst, &l_rctx);

	l_status = l_rctx.trailer ? s_vbk$fromcat(&l_lst, &l_rctx) : STS$K_WARN;

	if ( l_status == STS$K_WARN )
		{
		/* A pipe has its catalog at its end: it is listed as it is read, and that is no fault */
		if ( !l_rctx.trailer && !l_rctx.isstream )
			$VBKMSG(VBACKUP$_NOTRAILER, l_spec);

		if ( !l_rctx.isstream )
			$VBKMSG(VBACKUP$_NOCATALOG, l_spec);
		s_vbk$fromstream(&l_lst, &l_rctx);
		}

	if ( a_opts->lstfmt != VBACKUP$K_LST_LS )
		{
		$VBKFAOP(l_lst.out, "\nTotal of !UQ file!%S, !UQ byte!%S\n", l_lst.nfiles, l_lst.nbytes);

		if ( l_lst.npresent )
			$VBKFAOP(l_lst.out, "and !UQ unchanged file!%S present, not saved here\n", l_lst.npresent);

		$VBKFAOP(l_lst.out, "End of save set\n");
		}

	if ( (l_lst.out != stdout) && fclose(l_lst.out) )
		$VBKMSG(VBACKUP$_WRITERR, a_opts->lstfile, errno, strerror(errno));
	else if ( l_lst.out == stdout )
		fflush(stdout);

	/* A stream: its TRAILER is known only at its end - a stream that ended without one was cut */
	if ( l_rctx.isstream && l_rctx.eof && !l_rctx.trailer && !l_rctx.trlraw )
		$VBKMSG(VBACKUP$_NOTRAILER, l_spec);

	vbk$rd_close(&l_rctx);

	return	STS$K_SUCCESS;
}
