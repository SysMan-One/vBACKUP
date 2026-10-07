#ifndef	__VBKLZ4$H__
#define	__VBKLZ4$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKLZ4"
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
**  MODULE:	vbklz4.h
**
**  ABSTRACT:	The codec of the DATAZ record (format.md, 5.1 and 6.7): the
**		LZ4 block format, written here from its description - no
**		library - and the one view of a DATA or DATAZ record every
**		reader takes the data of a file through.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  4-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-22		 7-OCT-2026	RRL
**		VBK$LZ4_DECODE: as VBK$LZ4_DECOMPRESS, and how many octets out are right
**		when it fails - a SOLID cut by lost blocks is read up to there.
**
**	X01-21		 7-OCT-2026	RRL
**		VBK$DATA_UNPACK: the octets of a codec, for the SOLID records too.
**
**	X01-19		 6-OCT-2026	RRL
**		Codecs 2 (raw Deflate, lib/vbkdfl.c) and 3 (raw LZMA1, lib/vbklzm.c);
**		VBK$DATA_PACK: the codec by /LEVEL, every record decompressed and
**		compared before it is written.
**
**	X01-04		 4-OCT-2026	RRL
**		Initial version.  VBK$LZ4_PACK: a probe of the head first.
**
**--
*/

#include	<stdint.h>

#include	"vbkfmt.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	VBK$K_DATAZHDR	20			/* fileno, codec, offset, rawlen of a DATAZ	*/

enum	{					/* CODEC of a DATAZ record			*/
	VBK$K_CODEC_LZ4 = 1,			/* LZ4 block format				*/
	VBK$K_CODEC_DEFLATE,			/* Raw Deflate, RFC 1951			*/
	VBK$K_CODEC_LZMA			/* Raw LZMA1, lc=3 lp=0 pb=2, end marker	*/
	};

#define	VBK$K_ZLEVELS	9			/* /LEVEL: 1 LZ4, 2 .. 5 Deflate, 6 .. 9 LZMA	*/

/*
**  The most octets VBK$LZ4_COMPRESS may write for <n> octets in: what
**  does not compress costs a token and the length bytes more
*/
#define	VBK$LZ4_BOUND(n)	((n) + ((n) / 255) + 16)

int	vbk$lz4_compress	(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_cap, uint32_t *a_outlen);
int	vbk$lz4_decompress	(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_rawlen);
int	vbk$lz4_decode		(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_rawlen, uint32_t *a_got);
int	vbk$lz4_pack		(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_cap, uint32_t *a_outlen);
int	vbk$data_pack		(int a_level, const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_cap, uint32_t *a_outlen,
				 uint32_t *a_codec, uint8_t *a_check);
uint32_t vbk$data_codec		(int a_level);
int	vbk$data_unpack		(uint32_t a_codec, const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_rawlen);
int	vbk$data_salvage	(uint32_t a_codec, const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_rawlen, uint32_t *a_got);
int	vbk$data_get		(uint16_t a_type, const uint8_t *a_body, uint32_t a_len, uint8_t *a_scratch,
				uint32_t *a_fileno, uint64_t *a_off, const uint8_t **a_data, uint32_t *a_n);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKLZ4$H__ */
