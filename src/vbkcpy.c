#define	__MODULE__	"VBKCPY"
#define	__IDENT__	"X01-07"
#define	__REV__		"1.7.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkcpy.c
**
**  ABSTRACT:	The copy: input specifications -> a directory, as BACKUP
**		copies disk to disk when no side is a saveset.
**
**  DESCRIPTION: No saveset in between, and no second way of making a
**		file either: every entry is turned into the body of a FILE
**		record, exactly as the save does it, and handed to the
**		creation code of the restore (vbk$rst_*), so a copy puts back
**		what a save and a restore would - owner, mode, ACLs, xattrs,
**		times, chattr flags, hard links, holes.
**
**		/VERIFY reads every regular file back from both sides and
**		compares them as soon as it has been copied.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-07		 5-OCT-2026	RRL
**		CPYSUMM always; the key of a hard link made by FAO.
**
**	X01-06		 5-OCT-2026	RRL
**		What lies under the output directory is not copied: a pattern
**		with "..." reached it without naming the directory itself.
**
**	X01-03		 3-OCT-2026	RRL
**		The files are read SEQUENTIAL, with the read-ahead of files; a
**		cold one is dropped from the cache behind the read.
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
#include	<sys/stat.h>

#include	"vbkdef.h"

typedef struct vbk_copy_t
{
	VBK$OPTS *	opts;
	struct vbk_rest_t *rst;
	VBK$TLVB	rec, xbuf;
	VBK$HASH	hlink;			/* "dev:ino" -> stored name of the first name	*/
	uint8_t *	buf, *buf2;		/* VBACKUP$K_IOBUF each				*/
	char		outabs [VBACKUP$K_SZ_PATH];	/* realpath of the output: not copied into itself */
	uint32_t	fileno;
	uint64_t	ndiff;
} VBK$COPY;


/*
**  /VERIFY of one regular file: both sides read back and compared
*/
static	void	s_vbk$verify	(
		VBK$COPY *	a_cpy,
	const	char *		a_src,
	const	char *		a_dst
			)
{
int	l_fs, l_fd;
ssize_t	l_n1, l_n2;
off_t	l_off = 0;

	if ( (0 > (l_fs = open(a_src, O_RDONLY | O_CLOEXEC))) || (0 > (l_fd = open(a_dst, O_RDONLY | O_CLOEXEC))) )
		{
		if ( l_fs >= 0 )
			close(l_fs);

		$VBKMSG(VBACKUP$_COMPARERR, a_dst, "cannot be read back");
		a_cpy->ndiff++;

		return;
		}

	for ( ;; )
		{
		l_n1	= pread(l_fs, a_cpy->buf, VBACKUP$K_IOBUF, l_off);
		l_n2	= pread(l_fd, a_cpy->buf2, VBACKUP$K_IOBUF, l_off);

		if ( (l_n1 != l_n2) || (l_n1 < 0) || memcmp(a_cpy->buf, a_cpy->buf2, (size_t) l_n1) )
			{
			$VBKMSG(VBACKUP$_COMPARERR, a_dst, "the copy differs from the original");
			a_cpy->ndiff++;
			break;
			}

		if ( !l_n1 )
			break;

		l_off	+= l_n1;
		}

	close(l_fs);
	close(l_fd);
}


