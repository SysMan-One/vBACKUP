#define	__MODULE__	"VBKWCX"
#define	__IDENT__	"X01-06"
#define	__REV__		"1.6.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkwcx.c
**
**  ABSTRACT:	The packer plugin (WCX) of Total Commander and Double
**		Commander for VBACKUP savesets: open a .bck like a folder,
**		view, copy out, test.  Read only - a saveset is not changed.
**
**  DESCRIPTION: What it does
**
**		    - the listing comes from the catalog of the saveset (the
**		      TRAILER points to it), so a saveset of hundreds of
**		      gigabytes opens at once; without a catalog (a save cut
**		      short), or with a hole in it, the saveset is read
**		      through, as VBKX does;
**		    - a file is copied out through its place in the catalog:
**		      nothing else is read; its FILENO is checked, so a lost
**		      block never gives the data of another file;
**		    - DATA and DATAZ (/DATA_FORMAT=COMPRESSED) records, holes,
**		      several volumes (x.bck, x.bck.002, ... beside each other;
**		      one missing between them, or - no TRAILER - the one after
**		      the last, is asked for through the ChangeVol callback),
**		      damaged blocks repaired as far as the XOR blocks allow;
**		    - the CRC of every file is checked: a file that does not
**		      come out whole is reported (E_BAD_DATA), never silently;
**		    - names in UTF-8 become UTF-16; on Windows '/' becomes '\'
**		      (on Linux it stays: Double Commander works with '/'
**		      there - assumed, DC was not at hand to try); a name that
**		      would lead out of the target - "..", a leading "/", a
**		      backslash, on Windows also "c:", "con", "x?" - is not
**		      listed at all;
**		    - a further name of a file (hard link) chosen alone gets
**		      the data of the first name;
**		    - nothing is written outside the target: on Linux the way
**		      to a file is opened a directory at a time with
**		      O_NOFOLLOW, symbolic links are made last (at the close);
**		    - attributes: folder, read-only (no write bit for the
**		      owner), hidden (a name beginning with a dot); the size
**		      in 64 bits (the High fields); the time of modification.
**
**  LIMITATIONS
**
**		    - an encrypted saveset (format.md 6.10) is opened with the
**		      first line of the file VBACKUP_KEY_FILE names (on Linux
**		      readable by its owner only): the API has no way to ask
**		      for a passphrase.  Without it - E_EOPEN, with a wrong one
**		      - E_BAD_ARCHIVE;
**		    - no add, delete, change: a saveset is written by vbackup
**		      only;
**		    - owners, ACLs, extended attributes, chattr flags are not
**		      put back; device files, FIFOs, sockets are listed, not
**		      made; symbolic links are made on Linux (Double
**		      Commander), skipped on Windows; a link that cannot be
**		      made (at the close) is not reported - the API has no
**		      way left by then;
**		    - the times of the listing are DOS times (local, two
**		      seconds): the WCX header has no FILETIME field; a file
**		      extracted gets its times in full; directories keep the
**		      time of their making;
**		    - the old ("ANSI") functions get names in the code page of
**		      Windows: a name it cannot hold comes out with '?' - TC
**		      itself uses the W ones;
**		    - records lost without a catalog: the listing ends with
**		      E_BAD_DATA, not E_END_ARCHIVE (TC shows an error after
**		      the list) - a file whose FILE record was lost is not
**		      listed, but it is not lost silently;
**		    - a save cut short has no TRAILER, like a saveset whose
**		      last volume is elsewhere: one question about a volume
**		      more, to be refused;
**		    - one piece of state is shared: the callbacks given for
**		      "every archive" (handle -1), written by TC before it
**		      opens one; everything else is in the open archive.
**
**  INSTALLATION
**
**		Total Commander: open the plugin archive (vbackup_wcx.zip,
**		with pluginst.inf) in TC and confirm - or by hand:
**		Configuration -> Options -> Packer -> Configure packer
**		extension WCXs -> extension "bck" -> New type -> vbackup.wcx64
**		(64-bit TC) or vbackup.wcx (32-bit TC).
**
**		Double Commander (Linux): Options -> Plugins -> Packer
**		plugins (WCX) -> Add -> vbackup.wcx, extension "bck".
**
**  BUILD
**
**		Linux .so (Double Commander), from the root of the sources:
**		    gcc -O2 -shared -fPIC -DVBK_NOSTARLET -D_GNU_SOURCE -Ilib
**			-o vbackup.wcx plugins/wcx/vbkwcx.c lib/vbkfmt.c
**			lib/vbkrd.c lib/vbklz4.c lib/vbkcrp.c
**		Windows, 64 and 32 bits:
**		    x86_64-w64-mingw32-gcc -O2 -shared -Ilib -o vbackup.wcx64
**			plugins/wcx/vbkwcx.c lib/vbkfmt.c lib/vbkrd.c
**			lib/vbklz4.c lib/vbkcrp.c -static -Wl,--kill-at
**		    i686-w64-mingw32-gcc ... -o vbackup.wcx ... (the same)
**		or make -f plugins/wcx/Makefile linux|win64|win32|all.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  4-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-06		 5-OCT-2026	RRL
**		Encrypted savesets: the passphrase from VBACKUP_KEY_FILE.
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
#include	<errno.h>
#include	<time.h>

#ifdef	_WIN32
#include	<windows.h>
#else
#include	<fcntl.h>
#include	<unistd.h>
#include	<sys/stat.h>
#endif

#include	"vbkrd.h"
#include	"vbkos.h"
#include	"vbklz4.h"

/*
**  The calling of the plugin API: __stdcall exported on Windows (built
**  with --kill-at: the names undecorated, as TC looks them up), plain C
**  on Linux.  WCHAR is 16 bits on both: the WideChar of Double Commander.
*/
#ifdef	_WIN32
#define	WCX$EXPORT	__declspec(dllexport)
#define	WCX$CALL	__stdcall
typedef	wchar_t		WCX$WCHAR;
#define	WCX$SEP		'\\'
#else
#define	WCX$EXPORT	__attribute__((visibility("default")))
#define	WCX$CALL
typedef	uint16_t	WCX$WCHAR;
#define	WCX$SEP		'/'
#endif

#define	WCX$K_NAMEW	1024			/* Names of the Ex structures			*/
#define	WCX$K_NAME	260			/* ... of the old one				*/
#define	WCX$K_SZ_PATH	4096

enum	{					/* The values of the WCX interface		*/
	WCX$K_E_END_ARCHIVE	= 10,
	WCX$K_E_NO_MEMORY	= 11,
	WCX$K_E_BAD_DATA	= 12,
	WCX$K_E_BAD_ARCHIVE	= 13,
	WCX$K_E_UNKNOWN_FORMAT	= 14,
	WCX$K_E_EOPEN		= 15,
	WCX$K_E_ECREATE		= 16,
	WCX$K_E_ECLOSE		= 17,
	WCX$K_E_EREAD		= 18,
	WCX$K_E_EWRITE		= 19,
	WCX$K_E_EABORTED	= 21,
	WCX$K_E_NOT_SUPPORTED	= 24,

	WCX$K_PK_OM_LIST	= 0,
	WCX$K_PK_OM_EXTRACT	= 1,

	WCX$K_PK_SKIP		= 0,
	WCX$K_PK_TEST		= 1,
	WCX$K_PK_EXTRACT	= 2,

	WCX$K_PK_VOL_ASK	= 0,
	WCX$K_PK_VOL_NOTIFY	= 1,

	WCX$M_PK_CAPS_MULTIPLE	= 4,
	WCX$M_PK_CAPS_BY_CONTENT = 64,

	WCX$M_ATTR_READONLY	= 0x01,
	WCX$M_ATTR_HIDDEN	= 0x02,
	WCX$M_ATTR_DIRECTORY	= 0x10,
	WCX$M_ATTR_ARCHIVE	= 0x20
	};

/*
**  The structures of the interface, by its published description
*/
typedef struct wcx_open_t			/* tOpenArchiveData				*/
{
	char *		ArcName;
	int		OpenMode;
	int		OpenResult;
	char *		CmtBuf;
	int		CmtBufSize;
	int		CmtSize;
	int		CmtState;
} WCX$OPEN;

typedef struct wcx_openw_t			/* tOpenArchiveDataW				*/
{
	WCX$WCHAR *	ArcName;
	int		OpenMode;
	int		OpenResult;
	WCX$WCHAR *	CmtBuf;
	int		CmtBufSize;
	int		CmtSize;
	int		CmtState;
} WCX$OPENW;

