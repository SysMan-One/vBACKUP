#define	__MODULE__	"VBKRST"
#define	__IDENT__	"X01-07"
#define	__REV__		"1.7.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkrst.c
**
**  ABSTRACT:	The restore: saveset -> directory; and /EXTRACT: one file
**		of a saveset -> a file or the standard output.
**
**  DESCRIPTION: The restore reads the saveset in sequential mode, so a
**		damaged one is restored as far as it can be: a file that
**		lost data in bad blocks is kept, and reported FILDAMAGED.
**
**		What is put back when (DESIGN.md, 4.3): the data, then the
**		owner, the mode, the extended attributes; the times of a file
**		right after it is closed; the chattr flags of every file and
**		everything of a directory at the very end, deepest first -
**		a directory made read-only, or immutable, at once would let
**		nothing more be created in it.
**
**		An existing file is not overwritten unless /REPLACE, as
**		BACKUP does not without /NEW_VERSION; it is then removed and
**		created anew, so a link at its name is never written through.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-07		 5-OCT-2026	RRL
**		RESTSUMM always; names made by FAO.
**
**	X01-06		 5-OCT-2026	RRL
**		An encrypted saveset: VBK$KEY_UNLOCK before anything is read.
**
**	X01-04		 4-OCT-2026	RRL
**		DATAZ: the compressed data is restored and extracted.
**
**	X01-03		 3-OCT-2026	RRL
**		/ORIGINAL: back to the bases of the SUMMARY.
**		/INCREMENTAL removes in the order of the names, not in that of
**		a hash table and of readdir.  A restore that lost blocks names every file of the catalog it
**		did not get to (FILLOST), or says that it cannot (UNNAMED).
**
**	X01-02		 3-OCT-2026	RRL
**		Stage 2.  The creation of files is an API of its own - vbk$rst_*,
**		fed with FILE record bodies - so that the copy creates files by
**		the very same code.  Several savesets restore in turn; under
**		/INCREMENTAL each must have a catalog and a KIND, and after it
**		what its catalog does not list is removed from the directories it
**		does list (format.md, 6.6) - through descriptors opened with
**		O_NOFOLLOW, component by component, so that a forged saveset
**		cannot lead the removal out through a link.  A PRESENT file that
**		is not on the disk is reported MISSING.
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
#include	<sys/sysmacros.h>
#include	<dirent.h>

#include	"vbkdef.h"

/*
**  Something to be done at the end: a directory whole, or the flags of a file
*/
typedef struct vbk_defer_t
{
	struct vbk_defer_t *next;
	int		isdir;
	uint8_t *	body;			/* A copy of the FILE record			*/
	uint32_t	len;
	char		path [];
} VBK$DEFER;

/*
**  The state of a restore
*/
typedef struct vbk_rest_t
{
	VBK$OPTS *	opts;
	VBK$RCTX *	rctx;			/* The saveset read, NULL - a copy		*/

	uint8_t *	file;			/* The current FILE record, copied		*/
	uint32_t	filesz, filelen;
	VBK$ATTR	attr;			/* ... decoded					*/
	char		path [VBACKUP$K_SZ_PATH];
	int		fd;			/* Open while DATA records are written		*/
	int		active;			/* The current file is being restored		*/
	int		damaged;
	uint32_t	crc;

	VBK$DEFER *	defer;			/* Newest first - deepest first			*/
	uint64_t	nfiles, nbytes;
	int		quit;

	uint8_t *	seen;			/* FILENOs of the FILE records read, a bit each	*/
	uint8_t *	zbuf;			/* A DATAZ record decompressed, VBK$K_MAXDATA	*/
	char **		bases;			/* /ORIGINAL, several bases: each file to its own */
	unsigned	nbases;
	uint32_t	seensz;			/* ... octets of it				*/
} VBK$REST;


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Make the directories a path needs, as plain ones - their attributes
**	come with their own FILE records, if they are in the saveset - and
**	refuse a path that goes through a symbolic link below the output
**	directory.  A saveset is a file anybody may have made: a link
**	tree/l -> /etc followed by a file tree/l/passwd would otherwise be
**	written outside the output directory, and with /REPLACE /etc/passwd
**	would be removed first.
**
**  FORMAL PARAMETERS:
**
**	a_path		The path; modified and put back in place
**	a_skip		Length of the output directory part, which is the
**			user's and may be a link
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the directories are there;
**	STS$K_WARN	- a component is a link, errno is ELOOP;
**	STS$K_ERROR	- a directory cannot be made, errno says why.
**--
*/
static	int	s_vbk$parents	(
		char *		a_path,
		size_t		a_skip
			)
{
struct stat	l_st;

	for ( char *l_p = a_path + a_skip + 1; *l_p; l_p++ )
		{
		if ( *l_p != '/' )
			continue;

		*l_p	= '\0';

		if ( !lstat(a_path, &l_st) )
			{
			if ( !S_ISDIR(l_st.st_mode) )
				{
				*l_p	= '/';
				errno	= S_ISLNK(l_st.st_mode) ? ELOOP : ENOTDIR;

				return	S_ISLNK(l_st.st_mode) ? STS$K_WARN : STS$K_ERROR;
				}
			}
		else if ( mkdir(a_path, 0755) )
			{
			*l_p	= '/';

			return	STS$K_ERROR;
			}

		*l_p	= '/';
		}

	return	STS$K_SUCCESS;
}


static	int	s_vbk$defer	(
		VBK$REST *	a_rst,
		int		a_isdir
			)
{
VBK$DEFER *	l_d;
size_t		l_plen = strlen(a_rst->path);

	if ( !(l_d = malloc(sizeof(*l_d) + l_plen + 1)) || !(l_d->body = malloc(a_rst->filelen ? a_rst->filelen : 1)) )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));

	memcpy(l_d->path, a_rst->path, l_plen + 1);
	memcpy(l_d->body, a_rst->file, a_rst->filelen);

	l_d->len	= a_rst->filelen;
	l_d->isdir	= a_isdir;
	l_d->next	= a_rst->defer;
	a_rst->defer	= l_d;

	return	STS$K_SUCCESS;
}


