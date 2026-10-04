#ifndef	__VBKLZ4$H__
#define	__VBKLZ4$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKLZ4"
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
	VBK$K_CODEC_LZ4 = 1			/* LZ4 block format				*/
	};

/*
**  The most octets VBK$LZ4_COMPRESS may write for <n> octets in: what
**  does not compress costs a token and the length bytes more
*/
#define	VBK$LZ4_BOUND(n)	((n) + ((n) / 255) + 16)

int	vbk$lz4_compress	(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_cap, uint32_t *a_outlen);
int	vbk$lz4_decompress	(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_rawlen);
int	vbk$lz4_pack		(const uint8_t *a_src, uint32_t a_len, uint8_t *a_dst, uint32_t a_cap, uint32_t *a_outlen);
int	vbk$data_get		(uint16_t a_type, const uint8_t *a_body, uint32_t a_len, uint8_t *a_scratch,
				uint32_t *a_fileno, uint64_t *a_off, const uint8_t **a_data, uint32_t *a_n);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKLZ4$H__ */
