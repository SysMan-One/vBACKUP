#ifndef	__VBKWIN$H__
#define	__VBKWIN$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKWIN"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-18"
#endif

#ifndef	__REV__
#define	__REV__		"1.18.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	win/vbkwin.h
**
**  ABSTRACT:	The POSIX the utility is written against, on Windows: the
**		calls of the file system by their POSIX names, mapped onto
**		the routines of src/vbkosw.c, which take and give UTF-8 and
**		call the wide (UTF-16) API of Windows.
**
**  DESCRIPTION: Put in front of every source of vbackup.exe by the
**		compiler (-include), before any header of the source itself:
**		the headers of MinGW-w64 are taken here first, then their
**		names are redefined, so a later #include of them is a no-op
**		and every call of the source goes to src/vbkosw.c.  A call
**		missed is not possible - the names are macros for the whole
**		translation unit - and the test win.sh looks at the imports
**		of the image for the ANSI calls of the C library besides.
**
**		What the macros give, against the C library of MinGW:
**		  - names in UTF-8, not in the ANSI code page;
**		  - binary descriptors, always - no CR LF, no ^Z;
**		  - sizes and offsets of 64 bits;
**		  - times in nanoseconds, the creation time (statx);
**		  - the links of NTFS (symbolic and hard), O_NOFOLLOW;
**		  - inode, device, link count from the file system (hard
**		    links are found as on Linux);
**		  - a directory opened as a descriptor (O_DIRECTORY), and the
**		    *at() calls relative to it.
**
**		What Windows has not, refused by the routine (errno ENOSYS or
**		ENOTTY), so that the caller reports it as it would on Linux:
**		mknod, mkfifo, ioctl, the extended attributes.  Owners are
**		not put back: chown and friends do nothing and succeed.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  6-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-18		 6-OCT-2026	RRL
**		The extended attributes are the alternate data streams of NTFS
**		("user.<stream>"), real routines of src/vbkosw.c now.
**
**	X01-17		 6-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	"vbkwcrt.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<stdint.h>
#include	<stdarg.h>
#include	<string.h>
#include	<strings.h>
#include	<ctype.h>
#include	<errno.h>
#include	<limits.h>
#include	<signal.h>
#include	<io.h>
#include	<direct.h>
#include	<process.h>
#include	<wchar.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<dirent.h>
#include	<pthread.h>

/*
**  Types and constants of POSIX that MinGW-w64 has not
*/
typedef	uint32_t	uid_t;
typedef	uint32_t	gid_t;
typedef	uint32_t	nlink_t;

#define	VBK$W_DEV	uint64_t		/* dev_t of MinGW is 32 bits, ino_t is 16		*/

#ifndef	EOPNOTSUPP
#define	EOPNOTSUPP	ENOTSUP
#endif

#ifndef	ELOOP
#define	ELOOP		114
#endif

#ifndef	ENODATA
#define	ENODATA		120
#endif

#define	O_NOFOLLOW	0x01000000		/* Bits the C library of Windows does not use	*/
#define	O_DIRECTORY	0x02000000
#define	O_NOATIME	0
#define	O_NONBLOCK	0
#define	O_NOCTTY	0

#ifndef	F_OK
#define	F_OK		0
#endif

#undef	S_IFBLK
#undef	S_ISBLK
#define	S_IFBLK		0060000			/* The Linux values: 0x3000 of MinGW is S_IFCHR|S_IFIFO */
#define	S_IFLNK		0120000
#define	S_IFSOCK	0140000
#define	S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define	S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define	S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)

#ifndef	S_IRWXG
#define	S_IRWXG		0070
#define	S_IRGRP		0040
#define	S_IWGRP		0020
#define	S_IXGRP		0010
#define	S_IRWXO		0007
#define	S_IROTH		0004
#define	S_IWOTH		0002
#define	S_IXOTH		0001
#endif

#define	S_ISUID		04000
#define	S_ISGID		02000
#define	S_ISVTX		01000

#define	SEEK_DATA	3			/* Refused, EINVAL: the file is all data		*/
#define	SEEK_HOLE	4

#define	AT_FDCWD		(-100)
#define	AT_SYMLINK_NOFOLLOW	0x100
#define	AT_REMOVEDIR		0x200
#define	AT_EMPTY_PATH		0x1000

#define	UTIME_NOW	((1L << 30) - 1L)
#define	UTIME_OMIT	((1L << 30) - 2L)

#define	STATX_BASIC_STATS	0x07FFU
#define	STATX_BTIME		0x0800U

#define	SYNC_FILE_RANGE_WAIT_BEFORE	1
#define	SYNC_FILE_RANGE_WRITE		2
#define	SYNC_FILE_RANGE_WAIT_AFTER	4

