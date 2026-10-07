#define	__MODULE__	"VBKX"
#define	__IDENT__	"X01-21"
#define	__REV__		"1.21.0"

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
**		    vbkx l saveset [-m]			list, from the catalog (-m:
**							no "-> target", for programs)
**		    vbkx x saveset [-C dir] [-f] [-j] [name...] extract (-j: no dirs)
**		    vbkx p saveset name			a file to stdout
**		    vbkx t saveset			test: read all, check CRCs
**		    ... [-k keyfile] [-n]		an encrypted saveset; -n: never
**							ask on the terminal (programs)
**
**		A saveset of OpenVMS BACKUP is known by its first block and
**		taken alike (LIB/VBKVMS.C): l, x, p, t by the Linux names of
**		its files, the texts made texts with LF (doc/vmsbackup.md).
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
**		On Windows (vbkx.exe) the names are UTF-8 in the saveset and
**		UTF-16 in the system, put under \\?\ - long paths are fine.
**		Put back: data, sizes, times, the read-only attribute (no write
**		bit for the owner), directories, hard links; symbolic links
**		where the system allows them (developer mode, or the right to
**		make them), else said.  Not made: FIFOs, device files, owners,
**		modes beyond read-only.  A name Windows cannot hold - a
**		component with <>:"\|?* or a control, a device name (CON,
**		PRN, AUX, NUL, COM1-9, LPT1-9, with an extension too), one
**		ending in a dot or a space - is not extracted, and said.  A
**		reparse point (a link, a junction) on the way is refused.
**
**		Build on Linux: with the product (CMake), or by hand -
**
**		    gcc -O2 -D_GNU_SOURCE -Ilib -I/usr/local/include \
**			tools/vbkx.c lib/vbkfmt.c lib/vbkrd.c lib/vbklz4.c lib/vbkcrp.c lib/vbkvms.c lib/vbkrs.c \
**			/usr/local/lib/libstarlet.a -static -o vbkx
**
**		Build on Windows / cross, from the top of the source tree -
**		no StarLet, no CMake, one command:
**
**		    x86_64-w64-mingw32-gcc -O2 -Ilib -o vbkx.exe tools/vbkx.c \
**			lib/vbkfmt.c lib/vbkrd.c lib/vbklz4.c lib/vbkcrp.c lib/vbkvms.c lib/vbkrs.c -static -lshell32
**
**		(make -f tools/Makefile.win does the same; on Windows itself
**		gcc of MinGW-w64 or MSYS2 takes the same line.)
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-21		 7-OCT-2026	RRL
**		Version 3: the SOLID records opened by the reader; a file whose place
**		is a SOLID found in there past the files before it.
**
**	X01-14		 5-OCT-2026	RRL
**		Savesets of version 2 (/PARITY) through VBKRD.C and VBKRS.C; the
**		event PARITY said.
**
**	X01-13		 5-OCT-2026	RRL
**		The savesets of OpenVMS BACKUP: l, x, p, t (LIB/VBKVMS.C).
**
**	X01-08		 5-OCT-2026	RRL
**		"-": a saveset from a pipe - listed and extracted as it is read;
**		names refused (its catalog is at its end).
**
**	X01-06		 5-OCT-2026	RRL
**		Encrypted savesets (format.md 6.10): -k keyfile, VBACKUP_KEY_FILE,
**		or the passphrase asked on the terminal (the console on Windows,
**		taken as UTF-8), -n never; a block whose authentication fails
**		is said.  Every message in the form of VBACKUP's: "File: name,
**		errno: n - words".  A stream that ends before its catalog (a saveset cut
**		down to its VHDR) is no longer "all files read": said, code 1.
**
**	X01-04		 4-OCT-2026	RRL
**		DATAZ: the data compressed with /DATA_FORMAT=COMPRESSED.  l -m:
**		the listing without link targets, for MultiArc.  x -j: the files
**		by their last component, without directories.
**		Windows: the calls that make files gathered into one layer
**		(s_vbkx$os_*), a second one for Windows (UTF-16 names under
**		\\?\, reparse points refused, names Windows cannot hold
**		refused); the command line taken as UTF-16 and made UTF-8.
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
#include	<time.h>
#include	<sys/stat.h>

#ifdef	_WIN32
#include	<windows.h>
#include	<shellapi.h>
#include	<io.h>
#else
#include	<unistd.h>
#include	<termios.h>
#include	<sys/sysmacros.h>
#endif

#include	"vbkrd.h"
#include	"vbkos.h"
#include	"vbklz4.h"
#include	"vbkvms.h"

#define	VBKX$K_SZ_PATH	4096
#define	VBKX$K_WPATH	32768			/* A path of Windows, in UTF-16 units		*/

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
static	int		s_force, s_root, s_bad, s_bare, s_junk;

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
	s_vbkx$msg("File: %s, errno: %d - the %s cannot be set (%s)", a_name, errno, a_what, strerror(errno));
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
			s_vbkx$msg("Block: %llu, Volume: %u - was bad, rebuilt from its group", (unsigned long long) a_blk, a_vol);
			break;

		case	VBK$K_EV_LOST:
			s_vbkx$msg("Block: %llu, Volume: %u - is bad and cannot be rebuilt", (unsigned long long) a_blk, a_vol);
			s_bad	= 1;
			break;

		case	VBK$K_EV_MISSVOL:
			s_vbkx$msg("Volume: %u - is missing", a_vol);
			s_bad	= 1;
			break;

		case	VBK$K_EV_WRONGVOL:
			s_vbkx$msg("Volume: %u - belongs to another saveset, or is none", a_vol);
			break;

		case	VBK$K_EV_BADREC:
			s_vbkx$msg("Block: %llu, Volume: %u - an invalid record, skipped", (unsigned long long) a_blk, a_vol);
			s_bad	= 1;
			break;

		case	VBK$K_EV_BADTAG:
			s_vbkx$msg("Block: %llu, Volume: %u - is not what was written: its CRC is right, its authentication fails",
				(unsigned long long) a_blk, a_vol);
			break;

		case	VBK$K_EV_PARITY:
			s_vbkx$msg("Block: %llu, Volume: %u - the group beginning here does not agree with its parity: nothing of it is rebuilt",
				(unsigned long long) a_blk, a_vol);
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
**  The calls that make files, one set for each system.  Everything that
**  lies below the output directory is reached from it, one component at
**  a time, and refused when a component is a link: a parent is an open
**  directory on Linux, a checked path on Windows.
*/
#ifdef	_WIN32

typedef	wchar_t *	VBKX$PAR;		/* The directory: a path of its own, NULL - none	*/
#define	VBKX$K_NOPAR	NULL

static	wchar_t	s_outw [VBKX$K_WPATH];		/* \\?\ and the full path of the output directory	*/

/*
**  An errno for the last error of Windows: what strerror() can say
*/
static	void	s_vbkx$os_errno	(void)
{
	switch ( GetLastError() )
		{
		case	ERROR_FILE_EXISTS:
		case	ERROR_ALREADY_EXISTS:		errno = EEXIST;		break;
		case	ERROR_FILE_NOT_FOUND:
		case	ERROR_PATH_NOT_FOUND:		errno = ENOENT;		break;
		case	ERROR_ACCESS_DENIED:
		case	ERROR_PRIVILEGE_NOT_HELD:	errno = EACCES;		break;
		case	ERROR_INVALID_NAME:		errno = EINVAL;		break;
		case	ERROR_DISK_FULL:		errno = ENOSPC;		break;
		case	ERROR_FILENAME_EXCED_RANGE:	errno = ENAMETOOLONG;	break;
		default:				errno = EIO;
		}
}

/*
**  A time of the saveset as a FILETIME: 100-ns steps since 1601
*/
static	FILETIME	s_vbkx$os_ft	(
	const	VBK$TIME *	a_t
			)
{
FILETIME	l_ft;
int64_t		l_v = (a_t->sec * 10000000LL) + (a_t->nsec / 100) + 116444736000000000LL;

	if ( l_v < 0 )
		l_v	= 0;

	l_ft.dwLowDateTime  = (DWORD) (l_v & 0xFFFFFFFF);
	l_ft.dwHighDateTime = (DWORD) ((uint64_t) l_v >> 32);

	return	l_ft;
}

