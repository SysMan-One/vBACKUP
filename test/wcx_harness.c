#define	__MODULE__	"WCX_HARNESS"
#define	__IDENT__	"X01-05"
#define	__REV__		"1.5.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	wcx_harness.c
**
**  ABSTRACT:	A file manager in miniature for test/wcx.sh: loads a WCX
**		plugin - dlopen on Linux, LoadLibraryW under wine - and
**		calls it the way Total Commander and Double Commander do.
**
**  DESCRIPTION:
**
**		wcx_harness <plugin> [-a] [-v <dir>] <op> <saveset> [<out> [<name>...]]
**
**		    c	CanYouHandleThisFile and GetPackerCaps
**		    l	the listing: "<attr hex> <size> <crc hex> <name>" a line,
**			names in UTF-8 with '/'
**		    t	every file tested (PK_TEST)
**		    x	every file - or the names given - extracted under <out>
**			(DestPath NULL, DestName the full name: as TC does)
**		    -a	the old ("ANSI") functions instead of the W ones
**		    -v	a volume asked for is looked for in <dir>
**
**		A file that fails: "ERR <code> <name>"; the completion is 0 -
**		all well, 1 - a file failed, 2 - the saveset did not open.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  4-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-05		 4-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdint.h>

#ifdef	_WIN32
#include	<windows.h>
#include	<shellapi.h>
#include	<io.h>
#include	<fcntl.h>
#define	WCX$CALL	__stdcall
typedef	wchar_t		WCX$WCHAR;
#define	WCX$SEP		'\\'
#else
#include	<dlfcn.h>
#define	WCX$CALL
typedef	uint16_t	WCX$WCHAR;
#define	WCX$SEP		'/'
#endif

#define	WCX$K_SZ	4096

typedef struct { WCX$WCHAR *ArcName; int OpenMode, OpenResult; WCX$WCHAR *CmtBuf; int CmtBufSize, CmtSize, CmtState; } WCX$OPENW;
typedef struct { char *ArcName; int OpenMode, OpenResult; char *CmtBuf; int CmtBufSize, CmtSize, CmtState; } WCX$OPEN;

typedef struct
{
	WCX$WCHAR	ArcName [1024], FileName [1024];
	int		Flags;
	unsigned int	PackSize, PackSizeHigh, UnpSize, UnpSizeHigh;
	int		HostOS, FileCRC, FileTime, UnpVer, Method, FileAttr;
	char *		CmtBuf;
	int		CmtBufSize, CmtSize, CmtState;
	char		Reserved [1024];
} WCX$HDREXW;

typedef struct
{
	char		ArcName [1024], FileName [1024];
	int		Flags;
	unsigned int	PackSize, PackSizeHigh, UnpSize, UnpSizeHigh;
	int		HostOS, FileCRC, FileTime, UnpVer, Method, FileAttr;
	char *		CmtBuf;
	int		CmtBufSize, CmtSize, CmtState;
	char		Reserved [1024];
} WCX$HDREX;

typedef	void *	(WCX$CALL *OPENW)	(WCX$OPENW *);
typedef	void *	(WCX$CALL *OPENA)	(WCX$OPEN *);
typedef	int	(WCX$CALL *READW)	(void *, WCX$HDREXW *);
typedef	int	(WCX$CALL *READA)	(void *, WCX$HDREX *);
typedef	int	(WCX$CALL *PROCW)	(void *, int, WCX$WCHAR *, WCX$WCHAR *);
typedef	int	(WCX$CALL *PROCA)	(void *, int, char *, char *);
typedef	int	(WCX$CALL *CLOSE)	(void *);
typedef	int	(WCX$CALL *CAPS)	(void);
typedef	int	(WCX$CALL *CANW)	(WCX$WCHAR *);
typedef	int	(WCX$CALL *CVPW)	(WCX$WCHAR *, int);
typedef	int	(WCX$CALL *PDPW)	(WCX$WCHAR *, int);
typedef	void	(WCX$CALL *SETCVPW)	(void *, CVPW);
typedef	void	(WCX$CALL *SETPDPW)	(void *, PDPW);

static	const char *	s_voldir;
static	long long	s_progress;

static	void	s_wide	(const char *a_in, WCX$WCHAR *a_out, size_t a_n)
{
#ifdef	_WIN32
	if ( !MultiByteToWideChar(CP_UTF8, 0, a_in, -1, a_out, (int) a_n) )
		a_out [0] = 0;
#else
size_t	o = 0;
const unsigned char *p = (const unsigned char *) a_in;

	while ( *p && (o + 2 < a_n) )
		{
		uint32_t c = *p++;
		int	 n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;

		c	&= (n == 3) ? 0x07 : (n == 2) ? 0x0F : (n == 1) ? 0x1F : 0x7F;

		for ( ; n && *p; n-- )
			c = (c << 6) | (*p++ & 0x3F);

		if ( c >= 0x10000 )
			{
			c	-= 0x10000;
			a_out [o++] = (WCX$WCHAR) (0xD800 | (c >> 10));
			a_out [o++] = (WCX$WCHAR) (0xDC00 | (c & 0x3FF));
			}
		else	a_out [o++] = (WCX$WCHAR) c;
		}

	a_out [o] = 0;
#endif
}