/*
**  What has been deferred, deepest first: a directory gets its owner,
**  mode, attributes and times, everything its flags
*/
static	void	s_vbk$finale	(
		VBK$REST *	a_rst
			)
{
VBK$ATTR	l_attr;

	for ( VBK$DEFER *l_d = a_rst->defer, *l_n; l_d; l_d = l_n )
		{
		l_n	= l_d->next;

		if ( 1 & vbk$atr_parse(l_d->body, l_d->len, &l_attr) )
			{
			/*
			**  Through a descriptor that does not follow a link: the name
			**  may have been made a link to elsewhere since the directory
			**  was created, by a later record of a forged saveset.
			*/
			if ( l_d->isdir )
				{
				int	l_fd = open(l_d->path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

				if ( l_fd < 0 )
					$VBKMSG(VBACKUP$_ATTRERR, l_d->path, "attributes", errno, strerror(errno));
				else	{
					vbk$atr_apply(a_rst->opts, l_d->path, l_fd, &l_attr);
					close(l_fd);
					vbk$atr_settimes(l_d->path, &l_attr);
					}
				}

			if ( l_attr.hasflags && l_attr.fsflags )
				vbk$atr_setflags(l_d->path, l_attr.fsflags);
			}

		free(l_d->body);
		free(l_d);
		}

	a_rst->defer	= NULL;
}


/*
**  The current file is done with: closed, its attributes put back
*/
static	void	s_vbk$close	(
		VBK$REST *	a_rst,
		int		a_complete
			)
{
	if ( !a_rst->active )
		return;

	a_rst->active	= 0;

	if ( !a_complete )
		$VBKMSG(VBACKUP$_FILDAMAGED, a_rst->path);

	vbk$atr_apply(a_rst->opts, a_rst->path, a_rst->fd, &a_rst->attr);

	if ( a_rst->fd >= 0 )
		{
		if ( close(a_rst->fd) )
			$VBKMSG(VBACKUP$_WRITERR, a_rst->path, errno, strerror(errno));

		a_rst->fd	= -1;
		}

	vbk$atr_settimes(a_rst->path, &a_rst->attr);

	if ( a_rst->attr.hasflags && a_rst->attr.fsflags )
		s_vbk$defer(a_rst, 0);

	a_rst->nfiles++;

	if ( a_complete && a_rst->opts->log )
		$VBKMSG(a_rst->rctx ? VBACKUP$_RESTORED : VBACKUP$_COPIED, a_rst->path);
}


/*
**  An existing name in the way: removed under /REPLACE, else the file is
**  not restored.  STS$K_SUCCESS - the name is free.
*/
static	int	s_vbk$clear	(
		VBK$REST *	a_rst
			)
{
struct stat	l_st;

	if ( lstat(a_rst->path, &l_st) )
		return	STS$K_SUCCESS;

	if ( !a_rst->opts->replace )
		return	$VBKMSG(VBACKUP$_FILEEXISTS, a_rst->path), STS$K_WARN;

	if ( S_ISDIR(l_st.st_mode) ? rmdir(a_rst->path) : unlink(a_rst->path) )
		return	$VBKMSG(VBACKUP$_OPENOUT, a_rst->path, errno, strerror(errno)), STS$K_WARN;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	A FILE record: decide whether the file is restored, and create it.
**	For a regular file the data follows through VBK$RST_WRITE and the
**	file is completed by VBK$RST_END.  The restore and the copy alike
**	come here.
**
**  FORMAL PARAMETERS:
**
**	a_rst		The restore
**	a_body		The record body
**	a_len		Its length
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_FATAL when the restore is to stop.
**--
*/
int	vbk$rst_file	(
		VBK$REST *	a_rst,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
VBK$OPTS *	l_o = a_rst->opts;
VBK$ATTR *	l_a = &a_rst->attr;
const char *	l_outdir = l_o->output;
char		l_name [VBACKUP$K_SZ_PATH], l_tgt [VBACKUP$K_SZ_PATH];
mode_t		l_type = 0;
int		l_status;

	/* The record is kept: the body is reused by the next read, the attributes point into it */
	if ( a_len > a_rst->filesz )
		{
		uint8_t *	l_p;

		if ( !(l_p = realloc(a_rst->file, a_len)) )
			return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM)), STS$K_FATAL;

		a_rst->file	= l_p;
		a_rst->filesz	= a_len;
		}

	memcpy(a_rst->file, a_body, a_len);
	a_rst->filelen	= a_len;

	if ( !(1 & vbk$atr_parse(a_rst->file, a_len, l_a)) )
		return	$VBKMSG(VBACKUP$_BADREC, a_rst->rctx ? a_rst->rctx->payblk : 0, a_rst->rctx ? a_rst->rctx->payvol : 0), STS$K_SUCCESS;

	/* Read, whatever comes of it: a file of the catalog not read at all is reported at the end */
	if ( a_rst->rctx )
		{
		uint32_t	l_byte = l_a->fileno / 8;

		if ( l_byte >= a_rst->seensz )
			{
			uint32_t	l_new = (l_byte + 1) * 2;
			uint8_t *	l_p;

			if ( (l_p = realloc(a_rst->seen, l_new)) )
				{
				memset(l_p + a_rst->seensz, 0, l_new - a_rst->seensz);
				a_rst->seen	= l_p;
				a_rst->seensz	= l_new;
				}
			}

		if ( l_byte < a_rst->seensz )
			a_rst->seen [l_byte] |= (uint8_t) (1 << (l_a->fileno % 8));
		}

	$VBKFAOB(l_name, sizeof(l_name), "!AD", l_a->pathlen, l_a->path);

	if ( l_o->nexclude && (1 & vbk$match(l_name, l_o->exclude, l_o->nexclude)) )
		return	STS$K_SUCCESS;

	if ( l_o->nselect && (l_a->ftype != VBK$K_FT_DIR) && !(1 & vbk$match(l_name, l_o->select, l_o->nselect)) )
		return	STS$K_SUCCESS;

	/* A directory under /SELECT: only when it is selected itself; its parents come anyway */
	if ( l_o->nselect && (l_a->ftype == VBK$K_FT_DIR) && !(1 & vbk$match(l_name, l_o->select, l_o->nselect)) )
		return	STS$K_SUCCESS;

	/* /ORIGINAL with several bases: each file back under its own */
	if ( a_rst->nbases && (l_a->baseidx < a_rst->nbases) )
		l_outdir = a_rst->bases [l_a->baseidx];

	if ( !(1 & vbk$mkpath(l_outdir, l_a->path, l_a->pathlen, a_rst->path, sizeof(a_rst->path))) )
		return	$VBKMSG(VBACKUP$_OPENOUT, l_name, EINVAL, "a name that leads out of the output directory"), STS$K_SUCCESS;

	if ( l_o->confirm )
		{
		if ( STS$K_FATAL == (l_status = vbk$confirm("Restore", a_rst->path)) )
			return	STS$K_FATAL;

		if ( !(1 & l_status) )
			return	STS$K_SUCCESS;
		}

	if ( !(1 & s_vbk$parents(a_rst->path, strlen(l_outdir))) )
		return	$VBKMSG(VBACKUP$_OPENOUT, a_rst->path, errno, strerror(errno)), STS$K_SUCCESS;

	a_rst->fd	= -1;
	a_rst->crc	= 0;
	a_rst->damaged	= 0;

	switch ( l_a->ftype )
		{
		case	VBK$K_FT_DIR:
			if ( mkdir(a_rst->path, 0700) && (errno != EEXIST) )
				return	$VBKMSG(VBACKUP$_OPENOUT, a_rst->path, errno, strerror(errno)), STS$K_SUCCESS;

			/* Everything of a directory waits for the end */
			s_vbk$defer(a_rst, 1);
			a_rst->nfiles++;

			if ( l_o->log )
				$VBKMSG(a_rst->rctx ? VBACKUP$_RESTORED : VBACKUP$_COPIED, a_rst->path);

			return	STS$K_SUCCESS;

		case	VBK$K_FT_REG:
			if ( !(1 & s_vbk$clear(a_rst)) )
				return	STS$K_SUCCESS;

			if ( 0 > (a_rst->fd = open(a_rst->path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)) )
				return	$VBKMSG(VBACKUP$_OPENOUT, a_rst->path, errno, strerror(errno)), STS$K_SUCCESS;

			a_rst->active	= 1;

			return	STS$K_SUCCESS;

		case	VBK$K_FT_SYMLINK:
			$VBKFAOB(l_tgt, sizeof(l_tgt), "!AD", l_a->link ? l_a->linklen : 0, l_a->link ? l_a->link : "");

			if ( !(1 & s_vbk$clear(a_rst)) )
				return	STS$K_SUCCESS;

			if ( symlink(l_tgt, a_rst->path) )
				return	$VBKMSG(VBACKUP$_OPENOUT, a_rst->path, errno, strerror(errno)), STS$K_SUCCESS;

			a_rst->active	= 1;
			s_vbk$close(a_rst, 1);

			return	STS$K_SUCCESS;

		case	VBK$K_FT_HARDLINK:
			if ( !l_a->link || !(1 & vbk$mkpath(l_outdir, l_a->link, l_a->linklen, l_tgt, sizeof(l_tgt))) )
				return	$VBKMSG(VBACKUP$_BADREC, a_rst->rctx ? a_rst->rctx->payblk : 0, a_rst->rctx ? a_rst->rctx->payvol : 0), STS$K_SUCCESS;

			/* Several bases: the first name may be under another one - the one where it is */
			for ( unsigned i = 0; a_rst->nbases && (i < a_rst->nbases) && access(l_tgt, F_OK); i++ )
				if ( 1 & vbk$mkpath(a_rst->bases [i], l_a->link, l_a->linklen, l_tgt, sizeof(l_tgt)) )
					l_outdir = a_rst->bases [i];

			/* The first name, too, must not lead out through a link */
			if ( !(1 & s_vbk$parents(l_tgt, strlen(l_outdir))) )
				return	$VBKMSG(VBACKUP$_OPENOUT, a_rst->path, errno, strerror(errno)), STS$K_SUCCESS;

			if ( !(1 & s_vbk$clear(a_rst)) )
				return	STS$K_SUCCESS;

			if ( link(l_tgt, a_rst->path) )
				return	$VBKMSG(VBACKUP$_OPENOUT, a_rst->path, errno, strerror(errno)), STS$K_SUCCESS;

			/* The inode is the first name's: nothing of its own to put back */
			a_rst->nfiles++;

			if ( l_o->log )
				$VBKMSG(a_rst->rctx ? VBACKUP$_RESTORED : VBACKUP$_COPIED, a_rst->path);

			return	STS$K_SUCCESS;

		case	VBK$K_FT_CHR:	l_type = S_IFCHR;	break;
		case	VBK$K_FT_BLK:	l_type = S_IFBLK;	break;
		case	VBK$K_FT_FIFO:	l_type = S_IFIFO;	break;
		case	VBK$K_FT_SOCK:	l_type = S_IFSOCK;	break;

		default:
			return	$VBKMSG(VBACKUP$_UNSUPP, a_rst->path, "an unknown file type", EINVAL, strerror(EINVAL)), STS$K_SUCCESS;
		}

	/* The special files */
	if ( !(1 & s_vbk$clear(a_rst)) )
		return	STS$K_SUCCESS;

	if ( mknod(a_rst->path, l_type | 0600, (l_type == S_IFCHR) || (l_type == S_IFBLK)
			? makedev((unsigned) (l_a->rdev >> 32), (unsigned) (l_a->rdev & 0xFFFFFFFF)) : 0) )
		return	$VBKMSG(VBACKUP$_UNSUPP, a_rst->path, "a special file", errno, strerror(errno)), STS$K_SUCCESS;

	a_rst->active	= 1;
	s_vbk$close(a_rst, 1);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Octets of the current regular file, at their offset.
**
**  FORMAL PARAMETERS:
**
**	a_rst		The restore
**	a_off		Offset in the file
**	a_data		The octets
**	a_len		How many
**
**  RETURN VALUE:
**	STS$K_SUCCESS - written, or the file is not being restored (any
**	more: a failed write gives the rest of it up, and is reported).
**--
*/
int	vbk$rst_write	(
		VBK$REST *	a_rst,
		uint64_t	a_off,
	const	uint8_t *	a_data,
		uint32_t	a_len
			)
{
ssize_t	l_rc;

	if ( !a_rst->active || (a_rst->fd < 0) )
		return	STS$K_SUCCESS;

	while ( a_len )
		{
		if ( 0 > (l_rc = pwrite(a_rst->fd, a_data, a_len, (off_t) a_off)) )
			{
			if ( errno == EINTR )
				continue;

			$VBKMSG(VBACKUP$_WRITERR, a_rst->path, errno, strerror(errno));

			/* The rest of the file is given up; what has been written stays */
			a_rst->damaged	= 1;
			close(a_rst->fd);
			a_rst->fd	= -1;
			a_rst->active	= 0;

			return	STS$K_SUCCESS;
			}

		a_rst->crc	= $VBK_CRC(a_rst->crc, a_data, l_rc);
		a_rst->nbytes	+= (uint64_t) l_rc;
		a_data		+= l_rc;
		a_off		+= (uint64_t) l_rc;
		a_len		-= (uint32_t) l_rc;
		}

	return	STS$K_SUCCESS;
}


/*
**  A DATA or DATAZ record of the current file; a DATAZ that does not
**  decompress leaves the file incomplete
*/
static	int	s_vbk$data	(
		VBK$REST *	a_rst,
		uint16_t	a_type,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
const uint8_t *	l_data;
uint32_t	l_fileno, l_n;
uint64_t	l_off;

	if ( !a_rst->active )
		return	STS$K_SUCCESS;

	if ( (a_type == VBK$K_RT_DATAZ) && !a_rst->zbuf && !(a_rst->zbuf = malloc(VBK$K_MAXDATA)) )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));

	if ( STS$K_SUCCESS != vbk$data_get(a_type, a_body, a_len, a_rst->zbuf, &l_fileno, &l_off, &l_data, &l_n) )
		{
		a_rst->damaged	= 1;

		return	$VBKMSG(VBACKUP$_BADREC, a_rst->rctx ? a_rst->rctx->payblk : 0, a_rst->rctx ? a_rst->rctx->payvol : 0);
		}

	if ( l_fileno != a_rst->attr.fileno )
		return	STS$K_SUCCESS;

	return	vbk$rst_write(a_rst, l_off, l_data, l_n);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Complete the current regular file: its size, the check of its