/*
**  <a_dir> "\" <a_comp>, the component turned into UTF-16; 0 - it is not
**  valid UTF-8, or the path is too long
*/
static	int	s_vbkx$os_join	(
	const	wchar_t *	a_dir,
	const	char *		a_comp,
		wchar_t *	a_out
			)
{
size_t	l_len = wcslen(a_dir);

	if ( (l_len + 2) >= VBKX$K_WPATH )
		return	0;

	memcpy(a_out, a_dir, l_len * sizeof(wchar_t));
	a_out [l_len++] = L'\\';

	return	vbk$os_wide(a_comp, a_out + l_len, (int) (VBKX$K_WPATH - l_len));
}

/*
**  A name the file system of Windows can hold: no component with a
**  character it reserves (<>:"/\|?* and the controls), none that is a
**  device (CON, PRN, AUX, NUL, COM1-9, LPT1-9, with an extension too),
**  none ending in a dot or a space, and valid UTF-8 all through
*/
static	int	s_vbkx$winname	(
	const	char *		a_name
			)
{
static	const char *	l_dev [] = { "CON", "PRN", "AUX", "NUL", NULL };
wchar_t			l_w [VBKX$K_WPATH];

	if ( !vbk$os_wide(a_name, l_w, VBKX$K_WPATH) )
		return	0;

	for ( const char *l_c = a_name; *l_c; )
		{
		const char *	l_e = strchr(l_c, '/');
		size_t		l_n = l_e ? (size_t) (l_e - l_c) : strlen(l_c), l_base;

		for ( size_t i = 0; i < l_n; i++ )
			if ( ((unsigned char) l_c [i] < 32) || strchr("<>:\"\\|?*", l_c [i]) )
				return	0;

		if ( l_n && ((l_c [l_n - 1] == '.') || (l_c [l_n - 1] == ' ')) )
			return	0;

		/* The part before the first dot is what Windows takes for a device */
		for ( l_base = 0; (l_base < l_n) && (l_c [l_base] != '.'); l_base++ )
			;

		for ( int i = 0; l_dev [i]; i++ )
			if ( (l_base == 3) && !_strnicmp(l_c, l_dev [i], 3) )
				return	0;

		if ( (l_base == 4) && (!_strnicmp(l_c, "COM", 3) || !_strnicmp(l_c, "LPT", 3)) && (l_c [3] >= '1') && (l_c [3] <= '9') )
			return	0;

		l_c	+= l_n;

		if ( *l_c == '/' )
			l_c++;
		}

	return	1;
}

static	int	s_vbkx$os_outdir	(
	const	char *		a_dir
			)
{
wchar_t	l_w [VBKX$K_WPATH], l_full [VBKX$K_WPATH];
DWORD	l_n;

	if ( !vbk$os_wide(a_dir, l_w, VBKX$K_WPATH) )
		{
		errno	= EINVAL;

		return	-1;
		}

	if ( !CreateDirectoryW(l_w, NULL) && (GetLastError() != ERROR_ALREADY_EXISTS) )
		return	s_vbkx$os_errno(), -1;

	if ( !(l_n = GetFullPathNameW(l_w, VBKX$K_WPATH, l_full, NULL)) || (l_n >= (VBKX$K_WPATH - 8)) )
		return	s_vbkx$os_errno(), -1;

	while ( (l_n > 3) && (l_full [l_n - 1] == L'\\') )
		l_full [--l_n] = L'\0';

	/* \\?\: long paths, and no second guessing of the names by the system */
	if ( !wcsncmp(l_full, L"\\\\", 2) )
		_snwprintf(s_outw, VBKX$K_WPATH, L"\\\\?\\UNC\\%ls", l_full + 2);
	else	_snwprintf(s_outw, VBKX$K_WPATH, L"\\\\?\\%ls", l_full);

	return	0;
}

static	void	s_vbkx$os_outclose	(void)
{
}

static	int	s_vbkx$os_parent	(
		char *		a_name,
		int		a_create,
		char **		a_last,
		VBKX$PAR *	a_par
			)
{
wchar_t	l_cur [VBKX$K_WPATH], l_next [VBKX$K_WPATH];
char *	l_c = a_name, *l_s;
DWORD	l_attr;

	*a_par	= VBKX$K_NOPAR;
	wcscpy(l_cur, s_outw);

	while ( (l_s = strchr(l_c, '/')) )
		{
		int	l_ok;

		*l_s	= '\0';
		l_ok	= s_vbkx$os_join(l_cur, l_c, l_next);
		*l_s	= '/';

		if ( !l_ok )
			{
			errno	= EINVAL;

			return	-1;
			}

		if ( INVALID_FILE_ATTRIBUTES == (l_attr = GetFileAttributesW(l_next)) )
			{
			if ( !a_create || !CreateDirectoryW(l_next, NULL) )
				return	s_vbkx$os_errno(), -1;
			}
		else if ( l_attr & FILE_ATTRIBUTE_REPARSE_POINT )
			{
			errno	= ELOOP;

			return	-1;
			}
		else if ( !(l_attr & FILE_ATTRIBUTE_DIRECTORY) )
			{
			errno	= ENOTDIR;

			return	-1;
			}

		wcscpy(l_cur, l_next);
		l_c	= l_s + 1;
		}

	*a_last	= l_c;

	if ( !(*a_par = _wcsdup(l_cur)) )
		{
		errno	= ENOMEM;

		return	-1;
		}

	return	0;
}

static	void	s_vbkx$os_pclose	(
		VBKX$PAR	a_par
			)
{
	free(a_par);
}

static	int	s_vbkx$os_mkdir	(
		VBKX$PAR	a_par,
	const	char *		a_last
			)
{
wchar_t	l_p [VBKX$K_WPATH];
DWORD	l_attr;

	if ( !s_vbkx$os_join(a_par, a_last, l_p) )
		{
		errno	= EINVAL;

		return	-1;
		}

	if ( CreateDirectoryW(l_p, NULL) )
		return	0;

	/* There already: a directory will do, a link or a file will not */
	if ( (GetLastError() == ERROR_ALREADY_EXISTS) && (INVALID_FILE_ATTRIBUTES != (l_attr = GetFileAttributesW(l_p)))
		&& (l_attr & FILE_ATTRIBUTE_DIRECTORY) && !(l_attr & FILE_ATTRIBUTE_REPARSE_POINT) )
		return	0;

	errno	= EEXIST;

	return	-1;
}

static	int	s_vbkx$os_exists	(
		VBKX$PAR	a_par,
	const	char *		a_last
			)
{
wchar_t	l_p [VBKX$K_WPATH];

	return	s_vbkx$os_join(a_par, a_last, l_p) && (INVALID_FILE_ATTRIBUTES != GetFileAttributesW(l_p));
}

static	void	s_vbkx$os_remove	(
		VBKX$PAR	a_par,
	const	char *		a_last
			)
{
wchar_t	l_p [VBKX$K_WPATH];
DWORD	l_attr;

	if ( !s_vbkx$os_join(a_par, a_last, l_p) || (INVALID_FILE_ATTRIBUTES == (l_attr = GetFileAttributesW(l_p))) )
		return;

	/* A link to a directory is removed as a directory - the link, not what it points to */
	if ( l_attr & FILE_ATTRIBUTE_DIRECTORY )
		{
		if ( l_attr & FILE_ATTRIBUTE_REPARSE_POINT )
			RemoveDirectoryW(l_p);

		return;
		}

	SetFileAttributesW(l_p, FILE_ATTRIBUTE_NORMAL);
	DeleteFileW(l_p);
}

