#ifndef	__VBKWCRT$H__
#define	__VBKWCRT$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKWCRT"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-17"
#endif

#ifndef	__REV__
#define	__REV__		"1.17.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	win/vbkwcrt.h
**
**  ABSTRACT:	What the C library of MinGW-w64 lacks and StarLet takes for
**		granted: TIME_UTC and timespec_get() of the old MSVCRT,
**		O_CLOEXEC, NAME_MAX, S___TIME of utility_routines.h.
**
**  DESCRIPTION: Put in front of every source of the Windows build by the
**		compiler (-include), StarLet's own sources too: StarLet is
**		compiled from its source kit there, not taken installed, and
**		is not changed for it.  The utility gets win/vbkwin.h, which
**		includes this one.
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

#ifndef	WIN32_LEAN_AND_MEAN
#define	WIN32_LEAN_AND_MEAN			/* As utility_routines.h of StarLet has it	*/
#endif

#ifndef	NOMINMAX
#define	NOMINMAX		1
#endif

#ifndef	NOGDI
#define	NOGDI			1		/* wingdi.h defines ERROR				*/
#endif

#include	<winsock2.h>
#include	<ws2tcpip.h>
#include	<windows.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<time.h>

/*
**  The time of day in nanoseconds: the MSVCRT has no timespec_get, only
**  the UCRT has
*/
#ifndef	TIME_UTC
#define	TIME_UTC	1

static inline int	timespec_get (
		struct timespec *	a_ts,
		int			a_base
			)
{
FILETIME	l_ft;
ULARGE_INTEGER	l_t;

	GetSystemTimePreciseAsFileTime(&l_ft);

	l_t.LowPart	= l_ft.dwLowDateTime;
	l_t.HighPart	= l_ft.dwHighDateTime;
	l_t.QuadPart	-= 116444736000000000ULL;	/* 1601 -> 1970, in 100 ns			*/

	a_ts->tv_sec	= (time_t) (l_t.QuadPart / 10000000ULL);
	a_ts->tv_nsec	= (long) ((l_t.QuadPart % 10000000ULL) * 100);

	return	a_base;
}
#endif

#ifndef	O_CLOEXEC
#define	O_CLOEXEC	O_NOINHERIT
#endif

#ifndef	NAME_MAX
#define	NAME_MAX	255
#endif

/*
**  utility_routines.h defines ____TIME for Windows and S___TIME for Linux
**  alone, and the prefix of $LOG calls S___TIME on both
*/
#define	s___time	____time

/*
**  Put in front of a source, this header leaves no __MODULE__ behind: the
**  source defines its own
*/
#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__

#endif	/* __VBKWCRT$H__ */