#define	LOCK_SH		1
#define	LOCK_EX		2
#define	LOCK_NB		4
#define	LOCK_UN		8

#define	_SC_PAGESIZE		30
#define	_SC_NPROCESSORS_ONLN	84

#define	makedev(maj, min)	((((uint64_t) (maj)) << 32) | (uint32_t) (min))
#define	major(dev)		((uint32_t) (((uint64_t) (dev)) >> 32))
#define	minor(dev)		((uint32_t) (dev))

/*
**  The structures below are named as the macros at the end turn the POSIX
**  names into: struct stat is struct vbk$w_stat, struct statx is struct
**  vbk$w_statx, struct dirent is struct vbk$w_dirent.
**
**  What lstat() and friends give: the fields the utility reads, all of
**  them of the full width
*/
struct	vbk$w_stat
{
	VBK$W_DEV	st_dev;			/* Serial number of the volume			*/
	uint64_t	st_ino;			/* The file ID of NTFS				*/
	uint32_t	st_mode;
	uint32_t	st_nlink;
	uid_t		st_uid;
	gid_t		st_gid;
	uint64_t	st_rdev;
	int64_t		st_size;
	int64_t		st_blocks;
	int32_t		st_blksize;
	struct timespec	st_atim, st_mtim, st_ctim;
};

struct	statx_timestamp
{
	int64_t		tv_sec;
	uint32_t	tv_nsec;
	int32_t		reserved;
};

struct	vbk$w_statx
{
	uint32_t	stx_mask;
	uint32_t	stx_blksize;
	uint64_t	stx_attributes;
	uint32_t	stx_nlink;
	uint32_t	stx_uid;
	uint32_t	stx_gid;
	uint16_t	stx_mode;
	uint64_t	stx_ino;
	uint64_t	stx_size;
	uint64_t	stx_blocks;
	struct statx_timestamp	stx_atime, stx_btime, stx_ctime, stx_mtime;
	uint32_t	stx_rdev_major, stx_rdev_minor;
	uint32_t	stx_dev_major, stx_dev_minor;
};

/*
**  A directory entry: the name in UTF-8 - up to 255 UTF-16 units, three
**  octets each at most
*/
#define	DT_UNKNOWN	0
#define	DT_FIFO		1
#define	DT_CHR		2
#define	DT_DIR		4
#define	DT_BLK		6
#define	DT_REG		8
#define	DT_LNK		10
#define	DT_SOCK		12

struct	vbk$w_dirent
{
	uint64_t	d_ino;
	unsigned char	d_type;
	char		d_name [NAME_MAX * 3 + 1];
};

typedef	struct	vbk_w_dir_t	VBK$W_DIR;

/*
**  struct utsname of uname(): the name of the node is all the utility asks
*/
struct	utsname
{
	char	sysname [65];
	char	nodename [256];
	char	release [65];
	char	version [65];
	char	machine [65];
};

/*
**  The routines of src/vbkosw.c
*/
void	vbk$w_init	(int *a_argc, char ***a_argv);
int	vbk$w_badname	(const char *a_name, uint32_t a_len);
int	vbk$w_samecase	(const char *a_path);
int	vbk$w_open	(const char *a_path, int a_flags, ...);
int	vbk$w_openat	(int a_dirfd, const char *a_name, int a_flags, ...);
FILE *	vbk$w_fopen	(const char *a_path, const char *a_mode);
FILE *	vbk$w_tmpfile	(void);
int	vbk$w_stat	(const char *a_path, struct vbk$w_stat *a_st);
int	vbk$w_lstat	(const char *a_path, struct vbk$w_stat *a_st);
int	vbk$w_fstat	(int a_fd, struct vbk$w_stat *a_st);
int	vbk$w_fstatat	(int a_dirfd, const char *a_name, struct vbk$w_stat *a_st, int a_flags);
int	vbk$w_statx	(int a_dirfd, const char *a_path, int a_flags, unsigned a_mask, struct vbk$w_statx *a_stx);
int64_t	vbk$w_lseek	(int a_fd, int64_t a_off, int a_whence);
int64_t	vbk$w_pread	(int a_fd, void *a_buf, size_t a_len, int64_t a_off);
int64_t	vbk$w_pwrite	(int a_fd, const void *a_buf, size_t a_len, int64_t a_off);
int	vbk$w_ftruncate	(int a_fd, int64_t a_len);
int	vbk$w_fsync	(int a_fd);
int	vbk$w_flock	(int a_fd, int a_op);
int	vbk$w_mkdir	(const char *a_path, unsigned a_mode);
int	vbk$w_rmdir	(const char *a_path);
int	vbk$w_unlink	(const char *a_path);
int	vbk$w_unlinkat	(int a_dirfd, const char *a_name, int a_flags);
int	vbk$w_rename	(const char *a_old, const char *a_new);
int	vbk$w_link	(const char *a_old, const char *a_new);
int	vbk$w_symlink	(const char *a_target, const char *a_path);
ssize_t	vbk$w_readlink	(const char *a_path, char *a_buf, size_t a_size);
int	vbk$w_access	(const char *a_path, int a_mode);
char *	vbk$w_realpath	(const char *a_path, char *a_out);
int	vbk$w_chmod	(const char *a_path, unsigned a_mode);
int	vbk$w_fchmod	(int a_fd, unsigned a_mode);
int	vbk$w_fchmodat	(int a_dirfd, const char *a_path, unsigned a_mode, int a_flags);
int	vbk$w_utimensat	(int a_dirfd, const char *a_path, const struct timespec a_ts [2], int a_flags);
int	vbk$w_futimens	(int a_fd, const struct timespec a_ts [2]);
int	vbk$w_mknod	(const char *a_path, unsigned a_mode, uint64_t a_dev);
int	vbk$w_ioctl	(int a_fd, unsigned long a_req, ...);
int	vbk$w_pipe	(int a_fds [2]);
long	vbk$w_sysconf	(int a_name);
int	vbk$w_uname	(struct utsname *a_u);
char *	vbk$w_strndup	(const char *a_s, size_t a_n);
struct tm *vbk$w_localtime_r	(const time_t *a_t, struct tm *a_tm);
struct tm *vbk$w_gmtime_r	(const time_t *a_t, struct tm *a_tm);
long	vbk$w_gmtoff	(time_t a_t);
char *	vbk$w_getenv	(const char *a_name);
int	vbk$w_setenv	(const char *a_name, const char *a_value, int a_over);