**	checksum, its attributes.
**
**  FORMAL PARAMETERS:
**
**	a_rst		The restore
**	a_size		The size it is to have
**	a_crc		Its checksum, as saved
**	a_status	Its status, as saved (VBK$K_FS_*)
**	a_checkcrc	Compare the checksum (a copy has none to compare)
**
**  RETURN VALUE:
**	STS$K_SUCCESS.
**--
*/
int	vbk$rst_end	(
		VBK$REST *	a_rst,
		uint64_t	a_size,
		uint32_t	a_crc,
		uint8_t		a_status,
		int		a_checkcrc
			)
{
	if ( !a_rst->active )
		return	STS$K_SUCCESS;

	if ( (a_rst->fd >= 0) && ftruncate(a_rst->fd, (off_t) a_size) )
		$VBKMSG(VBACKUP$_WRITERR, a_rst->path, errno, strerror(errno));

	if ( a_checkcrc && !a_rst->damaged && (a_crc != a_rst->crc) )
		{
		$VBKMSG(VBACKUP$_CRCERR, a_rst->path);
		a_rst->damaged	= 1;
		}

	/* It was incomplete in the saveset already: a read error at save time */
	if ( a_status == VBK$K_FS_READERR )
		a_rst->damaged	= 1;

	s_vbk$close(a_rst, !a_rst->damaged);

	return	STS$K_SUCCESS;
}


