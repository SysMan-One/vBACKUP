#ifndef	__VBKOS$H__
#define	__VBKOS$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKOS"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-03"
#endif

#ifndef	__REV__
#define	__REV__		"1.3.0"
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
#include	<unistd.h>
#include	<fcntl.h>
#include	<sys/random.h>
#include	<sys/mman.h>

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

#endif	/* __VBKOS$H__ */
