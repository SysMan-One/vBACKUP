#define	__MODULE__	"VBKOSW"
#define	__IDENT__	"X01-17"
#define	__REV__		"1.17.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkosw.c
**
**  ABSTRACT:	The system calls of the utility on Windows: POSIX by name
**		(win/vbkwin.h maps the names onto these routines), the wide
**		API of Windows underneath.
**
**  DESCRIPTION: Every name comes in and goes out in UTF-8, as a saveset
**		and the rest of the utility have it; it is made UTF-16, the
**		full path, and given the \\?\ prefix when it is long, so the
**		limit of MAX_PATH does not apply.  A file is opened by
**		CreateFileW and given a descriptor of the C library
**		(_open_osfhandle), in binary mode: read(), write() and close()
**		of the C library then serve it.  A directory is opened so
**		too - FILE_FLAG_BACKUP_SEMANTICS - and the *at() calls find
**		its path again by GetFinalPathNameByHandleW.
**
**		The symbolic links and the junctions of NTFS are the links
**		of Linux here: lstat() says S_IFLNK, readlink() reads them,
**		O_NOFOLLOW refuses them (ELOOP).  The other reparse points -
**		deduplicated, cloud files - are what they hold, followed.
**
**		The mode is made up: a directory is 0755, a file 0644, or
**		0444 when it is read-only; chmod() sets or clears the
**		read-only attribute by S_IWUSR, nothing else.  The owner is
**		none (0): there are no uid and gid on Windows.
**
**		What vbackup.exe has not, at the end of the module: /PHYSICAL,
**		/IMAGE (refused by the command line already), the read-ahead
**		of files.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  6-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-17		 6-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	"vbkdef.h"

#include	<winioctl.h>
#include	<shellapi.h>

#define	VBK$K_WMAX	32768			/* UTF-16 units of a path, \\?\ and all		*/
#define	VBK$K_LONG	240			/* From here on the \\?\ prefix			*/
#define	VBK$K_ENVS	32			/* Environment variables remembered in UTF-8	*/

#ifndef	SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define	SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE	0x2
#endif

#ifndef	IO_REPARSE_TAG_SYMLINK
#define	IO_REPARSE_TAG_SYMLINK		0xA000000CUL
#endif

#ifndef	IO_REPARSE_TAG_MOUNT_POINT
#define	IO_REPARSE_TAG_MOUNT_POINT	0xA0000003UL
#endif

#define	VBK$M_SYMLINK_RELATIVE		0x1	/* SYMLINK_FLAG_RELATIVE of the reparse data	*/

/*
**  The reparse data of a link (ntifs.h, not among the headers of MinGW-w64)
*/
typedef struct vbk_reparse_t
{
	ULONG		tag;
	USHORT		len;
	USHORT		reserved;
	USHORT		subst_off;
	USHORT		subst_len;
	USHORT		print_off;
	USHORT		print_len;
	union
		{
		struct
			{
			ULONG	flags;
			WCHAR	buf [1];
			} sym;
		struct
			{
			WCHAR	buf [1];
			} mnt;
		} u;
} VBK$REPARSE;

/*
**  An open directory: the search of FindFirstFileExW
*/
struct vbk_w_dir_t
{
	HANDLE		find;
	WIN32_FIND_DATAW data;
	int		first;			/* DATA holds an entry not yet given		*/
	int		fd;			/* dirfd(), fdopendir(): -1 - none yet		*/
	struct vbk$w_dirent ent;
	char		path [];		/* UTF-8, as given				*/
};

typedef struct vbk_env_t
{
	char *		name;
	char *		value;
} VBK$ENV;

static	VBK$ENV		s_envs [VBK$K_ENVS];
static	pthread_mutex_t	s_envlock = PTHREAD_MUTEX_INITIALIZER;


/*
**  The errno of a Windows error
*/
static	int	s_vbk$errno	(
		DWORD		a_err
			)
{
	switch ( a_err )
		{
		case	ERROR_FILE_NOT_FOUND:
		case	ERROR_PATH_NOT_FOUND:
		case	ERROR_INVALID_DRIVE:
		case	ERROR_BAD_NETPATH:
		case	ERROR_BAD_NET_NAME:
		case	ERROR_INVALID_NAME:
		case	ERROR_BAD_PATHNAME:	return	ENOENT;
		case	ERROR_ACCESS_DENIED:
		case	ERROR_SHARING_VIOLATION:
		case	ERROR_LOCK_VIOLATION:	return	EACCES;
		case	ERROR_ALREADY_EXISTS:
		case	ERROR_FILE_EXISTS:	return	EEXIST;
		case	ERROR_DIR_NOT_EMPTY:	return	ENOTEMPTY;
		case	ERROR_DIRECTORY:	return	ENOTDIR;
		case	ERROR_DISK_FULL:
		case	ERROR_HANDLE_DISK_FULL:	return	ENOSPC;
		case	ERROR_NOT_SAME_DEVICE:	return	EXDEV;
		case	ERROR_PRIVILEGE_NOT_HELD: return EPERM;
		case	ERROR_CANT_RESOLVE_FILENAME: return ELOOP;
		case	ERROR_FILENAME_EXCED_RANGE: return ENAMETOOLONG;
		case	ERROR_NOT_SUPPORTED:
		case	ERROR_INVALID_FUNCTION:	return	ENOTSUP;
		case	ERROR_INVALID_HANDLE:	return	EBADF;
		case	ERROR_NEGATIVE_SEEK:
		case	ERROR_INVALID_PARAMETER: return	EINVAL;
		case	ERROR_BROKEN_PIPE:
		case	ERROR_NO_DATA:		return	EPIPE;
		case	ERROR_WRITE_PROTECT:	return	EROFS;
		case	ERROR_NOT_ENOUGH_MEMORY:
		case	ERROR_OUTOFMEMORY:	return	ENOMEM;
		}

	return	EIO;
}

/*
**  -1 with errno of the last Windows error
*/
static	int	s_vbk$fail	(void)
{
	errno	= s_vbk$errno(GetLastError());

	return	-1;
}


/*
**  UTF-16 into UTF-8, '\' made '/': the length, -1 - it does not fit
*/
static	int	s_vbk$utf8	(
	const	wchar_t *	a_w,
		int		a_wlen,
		char *		a_out,
		size_t		a_outsz
			)
{
int	l_n;

	if ( 0 >= (l_n = WideCharToMultiByte(CP_UTF8, 0, a_w, a_wlen, a_out, (int) a_outsz - 1, NULL, NULL)) )
		{
		if ( !a_wlen )
			{
			a_out [0] = '\0';

			return	0;
			}

		errno	= ENAMETOOLONG;

		return	-1;
		}

	a_out [l_n] = '\0';

	for ( int i = 0; i < l_n; i++ )
		if ( a_out [i] == '\\' )
			a_out [i] = '/';

	return	l_n;
}

/*
**  A name in UTF-8 made the full UTF-16 path of the system calls: '/'
**  made '\', relative made absolute, long given the \\?\ prefix.
**  0 - not valid UTF-8, or too long; errno says which.
*/
static	int	s_vbk$wpath	(
	const	char *		a_path,
		wchar_t *	a_out
			)
{
wchar_t	l_rel [VBK$K_WMAX];
DWORD	l_n;

	if ( !strcmp(a_path, "/dev/null") )
		return	wcscpy(a_out, L"NUL"), 1;

	/* The terminal of the passphrase (src/vbkkey.c): the input of the console */
	if ( !strcmp(a_path, "/dev/tty") )
		return	wcscpy(a_out, L"CONIN$"), 1;

	if ( !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_path, -1, l_rel, VBK$K_WMAX) )
		{
		errno	= (GetLastError() == ERROR_INSUFFICIENT_BUFFER) ? ENAMETOOLONG : EINVAL;

		return	0;
		}

	if ( !l_rel [0] )
		{
		errno	= ENOENT;

		return	0;
		}

	for ( wchar_t *l_p = l_rel; *l_p; l_p++ )
		if ( *l_p == L'/' )
			*l_p = L'\\';

	/* Already \\?\ - from GetFinalPathNameByHandleW: as it is */
	if ( !wcsncmp(l_rel, L"\\\\?\\", 4) )
		return	wcscpy(a_out, l_rel), 1;

	if ( !(l_n = GetFullPathNameW(l_rel, VBK$K_WMAX - 8, a_out + 8, NULL)) || (l_n >= (VBK$K_WMAX - 8)) )
		{
		errno	= ENAMETOOLONG;

		return	0;
		}

	if ( l_n < VBK$K_LONG )
		memmove(a_out, a_out + 8, (l_n + 1) * sizeof(wchar_t));
	else if ( !wcsncmp(a_out + 8, L"\\\\", 2) )
		{
		/* \\server\share\... - \\?\UNC\server\share\... */
		memcpy(a_out + 2, L"\\\\?\\UNC", 7 * sizeof(wchar_t));
		memmove(a_out, a_out + 2, (l_n + 7) * sizeof(wchar_t));
		}
	else	memcpy(a_out + 4, L"\\\\?\\", 4 * sizeof(wchar_t)), memmove(a_out, a_out + 4, (l_n + 5) * sizeof(wchar_t));

	return	1;
}