typedef struct wcx_hdr_t			/* tHeaderData					*/
{
	char		ArcName [WCX$K_NAME];
	char		FileName [WCX$K_NAME];
	int		Flags;
	int		PackSize;
	int		UnpSize;
	int		HostOS;
	int		FileCRC;
	int		FileTime;
	int		UnpVer;
	int		Method;
	int		FileAttr;
	char *		CmtBuf;
	int		CmtBufSize;
	int		CmtSize;
	int		CmtState;
} WCX$HDR;

typedef struct wcx_hdrex_t			/* tHeaderDataEx				*/
{
	char		ArcName [WCX$K_NAMEW];
	char		FileName [WCX$K_NAMEW];
	int		Flags;
	unsigned int	PackSize;
	unsigned int	PackSizeHigh;
	unsigned int	UnpSize;
	unsigned int	UnpSizeHigh;
	int		HostOS;
	int		FileCRC;
	int		FileTime;
	int		UnpVer;
	int		Method;
	int		FileAttr;
	char *		CmtBuf;
	int		CmtBufSize;
	int		CmtSize;
	int		CmtState;
	char		Reserved [1024];
} WCX$HDREX;

typedef struct wcx_hdrexw_t			/* tHeaderDataExW				*/
{
	WCX$WCHAR	ArcName [WCX$K_NAMEW];
	WCX$WCHAR	FileName [WCX$K_NAMEW];
	int		Flags;
	unsigned int	PackSize;
	unsigned int	PackSizeHigh;
	unsigned int	UnpSize;
	unsigned int	UnpSizeHigh;
	int		HostOS;
	int		FileCRC;
	int		FileTime;
	int		UnpVer;
	int		Method;
	int		FileAttr;
	char *		CmtBuf;
	int		CmtBufSize;
	int		CmtSize;
	int		CmtState;
	char		Reserved [1024];
} WCX$HDREXW;

typedef	int	(WCX$CALL *WCX$CHANGEVOL)	(char *a_name, int a_mode);
typedef	int	(WCX$CALL *WCX$CHANGEVOLW)	(WCX$WCHAR *a_name, int a_mode);
typedef	int	(WCX$CALL *WCX$PROCDATA)	(char *a_name, int a_size);
typedef	int	(WCX$CALL *WCX$PROCDATAW)	(WCX$WCHAR *a_name, int a_size);

/*
**  One file of the saveset, as the catalog (or a FILE record) says
*/
typedef struct vbkw_ent_t
{
	uint32_t	fileno;
	char *		path;			/* Own copy, terminated				*/
	char *		link;
	uint8_t		ftype;
	uint32_t	mode;
	uint64_t	size;
	VBK$TIME	mtime, atime;
	uint32_t	crc;
	int		hascrc;
	uint8_t		status;
	VBK$LOC		loc;
	int		data;			/* Index of the entry with the data (a further name) */
} VBKW$ENT;

/*
**  An archive open: everything of it here, nothing global but the
**  callbacks TC may give before any archive is open
*/
typedef struct vbkw_arc_t
{
	VBK$RCTX	rctx;
	VBKW$ENT *	ents;			/* The catalog					*/
	size_t		nents, next;
	long		cur;			/* The entry of the last header, -1 - none	*/
	int		catalog;		/* 0 - read through the stream			*/

	VBKW$ENT	sent;			/* Stream: the file of the last header		*/
	int		shave;
	uint8_t *	look;			/* Stream: a FILE record read ahead		*/
	uint32_t	looklen, looksz;
	int		haslook;
	int		lost;			/* Stream: records lost - a file may be unlisted */
	VBK$LOC		lookloc;
	VBKW$ENT *	seen;			/* Stream: the files gone by, for a further name */
	size_t		nseen, szseen;

	struct vbkw_lnk_t			/* Symbolic links, made at the close		*/
	{
		char *	path;			/* Where - the destination			*/
		char *	name;			/* The stored name				*/
		char *	target;
	} *		lnks;
	size_t		nlnks, szlnks;

	uint8_t *	zbuf;			/* A DATAZ record decompressed			*/

	WCX$CHANGEVOL	cvp;
	WCX$CHANGEVOLW	cvpw;
	WCX$PROCDATA	pdp;
	WCX$PROCDATAW	pdpw;
} VBKW$ARC;

static	WCX$CHANGEVOL	s_cvp;			/* Given for "any archive" (handle -1)		*/
static	WCX$CHANGEVOLW	s_cvpw;
static	WCX$PROCDATA	s_pdp;
static	WCX$PROCDATAW	s_pdpw;


/*
**  UTF-8 into UTF-16; a broken sequence becomes U+FFFD.  Always ended
**  by a zero within <a_outn>.
*/
static	void	s_vbkw$towide	(
	const	char *		a_in,
		size_t		a_inlen,
		WCX$WCHAR *	a_out,
		size_t		a_outn
			)
{
const	uint8_t *	l_p = (const uint8_t *) a_in, *l_e = l_p + a_inlen;
size_t		l_o = 0;

	while ( (l_p < l_e) && ((l_o + 2) < a_outn) )
		{
		uint32_t	l_c = *l_p++;
		int		l_n = 0;

		if ( l_c >= 0xF0 && l_c < 0xF8 )	{ l_c &= 0x07; l_n = 3; }
		else if ( l_c >= 0xE0 && l_c < 0xF0 )	{ l_c &= 0x0F; l_n = 2; }
		else if ( l_c >= 0xC0 && l_c < 0xE0 )	{ l_c &= 0x1F; l_n = 1; }
		else if ( l_c >= 0x80 )			l_c = 0xFFFD;

		for ( ; l_n && (l_p < l_e) && ((*l_p & 0xC0) == 0x80); l_n-- )
			l_c = (l_c << 6) | (*l_p++ & 0x3F);

		if ( l_n || (l_c > 0x10FFFF) )
			l_c = 0xFFFD;

		if ( l_c >= 0x10000 )
			{
			l_c	-= 0x10000;
			a_out [l_o++] = (WCX$WCHAR) (0xD800 | (l_c >> 10));
			a_out [l_o++] = (WCX$WCHAR) (0xDC00 | (l_c & 0x3FF));
			}
		else	a_out [l_o++] = (WCX$WCHAR) l_c;
		}

	a_out [l_o] = 0;
}


/*
**  UTF-16 into UTF-8, ended by a zero within <a_outn>
*/
static	void	s_vbkw$tonarrow	(
	const	WCX$WCHAR *	a_in,
		char *		a_out,
		size_t		a_outn
			)
{
size_t	l_o = 0;

	for ( ; a_in && *a_in && ((l_o + 5) < a_outn); a_in++ )
		{
		uint32_t	l_c = *a_in;

		if ( (l_c >= 0xD800) && (l_c < 0xDC00) && (a_in [1] >= 0xDC00) && (a_in [1] < 0xE000) )
			l_c = 0x10000 + ((l_c - 0xD800) << 10) + (*++a_in - 0xDC00);

		if ( l_c < 0x80 )
			a_out [l_o++] = (char) l_c;
		else if ( l_c < 0x800 )
			{
			a_out [l_o++] = (char) (0xC0 | (l_c >> 6));
			a_out [l_o++] = (char) (0x80 | (l_c & 0x3F));
			}
		else if ( l_c < 0x10000 )
			{
			a_out [l_o++] = (char) (0xE0 | (l_c >> 12));
			a_out [l_o++] = (char) (0x80 | ((l_c >> 6) & 0x3F));
			a_out [l_o++] = (char) (0x80 | (l_c & 0x3F));
			}
		else	{
			a_out [l_o++] = (char) (0xF0 | (l_c >> 18));
			a_out [l_o++] = (char) (0x80 | ((l_c >> 12) & 0x3F));
			a_out [l_o++] = (char) (0x80 | ((l_c >> 6) & 0x3F));
			a_out [l_o++] = (char) (0x80 | (l_c & 0x3F));
			}
		}

	a_out [l_o] = '\0';
}


/*
**  The "ANSI" of the old functions: the code page of Windows, UTF-8 on
**  Linux
*/
static	void	s_vbkw$ansitowide	(
	const	char *		a_in,
		WCX$WCHAR *	a_out,
		size_t		a_outn
			)
{
#ifdef	_WIN32
	if ( !MultiByteToWideChar(CP_ACP, 0, a_in, -1, a_out, (int) a_outn) )
		a_out [0] = 0;
#else
	s_vbkw$towide(a_in, strlen(a_in), a_out, a_outn);
#endif
}

