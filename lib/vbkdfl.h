#ifndef	__VBKDFL$H__
#define	__VBKDFL$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKDFL"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-22"
#endif

#ifndef	__REV__
#define	__REV__		"1.22.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkdfl.h
**
**  ABSTRACT:	Codec 2 of the DATAZ record (format.md 6.7.2): raw Deflate,
**		RFC 1951 - no zlib header, no checksum of its own - written
**		here from the RFC, no library.  The writer: LZ77 over hash
**		chains, lazy matching, dynamic, fixed or stored blocks, the
**		smallest; the same data makes the same octets.  The reader:
**		all three block types, every length and distance checked.
**
**  DESCRIPTION: No stdio, no StarLet, no threads: the utility, vbkx,
**		vbkx.exe and the WCX plugin link it alike.  Every call is
**		self-contained, so the compression pool runs them at once.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  6-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-22		 7-OCT-2026	RRL
**		VBK$DFL_DECODE: as VBK$DFL_DECOMPRESS, and how many octets out are right
**		when it fails - a SOLID cut by lost blocks is read up to there; the
**		codes of a block keep their place when they fail.
**
**	X01-19		 6-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdint.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	VBK$K_DFLLEVELS	4			/* Efforts of the writer, 1 .. 4 (/LEVEL=2 .. 5) */

/*
**  The most octets VBK$DFL_COMPRESS may write for <n> octets in: every
**  block - VBK$K_DFLBLOCK symbols, as many octets at most - stored, with its
**  header of 5 octets and its padding
*/
#define	VBK$DFL_BOUND(n)	((n) + 6 * (((n) / 16384) + 1) + 16)

int	vbk$dfl_compress	(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_cap, uint32_t *a_outlen, int a_effort);
int	vbk$dfl_decompress	(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_rawlen);
int	vbk$dfl_decode		(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_rawlen, uint32_t *a_got);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKDFL$H__ */