static	void	s_narrow	(const WCX$WCHAR *a_in, char *a_out, size_t a_n)
{
size_t	o = 0;

	for ( ; *a_in && (o + 5 < a_n); a_in++ )
		{
		uint32_t c = *a_in;

		if ( (c >= 0xD800) && (c < 0xDC00) && a_in [1] )
			c = 0x10000 + ((c - 0xD800) << 10) + (*++a_in - 0xDC00);

		if ( c < 0x80 )
			a_out [o++] = (char) c;
		else if ( c < 0x800 )
			{
			a_out [o++] = (char) (0xC0 | (c >> 6));
			a_out [o++] = (char) (0x80 | (c & 0x3F));
			}
		else if ( c < 0x10000 )
			{
			a_out [o++] = (char) (0xE0 | (c >> 12));
			a_out [o++] = (char) (0x80 | ((c >> 6) & 0x3F));
			a_out [o++] = (char) (0x80 | (c & 0x3F));
			}
		else	{
			a_out [o++] = (char) (0xF0 | (c >> 18));
			a_out [o++] = (char) (0x80 | ((c >> 12) & 0x3F));
			a_out [o++] = (char) (0x80 | ((c >> 6) & 0x3F));
			a_out [o++] = (char) (0x80 | (c & 0x3F));
			}
		}

	a_out [o] = '\0';
}

/*
**  A volume asked for: the same name in the directory of -v; none - no
*/
static	int	WCX$CALL	s_cvp	(WCX$WCHAR *a_name, int a_mode)
{
char		l_n [WCX$K_SZ], l_new [WCX$K_SZ * 2];
const char *	l_base;

	if ( a_mode != 0 )
		return	1;

	s_narrow(a_name, l_n, sizeof(l_n));
	l_base	= strrchr(l_n, WCX$SEP);
	l_base	= l_base ? (l_base + 1) : l_n;

	printf("ASK %s\n", l_base);

	if ( !s_voldir )
		return	0;

	snprintf(l_new, sizeof(l_new), "%s%c%s", s_voldir, WCX$SEP, l_base);
	s_wide(l_new, a_name, 260);

	return	1;
}

static	int	WCX$CALL	s_pdp	(WCX$WCHAR *a_name, int a_size)
{
	(void) a_name;
	s_progress	+= a_size;

	return	1;
}

static	void *	s_sym	(void *a_lib, const char *a_name)
{
#ifdef	_WIN32
	return	(void *) GetProcAddress((HMODULE) a_lib, a_name);
#else
	return	dlsym(a_lib, a_name);
#endif
}

