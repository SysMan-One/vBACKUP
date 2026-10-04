#define	__MODULE__	"VBKX"
#define	__IDENT__	"X01-04"
#define	__REV__		"1.4.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkx.c
**
**  ABSTRACT:	The stand-alone extractor: list, extract, print and test a
**		saveset with nothing but the reading core (libvbkrd) - no
**		command language, no help library, no message facility.
**
**  DESCRIPTION: For the machine VBACKUP is not installed on (a rescue
**		system, another distribution), and for the file managers,
**		whose archive plugins want a plain command with a plain
**		listing.  It is linked statically where the system allows it.
**
**		    vbkx l saveset			list, from the catalog
**		    vbkx x saveset [-C dir] [-f] [name...]	extract
**		    vbkx p saveset name			a file to stdout
**		    vbkx t saveset			test: read all, check CRCs
**
**		A name is a stored name as the listing shows it; a directory
**		name takes what is below it.  Without names the whole saveset
**		is read in one pass; with names each file is reached through
**		the catalog.  Put back: data and holes, the mode, the times,
**		symbolic and hard links, FIFOs; as root the owner (by number)
**		and the device files.  Not put back: ACLs, extended
**		attributes, chattr flags - that is VBACKUP's business.
**
**		The names of a saveset are not trusted: "..", a leading "/"
**		and a way through a symbolic link are refused, every
**		directory on the way is opened with O_NOFOLLOW.  Damage is
**		repaired by the core as far as the XOR blocks allow; a file
**		cut short is named "incomplete", a file of the catalog that
**		was never reached "not extracted".
**
**		The listing, one line per file, for MultiArc and its kin:
**
**		    2026-10-03 23:40:12        1234 -0644 tree/a.txt
**		    2026-10-03 23:40:12           5 l0777 tree/link -> a.txt
**
**		Type letters as ls: - d l h (hard link) c b p s.
**
**		Completion: 0 - all done, 1 - something damaged or not done,
**		2 - the command or the saveset is not usable.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-04		 4-OCT-2026	RRL
**		DATAZ: the data compressed with /DATA_FORMAT=COMPRESSED.
**
**	X01-03		 4-OCT-2026	RRL
**		The owner, mode and times that cannot be set are said, not
**		passed over (a warning of glibc on Ubuntu, -Wunused-result).
**
**	X01-03		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdarg.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<time.h>
#include	<sys/stat.h>
#include	<sys/sysmacros.h>

#include	"vbkrd.h"
#include	"vbkos.h"
#include	"vbklz4.h"

#define	VBKX$K_SZ_PATH	4096

/*
**  One file as a FILE record or a catalog entry describes it
*/
typedef struct vbkx_ent_t
{
	uint32_t	fileno;
	const char *	path;			/* Points into the body: not terminated		*/
	uint32_t	pathlen;
	uint8_t		ftype;
	uint32_t	mode, uid, gid;
	uint64_t	size, rdev;
	VBK$TIME	mtime, atime;
	const char *	link;
	uint32_t	linklen;
	uint32_t	crc;
	uint8_t		status;
	VBK$LOC		loc;
	uint8_t *	body;			/* A catalog entry: its own copy		*/
} VBKX$ENT;

typedef struct vbkx_dir_t			/* A directory whose mode and times wait	*/
{
	char *		path;
	uint32_t	mode;
	VBK$TIME	mtime, atime;
} VBKX$DIR;

static	const char *	s_spec;			/* The saveset				*/
static	const char *	s_outdir = ".";
static	int		s_force, s_root, s_bad;
static	int		s_outfd = -1;

static	VBKX$DIR *	s_dirs;
static	size_t		s_ndirs, s_szdirs;

static	uint8_t *	s_seen;			/* FILENOs met in the stream, a bit each	*/
static	uint32_t	s_seensz;


static	void	s_vbkx$msg	(
	const	char *		a_fmt,
		...
			)
{
va_list	l_ap;

	fputs("vbkx: ", stderr);
	va_start(l_ap, a_fmt);
	vfprintf(stderr, a_fmt, l_ap);
	va_end(l_ap);
	fputc('\n', stderr);
}


/*
**  An attribute that cannot be put back: said, the file stays as it is -
**  the data is there, which is what counts
*/
static	void	s_vbkx$attrerr	(
	const	char *		a_name,
	const	char *		a_what
			)
{
	s_vbkx$msg("%s: the %s cannot be set: %s", a_name, a_what, strerror(errno));
}


static	void	s_vbkx$event	(
		void *		a_arg,
		int		a_ev,
		uint32_t	a_vol,
		uint64_t	a_blk
			)
{
	switch ( a_ev )
		{
		case	VBK$K_EV_REPAIRED:
			s_vbkx$msg("block %llu of volume %u was bad and has been repaired", (unsigned long long) a_blk, a_vol);
			break;

		case	VBK$K_EV_LOST:
			s_vbkx$msg("block %llu of volume %u is bad and cannot be repaired", (unsigned long long) a_blk, a_vol);
			s_bad	= 1;
			break;

		case	VBK$K_EV_MISSVOL:
			s_vbkx$msg("volume %u is missing", a_vol);
			s_bad	= 1;
			break;

		case	VBK$K_EV_WRONGVOL:
			s_vbkx$msg("volume %u belongs to another saveset, or is none", a_vol);
			break;

		case	VBK$K_EV_BADREC:
			s_vbkx$msg("a bad record in block %llu of volume %u", (unsigned long long) a_blk, a_vol);
			s_bad	= 1;
			break;
		}
}


