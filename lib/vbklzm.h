#ifndef	__VBKLZM$H__
#define	__VBKLZM$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKLZM"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-20"
#endif

#ifndef	__REV__
#define	__REV__		"1.20.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbklzm.h
**
**  ABSTRACT:	Codec 3 of the DATAZ record (format.md 6.7.3): raw LZMA1,
**		lc=3 lp=0 pb=2, a dictionary the size of the record, ended
**		by the end marker - written here from the specification of
**		the LZMA SDK, no library.  The writer: the "fast" parse of
**		the reference encoder over hash chains (/LEVEL=6, 7), the
**		optimal parse by the prices of the model over binary trees
**		(/LEVEL=8, 9); the same data makes the same octets.  The reader: every distance and length checked,
**		exactly the length expected, then the end marker.
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
**	X01-20		 6-OCT-2026	RRL
**		/LEVEL=8, 9: the optimal parse.
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

#define	VBK$K_LZMLEVELS	4			/* Efforts of the writer, 1 .. 4 (/LEVEL=6 .. 9) */

/*
**  The most octets VBK$LZM_COMPRESS may write for <n> octets in: a
**  literal that does not compress costs a little more than its 8 bits
*/
#define	VBK$LZM_BOUND(n)	((n) + ((n) / 32) + 64)

int	vbk$lzm_compress	(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_cap, uint32_t *a_outlen, int a_effort);
int	vbk$lzm_decompress	(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_rawlen);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKLZM$H__ */