int	main	(int argc, char **argv)
{
void *		l_lib, *l_arc;
int		l_ansi = 0, l_rc = 0, l_status, i = 1, l_names;
const char *	l_plug, *l_op, *l_spec, *l_out = NULL;
WCX$WCHAR	l_w [WCX$K_SZ], l_dest [WCX$K_SZ];
char		l_n [WCX$K_SZ], l_d [WCX$K_SZ * 2];
WCX$HDREXW	l_h;
WCX$HDREX	l_ha;

#ifdef	_WIN32
	/* Lines end in LF, as on Linux: the outputs are compared */
	_setmode(_fileno(stdout), _O_BINARY);
	_setmode(_fileno(stderr), _O_BINARY);

	/* The arguments in UTF-8, from the UTF-16 of the command line: a Cyrillic name gets through */
	{
	int		l_n;
	wchar_t **	l_wv = CommandLineToArgvW(GetCommandLineW(), &l_n);

	if ( l_wv && (l_n == argc) )
		for ( int k = 0; k < argc; k++ )
			{
			char *	l_a = malloc(WCX$K_SZ);

			if ( l_a && WideCharToMultiByte(CP_UTF8, 0, l_wv [k], -1, l_a, WCX$K_SZ, NULL, NULL) )
				argv [k] = l_a;
			}
	}
#endif

	if ( argc < 4 )
		{
		fprintf(stderr, "usage: wcx_harness <plugin> [-a] [-v <dir>] c|l|t|x <saveset> [<out> [<name>...]]\n");
		return	2;
		}

	l_plug	= argv [i++];

	for ( ; (i < argc) && (argv [i][0] == '-'); i++ )
		if ( !strcmp(argv [i], "-a") )
			l_ansi = 1;
		else if ( !strcmp(argv [i], "-v") && (i + 1 < argc) )
			s_voldir = argv [++i];

	if ( i + 2 > argc )
		return	2;

	l_op	= argv [i++];
	l_spec	= argv [i++];

	if ( i < argc )
		l_out = argv [i++];

	l_names	= i;

#ifdef	_WIN32
	s_wide(l_plug, l_w, WCX$K_SZ);
	l_lib	= (void *) LoadLibraryW(l_w);
#else
	l_lib	= dlopen(l_plug, RTLD_NOW | RTLD_LOCAL);
#endif

	if ( !l_lib )
		{
		fprintf(stderr, "wcx_harness: %s cannot be loaded\n", l_plug);
		return	2;
		}

	{
	OPENW	l_openw = (OPENW) s_sym(l_lib, "OpenArchiveW");
	OPENA	l_opena = (OPENA) s_sym(l_lib, "OpenArchive");
	READW	l_readw = (READW) s_sym(l_lib, "ReadHeaderExW");
	READA	l_reada = (READA) s_sym(l_lib, "ReadHeaderEx");
	PROCW	l_procw = (PROCW) s_sym(l_lib, "ProcessFileW");
	PROCA	l_proca = (PROCA) s_sym(l_lib, "ProcessFile");
	CLOSE	l_close = (CLOSE) s_sym(l_lib, "CloseArchive");
	CAPS	l_caps  = (CAPS) s_sym(l_lib, "GetPackerCaps");
	CANW	l_canw  = (CANW) s_sym(l_lib, "CanYouHandleThisFileW");
	SETCVPW	l_setcv = (SETCVPW) s_sym(l_lib, "SetChangeVolProcW");
	SETPDPW	l_setpd = (SETPDPW) s_sym(l_lib, "SetProcessDataProcW");

	if ( !l_openw || !l_opena || !l_readw || !l_reada || !l_procw || !l_proca || !l_close || !l_caps || !l_canw
		|| !l_setcv || !l_setpd || !s_sym(l_lib, "ReadHeader") || !s_sym(l_lib, "CanYouHandleThisFile")
		|| !s_sym(l_lib, "SetChangeVolProc") || !s_sym(l_lib, "SetProcessDataProc") )
		{
		fprintf(stderr, "wcx_harness: %s lacks a function of the interface\n", l_plug);
		return	2;
		}

	s_wide(l_spec, l_w, WCX$K_SZ);

	if ( *l_op == 'c' )
		{
		printf("CAPS %d CAN %d\n", l_caps(), l_canw(l_w));
		return	0;
		}

	/* TC gives the callbacks for every archive, the handle -1, before it opens one */
	l_setcv((void *) (intptr_t) -1, s_cvp);
	l_setpd((void *) (intptr_t) -1, s_pdp);

	if ( l_ansi )
		{
		WCX$OPEN	l_o = { (char *) l_spec, (*l_op == 'l') ? 0 : 1, 0, NULL, 0, 0, 0 };

		l_arc	= l_opena(&l_o);
		l_status = l_o.OpenResult;
		}
	else	{
		WCX$OPENW	l_o = { l_w, (*l_op == 'l') ? 0 : 1, 0, NULL, 0, 0, 0 };

		l_arc	= l_openw(&l_o);
		l_status = l_o.OpenResult;
		}

	if ( !l_arc )
		{
		printf("OPEN %d\n", l_status);
		return	2;
		}

	for ( ;; )
		{
		unsigned long long	l_size;
		int			l_crc, l_attr, l_want, l_opc;

		if ( l_ansi )
			{
			if ( (l_status = l_reada(l_arc, &l_ha)) )
				break;

			snprintf(l_n, sizeof(l_n), "%s", l_ha.FileName);
			l_size	= ((unsigned long long) l_ha.UnpSizeHigh << 32) | l_ha.UnpSize;
			l_crc	= l_ha.FileCRC;
			l_attr	= l_ha.FileAttr;
			}
		else	{
			if ( (l_status = l_readw(l_arc, &l_h)) )
				break;

			s_narrow(l_h.FileName, l_n, sizeof(l_n));
			l_size	= ((unsigned long long) l_h.UnpSizeHigh << 32) | l_h.UnpSize;
			l_crc	= l_h.FileCRC;
			l_attr	= l_h.FileAttr;
			}

		/* The name as TC would make the target of it, and as the listing shows it */
		snprintf(l_d, sizeof(l_d), "%s%c%s", l_out ? l_out : "", WCX$SEP, l_n);

		for ( char *p = l_n; *p; p++ )
			if ( *p == '\\' )
				*p = '/';

		l_want	= (l_names >= argc);

		for ( int k = l_names; k < argc; k++ )
			if ( !strcmp(argv [k], l_n) )
				l_want = 1;

		if ( *l_op == 'l' )
			{
			printf("%02x %llu %08x %s\n", l_attr, l_size, (unsigned) l_crc, l_n);
			l_opc	= 0;
			}
		else	l_opc = !l_want ? 0 : (*l_op == 't') ? 1 : 2;

		if ( l_ansi )
			l_status = l_proca(l_arc, l_opc, NULL, (l_opc == 2) ? l_d : NULL);
		else	{
			s_wide(l_d, l_dest, WCX$K_SZ);
			l_status = l_procw(l_arc, l_opc, NULL, (l_opc == 2) ? l_dest : NULL);
			}

		if ( l_status )
			{
			printf("ERR %d %s\n", l_status, l_n);
			l_rc	= 1;
			}
		}

	if ( l_status != 10 )
		{
		printf("END %d\n", l_status);
		l_rc	= 1;
		}

	if ( l_close(l_arc) )
		l_rc	= 1;

	fprintf(stderr, "PROGRESS %lld\n", s_progress);
	}

	return	l_rc;
}