/*
**  The FEND of the current file: its size, its checksum
*/
static	int	s_vbk$fend	(
		VBK$REST *	a_rst,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
uint32_t	l_pos = 0, l_vlen, l_crc = 0, l_fileno = 0;
uint64_t	l_size = 0;
uint8_t		l_status = VBK$K_FS_OK;
uint16_t	l_tag;
const uint8_t *	l_val;

	if ( !a_rst->active )
		return	STS$K_SUCCESS;

	while ( 1 & vbk$tlv_next(a_body, a_len, &l_pos, &l_tag, &l_vlen, &l_val) )
		{
		switch ( l_tag )
			{
			case	VBK$K_TAG_FILENO:	l_fileno = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_SIZE:		l_size	 = vbk$tlv_getu(l_vlen, l_val);			break;
			case	VBK$K_TAG_CRC:		l_crc	 = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_STATUS:	l_status = (uint8_t) vbk$tlv_getu(l_vlen, l_val);	break;
			}
		}

	if ( l_fileno != a_rst->attr.fileno )
		return	STS$K_SUCCESS;

	return	vbk$rst_end(a_rst, l_size, l_crc, l_status, 1);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Begin a restore - or a copy - into OUTPUT of the command.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command
**	a_rst		Receives the context
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the condition reported (no memory, no directory).
**--
*/
int	vbk$rst_begin	(
		VBK$OPTS *	a_opts,
		VBK$REST **	a_rst
			)
{
VBK$REST *	l_rst;

	if ( mkdir(a_opts->output, 0755) && (errno != EEXIST) )
		return	$VBKMSG(VBACKUP$_OPENOUT, a_opts->output, errno, strerror(errno));

	if ( !(l_rst = calloc(1, sizeof(*l_rst))) )
		return	$VBKMSG(VBACKUP$_NOMEM, errno, strerror(errno));

	l_rst->opts	= a_opts;
	l_rst->fd	= -1;
	*a_rst		= l_rst;

	return	STS$K_SUCCESS;
}


/*
**  The specification of the current file, for a caller's diagnostics
*/
const char *	vbk$rst_path	(
	const	VBK$REST *	a_rst
			)
{
	return	a_rst->path;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	End a restore or a copy: a file still open is closed as damaged,
**	what has been deferred is put back, the context is released.
**
**  FORMAL PARAMETERS:
**
**	a_rst		The context
**	a_nfiles	Receives the number of files restored, NULL - not wanted
**	a_nbytes	Receives the number of data octets written, NULL - ditto
**
**  RETURN VALUE:
**	STS$K_SUCCESS.
**--
*/
int	vbk$rst_finish	(
		VBK$REST *	a_rst,
		uint64_t *	a_nfiles,
		uint64_t *	a_nbytes
			)
{
	if ( a_rst->active )
		s_vbk$close(a_rst, 0);

	s_vbk$finale(a_rst);

	if ( a_nfiles )
		*a_nfiles = a_rst->nfiles;

	if ( a_nbytes )
		*a_nbytes = a_rst->nbytes;

	free(a_rst->file);
	free(a_rst->zbuf);

	for ( unsigned i = 0; i < a_rst->nbases; i++ )
		free(a_rst->bases [i]);

	free(a_rst->bases);
	free(a_rst->seen);
	free(a_rst);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Read the catalog of a saveset into a table of its names - the value
**	of a name is its FTYPE, or its FTYPE with 0x100 for a PRESENT entry -
**	for /INCREMENTAL.  The saveset must say what it is (KIND): one that
**	does not, or has no catalog, is refused, nothing being removed on the
**	strength of an incomplete description.
**
**  FORMAL PARAMETERS:
**
**	a_rctx		The saveset, open
**	a_spec		Its name, for the diagnostics
**	a_names		Receives the names
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_ERROR - refused and reported.
**--
*/
static	int	s_vbk$catnames	(
		VBK$RCTX *	a_rctx,
	const	char *		a_spec,
		VBK$HASH *	a_names
			)
{
VBK$LOC		l_loc = {0};
VBK$ATTR	l_attr;
const uint8_t *	l_val, *l_body;
uint32_t	l_pos = 0, l_vlen, l_len;
uint16_t	l_tag, l_type;
int		l_kind = 0;
char		l_name [VBACKUP$K_SZ_PATH];

	while ( 1 & vbk$tlv_next(a_rctx->summary, a_rctx->sumlen, &l_pos, &l_tag, &l_vlen, &l_val) )
		if ( l_tag == VBK$K_TAG_KIND )
			l_kind = 1;

	if ( !l_kind )
		return	$VBKMSG(VBACKUP$_NOTINCR, a_spec, "it does not say whether it is full or incremental (written before X01-02)");

	if ( !a_rctx->trailer )
		return	$VBKMSG(VBACKUP$_NOTINCR, a_spec, "it has no catalog");

	for ( l_pos = 0; 1 & vbk$tlv_next(a_rctx->trailer, a_rctx->trllen, &l_pos, &l_tag, &l_vlen, &l_val); )
		{
		switch ( l_tag )
			{
			case	VBK$K_TAG_CATVOL:	l_loc.vol = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_CATBLK:	l_loc.blk = vbk$tlv_getu(l_vlen, l_val);		break;
			case	VBK$K_TAG_CATOFF:	l_loc.off = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			}
		}

	if ( STS$K_SUCCESS != vbk$rd_seek(a_rctx, &l_loc) )
		return	$VBKMSG(VBACKUP$_NOTINCR, a_spec, "its catalog cannot be read");

	while ( (1 & vbk$rd_next(a_rctx, &l_type, &l_body, &l_len, NULL)) && (l_type != VBK$K_RT_END) )
		{
		/* A catalog with a hole in it is an incomplete description too */
		if ( a_rctx->resync )
			return	$VBKMSG(VBACKUP$_NOTINCR, a_spec, "its catalog is damaged");

		if ( l_type != VBK$K_RT_CATALOG )
			continue;

		for ( uint32_t l_off = 0; (l_off + 4) <= l_len; )
			{
			uint32_t	l_elen = vbk$get32(l_body + l_off);

			if ( (l_off + 4 + l_elen) > l_len )
				break;

			if ( (1 & vbk$atr_parse(l_body + l_off + 4, l_elen, &l_attr)) && (l_attr.pathlen < sizeof(l_name)) )
				{
				memcpy(l_name, l_attr.path, l_attr.pathlen);
				l_name [l_attr.pathlen] = '\0';

				if ( !(1 & vbk$hash_put(a_names, l_name,
						(void *) (uintptr_t) (l_attr.ftype | ((l_attr.status == VBK$K_FS_PRESENT) ? 0x100 : 0)), NULL)) )
					return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));
				}

			l_off	+= 4 + l_elen;
			}
		}

	return	vbk$rd_rewind(a_rctx);
}


/*
**  Open a directory below the output directory, one component at a time,
**  each with O_NOFOLLOW: a link anywhere on the way makes it fail
*/
static	int	s_vbk$opendir	(
	const	char *		a_outdir,
	const	char *		a_rel
			)
{
char	l_rel [VBACKUP$K_SZ_PATH];
int	l_fd, l_next;

	if ( 0 > (l_fd = open(a_outdir, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) )
		return	-1;

	vbk$strcpy(sizeof(l_rel), l_rel, a_rel);

	for ( char *l_save = NULL, *l_c = strtok_r(l_rel, "/", &l_save); l_c; l_c = strtok_r(NULL, "/", &l_save) )
		{
		l_next	= openat(l_fd, l_c, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		close(l_fd);

		if ( 0 > (l_fd = l_next) )
			return	-1;
		}

	return	l_fd;
}


/*
**  Remove an entry of a directory, a directory with all it holds; links
**  are removed, never followed
*/
static	int	s_vbk$rmtree	(
		int		a_dfd,
	const	char *		a_name
			)
{
struct stat	l_st;
DIR *		l_dir;
struct dirent *	l_de;
int		l_fd, l_status = STS$K_SUCCESS;

	if ( fstatat(a_dfd, a_name, &l_st, AT_SYMLINK_NOFOLLOW) )
		return	STS$K_ERROR;

	if ( !S_ISDIR(l_st.st_mode) )
		return	unlinkat(a_dfd, a_name, 0) ? STS$K_ERROR : STS$K_SUCCESS;

	if ( 0 > (l_fd = openat(a_dfd, a_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) )
		return	STS$K_ERROR;

	if ( !(l_dir = fdopendir(l_fd)) )
		{
		close(l_fd);

		return	STS$K_ERROR;
		}

	while ( (l_de = readdir(l_dir)) )
		if ( strcmp(l_de->d_name, ".") && strcmp(l_de->d_name, "..") )
			if ( !(1 & s_vbk$rmtree(dirfd(l_dir), l_de->d_name)) )
				l_status = STS$K_ERROR;

	closedir(l_dir);

	if ( unlinkat(a_dfd, a_name, AT_REMOVEDIR) )
		l_status = STS$K_ERROR;

	return	l_status;
}


/*
**  Names in the order of their bytes
*/
static	int	s_vbk$cmpstr	(
	const	void *		a_a,
	const	void *		a_b
			)
{
	return	strcmp(*(char * const *) a_a, *(char * const *) a_b);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	/INCREMENTAL, after a saveset has been restored: report the PRESENT
**	entries that are not on the disk, and remove from every directory
**	the catalog lists whatever the catalog does not list there.  The
**	top of the output directory itself is never touched.
**
**  FORMAL PARAMETERS:
**
**	a_rst		The restore
**	a_names		The names of the catalog, see S_VBK$CATNAMES
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_FATAL - /CONFIRM answered QUIT.
**--
*/
static	int	s_vbk$prune	(
		VBK$REST *	a_rst,
		VBK$HASH *	a_names
			)
{
VBK$OPTS *	l_o = a_rst->opts;
char		l_path [VBACKUP$K_SZ_PATH], l_rel [VBACKUP$K_SZ_PATH];
struct stat	l_st;

	/* The names in their byte order, the entries of a directory too: the same restore does the same, in the same order */
	const VBK$HENT **	l_sorted = vbk$hash_sorted(a_names);

	if ( a_names->cnt && !l_sorted )
		return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));

	for ( size_t i = 0; i < a_names->cnt; i++ )
		{
		const char *	l_name = l_sorted [i]->key;
		unsigned	l_val = (unsigned) (uintptr_t) l_sorted [i]->val;
		DIR *		l_dir;
		struct dirent *	l_de;
		char **		l_ents = NULL;
		size_t		l_nents = 0, l_szents = 0;
		int		l_fd, l_quit = 0;

		/* A file the saveset covers without holding it: an earlier saveset had to bring it */
		if ( (l_val & 0x100) && (1 & vbk$mkpath(l_o->output, l_name, (uint32_t) strlen(l_name), l_path, sizeof(l_path)))
			&& lstat(l_path, &l_st) && (errno == ENOENT) )
			$VBKMSG(VBACKUP$_MISSING, l_path);

		if ( (l_val & 0xFF) != VBK$K_FT_DIR )
			continue;

		if ( 0 > (l_fd = s_vbk$opendir(l_o->output, l_name)) )
			continue;

		if ( !(l_dir = fdopendir(l_fd)) )
			{
			close(l_fd);
			continue;
			}

		/* What is there and not in the catalog, gathered first and sorted */
		while ( (l_de = readdir(l_dir)) )
			{
			if ( !strcmp(l_de->d_name, ".") || !strcmp(l_de->d_name, "..") )
				continue;

			if ( $VBKFAOB(l_rel, sizeof(l_rel), "!AZ/!AZ", l_name, l_de->d_name) >= (int) sizeof(l_rel) )
				continue;

			if ( vbk$hash_get(a_names, l_rel) )
				continue;

			if ( l_nents == l_szents )
				{
				size_t		l_new = l_szents ? (l_szents * 2) : 16;
				char **		l_p = realloc(l_ents, l_new * sizeof(char *));

				if ( !l_p )
					break;

				l_ents	 = l_p;
				l_szents = l_new;
				}

			if ( (l_ents [l_nents] = strdup(l_de->d_name)) )
				l_nents++;
			}

		if ( l_nents )
			qsort(l_ents, l_nents, sizeof(char *), s_vbk$cmpstr);

		for ( size_t j = 0; j < l_nents; j++ )
			{
			int	l_status;

			if ( l_quit || ($VBKFAOB(l_path, sizeof(l_path), "!AZ/!AZ/!AZ", l_o->output, l_name, l_ents [j]) >= (int) sizeof(l_path)) )
				continue;

			if ( l_o->confirm )
				{
				if ( STS$K_FATAL == (l_status = vbk$confirm("Delete", l_path)) )
					{
					l_quit	= 1;
					continue;
					}

				if ( !(1 & l_status) )
					continue;
				}

			if ( !(1 & s_vbk$rmtree(dirfd(l_dir), l_ents [j])) )
				$VBKMSG(VBACKUP$_ATTRERR, l_path, "the removal", errno, strerror(errno));
			else if ( l_o->log )
				$VBKMSG(VBACKUP$_DELETED, l_path);
			}

		for ( size_t j = 0; j < l_nents; j++ )
			free(l_ents [j]);

		free(l_ents);
		closedir(l_dir);

		if ( l_quit )
			{
			free(l_sorted);

			return	STS$K_FATAL;
			}
		}

	free(l_sorted);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	After a restore that lost blocks: name every file of the catalog
**	whose FILE record was never read - it is not in the output at all,
**	and FILDAMAGED, which names the files cut short, cannot know it.  A
**	saveset without a catalog, or with a damaged one, cannot tell all
**	its names: that is said once.
**
**  FORMAL PARAMETERS:
**
**	a_rst		The restore, the FILENOs read in SEEN
**	a_rctx		The saveset, open
**	a_spec		Its name, for the diagnostics
**
**  RETURN VALUE:
**	None.
**--
*/
static	void	s_vbk$lost	(
		VBK$REST *	a_rst,
		VBK$RCTX *	a_rctx,
	const	char *		a_spec
			)
{
VBK$OPTS *	l_o = a_rst->opts;
VBK$LOC		l_loc = {0};
VBK$ATTR	l_attr;
const uint8_t *	l_val, *l_body;
uint32_t	l_pos = 0, l_vlen, l_len;
uint16_t	l_tag, l_type;
int		l_status, l_hole = 0;
char		l_name [VBACKUP$K_SZ_PATH], l_out [VBACKUP$K_SZ_PATH];

	if ( !a_rctx->trailer )
		{
		$VBKMSG(VBACKUP$_UNNAMED, a_spec, "it has no catalog");

		return;
		}

	for ( ; 1 & vbk$tlv_next(a_rctx->trailer, a_rctx->trllen, &l_pos, &l_tag, &l_vlen, &l_val); )
		{
		switch ( l_tag )
			{
			case	VBK$K_TAG_CATVOL:	l_loc.vol = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_CATBLK:	l_loc.blk = vbk$tlv_getu(l_vlen, l_val);		break;
			case	VBK$K_TAG_CATOFF:	l_loc.off = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			}
		}

	/* Its first block lost: the seek stands at the next record that can be had - a hole */
	if ( STS$K_ERROR == (l_status = vbk$rd_seek(a_rctx, &l_loc)) )
		{
		$VBKMSG(VBACKUP$_UNNAMED, a_spec, "its catalog cannot be read");

		return;
		}

	l_hole	= (l_status == STS$K_WARN);

	while ( (1 & (l_status = vbk$rd_next(a_rctx, &l_type, &l_body, &l_len, NULL))) && (l_type != VBK$K_RT_END) )
		{
		/* A CATALOG record is whole in itself: past a hole the rest still names what it can */
		l_hole	|= a_rctx->resync;

		if ( l_type != VBK$K_RT_CATALOG )
			continue;

		for ( uint32_t l_off = 0; (l_off + 4) <= l_len; )
			{
			uint32_t	l_elen = vbk$get32(l_body + l_off);

			if ( (l_off + 4 + l_elen) > l_len )
				break;

			if ( (1 & vbk$atr_parse(l_body + l_off + 4, l_elen, &l_attr)) && (l_attr.pathlen < sizeof(l_name))
				&& (l_attr.status != VBK$K_FS_PRESENT)
				&& !(((l_attr.fileno / 8) < a_rst->seensz) && (a_rst->seen [l_attr.fileno / 8] & (1 << (l_attr.fileno % 8)))) )
				{
				memcpy(l_name, l_attr.path, l_attr.pathlen);
				l_name [l_attr.pathlen] = '\0';

				/* Only what this restore would have restored: the same choice as VBK$RST_FILE */
				if ( !(l_o->nexclude && (1 & vbk$match(l_name, l_o->exclude, l_o->nexclude)))
					&& !(l_o->nselect && !(1 & vbk$match(l_name, l_o->select, l_o->nselect))) )
					$VBKMSG(VBACKUP$_FILLOST, (1 & vbk$mkpath(l_o->output, l_attr.path, l_attr.pathlen, l_out, sizeof(l_out)))
						? l_out : l_name);
				}

			l_off	+= 4 + l_elen;
			}
		}

	/* A hole in the catalog - before END too, - or its end not reached: some names may be missing */
	l_hole	|= a_rctx->resync;

	if ( l_hole || !(1 & l_status) )
		$VBKMSG(VBACKUP$_UNNAMED, a_spec, "its catalog is damaged");
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	/ORIGINAL: where the files of a saveset came from - its BASE items,
**	absolute since X01-02.  One base: it becomes the output directory,
**	and the restore is any restore (/INCREMENTAL included).  Several:
**	each file goes under its own (VBK$RST_FILE), and the output is the
**	first, for what needs one.  Where they go is always said.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: OUTPUT is set
**	a_rctx		The saveset, open
**	a_spec		Its name
**	a_bases		Receives the bases, several - the caller's to free
**	a_nbases	Receives how many
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_ERROR - refused and said why.
**--
*/
static	int	s_vbk$original	(
		VBK$OPTS *	a_opts,
		VBK$RCTX *	a_rctx,
	const	char *		a_spec,
		char ***	a_bases,
		unsigned *	a_nbases
			)
{
const uint8_t *	l_val;
uint32_t	l_pos = 0, l_vlen;
uint16_t	l_tag;
int		l_kind = 0;
char **		l_b = NULL;
unsigned	l_n = 0;

	while ( 1 & vbk$tlv_next(a_rctx->summary, a_rctx->sumlen, &l_pos, &l_tag, &l_vlen, &l_val) )
		{
		if ( l_tag == VBK$K_TAG_KIND )
			l_kind	= 1;

		if ( (l_tag != VBK$K_TAG_BASE) || !l_vlen || (l_val [0] != '/') || (l_vlen >= VBACKUP$K_SZ_PATH) || memchr(l_val, 0, l_vlen) )
			continue;

		{
		char **	l_p = realloc(l_b, (l_n + 1) * sizeof(char *));

		if ( !l_p || !(l_p [l_n] = strndup((const char *) l_val, l_vlen)) )
			{
			free(l_p ? l_p : l_b);

			return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));
			}

		l_b	= l_p;
		l_n++;
		}
		}

	/* Before X01-02 a base was kept as it was given, maybe relative: no place to go back to */
	if ( !l_kind || !l_n )
		{
		for ( unsigned i = 0; i < l_n; i++ )
			free(l_b [i]);

		free(l_b);

		return	$VBKMSG(VBACKUP$_ORIGNOBASE, a_spec);
		}

	if ( (l_n > 1) && a_opts->incremental )
		{
		for ( unsigned i = 0; i < l_n; i++ )
			free(l_b [i]);

		free(l_b);

		return	$VBKMSG(VBACKUP$_QUALUSE, "ORIGINAL", "with /INCREMENTAL, only for a saveset of one base");
		}

	for ( unsigned i = 0; i < l_n; i++ )
		$VBKMSG(VBACKUP$_ORIGTARGET, a_spec, l_b [i]);

	vbk$strcpy(sizeof(a_opts->output), a_opts->output, l_b [0]);

	if ( l_n == 1 )
		{
		free(l_b [0]);
		free(l_b);
		l_b	= NULL;
		}

	*a_bases  = l_b;
	*a_nbases = l_n;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The restore of one saveset.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: the directory OUTPUT, /INCREMENTAL
**	a_spec		The saveset
**	a_nfiles	Advanced by the number of files restored
**	a_nbytes	Advanced by the number of data octets written
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- gone through (files may have been reported);
**	STS$K_ERROR	- it cannot be read, or /INCREMENTAL refuses it;
**	STS$K_FATAL	- /CONFIRM answered QUIT.
**--
*/
static	int	s_vbk$restore1	(
		VBK$OPTS *	a_opts,
	const	char *		a_spec,
		uint64_t *	a_nfiles,
		uint64_t *	a_nbytes
			)
{
VBK$RCTX	l_rctx = {0};
VBK$REST *	l_rst;
VBK$HASH	l_names = {0};
const uint8_t *	l_body;
uint64_t	l_nf = 0, l_nb = 0;
uint32_t	l_len;
uint16_t	l_type;
int		l_status, l_quit = 0, l_lossy = 0, l_end = 0;
char **		l_bases = NULL;
unsigned	l_nbases = 0;

	if ( !(1 & (l_status = vbk$rd_open(&l_rctx, a_spec, vbk$rdevent, (void *) a_spec))) )
		return	(l_status == STS$K_WARN) ? $VBKMSG(VBACKUP$_NOTSAVESET, a_spec) : $VBKMSG(VBACKUP$_OPENIN, a_spec, l_rctx.err, strerror(l_rctx.err));

	/* Encrypted: the passphrase first - nothing of it can be read before */
	if ( !(1 & vbk$key_unlock(a_opts, &l_rctx, a_spec)) )
		{
		vbk$rd_close(&l_rctx);

		return	STS$K_ERROR;
		}


	if ( !l_rctx.trailer && !a_opts->incremental )
		$VBKMSG(VBACKUP$_NOTRAILER, a_spec);

	/* /ORIGINAL: the absolute bases of the SUMMARY (X01-02 and later) are the output */
	if ( a_opts->original && !(1 & s_vbk$original(a_opts, &l_rctx, a_spec, &l_bases, &l_nbases)) )
		{
		vbk$rd_close(&l_rctx);

		return	STS$K_ERROR;
		}

	/* /INCREMENTAL: the catalog first, before a single file is touched */
	if ( a_opts->incremental && !(1 & s_vbk$catnames(&l_rctx, a_spec, &l_names)) )
		{
		vbk$hash_free(&l_names, 0);
		vbk$rd_close(&l_rctx);

		for ( unsigned i = 0; l_bases && (i < l_nbases); i++ )
			free(l_bases [i]);

		free(l_bases);

		return	STS$K_ERROR;
		}

	if ( !(1 & vbk$rst_begin(a_opts, &l_rst)) )
		{
		vbk$hash_free(&l_names, 0);
		vbk$rd_close(&l_rctx);

		for ( unsigned i = 0; l_bases && (i < l_nbases); i++ )
			free(l_bases [i]);

		free(l_bases);

		return	STS$K_ERROR;
		}

	l_rst->rctx	= &l_rctx;

	if ( l_nbases > 1 )
		{
		l_rst->bases	= l_bases;
		l_rst->nbases	= l_nbases;
		l_bases		= NULL;
		}

	while ( 1 & vbk$rd_next(&l_rctx, &l_type, &l_body, &l_len, NULL) )
		{
		/* Blocks were lost before this record: the file being restored lost data */
		if ( l_rctx.resync && l_rst->active )
			l_rst->damaged	= 1;

		l_lossy	|= l_rctx.resync;

		if ( l_type == VBK$K_RT_FILE )
			{
			if ( l_rst->active )
				s_vbk$close(l_rst, 0);

			if ( STS$K_FATAL == vbk$rst_file(l_rst, l_body, l_len) )
				{
				l_quit	= 1;
				break;
				}
			}
		else if ( (l_type == VBK$K_RT_DATA) || (l_type == VBK$K_RT_DATAZ) )
			s_vbk$data(l_rst, l_type, l_body, l_len);
		else if ( l_type == VBK$K_RT_FEND )
			s_vbk$fend(l_rst, l_body, l_len);
		else if ( (l_type == VBK$K_RT_CATALOG) || (l_type == VBK$K_RT_END) )
			{
			l_end	= 1;
			break;
			}
		}

	if ( l_rst->active )
		s_vbk$close(l_rst, 0);

	/* Files whose records went with lost blocks are nowhere in the output: named here */
	if ( !l_quit && (l_lossy || l_rctx.nlost || !l_end) )
		s_vbk$lost(l_rst, &l_rctx, a_spec);

	/* Removed before the directories get their modes back: a read-only one would refuse */
	if ( a_opts->incremental && !l_quit && (STS$K_FATAL == s_vbk$prune(l_rst, &l_names)) )
		l_quit	= 1;

	vbk$rst_finish(l_rst, &l_nf, &l_nb);
	vbk$hash_free(&l_names, 0);
	vbk$rd_close(&l_rctx);

	*a_nfiles += l_nf;
	*a_nbytes += l_nb;

	return	l_quit ? STS$K_FATAL : STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The restore operation: the savesets of INPUT, one after the other,
**	into OUTPUT.  Under /INCREMENTAL they are the full one and the
**	incremental ones after it, in the order they were made; a saveset
**	/INCREMENTAL refuses ends the chain - the later ones build on it.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the status of the saveset that stopped the chain.
**--
*/
int	vbk$restore	(
		VBK$OPTS *	a_opts
			)
{
uint64_t	l_nfiles = 0, l_nbytes = 0;
int		l_status = STS$K_SUCCESS;

	for ( unsigned i = 0; i < a_opts->ninput; i++ )
		{
		if ( !(1 & (l_status = s_vbk$restore1(a_opts, a_opts->input [i], &l_nfiles, &l_nbytes))) && a_opts->incremental )
			break;

		if ( l_status == STS$K_FATAL )
			break;
		}

	a_opts->rstfiles = l_nfiles;
	a_opts->rstbytes = l_nbytes;

	/* Always: a command says what it did, not only under /LOG */
	$VBKMSG(VBACKUP$_RESTSUMM, l_nfiles, l_nbytes);

	return	l_status;
}


/*
**  Write <a_len> zeros at the current position of a stream: the holes of
**  a sparse file sent to the standard output
*/
static	int	s_vbk$zeros	(
		int		a_fd,
		uint64_t	a_len
			)
{
static	const uint8_t	l_zero [65536];
ssize_t			l_rc;

	while ( a_len )
		{
		if ( 0 > (l_rc = write(a_fd, l_zero, (a_len < sizeof(l_zero)) ? (size_t) a_len : sizeof(l_zero))) )
			{
			if ( errno == EINTR )
				continue;

			return	STS$K_ERROR;
			}

		a_len	-= (uint64_t) l_rc;
		}

	return	STS$K_SUCCESS;
}


/*
**  Look a stored name up in the catalog; STS$K_SUCCESS - <a_loc> is where
**  its FILE record begins
*/
static	int	s_vbk$lookup	(
		VBK$RCTX *	a_rctx,
	const	char *		a_name,
		VBK$LOC *	a_loc
			)
{
VBK$LOC		l_loc = {0};
VBK$ATTR	l_attr;
uint32_t	l_pos = 0, l_vlen, l_len;
size_t		l_nlen = strlen(a_name);
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
		return	STS$K_ERROR;

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

			if ( (1 & vbk$atr_parse(l_body + l_off + 4, l_elen, &l_attr)) && (l_attr.pathlen == l_nlen)
				&& !memcmp(l_attr.path, a_name, l_nlen) )
				{
				*a_loc	= l_attr.loc;

				return	STS$K_SUCCESS;
				}

			l_off	+= 4 + l_elen;
			}
		}

	return	STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The /EXTRACT operation: the contents of one regular file, found by
**	its exact stored name through the catalog, written to a file or -
**	output "-" or none - to the standard output.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: the saveset INPUT [0], the name EXTRACT,
**			the destination OUTPUT
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- extracted;
**	STS$K_ERROR	- not, and reported.
**--
*/
int	vbk$extract	(
		VBK$OPTS *	a_opts
			)
{
uint8_t *	l_zbuf = NULL;
VBK$RCTX	l_rctx = {0};
VBK$ATTR	l_attr;
VBK$LOC		l_loc;
const char *	l_spec = a_opts->input [0];
const uint8_t *	l_body;
uint32_t	l_len, l_crc = 0, l_fileno = 0;
uint64_t	l_pos = 0;
uint16_t	l_type;
int		l_fd = -1, l_tostd = !a_opts->output [0] || !strcmp(a_opts->output, "-"), l_status = STS$K_ERROR, l_found;

	if ( !(1 & (l_status = vbk$rd_open(&l_rctx, l_spec, vbk$rdevent, (void *) l_spec))) )
		return	(l_status == STS$K_WARN) ? $VBKMSG(VBACKUP$_NOTSAVESET, l_spec)
					 : $VBKMSG(VBACKUP$_OPENIN, l_spec, l_rctx.err, strerror(l_rctx.err));

	/* Encrypted: the passphrase first - nothing of it can be read before */
	if ( !(1 & vbk$key_unlock(a_opts, &l_rctx, l_spec)) )
		{
		vbk$rd_close(&l_rctx);

		return	STS$K_ERROR;
		}


	l_status = STS$K_ERROR;
	l_found	 = l_rctx.trailer ? (1 & s_vbk$lookup(&l_rctx, a_opts->extract, &l_loc)) : 0;

	if ( !l_rctx.trailer )
		{
		/* No catalog: the stream is searched for the FILE record */
		$VBKMSG(VBACKUP$_NOTRAILER, l_spec);
		vbk$rd_rewind(&l_rctx);
		l_found	= 2;
		}
	else if ( !l_found || (STS$K_SUCCESS != vbk$rd_seek(&l_rctx, &l_loc)) )
		{
		vbk$rd_close(&l_rctx);

		return	$VBKMSG(VBACKUP$_NOTFOUND, a_opts->extract);
		}

	while ( 1 & vbk$rd_next(&l_rctx, &l_type, &l_body, &l_len, NULL) )
		{
		if ( (l_type == VBK$K_RT_CATALOG) || (l_type == VBK$K_RT_END) )
			break;

		if ( (l_type == VBK$K_RT_FILE) && !l_fileno )
			{
			if ( !(1 & vbk$atr_parse(l_body, l_len, &l_attr)) )
				continue;

			if ( (l_attr.pathlen != strlen(a_opts->extract)) || memcmp(l_attr.path, a_opts->extract, l_attr.pathlen) )
				{
				if ( l_found == 2 )
					continue;

				break;
				}

			if ( l_attr.ftype != VBK$K_FT_REG )
				{
				$VBKMSG(VBACKUP$_UNSUPP, a_opts->extract, "anything but a regular file", EINVAL, strerror(EINVAL));
				break;
				}

			l_fileno = l_attr.fileno;

			if ( l_tostd )
				l_fd	= STDOUT_FILENO;
			else	{
				if ( !a_opts->replace && !access(a_opts->output, F_OK) )
					{
					$VBKMSG(VBACKUP$_FILEEXISTS, a_opts->output);
					break;
					}

				if ( 0 > (l_fd = open(a_opts->output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)) )
					{
					$VBKMSG(VBACKUP$_OPENOUT, a_opts->output, errno, strerror(errno));
					break;
					}
				}

			continue;
			}

		if ( !l_fileno )
			continue;

		if ( l_rctx.resync )
			{
			$VBKMSG(VBACKUP$_FILDAMAGED, a_opts->extract);
			l_crc	= ~l_crc;		/* Known bad: the checksum is not to agree by chance */
			}

		if ( (l_type == VBK$K_RT_DATA) || (l_type == VBK$K_RT_DATAZ) )
			{
			const uint8_t *	l_data;
			uint32_t	l_dfno, l_n;
			uint64_t	l_off;
			int		l_ok;

			if ( (l_type == VBK$K_RT_DATAZ) && !l_zbuf && !(l_zbuf = malloc(VBK$K_MAXDATA)) )
				{
				$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));
				break;
				}

			/* A record that does not decompress: the file is incomplete, the checksum must not agree */
			if ( STS$K_SUCCESS != vbk$data_get(l_type, l_body, l_len, l_zbuf, &l_dfno, &l_off, &l_data, &l_n) )
				{
				$VBKMSG(VBACKUP$_FILDAMAGED, a_opts->extract);
				l_crc	= ~l_crc;
				continue;
				}

			if ( l_dfno != l_fileno )
				continue;

			if ( l_tostd )
				l_ok = (l_off >= l_pos) && (1 & s_vbk$zeros(l_fd, l_off - l_pos)) && (write(l_fd, l_data, l_n) == (ssize_t) l_n);
			else	l_ok = (pwrite(l_fd, l_data, l_n, (off_t) l_off) == (ssize_t) l_n);

			if ( !l_ok )
				{
				$VBKMSG(VBACKUP$_WRITERR, l_tostd ? "(standard output)" : a_opts->output, errno, strerror(errno));
				break;
				}

			l_crc	= $VBK_CRC(l_crc, l_data, l_n);
			l_pos	= l_off + l_n;
			}
		else if ( l_type == VBK$K_RT_FEND )
			{
			uint32_t	l_p = 0, l_vlen, l_fcrc = 0;
			uint64_t	l_size = 0;
			uint16_t	l_tag;
			const uint8_t *	l_val;

			while ( 1 & vbk$tlv_next(l_body, l_len, &l_p, &l_tag, &l_vlen, &l_val) )
				{
				if ( l_tag == VBK$K_TAG_SIZE )
					l_size	= vbk$tlv_getu(l_vlen, l_val);
				else if ( l_tag == VBK$K_TAG_CRC )
					l_fcrc	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);
				}

			if ( l_tostd )
				s_vbk$zeros(l_fd, (l_size > l_pos) ? (l_size - l_pos) : 0);
			else if ( ftruncate(l_fd, (off_t) l_size) )
				$VBKMSG(VBACKUP$_WRITERR, a_opts->output, errno, strerror(errno));

			l_status = (l_fcrc == l_crc) ? STS$K_SUCCESS : $VBKMSG(VBACKUP$_CRCERR, a_opts->extract);
			break;
			}
		}

	if ( !l_fileno )
		$VBKMSG(VBACKUP$_NOTFOUND, a_opts->extract);

	if ( (l_fd >= 0) && !l_tostd && close(l_fd) )
		l_status = $VBKMSG(VBACKUP$_WRITERR, a_opts->output, errno, strerror(errno));

	vbk$rd_close(&l_rctx);
	free(l_zbuf);

	return	l_status;
}