static	int	s_vbkx$os_creat	(
		VBKX$PAR	a_par,
	const	char *		a_last,
		wchar_t *	a_path
			)
{
	if ( !s_vbkx$os_join(a_par, a_last, a_path) )
		{
		errno	= EINVAL;

		return	-1;
		}

	return	_wopen(a_path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
}

/*
**  A symbolic link: made when the system lets an ordinary user (developer
**  mode) or the user has the right; else said, and the extraction goes on
*/
static	int	s_vbkx$os_symlink	(
	const	char *		a_tgt,
		VBKX$PAR	a_par,
	const	char *		a_last,
	const	char *		a_name
			)
{
wchar_t	l_p [VBKX$K_WPATH], l_t [VBKX$K_WPATH], l_abs [VBKX$K_WPATH];
DWORD	l_flags = 0x2, l_attr;		/* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */

	if ( !s_vbkx$os_join(a_par, a_last, l_p) || !vbk$os_wide(a_tgt, l_t, VBKX$K_WPATH) )
		{
		errno	= EINVAL;

		return	-1;
		}

	for ( wchar_t *l_c = l_t; *l_c; l_c++ )
		if ( *l_c == L'/' )
			*l_c = L'\\';

	/* A link to a directory must say so on Windows: looked at, when the target is relative and there */
	if ( (l_t [0] != L'\\') && !wcschr(l_t, L':') && ((wcslen(a_par) + wcslen(l_t) + 2) < VBKX$K_WPATH) )
		{
		_snwprintf(l_abs, VBKX$K_WPATH, L"%ls\\%ls", a_par, l_t);

		if ( (INVALID_FILE_ATTRIBUTES != (l_attr = GetFileAttributesW(l_abs))) && (l_attr & FILE_ATTRIBUTE_DIRECTORY) )
			l_flags |= 0x1;	/* SYMBOLIC_LINK_FLAG_DIRECTORY */
		}

	/* Made, and there as a link: a system that says yes and makes nothing (wine) is caught here */
	if ( CreateSymbolicLinkW(l_p, l_t, l_flags) && (INVALID_FILE_ATTRIBUTES != (l_attr = GetFileAttributesW(l_p)))
		&& (l_attr & FILE_ATTRIBUTE_REPARSE_POINT) )
		return	0;

	s_vbkx$msg("File: %s - a symbolic link, not made on this system", a_name);
	s_bad	= 1;

	return	1;
}

static	int	s_vbkx$os_link	(
		VBKX$PAR	a_tpar,
	const	char *		a_tlast,
		VBKX$PAR	a_par,
	const	char *		a_last
			)
{
wchar_t	l_p [VBKX$K_WPATH], l_t [VBKX$K_WPATH];

	if ( !s_vbkx$os_join(a_par, a_last, l_p) || !s_vbkx$os_join(a_tpar, a_tlast, l_t) )
		{
		errno	= EINVAL;

		return	-1;
		}

	return	CreateHardLinkW(l_p, l_t, NULL) ? 0 : (s_vbkx$os_errno(), -1);
}

static	int	s_vbkx$os_special	(
	const	VBKX$ENT *	a_e,
		VBKX$PAR	a_par,
	const	char *		a_last,
	const	char *		a_name
			)
{
	s_vbkx$msg("File: %s - %s, not made on Windows", a_name, (a_e->ftype == VBK$K_FT_FIFO) ? "a FIFO" : "a device file");
	s_bad	= 1;

	return	1;
}

/*
**  The times of what has no data - a symbolic link - on the link itself
*/
static	void	s_vbkx$os_attrs	(
		VBKX$PAR	a_par,
	const	char *		a_last,
	const	VBKX$ENT *	a_e,
	const	char *		a_name
			)
{
wchar_t		l_p [VBKX$K_WPATH];
FILETIME	l_at = s_vbkx$os_ft(&a_e->atime), l_mt = s_vbkx$os_ft(&a_e->mtime);
HANDLE		l_h;

	if ( !s_vbkx$os_join(a_par, a_last, l_p) )
		return;

	if ( INVALID_HANDLE_VALUE == (l_h = CreateFileW(l_p, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
				OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL)) )
		{
		s_vbkx$os_errno();
		s_vbkx$attrerr(a_name, "times");

		return;
		}

	if ( !SetFileTime(l_h, NULL, &l_at, &l_mt) )
		s_vbkx$os_errno(), s_vbkx$attrerr(a_name, "times");

	CloseHandle(l_h);
}

static	int	s_vbkx$os_pwrite	(
		int		a_fd,
	const	uint8_t *	a_data,
		uint32_t	a_n,
		uint64_t	a_off
			)
{
	if ( _lseeki64(a_fd, (__int64) a_off, SEEK_SET) < 0 )
		return	-1;

	while ( a_n )
		{
		int	l_rc = _write(a_fd, a_data, a_n);

		if ( l_rc <= 0 )
			return	-1;

		a_data	+= l_rc;
		a_n	-= (uint32_t) l_rc;
		}

	return	0;
}

/*
**  A regular file complete: its size, its times, read-only when its
**  owner may not write it - the one bit of the mode Windows has
*/
static	int	s_vbkx$os_fdend	(
		int		a_fd,
	const	wchar_t *	a_path,
	const	VBKX$ENT *	a_e,
		uint64_t	a_size,
	const	char *		a_name
			)
{
FILETIME	l_at = s_vbkx$os_ft(&a_e->atime), l_mt = s_vbkx$os_ft(&a_e->mtime);
int		l_rc;

	if ( _chsize_s(a_fd, (__int64) a_size) )
		s_vbkx$msg("File: %s, errno: %d - its size cannot be set (%s)", a_name, errno, strerror(errno));

	if ( !SetFileTime((HANDLE) _get_osfhandle(a_fd), NULL, &l_at, &l_mt) )
		s_vbkx$os_errno(), s_vbkx$attrerr(a_name, "times");

	l_rc	= _close(a_fd);

	if ( !(a_e->mode & 0200) && !SetFileAttributesW(a_path, FILE_ATTRIBUTE_READONLY) )
		s_vbkx$os_errno(), s_vbkx$attrerr(a_name, "mode");

	return	l_rc;
}

/*
**  The times of a directory, at the end (its mode has no meaning there)
*/
static	void	s_vbkx$os_dirattr	(
		VBKX$DIR *	a_d
			)
{
wchar_t		l_p [VBKX$K_WPATH];
FILETIME	l_at = s_vbkx$os_ft(&a_d->atime), l_mt = s_vbkx$os_ft(&a_d->mtime);
VBKX$PAR	l_par;
char *		l_last;
HANDLE		l_h;
DWORD		l_attr;

	if ( s_vbkx$os_parent(a_d->path, 0, &l_last, &l_par) )
		return;

	if ( s_vbkx$os_join(l_par, l_last, l_p) && (INVALID_FILE_ATTRIBUTES != (l_attr = GetFileAttributesW(l_p)))
		&& !(l_attr & FILE_ATTRIBUTE_REPARSE_POINT)
		&& (INVALID_HANDLE_VALUE != (l_h = CreateFileW(l_p, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL))) )
		{
		if ( !SetFileTime(l_h, NULL, &l_at, &l_mt) )
			s_vbkx$os_errno(), s_vbkx$attrerr(a_d->path, "times");

		CloseHandle(l_h);
		}

	s_vbkx$os_pclose(l_par);
}

static	void	s_vbkx$os_localtime	(
		time_t		a_t,
		struct tm *	a_tm
			)
{
	if ( localtime_s(a_tm, &a_t) )
		memset(a_tm, 0, sizeof(*a_tm));
}

#else	/* Linux */

typedef	int		VBKX$PAR;		/* The directory, open; -1 - none		*/
#define	VBKX$K_NOPAR	(-1)

static	int	s_outfd = -1;			/* The output directory				*/

static	int	s_vbkx$os_outdir	(
	const	char *		a_dir
			)
{
	if ( mkdir(a_dir, 0755) && (errno != EEXIST) )
		return	-1;

	if ( 0 > (s_outfd = open(a_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) )
		return	-1;

	umask(0);

	return	0;
}

static	void	s_vbkx$os_outclose	(void)
{
	if ( s_outfd >= 0 )
		close(s_outfd);

	s_outfd	= -1;
}

/*
**  Open the directory a name lies in, below the output directory, one
**  component at a time with O_NOFOLLOW - a symbolic link on the way makes
**  it fail.  <a_create>: the missing ones are made.  <*a_last> receives
**  the last component of the name.
*/
static	int	s_vbkx$os_parent	(
		char *		a_name,
		int		a_create,
		char **		a_last,
		VBKX$PAR *	a_par
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
	*a_par	= l_fd;

	return	(l_fd >= 0) ? 0 : -1;
}

static	void	s_vbkx$os_pclose	(
		VBKX$PAR	a_par
			)
{
	if ( a_par >= 0 )
		close(a_par);
}

static	int	s_vbkx$os_mkdir	(
		VBKX$PAR	a_par,
	const	char *		a_last
			)
{
	return	(mkdirat(a_par, a_last, 0700) && (errno != EEXIST)) ? -1 : 0;
}

static	int	s_vbkx$os_exists	(
		VBKX$PAR	a_par,
	const	char *		a_last
			)
{
	return	!faccessat(a_par, a_last, F_OK, AT_SYMLINK_NOFOLLOW);
}

static	void	s_vbkx$os_remove	(
		VBKX$PAR	a_par,
	const	char *		a_last
			)
{
	unlinkat(a_par, a_last, 0);
}

static	int	s_vbkx$os_creat	(
		VBKX$PAR	a_par,
	const	char *		a_last,
		void *		a_path
			)
{
	return	openat(a_par, a_last, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
}

static	int	s_vbkx$os_symlink	(
	const	char *		a_tgt,
		VBKX$PAR	a_par,
	const	char *		a_last,
	const	char *		a_name
			)
{
	return	symlinkat(a_tgt, a_par, a_last);
}

static	int	s_vbkx$os_link	(
		VBKX$PAR	a_tpar,
	const	char *		a_tlast,
		VBKX$PAR	a_par,
	const	char *		a_last
			)
{
	return	linkat(a_tpar, a_tlast, a_par, a_last, 0);
}

/*
**  A FIFO, and - root only - a device file
*/
static	int	s_vbkx$os_special	(
	const	VBKX$ENT *	a_e,
		VBKX$PAR	a_par,
	const	char *		a_last,
	const	char *		a_name
			)
{
	if ( a_e->ftype == VBK$K_FT_FIFO )
		return	mkfifoat(a_par, a_last, 0600);

	if ( !s_root )
		{
		s_vbkx$msg("File: %s - a device file, made by root only", a_name);

		return	1;
		}

	return	mknodat(a_par, a_last, ((a_e->ftype == VBK$K_FT_CHR) ? S_IFCHR : S_IFBLK) | 0600,
			makedev((unsigned) (a_e->rdev >> 32), (unsigned) (a_e->rdev & 0xFFFFFFFF)));
}

/*
**  What has no data is complete as soon as it is made: owner, mode, times
*/
static	void	s_vbkx$os_attrs	(
		VBKX$PAR	a_par,
	const	char *		a_last,
	const	VBKX$ENT *	a_e,
	const	char *		a_name
			)
{
struct timespec	l_ts [2] = { { a_e->atime.sec, a_e->atime.nsec }, { a_e->mtime.sec, a_e->mtime.nsec } };

	if ( s_root && fchownat(a_par, a_last, a_e->uid, a_e->gid, AT_SYMLINK_NOFOLLOW) )
		s_vbkx$attrerr(a_name, "owner");

	if ( (a_e->ftype != VBK$K_FT_SYMLINK) && fchmodat(a_par, a_last, (mode_t) (a_e->mode & 07777), 0) )
		s_vbkx$attrerr(a_name, "mode");

	if ( utimensat(a_par, a_last, l_ts, AT_SYMLINK_NOFOLLOW) )
		s_vbkx$attrerr(a_name, "times");
}

static	int	s_vbkx$os_pwrite	(
		int		a_fd,
	const	uint8_t *	a_data,
		uint32_t	a_n,
		uint64_t	a_off
			)
{
	return	(pwrite(a_fd, a_data, a_n, (off_t) a_off) == (ssize_t) a_n) ? 0 : -1;
}

/*
**  A regular file complete: its size, owner, mode, times; then closed
*/
static	int	s_vbkx$os_fdend	(
		int		a_fd,
	const	void *		a_path,
	const	VBKX$ENT *	a_e,
		uint64_t	a_size,
	const	char *		a_name
			)
{
struct timespec	l_ts [2] = { { a_e->atime.sec, a_e->atime.nsec }, { a_e->mtime.sec, a_e->mtime.nsec } };

	if ( ftruncate(a_fd, (off_t) a_size) )
		s_vbkx$msg("File: %s, errno: %d - its size cannot be set (%s)", a_name, errno, strerror(errno));

	if ( s_root && fchown(a_fd, a_e->uid, a_e->gid) )
		s_vbkx$attrerr(a_name, "owner");

	if ( fchmod(a_fd, (mode_t) (a_e->mode & 07777)) )
		s_vbkx$attrerr(a_name, "mode");

	if ( futimens(a_fd, l_ts) )
		s_vbkx$attrerr(a_name, "times");

	return	close(a_fd);
}

static	void	s_vbkx$os_dirattr	(
		VBKX$DIR *	a_d
			)
{
VBKX$PAR	l_pfd;
char *		l_last;
int		l_fd;

	if ( s_vbkx$os_parent(a_d->path, 0, &l_last, &l_pfd) )
		return;

	if ( 0 <= (l_fd = openat(l_pfd, l_last, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) )
		{
		struct timespec	l_ts [2] = { { a_d->atime.sec, a_d->atime.nsec }, { a_d->mtime.sec, a_d->mtime.nsec } };

		if ( fchmod(l_fd, (mode_t) (a_d->mode & 07777)) )
			s_vbkx$attrerr(a_d->path, "mode");

		if ( futimens(l_fd, l_ts) )
			s_vbkx$attrerr(a_d->path, "times");

		close(l_fd);
		}

	s_vbkx$os_pclose(l_pfd);
}

static	void	s_vbkx$os_localtime	(
		time_t		a_t,
		struct tm *	a_tm
			)
{
	localtime_r(&a_t, a_tm);
}

#endif	/* _WIN32 */


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
	while ( s_ndirs-- )
		{
		s_vbkx$os_dirattr(&s_dirs [s_ndirs]);
		free(s_dirs [s_ndirs].path);
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
#ifdef	_WIN32
	wchar_t		wpath [VBKX$K_WPATH];	/* The file being written, for its attributes	*/
#endif
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
VBKX$PAR	l_par, l_tpar;
int		l_rc = 0;

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
		s_vbkx$msg("Record: FILE - makes no sense, skipped");
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

	/* -j: by the last component only, no directories - MultiArc's "extract without pathnames" */
	if ( s_junk )
		{
		char *	l_slash = strrchr(a_out->name, '/');

		if ( l_e->ftype == VBK$K_FT_DIR )
			{
			a_out->active	= 0;

			return;
			}

		if ( l_slash )
			memmove(a_out->name, l_slash + 1, strlen(l_slash + 1) + 1);
		}

	if ( !s_vbkx$nameok(a_out->name, strlen(a_out->name)) )
		{
		s_vbkx$msg("File: %s - its name leads out of the output directory, not extracted", a_out->name);
		a_out->active	= 0;
		s_bad		= 1;

		return;
		}

#ifdef	_WIN32
	if ( !s_vbkx$winname(a_out->name) )
		{
		s_vbkx$msg("File: %s - not a valid name on Windows, not extracted", a_out->name);
		a_out->active	= 0;
		s_bad		= 1;

		return;
		}
#endif

	strcpy(l_name, a_out->name);

	if ( s_vbkx$os_parent(l_name, 1, &l_last, &l_par) )
		{
		s_vbkx$msg("File: %s, errno: %d - a directory on the way cannot be made, or is a link (%s)", a_out->name, errno, strerror(errno));
		a_out->active	= 0;
		s_bad		= 1;

		return;
		}

	if ( l_e->ftype == VBK$K_FT_DIR )
		{
		if ( !(l_rc = s_vbkx$os_mkdir(l_par, l_last)) )
			s_vbkx$defer(a_out->name, l_e);

		goto	l_done;
		}

	/* Something there: kept, unless -f - a directory is never removed */
	if ( s_vbkx$os_exists(l_par, l_last) )
		{
		if ( !s_force )
			{
			s_vbkx$msg("File: %s - already exists, not extracted (-f to overwrite)", a_out->name);
			a_out->active	= 0;
			s_bad		= 1;
			s_vbkx$os_pclose(l_par);

			return;
			}

		s_vbkx$os_remove(l_par, l_last);
		}

	switch ( l_e->ftype )
		{
		case	VBK$K_FT_REG:
#ifdef	_WIN32
			if ( 0 > (a_out->fd = s_vbkx$os_creat(l_par, l_last, a_out->wpath)) )
#else
			if ( 0 > (a_out->fd = s_vbkx$os_creat(l_par, l_last, NULL)) )
#endif
				l_rc	= -1;
			break;

		case	VBK$K_FT_SYMLINK:
			snprintf(l_tgt, sizeof(l_tgt), "%.*s", (int) l_e->linklen, l_e->link ? l_e->link : "");
			l_rc	= s_vbkx$os_symlink(l_tgt, l_par, l_last, a_out->name);
			break;

		case	VBK$K_FT_HARDLINK:
			snprintf(l_tgt, sizeof(l_tgt), "%.*s", (int) l_e->linklen, l_e->link ? l_e->link : "");

			if ( !s_vbkx$nameok(l_tgt, strlen(l_tgt)) || s_vbkx$os_parent(l_tgt, 0, &l_tlast, &l_tpar) )
				{
				l_rc	= -1;
				errno	= ENOENT;
				break;
				}

			l_rc	= s_vbkx$os_link(l_tpar, l_tlast, l_par, l_last);
			s_vbkx$os_pclose(l_tpar);
			break;

		case	VBK$K_FT_FIFO:
		case	VBK$K_FT_CHR:
		case	VBK$K_FT_BLK:
			l_rc	= s_vbkx$os_special(l_e, l_par, l_last, a_out->name);
			break;

		default:
			/* A socket is made by its server, not by a restore */
			a_out->active	= 0;
			s_vbkx$os_pclose(l_par);

			return;
		}

l_done:
	if ( l_rc < 0 )
		{
		s_vbkx$msg("File: %s, errno: %d - cannot be made (%s)", a_out->name, errno, strerror(errno));
		a_out->active	= 0;
		s_bad		= 1;
		}
	else if ( !l_rc && (l_e->ftype != VBK$K_FT_DIR) && (l_e->ftype != VBK$K_FT_REG) && (l_e->ftype != VBK$K_FT_HARDLINK) )
		s_vbkx$os_attrs(l_par, l_last, l_e, a_out->name);

	if ( l_e->ftype != VBK$K_FT_REG )
		a_out->active	= 0;

	s_vbkx$os_pclose(l_par);
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
		s_vbkx$msg("File: %s - a data record that makes no sense", a_out->name);
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
	else if ( (a_out->fd >= 0) && s_vbkx$os_pwrite(a_out->fd, l_data, l_n, l_off) )
		{
		s_vbkx$msg("File: %s, errno: %d - cannot be written (%s)", a_out->name, errno, strerror(errno));
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
		s_vbkx$msg("File: %s - checksum mismatch: the data differ from what was saved", a_out->name);
		a_out->damaged	= 1;
		}

	if ( a_out->damaged )
		{
		s_vbkx$msg("File: %s - is incomplete: its data was lost in bad blocks", a_out->name);
		s_bad	= 1;
		}
	else if ( l_status == VBK$K_FS_CHANGED )
		s_vbkx$msg("File: %s - changed while it was saved: the copy may be a mix", a_out->name);
	else if ( l_status == VBK$K_FS_READERR )
		s_vbkx$msg("File: %s - could not be read whole when it was saved", a_out->name);

	if ( a_out->tostd )
		{
		s_vbkx$zeros(a_out, l_size);

		return;
		}

	if ( a_out->fd < 0 )
		return;

#ifdef	_WIN32
	if ( s_vbkx$os_fdend(a_out->fd, a_out->wpath, l_e, l_size, a_out->name) )
#else
	if ( s_vbkx$os_fdend(a_out->fd, NULL, l_e, l_size, a_out->name) )
#endif
		{
		s_vbkx$msg("File: %s, errno: %d - cannot be written (%s)", a_out->name, errno, strerror(errno));
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
int		l_files = 0, l_ended = 0, l_pass = 0;

	while ( 1 & vbk$rd_next(a_rctx, &l_type, &l_body, &l_len, NULL) )
		{
		if ( a_rctx->resync && a_out->active )
			a_out->damaged	= 1;

		/* A file of a SOLID before the one wanted: its records passed */
		if ( l_pass && (l_type != VBK$K_RT_FILE) && a_rctx->insolid && !a_rctx->resync )
			continue;

		l_pass	= 0;

		if ( l_type == VBK$K_RT_FILE )
			{
			if ( a_out->active )
				s_vbkx$end(a_out, NULL, 0);

			/* The place is a SOLID: the files before it in there are not it */
			if ( a_one && !l_files && a_rctx->insolid && !a_rctx->resync && (s_vbkx$fileno(l_body, l_len) != a_one) )
				{
				l_pass	= 1;
				continue;
				}

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
			{
			l_ended	= 1;
			break;
			}
		}

	if ( a_out->active )
		s_vbkx$end(a_out, NULL, 0);

	/* The whole stream read, and no CATALOG or END at its end: what follows is not there - never "all done" */
	if ( !a_one && !l_ended )
		{
		s_vbkx$msg("Saveset: %s - ends before its catalog: the save did not complete, or its last volumes are missing", s_spec);
		s_bad	= 1;
		}
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

	s_vbkx$os_localtime(l_t, &l_tm);
	strftime(l_ts, sizeof(l_ts), "%Y-%m-%d %H:%M:%S", &l_tm);

	printf("%s %12llu %c%04o %.*s", l_ts, (unsigned long long) a_e->size, s_vbkx$tchar(a_e->ftype), a_e->mode & 07777,
		(int) a_e->pathlen, a_e->path);

	/* -m: for a program that parses the lines (MultiArc), the name and nothing after it */
	if ( !s_bare && a_e->link && ((a_e->ftype == VBK$K_FT_SYMLINK) || (a_e->ftype == VBK$K_FT_HARDLINK)) )
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

	/* A pipe ("-"): its catalog is at its end - it is listed as it is read, and that is no fault */
	if ( !a_rctx->isstream && (l_ents = s_vbkx$catalog(a_rctx, &l_n, &l_hole)) && !l_hole )
		{
		for ( size_t i = 0; i < l_n; i++ )
			if ( l_ents [i].status == VBK$K_FS_PRESENT )
				l_npres++;
			else	s_vbkx$line(&l_ents [i]);

		if ( l_npres )
			s_vbkx$msg("Files: %llu - unchanged, listed as present, not saved here", (unsigned long long) l_npres);

		s_vbkx$freecat(l_ents, l_n);

		return	s_bad ? 1 : 0;
		}

	if ( !a_rctx->isstream )
		{
		s_vbkx$freecat(l_ents, l_n);
		s_vbkx$msg("Saveset: %s - %s: the whole saveset is read", s_spec, a_rctx->trailer ? "the catalog is damaged" : "no catalog");
		vbk$rd_rewind(a_rctx);
		}

	while ( 1 & vbk$rd_next(a_rctx, &l_type, &l_body, &l_len, NULL) )
		{
		if ( (l_type == VBK$K_RT_CATALOG) || (l_type == VBK$K_RT_END) )
			break;

		if ( (l_type == VBK$K_RT_FILE) && (1 & s_vbkx$parse(l_body, l_len, &l_e)) )
			s_vbkx$line(&l_e);
		}

	/* A pipe read through to its TRAILER is whole; one that stopped short, or a saveset without its catalog, is not */
	if ( a_rctx->isstream && !s_bad && (a_rctx->trailer || a_rctx->trlraw) )
		return	0;

	if ( a_rctx->isstream && !a_rctx->trailer && !a_rctx->trlraw )
		s_vbkx$msg("Saveset: %s - ends before its catalog: the save did not complete", s_spec);

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
			s_vbkx$msg("File: %.*s - not extracted: its records were lost in bad blocks", (int) l_ents [i].pathlen, l_ents [i].path);

	if ( !l_ents || l_hole )
		s_vbkx$msg("Saveset: %s - %s: files missing from the output cannot all be named", s_spec,
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
		if ( a_rctx->isstream )
			{
			s_vbkx$msg("Saveset: %s - a pipe has its catalog at its end: names cannot be looked up; give no names to extract it all,"
				" or keep the stream in a file", s_spec);

			return	2;
			}

		if ( !(l_ents = s_vbkx$catalog(a_rctx, &l_n, &l_hole)) )
			{
			s_vbkx$msg("Saveset: %s - %s: names cannot be looked up; give no names to extract it all", s_spec,
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
				s_vbkx$msg("File: %.*s - is not a regular file", (int) l_e->pathlen, l_e->path);
				s_vbkx$freecat(l_ents, l_n);

				return	2;
				}

			if ( STS$K_ERROR == vbk$rd_seek(a_rctx, &l_e->loc) )
				{
				s_vbkx$msg("File: %.*s - not extracted: its records cannot be reached", (int) l_e->pathlen, l_e->path);
				s_bad	= 1;
				continue;
				}

			s_vbkx$stream(a_rctx, &l_out, a_make, l_e->fileno ? l_e->fileno : UINT32_MAX, l_asp);

			if ( !s_vbkx$isseen(l_e->fileno) )
				{
				s_vbkx$msg("File: %.*s - not extracted: its records were lost in bad blocks", (int) l_e->pathlen, l_e->path);
				s_bad	= 1;
				}

			if ( a_tostd )
				break;
			}

		s_vbkx$freecat(l_ents, l_n);

		if ( !l_found )
			{
			s_vbkx$msg("Saveset: %s - no such file in it", s_spec);

			return	2;
			}
		}

	free(l_out.body);

	if ( a_make )
		s_vbkx$dirs();

	return	s_bad ? 1 : 0;
}


/*
**  The passphrase of an encrypted saveset: the first line of the key file
**  (-k, else VBACKUP_KEY_FILE) - on Linux one only its owner may read -
**  else asked for on the terminal without echo.  Returns its length, -1 -
**  none to be had (said).
*/
static	int	s_noprompt;			/* -n, VBACKUP_NOPROMPT=1: no questions		*/

static	int	s_vbkx$pass	(
	const	char *		a_keyfile,
	const	char *		a_spec,
		char *		a_buf,
		size_t		a_size
			)
{
const char *	l_kf = a_keyfile ? a_keyfile : getenv("VBACKUP_KEY_FILE");
size_t		l_n = 0;

	if ( l_kf && *l_kf )
		{
		FILE *		l_fp;
		struct stat	l_st;
		int		l_c;

		if ( !(l_fp = fopen(l_kf, "rb")) )
			return	s_vbkx$msg("Key file: %s, errno: %d - cannot be read (%s)", l_kf, errno, strerror(errno)), -1;
#ifndef	_WIN32
		if ( fstat(fileno(l_fp), &l_st) || !S_ISREG(l_st.st_mode) || (l_st.st_mode & (S_IRWXG | S_IRWXO)) )
			{
			fclose(l_fp);

			return	s_vbkx$msg("Key file: %s - not a regular file, or others may read it: chmod 600 it", l_kf), -1;
			}
#else
		(void) l_st;
#endif
		while ( ((l_c = fgetc(l_fp)) != EOF) && (l_c != '\n') )
			if ( l_n < a_size )
				a_buf [l_n++] = (char) l_c;

		fclose(l_fp);
		}
	else if ( s_noprompt )
		return	s_vbkx$msg("Saveset: %s - is encrypted: give -k file or VBACKUP_KEY_FILE (no questions asked: -n)", a_spec), -1;
	else	{
#ifdef	_WIN32
		/* The console, UTF-16 without echo, made UTF-8 - the bytes Linux would have taken */
		HANDLE	l_in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		WCHAR	l_w [1100];
		DWORD	l_mode, l_got = 0;
		int	l_u;

		if ( l_in == INVALID_HANDLE_VALUE || !GetConsoleMode(l_in, &l_mode) )
			return	s_vbkx$msg("Saveset: %s - is encrypted, and there is no console to ask the passphrase on: give -k file", a_spec), -1;

		fprintf(stderr, "Passphrase for %s: ", a_spec);
		SetConsoleMode(l_in, (l_mode & ~ENABLE_ECHO_INPUT) | ENABLE_LINE_INPUT);
		ReadConsoleW(l_in, l_w, 1099, &l_got, NULL);
		SetConsoleMode(l_in, l_mode);
		CloseHandle(l_in);
		fprintf(stderr, "\n");

		while ( l_got && ((l_w [l_got - 1] == L'\n') || (l_w [l_got - 1] == L'\r')) )
			l_got--;

		l_u	= l_got ? WideCharToMultiByte(CP_UTF8, 0, l_w, (int) l_got, a_buf, (int) a_size, NULL, NULL) : 0;
		SecureZeroMemory(l_w, sizeof(l_w));
		l_n	= (l_u > 0) ? (size_t) l_u : 0;
#else
		struct termios	l_save, l_t;
		FILE *		l_tty;
		int		l_c;

		if ( !(l_tty = fopen("/dev/tty", "r+")) || tcgetattr(fileno(l_tty), &l_save) )
			{
			if ( l_tty )
				fclose(l_tty);

			return	s_vbkx$msg("Saveset: %s - is encrypted, and there is no terminal to ask the passphrase on: give -k file", a_spec), -1;
			}

		fprintf(l_tty, "Passphrase for %s: ", a_spec);
		fflush(l_tty);
		l_t		= l_save;
		l_t.c_lflag	&= ~(tcflag_t) (ECHO | ECHOE | ECHOK | ECHONL);
		tcsetattr(fileno(l_tty), TCSAFLUSH, &l_t);

		while ( ((l_c = fgetc(l_tty)) != EOF) && (l_c != '\n') )
			if ( l_n < a_size )
				a_buf [l_n++] = (char) l_c;

		tcsetattr(fileno(l_tty), TCSAFLUSH, &l_save);
		fprintf(l_tty, "\n");
		fclose(l_tty);
#endif
		}

	if ( l_n && (a_buf [l_n - 1] == '\r') )
		l_n--;

	if ( !l_n || (l_n >= a_size) )
		return	s_vbkx$msg("Passphrase: none - empty, or longer than %u bytes", (unsigned) (a_size - 1)), -1;

	return	(int) l_n;
}


/*
**  A saveset of OpenVMS BACKUP (LIB/VBKVMS.C, doc/vmsbackup.md): l, x, p
**  and t on it as on one of VBACKUP.  The names are the Linux names -
**  [A.B]C.TXT;3 is A/B/C.TXT, the older versions keep ";n" - and so are
**  the names given; a text file becomes a text with LF.
*/
typedef struct vbkx_vout_t
{
	VBK$VMSFILE	file;
	VBKX$ENT	ent;
	char		name [VBKX$K_SZ_PATH];
	int		fd;			/* -1 - its data goes nowhere			*/
	int		tostd;
	int		mode;			/* VBK$K_VMSCNV_*				*/
	VBK$VMSCNV	cnv;
	uint64_t	pos;			/* Octets written				*/
	uint32_t	nextvbn;
	int		damaged, failed;
#ifdef	_WIN32
	wchar_t		wpath [VBKX$K_WPATH];
#endif
} VBKX$VOUT;

/*
**  Is the saveset one of OpenVMS BACKUP: its first block judged
*/
static	int	s_vbkx$isvms	(
	const	char *		a_spec
			)
{
uint8_t *	l_blk;
size_t		l_got = 0;
int64_t		l_rc;
int		l_fd, l_is = 0;
struct stat	l_st;

	/* A regular file only: a FIFO would hang the open, a device be read from */
	if ( !strcmp(a_spec, "-") || stat(a_spec, &l_st) || !S_ISREG(l_st.st_mode) || (0 > (l_fd = vbk$os_open(a_spec))) )
		return	0;

	if ( (l_blk = malloc(VBK$K_VMSMAXBSZ)) )
		{
		while ( (l_got < VBK$K_VMSMAXBSZ) && (0 < (l_rc = vbk$os_read(l_fd, l_blk + l_got, VBK$K_VMSMAXBSZ - l_got))) )
			l_got	+= (size_t) l_rc;

		/* By its header: a bad first block is rebuilt from its group */
		l_is	= (l_got >= VBK$K_VMSHDR) && (1 & vbk$vms_probe(l_blk, VBK$K_VMSHDR));
		free(l_blk);
		}

	vbk$os_close(l_fd);

	return	l_is;
}

/*
**  VMS time - the local time of the system that wrote it - as the time here
*/
static	VBK$TIME	s_vbkx$vtime	(
		uint64_t	a_vtime
			)
{
VBK$TIME	l_t = { 0, 0 };
int64_t		l_sec;
uint32_t	l_nsec;
time_t		l_w;
struct tm *	l_tm;

	if ( !vbk$vms_time(a_vtime, &l_sec, &l_nsec) )
		return	l_t;

	l_w	= (time_t) l_sec;

	if ( (l_tm = gmtime(&l_w)) )
		{
		l_tm->tm_isdst = -1;
		l_t.sec	= (int64_t) mktime(l_tm);
		l_t.nsec = l_nsec;
		}

	return	l_t;
}

static	int	s_vbkx$vwrite	(
		void *		a_arg,
	const	uint8_t *	a_buf,
		size_t		a_len
			)
{
VBKX$VOUT *	l_o = (VBKX$VOUT *) a_arg;

	if ( l_o->tostd ? (fwrite(a_buf, 1, a_len, stdout) != a_len) : s_vbkx$os_pwrite(l_o->fd, a_buf, (uint32_t) a_len, l_o->pos) )
		{
		s_vbkx$msg("File: %s, errno: %d - cannot be written (%s)", l_o->name, errno, strerror(errno));
		l_o->failed	= 1;

		return	STS$K_ERROR;
		}

	l_o->pos	+= a_len;

	return	STS$K_SUCCESS;
}

/*
**  A file begins: its entry, and - x - the file or the directory made
*/
static	void	s_vbkx$vbegin	(
		VBKX$VOUT *	a_o,
		int		a_make
			)
{
char		l_name [VBKX$K_SZ_PATH], *l_last;
VBKX$PAR	l_par;
int		l_rc = 0;

	a_o->fd		= -1;
	a_o->pos	= 0;
	a_o->nextvbn	= 1;
	a_o->damaged	= 0;
	a_o->failed	= 0;
	a_o->mode	= vbk$vms_cnvmode(&a_o->file);

	if ( !a_make || a_o->file.isdir )
		{
		if ( !a_make || s_junk )
			return;
		}

	if ( s_junk )
		{
		char *	l_slash = strrchr(a_o->name, '/');

		if ( l_slash )
			memmove(a_o->name, l_slash + 1, strlen(l_slash + 1) + 1);
		}

	if ( !s_vbkx$nameok(a_o->name, strlen(a_o->name)) )
		{
		s_vbkx$msg("File: %s - its name leads out of the output directory, not extracted", a_o->name);
		s_bad	= 1;

		return;
		}

#ifdef	_WIN32
	if ( !s_vbkx$winname(a_o->name) )
		{
		s_vbkx$msg("File: %s - not a valid name on Windows, not extracted", a_o->name);
		s_bad	= 1;

		return;
		}
#endif

	strcpy(l_name, a_o->name);

	if ( s_vbkx$os_parent(l_name, 1, &l_last, &l_par) )
		{
		s_vbkx$msg("File: %s, errno: %d - a directory on the way cannot be made, or is a link (%s)", a_o->name, errno, strerror(errno));
		s_bad	= 1;

		return;
		}

	if ( a_o->file.isdir )
		{
		if ( !(l_rc = s_vbkx$os_mkdir(l_par, l_last)) )
			s_vbkx$defer(a_o->name, &a_o->ent);
		}
	else if ( s_vbkx$os_exists(l_par, l_last) && !s_force )
		{
		s_vbkx$msg("File: %s - already exists, not extracted (-f to overwrite)", a_o->name);
		s_bad	= 1;
		}
	else	{
		s_vbkx$os_remove(l_par, l_last);
#ifdef	_WIN32
		l_rc	= (0 > (a_o->fd = s_vbkx$os_creat(l_par, l_last, a_o->wpath)));
#else
		l_rc	= (0 > (a_o->fd = s_vbkx$os_creat(l_par, l_last, NULL)));
#endif
		}

	if ( l_rc )
		{
		s_vbkx$msg("File: %s, errno: %d - cannot be made (%s)", a_o->name, errno, strerror(errno));
		s_bad	= 1;
		a_o->fd	= -1;
		}

	s_vbkx$os_pclose(l_par);

	if ( (a_o->fd >= 0) && (a_o->file.org != VBK$K_VMSORG_SEQ) )
		s_vbkx$msg("File: %s, Organization: %s - extracted as it is on the VMS disk: its records are not converted", a_o->name,
			vbk$vms_orgname(a_o->file.org));

	if ( a_o->fd >= 0 )
		vbk$vms_cnvinit(&a_o->cnv, &a_o->file, a_o->mode, s_vbkx$vwrite, a_o);
}

/*
**  A VBN record of the file in hand; 0 - it goes back: another file's,
**  whose FILE record was lost
*/
static	int	s_vbkx$vdata	(
		VBKX$VOUT *	a_o,
	const	VBK$VMSREC *	a_rec
			)
{
uint64_t	l_off = (uint64_t) (a_rec->address - 1) * 512, l_n = a_rec->len;

	if ( !a_rec->address || (a_rec->address < a_o->nextvbn) )
		return	0;

	if ( a_rec->resync || (a_rec->address != a_o->nextvbn) )
		a_o->damaged	= 1;

	a_o->nextvbn	= a_rec->address + a_rec->len / 512;

	if ( ((a_o->fd < 0) && !a_o->tostd) || a_o->failed || (l_off >= a_o->file.bytes) )
		return	1;

	if ( l_off + l_n > a_o->file.bytes )
		l_n	= a_o->file.bytes - l_off;

	/* As it is: at its place, a lost block a hole */
	if ( (a_o->mode == VBK$K_VMSCNV_RAW) && !a_o->tostd )
		a_o->pos = l_off;

	vbk$vms_cnv(&a_o->cnv, a_rec->body, (size_t) l_n);

	return	1;
}

static	void	s_vbkx$vend	(
		VBKX$VOUT *	a_o
			)
{
	if ( (a_o->fd >= 0) || a_o->tostd )
		vbk$vms_cnvend(&a_o->cnv);

	if ( (uint64_t) (a_o->nextvbn - 1) * 512 < a_o->file.bytes )
		a_o->damaged	= 1;

	if ( a_o->damaged )
		{
		s_vbkx$msg("File: %s - incomplete: its data was lost in bad blocks", a_o->name);
		s_bad	= 1;
		}

	if ( a_o->fd >= 0 )
		{
		/* The size: a converted text is what was written, an image the end of file */
		if ( s_vbkx$os_fdend(a_o->fd, NULL, &a_o->ent, (a_o->mode == VBK$K_VMSCNV_RAW) ? a_o->file.bytes : a_o->pos, a_o->name) )
			s_bad	= 1;
		}

	a_o->fd	= -1;
}

static	int	s_vbkx$vms	(
		char		a_op,
		char **		a_names,
		int		a_nnames
			)
{
VBK$VMS		l_vms;
VBK$VMSREC	l_rec;
static	VBKX$VOUT	l_o;
char		l_prev [VBK$K_VMSNAME] = "";
int		l_fd, l_in = 0, l_found = 0, l_files = 0, l_status;

	if ( 0 > (l_fd = vbk$os_open(s_spec)) )
		return	s_vbkx$msg("File: %s, errno: %d - cannot be opened (%s)", s_spec, errno, strerror(errno)), 2;

	if ( !(1 & (l_status = vbk$vms_open(&l_vms, l_fd, s_vbkx$event, NULL))) )
		{
		vbk$os_close(l_fd);

		return	s_vbkx$msg("File: %s - is not a saveset", s_spec), 2;
		}

	if ( l_vms.nocrc && (a_op == 't') )
		s_vbkx$msg("Saveset: %s - written /NOCRC: its blocks carry no CRC, damage in them cannot be seen", s_spec);

	if ( (a_op == 'x') && s_vbkx$os_outdir(s_outdir) )
		{
		s_vbkx$msg("Directory: %s, errno: %d - cannot be made or entered (%s)", s_outdir, errno, strerror(errno));
		vbk$vms_close(&l_vms);
		vbk$os_close(l_fd);

		return	2;
		}

	memset(&l_o, 0, sizeof(l_o));
	l_o.fd		= -1;
	l_o.tostd	= (a_op == 'p');

	while ( 1 & vbk$vms_next(&l_vms, &l_rec) )
		{
		if ( l_rec.rtype == VBK$K_VMSRT_FILE )
			{
			int	l_older;

			if ( l_in )
				s_vbkx$vend(&l_o);

			l_in	= 0;

			if ( (a_op == 'p') && l_found )
				break;

			if ( !(1 & vbk$vms_file(l_rec.body, l_rec.len, &l_o.file)) )
				{
				s_vbkx$msg("Block: %llu, Volume: 1 - an invalid record, skipped", (unsigned long long) l_rec.blkno);
				s_bad	= 1;
				continue;
				}

			l_older	= !l_o.file.isdir && vbk$vms_same(l_o.file.spec, l_o.file.speclen, l_prev, strlen(l_prev));
			snprintf(l_prev, sizeof(l_prev), "%s", l_o.file.spec);

			if ( !vbk$vms_unix(l_o.file.spec, l_o.file.speclen, l_o.file.isdir, l_older, l_o.name, sizeof(l_o.name)) )
				continue;

			memset(&l_o.ent, 0, sizeof(l_o.ent));
			l_o.ent.path	= l_o.name;
			l_o.ent.pathlen	= (uint32_t) strlen(l_o.name);
			l_o.ent.ftype	= l_o.file.isdir ? VBK$K_FT_DIR : VBK$K_FT_REG;
			l_o.ent.mode	= vbk$vms_mode(l_o.file.fpro, l_o.file.isdir) | (l_o.file.isdir ? 0700 : 0);
			l_o.ent.size	= l_o.file.bytes;
			l_o.ent.mtime	= s_vbkx$vtime(l_o.file.revdate);
			l_o.ent.atime	= l_o.file.accdate ? s_vbkx$vtime(l_o.file.accdate) : l_o.ent.mtime;
#ifndef	_WIN32
			l_o.ent.uid	= getuid();
			l_o.ent.gid	= getgid();
#endif
			/* p: the file asked for - its Linux name with or without the version, or its VMS name */
			if ( a_op == 'p' )
				{
				char	l_plain [VBKX$K_SZ_PATH];

				if ( l_o.file.isdir || (strcmp(a_names [0], l_o.name) && strcmp(a_names [0], l_o.file.spec)
					&& !(vbk$vms_unix(l_o.file.spec, l_o.file.speclen, 0, 0, l_plain, sizeof(l_plain)) && !strcmp(a_names [0], l_plain))) )
					continue;

				l_found	= 1;
				}
			else if ( !s_vbkx$wanted(&l_o.ent, a_names, a_nnames) )
				continue;

			l_files++;

			if ( a_op == 'l' )
				{
				s_vbkx$line(&l_o.ent);
				continue;
				}

			s_vbkx$vbegin(&l_o, a_op == 'x');

			if ( l_o.tostd )
				vbk$vms_cnvinit(&l_o.cnv, &l_o.file, l_o.mode, s_vbkx$vwrite, &l_o);

			l_in	= !l_o.file.isdir;
			continue;
			}

		if ( (l_rec.rtype == VBK$K_VMSRT_VBN) && l_in && !s_vbkx$vdata(&l_o, &l_rec) )
			{
			s_vbkx$vend(&l_o);
			l_in	= 0;
			}
		}

	if ( l_in )
		s_vbkx$vend(&l_o);

	vbk$vms_close(&l_vms);
	vbk$os_close(l_fd);

	if ( a_op == 'x' )
		{
		s_vbkx$dirs();
		s_vbkx$os_outclose();
		}

	if ( (a_op == 'p') && !l_found )
		return	s_vbkx$msg("Saveset: %s - no such file in it", s_spec), 2;

	if ( (a_op == 'x') && a_nnames && !l_files )
		return	s_vbkx$msg("Saveset: %s - no such file in it", s_spec), 2;

	if ( (a_op == 'p') && fflush(stdout) )
		return	2;

	if ( (a_op == 't') && !s_bad )
		printf("%s: an OpenVMS BACKUP saveset, all files read, %s\n", s_spec, l_vms.nocrc ? "no CRC to check (/NOCRC)" : "all block CRCs match");

	return	s_bad ? 1 : 0;
}


static	int	s_vbkx$usage	(void)
{
	fprintf(stderr,
		"VBKX " __IDENT__ " - the stand-alone extractor of VBACKUP savesets, and of OpenVMS BACKUP ones\n"
		"\n"
		"  vbkx l saveset [-m]                  list the files (-m: no link targets, for programs)\n"
		"  vbkx x saveset [-C dir] [-f] [-j] [name...]  extract (all, or the names given; -j: no directories)\n"
		"  vbkx p saveset name                  write one file to the standard output\n"
		"  vbkx t saveset                       test: read it all, check the checksums\n"
		"\n"
		"  -C dir  extract into dir (made if missing), default the current one\n"
		"  -f      overwrite files that are there\n"
		"  -k file the passphrase of an encrypted saveset: the first line of file\n"
		"          (else VBACKUP_KEY_FILE, else it is asked for on the terminal)\n"
		"  -n      no questions: never ask for a passphrase (for programs; so does VBACKUP_NOPROMPT=1)\n"
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
const char *	l_keyfile = NULL;

#ifdef	_WIN32
	/* The arguments as the system has them, UTF-16, made UTF-8 - the names of a saveset are */
	{
	wchar_t **	l_wargv = CommandLineToArgvW(GetCommandLineW(), &argc);

	if ( !l_wargv || !(argv = calloc((size_t) argc + 1, sizeof(char *))) )
		return	2;

	for ( int i = 0; i < argc; i++ )
		{
		int	l_n = WideCharToMultiByte(CP_UTF8, 0, l_wargv [i], -1, NULL, 0, NULL, NULL);

		if ( (l_n <= 0) || !(argv [i] = malloc((size_t) l_n)) )
			return	2;

		WideCharToMultiByte(CP_UTF8, 0, l_wargv [i], -1, argv [i], l_n, NULL, NULL);
		}

	LocalFree(l_wargv);

	/* The data of a file, and the listing, byte for byte as on Linux: no CR LF */
	_setmode(_fileno(stdout), _O_BINARY);
	}
#endif

	if ( (argc < 3) || (strlen(argv [1]) != 1) || !strchr("lxpt", argv [1] [0]) )
		return	s_vbkx$usage();

	l_op	= argv [1] [0];
	s_spec	= argv [2];
#ifdef	_WIN32
	s_root	= 0;
#else
	s_root	= !geteuid();
#endif

	if ( !(l_names = calloc((size_t) argc, sizeof(char *))) )
		return	2;

	for ( int i = 3; i < argc; i++ )
		{
		if ( !strcmp(argv [i], "-C") && ((i + 1) < argc) && (l_op == 'x') )
			s_outdir = argv [++i];
		else if ( !strcmp(argv [i], "-f") && (l_op == 'x') )
			s_force	= 1;
		else if ( !strcmp(argv [i], "-m") && (l_op == 'l') )
			s_bare	= 1;
		else if ( !strcmp(argv [i], "-j") && (l_op == 'x') )
			s_junk	= 1;
		else if ( !strcmp(argv [i], "-k") && ((i + 1) < argc) )
			l_keyfile = argv [++i];
		else if ( !strcmp(argv [i], "-n") )
			s_noprompt = 1;
		else	l_names [l_nnames++] = argv [i];
		}

	if ( ((l_op == 'p') && (l_nnames != 1)) || (((l_op == 'l') || (l_op == 't')) && l_nnames) )
		return	s_vbkx$usage();

	/* A saveset of OpenVMS BACKUP: read by VBKVMS.C */
	if ( s_vbkx$isvms(s_spec) )
		{
		l_rc	= s_vbkx$vms(l_op, l_names, l_nnames);
		free(l_names);
		free(s_seen);

		return	l_rc;
		}

	if ( !(1 & (l_status = vbk$rd_open(&l_rctx, s_spec, s_vbkx$event, NULL))) )
		{
		if ( l_status == STS$K_WARN )
			s_vbkx$msg("File: %s - is not a saveset", s_spec);
		else	s_vbkx$msg("File: %s, errno: %d - cannot be opened (%s)", s_spec, l_rctx.err ? l_rctx.err : errno, strerror(l_rctx.err ? l_rctx.err : errno));

		return	2;
		}

	if ( getenv("VBACKUP_NOPROMPT") && !strcmp(getenv("VBACKUP_NOPROMPT"), "1") )
		s_noprompt = 1;

	/* Encrypted: nothing of it is read before the passphrase is right */
	if ( l_rctx.crypt )
		{
		char	l_pass [VBK$K_PASSMAX + 2];
		int	l_plen = s_vbkx$pass(l_keyfile, s_spec, l_pass, sizeof(l_pass));

		l_status = (l_plen > 0) ? vbk$rd_setkey(&l_rctx, l_pass, (size_t) l_plen) : STS$K_ERROR;
		vbk$crp_wipe(l_pass, sizeof(l_pass));

		if ( (l_plen > 0) && (l_status == STS$K_ERROR) )
			s_vbkx$msg("Saveset: %s - the passphrase does not open it", s_spec);

		if ( l_status == STS$K_ERROR )
			{
			vbk$rd_close(&l_rctx);

			return	2;
			}

		if ( l_status == STS$K_WARN )
			{
			s_vbkx$msg("Saveset: %s - its trailer fails its authentication: read as a saveset without a catalog", s_spec);
			s_bad	= 1;
			}
		}

	switch ( l_op )
		{
		case	'l':
			l_rc	= s_vbkx$list(&l_rctx);
			break;

		case	'x':
			if ( s_vbkx$os_outdir(s_outdir) )
				{
				s_vbkx$msg("Directory: %s, errno: %d - cannot be made or entered (%s)", s_outdir, errno, strerror(errno));
				l_rc	= 2;
				break;
				}

			l_rc	= s_vbkx$extract(&l_rctx, l_names, l_nnames, 1, 0);
			s_vbkx$os_outclose();
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
