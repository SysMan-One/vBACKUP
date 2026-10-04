#define	__MODULE__	"VBKLST"
#define	__IDENT__	"X01-02"
#define	__REV__		"1.2.0"

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
	if ( localtime_r(&l_t, &l_tm) )
		a_sec	+= l_tm.tm_gmtoff;

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
			strftime(l_tim, sizeof(l_tim), "%m-%d-%Y %H:%M:%S", &l_tm);

			s_vbk$perms(a_attr, l_perm);

			/* A hard link: a regular file whose further name points at the first */
			if ( a_attr->ftype == VBK$K_FT_HARDLINK )
				l_perm [0] = '-';

			fprintf(a_lst->out, "%s %3u %-8s %-8s %10llu %s %.*s", l_perm, a_attr->nlink ? a_attr->nlink : 1,
				a_attr->uname ? a_attr->uname : "?", a_attr->gname ? a_attr->gname : "?",
				(unsigned long long) a_attr->size, l_tim, (int) a_attr->pathlen, a_attr->path);

			if ( a_attr->link )
				fprintf(a_lst->out, " -> %.*s", (int) a_attr->linklen, a_attr->link);

			fputc('\n', a_lst->out);
			break;

		case	VBACKUP$K_LST_FULL:
			s_vbk$fmttim(a_attr->mtime.sec, l_tim, sizeof(l_tim));
			s_vbk$perms(a_attr, l_perm);

			fprintf(a_lst->out, "%.*s\n", (int) a_attr->pathlen, a_attr->path);
			fprintf(a_lst->out, "    Type: %-12s Size: %-14llu Owner: %s:%s (%u,%u)  Protection: %s\n",
				l_ft, (unsigned long long) a_attr->size, a_attr->uname ? a_attr->uname : "?",
				a_attr->gname ? a_attr->gname : "?", a_attr->uid, a_attr->gid, l_perm);
			fprintf(a_lst->out, "    Modified: %s  Checksum: %08X  Status: %s\n", l_tim, a_attr->crc,
				(a_attr->status < $ARRSZ(s_fsname)) ? s_fsname [a_attr->status] : "?");

			if ( a_attr->link )
				fprintf(a_lst->out, "    Link to: %.*s\n", (int) a_attr->linklen, a_attr->link);

			break;

		default:
			s_vbk$fmttim(a_attr->mtime.sec, l_tim, sizeof(l_tim));

			/* hh:mm, as BACKUP/LIST: the seconds and hundredths go */
			if ( strlen(l_tim) > 6 )
				l_tim [strlen(l_tim) - 6] = '\0';

			if ( a_attr->ftype == VBK$K_FT_DIR )
				fprintf(a_lst->out, "%.*s/\n", (int) a_attr->pathlen, a_attr->path);
			else	fprintf(a_lst->out, "%-46.*s %12llu  %s\n", (int) a_attr->pathlen, a_attr->path,
					(unsigned long long) a_attr->size, l_tim);
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

	fprintf(a_lst->out, "Listing of save set(s)\n\n");
	fprintf(a_lst->out, "Save set:          %s\n", a_rctx->spec);
	fprintf(a_lst->out, "Volumes:           %u\n", a_rctx->nvols);

	while ( 1 & vbk$tlv_next(a_rctx->summary, a_rctx->sumlen, &l_pos, &l_tag, &l_vlen, &l_val) )
		{
		switch ( l_tag )
			{
			case	VBK$K_TAG_USER:		fprintf(a_lst->out, "Written by:        %.*s\n", (int) l_vlen, l_val);	break;
			case	VBK$K_TAG_CMDLINE:	fprintf(a_lst->out, "Command:           %.*s\n", (int) l_vlen, l_val);	break;
			case	VBK$K_TAG_SYSTEM:	fprintf(a_lst->out, "Operating system:  %.*s\n", (int) l_vlen, l_val);	break;
			case	VBK$K_TAG_PRODUCT:	fprintf(a_lst->out, "Written with:      %.*s\n", (int) l_vlen, l_val);	break;
			case	VBK$K_TAG_HOST:		fprintf(a_lst->out, "Node name:         %.*s\n", (int) l_vlen, l_val);	break;
			case	VBK$K_TAG_BASE:		fprintf(a_lst->out, "Base:              %.*s\n", (int) l_vlen, l_val);	break;
			case	VBK$K_TAG_COMMENT:	fprintf(a_lst->out, "Comment:           %.*s\n", (int) l_vlen, l_val);	break;
			case	VBK$K_TAG_FILTER:	fprintf(a_lst->out, "Filter:            %.*s\n", (int) l_vlen, l_val);	break;
			case	VBK$K_TAG_KIND:		fprintf(a_lst->out, "Kind:              %s\n", vbk$tlv_getu(l_vlen, l_val) ? "incremental" : "full"); break;
			case	VBK$K_TAG_BLOCKSIZE:	fprintf(a_lst->out, "Block size:        %llu\n", (unsigned long long) vbk$tlv_getu(l_vlen, l_val));	break;
			case	VBK$K_TAG_GROUPSIZE:	fprintf(a_lst->out, "Group size:        %llu\n", (unsigned long long) vbk$tlv_getu(l_vlen, l_val));	break;

			case	VBK$K_TAG_VOLSIZE:
				if ( vbk$tlv_getu(l_vlen, l_val) )
					fprintf(a_lst->out, "Volume size:       %llu\n", (unsigned long long) vbk$tlv_getu(l_vlen, l_val));
				break;

			case	VBK$K_TAG_CREATED:
				vbk$tlv_gettime(l_vlen, l_val, &l_t);
				s_vbk$fmttim(l_t.sec, l_tim, sizeof(l_tim));
				fprintf(a_lst->out, "Date:              %s\n", l_tim);
				break;
			}
		}

	fputc('\n', a_lst->out);
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
		if ( !l_rctx.trailer )
			$VBKMSG(VBACKUP$_NOTRAILER, l_spec);

		$VBKMSG(VBACKUP$_NOCATALOG, l_spec);
		s_vbk$fromstream(&l_lst, &l_rctx);
		}

	if ( a_opts->lstfmt != VBACKUP$K_LST_LS )
		{
		fprintf(l_lst.out, "\nTotal of %llu file%s, %llu byte%s\n", (unsigned long long) l_lst.nfiles, (l_lst.nfiles == 1) ? "" : "s",
			(unsigned long long) l_lst.nbytes, (l_lst.nbytes == 1) ? "" : "s");

		if ( l_lst.npresent )
			fprintf(l_lst.out, "and %llu unchanged file%s present, not saved here\n", (unsigned long long) l_lst.npresent,
				(l_lst.npresent == 1) ? "" : "s");
		fprintf(l_lst.out, "End of save set\n");
		}

	if ( (l_lst.out != stdout) && fclose(l_lst.out) )
		$VBKMSG(VBACKUP$_WRITERR, a_opts->lstfile, errno, strerror(errno));
	else if ( l_lst.out == stdout )
		fflush(stdout);

	vbk$rd_close(&l_rctx);

	return	STS$K_SUCCESS;
}
