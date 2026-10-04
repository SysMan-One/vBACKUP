#ifndef	__VBKOS$H__
#define	__VBKOS$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKOS"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-04"
#endif

#ifndef	__REV__
#define	__REV__		"1.4.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkos.h
**
**  ABSTRACT:	The shim between the saveset core (lib/) and the system:
**		the checksum and the few calls of the operating system the
**		core makes.  Today it is StarLet and POSIX; a port of the
**		reader to another system replaces this header, not the core.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-04		 4-OCT-2026	RRL
**		Windows (_WIN32): the STS$K values and the CRC of its own, no
**		StarLet; the calls of the reader - VBK$OS_OPEN, _CLOSE, _PREAD,
**		_FSIZE - for both systems; the page-cache hints are no-ops there.
**
**	X01-03		 3-OCT-2026	RRL
**		Hints to the page cache: VBK$OS_SEQ, VBK$OS_COLD, VBK$OS_DROP.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdint.h>
#include	<stddef.h>
#include	<stdlib.h>
#include	<errno.h>
#include	<fcntl.h>

#ifdef	_WIN32

/*
**  Windows: no StarLet there.  The status values the core returns, the
**  same as StarLet's (utility_routines.h), and the CRC done here.
*/
#include	<windows.h>
#include	<io.h>
#include	<sys/types.h>
#include	<sys/stat.h>

enum	{
	STS$K_WARN	= 0,
	STS$K_SUCCESS	= 1,
	STS$K_ERROR	= 2,
	STS$K_INFO	= 3,
	STS$K_FATAL	= 4
	};

/*
**  CRC-32/IEEE, format.md section 1: reflected 0xEDB88320, a table of
**  256 made at the first call.  CRC("123456789") = 0xCBF43926.
*/
static inline uint32_t	vbk$os_crc32 (
		uint32_t	a_crc,
	const	void *		a_buf,
		size_t		a_len
			)
{
static	uint32_t	s_tab [256];
static	int		s_made;
const	uint8_t *	l_p = (const uint8_t *) a_buf;

	if ( !s_made )
		{
		for ( uint32_t n = 0; n < 256; n++ )
			{
			uint32_t	l_c = n;

			for ( int k = 0; k < 8; k++ )
				l_c = (l_c & 1) ? (0xEDB88320U ^ (l_c >> 1)) : (l_c >> 1);

			s_tab [n] = l_c;
			}

		s_made	= 1;
		}

	a_crc	^= 0xFFFFFFFFU;

	while ( a_len-- )
		a_crc = s_tab [(a_crc ^ *l_p++) & 0xFF] ^ (a_crc >> 8);

	return	a_crc ^ 0xFFFFFFFFU;
}

#define	$VBK_CRC(crc, buf, len)		vbk$os_crc32((uint32_t) (crc), (buf), (size_t) (len))

/*
**  A name in UTF-8 - as a saveset and the command line give it - turned
**  into the UTF-16 of the system calls; 0 - it is not valid UTF-8, or
**  does not fit
*/
static inline int	vbk$os_wide (
	const	char *		a_utf8,
		wchar_t *	a_out,
		int		a_outsz
			)
{
	return	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_utf8, -1, a_out, a_outsz) > 0;
}

/*
**  The random octets of a saveset UUID: only the writer wants them, and
**  there is no writer on Windows
*/
static inline int	vbk$os_random (
		void *		a_buf,
		size_t		a_len
			)
{
	(void) a_buf;
	(void) a_len;

	return	STS$K_ERROR;
}

static inline void	vbk$os_seq (
		int		a_fd
			)
{
	(void) a_fd;
}

static inline int	vbk$os_cold (
		int		a_fd,
		uint64_t	a_size
			)
{
	(void) a_fd;
	(void) a_size;

	return	0;
}

static inline void	vbk$os_drop (
		int		a_fd,
		uint64_t	a_off,
		uint64_t	a_len
			)
{
	(void) a_fd;
	(void) a_off;
	(void) a_len;
}

