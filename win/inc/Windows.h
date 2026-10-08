#ifndef	__VBKW_WINDOWS_H__
#define	__VBKW_WINDOWS_H__	1

#ifndef	__MODULE__
#define	__MODULE__	"WINDOWS"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-24"
#endif

#ifndef	__REV__
#define	__REV__		"1.24.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	win/inc/Windows.h
**
**  ABSTRACT:	The name StarLet includes <Windows.h> by: the headers of MinGW-w64 on a
**		case-sensitive file system have it in lower case.  On Windows itself
**		the name is one: <windows.h> of winsock2.h finds this very file -
**		the next one in the path is taken, not this one again.
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
**	X01-24		 8-OCT-2026	RRL
**		#include_next: built by the MinGW of MSYS2, on NTFS, Windows.h and
**		windows.h are one file - it took itself, and windows.h was empty.
**
**--
*/

#include_next	<windows.h>

#endif