static	void	s_vbkw$widetoansi	(
	const	WCX$WCHAR *	a_in,
		char *		a_out,
		size_t		a_outn
			)
{
#ifdef	_WIN32
	if ( !WideCharToMultiByte(CP_ACP, 0, a_in, -1, a_out, (int) a_outn, NULL, NULL) )
		a_out [0] = '\0';
#else
	s_vbkw$tonarrow(a_in, a_out, a_outn);
#endif
}


/*
**  A stored name that is safe to show and to make: relative, no "." or
**  ".." component, no empty one, no backslash (Windows would take it for
**  a separator); on Windows also none it cannot hold - "c:" would be a
**  drive or a stream, "con" a device
*/
static	int	s_vbkw$nameok	(
	const	char *		a_name
			)
{
size_t	l_len = strlen(a_name);

	if ( !l_len || (a_name [0] == '/') || strchr(a_name, '\\') )
		return	0;

	for ( size_t i = 0, j; i < l_len; i = j + 1 )
		{
		for ( j = i; (j < l_len) && (a_name [j] != '/'); j++ )
			;

		if ( (j == i) || (((j - i) == 1) && (a_name [i] == '.')) || (((j - i) == 2) && (a_name [i] == '.') && (a_name [i + 1] == '.')) )
			return	0;

#ifdef	_WIN32
		{
		char	l_b [5] = {0};
		size_t	k;

		for ( k = i; k < j; k++ )
			if ( ((unsigned char) a_name [k] < 32) || strchr("<>:\"|?*", a_name [k]) )
				return	0;

		if ( (a_name [j - 1] == '.') || (a_name [j - 1] == ' ') )
			return	0;

		/* CON, PRN, AUX, NUL, COM1-9, LPT1-9 - with any extension */
		for ( k = 0; (k < 4) && ((i + k) < j) && (a_name [i + k] != '.'); k++ )
			l_b [k] = (char) (a_name [i + k] & ~0x20);

		if ( ((i + k) == j) || (a_name [i + k] == '.') )
			if ( !strcmp(l_b, "CON") || !strcmp(l_b, "PRN") || !strcmp(l_b, "AUX") || !strcmp(l_b, "NUL")
				|| ((k == 4) && (!strncmp(l_b, "COM", 3) || !strncmp(l_b, "LPT", 3)) && (a_name [i + 3] >= '1') && (a_name [i + 3] <= '9')) )
				return	0;
		}
#endif
		}

	return	1;
}


static	void	s_vbkw$entfree	(
		VBKW$ENT *	a_e
			)
{
	free(a_e->path);
	free(a_e->link);
	memset(a_e, 0, sizeof(*a_e));
}