/*
**  The path of an open directory in UTF-16, and a name below it
*/
static	int	s_vbk$atpath	(
		int		a_dirfd,
	const	char *		a_name,
		wchar_t *	a_out
			)
{
HANDLE	l_h = INVALID_HANDLE_VALUE;
DWORD	l_n;
size_t	l_len;

	if ( a_dirfd == AT_FDCWD )
		return	s_vbk$wpath(a_name, a_out);

	if ( INVALID_HANDLE_VALUE == (l_h = (HANDLE) _get_osfhandle(a_dirfd)) )
		return	errno = EBADF, 0;

	if ( !(l_n = GetFinalPathNameByHandleW(l_h, a_out, VBK$K_WMAX - 512, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)) || (l_n >= (VBK$K_WMAX - 512)) )
		return	s_vbk$fail(), 0;

	l_len	= l_n;

	if ( l_len && (a_out [l_len - 1] != L'\\') )
		a_out [l_len++] = L'\\';

	if ( !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_name, -1, a_out + l_len, (int) (VBK$K_WMAX - l_len)) )
		return	errno = EINVAL, 0;

	for ( wchar_t *l_p = a_out + l_len; *l_p; l_p++ )
		if ( *l_p == L'/' )
			*l_p = L'\\';

	return	1;
}


/*
**  FILETIME and struct timespec
*/
static	void	s_vbk$ft2ts	(
	const	FILETIME *	a_ft,
		struct timespec *a_ts
			)
{
int64_t	l_t = (int64_t) (((uint64_t) a_ft->dwHighDateTime << 32) | a_ft->dwLowDateTime) - 116444736000000000LL;

	a_ts->tv_sec	= (time_t) (l_t / 10000000LL);
	a_ts->tv_nsec	= (long) ((l_t % 10000000LL) * 100);

	if ( a_ts->tv_nsec < 0 )
		{
		a_ts->tv_sec--;
		a_ts->tv_nsec += 1000000000L;
		}
}

static	void	s_vbk$li2ts	(
	const	LARGE_INTEGER *	a_li,
		struct timespec *a_ts
			)
{
FILETIME	l_ft = { .dwLowDateTime = a_li->LowPart, .dwHighDateTime = (DWORD) a_li->HighPart };

	s_vbk$ft2ts(&l_ft, a_ts);
}

static	int	s_vbk$ts2ft	(
	const	struct timespec *a_ts,
		FILETIME *	a_ft
			)
{
uint64_t	l_t;

	if ( a_ts->tv_nsec == UTIME_OMIT )
		return	0;

	if ( a_ts->tv_nsec == UTIME_NOW )
		{
		GetSystemTimeAsFileTime(a_ft);

		return	1;
		}

	l_t	= (uint64_t) ((int64_t) a_ts->tv_sec * 10000000LL + a_ts->tv_nsec / 100 + 116444736000000000LL);

	a_ft->dwLowDateTime	= (DWORD) l_t;
	a_ft->dwHighDateTime	= (DWORD) (l_t >> 32);

	return	1;
}


