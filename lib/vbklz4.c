#define	__MODULE__	"VBKLZ4"
#define	__IDENT__	"X01-04"
#define	__REV__		"1.4.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbklz4.c
**
**  ABSTRACT:	The LZ4 block format (format.md, 6.7): a greedy compressor
**		and a decompressor that trusts nothing it is given.
**
**  DESCRIPTION: The compressor is the plain greedy one: a table of 4096
**		positions hashed from 4 octets, cleared for every call, the
**		first match taken and extended forward.  It is not the
**		fastest nor the tightest; it is short, and the same input
**		makes the same output on every machine - the saveset of one
**		tree is the same however often it is made.
**
**		The decompressor checks every length against what is left of
**		the input and of the output, every offset against what has
**		been written, and wants exactly RAWLEN octets out of exactly
**		the input given: anything else is a bad record, never a write
**		out of bounds.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  4-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-04		 4-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<string.h>

#include	"vbklz4.h"
#include	"vbkos.h"

#define	VBK$K_LZ4HLOG	12			/* log2 of the entries of the hash table	*/
#define	VBK$K_LZ4MIN	4			/* Shortest match				*/
#define	VBK$K_LZ4LAST	5			/* The last octets are always literals		*/
#define	VBK$K_LZ4MFL	12			/* No match begins in the last 12 octets	*/
#define	VBK$K_LZ4MAXOFF	65535			/* Farthest a match looks back			*/


static	uint32_t	s_vbk$get32	(
	const	uint8_t *	a_p
			)
{
	return	(uint32_t) a_p [0] | ((uint32_t) a_p [1] << 8) | ((uint32_t) a_p [2] << 16) | ((uint32_t) a_p [3] << 24);
}


/*
**  A length beyond the nibble: as many 255s as it takes, then the rest
*/
static	int	s_vbk$putlen	(
		uint8_t *	a_dst,
		uint32_t	a_cap,
		uint32_t *	a_op,
		uint32_t	a_len
			)
{
	for ( ; a_len >= 255; a_len -= 255 )
		{
		if ( *a_op >= a_cap )
			return	0;

		a_dst [(*a_op)++] = 255;
		}

	if ( *a_op >= a_cap )
		return	0;

	a_dst [(*a_op)++] = (uint8_t) a_len;

	return	1;
}