/*
**  The per-file tags of a FILE record or a catalog entry into an entry
**  (format.md, 6.1); the strings are copied
*/
static	int	s_vbkw$parse	(
	const	uint8_t *	a_body,
		uint32_t	a_len,
		VBKW$ENT *	a_e
			)
{
uint32_t	l_pos = 0, l_vlen;
uint16_t	l_tag;
const uint8_t *	l_val, *l_path = NULL, *l_link = NULL;
uint32_t	l_plen = 0, l_llen = 0;
int		l_status;

	memset(a_e, 0, sizeof(*a_e));
	a_e->data = -1;

	while ( 1 & (l_status = vbk$tlv_next(a_body, a_len, &l_pos, &l_tag, &l_vlen, &l_val)) )
		switch ( l_tag )
			{
			case	VBK$K_TAG_FILENO:	a_e->fileno = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_PATH:		l_path = l_val; l_plen = l_vlen;			break;
			case	VBK$K_TAG_FTYPE:	a_e->ftype = (uint8_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_MODE:		a_e->mode = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_SIZE:		a_e->size = vbk$tlv_getu(l_vlen, l_val);		break;
			case	VBK$K_TAG_MTIME:	vbk$tlv_gettime(l_vlen, l_val, &a_e->mtime);		break;
			case	VBK$K_TAG_ATIME:	vbk$tlv_gettime(l_vlen, l_val, &a_e->atime);		break;
			case	VBK$K_TAG_LINK:		l_link = l_val; l_llen = l_vlen;			break;
			case	VBK$K_TAG_CRC:		a_e->crc = (uint32_t) vbk$tlv_getu(l_vlen, l_val);
							a_e->hascrc = 1;					break;
			case	VBK$K_TAG_STATUS:	a_e->status = (uint8_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_LOCVOL:	a_e->loc.vol = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_LOCBLK:	a_e->loc.blk = vbk$tlv_getu(l_vlen, l_val);		break;
			case	VBK$K_TAG_LOCOFF:	a_e->loc.off = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			}

	if ( (l_status != STS$K_WARN) || !l_path || !l_plen || (l_plen >= WCX$K_SZ_PATH) || memchr(l_path, 0, l_plen) || !a_e->ftype )
		return	STS$K_ERROR;

	if ( !(a_e->path = malloc(l_plen + 1)) || (l_link && !(a_e->link = malloc(l_llen + 1))) )
		{
		s_vbkw$entfree(a_e);

		return	STS$K_FATAL;
		}

	memcpy(a_e->path, l_path, l_plen);
	a_e->path [l_plen] = '\0';

	if ( l_link )
		{
		memcpy(a_e->link, l_link, l_llen);
		a_e->link [l_llen] = '\0';
		}

	if ( !a_e->atime.sec )
		a_e->atime = a_e->mtime;

	return	STS$K_SUCCESS;
}


/*
**  The catalog into the archive: every entry saved here (not PRESENT),
**  with a safe name; a further name of a file points to the entry that
**  holds the data.  0 - no catalog, or a damaged one: read the stream.
*/
static	int	s_vbkw$catalog	(
		VBKW$ARC *	a_arc
			)
{
VBK$RCTX *	l_r = &a_arc->rctx;
VBK$LOC		l_loc = {0};
const uint8_t *	l_val, *l_body;
uint32_t	l_pos = 0, l_vlen, l_len;
uint16_t	l_tag, l_type;
size_t		l_sz = 0;
int		l_status, l_hole;

	if ( !l_r->trailer )
		return	0;

	while ( 1 & vbk$tlv_next(l_r->trailer, l_r->trllen, &l_pos, &l_tag, &l_vlen, &l_val) )
		switch ( l_tag )
			{
			case	VBK$K_TAG_CATVOL:	l_loc.vol = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_CATBLK:	l_loc.blk = vbk$tlv_getu(l_vlen, l_val);		break;
			case	VBK$K_TAG_CATOFF:	l_loc.off = (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			}

	if ( STS$K_SUCCESS != vbk$rd_seek(l_r, &l_loc) )
		return	0;

	l_hole	= 0;

	while ( (1 & (l_status = vbk$rd_next(l_r, &l_type, &l_body, &l_len, NULL))) && (l_type != VBK$K_RT_END) )
		{
		l_hole	|= l_r->resync;

		if ( l_type != VBK$K_RT_CATALOG )
			continue;

		for ( uint32_t l_off = 0; (l_off + 4) <= l_len; )
			{
			uint32_t	l_elen = vbk$get32(l_body + l_off);
			VBKW$ENT	l_e;

			if ( (l_off + 4 + l_elen) > l_len )
				break;

			if ( (1 & s_vbkw$parse(l_body + l_off + 4, l_elen, &l_e)) )
				{
				if ( (l_e.status == VBK$K_FS_PRESENT) || !s_vbkw$nameok(l_e.path) )
					s_vbkw$entfree(&l_e);
				else	{
					if ( a_arc->nents == l_sz )
						{
						size_t		l_new = l_sz ? (l_sz * 2) : 1024;
						VBKW$ENT *	l_p = realloc(a_arc->ents, l_new * sizeof(VBKW$ENT));

						if ( !l_p )
							{
							s_vbkw$entfree(&l_e);
							break;
							}

						a_arc->ents = l_p;
						l_sz	    = l_new;
						}

					a_arc->ents [a_arc->nents++] = l_e;
					}
				}

			l_off	+= 4 + l_elen;
			}
		}

	/* A catalog with a hole lists less than there is: the stream lists all that can be had */
	if ( l_hole || l_r->resync || !(1 & l_status) || !a_arc->nents )
		{
		for ( size_t i = 0; i < a_arc->nents; i++ )
			s_vbkw$entfree(&a_arc->ents [i]);

		free(a_arc->ents);
		a_arc->ents	= NULL;
		a_arc->nents	= 0;

		return	0;
		}

	/* A further name of a file: the entry of its first name holds the data */
	for ( size_t i = 0; i < a_arc->nents; i++ )
		{
		VBKW$ENT *	l_e = &a_arc->ents [i];

		if ( (l_e->ftype != VBK$K_FT_HARDLINK) || !l_e->link )
			continue;

		for ( size_t j = 0; j < a_arc->nents; j++ )
			if ( (a_arc->ents [j].ftype == VBK$K_FT_REG) && !strcmp(a_arc->ents [j].path, l_e->link) )
				{
				l_e->data = (int) j;
				l_e->size = a_arc->ents [j].size;
				break;
				}
		}

	return	1;
}


/*
**  The volumes not found beside volume 1: asked for, when TC gave the
**  callback for it: a gap between the volumes found, and - no TRAILER -
**  the one after the last (on another disk, say), as long as volumes
**  come.  A save cut short has no TRAILER either: then one question
**  more, which the user refuses.  A volume refused is reported as VBKX
**  does: a file with records there fails with E_BAD_DATA; without the
**  TRAILER the stream is read, and the listing ends with E_BAD_DATA.
*/
static	void	s_vbkw$volumes	(
		VBKW$ARC *	a_arc
			)
{
VBK$RCTX *	l_r = &a_arc->rctx;
char		l_spec [VBK$K_SZ_SPEC];
WCX$WCHAR	l_w [WCX$K_NAMEW];
char		l_a [WCX$K_NAMEW];
uint32_t	l_last = l_r->nvols + (l_r->trailer ? 0 : 1);

	if ( !a_arc->cvpw && !a_arc->cvp )
		return;

	for ( uint32_t v = 2; (v <= l_last) && (v <= VBK$K_MAXVOL); v++ )
		{
		if ( (v <= l_r->nvols) && (l_r->vols [v - 1].fd >= 0) )
			continue;

		if ( !(1 & vbk$volspec(l_r->spec, v, l_spec, sizeof(l_spec))) )
			break;

		/* The callback may change the name: the user points at the volume elsewhere */
		if ( a_arc->cvpw )
			{
			s_vbkw$towide(l_spec, strlen(l_spec), l_w, WCX$K_NAMEW);

			if ( !a_arc->cvpw(l_w, WCX$K_PK_VOL_ASK) )
				break;

			s_vbkw$tonarrow(l_w, l_spec, sizeof(l_spec));
			}
		else	{
			s_vbkw$towide(l_spec, strlen(l_spec), l_w, WCX$K_NAMEW);
			s_vbkw$widetoansi(l_w, l_a, sizeof(l_a));

			if ( !a_arc->cvp(l_a, WCX$K_PK_VOL_ASK) )
				break;

			s_vbkw$ansitowide(l_a, l_w, WCX$K_NAMEW);
			s_vbkw$tonarrow(l_w, l_spec, sizeof(l_spec));
			}

		/* Taken in, and still no TRAILER: the next one is wanted too */
		if ( (1 & vbk$rd_addvol(l_r, v, l_spec)) && (v == l_last) && !l_r->trailer )
			l_last++;
		}
}



/*
**  The passphrase of an encrypted saveset: the first line of the file
**  VBACKUP_KEY_FILE names (on Linux one only its owner may read).
**  Returns STS$K_SUCCESS / STS$K_WARN (opened, the TRAILER fails) from
**  VBK$RD_SETKEY, STS$K_ERROR - the passphrase is wrong, STS$K_FATAL -
**  there is none to be had.
*/
static	int	s_vbkw$unlock	(
		VBK$RCTX *	a_rctx
			)
{
char		l_pass [VBK$K_PASSMAX + 2], l_kf [VBK$K_SZ_SPEC];
size_t		l_n = 0;
FILE *		l_fp;
int		l_c, l_status;
#ifdef	_WIN32
const WCHAR *	l_wkf = _wgetenv(L"VBACKUP_KEY_FILE");

	if ( !l_wkf || !*l_wkf || !(l_fp = _wfopen(l_wkf, L"rb")) )
		return	STS$K_FATAL;

	(void) l_kf;
#else
const char *	l_env = getenv("VBACKUP_KEY_FILE");
struct stat	l_st;

	if ( !l_env || !*l_env )
		return	STS$K_FATAL;

	snprintf(l_kf, sizeof(l_kf), "%s", l_env);

	if ( !(l_fp = fopen(l_kf, "rb")) )
		return	STS$K_FATAL;

	if ( fstat(fileno(l_fp), &l_st) || !S_ISREG(l_st.st_mode) || (l_st.st_mode & (S_IRWXG | S_IRWXO)) )
		{
		fclose(l_fp);

		return	STS$K_FATAL;
		}
#endif
	while ( ((l_c = fgetc(l_fp)) != EOF) && (l_c != '\n') )
		if ( l_n < sizeof(l_pass) )
			l_pass [l_n++] = (char) l_c;

	fclose(l_fp);

	if ( l_n && (l_n < sizeof(l_pass)) && (l_pass [l_n - 1] == '\r') )
		l_n--;

	l_status = (l_n && (l_n <= VBK$K_PASSMAX)) ? vbk$rd_setkey(a_rctx, l_pass, l_n) : STS$K_FATAL;
	vbk$crp_wipe(l_pass, sizeof(l_pass));

	return	l_status;
}


/*
**  Open an archive: the name in UTF-16
*/
static	VBKW$ARC *	s_vbkw$open	(
	const	WCX$WCHAR *	a_name,
		int *		a_result
			)
{
VBKW$ARC *	l_arc;
char		l_spec [VBK$K_SZ_SPEC];
int		l_status;

	s_vbkw$tonarrow(a_name, l_spec, sizeof(l_spec));

	if ( !(l_arc = calloc(1, sizeof(VBKW$ARC))) || !(l_arc->zbuf = malloc(VBK$K_MAXDATA)) )
		{
		free(l_arc);
		*a_result = WCX$K_E_NO_MEMORY;

		return	NULL;
		}

	if ( !(1 & (l_status = vbk$rd_open(&l_arc->rctx, l_spec, NULL, NULL))) )
		{
		*a_result = (l_status == STS$K_WARN) ? WCX$K_E_UNKNOWN_FORMAT : (l_status == STS$K_FATAL) ? WCX$K_E_NO_MEMORY : WCX$K_E_EOPEN;

		if ( l_status != STS$K_ERROR )
			vbk$rd_close(&l_arc->rctx);

		free(l_arc->zbuf);
		free(l_arc);

		return	NULL;
		}

	/* Encrypted (format.md 6.10): the passphrase from the key file of VBACKUP_KEY_FILE - the API has no way to ask for one */
	if ( l_arc->rctx.crypt && (STS$K_SUCCESS != (l_status = s_vbkw$unlock(&l_arc->rctx))) && (l_status != STS$K_WARN) )
		{
		*a_result = (l_status == STS$K_ERROR) ? WCX$K_E_BAD_ARCHIVE : WCX$K_E_EOPEN;
		vbk$rd_close(&l_arc->rctx);
		free(l_arc->zbuf);
		free(l_arc);

		return	NULL;
		}

	l_arc->cvp	= s_cvp;
	l_arc->cvpw	= s_cvpw;
	l_arc->pdp	= s_pdp;
	l_arc->pdpw	= s_pdpw;
	l_arc->cur	= -1;

	s_vbkw$volumes(l_arc);

	l_arc->catalog	= s_vbkw$catalog(l_arc);

	vbk$rd_rewind(&l_arc->rctx);

	*a_result = 0;

	return	l_arc;
}


/*
**  The next file of the stream - no catalog - its FILE record read
*/
static	int	s_vbkw$streamnext	(
		VBKW$ARC *	a_arc
			)
{
const uint8_t *	l_body;
uint32_t	l_len;
uint16_t	l_type;
VBK$LOC		l_loc = {0};

	s_vbkw$entfree(&a_arc->sent);
	a_arc->shave	= 0;

	for ( ;; )
		{
		if ( a_arc->haslook )
			{
			a_arc->haslook = 0;
			l_body	= a_arc->look;
			l_len	= a_arc->looklen;
			l_type	= VBK$K_RT_FILE;
			l_loc	= a_arc->lookloc;
			}
		else if ( !(1 & vbk$rd_next(&a_arc->rctx, &l_type, &l_body, &l_len, &l_loc)) )
			return	WCX$K_E_BAD_DATA;		/* Cut short: no END */

		a_arc->lost	|= a_arc->rctx.resync;

		/*
		**  The end.  Records lost on the way may have held the FILE record
		**  of a file that is not listed: said by the end, not kept quiet
		*/
		if ( (l_type == VBK$K_RT_CATALOG) || (l_type == VBK$K_RT_END) )
			return	a_arc->lost ? WCX$K_E_BAD_DATA : WCX$K_E_END_ARCHIVE;

		if ( l_type != VBK$K_RT_FILE )
			continue;

		if ( !(1 & s_vbkw$parse(l_body, l_len, &a_arc->sent)) )
			continue;

		if ( !s_vbkw$nameok(a_arc->sent.path) )
			{
			s_vbkw$entfree(&a_arc->sent);
			continue;
			}

		a_arc->shave	= 1;
		a_arc->sent.loc	= l_loc;

		/* A file to come back to: a further name of it may follow */
		if ( a_arc->sent.ftype == VBK$K_FT_REG )
			{
			VBKW$ENT *	l_s;

			if ( a_arc->nseen == a_arc->szseen )
				{
				size_t	l_new = a_arc->szseen ? (a_arc->szseen * 2) : 256;

				if ( !(l_s = realloc(a_arc->seen, l_new * sizeof(VBKW$ENT))) )
					return	0;

				a_arc->seen	= l_s;
				a_arc->szseen	= l_new;
				}

			l_s	= &a_arc->seen [a_arc->nseen];
			*l_s	= a_arc->sent;
			l_s->link = NULL;

			if ( (l_s->path = strdup(a_arc->sent.path)) )
				a_arc->nseen++;
			}

		return	0;
		}
}


/*
**  The entry of the last header
*/
static	VBKW$ENT *	s_vbkw$current	(
		VBKW$ARC *	a_arc
			)
{
	if ( a_arc->catalog )
		return	((a_arc->cur >= 0) && ((size_t) a_arc->cur < a_arc->nents)) ? &a_arc->ents [a_arc->cur] : NULL;

	return	a_arc->shave ? &a_arc->sent : NULL;
}


/*
**  DOS date and time of an entry, local, as the old headers want them
*/
static	int	s_vbkw$dostime	(
	const	VBK$TIME *	a_t
			)
{
struct tm	l_tm;
time_t		l_s = (time_t) a_t->sec;

#ifdef	_WIN32
	if ( localtime_s(&l_tm, &l_s) )
		return	0;
#else
	if ( !localtime_r(&l_s, &l_tm) )
		return	0;
#endif

	if ( l_tm.tm_year < 80 )
		return	(1 << 21) | (1 << 16);

	return	((l_tm.tm_year - 80) << 25) | ((l_tm.tm_mon + 1) << 21) | (l_tm.tm_mday << 16) | (l_tm.tm_hour << 11) | (l_tm.tm_min << 5) | (l_tm.tm_sec / 2);
}


static	int	s_vbkw$attr	(
	const	VBKW$ENT *	a_e
			)
{
const char *	l_base = strrchr(a_e->path, '/');
int		l_attr = 0;

	l_base	= l_base ? (l_base + 1) : a_e->path;

	if ( a_e->ftype == VBK$K_FT_DIR )
		l_attr	|= WCX$M_ATTR_DIRECTORY;
	else	l_attr	|= WCX$M_ATTR_ARCHIVE;

	if ( !(a_e->mode & 0200) )
		l_attr	|= WCX$M_ATTR_READONLY;

	if ( l_base [0] == '.' )
		l_attr	|= WCX$M_ATTR_HIDDEN;

	return	l_attr;
}


/*
**  The next header into the Unicode Ex structure; the others are made
**  from it
*/
static	int	s_vbkw$header	(
		VBKW$ARC *	a_arc,
		WCX$HDREXW *	a_h
			)
{
VBKW$ENT *	l_e;
int		l_status;

	if ( a_arc->catalog )
		{
		if ( a_arc->next >= a_arc->nents )
			return	WCX$K_E_END_ARCHIVE;

		a_arc->cur = (long) a_arc->next++;
		}
	else if ( (l_status = s_vbkw$streamnext(a_arc)) )
		return	l_status;

	l_e	= s_vbkw$current(a_arc);

	memset(a_h, 0, sizeof(*a_h));

	s_vbkw$towide(a_arc->rctx.spec, strlen(a_arc->rctx.spec), a_h->ArcName, WCX$K_NAMEW);
	s_vbkw$towide(l_e->path, strlen(l_e->path), a_h->FileName, WCX$K_NAMEW);

	for ( WCX$WCHAR *l_p = a_h->FileName; *l_p; l_p++ )
		if ( *l_p == '/' )
			*l_p = WCX$SEP;

	a_h->UnpSize	  = (unsigned int) (l_e->size & 0xFFFFFFFFU);
	a_h->UnpSizeHigh  = (unsigned int) (l_e->size >> 32);
	a_h->PackSize	  = a_h->UnpSize;
	a_h->PackSizeHigh = a_h->UnpSizeHigh;
	a_h->FileCRC	  = (int) l_e->crc;
	a_h->FileTime	  = s_vbkw$dostime(&l_e->mtime);
	a_h->FileAttr	  = s_vbkw$attr(l_e);

	return	0;
}


/*
**  The output file: the system's own calls, on both
*/
typedef struct vbkw_out_t
{
#ifdef	_WIN32
	HANDLE		h;
#else
	int		fd;
#endif
} VBKW$OUT;


#ifdef	_WIN32
/*
**  The directories a path needs, made.  Windows: no link is ever made
**  by the plugin there (a junction put in the target by somebody else is
**  that somebody's business)
*/
static	void	s_vbkw$mkparents	(
		WCX$WCHAR *	a_path
			)
{
	for ( WCX$WCHAR *l_p = a_path + 1; *l_p; l_p++ )
		{
		WCX$WCHAR	l_c = *l_p;

		if ( ((l_c != '/') && (l_c != '\\')) || (l_p [-1] == ':') )	/* ':\' - the root of a drive */
			continue;

		*l_p	= 0;
		CreateDirectoryW(a_path, NULL);
		*l_p	= l_c;
		}
}

#else

/*
**  Linux: the way from the target down to a file, a component at a time,
**  each directory made and opened with O_NOFOLLOW - a symbolic link on
**  the way (one this saveset made in an earlier run, or one that was
**  there) stops it: nothing is written outside the target.  The target
**  is the destination less the stored name of the file, when it ends so
**  (it does: the file manager adds the name it was given to its
**  directory); else its directory.  <a_all> - the last component is a
**  directory too, made and opened.
**
**  Returns the descriptor of the parent (or of the directory), and the
**  last component in <a_last>; -1 - the way is barred.
*/
static	int	s_vbkw$walk	(
	const	WCX$WCHAR *	a_dest,
	const	char *		a_path,
		int		a_all,
		char *		a_last,
		size_t		a_lastn
			)
{
char		l_n [WCX$K_SZ_PATH * 2], l_rel [WCX$K_SZ_PATH * 2], *l_c, *l_e;
const char *	l_root;
size_t		l_ln, l_lp = strlen(a_path);
int		l_fd, l_nfd;

	s_vbkw$tonarrow(a_dest, l_n, sizeof(l_n));
	l_ln	= strlen(l_n);

	if ( (l_ln > l_lp) && !strcmp(l_n + l_ln - l_lp, a_path) && (l_n [l_ln - l_lp - 1] == '/') )
		{
		snprintf(l_rel, sizeof(l_rel), "%s", a_path);
		l_n [l_ln - l_lp - 1] = '\0';
		l_root	= l_n [0] ? l_n : "/";
		}
	else if ( (l_c = strrchr(l_n, '/')) )
		{
		snprintf(l_rel, sizeof(l_rel), "%s", l_c + 1);
		*l_c	= '\0';
		l_root	= l_n [0] ? l_n : "/";
		}
	else	{
		snprintf(l_rel, sizeof(l_rel), "%s", l_n);
		l_root	= ".";
		}

	/* The target itself is the user's choice: made if it is not there, its links followed */
	if ( (0 > (l_fd = open(l_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC))) && (errno == ENOENT) && (l_root == l_n) )
		{
		for ( char *l_p = l_n + 1; *l_p; l_p++ )
			if ( *l_p == '/' )
				{
				*l_p	= '\0';
				mkdir(l_n, 0755);
				*l_p	= '/';
				}

		mkdir(l_root, 0755);
		l_fd	= open(l_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		}

	if ( l_fd < 0 )
		return	-1;

	for ( l_c = l_rel; ; l_c = l_e + 1 )
		{
		l_e	= strchr(l_c, '/');

		if ( l_e )
			*l_e	= '\0';

		if ( !l_e && !a_all )
			break;

		if ( !*l_c )
			{
			close(l_fd);

			return	-1;
			}

		mkdirat(l_fd, l_c, 0755);
		l_nfd	= openat(l_fd, l_c, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		close(l_fd);

		if ( 0 > (l_fd = l_nfd) )
			return	-1;

		if ( !l_e )
			{
			l_c	= (char *) ".";
			break;
			}
		}

	snprintf(a_last, a_lastn, "%s", l_c);

	return	l_fd;
}
#endif


/*
**  The destination of TC: DestPath and DestName, or DestName alone
*/
static	void	s_vbkw$dest	(
	const	WCX$WCHAR *	a_path,
	const	WCX$WCHAR *	a_name,
		WCX$WCHAR *	a_out,
		size_t		a_outn
			)
{
size_t	l_o = 0;

#ifdef	_WIN32
	/* A long name wants the \\?\ form; only a full one with a drive can have it */
	if ( a_path && a_path [0] && (a_path [1] == ':') && (l_o + 4 < a_outn) )
		{
		a_out [l_o++] = '\\'; a_out [l_o++] = '\\'; a_out [l_o++] = '?'; a_out [l_o++] = '\\';
		}
	else if ( !(a_path && a_path [0]) && a_name && a_name [0] && (a_name [1] == ':') && (l_o + 4 < a_outn) )
		{
		a_out [l_o++] = '\\'; a_out [l_o++] = '\\'; a_out [l_o++] = '?'; a_out [l_o++] = '\\';
		}
#endif

	for ( ; a_path && *a_path && ((l_o + 2) < a_outn); a_path++ )
		a_out [l_o++] = *a_path;

	if ( l_o && (a_out [l_o - 1] != '/') && (a_out [l_o - 1] != '\\') && a_name && a_name [0] && ((l_o + 2) < a_outn) )
		a_out [l_o++] = WCX$SEP;

	for ( ; a_name && *a_name && ((l_o + 1) < a_outn); a_name++ )
		a_out [l_o++] = (*a_name == '/') ? WCX$SEP : *a_name;

	a_out [l_o] = 0;

#ifdef	_WIN32
	/* The \\?\ form takes no '/' and no relative parts: TC gives a full name */
	for ( size_t i = 0; i < l_o; i++ )
		if ( a_out [i] == '/' )
			a_out [i] = '\\';
#endif
}


static	int	s_vbkw$create	(
		VBKW$OUT *	a_out,
		WCX$WCHAR *	a_dest,
	const	char *		a_path
			)
{
#ifdef	_WIN32
	(void) a_path;

	s_vbkw$mkparents(a_dest);
	a_out->h = CreateFileW(a_dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	return	(a_out->h != INVALID_HANDLE_VALUE) ? STS$K_SUCCESS : STS$K_ERROR;
#else
	{
	char	l_last [WCX$K_SZ_PATH * 2];
	int	l_par;

	if ( 0 > (l_par = s_vbkw$walk(a_dest, a_path, 0, l_last, sizeof(l_last))) )
		return	STS$K_ERROR;

	/* What is there - a link above all - is replaced, never followed nor written through (another name of it) */
	unlinkat(l_par, l_last, 0);
	a_out->fd = openat(l_par, l_last, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	close(l_par);

	return	(a_out->fd >= 0) ? STS$K_SUCCESS : STS$K_ERROR;
	}
#endif
}


static	int	s_vbkw$write	(
		VBKW$OUT *	a_out,
		uint64_t	a_off,
	const	uint8_t *	a_data,
		uint32_t	a_n
			)
{
#ifdef	_WIN32
LARGE_INTEGER	l_pos;
DWORD		l_done = 0;

	l_pos.QuadPart = (LONGLONG) a_off;

	if ( !SetFilePointerEx(a_out->h, l_pos, NULL, FILE_BEGIN) || !WriteFile(a_out->h, a_data, a_n, &l_done, NULL) || (l_done != a_n) )
		return	STS$K_ERROR;

	return	STS$K_SUCCESS;
#else
	while ( a_n )
		{
		ssize_t	l_rc = pwrite(a_out->fd, a_data, a_n, (off_t) a_off);

		if ( l_rc < 0 )
			{
			if ( errno == EINTR )
				continue;

			return	STS$K_ERROR;
			}

		a_data	+= l_rc;
		a_off	+= (uint64_t) l_rc;
		a_n	-= (uint32_t) l_rc;
		}

	return	STS$K_SUCCESS;
#endif
}


/*
**  The end of a file: its size, its times, read-only; closed
*/
static	int	s_vbkw$finish	(
		VBKW$OUT *	a_out,
		WCX$WCHAR *	a_path,
		uint64_t	a_size,
	const	VBKW$ENT *	a_e
			)
{
int	l_status = STS$K_SUCCESS;

#ifdef	_WIN32
LARGE_INTEGER	l_pos;
FILETIME	l_m, l_a;
uint64_t	l_t;

	l_pos.QuadPart = (LONGLONG) a_size;

	if ( !SetFilePointerEx(a_out->h, l_pos, NULL, FILE_BEGIN) || !SetEndOfFile(a_out->h) )
		l_status = STS$K_ERROR;

	l_t = (uint64_t) a_e->mtime.sec * 10000000ULL + a_e->mtime.nsec / 100 + 116444736000000000ULL;
	l_m.dwLowDateTime = (DWORD) l_t;
	l_m.dwHighDateTime = (DWORD) (l_t >> 32);
	l_t = (uint64_t) a_e->atime.sec * 10000000ULL + a_e->atime.nsec / 100 + 116444736000000000ULL;
	l_a.dwLowDateTime = (DWORD) l_t;
	l_a.dwHighDateTime = (DWORD) (l_t >> 32);

	SetFileTime(a_out->h, NULL, &l_a, &l_m);

	if ( !CloseHandle(a_out->h) )
		l_status = STS$K_ERROR;

	if ( !(a_e->mode & 0200) )
		SetFileAttributesW(a_path, FILE_ATTRIBUTE_READONLY);
#else
struct timespec	l_ts [2] = { { (time_t) a_e->atime.sec, (long) a_e->atime.nsec }, { (time_t) a_e->mtime.sec, (long) a_e->mtime.nsec } };

	(void) a_path;

	if ( ftruncate(a_out->fd, (off_t) a_size) )
		l_status = STS$K_ERROR;

	/* The mode and the times are worth having, not worth failing the file for */
	(void) !fchmod(a_out->fd, (mode_t) (a_e->mode & 07777));
	(void) !futimens(a_out->fd, l_ts);

	if ( close(a_out->fd) )
		l_status = STS$K_ERROR;
#endif

	return	l_status;
}


/*
**  The data of the file from where the stream stands: the FILE record
**  of <a_fileno> first (catalog mode: just reached by the seek), then its
**  DATA and DATAZ records up to its FEND - written into <a_out>, or only
**  summed (a test).  The progress goes to TC, which may stop it.
*/
static	int	s_vbkw$data	(
		VBKW$ARC *	a_arc,
		uint32_t	a_fileno,
		int		a_wantfile,
		VBKW$OUT *	a_out,
		WCX$WCHAR *	a_name,
		uint64_t *	a_size,
		uint32_t	a_crc,
		int		a_hascrc,
		int		a_keep
			)
{
VBK$RCTX *	l_r = &a_arc->rctx;
const uint8_t *	l_body, *l_data;
VBK$LOC		l_loc = {0};
uint32_t	l_len, l_crc = 0, l_dfno, l_n, l_fcrc = 0, l_pos;
uint64_t	l_off, l_fsize = *a_size;
uint16_t	l_type, l_tag;
int		l_damaged = 0, l_fend = 0, l_hasfcrc = 0;

	for ( ;; )
		{
		if ( !(1 & vbk$rd_next(l_r, &l_type, &l_body, &l_len, &l_loc)) )
			{
			l_damaged = 1;
			break;
			}

		if ( l_r->resync )
			l_damaged = a_arc->lost = 1;

		if ( l_type == VBK$K_RT_FILE )
			{
			VBKW$ENT	l_e;

			/* The FILE record that was sought: it must be this file's - a lost block would give the next one */
			if ( a_wantfile )
				{
				a_wantfile = 0;

				if ( !(1 & s_vbkw$parse(l_body, l_len, &l_e)) || (l_e.fileno != a_fileno) )
					{
					s_vbkw$entfree(&l_e);

					return	WCX$K_E_BAD_DATA;
					}

				s_vbkw$entfree(&l_e);
				continue;
				}

			/* The next file begins and this one has had no FEND: kept for the next header (the stream) */
			if ( a_keep )
				{
				if ( l_len > a_arc->looksz )
					{
					uint8_t *	l_p = realloc(a_arc->look, l_len);

					if ( !l_p )
						return	WCX$K_E_NO_MEMORY;

					a_arc->look	= l_p;
					a_arc->looksz	= l_len;
					}

				memcpy(a_arc->look, l_body, l_len);
				a_arc->looklen	= l_len;
				a_arc->lookloc	= l_loc;
				a_arc->haslook	= 1;
				}

			l_damaged = 1;
			break;
			}

		if ( a_wantfile )
			return	WCX$K_E_BAD_DATA;

		if ( (l_type == VBK$K_RT_DATA) || (l_type == VBK$K_RT_DATAZ) )
			{
			if ( STS$K_SUCCESS != vbk$data_get(l_type, l_body, l_len, a_arc->zbuf, &l_dfno, &l_off, &l_data, &l_n) )
				{
				l_damaged = 1;
				continue;
				}

			if ( l_dfno != a_fileno )
				continue;

			l_crc	= $VBK_CRC(l_crc, l_data, l_n);

			if ( a_out && !(1 & s_vbkw$write(a_out, l_off, l_data, l_n)) )
				return	WCX$K_E_EWRITE;

			/* TC's progress: 0 back - the user has stopped it */
			if ( a_arc->pdpw && !a_arc->pdpw(a_name, (int) l_n) )
				return	WCX$K_E_EABORTED;

			if ( !a_arc->pdpw && a_arc->pdp )
				{
				char	l_a [WCX$K_NAMEW];

				s_vbkw$widetoansi(a_name, l_a, sizeof(l_a));

				if ( !a_arc->pdp(l_a, (int) l_n) )
					return	WCX$K_E_EABORTED;
				}

			continue;
			}

		if ( l_type == VBK$K_RT_FEND )
			{
			l_pos	= 0;

			while ( 1 & vbk$tlv_next(l_body, l_len, &l_pos, &l_tag, &l_n, &l_data) )
				switch ( l_tag )
					{
					case	VBK$K_TAG_FILENO:	l_dfno = (uint32_t) vbk$tlv_getu(l_n, l_data);	break;
					case	VBK$K_TAG_SIZE:		l_fsize = vbk$tlv_getu(l_n, l_data);		break;
					case	VBK$K_TAG_CRC:		l_fcrc = (uint32_t) vbk$tlv_getu(l_n, l_data);
									l_hasfcrc = 1;					break;
					}

			l_fend	= 1;
			break;
			}

		if ( (l_type == VBK$K_RT_CATALOG) || (l_type == VBK$K_RT_END) )
			{
			l_damaged = 1;
			break;
			}
		}

	*a_size	= l_fsize;

	if ( l_damaged || !l_fend || (l_hasfcrc && (l_crc != l_fcrc)) || (a_hascrc && (l_crc != a_crc)) )
		return	WCX$K_E_BAD_DATA;

	return	0;
}


/*
**  Copy out, or test, the file of the last header
*/
static	int	s_vbkw$process	(
		VBKW$ARC *	a_arc,
		int		a_op,
		WCX$WCHAR *	a_dest
			)
{
VBKW$ENT *	l_e = s_vbkw$current(a_arc), *l_d;
VBKW$OUT	l_out;
uint64_t	l_size;
int		l_status, l_seek = a_arc->catalog;

	if ( !l_e )
		return	WCX$K_E_NOT_SUPPORTED;

	if ( a_op == WCX$K_PK_SKIP )
		return	0;

	if ( l_e->ftype == VBK$K_FT_DIR )
		{
		if ( a_op == WCX$K_PK_EXTRACT )
			{
#ifdef	_WIN32
			size_t	l_n = 0;

			while ( a_dest [l_n] )
				l_n++;

			if ( l_n + 2 < WCX$K_SZ_PATH )
				{
				a_dest [l_n] = WCX$SEP;
				a_dest [l_n + 1] = 0;
				s_vbkw$mkparents(a_dest);
				a_dest [l_n] = 0;
				}
#else
			char	l_last [WCX$K_SZ_PATH * 2];
			int	l_fd = s_vbkw$walk(a_dest, l_e->path, 1, l_last, sizeof(l_last));

			if ( l_fd < 0 )
				return	WCX$K_E_ECREATE;

			close(l_fd);
#endif
			}

		return	0;
		}

#ifndef	_WIN32
	/* A symbolic link, on Linux: made, never followed */
	if ( (l_e->ftype == VBK$K_FT_SYMLINK) && (a_op == WCX$K_PK_EXTRACT) && l_e->link )
		{
		char	l_n [WCX$K_SZ_PATH * 2];

		/*
		**  Made at the close, after every file of this run; and a link on
		**  the way to a file is never gone through (S_VBKW$WALK)
		*/
		if ( a_arc->nlnks == a_arc->szlnks )
			{
			size_t			l_new = a_arc->szlnks ? (a_arc->szlnks * 2) : 64;
			struct vbkw_lnk_t *	l_p = realloc(a_arc->lnks, l_new * sizeof(*l_p));

			if ( !l_p )
				return	WCX$K_E_NO_MEMORY;

			a_arc->lnks	= l_p;
			a_arc->szlnks	= l_new;
			}

		s_vbkw$tonarrow(a_dest, l_n, sizeof(l_n));

		if ( !(a_arc->lnks [a_arc->nlnks].path = strdup(l_n)) )
			return	WCX$K_E_NO_MEMORY;

		if ( !(a_arc->lnks [a_arc->nlnks].target = strdup(l_e->link)) || !(a_arc->lnks [a_arc->nlnks].name = strdup(l_e->path)) )
			{
			free(a_arc->lnks [a_arc->nlnks].path);
			free(a_arc->lnks [a_arc->nlnks].target);

			return	WCX$K_E_NO_MEMORY;
			}

		a_arc->nlnks++;

		return	0;
		}
#endif

	/* What is not a file - a link on Windows, a device, a FIFO, a socket - is listed, not made */
	if ( (l_e->ftype != VBK$K_FT_REG) && (l_e->ftype != VBK$K_FT_HARDLINK) )
		return	0;

	l_d	= l_e;

	/*
	**  A further name: the data of the first one.  Without a catalog the
	**  first one has gone by in the stream: back to it, and then back here.
	*/
	if ( l_e->ftype == VBK$K_FT_HARDLINK )
		{
		if ( a_arc->catalog )
			l_d	= (l_e->data >= 0) ? &a_arc->ents [l_e->data] : NULL;
		else	{
			l_d	= NULL;

			for ( size_t i = 0; l_e->link && (i < a_arc->nseen); i++ )
				if ( !strcmp(a_arc->seen [i].path, l_e->link) )
					l_d = &a_arc->seen [i];
			}

		if ( !l_d )
			return	WCX$K_E_BAD_DATA;

		l_seek	= 1;
		}

	if ( l_seek && (STS$K_SUCCESS != vbk$rd_seek(&a_arc->rctx, &l_d->loc)) )
		l_status = WCX$K_E_BAD_DATA;
	else if ( (a_op == WCX$K_PK_EXTRACT) && !(1 & s_vbkw$create(&l_out, a_dest, l_e->path)) )
		return	WCX$K_E_ECREATE;
	else	{
		l_size	= l_d->size;
		l_status = s_vbkw$data(a_arc, l_d->fileno, l_seek, (a_op == WCX$K_PK_EXTRACT) ? &l_out : NULL, a_dest, &l_size,
				l_d->crc, a_arc->catalog && l_d->hascrc, !l_seek);

		if ( (a_op == WCX$K_PK_EXTRACT) && !(1 & s_vbkw$finish(&l_out, a_dest, l_size, l_e)) && !l_status )
			l_status = WCX$K_E_EWRITE;
		}

	/* The stream goes on after the FILE record of the further name */
	if ( l_seek && !a_arc->catalog )
		{
		const uint8_t *	l_body;
		uint32_t	l_len;
		uint16_t	l_type;

		if ( STS$K_SUCCESS == vbk$rd_seek(&a_arc->rctx, &l_e->loc) )
			vbk$rd_next(&a_arc->rctx, &l_type, &l_body, &l_len, NULL);
		}

	return	l_status;
}


/*
**  The interface
*/
WCX$EXPORT	void *	WCX$CALL	OpenArchiveW	(
		WCX$OPENW *	a_data
			)
{
	return	s_vbkw$open(a_data->ArcName, &a_data->OpenResult);
}


WCX$EXPORT	void *	WCX$CALL	OpenArchive	(
		WCX$OPEN *	a_data
			)
{
WCX$WCHAR	l_w [WCX$K_SZ_PATH];

	s_vbkw$ansitowide(a_data->ArcName, l_w, WCX$K_SZ_PATH);

	return	s_vbkw$open(l_w, &a_data->OpenResult);
}


WCX$EXPORT	int	WCX$CALL	ReadHeaderExW	(
		void *		a_arc,
		WCX$HDREXW *	a_h
			)
{
	return	a_arc ? s_vbkw$header((VBKW$ARC *) a_arc, a_h) : WCX$K_E_BAD_ARCHIVE;
}


WCX$EXPORT	int	WCX$CALL	ReadHeaderEx	(
		void *		a_arc,
		WCX$HDREX *	a_h
			)
{
WCX$HDREXW	l_w;
int		l_status;

	if ( !a_arc )
		return	WCX$K_E_BAD_ARCHIVE;

	if ( (l_status = s_vbkw$header((VBKW$ARC *) a_arc, &l_w)) )
		return	l_status;

	memset(a_h, 0, sizeof(*a_h));
	s_vbkw$widetoansi(l_w.ArcName, a_h->ArcName, sizeof(a_h->ArcName));
	s_vbkw$widetoansi(l_w.FileName, a_h->FileName, sizeof(a_h->FileName));
	a_h->PackSize	  = l_w.PackSize;
	a_h->PackSizeHigh = l_w.PackSizeHigh;
	a_h->UnpSize	  = l_w.UnpSize;
	a_h->UnpSizeHigh  = l_w.UnpSizeHigh;
	a_h->FileCRC	  = l_w.FileCRC;
	a_h->FileTime	  = l_w.FileTime;
	a_h->FileAttr	  = l_w.FileAttr;

	return	0;
}


WCX$EXPORT	int	WCX$CALL	ReadHeader	(
		void *		a_arc,
		WCX$HDR *	a_h
			)
{
WCX$HDREXW	l_w;
int		l_status;

	if ( !a_arc )
		return	WCX$K_E_BAD_ARCHIVE;

	if ( (l_status = s_vbkw$header((VBKW$ARC *) a_arc, &l_w)) )
		return	l_status;

	memset(a_h, 0, sizeof(*a_h));
	s_vbkw$widetoansi(l_w.ArcName, a_h->ArcName, sizeof(a_h->ArcName));
	s_vbkw$widetoansi(l_w.FileName, a_h->FileName, sizeof(a_h->FileName));
	a_h->PackSize	= (l_w.PackSizeHigh || (l_w.PackSize > 0x7FFFFFFFU)) ? 0x7FFFFFFF : (int) l_w.PackSize;
	a_h->UnpSize	= a_h->PackSize;
	a_h->FileCRC	= l_w.FileCRC;
	a_h->FileTime	= l_w.FileTime;
	a_h->FileAttr	= l_w.FileAttr;

	return	0;
}


WCX$EXPORT	int	WCX$CALL	ProcessFileW	(
		void *		a_arc,
		int		a_op,
		WCX$WCHAR *	a_path,
		WCX$WCHAR *	a_name
			)
{
WCX$WCHAR	l_dest [WCX$K_SZ_PATH];

	if ( !a_arc )
		return	WCX$K_E_BAD_ARCHIVE;

	s_vbkw$dest(a_path, a_name, l_dest, WCX$K_SZ_PATH);

	return	s_vbkw$process((VBKW$ARC *) a_arc, a_op, l_dest);
}


WCX$EXPORT	int	WCX$CALL	ProcessFile	(
		void *		a_arc,
		int		a_op,
		char *		a_path,
		char *		a_name
			)
{
WCX$WCHAR	l_p [WCX$K_SZ_PATH], l_n [WCX$K_SZ_PATH], l_dest [WCX$K_SZ_PATH];

	if ( !a_arc )
		return	WCX$K_E_BAD_ARCHIVE;

	l_p [0] = l_n [0] = 0;

	if ( a_path )
		s_vbkw$ansitowide(a_path, l_p, WCX$K_SZ_PATH);

	if ( a_name )
		s_vbkw$ansitowide(a_name, l_n, WCX$K_SZ_PATH);

	s_vbkw$dest(l_p, l_n, l_dest, WCX$K_SZ_PATH);

	return	s_vbkw$process((VBKW$ARC *) a_arc, a_op, l_dest);
}


WCX$EXPORT	int	WCX$CALL	CloseArchive	(
		void *		a_arc
			)
{
VBKW$ARC *	l_arc = (VBKW$ARC *) a_arc;

	if ( !l_arc )
		return	WCX$K_E_ECLOSE;

	for ( size_t i = 0; i < l_arc->nents; i++ )
		s_vbkw$entfree(&l_arc->ents [i]);

	for ( size_t i = 0; i < l_arc->nseen; i++ )
		s_vbkw$entfree(&l_arc->seen [i]);

	/* The symbolic links, last: nothing is written through them now; one where a file is is not made */
	for ( size_t i = 0; i < l_arc->nlnks; i++ )
		{
#ifndef	_WIN32
		WCX$WCHAR	l_w [WCX$K_SZ_PATH];
		char		l_last [WCX$K_SZ_PATH * 2];
		struct stat	l_st;
		int		l_par;

		s_vbkw$towide(l_arc->lnks [i].path, strlen(l_arc->lnks [i].path), l_w, WCX$K_SZ_PATH);

		if ( 0 <= (l_par = s_vbkw$walk(l_w, l_arc->lnks [i].name, 0, l_last, sizeof(l_last))) )
			{
			if ( !fstatat(l_par, l_last, &l_st, AT_SYMLINK_NOFOLLOW) && S_ISLNK(l_st.st_mode) )
				unlinkat(l_par, l_last, 0);

			(void) !symlinkat(l_arc->lnks [i].target, l_par, l_last);
			close(l_par);
			}
#endif
		free(l_arc->lnks [i].path);
		free(l_arc->lnks [i].name);
		free(l_arc->lnks [i].target);
		}

	s_vbkw$entfree(&l_arc->sent);
	free(l_arc->seen);
	free(l_arc->lnks);
	vbk$rd_close(&l_arc->rctx);
	free(l_arc->ents);
	free(l_arc->look);
	free(l_arc->zbuf);
	free(l_arc);

	return	0;
}


/*
**  The callbacks: for one archive, or - the handle -1 (or none) - for all
**  that are opened afterwards
*/
#define	WCX$ANYARC(h)	(!(h) || ((h) == (void *) (intptr_t) -1))

WCX$EXPORT	void	WCX$CALL	SetChangeVolProc	(
		void *		a_arc,
		WCX$CHANGEVOL	a_proc
			)
{
	if ( WCX$ANYARC(a_arc) )
		s_cvp = a_proc;
	else	((VBKW$ARC *) a_arc)->cvp = a_proc;
}


WCX$EXPORT	void	WCX$CALL	SetChangeVolProcW	(
		void *		a_arc,
		WCX$CHANGEVOLW	a_proc
			)
{
	if ( WCX$ANYARC(a_arc) )
		s_cvpw = a_proc;
	else	((VBKW$ARC *) a_arc)->cvpw = a_proc;
}


WCX$EXPORT	void	WCX$CALL	SetProcessDataProc	(
		void *		a_arc,
		WCX$PROCDATA	a_proc
			)
{
	if ( WCX$ANYARC(a_arc) )
		s_pdp = a_proc;
	else	((VBKW$ARC *) a_arc)->pdp = a_proc;
}


WCX$EXPORT	void	WCX$CALL	SetProcessDataProcW	(
		void *		a_arc,
		WCX$PROCDATAW	a_proc
			)
{
	if ( WCX$ANYARC(a_arc) )
		s_pdpw = a_proc;
	else	((VBKW$ARC *) a_arc)->pdpw = a_proc;
}


/*
**  Read only: listed, copied out, tested - several files at once; and
**  known by its contents as well as by the extension
*/
WCX$EXPORT	int	WCX$CALL	GetPackerCaps	(void)
{
	return	WCX$M_PK_CAPS_MULTIPLE | WCX$M_PK_CAPS_BY_CONTENT;
}


WCX$EXPORT	int	WCX$CALL	CanYouHandleThisFileW	(
		WCX$WCHAR *	a_name
			)
{
char	l_spec [VBK$K_SZ_SPEC];

	s_vbkw$tonarrow(a_name, l_spec, sizeof(l_spec));

	return	vbk$rd_probe(l_spec) == STS$K_SUCCESS;
}


WCX$EXPORT	int	WCX$CALL	CanYouHandleThisFile	(
		char *		a_name
			)
{
WCX$WCHAR	l_w [WCX$K_SZ_PATH];

	s_vbkw$ansitowide(a_name, l_w, WCX$K_SZ_PATH);

	return	CanYouHandleThisFileW(l_w);
}