VBK$W_DIR *		vbk$w_opendir	(const char *a_path);
VBK$W_DIR *		vbk$w_fdopendir	(int a_fd);
struct vbk$w_dirent *	vbk$w_readdir	(VBK$W_DIR *a_dir);
int			vbk$w_closedir	(VBK$W_DIR *a_dir);
int			vbk$w_dirfd	(VBK$W_DIR *a_dir);
int			vbk$w_scandir	(const char *a_path, struct vbk$w_dirent ***a_list,
					 int (*a_filter) (const struct vbk$w_dirent *),
					 int (*a_cmp) (const struct vbk$w_dirent **, const struct vbk$w_dirent **));

/*
**  The POSIX names, from here on the routines above
*/
#undef	stat
#undef	fstat
#undef	lstat
#undef	lseek
#undef	ftruncate
#undef	open
#undef	mkdir
#undef	rmdir
#undef	unlink
#undef	rename
#undef	access
#undef	chmod
#undef	fopen
#undef	tmpfile
#undef	opendir
#undef	readdir
#undef	closedir
#undef	getenv

#define	stat			vbk$w_stat
#define	lstat			vbk$w_lstat
#define	fstat			vbk$w_fstat
#define	fstatat			vbk$w_fstatat
#define	statx			vbk$w_statx
#define	open			vbk$w_open
#define	openat			vbk$w_openat
#define	fopen			vbk$w_fopen
#define	tmpfile			vbk$w_tmpfile
#define	lseek			vbk$w_lseek
#define	pread			vbk$w_pread
#define	pwrite			vbk$w_pwrite
#define	ftruncate		vbk$w_ftruncate
#define	fsync			vbk$w_fsync
#define	fdatasync		vbk$w_fsync
#define	flock			vbk$w_flock
#define	mkdir			vbk$w_mkdir
#define	rmdir			vbk$w_rmdir
#define	unlink			vbk$w_unlink
#define	unlinkat		vbk$w_unlinkat
#define	rename			vbk$w_rename
#define	link			vbk$w_link
#define	symlink			vbk$w_symlink
#define	readlink		vbk$w_readlink
#define	access			vbk$w_access
#define	realpath		vbk$w_realpath
#define	chmod			vbk$w_chmod
#define	fchmod			vbk$w_fchmod
#define	fchmodat		vbk$w_fchmodat
#define	utimensat		vbk$w_utimensat
#define	futimens		vbk$w_futimens
#define	mknod			vbk$w_mknod
#define	mkfifo(p, m)		vbk$w_mknod((p), S_IFIFO | (m), 0)
#define	ioctl			vbk$w_ioctl
#define	pipe			vbk$w_pipe
#define	sysconf			vbk$w_sysconf
#define	uname			vbk$w_uname
#define	strndup			vbk$w_strndup
#define	getenv			vbk$w_getenv
#define	setenv			vbk$w_setenv
#define	unsetenv(n)		vbk$w_setenv((n), "", 1)
#define	localtime_r		vbk$w_localtime_r
#define	gmtime_r		vbk$w_gmtime_r
#define	dirent			vbk$w_dirent
#define	DIR			VBK$W_DIR
#define	opendir			vbk$w_opendir
#define	fdopendir		vbk$w_fdopendir
#define	readdir			vbk$w_readdir
#define	closedir		vbk$w_closedir
#define	dirfd			vbk$w_dirfd
#define	scandir			vbk$w_scandir