/*
**  A volume opened for reading, in binary mode: -1 and errno, or a descriptor
*/
static inline int	vbk$os_open (
	const	char *		a_spec
			)
{
wchar_t	l_w [32768];

	if ( !vbk$os_wide(a_spec, l_w, (int) (sizeof(l_w) / sizeof(l_w [0]))) )
		{
		errno	= EINVAL;

		return	-1;
		}

	return	_wopen(l_w, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
}

static inline int	vbk$os_close (
		int		a_fd
			)
{
	return	_close(a_fd);
}

/*
**  Read at an offset: one thread reads a volume, so seek and read will do
*/
static inline int64_t	vbk$os_pread (
		int		a_fd,
		void *		a_buf,
		size_t		a_len,
		uint64_t	a_off
			)
{
	if ( _lseeki64(a_fd, (__int64) a_off, SEEK_SET) < 0 )
		return	-1;

	return	_read(a_fd, a_buf, (unsigned) ((a_len > 0x40000000U) ? 0x40000000U : a_len));
}

/*
**  A buffer of blocks: no aligned_alloc in the C library of Windows, and
**  malloc's 16 octets are enough for the XOR a quadword at a time.  Freed
**  by free() on both systems.
*/
static inline void *	vbk$os_balloc (
		size_t		a_size
			)
{
	return	malloc(a_size);
}

static inline int	vbk$os_fsize (
		int		a_fd,
		uint64_t *	a_size,
		int *		a_isreg
			)
{
struct _stati64	l_st;

	if ( _fstati64(a_fd, &l_st) )
		return	-1;

	*a_size	 = (uint64_t) l_st.st_size;
	*a_isreg = (l_st.st_mode & _S_IFMT) == _S_IFREG;

	return	0;
}

#else	/* Linux */

#include	<unistd.h>
#include	<sys/random.h>
#include	<sys/mman.h>
#include	<sys/stat.h>

#include	"utility_routines.h"

/*
**  CRC-32/IEEE, format.md section 1.  __util$crc32c of StarLet is that
**  one, its name notwithstanding: CRC("123456789") = 0xCBF43926.
*/
#define	$VBK_CRC(crc, buf, len)		((uint32_t) __util$crc32c((unsigned) (crc), (buf), (size_t) (len)))

/*
**  The 16 random octets of a saveset UUID
*/
static inline int	vbk$os_random (
		void *		a_buf,
		size_t		a_len
			)
{
	return	(getrandom(a_buf, a_len, 0) == (ssize_t) a_len) ? STS$K_SUCCESS : STS$K_ERROR;
}

/*
**  A file is about to be read from start to end: a larger read-ahead
*/
static inline void	vbk$os_seq (
		int		a_fd
			)
{
	posix_fadvise(a_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
	posix_fadvise(a_fd, 0, 0, POSIX_FADV_NOREUSE);
}

/*
**  Is a file "cold" - not a page of its head in the page cache?  Only
**  then are its pages dropped behind the read (VBK$OS_DROP): a file that
**  is in the cache is somebody's working data, and a backup that evicted
**  it would slow the system down.  The head is looked at before the
**  first read, so the read-ahead of this very read does not count.  A
**  file that cannot be mapped is taken as warm - it is left alone.
*/
static inline int	vbk$os_cold (
		int		a_fd,
		uint64_t	a_size
			)
{
unsigned char	l_vec [256];
size_t		l_pg = (size_t) sysconf(_SC_PAGESIZE), l_len, l_n;
void *		l_map;
int		l_cold = 1;

	if ( !a_size )
		return	0;

	l_len	= (a_size < (uint64_t) (sizeof(l_vec) * l_pg)) ? (size_t) a_size : (sizeof(l_vec) * l_pg);
	l_n	= (l_len + l_pg - 1) / l_pg;

	if ( MAP_FAILED == (l_map = mmap(NULL, l_len, PROT_READ, MAP_SHARED, a_fd, 0)) )
		return	0;

	if ( mincore(l_map, l_len, l_vec) )
		l_cold	= 0;

	for ( size_t i = 0; l_cold && (i < l_n); i++ )
		if ( l_vec [i] & 1 )
			l_cold	= 0;

	munmap(l_map, l_len);

	return	l_cold;
}

/*
**  The pages of a range that has been read or written are no longer wanted
*/
static inline void	vbk$os_drop (
		int		a_fd,
		uint64_t	a_off,
		uint64_t	a_len
			)
{
	posix_fadvise(a_fd, (off_t) a_off, (off_t) a_len, POSIX_FADV_DONTNEED);
}


/*
**  The calls of the reader: a volume opened, read at an offset, measured
*/
static inline int	vbk$os_open (
	const	char *		a_spec
			)
{
	return	open(a_spec, O_RDONLY | O_CLOEXEC);
}

static inline int	vbk$os_close (
		int		a_fd
			)
{
	return	close(a_fd);
}

static inline int64_t	vbk$os_pread (
		int		a_fd,
		void *		a_buf,
		size_t		a_len,
		uint64_t	a_off
			)
{
	return	(int64_t) pread(a_fd, a_buf, a_len, (off_t) a_off);
}

/*
**  A buffer of blocks, aligned on a cache line; its size is a multiple of
**  512, so of 64, as aligned_alloc wants.  Freed by free().
*/
static inline void *	vbk$os_balloc (
		size_t		a_size
			)
{
	return	aligned_alloc(64, a_size);
}

static inline int	vbk$os_fsize (
		int		a_fd,
		uint64_t *	a_size,
		int *		a_isreg
			)
{
struct stat	l_st;

	if ( fstat(a_fd, &l_st) )
		return	-1;

	*a_size	 = (uint64_t) l_st.st_size;
	*a_isreg = S_ISREG(l_st.st_mode);

	return	0;
}

#endif	/* _WIN32 */

#endif	/* __VBKOS$H__ */