/*
**  The data of a regular file, region by region, into the creation code
*/
static	int	s_vbk$data	(
		VBK$COPY *	a_cpy,
		int		a_fd,
	const	char *		a_path,
		uint64_t	a_size,
		uint64_t *	a_copied
			)
{
off_t	l_beg, l_end, l_off;
ssize_t	l_n;
int	l_cold;

	*a_copied	= a_size;

	if ( 0 > (l_cold = vbk$pre_take(a_cpy->opts->pre, a_path)) )
		l_cold	= vbk$os_cold(a_fd, a_size);

	vbk$os_seq(a_fd);

	for ( l_off = 0; (uint64_t) l_off < a_size; )
		{
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
			size_t	l_want = (size_t) (((l_end - l_off) < VBACKUP$K_IOBUF) ? (l_end - l_off) : VBACKUP$K_IOBUF);

			if ( 0 > (l_n = pread(a_fd, a_cpy->buf, l_want, l_off)) )
				{
				if ( errno == EINTR )
					continue;

				*a_copied = (uint64_t) l_off;

				return	$VBKMSG(VBACKUP$_READERR, a_path, errno, strerror(errno));
				}

			if ( !l_n )
				{
				*a_copied = (uint64_t) l_off;

				return	STS$K_SUCCESS;
				}

			vbk$rst_write(a_cpy->rst, (uint64_t) l_off, a_cpy->buf, (uint32_t) l_n);

			if ( l_cold )
				vbk$os_drop(a_fd, (uint64_t) l_off, (uint64_t) l_n);

			l_off	+= l_n;
			}
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The action routine of the walk: copy one entry.
**
**  FORMAL PARAMETERS:
**
**	a_ent		The entry
**	a_arg		The copy
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- copied, or skipped and reported;
**	STS$K_WARN	- the output directory itself: not descended into;
**	STS$K_FATAL	- the copy is to stop (/CONFIRM answered QUIT).
**--
*/
static	int	s_vbk$entry	(
	const	VBK$ENT *	a_ent,
		void *		a_arg
			)
{
VBK$COPY *	l_cpy = (VBK$COPY *) a_arg;
VBK$OPTS *	l_o = l_cpy->opts;
VBK$ATTR	l_attr, l_after;
VBK$TLVB	l_xafter = {0};
char		l_key [64], l_dst [VBACKUP$K_SZ_PATH];
const char *	l_first;
uint64_t	l_copied = 0;
uint8_t		l_fstat = VBK$K_FS_OK;
int		l_fd = -1, l_status;

	/*
	**  The copy is not copied into itself: the output directory is not
	**  descended into, and what lies under it - a pattern with "..."
	**  reaches it without ever naming the directory - is not taken.
	*/
	{
	size_t	l_olen = strlen(l_cpy->outabs);

	if ( !strcmp(a_ent->abspath, l_cpy->outabs) )
		return	STS$K_WARN;

	if ( l_olen && !strncmp(a_ent->abspath, l_cpy->outabs, l_olen) && (a_ent->abspath [l_olen] == '/') )
		return	STS$K_SUCCESS;
	}

	/* A time filter chose against it: a copy has no catalog to list it in */
	if ( a_ent->present )
		return	STS$K_SUCCESS;

	if ( STS$K_SUCCESS != (l_status = vbk$sav_open(l_o, a_ent, &l_fd, &l_attr, &l_cpy->xbuf)) )
		return	(l_status == STS$K_FATAL) ? STS$K_FATAL : STS$K_SUCCESS;

	l_attr.fileno	= ++l_cpy->fileno;
	l_attr.path	= a_ent->name;
	l_attr.pathlen	= (uint32_t) strlen(a_ent->name);
	l_attr.baseidx	= a_ent->baseidx;

	/* A further name of a file already copied: a link to the first one */
	if ( (l_attr.ftype == VBK$K_FT_REG) && (l_attr.nlink > 1) )
		{
		$VBKFAOB(l_key, sizeof(l_key), "!XQ:!XQ", l_attr.dev, l_attr.ino);

		if ( (l_first = (const char *) vbk$hash_get(&l_cpy->hlink, l_key)) )
			{
			l_attr.ftype	= VBK$K_FT_HARDLINK;
			l_attr.link	= l_first;
			l_attr.linklen	= (uint32_t) strlen(l_first);
			}
		else	{
			char *	l_name = strdup(a_ent->name);

			if ( !l_name || !(1 & vbk$hash_put(&l_cpy->hlink, l_key, l_name, NULL)) )
				return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM)), STS$K_FATAL;
			}
		}

	vbk$tlv_reset(&l_cpy->rec);

	if ( !(1 & vbk$atr_tlv(&l_attr, &l_cpy->rec)) )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM)), STS$K_FATAL;

	if ( STS$K_FATAL == vbk$rst_file(l_cpy->rst, l_cpy->rec.buf, l_cpy->rec.len) )
		{
		if ( l_fd >= 0 )
			close(l_fd);

		return	STS$K_FATAL;
		}

	if ( l_attr.ftype == VBK$K_FT_REG )
		{
		if ( !(1 & s_vbk$data(l_cpy, l_fd, a_ent->path, l_attr.size, &l_copied)) )
			l_fstat	= VBK$K_FS_READERR;
		else if ( (1 & vbk$atr_get(a_ent->path, l_fd, 0, &l_after, &l_xafter))
			&& ((l_after.size != l_attr.size) || (l_after.mtime.sec != l_attr.mtime.sec) || (l_after.mtime.nsec != l_attr.mtime.nsec)) )
			{
			$VBKMSG(VBACKUP$_FILCHANGED, a_ent->path);
			l_fstat	= VBK$K_FS_CHANGED;
			}

		vbk$tlv_free(&l_xafter);

		vbk$strcpy(sizeof(l_dst), l_dst, vbk$rst_path(l_cpy->rst));
		vbk$rst_end(l_cpy->rst, l_copied, 0, l_fstat, 0);

		if ( l_o->verify && (l_fstat == VBK$K_FS_OK) )
			s_vbk$verify(l_cpy, a_ent->path, l_dst);
		}

	if ( l_fd >= 0 )
		close(l_fd);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The copy operation: every input specification into OUTPUT, the