/*
**  One sequence: the literals from <a_lit> on, <a_nlit> of them, then -
**  but for the last sequence, <a_mlen> 0 - the match
*/
static	int	s_vbk$sequence	(
		uint8_t *	a_dst,
		uint32_t	a_cap,
		uint32_t *	a_op,
	const	uint8_t *	a_lit,
		uint32_t	a_nlit,
		uint32_t	a_off,
		uint32_t	a_mlen
			)
{
uint32_t	l_tok = *a_op, l_ml = a_mlen ? (a_mlen - VBK$K_LZ4MIN) : 0;

	if ( *a_op >= a_cap )
		return	0;

	a_dst [(*a_op)++] = (uint8_t) (((a_nlit < 15) ? a_nlit : 15) << 4);

	if ( (a_nlit >= 15) && !s_vbk$putlen(a_dst, a_cap, a_op, a_nlit - 15) )
		return	0;

	if ( a_nlit > (a_cap - *a_op) )
		return	0;

	memcpy(a_dst + *a_op, a_lit, a_nlit);
	*a_op	+= a_nlit;

	if ( !a_mlen )
		return	1;

	if ( (a_cap - *a_op) < 2 )
		return	0;

	a_dst [(*a_op)++] = (uint8_t) (a_off & 0xFF);
	a_dst [(*a_op)++] = (uint8_t) (a_off >> 8);
	a_dst [l_tok]	 |= (uint8_t) ((l_ml < 15) ? l_ml : 15);

	if ( (l_ml >= 15) && !s_vbk$putlen(a_dst, a_cap, a_op, l_ml - 15) )
		return	0;

	return	1;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Compress a buffer into the LZ4 block format.
**
**  FORMAL PARAMETERS:
**
**	a_src, a_len	The octets in
**	a_dst, a_cap	Where they go, and its size
**	a_outlen	Receives the octets written
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- compressed;
**	STS$K_WARN	- it does not fit into <a_cap>: the caller stores the
**			  octets as they are.
**--
*/
int	vbk$lz4_compress	(
	const	uint8_t *	a_src,
		uint32_t	a_len,
		uint8_t *	a_dst,
		uint32_t	a_cap,
		uint32_t *	a_outlen
			)
{
uint32_t	l_tab [1 << VBK$K_LZ4HLOG];
uint32_t	l_ip = 0, l_anchor = 0, l_op = 0, l_seq, l_h, l_ref, l_mlen;

	memset(l_tab, 0xFF, sizeof(l_tab));

	if ( a_len > VBK$K_LZ4MFL )
		{
		uint32_t	l_limit = a_len - VBK$K_LZ4MFL, l_mlimit = a_len - VBK$K_LZ4LAST;

		while ( l_ip < l_limit )
			{
			l_seq	= s_vbk$get32(a_src + l_ip);
			l_h	= (l_seq * 2654435761U) >> (32 - VBK$K_LZ4HLOG);
			l_ref	= l_tab [l_h];
			l_tab [l_h] = l_ip;

			if ( (l_ref == UINT32_MAX) || ((l_ip - l_ref) > VBK$K_LZ4MAXOFF) || (s_vbk$get32(a_src + l_ref) != l_seq) )
				{
				l_ip++;
				continue;
				}

			for ( l_mlen = VBK$K_LZ4MIN; ((l_ip + l_mlen) < l_mlimit) && (a_src [l_ref + l_mlen] == a_src [l_ip + l_mlen]); l_mlen++ )
				;

			if ( !s_vbk$sequence(a_dst, a_cap, &l_op, a_src + l_anchor, l_ip - l_anchor, l_ip - l_ref, l_mlen) )
				return	STS$K_WARN;

			l_ip	+= l_mlen;
			l_anchor = l_ip;
			}
		}

	/* The last literals: at least the last five octets, or all of a short buffer */
	if ( !s_vbk$sequence(a_dst, a_cap, &l_op, a_src + l_anchor, a_len - l_anchor, 0, 0) )
		return	STS$K_WARN;

	*a_outlen = l_op;

	return	STS$K_SUCCESS;
}


/*
**  A length beyond the nibble, read: none of it may pass <a_max>
*/
static	int	s_vbk$getlen	(
	const	uint8_t *	a_src,
		uint32_t	a_len,
		uint32_t *	a_ip,
		uint32_t *	a_val,
		uint32_t	a_max
			)
{
uint8_t	l_b;

	do	{
		if ( *a_ip >= a_len )
			return	0;

		l_b	= a_src [(*a_ip)++];
		*a_val	+= l_b;

		if ( *a_val > a_max )
			return	0;
		}
	while ( l_b == 255 );

	return	1;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Decompress an LZ4 block of exactly <a_len> octets into exactly
**	<a_rawlen> octets.
**
**  FORMAL PARAMETERS:
**
**	a_src, a_len	The compressed octets
**	a_dst		Receives <a_rawlen> octets
**	a_rawlen	How many they must be
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- done;
**	STS$K_ERROR	- the block is bad: a length or an offset out of
**			  bounds, too few or too many octets.  What is in
**			  <a_dst> then is not to be used.
**--
*/
int	vbk$lz4_decompress	(
	const	uint8_t *	a_src,
		uint32_t	a_len,
		uint8_t *	a_dst,
		uint32_t	a_rawlen
			)
{
uint32_t	l_ip = 0, l_op = 0, l_lit, l_off, l_ml;
uint8_t		l_tok;

	for ( ;; )
		{
		if ( l_ip >= a_len )
			return	STS$K_ERROR;

		l_tok	= a_src [l_ip++];
		l_lit	= l_tok >> 4;

		if ( (l_lit == 15) && !s_vbk$getlen(a_src, a_len, &l_ip, &l_lit, a_rawlen) )
			return	STS$K_ERROR;

		if ( (l_lit > (a_len - l_ip)) || (l_lit > (a_rawlen - l_op)) )
			return	STS$K_ERROR;

		memcpy(a_dst + l_op, a_src + l_ip, l_lit);
		l_ip	+= l_lit;
		l_op	+= l_lit;

		/* The last sequence has its literals only */
		if ( l_ip == a_len )
			break;

		if ( (a_len - l_ip) < 2 )
			return	STS$K_ERROR;

		l_off	= (uint32_t) a_src [l_ip] | ((uint32_t) a_src [l_ip + 1] << 8);
		l_ip	+= 2;

		if ( !l_off || (l_off > l_op) )
			return	STS$K_ERROR;

		l_ml	= l_tok & 15;

		if ( (l_ml == 15) && !s_vbk$getlen(a_src, a_len, &l_ip, &l_ml, a_rawlen) )
			return	STS$K_ERROR;

		l_ml	+= VBK$K_LZ4MIN;

		if ( l_ml > (a_rawlen - l_op) )
			return	STS$K_ERROR;

		/* Octet by octet: a match may overlap what it makes (a run) */
		for ( uint32_t i = 0; i < l_ml; i++, l_op++ )
			a_dst [l_op] = a_dst [l_op - l_off];
		}

	return	(l_op == a_rawlen) ? STS$K_SUCCESS : STS$K_ERROR;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The data of a DATA or a DATAZ record, one view for both: the file,
**	the offset, the octets - decompressed into <a_scratch> for DATAZ.
**
**  FORMAL PARAMETERS:
**
**	a_type		The record type
**	a_body, a_len	The record body
**	a_scratch	VBK$K_MAXDATA octets for a DATAZ record
**	a_fileno	Receives the FILENO
**	a_off		Receives the offset in the file
**	a_data		Receives where the octets are
**	a_n		Receives how many
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the data;
**	STS$K_WARN	- not a data record;
**	STS$K_ERROR	- a data record that is bad: too short, an unknown
**			  codec, a length beyond VBK$K_MAXDATA, a block that
**			  does not decompress.  Its file is incomplete.
**--
*/
int	vbk$data_get	(
		uint16_t	a_type,
	const	uint8_t *	a_body,
		uint32_t	a_len,
		uint8_t *	a_scratch,
		uint32_t *	a_fileno,
		uint64_t *	a_off,
	const	uint8_t **	a_data,
		uint32_t *	a_n
			)
{
uint32_t	l_raw;

	if ( a_type == VBK$K_RT_DATA )
		{
		if ( a_len < VBK$K_DATAHDR )
			return	STS$K_ERROR;

		*a_fileno = vbk$get32(a_body);
		*a_off	  = vbk$get64(a_body + 8);
		*a_data	  = a_body + VBK$K_DATAHDR;
		*a_n	  = a_len - VBK$K_DATAHDR;

		return	STS$K_SUCCESS;
		}

	if ( a_type != VBK$K_RT_DATAZ )
		return	STS$K_WARN;

	if ( a_len < VBK$K_DATAZHDR )
		return	STS$K_ERROR;

	*a_fileno = vbk$get32(a_body);
	*a_off	  = vbk$get64(a_body + 8);
	l_raw	  = vbk$get32(a_body + 16);

	if ( (vbk$get32(a_body + 4) != VBK$K_CODEC_LZ4) || (l_raw > VBK$K_MAXDATA) )
		return	STS$K_ERROR;

	if ( !(1 & vbk$lz4_decompress(a_body + VBK$K_DATAZHDR, a_len - VBK$K_DATAZHDR, a_scratch, l_raw)) )
		return	STS$K_ERROR;

	*a_data	= a_scratch;
	*a_n	= l_raw;

	return	STS$K_SUCCESS;
}