/*
**  The reparse tag of an open file, 0 - it is no reparse point
*/
static	DWORD	s_vbk$tag	(
		HANDLE		a_h
			)
{
FILE_ATTRIBUTE_TAG_INFO	l_ti;

	if ( !GetFileInformationByHandleEx(a_h, FileAttributeTagInfo, &l_ti, sizeof(l_ti)) )
		return	0;

	return	(l_ti.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? l_ti.ReparseTag : 0;
}

/*
**  A link, as lstat() sees it: a symbolic link or a junction
*/
static	int	s_vbk$islink	(
		DWORD		a_tag
			)
{
	return	(a_tag == IO_REPARSE_TAG_SYMLINK) || (a_tag == IO_REPARSE_TAG_MOUNT_POINT);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Open a file or a directory by CreateFileW.  The other reparse
**	points than links are followed even with <a_nofollow>: what a
**	deduplicated or a cloud file holds is the file.
**
**  FORMAL PARAMETERS:
**
**	a_w		The path, UTF-16
**	a_access	The access wanted
**	a_disp		The creation disposition
**	a_attrs		The attributes of a file created
**	a_nofollow	A link is opened itself, not followed
**	a_h		Receives the handle
**	a_tag		Receives the reparse tag, 0 - none; NULL - not wanted
**
**  RETURN VALUE:
**	0, or -1 and errno.
**--
*/
static	int	s_vbk$create	(
	const	wchar_t *	a_w,
		DWORD		a_access,
		DWORD		a_disp,
		DWORD		a_attrs,
		int		a_nofollow,
		HANDLE *	a_h,
		DWORD *		a_tag
			)
{
DWORD	l_share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, l_flags = a_attrs | FILE_FLAG_BACKUP_SEMANTICS, l_tag = 0;
HANDLE	l_h = INVALID_HANDLE_VALUE;

	if ( INVALID_HANDLE_VALUE == (l_h = CreateFileW(a_w, a_access, l_share, NULL, a_disp, l_flags | FILE_FLAG_OPEN_REPARSE_POINT, NULL)) )
		return	s_vbk$fail();

	if ( (l_tag = s_vbk$tag(l_h)) && !(a_nofollow && s_vbk$islink(l_tag)) )
		{
		/* Followed: the link, or a reparse point that is no link at all */
		CloseHandle(l_h);

		if ( INVALID_HANDLE_VALUE == (l_h = CreateFileW(a_w, a_access, l_share, NULL, a_disp, l_flags, NULL)) )
			return	s_vbk$fail();

		l_tag	= 0;
		}

	*a_h	= l_h;

	if ( a_tag )
		*a_tag	= l_tag;

	return	0;
}


/*
**  The attributes of an open file as struct stat; <a_tag> - its reparse
**  tag, nonzero when the link itself was opened
*/
static	int	s_vbk$hstat	(
		HANDLE		a_h,
		DWORD		a_tag,
		struct vbk$w_stat *a_st,
		struct timespec *a_btime
			)
{
BY_HANDLE_FILE_INFORMATION	l_bi;
FILE_BASIC_INFO			l_basic;
DWORD				l_type;

	memset(a_st, 0, sizeof(*a_st));

	if ( FILE_TYPE_DISK != (l_type = GetFileType(a_h)) )
		{
		/* A pipe or a console: a FIFO or a character device, as on Linux */
		a_st->st_mode	= ((l_type == FILE_TYPE_PIPE) ? S_IFIFO : S_IFCHR) | 0600;
		a_st->st_nlink	= 1;

		return	0;
		}

	if ( !GetFileInformationByHandle(a_h, &l_bi) )
		return	s_vbk$fail();

	a_st->st_dev	= l_bi.dwVolumeSerialNumber;
	a_st->st_ino	= ((uint64_t) l_bi.nFileIndexHigh << 32) | l_bi.nFileIndexLow;
	a_st->st_nlink	= l_bi.nNumberOfLinks;
	a_st->st_size	= (int64_t) (((uint64_t) l_bi.nFileSizeHigh << 32) | l_bi.nFileSizeLow);
	a_st->st_blksize = 4096;
	a_st->st_blocks	= (a_st->st_size + 511) / 512;

	if ( s_vbk$islink(a_tag) )
		a_st->st_mode	= S_IFLNK | 0777;
	else if ( l_bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
		a_st->st_mode	= S_IFDIR | 0755;
	else	a_st->st_mode	= S_IFREG | ((l_bi.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? 0444 : 0644);

	s_vbk$ft2ts(&l_bi.ftLastAccessTime, &a_st->st_atim);
	s_vbk$ft2ts(&l_bi.ftLastWriteTime, &a_st->st_mtim);

	/* The change time: NTFS keeps it, the API above does not give it */
	if ( GetFileInformationByHandleEx(a_h, FileBasicInfo, &l_basic, sizeof(l_basic)) )
		s_vbk$li2ts(&l_basic.ChangeTime, &a_st->st_ctim);
	else	a_st->st_ctim	= a_st->st_mtim;

	if ( a_btime )
		s_vbk$ft2ts(&l_bi.ftCreationTime, a_btime);

	return	0;
}

/*
**  The attributes of a file that cannot be opened even for its
**  attributes (the paging file, a file locked): from the directory
*/
static	int	s_vbk$fstatdir	(
	const	wchar_t *	a_w,
		struct vbk$w_stat *a_st,
		struct timespec *a_btime
			)
{
WIN32_FILE_ATTRIBUTE_DATA	l_ad;

	if ( !GetFileAttributesExW(a_w, GetFileExInfoStandard, &l_ad) )
		return	s_vbk$fail();

	memset(a_st, 0, sizeof(*a_st));

	a_st->st_nlink	= 1;
	a_st->st_size	= (int64_t) (((uint64_t) l_ad.nFileSizeHigh << 32) | l_ad.nFileSizeLow);
	a_st->st_blksize = 4096;
	a_st->st_blocks	= (a_st->st_size + 511) / 512;
	a_st->st_mode	= (l_ad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? (S_IFDIR | 0755)
			: (S_IFREG | ((l_ad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? 0444 : 0644));

	s_vbk$ft2ts(&l_ad.ftLastAccessTime, &a_st->st_atim);
	s_vbk$ft2ts(&l_ad.ftLastWriteTime, &a_st->st_mtim);
	a_st->st_ctim	= a_st->st_mtim;

	if ( a_btime )
		s_vbk$ft2ts(&l_ad.ftCreationTime, a_btime);

	return	0;
}

static	int	s_vbk$wstat	(
	const	wchar_t *	a_w,
		int		a_nofollow,
		struct vbk$w_stat *a_st,
		struct timespec *a_btime
			)
{
HANDLE	l_h = INVALID_HANDLE_VALUE;
DWORD	l_tag;
int	l_rc;

	if ( s_vbk$create(a_w, FILE_READ_ATTRIBUTES, OPEN_EXISTING, 0, a_nofollow, &l_h, &l_tag) )
		return	(errno == EACCES) ? s_vbk$fstatdir(a_w, a_st, a_btime) : -1;

	l_rc	= s_vbk$hstat(l_h, l_tag, a_st, a_btime);
	CloseHandle(l_h);

	return	l_rc;
}


int	vbk$w_stat	(
	const	char *		a_path,
		struct vbk$w_stat *a_st
			)
{
wchar_t	l_w [VBK$K_WMAX];

	return	s_vbk$wpath(a_path, l_w) ? s_vbk$wstat(l_w, 0, a_st, NULL) : -1;
}

int	vbk$w_lstat	(
	const	char *		a_path,
		struct vbk$w_stat *a_st
			)
{
wchar_t	l_w [VBK$K_WMAX];

	return	s_vbk$wpath(a_path, l_w) ? s_vbk$wstat(l_w, 1, a_st, NULL) : -1;
}

int	vbk$w_fstat	(
		int		a_fd,
		struct vbk$w_stat *a_st
			)
{
HANDLE	l_h = (HANDLE) _get_osfhandle(a_fd);

	if ( l_h == INVALID_HANDLE_VALUE )
		return	errno = EBADF, -1;

	return	s_vbk$hstat(l_h, s_vbk$tag(l_h), a_st, NULL);
}

int	vbk$w_fstatat	(
		int		a_dirfd,
	const	char *		a_name,
		struct vbk$w_stat *a_st,
		int		a_flags
			)
{
wchar_t	l_w [VBK$K_WMAX];

	return	s_vbk$atpath(a_dirfd, a_name, l_w) ? s_vbk$wstat(l_w, a_flags & AT_SYMLINK_NOFOLLOW, a_st, NULL) : -1;
}


/*
**  statx(): what stat() gives, and the creation time
*/
int	vbk$w_statx	(
		int		a_dirfd,
	const	char *		a_path,
		int		a_flags,
		unsigned	a_mask,
		struct vbk$w_statx *a_stx
			)
{
wchar_t		l_w [VBK$K_WMAX];
struct vbk$w_stat l_st;
struct timespec	l_bt = { 0 };
HANDLE		l_h = INVALID_HANDLE_VALUE;
int		l_rc;

	(void) a_mask;

	if ( (a_flags & AT_EMPTY_PATH) && !*a_path )
		{
		if ( INVALID_HANDLE_VALUE == (l_h = (HANDLE) _get_osfhandle(a_dirfd)) )
			return	errno = EBADF, -1;

		l_rc	= s_vbk$hstat(l_h, s_vbk$tag(l_h), &l_st, &l_bt);
		}
	else if ( !s_vbk$atpath(a_dirfd, a_path, l_w) )
		return	-1;
	else	l_rc	= s_vbk$wstat(l_w, a_flags & AT_SYMLINK_NOFOLLOW, &l_st, &l_bt);

	if ( l_rc )
		return	l_rc;

	memset(a_stx, 0, sizeof(*a_stx));

	a_stx->stx_mask		= STATX_BASIC_STATS | STATX_BTIME;
	a_stx->stx_blksize	= (uint32_t) l_st.st_blksize;
	a_stx->stx_nlink	= l_st.st_nlink;
	a_stx->stx_uid		= l_st.st_uid;
	a_stx->stx_gid		= l_st.st_gid;
	a_stx->stx_mode		= (uint16_t) l_st.st_mode;
	a_stx->stx_ino		= l_st.st_ino;
	a_stx->stx_size		= (uint64_t) l_st.st_size;
	a_stx->stx_blocks	= (uint64_t) l_st.st_blocks;
	a_stx->stx_dev_major	= 0;			/* makedev(0, serial) is st_dev of stat()	*/
	a_stx->stx_dev_minor	= (uint32_t) l_st.st_dev;

	a_stx->stx_atime.tv_sec	 = l_st.st_atim.tv_sec;
	a_stx->stx_atime.tv_nsec = (uint32_t) l_st.st_atim.tv_nsec;
	a_stx->stx_mtime.tv_sec	 = l_st.st_mtim.tv_sec;
	a_stx->stx_mtime.tv_nsec = (uint32_t) l_st.st_mtim.tv_nsec;
	a_stx->stx_ctime.tv_sec	 = l_st.st_ctim.tv_sec;
	a_stx->stx_ctime.tv_nsec = (uint32_t) l_st.st_ctim.tv_nsec;
	a_stx->stx_btime.tv_sec	 = l_bt.tv_sec;
	a_stx->stx_btime.tv_nsec = (uint32_t) l_bt.tv_nsec;

	return	0;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	open() of POSIX: see the module description.  O_NOFOLLOW refuses a
**	link (ELOOP), O_DIRECTORY anything but a directory (ENOTDIR).
**
**  FORMAL PARAMETERS:
**
**	a_w		The path, UTF-16
**	a_flags		O_* of POSIX
**	a_mode		The mode of a file created: read-only without S_IWUSR
**
**  RETURN VALUE:
**	A descriptor, or -1 and errno.
**--
*/
static	int	s_vbk$wopen	(
	const	wchar_t *	a_w,
		int		a_flags,
		unsigned	a_mode
			)
{
DWORD	l_access, l_disp, l_tag;
HANDLE	l_h = INVALID_HANDLE_VALUE;
BY_HANDLE_FILE_INFORMATION l_bi;
int	l_fd, l_isdir;

	/* The console (the passphrase, src/vbkkey.c): opened the way a console is, no flags of a file */
	if ( !wcscmp(a_w, L"CONIN$") )
		{
		if ( INVALID_HANDLE_VALUE == (l_h = CreateFileW(a_w, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL)) )
			return	s_vbk$fail();

		if ( 0 > (l_fd = _open_osfhandle((intptr_t) l_h, 0)) )
			return	CloseHandle(l_h), errno = EMFILE, -1;

		return	l_fd;
		}

	switch ( a_flags & (O_RDONLY | O_WRONLY | O_RDWR) )
		{
		case	O_WRONLY:	l_access = GENERIC_WRITE;			break;
		case	O_RDWR:		l_access = GENERIC_READ | GENERIC_WRITE;	break;
		default:		l_access = GENERIC_READ;
		}

	if ( (a_flags & O_CREAT) && (a_flags & O_EXCL) )
		l_disp	= CREATE_NEW;
	else if ( (a_flags & O_CREAT) && (a_flags & O_TRUNC) )
		l_disp	= CREATE_ALWAYS;
	else if ( a_flags & O_CREAT )
		l_disp	= OPEN_ALWAYS;
	else if ( a_flags & O_TRUNC )
		l_disp	= TRUNCATE_EXISTING;
	else	l_disp	= OPEN_EXISTING;

	/* A directory: its attributes are put back through it too (fchmod, futimens) */
	if ( (a_flags & O_DIRECTORY) && !s_vbk$create(a_w, l_access | FILE_WRITE_ATTRIBUTES, l_disp, 0, a_flags & O_NOFOLLOW, &l_h, &l_tag) )
		;
	else if ( s_vbk$create(a_w, l_access, l_disp, ((a_flags & O_CREAT) && !(a_mode & S_IWUSR)) ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL,
			a_flags & O_NOFOLLOW, &l_h, &l_tag) )
		return	-1;

	if ( l_tag )
		{
		CloseHandle(l_h);

		return	errno = ELOOP, -1;
		}

	l_isdir	= GetFileInformationByHandle(l_h, &l_bi) && (l_bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);

	if ( ((a_flags & O_DIRECTORY) && !l_isdir) || (l_isdir && (l_access & GENERIC_WRITE)) )
		{
		CloseHandle(l_h);

		return	errno = l_isdir ? EISDIR : ENOTDIR, -1;
		}

	if ( 0 > (l_fd = _open_osfhandle((intptr_t) l_h, ((a_flags & O_APPEND) ? _O_APPEND : 0) | (((a_flags & 3) == O_RDONLY) ? _O_RDONLY : 0))) )
		{
		CloseHandle(l_h);

		return	errno = EMFILE, -1;
		}

	_setmode(l_fd, _O_BINARY);

	return	l_fd;
}

int	vbk$w_open	(
	const	char *		a_path,
		int		a_flags,
			...
			)
{
wchar_t		l_w [VBK$K_WMAX];
unsigned	l_mode = 0666;
va_list		l_ap;

	if ( a_flags & O_CREAT )
		{
		va_start(l_ap, a_flags);
		l_mode	= va_arg(l_ap, unsigned);
		va_end(l_ap);
		}

	return	s_vbk$wpath(a_path, l_w) ? s_vbk$wopen(l_w, a_flags, l_mode) : -1;
}

int	vbk$w_openat	(
		int		a_dirfd,
	const	char *		a_name,
		int		a_flags,
			...
			)
{
wchar_t		l_w [VBK$K_WMAX];
unsigned	l_mode = 0666;
va_list		l_ap;

	if ( a_flags & O_CREAT )
		{
		va_start(l_ap, a_flags);
		l_mode	= va_arg(l_ap, unsigned);
		va_end(l_ap);
		}

	return	s_vbk$atpath(a_dirfd, a_name, l_w) ? s_vbk$wopen(l_w, a_flags, l_mode) : -1;
}


/*
**  fopen(): binary always, the 'e' of glibc (close on exec) left out
*/
FILE *	vbk$w_fopen	(
	const	char *		a_path,
	const	char *		a_mode
			)
{
wchar_t	l_w [VBK$K_WMAX], l_m [16];
size_t	l_n = 0;

	if ( !s_vbk$wpath(a_path, l_w) )
		return	NULL;

	for ( const char *l_p = a_mode; *l_p && (l_n < 12); l_p++ )
		if ( (*l_p != 'e') && (*l_p != 'b') && (*l_p != 't') )
			l_m [l_n++] = (wchar_t) *l_p;

	l_m [l_n++] = L'b';
	l_m [l_n]   = L'\0';

	return	_wfopen(l_w, l_m);
}


/*
**  tmpfile(): in the temporary directory of the user, removed at its close
**  - the one of the C library is made in the root of the drive, which a
**  user may not write
*/
FILE *	vbk$w_tmpfile	(void)
{
wchar_t	l_dir [MAX_PATH + 1], l_name [MAX_PATH + 1];
HANDLE	l_h;
FILE *	l_f;
int	l_fd;

	if ( !GetTempPathW(MAX_PATH + 1, l_dir) || !GetTempFileNameW(l_dir, L"vbk", 0, l_name) )
		return	s_vbk$fail(), NULL;

	if ( INVALID_HANDLE_VALUE == (l_h = CreateFileW(l_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL)) )
		{
		s_vbk$fail();
		DeleteFileW(l_name);

		return	NULL;
		}

	if ( 0 > (l_fd = _open_osfhandle((intptr_t) l_h, 0)) )
		return	CloseHandle(l_h), errno = EMFILE, NULL;

	if ( !(l_f = _fdopen(l_fd, "w+b")) )
		_close(l_fd);

	return	l_f;
}


int64_t	vbk$w_lseek	(
		int		a_fd,
		int64_t		a_off,
		int		a_whence
			)
{
	if ( (a_whence == SEEK_DATA) || (a_whence == SEEK_HOLE) )
		return	errno = EINVAL, -1;

	return	_lseeki64(a_fd, a_off, a_whence);
}

/*
**  Read and write at an offset, by the OVERLAPPED of a synchronous handle.
**  Unlike POSIX it moves the file pointer: a descriptor is to be read or
**  written either by offset or in sequence, never both - as every caller
**  does today (src/vbkrst.c: the file by pwrite, the standard output by
**  write; src/vbkvms.c: raw records by pwrite, texts by write).
*/
int64_t	vbk$w_pread	(
		int		a_fd,
		void *		a_buf,
		size_t		a_len,
		int64_t		a_off
			)
{
HANDLE		l_h = (HANDLE) _get_osfhandle(a_fd);
OVERLAPPED	l_ov = { 0 };
DWORD		l_n = 0;

	if ( l_h == INVALID_HANDLE_VALUE )
		return	errno = EBADF, -1;

	l_ov.Offset	= (DWORD) a_off;
	l_ov.OffsetHigh	= (DWORD) ((uint64_t) a_off >> 32);

	if ( !ReadFile(l_h, a_buf, (DWORD) ((a_len > 0x40000000U) ? 0x40000000U : a_len), &l_n, &l_ov) )
		return	(GetLastError() == ERROR_HANDLE_EOF) ? 0 : s_vbk$fail();

	return	(int64_t) l_n;
}

int64_t	vbk$w_pwrite	(
		int		a_fd,
	const	void *		a_buf,
		size_t		a_len,
		int64_t		a_off
			)
{
HANDLE		l_h = (HANDLE) _get_osfhandle(a_fd);
OVERLAPPED	l_ov = { 0 };
DWORD		l_n = 0;

	if ( l_h == INVALID_HANDLE_VALUE )
		return	errno = EBADF, -1;

	l_ov.Offset	= (DWORD) a_off;
	l_ov.OffsetHigh	= (DWORD) ((uint64_t) a_off >> 32);

	if ( !WriteFile(l_h, a_buf, (DWORD) ((a_len > 0x40000000U) ? 0x40000000U : a_len), &l_n, &l_ov) )
		return	s_vbk$fail();

	return	(int64_t) l_n;
}

int	vbk$w_ftruncate	(
		int		a_fd,
		int64_t		a_len
			)
{
errno_t	l_rc;

	if ( (l_rc = _chsize_s(a_fd, a_len)) )
		return	errno = l_rc, -1;

	return	0;
}

/*
**  fsync(): a directory has nothing to flush on Windows
*/
int	vbk$w_fsync	(
		int		a_fd
			)
{
HANDLE	l_h = (HANDLE) _get_osfhandle(a_fd);
BY_HANDLE_FILE_INFORMATION l_bi;

	if ( l_h == INVALID_HANDLE_VALUE )
		return	errno = EBADF, -1;

	if ( GetFileInformationByHandle(l_h, &l_bi) && (l_bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
		return	0;

	if ( (GetFileType(l_h) == FILE_TYPE_DISK) && !FlushFileBuffers(l_h) )
		return	s_vbk$fail();

	return	0;
}

/*
**  flock() by LockFileEx: the whole file, from the start on
*/
int	vbk$w_flock	(
		int		a_fd,
		int		a_op
			)
{
HANDLE		l_h = (HANDLE) _get_osfhandle(a_fd);
OVERLAPPED	l_ov = { 0 };
DWORD		l_flags = 0;

	if ( l_h == INVALID_HANDLE_VALUE )
		return	errno = EBADF, -1;

	if ( a_op & LOCK_UN )
		return	UnlockFileEx(l_h, 0, MAXDWORD, MAXDWORD, &l_ov) ? 0 : s_vbk$fail();

	if ( a_op & LOCK_EX )
		l_flags	|= LOCKFILE_EXCLUSIVE_LOCK;

	if ( a_op & LOCK_NB )
		l_flags	|= LOCKFILE_FAIL_IMMEDIATELY;

	if ( !LockFileEx(l_h, l_flags, 0, MAXDWORD, MAXDWORD, &l_ov) )
		return	(GetLastError() == ERROR_LOCK_VIOLATION) ? (errno = EWOULDBLOCK, -1) : s_vbk$fail();

	return	0;
}


/*
**  mkdir(): a directory that is there already, a root ("C:\") among
**  them, is EEXIST, whatever Windows says
*/
int	vbk$w_mkdir	(
	const	char *		a_path,
		unsigned	a_mode
			)
{
wchar_t	l_w [VBK$K_WMAX];
DWORD	l_attr;

	(void) a_mode;

	if ( !s_vbk$wpath(a_path, l_w) )
		return	-1;

	if ( CreateDirectoryW(l_w, NULL) )
		return	0;

	if ( (INVALID_FILE_ATTRIBUTES != (l_attr = GetFileAttributesW(l_w))) )
		return	errno = EEXIST, -1;

	return	s_vbk$fail();
}

int	vbk$w_rmdir	(
	const	char *		a_path
			)
{
wchar_t	l_w [VBK$K_WMAX];

	if ( !s_vbk$wpath(a_path, l_w) )
		return	-1;

	return	RemoveDirectoryW(l_w) ? 0 : s_vbk$fail();
}

/*
**  unlink() of a name: a read-only file too, as on Linux; a link to a
**  directory is removed as the directory it is to Windows
*/
static	int	s_vbk$wunlink	(
	const	wchar_t *	a_w
			)
{
DWORD	l_attr = GetFileAttributesW(a_w), l_err;

	if ( l_attr == INVALID_FILE_ATTRIBUTES )
		return	s_vbk$fail();

	if ( l_attr & FILE_ATTRIBUTE_DIRECTORY )
		{
		if ( !(l_attr & FILE_ATTRIBUTE_REPARSE_POINT) )
			return	errno = EISDIR, -1;

		return	RemoveDirectoryW(a_w) ? 0 : s_vbk$fail();
		}

	if ( (l_attr & FILE_ATTRIBUTE_READONLY) && !SetFileAttributesW(a_w, l_attr & ~FILE_ATTRIBUTE_READONLY) )
		return	s_vbk$fail();

	if ( DeleteFileW(a_w) )
		return	0;

	/* Not removed: the read-only attribute cleared above is put back */
	l_err	= GetLastError();

	if ( l_attr & FILE_ATTRIBUTE_READONLY )
		SetFileAttributesW(a_w, l_attr);

	return	errno = s_vbk$errno(l_err), -1;
}

int	vbk$w_unlink	(
	const	char *		a_path
			)
{
wchar_t	l_w [VBK$K_WMAX];

	return	s_vbk$wpath(a_path, l_w) ? s_vbk$wunlink(l_w) : -1;
}

int	vbk$w_unlinkat	(
		int		a_dirfd,
	const	char *		a_name,
		int		a_flags
			)
{
wchar_t	l_w [VBK$K_WMAX];

	if ( !s_vbk$atpath(a_dirfd, a_name, l_w) )
		return	-1;

	if ( a_flags & AT_REMOVEDIR )
		return	RemoveDirectoryW(l_w) ? 0 : s_vbk$fail();

	return	s_vbk$wunlink(l_w);
}

int	vbk$w_rename	(
	const	char *		a_old,
	const	char *		a_new
			)
{
wchar_t	l_o [VBK$K_WMAX], l_n [VBK$K_WMAX];

	if ( !s_vbk$wpath(a_old, l_o) || !s_vbk$wpath(a_new, l_n) )
		return	-1;

	return	MoveFileExW(l_o, l_n, MOVEFILE_REPLACE_EXISTING) ? 0 : s_vbk$fail();
}

int	vbk$w_link	(
	const	char *		a_old,
	const	char *		a_new
			)
{
wchar_t	l_o [VBK$K_WMAX], l_n [VBK$K_WMAX];

	if ( !s_vbk$wpath(a_old, l_o) || !s_vbk$wpath(a_new, l_n) )
		return	-1;

	return	CreateHardLinkW(l_n, l_o, NULL) ? 0 : s_vbk$fail();
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	symlink(): a symbolic link of NTFS.  It is a link to a directory
**	when its target is a directory now - Windows wants to know it, POSIX
**	does not; a target not there yet makes a link to a file.  Made
**	without the privilege when Windows allows it (developer mode).
**
**  FORMAL PARAMETERS:
**
**	a_target	What the link points at, UTF-8, '/' or '\'
**	a_path		The link
**
**  RETURN VALUE:
**	0, or -1 and errno: EPERM - the privilege to make links is not held.
**--
*/
int	vbk$w_symlink	(
	const	char *		a_target,
	const	char *		a_path
			)
{
wchar_t	l_link [VBK$K_WMAX], l_tgt [VBK$K_WMAX], l_full [VBK$K_WMAX];
DWORD	l_flags = 0, l_attr;
wchar_t	*l_slash;

	if ( !s_vbk$wpath(a_path, l_link) )
		return	-1;

	if ( !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_target, -1, l_tgt, VBK$K_WMAX) )
		return	errno = EINVAL, -1;

	for ( wchar_t *l_p = l_tgt; *l_p; l_p++ )
		if ( *l_p == L'/' )
			*l_p = L'\\';

	/* The target as the link will see it: relative - to the directory of the link */
	if ( (l_tgt [0] == L'\\') || (l_tgt [0] && (l_tgt [1] == L':')) )
		wcscpy(l_full, l_tgt);
	else	{
		wcscpy(l_full, l_link);

		if ( (l_slash = wcsrchr(l_full, L'\\')) && ((size_t) (l_slash - l_full + 1) + wcslen(l_tgt) < VBK$K_WMAX) )
			wcscpy(l_slash + 1, l_tgt);
		}

	if ( (INVALID_FILE_ATTRIBUTES != (l_attr = GetFileAttributesW(l_full))) && (l_attr & FILE_ATTRIBUTE_DIRECTORY) )
		l_flags	|= SYMBOLIC_LINK_FLAG_DIRECTORY;

	if ( CreateSymbolicLinkW(l_link, l_tgt, l_flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) )
		return	0;

	/* A Windows older than 10 1703 does not know the flag */
	if ( (GetLastError() == ERROR_INVALID_PARAMETER) && CreateSymbolicLinkW(l_link, l_tgt, l_flags) )
		return	0;

	return	s_vbk$fail();
}

/*
**  readlink(): the target of a symbolic link or a junction, '\' made
**  '/'; not ended by a NUL, as on Linux
*/
ssize_t	vbk$w_readlink	(
	const	char *		a_path,
		char *		a_buf,
		size_t		a_size
			)
{
wchar_t		l_w [VBK$K_WMAX];
union
	{
	VBK$REPARSE	rp;
	uint8_t		raw [MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
	} l_u;
HANDLE		l_h = INVALID_HANDLE_VALUE;
DWORD		l_n, l_tag;
const WCHAR *	l_name;
int		l_len;
char		l_out [VBACKUP$K_SZ_PATH * 2];

	if ( !s_vbk$wpath(a_path, l_w) || s_vbk$create(l_w, FILE_READ_ATTRIBUTES, OPEN_EXISTING, 0, 1, &l_h, &l_tag) )
		return	-1;

	if ( !l_tag || !DeviceIoControl(l_h, FSCTL_GET_REPARSE_POINT, NULL, 0, &l_u, sizeof(l_u), &l_n, NULL) )
		{
		CloseHandle(l_h);

		return	errno = EINVAL, -1;
		}

	CloseHandle(l_h);

	if ( l_u.rp.tag == IO_REPARSE_TAG_SYMLINK )
		{
		/* Relative: the substitute name is the target; absolute: the print name is the one a user wrote */
		if ( l_u.rp.u.sym.flags & VBK$M_SYMLINK_RELATIVE )
			l_name = l_u.rp.u.sym.buf + l_u.rp.subst_off / 2, l_len = l_u.rp.subst_len / 2;
		else	l_name = l_u.rp.u.sym.buf + l_u.rp.print_off / 2, l_len = l_u.rp.print_len / 2;

		if ( !l_len )
			l_name = l_u.rp.u.sym.buf + l_u.rp.subst_off / 2, l_len = l_u.rp.subst_len / 2;
		}
	else if ( l_u.rp.tag == IO_REPARSE_TAG_MOUNT_POINT )
		{
		l_name	= l_u.rp.u.mnt.buf + l_u.rp.print_off / 2;
		l_len	= l_u.rp.print_len / 2;

		if ( !l_len )
			l_name = l_u.rp.u.mnt.buf + l_u.rp.subst_off / 2, l_len = l_u.rp.subst_len / 2;
		}
	else	return	errno = EINVAL, -1;

	/* \??\C:\x of the substitute name: C:\x */
	if ( (l_len >= 4) && !wcsncmp(l_name, L"\\??\\", 4) )
		l_name += 4, l_len -= 4;

	if ( 0 > (l_len = s_vbk$utf8(l_name, l_len, l_out, sizeof(l_out))) )
		return	-1;

	if ( (size_t) l_len > a_size )
		l_len	= (int) a_size;

	memcpy(a_buf, l_out, (size_t) l_len);

	return	l_len;
}


int	vbk$w_access	(
	const	char *		a_path,
		int		a_mode
			)
{
wchar_t	l_w [VBK$K_WMAX];
DWORD	l_attr;

	if ( !s_vbk$wpath(a_path, l_w) )
		return	-1;

	if ( INVALID_FILE_ATTRIBUTES == (l_attr = GetFileAttributesW(l_w)) )
		return	s_vbk$fail();

	if ( (a_mode & W_OK) && (l_attr & FILE_ATTRIBUTE_READONLY) && !(l_attr & FILE_ATTRIBUTE_DIRECTORY) )
		return	errno = EACCES, -1;

	return	0;
}

/*
**  realpath(): the final path of the file - links resolved - in UTF-8,
**  "C:/dir/file"; //server/share/... for a share
*/
char *	vbk$w_realpath	(
	const	char *		a_path,
		char *		a_out
			)
{
wchar_t	l_w [VBK$K_WMAX], l_f [VBK$K_WMAX];
const wchar_t *l_p = l_f;
HANDLE	l_h = INVALID_HANDLE_VALUE;
DWORD	l_n;
char *	l_out = a_out;

	if ( !s_vbk$wpath(a_path, l_w) || s_vbk$create(l_w, FILE_READ_ATTRIBUTES, OPEN_EXISTING, 0, 0, &l_h, NULL) )
		return	NULL;

	l_n	= GetFinalPathNameByHandleW(l_h, l_f, VBK$K_WMAX, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	CloseHandle(l_h);

	if ( !l_n || (l_n >= VBK$K_WMAX) )
		return	s_vbk$fail(), NULL;

	if ( !wcsncmp(l_f, L"\\\\?\\UNC\\", 8) )
		{
		l_f [6]	= L'\\';
		l_p	= l_f + 6;
		}
	else if ( !wcsncmp(l_f, L"\\\\?\\", 4) )
		l_p	= l_f + 4;

	if ( !l_out && !(l_out = malloc(VBACKUP$K_SZ_PATH)) )
		return	errno = ENOMEM, NULL;

	if ( 0 > s_vbk$utf8(l_p, -1, l_out, VBACKUP$K_SZ_PATH) )
		{
		if ( !a_out )
			free(l_out);

		return	NULL;
		}

	return	l_out;
}


/*
**  The mode, as far as Windows has one: S_IWUSR is the read-only
**  attribute of a file.  A directory takes none: read-only on a
**  directory means another thing to Explorer.
*/
static	int	s_vbk$setro	(
		HANDLE		a_h,
		unsigned	a_mode
			)
{
FILE_BASIC_INFO	l_bi;
DWORD		l_attr;

	if ( !GetFileInformationByHandleEx(a_h, FileBasicInfo, &l_bi, sizeof(l_bi)) )
		return	s_vbk$fail();

	if ( l_bi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY )
		return	0;

	l_attr	= (a_mode & S_IWUSR) ? (l_bi.FileAttributes & ~FILE_ATTRIBUTE_READONLY) : (l_bi.FileAttributes | FILE_ATTRIBUTE_READONLY);

	if ( l_attr == l_bi.FileAttributes )
		return	0;

	l_bi.FileAttributes = l_attr ? l_attr : FILE_ATTRIBUTE_NORMAL;
	memset(&l_bi.CreationTime, 0, sizeof(LARGE_INTEGER) * 4);	/* The times: 0 - left as they are		*/

	return	SetFileInformationByHandle(a_h, FileBasicInfo, &l_bi, sizeof(l_bi)) ? 0 : s_vbk$fail();
}

int	vbk$w_fchmod	(
		int		a_fd,
		unsigned	a_mode
			)
{
HANDLE	l_h = (HANDLE) _get_osfhandle(a_fd);

	if ( l_h == INVALID_HANDLE_VALUE )
		return	errno = EBADF, -1;

	return	s_vbk$setro(l_h, a_mode);
}

int	vbk$w_fchmodat	(
		int		a_dirfd,
	const	char *		a_path,
		unsigned	a_mode,
		int		a_flags
			)
{
wchar_t	l_w [VBK$K_WMAX];
HANDLE	l_h = INVALID_HANDLE_VALUE;
DWORD	l_tag;
int	l_rc;

	if ( !s_vbk$atpath(a_dirfd, a_path, l_w) || s_vbk$create(l_w, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, OPEN_EXISTING, 0,
			a_flags & AT_SYMLINK_NOFOLLOW, &l_h, &l_tag) )
		return	-1;

	l_rc	= l_tag ? 0 : s_vbk$setro(l_h, a_mode);
	CloseHandle(l_h);

	return	l_rc;
}

int	vbk$w_chmod	(
	const	char *		a_path,
		unsigned	a_mode
			)
{
	return	vbk$w_fchmodat(AT_FDCWD, a_path, a_mode, 0);
}


/*
**  The access and modification times; UTIME_OMIT and UTIME_NOW as on Linux
*/
static	int	s_vbk$settimes	(
		HANDLE		a_h,
	const	struct timespec	a_ts [2]
			)
{
FILETIME	l_at, l_mt;
int		l_hasat = 1, l_hasmt = 1;

	if ( a_ts )
		{
		l_hasat	= s_vbk$ts2ft(&a_ts [0], &l_at);
		l_hasmt	= s_vbk$ts2ft(&a_ts [1], &l_mt);
		}
	else	GetSystemTimeAsFileTime(&l_at), l_mt = l_at;

	return	SetFileTime(a_h, NULL, l_hasat ? &l_at : NULL, l_hasmt ? &l_mt : NULL) ? 0 : s_vbk$fail();
}

int	vbk$w_utimensat	(
		int		a_dirfd,
	const	char *		a_path,
	const	struct timespec	a_ts [2],
		int		a_flags
			)
{
wchar_t	l_w [VBK$K_WMAX];
HANDLE	l_h = INVALID_HANDLE_VALUE;
int	l_rc;

	if ( !s_vbk$atpath(a_dirfd, a_path, l_w) || s_vbk$create(l_w, FILE_WRITE_ATTRIBUTES, OPEN_EXISTING, 0, a_flags & AT_SYMLINK_NOFOLLOW, &l_h, NULL) )
		return	-1;

	l_rc	= s_vbk$settimes(l_h, a_ts);
	CloseHandle(l_h);

	return	l_rc;
}

int	vbk$w_futimens	(
		int		a_fd,
	const	struct timespec	a_ts [2]
			)
{
HANDLE	l_h = (HANDLE) _get_osfhandle(a_fd);

	if ( l_h == INVALID_HANDLE_VALUE )
		return	errno = EBADF, -1;

	return	s_vbk$settimes(l_h, a_ts);
}


/*
**  No devices, FIFOs or sockets in a directory of Windows
*/
int	vbk$w_mknod	(
	const	char *		a_path,
		unsigned	a_mode,
		uint64_t	a_dev
			)
{
	(void) a_path;
	(void) a_mode;
	(void) a_dev;

	return	errno = ENOSYS, -1;
}

int	vbk$w_ioctl	(
		int		a_fd,
		unsigned long	a_req,
			...
			)
{
	(void) a_fd;
	(void) a_req;

	return	errno = ENOTTY, -1;
}

int	vbk$w_pipe	(
		int		a_fds [2]
			)
{
	return	_pipe(a_fds, 65536, _O_BINARY | _O_NOINHERIT);
}

long	vbk$w_sysconf	(
		int		a_name
			)
{
SYSTEM_INFO	l_si;

	GetSystemInfo(&l_si);

	switch ( a_name )
		{
		case	_SC_PAGESIZE:
			return	(long) l_si.dwPageSize;

		case	_SC_NPROCESSORS_ONLN:
			return	(long) GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
		}

	return	errno = EINVAL, -1;
}

/*
**  uname(): the name of the node, in UTF-8
*/
int	vbk$w_uname	(
		struct utsname *a_u
			)
{
wchar_t	l_w [256];
DWORD	l_n = 256;

	memset(a_u, 0, sizeof(*a_u));

	strcpy(a_u->sysname, "Windows");
#if	defined(__aarch64__)
	strcpy(a_u->machine, "aarch64");
#elif	defined(__x86_64__)
	strcpy(a_u->machine, "x86_64");
#else
	strcpy(a_u->machine, "i686");
#endif

	if ( !GetComputerNameExW(ComputerNameDnsHostname, l_w, &l_n) || (0 > s_vbk$utf8(l_w, (int) l_n, a_u->nodename, sizeof(a_u->nodename))) )
		strcpy(a_u->nodename, "localhost");

	return	0;
}

char *	vbk$w_strndup	(
	const	char *		a_s,
		size_t		a_n
			)
{
size_t	l_len = strnlen(a_s, a_n);
char *	l_p;

	if ( !(l_p = malloc(l_len + 1)) )
		return	NULL;

	memcpy(l_p, a_s, l_len);
	l_p [l_len] = '\0';

	return	l_p;
}

struct tm *vbk$w_localtime_r	(
	const	time_t *	a_t,
		struct tm *	a_tm
			)
{
	return	localtime_s(a_tm, a_t) ? NULL : a_tm;
}

struct tm *vbk$w_gmtime_r	(
	const	time_t *	a_t,
		struct tm *	a_tm
			)
{
	return	gmtime_s(a_tm, a_t) ? NULL : a_tm;
}

/*
**  tm_gmtoff of glibc: seconds east of UTC at the time given
*/
long	vbk$w_gmtoff	(
		time_t		a_t
			)
{
struct tm	l_tm;

	if ( localtime_s(&l_tm, &a_t) )
		return	0;

	return	(long) (_mkgmtime(&l_tm) - a_t);
}


/*
**  The name of a user: only the one of geteuid() (1, see win/vbkwin.h) has
**  one - the user that runs the image
*/
int	vbk$w_getpwuid_r	(
		uid_t		a_uid,
		struct passwd *	a_pw,
		char *		a_buf,
		size_t		a_size,
		struct passwd **a_res
			)
{
wchar_t	l_w [257];
DWORD	l_n = 257;

	*a_res	= NULL;

	if ( (a_uid != geteuid()) || !GetUserNameW(l_w, &l_n) || !l_n || (0 > s_vbk$utf8(l_w, (int) l_n - 1, a_buf, a_size)) )
		return	0;

	memset(a_pw, 0, sizeof(*a_pw));

	a_pw->pw_name	= a_buf;
	a_pw->pw_uid	= a_uid;
	*a_res		= a_pw;

	return	0;
}


/*
**  getenv(): the value in UTF-8, remembered - the C library keeps the
**  narrow environment in the ANSI code page.  HOME, when not set, is the
**  profile of the user: the journal is under it.
*/
char *	vbk$w_getenv	(
	const	char *		a_name
			)
{
wchar_t		l_wn [256];
const wchar_t *	l_wv;
char		l_v [VBACKUP$K_SZ_PATH];
char *		l_ret = NULL;
unsigned	l_i;

	if ( !MultiByteToWideChar(CP_UTF8, 0, a_name, -1, l_wn, 256) )
		return	NULL;

	if ( !(l_wv = _wgetenv(l_wn)) && !strcmp(a_name, "HOME") )
		l_wv	= _wgetenv(L"USERPROFILE");

	if ( !l_wv || (0 > s_vbk$utf8(l_wv, -1, l_v, sizeof(l_v))) )
		return	NULL;

	pthread_mutex_lock(&s_envlock);

	for ( l_i = 0; (l_i < VBK$K_ENVS) && s_envs [l_i].name && strcmp(s_envs [l_i].name, a_name); l_i++ )
		;

	if ( l_i < VBK$K_ENVS )
		{
		if ( s_envs [l_i].name && !strcmp(s_envs [l_i].value, l_v) )
			l_ret	= s_envs [l_i].value;
		else	{
			/* The old value is not freed: a caller may hold it still */
			if ( !s_envs [l_i].name )
				s_envs [l_i].name = strdup(a_name);

			l_ret	= s_envs [l_i].value = strdup(l_v);
			}
		}

	pthread_mutex_unlock(&s_envlock);

	return	l_ret;
}


/*
**  setenv(): in the wide environment, where getenv() above looks; an
**  empty value removes the name, as unsetenv() does
*/
int	vbk$w_setenv	(
	const	char *		a_name,
	const	char *		a_value,
		int		a_over
			)
{
wchar_t	l_wn [256], l_wv [VBK$K_WMAX];

	if ( !MultiByteToWideChar(CP_UTF8, 0, a_name, -1, l_wn, 256) || !MultiByteToWideChar(CP_UTF8, 0, a_value, -1, l_wv, VBK$K_WMAX) )
		return	errno = EINVAL, -1;

	if ( !a_over && _wgetenv(l_wn) )
		return	0;

	if ( _wputenv_s(l_wn, l_wv) )
		return	errno = EINVAL, -1;

	return	0;
}


/*
**  The directories: FindFirstFileExW, the names in UTF-8
*/
VBK$W_DIR *	vbk$w_opendir	(
	const	char *		a_path
			)
{
wchar_t		l_w [VBK$K_WMAX];
size_t		l_len, l_plen = strlen(a_path);
VBK$W_DIR *	l_d;
DWORD		l_attr;

	if ( !s_vbk$wpath(a_path, l_w) )
		return	NULL;

	if ( (INVALID_FILE_ATTRIBUTES == (l_attr = GetFileAttributesW(l_w))) )
		return	s_vbk$fail(), NULL;

	if ( !(l_attr & FILE_ATTRIBUTE_DIRECTORY) )
		return	errno = ENOTDIR, NULL;

	if ( ((l_len = wcslen(l_w)) + 3) >= VBK$K_WMAX )
		return	errno = ENAMETOOLONG, NULL;

	if ( l_w [l_len - 1] != L'\\' )
		l_w [l_len++] = L'\\';

	l_w [l_len++] = L'*';
	l_w [l_len]   = L'\0';

	if ( !(l_d = calloc(1, sizeof(*l_d) + l_plen + 1)) )
		return	errno = ENOMEM, NULL;

	if ( INVALID_HANDLE_VALUE == (l_d->find = FindFirstFileExW(l_w, FindExInfoBasic, &l_d->data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH)) )
		{
		/* An empty root has not even "." - an empty directory to POSIX */
		if ( GetLastError() != ERROR_FILE_NOT_FOUND )
			{
			free(l_d);

			return	s_vbk$fail(), NULL;
			}
		}

	l_d->first	= (l_d->find != INVALID_HANDLE_VALUE);
	l_d->fd		= -1;
	memcpy(l_d->path, a_path, l_plen + 1);

	return	l_d;
}

struct vbk$w_dirent *vbk$w_readdir	(
		VBK$W_DIR *	a_dir
			)
{
	for ( ;; )
		{
		if ( !a_dir->first )
			{
			if ( (a_dir->find == INVALID_HANDLE_VALUE) || !FindNextFileW(a_dir->find, &a_dir->data) )
				return	NULL;
			}

		a_dir->first	= 0;

		/* A name not in UTF-16 proper (a lone surrogate) has no UTF-8: left out */
		if ( 0 > s_vbk$utf8(a_dir->data.cFileName, -1, a_dir->ent.d_name, sizeof(a_dir->ent.d_name)) )
			continue;

		if ( (a_dir->data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && s_vbk$islink(a_dir->data.dwReserved0) )
			a_dir->ent.d_type = DT_LNK;
		else if ( a_dir->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			a_dir->ent.d_type = DT_DIR;
		else	a_dir->ent.d_type = DT_REG;

		a_dir->ent.d_ino = 0;

		return	&a_dir->ent;
		}
}

int	vbk$w_closedir	(
		VBK$W_DIR *	a_dir
			)
{
	if ( a_dir->find != INVALID_HANDLE_VALUE )
		FindClose(a_dir->find);

	if ( a_dir->fd >= 0 )
		close(a_dir->fd);

	free(a_dir);

	return	0;
}

/*
**  fdopendir(): the directory is read by its path; the descriptor
**  belongs to the stream from here on, as on Linux
*/
VBK$W_DIR *	vbk$w_fdopendir	(
		int		a_fd
			)
{
wchar_t		l_w [VBK$K_WMAX];
char		l_path [VBACKUP$K_SZ_PATH];
HANDLE		l_h = (HANDLE) _get_osfhandle(a_fd);
VBK$W_DIR *	l_d;
DWORD		l_n;

	if ( l_h == INVALID_HANDLE_VALUE )
		return	errno = EBADF, NULL;

	if ( !(l_n = GetFinalPathNameByHandleW(l_h, l_w, VBK$K_WMAX, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)) || (l_n >= VBK$K_WMAX) )
		return	s_vbk$fail(), NULL;

	if ( 0 > s_vbk$utf8(l_w, (int) l_n, l_path, sizeof(l_path)) )
		return	NULL;

	/* The \\?\ made '/' by s_vbk$utf8: put back, as s_vbk$wpath takes it whole */
	if ( !strncmp(l_path, "//?/", 4) )
		l_path [0] = l_path [1] = l_path [3] = '\\';

	if ( !(l_d = vbk$w_opendir(l_path)) )
		return	NULL;

	l_d->fd	= a_fd;

	return	l_d;
}

int	vbk$w_dirfd	(
		VBK$W_DIR *	a_dir
			)
{
	if ( (a_dir->fd < 0) && (0 > (a_dir->fd = vbk$w_open(a_dir->path, O_RDONLY | O_DIRECTORY))) )
		return	-1;

	return	a_dir->fd;
}


static	__thread int	(*s_scancmp) (const struct vbk$w_dirent **, const struct vbk$w_dirent **);

static	int	s_vbk$scancmp	(
	const	void *		a_a,
	const	void *		a_b
			)
{
	return	s_scancmp((const struct vbk$w_dirent **) a_a, (const struct vbk$w_dirent **) a_b);
}

int	vbk$w_scandir	(
	const	char *		a_path,
		struct vbk$w_dirent ***a_list,
		int		(*a_filter) (const struct vbk$w_dirent *),
		int		(*a_cmp) (const struct vbk$w_dirent **, const struct vbk$w_dirent **)
			)
{
VBK$W_DIR *		l_d;
struct vbk$w_dirent	*l_e, **l_list = NULL, **l_new;
size_t			l_n = 0, l_max = 0;

	if ( !(l_d = vbk$w_opendir(a_path)) )
		return	-1;

	while ( (l_e = vbk$w_readdir(l_d)) )
		{
		if ( a_filter && !a_filter(l_e) )
			continue;

		if ( l_n == l_max )
			{
			l_max	= l_max ? (l_max * 2) : 64;

			if ( !(l_new = realloc(l_list, l_max * sizeof(*l_list))) )
				break;

			l_list	= l_new;
			}

		if ( !(l_list [l_n] = malloc(sizeof(*l_e))) )
			break;

		memcpy(l_list [l_n++], l_e, sizeof(*l_e));
		}

	vbk$w_closedir(l_d);

	if ( l_e )
		{
		for ( size_t i = 0; i < l_n; i++ )
			free(l_list [i]);

		free(l_list);

		return	errno = ENOMEM, -1;
		}

	if ( a_cmp && l_n )
		{
		s_scancmp = a_cmp;
		qsort(l_list, l_n, sizeof(*l_list), s_vbk$scancmp);
		}

	*a_list	= l_list;

	return	(int) l_n;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Is a stored name one that must not be made on Windows?  A name that
**	is legal on Linux may mean something else here: ':' writes a stream
**	of another file, "CON" is the console, a trailing dot is dropped
**	(two names - one file), '\' is a separator.
**
**  FORMAL PARAMETERS:
**
**	a_name		The stored name, '/' between its components
**	a_len		Its length
**
**  RETURN VALUE:
**	1 - refused; 0 - it may be made.
**--
*/
int	vbk$w_badname	(
	const	char *		a_name,
		uint32_t	a_len
			)
{
static	const char * const s_dev [] = { "CON", "PRN", "AUX", "NUL", "COM", "LPT", "CONIN$", "CONOUT$" };

	for ( uint32_t i = 0, j; i < a_len; i = j + 1 )
		{
		uint32_t	l_clen, l_base;

		for ( j = i; (j < a_len) && (a_name [j] != '/'); j++ )
			if ( ((uint8_t) a_name [j] < 0x20) || strchr("<>:\"\\|?*", a_name [j]) )
				return	1;

		if ( !(l_clen = j - i) )
			continue;

		if ( (a_name [j - 1] == '.') || (a_name [j - 1] == ' ') )
			return	1;

		/* The name of a device, with an extension too: "nul.txt" is the device on Windows 10 */
		for ( l_base = 0; (l_base < l_clen) && (a_name [i + l_base] != '.'); l_base++ )
			;

		while ( l_base && (a_name [i + l_base - 1] == ' ') )
			l_base--;

		for ( size_t k = 0; k < (sizeof(s_dev) / sizeof(s_dev [0])); k++ )
			{
			size_t	l_dl = strlen(s_dev [k]);

			if ( (k == 4) || (k == 5) )
				{
				/* COM1..COM9, LPT1..LPT9, and the superscripts ¹ ² ³ (UTF-8 C2 B9, C2 B2, C2 B3) */
				if ( (l_base == 4) && !strncasecmp(a_name + i, s_dev [k], 3) && (a_name [i + 3] >= '1') && (a_name [i + 3] <= '9') )
					return	1;

				if ( (l_base == 5) && !strncasecmp(a_name + i, s_dev [k], 3) && ((uint8_t) a_name [i + 3] == 0xC2)
					&& (((uint8_t) a_name [i + 4] == 0xB9) || ((uint8_t) a_name [i + 4] == 0xB2) || ((uint8_t) a_name [i + 4] == 0xB3)) )
					return	1;
				}
			else if ( (l_base == l_dl) && !strncasecmp(a_name + i, s_dev [k], l_dl) )
				return	1;
			}
		}

	return	0;
}


/*
**  Is the file there under this very name, or under one that differs in
**  case only - the name of another file of a saveset from Linux, where
**  "README" and "readme" are two?  1 - the same name, 0 - another case.
*/
int	vbk$w_samecase	(
	const	char *		a_path
			)
{
wchar_t		l_w [VBK$K_WMAX];
WIN32_FIND_DATAW l_fd;
HANDLE		l_h;
const wchar_t *	l_last;
int		l_same;

	if ( !s_vbk$wpath(a_path, l_w) || (INVALID_HANDLE_VALUE == (l_h = FindFirstFileExW(l_w, FindExInfoBasic, &l_fd, FindExSearchNameMatch, NULL, 0))) )
		return	1;

	FindClose(l_h);

	l_last	= wcsrchr(l_w, L'\\') ? (wcsrchr(l_w, L'\\') + 1) : l_w;
	l_same	= !wcscmp(l_last, l_fd.cFileName);

	return	l_same;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The start of vbackup.exe: the command line in UTF-8, '\' made '/';
**	the standard streams binary - a saveset goes through them; the
**	console in UTF-8.
**
**  FORMAL PARAMETERS:
**
**	a_argc, a_argv	The arguments of main(), replaced
**
**  RETURN VALUE:
**	None.
**--
*/
static	UINT	s_cp;				/* The output code page of the console, put back at exit */

static	void	s_vbk$cpback	(void)
{
	if ( s_cp )
		SetConsoleOutputCP(s_cp);
}

void	vbk$w_init	(
		int *		a_argc,
		char ***	a_argv
			)
{
wchar_t **	l_wargv;
char **		l_argv;
char		l_arg [VBACKUP$K_SZ_PATH * 2];
int		l_argc, l_len;

	for ( int i = 0; i < 3; i++ )
		_setmode(i, _O_BINARY);

	/* The console in UTF-8 while this image runs; as it was after */
	if ( (s_cp = GetConsoleOutputCP()) && (s_cp != CP_UTF8) )
		{
		SetConsoleOutputCP(CP_UTF8);
		atexit(s_vbk$cpback);
		}

	if ( !(l_wargv = CommandLineToArgvW(GetCommandLineW(), &l_argc)) )
		return;

	if ( !(l_argv = calloc((size_t) l_argc + 1, sizeof(char *))) )
		return;

	for ( int i = 0; i < l_argc; i++ )
		{
		if ( 0 > (l_len = s_vbk$utf8(l_wargv [i], -1, l_arg, sizeof(l_arg))) )
			l_arg [0] = '\0';

		if ( !(l_argv [i] = strdup(l_arg)) )
			return;
		}

	LocalFree(l_wargv);

	*a_argc	= l_argc;
	*a_argv	= l_argv;
}


/*
**  What vbackup.exe has not: /PHYSICAL and /IMAGE are refused by the
**  command line already, these are never called
*/
int	vbk$phy_open	(
		VBK$OPTS *	a_opts,
	const	char *		a_spec
			)
{
	(void) a_opts;

	return	$VBKMSG(VBACKUP$_QUALUSE, "PHYSICAL", "not on Windows"), (void) a_spec, STS$K_ERROR;
}

int	vbk$phy_restore	(
		VBK$OPTS *	a_opts
			)
{
	(void) a_opts;

	return	$VBKMSG(VBACKUP$_QUALUSE, "PHYSICAL", "not on Windows"), STS$K_ERROR;
}

int	vbk$img_prepare	(
		VBK$OPTS *	a_opts
			)
{
	(void) a_opts;

	return	$VBKMSG(VBACKUP$_QUALUSE, "IMAGE", "not on Windows"), STS$K_ERROR;
}

int	vbk$img_restore	(
		VBK$OPTS *	a_opts
			)
{
	(void) a_opts;

	return	$VBKMSG(VBACKUP$_QUALUSE, "IMAGE", "not on Windows"), STS$K_ERROR;
}

/*
**  The read-ahead of files (src/vbkpre.c) is none: it is built on
**  preadv2(RWF_NOWAIT) of Linux.  VBK$PRE_TAKE says "not read ahead".
*/
void	vbk$pre_start	(
		VBK$OPTS *	a_opts
			)
{
	a_opts->pre	= NULL;
}

void	vbk$pre_stop	(
		VBK$OPTS *	a_opts
			)
{
	(void) a_opts;
}

void	vbk$pre_push	(
		struct vbk_pre_t *a_pre,
	const	char *		a_dir,
	const	char * const *	a_names,
		uint32_t	a_n
			)
{
	(void) a_pre;
	(void) a_dir;
	(void) a_names;
	(void) a_n;
}

void	vbk$pre_pop	(
		struct vbk_pre_t *a_pre
			)
{
	(void) a_pre;
}

int	vbk$pre_take	(
		struct vbk_pre_t *a_pre,
	const	char *		a_path
			)
{
	(void) a_pre;
	(void) a_path;

	return	-1;
}