**	stored names below it as a restore would place them.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- copied (files may have been reported);
**	STS$K_WARN	- /VERIFY found differences;
**	STS$K_ERROR	- the output directory cannot be made.
**--
*/
int	vbk$copy	(
		VBK$OPTS *	a_opts
			)
{
VBK$COPY	l_cpy = { .opts = a_opts };
char		l_base [VBACKUP$K_SZ_PATH];
uint64_t	l_nfiles = 0, l_nbytes = 0;
int		l_status = STS$K_SUCCESS;

	if ( !(l_cpy.buf = malloc(VBACKUP$K_IOBUF)) || !(l_cpy.buf2 = malloc(VBACKUP$K_IOBUF)) )
		return	$VBKMSG(VBACKUP$_NOMEM, errno, strerror(errno));

	if ( !(1 & vbk$rst_begin(a_opts, &l_cpy.rst)) )
		{
		free(l_cpy.buf);
		free(l_cpy.buf2);

		return	STS$K_ERROR;
		}

	if ( !realpath(a_opts->output, l_cpy.outabs) )
		vbk$strcpy(sizeof(l_cpy.outabs), l_cpy.outabs, a_opts->output);

	vbk$pre_start(a_opts);

	for ( unsigned i = 0; i < a_opts->ninput; i++ )
		{
		l_status = vbk$walk(a_opts, a_opts->input [i], (uint16_t) i, l_base, sizeof(l_base), s_vbk$entry, &l_cpy);

		if ( l_status == STS$K_WARN )
			$VBKMSG(VBACKUP$_NOFILES, a_opts->input [i]);

		if ( l_status == STS$K_FATAL )
			break;
		}

	vbk$pre_stop(a_opts);

	vbk$rst_finish(l_cpy.rst, &l_nfiles, &l_nbytes);

	/* Always: a command says what it did, not only under /LOG */
	$VBKMSG(VBACKUP$_CPYSUMM, l_nfiles, l_nbytes);

	if ( a_opts->verify && l_cpy.ndiff )
		$VBKMSG(VBACKUP$_CMPSUMM, l_nfiles, l_cpy.ndiff);

	vbk$hash_free(&l_cpy.hlink, 1);
	vbk$tlv_free(&l_cpy.rec);
	vbk$tlv_free(&l_cpy.xbuf);
	free(l_cpy.buf);
	free(l_cpy.buf2);

	return	l_cpy.ndiff ? STS$K_WARN : STS$K_SUCCESS;
}