#define	chown(p, u, g)		((void) (p), (void) (u), (void) (g), 0)
#define	lchown(p, u, g)		((void) (p), (void) (u), (void) (g), 0)
#define	fchown(f, u, g)		((void) (f), (void) (u), (void) (g), 0)
#define	getuid()		((uid_t) 1)
#define	geteuid()		((uid_t) 1)	/* Not root: the journal under HOME, /OWNER=DEFAULT	*/
#define	getgid()		((gid_t) 1)
#define	umask(m)		((void) (m), 0)
#define	aligned_alloc(a, n)	malloc(n)	/* malloc gives 16 octets; the vector code loads unaligned */

static inline int	sync_file_range (
		int		a_fd,
		int64_t		a_off,
		int64_t		a_len,
		unsigned	a_flags
			)
{
	(void) a_fd;
	(void) a_off;
	(void) a_len;
	(void) a_flags;

	return	0;
}

/*
**  The extended attributes are the alternate data streams of NTFS, as
**  ntfs-3g shows them on Linux: the stream "name" is "user.name".  The
**  other name spaces of Linux (security., trusted., system.) have no place
**  on Windows: a set of them is skipped, and succeeds.
*/
ssize_t	vbk$w_flistxattr	(int a_fd, char *a_list, size_t a_size);
ssize_t	vbk$w_llistxattr	(const char *a_path, char *a_list, size_t a_size);
ssize_t	vbk$w_fgetxattr		(int a_fd, const char *a_name, void *a_val, size_t a_size);
ssize_t	vbk$w_lgetxattr		(const char *a_path, const char *a_name, void *a_val, size_t a_size);
int	vbk$w_fsetxattr		(int a_fd, const char *a_name, const void *a_val, size_t a_size, int a_flags);
int	vbk$w_lsetxattr		(const char *a_path, const char *a_name, const void *a_val, size_t a_size, int a_flags);

#define	flistxattr		vbk$w_flistxattr
#define	llistxattr		vbk$w_llistxattr
#define	fgetxattr		vbk$w_fgetxattr
#define	lgetxattr		vbk$w_lgetxattr
#define	fsetxattr		vbk$w_fsetxattr
#define	lsetxattr		vbk$w_lsetxattr

/*
**  The flags of chattr: asked by ioctl(), which refuses (ENOTTY)
*/
#define	FS_IOC_GETFLAGS		0x80086601UL
#define	FS_IOC_SETFLAGS		0x40086602UL
#define	BLKGETSIZE64		0x80081272UL
#define	BLKSSZGET		0x1268UL
#define	BLKZEROOUT		0x127FUL
#define	FS_SYNC_FL		0x00000008
#define	FS_IMMUTABLE_FL		0x00000010
#define	FS_APPEND_FL		0x00000020
#define	FS_NODUMP_FL		0x00000040
#define	FS_NOATIME_FL		0x00000080
#define	FS_DIRSYNC_FL		0x00010000
#define	FS_NOCOW_FL		0x00800000

/*
**  The users and groups of the system: none known by name, but the one
**  that runs the image - geteuid() - for the "Written by" of a saveset
*/
struct	passwd
{
	char *	pw_name;
	uid_t	pw_uid;
	gid_t	pw_gid;
	char *	pw_dir;
};

struct	group
{
	char *	gr_name;
	gid_t	gr_gid;
};

int	vbk$w_getpwuid_r	(uid_t a_uid, struct passwd *a_pw, char *a_buf, size_t a_size, struct passwd **a_res);

#define	getpwnam(n)			((void) (n), (struct passwd *) NULL)
#define	getpwuid(u)			((void) (u), (struct passwd *) NULL)
#define	getgrnam(n)			((void) (n), (struct group *) NULL)
#define	getgrgid(g)			((void) (g), (struct group *) NULL)
#define	getpwnam_r(n, p, b, s, r)	((void) (n), (void) (p), (void) (b), (void) (s), *(r) = NULL, 0)
#define	getpwuid_r			vbk$w_getpwuid_r
#define	getgrnam_r(n, p, b, s, r)	((void) (n), (void) (p), (void) (b), (void) (s), *(r) = NULL, 0)
#define	getgrgid_r(g, p, b, s, r)	((void) (g), (void) (p), (void) (b), (void) (s), *(r) = NULL, 0)

/*
**  Put in front of a source, this header leaves no __MODULE__ behind
*/
#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__

#endif	/* __VBKWIN$H__ */