/*
**  Decode the per-file tags of a FILE record or a catalog entry
*/
static	int	s_vbkx$parse	(
	const	uint8_t *	a_body,
		uint32_t	a_len,
		VBKX$ENT *	a_ent
			)
{
uint32_t	l_pos = 0, l_vlen;
uint16_t	l_tag;
const uint8_t *	l_val;
int		l_status;

	memset(a_ent, 0, sizeof(*a_ent));

	while ( 1 & (l_status = vbk$tlv_next(a_body, a_len, &l_pos, &l_tag, &l_vlen, &l_val)) )
		{
		switch ( l_tag )
			{
			case	VBK$K_TAG_FILENO:	a_ent->fileno	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_PATH:		a_ent->path	= (const char *) l_val;
							a_ent->pathlen	= l_vlen;					break;
			case	VBK$K_TAG_FTYPE:	a_ent->ftype	= (uint8_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_MODE:		a_ent->mode	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_UID:		a_ent->uid	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_GID:		a_ent->gid	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_SIZE:		a_ent->size	= vbk$tlv_getu(l_vlen, l_val);			break;
			case	VBK$K_TAG_RDEV:		a_ent->rdev	= vbk$tlv_getu(l_vlen, l_val);			break;
			case	VBK$K_TAG_MTIME:	vbk$tlv_gettime(l_vlen, l_val, &a_ent->mtime);			break;
			case	VBK$K_TAG_ATIME:	vbk$tlv_gettime(l_vlen, l_val, &a_ent->atime);			break;
			case	VBK$K_TAG_LINK:		a_ent->link	= (const char *) l_val;
							a_ent->linklen	= l_vlen;					break;
			case	VBK$K_TAG_CRC:		a_ent->crc	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_STATUS:	a_ent->status	= (uint8_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_LOCVOL:	a_ent->loc.vol	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_LOCBLK:	a_ent->loc.blk	= vbk$tlv_getu(l_vlen, l_val);			break;
			case	VBK$K_TAG_LOCOFF:	a_ent->loc.off	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			}
		}

	if ( (l_status != STS$K_WARN) || !a_ent->path || !a_ent->pathlen || (a_ent->pathlen >= VBKX$K_SZ_PATH) || !a_ent->ftype )
		return	STS$K_ERROR;

	if ( !a_ent->atime.sec )
		a_ent->atime	= a_ent->mtime;

	return	STS$K_SUCCESS;
}


/*
**  A stored name that is safe to use: relative, no "." or ".." component,
**  no empty one, no NUL
*/
static	int	s_vbkx$nameok	(
	const	char *		a_name,
		size_t		a_len
			)
{
	if ( !a_len || (a_name [0] == '/') || memchr(a_name, '\0', a_len) )
		return	0;

	for ( size_t i = 0, j; i < a_len; i = j + 1 )
		{
		for ( j = i; (j < a_len) && (a_name [j] != '/'); j++ )
			;

		if ( (j == i) || ((j - i) == 1 && (a_name [i] == '.')) || ((j - i) == 2 && (a_name [i] == '.') && (a_name [i + 1] == '.')) )
			return	0;
		}

	return	1;
}


/*
**  Open the directory a name lies in, below the output directory, one
**  component at a time with O_NOFOLLOW - a symbolic link on the way makes
**  it fail.  <a_create>: the missing ones are made.  <*a_last> receives
**  the last component of the name.
**
**  RETURN VALUE:
**	A descriptor of the directory, or -1 (errno says why).
*/
static	int	s_vbkx$parent	(
		char *		a_name,
		int		a_create,
		char **		a_last
			)
{
char *	l_c = a_name, *l_s;
int	l_fd = dup(s_outfd), l_next;

	while ( (l_fd >= 0) && (l_s = strchr(l_c, '/')) )
		{
		*l_s	= '\0';

		if ( a_create && mkdirat(l_fd, l_c, 0700) && (errno != EEXIST) )
			{
			*l_s	= '/';
			close(l_fd);

			return	-1;
			}

		l_next	= openat(l_fd, l_c, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		*l_s	= '/';

		close(l_fd);
		l_fd	= l_next;
		l_c	= l_s + 1;
		}

	*a_last	= l_c;

	return	l_fd;
}


static	void	s_vbkx$defer	(
	const	char *		a_name,
	const	VBKX$ENT *	a_ent
			)
{
	if ( s_ndirs == s_szdirs )
		{
		size_t		l_new = s_szdirs ? (s_szdirs * 2) : 256;
		VBKX$DIR *	l_p = realloc(s_dirs, l_new * sizeof(VBKX$DIR));

		if ( !l_p )
			return;

		s_dirs		= l_p;
		s_szdirs	= l_new;
		}

	if ( (s_dirs [s_ndirs].path = strdup(a_name)) )
		{
		s_dirs [s_ndirs].mode	= a_ent->mode;
		s_dirs [s_ndirs].mtime	= a_ent->mtime;
		s_dirs [s_ndirs].atime	= a_ent->atime;
		s_ndirs++;
		}
}


/*
**  The modes and times of the directories, deepest first: a file made in
**  a directory changes its times, a read-only one takes no more files
*/
static	void	s_vbkx$dirs	(void)
{
char *	l_last;
int	l_pfd, l_fd;

	while ( s_ndirs-- )
		{
		VBKX$DIR *	l_d = &s_dirs [s_ndirs];

		if ( 0 <= (l_pfd = s_vbkx$parent(l_d->path, 0, &l_last)) )
			{
			if ( 0 <= (l_fd = openat(l_pfd, l_last, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) )
				{
				struct timespec	l_ts [2] = { { l_d->atime.sec, l_d->atime.nsec }, { l_d->mtime.sec, l_d->mtime.nsec } };

				if ( fchmod(l_fd, (mode_t) (l_d->mode & 07777)) )
					s_vbkx$attrerr(l_d->path, "mode");

				if ( futimens(l_fd, l_ts) )
					s_vbkx$attrerr(l_d->path, "times");

				close(l_fd);
				}

			close(l_pfd);
			}

		free(l_d->path);
		}

	free(s_dirs);
	s_dirs	= NULL;
	s_ndirs	= s_szdirs = 0;
}


static	void	s_vbkx$seen	(
		uint32_t	a_fileno
			)
{
uint32_t	l_byte = a_fileno / 8;

	if ( l_byte >= s_seensz )
		{
		uint32_t	l_new = (l_byte + 1) * 2;
		uint8_t *	l_p = realloc(s_seen, l_new);

		if ( !l_p )
			return;

		memset(l_p + s_seensz, 0, l_new - s_seensz);
		s_seen	 = l_p;
		s_seensz = l_new;
		}

	s_seen [l_byte] |= (uint8_t) (1 << (a_fileno % 8));
}


static	int	s_vbkx$isseen	(
		uint32_t	a_fileno
			)
{
	return	((a_fileno / 8) < s_seensz) && (s_seen [a_fileno / 8] & (1 << (a_fileno % 8)));
}


/*
**  The file being put back: one at a time
*/
typedef struct vbkx_out_t
{
	VBKX$ENT	ent;
	uint8_t *	body;			/* The FILE record, copied			*/
	uint32_t	bodysz;
	char		name [VBKX$K_SZ_PATH];	/* Where it goes, relative to the output	*/
	int		fd;			/* Data goes here; -1 - nowhere (test, skipped)	*/
	int		active;
	int		tostd;			/* The standard output: holes are written	*/
	uint64_t	pos;			/* ... up to here				*/
	uint32_t	crc;
	int		damaged;
} VBKX$OUT;


/*
**  A file begins: made as what it is, or - test, not chosen - only
**  followed to its end.  <a_as> - the name to put it under, NULL - its own.
*/
static	void	s_vbkx$begin	(
		VBKX$OUT *	a_out,
	const	uint8_t *	a_body,
		uint32_t	a_len,
	const	char *		a_as,
		int		a_make
			)
{
VBKX$ENT *	l_e = &a_out->ent;
char		l_name [VBKX$K_SZ_PATH], l_tgt [VBKX$K_SZ_PATH], *l_last, *l_tlast;
int		l_pfd = -1, l_tfd, l_rc = 0;

	a_out->active	= 0;
	a_out->fd	= -1;
	a_out->crc	= 0;
	a_out->pos	= 0;
	a_out->damaged	= 0;

	if ( a_len > a_out->bodysz )
		{
		uint8_t *	l_p = realloc(a_out->body, a_len);

		if ( !l_p )
			return;

		a_out->body	= l_p;
		a_out->bodysz	= a_len;
		}

	memcpy(a_out->body, a_body, a_len);

	if ( !(1 & s_vbkx$parse(a_out->body, a_len, l_e)) )
		{
		s_vbkx$msg("a FILE record that makes no sense is skipped");
		s_bad	= 1;

		return;
		}

	s_vbkx$seen(l_e->fileno);

	if ( a_as )
		snprintf(a_out->name, sizeof(a_out->name), "%s", a_as);
	else	snprintf(a_out->name, sizeof(a_out->name), "%.*s", (int) l_e->pathlen, l_e->path);

	a_out->active	= 1;

	/* A test, or the standard output: only the data of a regular file is followed */
	if ( !a_make || a_out->tostd )
		{
		if ( l_e->ftype != VBK$K_FT_REG )
			a_out->active	= 0;

		return;
		}

	if ( !s_vbkx$nameok(a_out->name, strlen(a_out->name)) )
		{
		s_vbkx$msg("%s: a name that leads out of the output directory, not extracted", a_out->name);
		a_out->active	= 0;
		s_bad		= 1;

		return;
		}

	strcpy(l_name, a_out->name);

	if ( 0 > (l_pfd = s_vbkx$parent(l_name, 1, &l_last)) )
		{
		s_vbkx$msg("%s: %s (a directory on the way cannot be made, or is a link)", a_out->name, strerror(errno));
		a_out->active	= 0;
		s_bad		= 1;

		return;
		}

	if ( l_e->ftype == VBK$K_FT_DIR )
		{
		if ( mkdirat(l_pfd, l_last, 0700) && (errno != EEXIST) )
			l_rc	= -1;
		else	s_vbkx$defer(a_out->name, l_e);

		goto	l_done;
		}

	/* Something there: kept, unless -f - a directory is never removed */
	if ( !faccessat(l_pfd, l_last, F_OK, AT_SYMLINK_NOFOLLOW) )
		{
		if ( !s_force )
			{
			s_vbkx$msg("%s exists, not extracted (-f to overwrite)", a_out->name);
			a_out->active	= 0;
			s_bad		= 1;
			close(l_pfd);

			return;
			}

		unlinkat(l_pfd, l_last, 0);
		}

	switch ( l_e->ftype )
		{
		case	VBK$K_FT_REG:
			if ( 0 > (a_out->fd = openat(l_pfd, l_last, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)) )
				l_rc	= -1;
			break;

		case	VBK$K_FT_SYMLINK:
			snprintf(l_tgt, sizeof(l_tgt), "%.*s", (int) l_e->linklen, l_e->link ? l_e->link : "");
			l_rc	= symlinkat(l_tgt, l_pfd, l_last);
			break;

		case	VBK$K_FT_HARDLINK:
			snprintf(l_tgt, sizeof(l_tgt), "%.*s", (int) l_e->linklen, l_e->link ? l_e->link : "");

			if ( !s_vbkx$nameok(l_tgt, strlen(l_tgt)) || (0 > (l_tfd = s_vbkx$parent(l_tgt, 0, &l_tlast))) )
				{
				l_rc	= -1;
				errno	= ENOENT;
				break;
				}

			l_rc	= linkat(l_tfd, l_tlast, l_pfd, l_last, 0);
			close(l_tfd);
			break;

		case	VBK$K_FT_FIFO:
			l_rc	= mkfifoat(l_pfd, l_last, 0600);
			break;

		case	VBK$K_FT_CHR:
		case	VBK$K_FT_BLK:
			if ( !s_root )
				{
				s_vbkx$msg("%s: a device file, made by root only", a_out->name);
				a_out->active	= 0;
				close(l_pfd);

				return;
				}

			l_rc	= mknodat(l_pfd, l_last, ((l_e->ftype == VBK$K_FT_CHR) ? S_IFCHR : S_IFBLK) | 0600,
					makedev((unsigned) (l_e->rdev >> 32), (unsigned) (l_e->rdev & 0xFFFFFFFF)));
			break;

		default:
			/* A socket is made by its server, not by a restore */
			a_out->active	= 0;
			close(l_pfd);

			return;
		}

l_done:
	if ( l_rc )
		{
		s_vbkx$msg("%s: %s", a_out->name, strerror(errno));
		a_out->active	= 0;
		s_bad		= 1;
		}
	else if ( (l_e->ftype != VBK$K_FT_DIR) && (l_e->ftype != VBK$K_FT_REG) && (l_e->ftype != VBK$K_FT_HARDLINK) )
		{
		/* What has no data is complete now */
		struct timespec	l_ts [2] = { { l_e->atime.sec, l_e->atime.nsec }, { l_e->mtime.sec, l_e->mtime.nsec } };

		if ( s_root && fchownat(l_pfd, l_last, l_e->uid, l_e->gid, AT_SYMLINK_NOFOLLOW) )
			s_vbkx$attrerr(a_out->name, "owner");

		if ( (l_e->ftype != VBK$K_FT_SYMLINK) && fchmodat(l_pfd, l_last, (mode_t) (l_e->mode & 07777), 0) )
			s_vbkx$attrerr(a_out->name, "mode");

		if ( utimensat(l_pfd, l_last, l_ts, AT_SYMLINK_NOFOLLOW) )
			s_vbkx$attrerr(a_out->name, "times");
		}

	if ( l_e->ftype != VBK$K_FT_REG )
		a_out->active	= 0;

	close(l_pfd);
}


/*
**  Zeros up to <a_to> on the standard output: the holes of a sparse file
*/
static	void	s_vbkx$zeros	(
		VBKX$OUT *	a_out,
		uint64_t	a_to
			)
{
static	const uint8_t	l_zero [65536];

	while ( a_out->pos < a_to )
		{
		size_t	l_n = ((a_to - a_out->pos) < sizeof(l_zero)) ? (size_t) (a_to - a_out->pos) : sizeof(l_zero);

		if ( 1 != fwrite(l_zero, l_n, 1, stdout) )
			return;

		a_out->pos	+= l_n;
		}
}


/*
**  A DATA or DATAZ record of the file being put back; a DATAZ that does
**  not decompress leaves it incomplete
*/
static	void	s_vbkx$data	(
		VBKX$OUT *	a_out,
		uint16_t	a_type,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
static	uint8_t	s_zbuf [VBK$K_MAXDATA];
const uint8_t *	l_data;
uint64_t	l_off;
uint32_t	l_n, l_fileno;

	if ( !a_out->active )
		return;

	if ( STS$K_SUCCESS != vbk$data_get(a_type, a_body, a_len, s_zbuf, &l_fileno, &l_off, &l_data, &l_n) )
		{
		s_vbkx$msg("%s: a data record that makes no sense", a_out->name);
		a_out->damaged	= 1;

		return;
		}

	if ( l_fileno != a_out->ent.fileno )
		return;

	a_out->crc = $VBK_CRC(a_out->crc, l_data, l_n);

	if ( a_out->tostd )
		{
		s_vbkx$zeros(a_out, l_off);

		if ( (a_out->pos == l_off) && (1 == fwrite(l_data, l_n, 1, stdout)) )
			a_out->pos	+= l_n;
		}
	else if ( (a_out->fd >= 0) && (pwrite(a_out->fd, l_data, l_n, (off_t) l_off) != (ssize_t) l_n) )
		{
		s_vbkx$msg("%s: %s", a_out->name, strerror(errno));
		a_out->damaged	= 1;
		}
}


/*
**  The end of a regular file: its size, its checksum, its attributes.
**  <a_body> NULL - it ends without its FEND (lost, or the stream ends).
*/
static	void	s_vbkx$end	(
		VBKX$OUT *	a_out,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
VBKX$ENT *	l_e = &a_out->ent;
uint32_t	l_pos = 0, l_vlen, l_crc = 0, l_fileno = 0;
uint64_t	l_size = l_e->size;
uint8_t		l_status = 0;
uint16_t	l_tag;
const uint8_t *	l_val;
int		l_hascrc = 0;

	if ( !a_out->active )
		return;

	a_out->active	= 0;

	if ( a_body )
		while ( 1 & vbk$tlv_next(a_body, a_len, &l_pos, &l_tag, &l_vlen, &l_val) )
			switch ( l_tag )
				{
				case	VBK$K_TAG_FILENO:	l_fileno = (uint32_t) vbk$tlv_getu(l_vlen, l_val);		break;
				case	VBK$K_TAG_SIZE:		l_size	 = vbk$tlv_getu(l_vlen, l_val);			break;
				case	VBK$K_TAG_CRC:		l_crc	 = (uint32_t) vbk$tlv_getu(l_vlen, l_val);
								l_hascrc = 1;							break;
				case	VBK$K_TAG_STATUS:	l_status = (uint8_t) vbk$tlv_getu(l_vlen, l_val);		break;
				}

	if ( !a_body || (l_fileno != l_e->fileno) )
		a_out->damaged	= 1;
	else if ( l_hascrc && (l_crc != a_out->crc) )
		{
		s_vbkx$msg("%s: the checksum does not match", a_out->name);
		a_out->damaged	= 1;
		}

	if ( a_out->damaged )
		{
		s_vbkx$msg("%s is incomplete: its data was lost in bad blocks", a_out->name);
		s_bad	= 1;
		}
	else if ( l_status == VBK$K_FS_CHANGED )
		s_vbkx$msg("%s changed while it was saved: the copy may be a mix", a_out->name);
	else if ( l_status == VBK$K_FS_READERR )
		s_vbkx$msg("%s could not be read whole when it was saved", a_out->name);

	if ( a_out->tostd )
		{
		s_vbkx$zeros(a_out, l_size);

		return;
		}

	if ( a_out->fd < 0 )
		return;

	{
	struct timespec	l_ts [2] = { { l_e->atime.sec, l_e->atime.nsec }, { l_e->mtime.sec, l_e->mtime.nsec } };

	if ( ftruncate(a_out->fd, (off_t) l_size) )
		s_vbkx$msg("%s: %s", a_out->name, strerror(errno));

	if ( s_root && fchown(a_out->fd, l_e->uid, l_e->gid) )
		s_vbkx$attrerr(a_out->name, "owner");

	if ( fchmod(a_out->fd, (mode_t) (l_e->mode & 07777)) )
		s_vbkx$attrerr(a_out->name, "mode");

	if ( futimens(a_out->fd, l_ts) )
		s_vbkx$attrerr(a_out->name, "times");
	}

	if ( close(a_out->fd) )
		{
		s_vbkx$msg("%s: %s", a_out->name, strerror(errno));
		s_bad	= 1;
		}

	a_out->fd	= -1;
}


/*
**  The FILENO of a FILE record, 0 - none
*/
static	uint32_t	s_vbkx$fileno	(
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
uint32_t	l_pos = 0, l_vlen;
uint16_t	l_tag;
const uint8_t *	l_val;

	while ( 1 & vbk$tlv_next(a_body, a_len, &l_pos, &l_tag, &l_vlen, &l_val) )
		if ( l_tag == VBK$K_TAG_FILENO )
			return	(uint32_t) vbk$tlv_getu(l_vlen, l_val);

	return	0;
}


/*
**  Take the records of the stream from where it stands: every file (one
**  pass), or the one file <a_one> - a FILENO - that begins here.  When
**  its block is lost, the reader stands at the next record that can be
**  had: the FILE record of another file is not taken for it.
*/
static	void	s_vbkx$stream	(
		VBK$RCTX *	a_rctx,
		VBKX$OUT *	a_out,
		int		a_make,
		uint32_t	a_one,
	const	char *		a_as
			)
{
const uint8_t *	l_body;
uint32_t	l_len;
uint16_t	l_type;
int		l_files = 0;

	while ( 1 & vbk$rd_next(a_rctx, &l_type, &l_body, &l_len, NULL) )
		{
		if ( a_rctx->resync && a_out->active )
			a_out->damaged	= 1;

		if ( l_type == VBK$K_RT_FILE )
			{
			if ( a_out->active )
				s_vbkx$end(a_out, NULL, 0);

			if ( a_one && (l_files++ || (s_vbkx$fileno(l_body, l_len) != a_one)) )
				return;

			s_vbkx$begin(a_out, l_body, l_len, a_as, a_make);
			}
		else if ( (l_type == VBK$K_RT_DATA) || (l_type == VBK$K_RT_DATAZ) )
			s_vbkx$data(a_out, l_type, l_body, l_len);
		else if ( l_type == VBK$K_RT_FEND )
			{
			s_vbkx$end(a_out, l_body, l_len);

			if ( a_one )
				return;
			}
		else if ( (l_type == VBK$K_RT_CATALOG) || (l_type == VBK$K_RT_END) )
			break;
		}

	if ( a_out->active )
		s_vbkx$end(a_out, NULL, 0);
}


/*
**  The catalog, every entry with a copy of its own; NULL - none, or
**  damaged (<*a_hole>: it has a hole, what is there is returned)
*/
static	VBKX$ENT *	s_vbkx$catalog	(
		VBK$RCTX *	a_rctx,
		size_t *	a_n,
		int *		a_hole
			)
{
VBKX$ENT *	l_ents = NULL;
size_t		l_n = 0, l_sz = 0;
VBK$LOC		l_loc = {0};
const uint8_t *	l_val, *l_body;
uint32_t	l_pos = 0, l_vlen, l_len;
uint16_t	l_tag, l_type;
int		l_status;

	*a_n	= 0;
	*a_hole	= 0;

	if ( !a_rctx->trailer )
		return	NULL;

	while ( 1 & vbk$tlv_next(a_rctx->trailer, a_rctx->trllen, &l_pos, &l_tag, &l_vlen, &l_val) )
		switch ( l_tag )
			{
			case	VBK$K_TAG_CATVOL:	l_loc.vol = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_CATBLK:	l_loc.blk = vbk$tlv_getu(l_vlen, l_val);		break;
			case	VBK$K_TAG_CATOFF:	l_loc.off = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			}

	if ( STS$K_ERROR == (l_status = vbk$rd_seek(a_rctx, &l_loc)) )
		{
		*a_hole	= 1;

		return	NULL;
		}

	*a_hole	= (l_status == STS$K_WARN);

	while ( (1 & (l_status = vbk$rd_next(a_rctx, &l_type, &l_body, &l_len, NULL))) && (l_type != VBK$K_RT_END) )
		{
		*a_hole	|= a_rctx->resync;

		if ( l_type != VBK$K_RT_CATALOG )
			continue;

		for ( uint32_t l_off = 0; (l_off + 4) <= l_len; )
			{
			uint32_t	l_elen = vbk$get32(l_body + l_off);
			uint8_t *	l_copy;

			if ( (l_off + 4 + l_elen) > l_len )
				break;

			if ( l_n == l_sz )
				{
				size_t		l_new = l_sz ? (l_sz * 2) : 1024;
				VBKX$ENT *	l_p = realloc(l_ents, l_new * sizeof(VBKX$ENT));

				if ( !l_p )
					break;

				l_ents	= l_p;
				l_sz	= l_new;
				}

			if ( (l_copy = malloc(l_elen ? l_elen : 1)) )
				{
				memcpy(l_copy, l_body + l_off + 4, l_elen);

				if ( 1 & s_vbkx$parse(l_copy, l_elen, &l_ents [l_n]) )
					l_ents [l_n++].body = l_copy;
				else	free(l_copy);
				}

			l_off	+= 4 + l_elen;
			}
		}

	*a_hole	|= a_rctx->resync || !(1 & l_status);
	*a_n	= l_n;

	return	l_ents;
}


static	void	s_vbkx$freecat	(
		VBKX$ENT *	a_ents,
		size_t		a_n
			)
{
	for ( size_t i = 0; i < a_n; i++ )
		free(a_ents [i].body);

	free(a_ents);
}


/*
**  Does entry <a_e> answer one of the names given?  A name of a directory
**  answers for what is below it; no names - everything.
*/
static	int	s_vbkx$wanted	(
	const	VBKX$ENT *	a_e,
		char **		a_names,
		int		a_nnames
			)
{
	if ( !a_nnames )
		return	1;

	for ( int i = 0; i < a_nnames; i++ )
		{
		size_t	l_len = strlen(a_names [i]);

		while ( l_len && (a_names [i] [l_len - 1] == '/') )
			l_len--;

		if ( (a_e->pathlen >= l_len) && !memcmp(a_e->path, a_names [i], l_len)
			&& ((a_e->pathlen == l_len) || (a_e->path [l_len] == '/')) )
			return	1;
		}

	return	0;
}


static	char	s_vbkx$tchar	(
		uint8_t		a_ftype
			)
{
	return	"?-dlhcbps" [(a_ftype <= VBK$K_FT_SOCK) ? a_ftype : 0];
}


static	void	s_vbkx$line	(
	const	VBKX$ENT *	a_e
			)
{
struct tm	l_tm;
time_t		l_t = (time_t) a_e->mtime.sec;
char		l_ts [32];

	localtime_r(&l_t, &l_tm);
	strftime(l_ts, sizeof(l_ts), "%Y-%m-%d %H:%M:%S", &l_tm);

	printf("%s %12llu %c%04o %.*s", l_ts, (unsigned long long) a_e->size, s_vbkx$tchar(a_e->ftype), a_e->mode & 07777,
		(int) a_e->pathlen, a_e->path);

	if ( a_e->link && ((a_e->ftype == VBK$K_FT_SYMLINK) || (a_e->ftype == VBK$K_FT_HARDLINK)) )
		printf(" %s %.*s", (a_e->ftype == VBK$K_FT_SYMLINK) ? "->" : "link to", (int) a_e->linklen, a_e->link);

	putchar('\n');
}


/*
**  l: the catalog; without one, the stream
*/
static	int	s_vbkx$list	(
		VBK$RCTX *	a_rctx
			)
{
VBKX$ENT *	l_ents, l_e;
size_t		l_n;
uint64_t	l_npres = 0;
const uint8_t *	l_body;
uint32_t	l_len;
uint16_t	l_type;
int		l_hole;

	if ( (l_ents = s_vbkx$catalog(a_rctx, &l_n, &l_hole)) && !l_hole )
		{
		for ( size_t i = 0; i < l_n; i++ )
			if ( l_ents [i].status == VBK$K_FS_PRESENT )
				l_npres++;
			else	s_vbkx$line(&l_ents [i]);

		if ( l_npres )
			fprintf(stderr, "vbkx: and %llu unchanged files listed as present, not saved here\n", (unsigned long long) l_npres);

		s_vbkx$freecat(l_ents, l_n);

		return	s_bad ? 1 : 0;
		}

	s_vbkx$freecat(l_ents, l_n);
	s_vbkx$msg("%s: %s - the whole saveset is read", s_spec, a_rctx->trailer ? "the catalog is damaged" : "no catalog");

	vbk$rd_rewind(a_rctx);

	while ( 1 & vbk$rd_next(a_rctx, &l_type, &l_body, &l_len, NULL) )
		{
		if ( (l_type == VBK$K_RT_CATALOG) || (l_type == VBK$K_RT_END) )
			break;

		if ( (l_type == VBK$K_RT_FILE) && (1 & s_vbkx$parse(l_body, l_len, &l_e)) )
			s_vbkx$line(&l_e);
		}

	return	1;
}


/*
**  After a pass that lost blocks: the files of the catalog never met
*/
static	void	s_vbkx$lost	(
		VBK$RCTX *	a_rctx,
		char **		a_names,
		int		a_nnames
			)
{
VBKX$ENT *	l_ents;
size_t		l_n;
int		l_hole;

	l_ents	= s_vbkx$catalog(a_rctx, &l_n, &l_hole);

	for ( size_t i = 0; i < l_n; i++ )
		if ( (l_ents [i].status != VBK$K_FS_PRESENT) && !s_vbkx$isseen(l_ents [i].fileno) && s_vbkx$wanted(&l_ents [i], a_names, a_nnames) )
			s_vbkx$msg("%.*s was not extracted: its records were lost in bad blocks", (int) l_ents [i].pathlen, l_ents [i].path);

	if ( !l_ents || l_hole )
		s_vbkx$msg("%s: %s - files missing from the output cannot all be named", s_spec,
			a_rctx->trailer ? "the catalog is damaged" : "there is no catalog");

	s_vbkx$freecat(l_ents, l_n);
}


/*
**  x and t: the whole saveset in one pass, or the names through the catalog
*/
static	int	s_vbkx$extract	(
		VBK$RCTX *	a_rctx,
		char **		a_names,
		int		a_nnames,
		int		a_make,
		int		a_tostd
			)
{
VBKX$OUT	l_out = { .fd = -1, .tostd = a_tostd };
VBKX$ENT *	l_ents;
size_t		l_n;
int		l_hole, l_found = 0;

	if ( !a_nnames )
		{
		s_vbkx$stream(a_rctx, &l_out, a_make, 0, NULL);


		if ( s_bad )
			s_vbkx$lost(a_rctx, NULL, 0);
		}
	else	{
		if ( !(l_ents = s_vbkx$catalog(a_rctx, &l_n, &l_hole)) )
			{
			s_vbkx$msg("%s: %s - names cannot be looked up; give no names to extract it all", s_spec,
				a_rctx->trailer ? "the catalog cannot be read" : "there is no catalog");

			return	2;
			}

		for ( size_t i = 0; i < l_n; i++ )
			{
			VBKX$ENT *	l_e = &l_ents [i];
			char		l_as [VBKX$K_SZ_PATH];
			const char *	l_asp = NULL;

			if ( (l_e->status == VBK$K_FS_PRESENT) || !s_vbkx$wanted(l_e, a_names, a_nnames) )
				continue;

			l_found++;

			/* A further name whose first one is not taken: the data of the first, under this name */
			if ( (l_e->ftype == VBK$K_FT_HARDLINK) && l_e->link )
				{
				VBKX$ENT	l_t = { .path = l_e->link, .pathlen = l_e->linklen };

				if ( a_tostd || !s_vbkx$wanted(&l_t, a_names, a_nnames) )
					for ( size_t j = 0; j < l_n; j++ )
						if ( (l_ents [j].pathlen == l_e->linklen) && !memcmp(l_ents [j].path, l_e->link, l_e->linklen) )
							{
							snprintf(l_as, sizeof(l_as), "%.*s", (int) l_e->pathlen, l_e->path);
							l_asp	= l_as;
							l_e	= &l_ents [j];
							break;
							}
				}

			if ( a_tostd && (l_e->ftype != VBK$K_FT_REG) )
				{
				s_vbkx$msg("%.*s is not a regular file", (int) l_e->pathlen, l_e->path);
				s_vbkx$freecat(l_ents, l_n);

				return	2;
				}

			if ( STS$K_ERROR == vbk$rd_seek(a_rctx, &l_e->loc) )
				{
				s_vbkx$msg("%.*s was not extracted: its records cannot be reached", (int) l_e->pathlen, l_e->path);
				s_bad	= 1;
				continue;
				}

			s_vbkx$stream(a_rctx, &l_out, a_make, l_e->fileno ? l_e->fileno : UINT32_MAX, l_asp);

			if ( !s_vbkx$isseen(l_e->fileno) )
				{
				s_vbkx$msg("%.*s was not extracted: its records were lost in bad blocks", (int) l_e->pathlen, l_e->path);
				s_bad	= 1;
				}

			if ( a_tostd )
				break;
			}

		s_vbkx$freecat(l_ents, l_n);

		if ( !l_found )
			{
			s_vbkx$msg("no such file in the saveset");

			return	2;
			}
		}

	free(l_out.body);

	if ( a_make )
		s_vbkx$dirs();

	return	s_bad ? 1 : 0;
}


static	int	s_vbkx$usage	(void)
{
	fprintf(stderr,
		"VBKX " __IDENT__ " - the stand-alone extractor of VBACKUP savesets\n"
		"\n"
		"  vbkx l saveset                       list the files\n"
		"  vbkx x saveset [-C dir] [-f] [name...]  extract (all, or the names given)\n"
		"  vbkx p saveset name                  write one file to the standard output\n"
		"  vbkx t saveset                       test: read it all, check the checksums\n"
		"\n"
		"  -C dir  extract into dir (made if missing), default the current one\n"
		"  -f      overwrite files that are there\n"
		"\n"
		"Completion: 0 - done; 1 - something damaged or not done; 2 - not usable.\n");

	return	2;
}


int	main	(
		int		argc,
		char **		argv
			)
{
VBK$RCTX	l_rctx = {0};
char		l_op, **l_names;
int		l_nnames = 0, l_status, l_rc;

	if ( (argc < 3) || (strlen(argv [1]) != 1) || !strchr("lxpt", argv [1] [0]) )
		return	s_vbkx$usage();

	l_op	= argv [1] [0];
	s_spec	= argv [2];
	s_root	= !geteuid();

	if ( !(l_names = calloc((size_t) argc, sizeof(char *))) )
		return	2;

	for ( int i = 3; i < argc; i++ )
		{
		if ( !strcmp(argv [i], "-C") && ((i + 1) < argc) && (l_op == 'x') )
			s_outdir = argv [++i];
		else if ( !strcmp(argv [i], "-f") && (l_op == 'x') )
			s_force	= 1;
		else	l_names [l_nnames++] = argv [i];
		}

	if ( ((l_op == 'p') && (l_nnames != 1)) || (((l_op == 'l') || (l_op == 't')) && l_nnames) )
		return	s_vbkx$usage();

	if ( !(1 & (l_status = vbk$rd_open(&l_rctx, s_spec, s_vbkx$event, NULL))) )
		{
		if ( l_status == STS$K_WARN )
			s_vbkx$msg("%s is not a saveset", s_spec);
		else	s_vbkx$msg("%s: %s", s_spec, strerror(l_rctx.err ? l_rctx.err : errno));

		return	2;
		}

	switch ( l_op )
		{
		case	'l':
			l_rc	= s_vbkx$list(&l_rctx);
			break;

		case	'x':
			if ( mkdir(s_outdir, 0755) && (errno != EEXIST) )
				{
				s_vbkx$msg("%s: %s", s_outdir, strerror(errno));
				l_rc	= 2;
				break;
				}

			if ( 0 > (s_outfd = open(s_outdir, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) )
				{
				s_vbkx$msg("%s: %s", s_outdir, strerror(errno));
				l_rc	= 2;
				break;
				}

			umask(0);
			l_rc	= s_vbkx$extract(&l_rctx, l_names, l_nnames, 1, 0);
			close(s_outfd);
			break;

		case	'p':
			l_rc	= s_vbkx$extract(&l_rctx, l_names, l_nnames, 0, 1);

			if ( fflush(stdout) )
				l_rc	= 2;
			break;

		default:
			l_rc	= s_vbkx$extract(&l_rctx, NULL, 0, 0, 0);

			if ( !l_rc )
				printf("%s: all files read, all checksums match\n", s_spec);
		}

	vbk$rd_close(&l_rctx);
	free(l_names);
	free(s_seen);

	return	l_rc;
}
