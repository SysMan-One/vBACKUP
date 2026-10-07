#define	__MODULE__	"VBKDFL"
#define	__IDENT__	"X01-20"
#define	__REV__		"1.20.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkdfl.c
**
**  ABSTRACT:	Codec 2 of the DATAZ record: raw Deflate (RFC 1951,
**		format.md 6.7.2), writer and reader, from the RFC alone.
**
**  DESCRIPTION: The writer.  LZ77 over a window of 32 KB: hash chains of
**		three octets, a match of 3 .. 258 octets; at effort 1 the
**		first good match is taken (greedy), from effort 2 on the
**		match is put off by one octet when the next one is longer
**		(lazy, as zlib does).  Every 16384 symbols a block is closed
**		and written the smallest way of three: dynamic Huffman codes
**		(lengths limited to 15, the code lengths to 7, by halving the
**		counts until they fit), the fixed codes of the RFC, or stored.
**		Ties go to the lower index everywhere: the same octets in,
**		the same octets out, whatever thread compresses them.
**
**		The reader.  Stored, fixed and dynamic blocks; a Huffman code
**		is looked up in a table of 9 bits, a longer one is decoded a
**		bit at a time.  Every code set is checked (none
**		over-subscribed; an incomplete one only when it has a single
**		code), every length and distance against what has been
**		output and against the length expected.  A stream is good
**		when it gives exactly <rawlen> octets and its final block
**		ends in the last octet of the input.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  6-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-20		 6-OCT-2026	RRL
**		The hash table by the length of the record: a record of a few KB no
**		longer sets up 128 KB of it.
**
**	X01-19		 6-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>

#include	"vbkdfl.h"
#include	"vbkos.h"

#define	VBK$K_DFLWSIZE	32768			/* The window				*/
#define	VBK$K_DFLWMASK	(VBK$K_DFLWSIZE - 1)
#define	VBK$K_DFLHBITS	15			/* Bits of the hash table at most	*/
#define	VBK$K_DFLMIN	3			/* Shortest match			*/
#define	VBK$K_DFLMAX	258			/* Longest match			*/
#define	VBK$K_DFLBLOCK	16384			/* Symbols of a block			*/
#define	VBK$K_DFLNLIT	286			/* Literal/length codes			*/
#define	VBK$K_DFLNDIST	30			/* Distance codes			*/
#define	VBK$K_DFLFAST	9			/* Bits of the lookup table of the reader */

static	const uint16_t	s_lbase [29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
					 131, 163, 195, 227, 258 };
static	const uint8_t	s_lext [29]  = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static	const uint16_t	s_dbase [30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
					 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static	const uint8_t	s_dext [30]  = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static	const uint8_t	s_clorder [19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/*
**  The efforts of the writer: how many candidates of a chain are looked
**  at, the length that is good enough to stop, greedy or lazy
*/
static	const struct { uint16_t chain, nice; uint8_t lazy; } s_effort [VBK$K_DFLLEVELS] = {
	{ 4, 16, 0 }, { 16, 32, 1 }, { 64, 128, 1 }, { 512, 258, 1 } };


/*
**  The writer of bits: LSB first, as the RFC packs them
*/
typedef struct vbk_dflout_t
{
	uint8_t *	dst;
	uint32_t	cap, op;
	uint64_t	bits;
	int		n;
	int		over;			/* It did not fit			*/
} VBK$DFLOUT;

static	void	s_vbk$put	(
		VBK$DFLOUT *	a_o,
		uint32_t	a_v,
		int		a_n
			)
{
	a_o->bits |= (uint64_t) a_v << a_o->n;
	a_o->n	  += a_n;

	while ( a_o->n >= 8 )
		{
		if ( a_o->op >= a_o->cap )
			{
			a_o->over = 1;
			a_o->n	  = 0;
			a_o->bits = 0;

			return;
			}

		a_o->dst [a_o->op++] = (uint8_t) a_o->bits;
		a_o->bits >>= 8;
		a_o->n	  -= 8;
		}
}

/*
**  To the next octet: the padding bits are zero
*/
static	void	s_vbk$align	(
		VBK$DFLOUT *	a_o
			)
{
	if ( a_o->n & 7 )
		s_vbk$put(a_o, 0, 8 - (a_o->n & 7));
}

static	uint32_t	s_vbk$rev	(
		uint32_t	a_code,
		int		a_len
			)
{
uint32_t	l_r = 0;

	for ( int i = 0; i < a_len; i++, a_code >>= 1 )
		l_r = (l_r << 1) | (a_code & 1);

	return	l_r;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The lengths of a Huffman code for the counts given, none longer
**	than <a_limit>.  The tree is built by taking the two lightest nodes
**	(the lower index on a tie); when it is too deep the counts are
**	halved - none of them to zero - and it is built again.  A single
**	symbol gets the length 1.
**
**  FORMAL PARAMETERS:
**
**	a_freq		The counts, <a_n> of them
**	a_n		Symbols, 288 at most
**	a_limit		The longest code
**	a_len		Receives the lengths, 0 - a symbol not used
**
**  RETURN VALUE:
**	None.
**--
*/
static	void	s_vbk$lengths	(
	const	uint32_t *	a_freq,
		int		a_n,
		int		a_limit,
		uint8_t *	a_len
			)
{
uint32_t	l_w [576], l_f [288];
int16_t		l_par [576];
uint8_t		l_alive [576];
int		l_m = 0, l_nodes, l_max;

	memset(a_len, 0, (size_t) a_n);

	for ( int i = 0; i < a_n; i++ )
		if ( (l_f [i] = a_freq [i]) )
			l_m++;

	if ( !l_m )
		return;

	if ( l_m == 1 )
		{
		for ( int i = 0; i < a_n; i++ )
			if ( l_f [i] )
				a_len [i] = 1;

		return;
		}

	for ( ;; )
		{
		l_nodes	= a_n;

		for ( int i = 0; i < a_n; i++ )
			{
			l_w [i]	    = l_f [i];
			l_alive [i] = (l_f [i] != 0);
			l_par [i]   = -1;
			}

		for ( int k = 1; k < l_m; k++ )
			{
			int	l_a = -1, l_b = -1;

			for ( int i = 0; i < l_nodes; i++ )
				{
				if ( !l_alive [i] )
					continue;

				if ( (l_a < 0) || (l_w [i] < l_w [l_a]) )
					l_b = l_a, l_a = i;
				else if ( (l_b < 0) || (l_w [i] < l_w [l_b]) )
					l_b = i;
				}

			l_w [l_nodes]	  = l_w [l_a] + l_w [l_b];
			l_alive [l_nodes] = 1;
			l_par [l_nodes]	  = -1;
			l_alive [l_a]	  = l_alive [l_b] = 0;
			l_par [l_a]	  = l_par [l_b] = (int16_t) l_nodes;
			l_nodes++;
			}

		l_max	= 0;

		for ( int i = 0; i < a_n; i++ )
			{
			int	l_d = 0;

			if ( !l_f [i] )
				continue;

			for ( int j = i; l_par [j] >= 0; j = l_par [j] )
				l_d++;

			a_len [i] = (uint8_t) ((l_d > 255) ? 255 : l_d);
			l_max	  = (l_d > l_max) ? l_d : l_max;
			}

		if ( l_max <= a_limit )
			return;

		for ( int i = 0; i < a_n; i++ )
			if ( l_f [i] )
				l_f [i] = (l_f [i] >> 1) | 1;
		}
}

/*
**  The canonical codes of the lengths (RFC 1951 3.2.2), bit-reversed for
**  the writer of bits
*/
static	void	s_vbk$codes	(
	const	uint8_t *	a_len,
		int		a_n,
		uint16_t *	a_code
			)
{
uint16_t	l_count [16] = { 0 }, l_next [16];
uint16_t	l_c = 0;

	for ( int i = 0; i < a_n; i++ )
		l_count [a_len [i]]++;

	l_count [0] = 0;

	for ( int b = 1; b < 16; b++ )
		{
		l_c	   = (uint16_t) ((l_c + l_count [b - 1]) << 1);
		l_next [b] = l_c;
		}

	for ( int i = 0; i < a_n; i++ )
		a_code [i] = a_len [i] ? (uint16_t) s_vbk$rev(l_next [a_len [i]]++, a_len [i]) : 0;
}

static	int	s_vbk$lcode	(
		uint32_t	a_len
			)
{
int	l_i = 28;

	while ( s_lbase [l_i] > a_len )
		l_i--;

	return	l_i;
}

static	int	s_vbk$dcode	(
		uint32_t	a_dist
			)
{
int	l_i = 29;

	while ( s_dbase [l_i] > a_dist )
		l_i--;

	return	l_i;
}


/*
**  The fixed codes of the RFC: literal/length 8, 9, 7, 8 bits; distance 5
*/
static	void	s_vbk$fixed	(
		uint8_t *	a_llen,
		uint8_t *	a_dlen
			)
{
	for ( int i = 0; i < 288; i++ )
		a_llen [i] = (i < 144) ? 8 : (i < 256) ? 9 : (i < 280) ? 7 : 8;

	for ( int i = 0; i < 32; i++ )
		a_dlen [i] = 5;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Write one block of symbols the smallest way: dynamic codes, the
**	fixed ones, or stored octets.
**
**  FORMAL PARAMETERS:
**
**	a_o		The writer of bits
**	a_src		The input
**	a_beg, a_end	The octets of the input the block covers
**	a_tok		The symbols: a literal octet, or (length << 16) | distance
**	a_ntok		How many
**	a_last		The final block
**
**  RETURN VALUE:
**	None; A_O->OVER when it did not fit.
**--
*/
static	void	s_vbk$block	(
		VBK$DFLOUT *	a_o,
	const	uint8_t *	a_src,
		uint32_t	a_beg,
		uint32_t	a_end,
	const	uint32_t *	a_tok,
		uint32_t	a_ntok,
		int		a_last
			)
{
uint32_t	l_lf [288] = { 0 }, l_df [32] = { 0 }, l_cf [19] = { 0 };
uint8_t		l_llen [288], l_dlen [32], l_clen [19], l_flen [288], l_fdlen [32];
uint16_t	l_lcode [288], l_dcode [32], l_ccode [19];
uint8_t		l_seq [VBK$K_DFLNLIT + VBK$K_DFLNDIST], l_cl [VBK$K_DFLNLIT + VBK$K_DFLNDIST], l_cx [VBK$K_DFLNLIT + VBK$K_DFLNDIST];
uint64_t	l_dyn, l_fix, l_sto, l_extra = 0;
int		l_hlit, l_hdist, l_hclen, l_ncl = 0, l_nseq;
const uint8_t *	l_ul, *l_ud;
const uint16_t *l_uc, *l_udc;

	/* The counts, and the extra bits that are the same whatever the codes */
	for ( uint32_t i = 0; i < a_ntok; i++ )
		{
		if ( a_tok [i] < 256 )
			l_lf [a_tok [i]]++;
		else	{
			int	l_lc = s_vbk$lcode(a_tok [i] >> 16), l_dc = s_vbk$dcode(a_tok [i] & 0xFFFF);

			l_lf [257 + l_lc]++;
			l_df [l_dc]++;
			l_extra += s_lext [l_lc] + s_dext [l_dc];
			}
		}

	l_lf [256] = 1;

	/* No distance at all: one code of one bit all the same, as zlib does */
	{
	int	l_any = 0;

	for ( int i = 0; i < VBK$K_DFLNDIST; i++ )
		l_any |= (l_df [i] != 0);

	if ( !l_any )
		l_df [0] = 1;
	}

	s_vbk$lengths(l_lf, VBK$K_DFLNLIT, 15, l_llen);
	s_vbk$lengths(l_df, VBK$K_DFLNDIST, 15, l_dlen);
	memset(l_llen + VBK$K_DFLNLIT, 0, 2);
	memset(l_dlen + VBK$K_DFLNDIST, 0, 2);

	for ( l_hlit = VBK$K_DFLNLIT; (l_hlit > 257) && !l_llen [l_hlit - 1]; l_hlit-- )
		;

	for ( l_hdist = VBK$K_DFLNDIST; (l_hdist > 1) && !l_dlen [l_hdist - 1]; l_hdist-- )
		;

	/* The lengths, run-length coded by 16, 17, 18 */
	memcpy(l_seq, l_llen, (size_t) l_hlit);
	memcpy(l_seq + l_hlit, l_dlen, (size_t) l_hdist);
	l_nseq	= l_hlit + l_hdist;

	for ( int i = 0; i < l_nseq; )
		{
		int	l_r = 1;

		while ( ((i + l_r) < l_nseq) && (l_seq [i + l_r] == l_seq [i]) )
			l_r++;

		if ( !l_seq [i] && (l_r >= 3) )
			{
			l_r	= (l_r > 138) ? 138 : l_r;
			l_cl [l_ncl] = (l_r >= 11) ? 18 : 17;
			l_cx [l_ncl++] = (uint8_t) ((l_r >= 11) ? (l_r - 11) : (l_r - 3));
			i	+= l_r;
			}
		else if ( l_seq [i] && (l_r >= 4) )
			{
			l_cl [l_ncl] = l_seq [i];
			l_cx [l_ncl++] = 0;
			l_r	= ((l_r - 1) > 6) ? 6 : (l_r - 1);
			l_cl [l_ncl] = 16;
			l_cx [l_ncl++] = (uint8_t) (l_r - 3);
			i	+= 1 + l_r;
			}
		else	{
			l_cl [l_ncl] = l_seq [i];
			l_cx [l_ncl++] = 0;
			i++;
			}
		}

	for ( int i = 0; i < l_ncl; i++ )
		l_cf [l_cl [i]]++;

	s_vbk$lengths(l_cf, 19, 7, l_clen);

	for ( l_hclen = 19; (l_hclen > 4) && !l_clen [s_clorder [l_hclen - 1]]; l_hclen-- )
		;

	/* The three sizes, in bits */
	l_dyn	= 3 + 14 + 3 * (uint64_t) l_hclen + l_extra;

	for ( int i = 0; i < l_ncl; i++ )
		l_dyn += l_clen [l_cl [i]] + ((l_cl [i] == 16) ? 2 : (l_cl [i] == 17) ? 3 : (l_cl [i] == 18) ? 7 : 0);

	s_vbk$fixed(l_flen, l_fdlen);
	l_fix	= 3 + l_extra;

	for ( int i = 0; i < VBK$K_DFLNLIT; i++ )
		{
		l_dyn += (uint64_t) l_lf [i] * l_llen [i];
		l_fix += (uint64_t) l_lf [i] * l_flen [i];
		}

	for ( int i = 0; i < VBK$K_DFLNDIST; i++ )
		{
		/* The forced count of a block without distances is no symbol written */
		uint32_t	l_n = ((l_df [i] == 1) && (i == 0) && !l_extra) ? 0 : l_df [i];

		l_dyn += (uint64_t) l_n * l_dlen [i];
		l_fix += (uint64_t) l_n * l_fdlen [i];
		}

	l_sto	= 8 * (uint64_t) (a_end - a_beg) + (uint64_t) (((a_end - a_beg) / 65535) + 1) * (3 + 7 + 32);

	if ( (l_sto < l_dyn) && (l_sto < l_fix) )
		{
		/* Stored: 65535 octets a block at most */
		uint32_t	l_pos = a_beg;

		do	{
			uint32_t	l_n = ((a_end - l_pos) > 65535) ? 65535 : (a_end - l_pos);
			int		l_fin = a_last && ((l_pos + l_n) == a_end);

			s_vbk$put(a_o, (uint32_t) l_fin, 3);
			s_vbk$align(a_o);
			s_vbk$put(a_o, l_n, 16);
			s_vbk$put(a_o, ~l_n & 0xFFFF, 16);

			for ( uint32_t i = 0; (i < l_n) && !a_o->over; i++ )
				s_vbk$put(a_o, a_src [l_pos + i], 8);

			l_pos	+= l_n;
			}
		while ( (l_pos < a_end) && !a_o->over );

		return;
		}

	if ( l_fix <= l_dyn )
		{
		s_vbk$put(a_o, (uint32_t) a_last | (1 << 1), 3);
		l_ul	= l_flen;
		l_ud	= l_fdlen;
		}
	else	{
		s_vbk$put(a_o, (uint32_t) a_last | (2 << 1), 3);
		s_vbk$put(a_o, (uint32_t) (l_hlit - 257), 5);
		s_vbk$put(a_o, (uint32_t) (l_hdist - 1), 5);
		s_vbk$put(a_o, (uint32_t) (l_hclen - 4), 4);

		for ( int i = 0; i < l_hclen; i++ )
			s_vbk$put(a_o, l_clen [s_clorder [i]], 3);

		s_vbk$codes(l_clen, 19, l_ccode);

		for ( int i = 0; i < l_ncl; i++ )
			{
			s_vbk$put(a_o, l_ccode [l_cl [i]], l_clen [l_cl [i]]);

			if ( l_cl [i] >= 16 )
				s_vbk$put(a_o, l_cx [i], (l_cl [i] == 16) ? 2 : (l_cl [i] == 17) ? 3 : 7);
			}

		l_ul	= l_llen;
		l_ud	= l_dlen;
		}

	s_vbk$codes(l_ul, 288, l_lcode);
	s_vbk$codes(l_ud, 32, l_dcode);
	l_uc	= l_lcode;
	l_udc	= l_dcode;

	for ( uint32_t i = 0; (i < a_ntok) && !a_o->over; i++ )
		{
		if ( a_tok [i] < 256 )
			s_vbk$put(a_o, l_uc [a_tok [i]], l_ul [a_tok [i]]);
		else	{
			uint32_t	l_len = a_tok [i] >> 16, l_dist = a_tok [i] & 0xFFFF;
			int		l_lc = s_vbk$lcode(l_len), l_dc = s_vbk$dcode(l_dist);

			s_vbk$put(a_o, l_uc [257 + l_lc], l_ul [257 + l_lc]);
			s_vbk$put(a_o, l_len - s_lbase [l_lc], s_lext [l_lc]);
			s_vbk$put(a_o, l_udc [l_dc], l_ud [l_dc]);
			s_vbk$put(a_o, l_dist - s_dbase [l_dc], s_dext [l_dc]);
			}
		}

	s_vbk$put(a_o, l_uc [256], l_ul [256]);
}


/*
**  The hash of the three octets at <a_p>
*/
static	uint32_t	s_vbk$hash	(
	const	uint8_t *	a_p,
		uint32_t	a_bits
			)
{
	return	(((uint32_t) a_p [0] | ((uint32_t) a_p [1] << 8) | ((uint32_t) a_p [2] << 16)) * 2654435761U) >> (32 - a_bits);
}

typedef struct vbk_dflmf_t
{
	const uint8_t *	src;
	uint32_t	len;
	int32_t *	head;			/* 1 << HBITS: the last position of a hash	*/
	int32_t *	prev;			/* VBK$K_DFLWSIZE: the one before, in the window */
	uint32_t	chain, nice;
	uint32_t	hbits;			/* Of the hash table: by the length of the input */
} VBK$DFLMF;

static	void	s_vbk$insert	(
		VBK$DFLMF *	a_m,
		uint32_t	a_i
			)
{
uint32_t	l_h;

	if ( (a_i + 2) >= a_m->len )
		return;

	l_h	= s_vbk$hash(a_m->src + a_i, a_m->hbits);
	a_m->prev [a_i & VBK$K_DFLWMASK] = a_m->head [l_h];
	a_m->head [l_h] = (int32_t) a_i;
}

/*
**  The longest match at <a_i> among the positions of its chain, longer
**  than <a_best>; the position itself is inserted after the look
*/
static	uint32_t	s_vbk$longest	(
		VBK$DFLMF *	a_m,
		uint32_t	a_i,
		uint32_t	a_best,
		uint32_t *	a_dist
			)
{
const uint8_t *	l_s = a_m->src;
uint32_t	l_max = a_m->len - a_i, l_chain = a_m->chain;
int32_t		l_c;

	if ( l_max > VBK$K_DFLMAX )
		l_max	= VBK$K_DFLMAX;

	if ( l_max < VBK$K_DFLMIN )
		return	0;

	l_c	= a_m->head [s_vbk$hash(l_s + a_i, a_m->hbits)];

	while ( (l_c >= 0) && ((a_i - (uint32_t) l_c) <= VBK$K_DFLWSIZE) && l_chain-- )
		{
		const uint8_t *	l_p = l_s + l_c, *l_q = l_s + a_i;
		uint32_t	l_n;
		int32_t		l_next;

		if ( (a_best < l_max) && (l_p [a_best] == l_q [a_best]) && (l_p [0] == l_q [0]) )
			{
			for ( l_n = 0; (l_n < l_max) && (l_p [l_n] == l_q [l_n]); l_n++ )
				;

			if ( l_n > a_best )
				{
				a_best	= l_n;
				*a_dist	= a_i - (uint32_t) l_c;

				if ( l_n >= a_m->nice )
					break;
				}
			}

		l_next	= a_m->prev [l_c & VBK$K_DFLWMASK];

		/* A slot of the window taken by a newer position: the chain ends */
		if ( l_next >= l_c )
			break;

		l_c	= l_next;
		}

	return	(a_best >= VBK$K_DFLMIN) ? a_best : 0;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Compress octets into a raw Deflate stream (codec 2).
**
**  FORMAL PARAMETERS:
**
**	a_src, a_len	The octets
**	a_dst, a_cap	Where the stream goes, its size
**	a_outlen	Receives the length of the stream
**	a_effort	1 .. VBK$K_DFLLEVELS
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- compressed;
**	STS$K_ERROR	- it does not fit in <a_cap>;
**	STS$K_FATAL	- no memory.
**--
*/
int	vbk$dfl_compress	(
	const	uint8_t *	a_src,
		uint32_t	a_len,
		uint8_t *	a_dst,
		uint32_t	a_cap,
		uint32_t *	a_outlen,
		int		a_effort
			)
{
VBK$DFLOUT	l_o = { .dst = a_dst, .cap = a_cap };
VBK$DFLMF	l_m = { .src = a_src, .len = a_len };
uint32_t *	l_tok;
uint32_t	l_ntok = 0, l_beg = 0, l_i = 0, l_plen = 0, l_pdist = 0, l_len, l_dist = 0;
int		l_avail = 0, l_lazy;
void *		l_mem;

	a_effort = (a_effort < 1) ? 1 : (a_effort > VBK$K_DFLLEVELS) ? VBK$K_DFLLEVELS : a_effort;
	l_m.chain = s_effort [a_effort - 1].chain;
	l_m.nice  = s_effort [a_effort - 1].nice;
	l_lazy	  = s_effort [a_effort - 1].lazy;

	/* The hash table by the length: a record of a few KB gets a small one - setting it up costs as much as the record */
	for ( l_m.hbits = 8; (l_m.hbits < VBK$K_DFLHBITS) && ((1U << l_m.hbits) < (2 * a_len)); l_m.hbits++ )
		;

	if ( !(l_mem = malloc(((1U << l_m.hbits) + VBK$K_DFLWSIZE) * sizeof(int32_t) + (VBK$K_DFLBLOCK + 1) * sizeof(uint32_t))) )
		return	STS$K_FATAL;

	l_m.head = (int32_t *) l_mem;
	l_m.prev = l_m.head + (1U << l_m.hbits);
	l_tok	 = (uint32_t *) (l_m.prev + VBK$K_DFLWSIZE);
	memset(l_m.head, 0xFF, (1U << l_m.hbits) * sizeof(int32_t));

	/* A block is closed when it is full; <l_beg> is the first octet it covers */
#define	$DFL_EMIT(t, adv)	do { l_tok [l_ntok++] = (t); if ( l_ntok == VBK$K_DFLBLOCK ) { s_vbk$block(&l_o, a_src, l_beg, (adv), l_tok, l_ntok, 0); \
					l_ntok = 0; l_beg = (adv); } } while ( 0 )

	while ( (l_i < a_len) && !l_o.over )
		{
		l_len	= s_vbk$longest(&l_m, l_i, (l_lazy && l_avail) ? l_plen : (VBK$K_DFLMIN - 1), &l_dist);
		s_vbk$insert(&l_m, l_i);

		if ( !l_lazy )
			{
			if ( l_len )
				{
				$DFL_EMIT((l_len << 16) | l_dist, l_i + l_len);

				for ( uint32_t k = 1; k < l_len; k++ )
					s_vbk$insert(&l_m, l_i + k);

				l_i	+= l_len;
				}
			else	{
				$DFL_EMIT(a_src [l_i], l_i + 1);
				l_i++;
				}

			continue;
			}

		/* Lazy: the match of the octet before is taken unless this one has a longer */
		if ( l_avail && (l_plen >= VBK$K_DFLMIN) && (l_len <= l_plen) )
			{
			$DFL_EMIT((l_plen << 16) | l_pdist, l_i - 1 + l_plen);

			for ( uint32_t k = l_i + 1; k < (l_i - 1 + l_plen); k++ )
				s_vbk$insert(&l_m, k);

			l_i	= l_i - 1 + l_plen;
			l_avail	= 0;
			l_plen	= 0;
			continue;
			}

		if ( l_avail )
			$DFL_EMIT(a_src [l_i - 1], l_i);

		l_plen	= l_len;
		l_pdist	= l_dist;
		l_avail	= 1;
		l_i++;
		}

	if ( l_avail && !l_o.over )
		$DFL_EMIT(a_src [a_len - 1], a_len);

#undef	$DFL_EMIT

	if ( !l_o.over )
		s_vbk$block(&l_o, a_src, l_beg, a_len, l_tok, l_ntok, 1);

	if ( !l_o.over && l_o.n )
		s_vbk$put(&l_o, 0, 8 - l_o.n);

	free(l_mem);

	if ( l_o.over )
		return	STS$K_ERROR;

	*a_outlen = l_o.op;

	return	STS$K_SUCCESS;
}


/*
**  The reader of bits
*/
typedef struct vbk_dflin_t
{
	const uint8_t *	src;
	uint32_t	len, ip;
	uint64_t	bits;
	int		n;
} VBK$DFLIN;

static	void	s_vbk$fill	(
		VBK$DFLIN *	a_in
			)
{
	while ( (a_in->n <= 56) && (a_in->ip < a_in->len) )
		{
		a_in->bits |= (uint64_t) a_in->src [a_in->ip++] << a_in->n;
		a_in->n	   += 8;
		}
}

/*
**  <a_n> bits, 0 .. 16; -1 - the input ended
*/
static	int32_t	s_vbk$get	(
		VBK$DFLIN *	a_in,
		int		a_n
			)
{
int32_t	l_v;

	if ( a_in->n < a_n )
		{
		s_vbk$fill(a_in);

		if ( a_in->n < a_n )
			return	-1;
		}

	l_v	    = (int32_t) (a_in->bits & ((1U << a_n) - 1));
	a_in->bits >>= a_n;
	a_in->n	   -= a_n;

	return	l_v;
}

/*
**  A Huffman code of the reader: the counts of each length and the
**  symbols in canonical order (as puff of zlib keeps them), and a table
**  of the first VBK$K_DFLFAST bits: the symbol | its length << 9
*/
typedef struct vbk_dflhuff_t
{
	uint16_t	count [16];
	uint16_t	symbol [288];
	uint16_t	fast [1 << VBK$K_DFLFAST];
} VBK$DFLHUFF;

static	int	s_vbk$build	(
		VBK$DFLHUFF *	a_h,
	const	uint8_t *	a_len,
		int		a_n
			)
{
uint16_t	l_offs [16], l_next [16];
int		l_left = 1, l_codes = 0;
uint16_t	l_c = 0;

	memset(a_h->count, 0, sizeof(a_h->count));
	memset(a_h->fast, 0, sizeof(a_h->fast));

	for ( int i = 0; i < a_n; i++ )
		a_h->count [a_len [i]]++;

	/* Over-subscribed: refused; incomplete: only a single code (zlib allows it so) */
	for ( int b = 1; b < 16; b++ )
		{
		l_left	 = (l_left << 1) - a_h->count [b];
		l_codes	+= a_h->count [b];

		if ( l_left < 0 )
			return	0;
		}

	if ( l_left && (l_codes > 1) )
		return	0;

	l_offs [1] = 0;

	for ( int b = 1; b < 15; b++ )
		l_offs [b + 1] = (uint16_t) (l_offs [b] + a_h->count [b]);

	for ( int i = 0; i < a_n; i++ )
		if ( a_len [i] )
			a_h->symbol [l_offs [a_len [i]]++] = (uint16_t) i;

	/* The table of the short codes: every index whose low bits are the code reversed */
	for ( int b = 1; b < 16; b++ )
		{
		l_c	   = (uint16_t) ((l_c + ((b > 1) ? a_h->count [b - 1] : 0)) << 1);
		l_next [b] = l_c;
		}

	for ( int i = 0; i < a_n; i++ )
		{
		int		l_l = a_len [i];
		uint32_t	l_r;

		if ( !l_l || (l_l > VBK$K_DFLFAST) )
			continue;

		l_r	= s_vbk$rev(l_next [l_l]++, l_l);

		for ( uint32_t k = l_r; k < (1U << VBK$K_DFLFAST); k += (1U << l_l) )
			a_h->fast [k] = (uint16_t) (i | (l_l << 9));
		}

	return	1;
}

/*
**  One symbol; -1 - the input ended, or no code
*/
static	int	s_vbk$decode	(
		VBK$DFLIN *	a_in,
	const	VBK$DFLHUFF *	a_h
			)
{
int		l_code = 0, l_first = 0, l_index = 0;
uint16_t	l_e;

	if ( a_in->n < VBK$K_DFLFAST )
		s_vbk$fill(a_in);

	l_e	= a_h->fast [a_in->bits & ((1U << VBK$K_DFLFAST) - 1)];

	if ( l_e && ((l_e >> 9) <= a_in->n) )
		{
		a_in->bits >>= (l_e >> 9);
		a_in->n	   -= (l_e >> 9);

		return	l_e & 511;
		}

	/* A longer code: a bit at a time */
	for ( int b = 1; b < 16; b++ )
		{
		int32_t	l_bit = s_vbk$get(a_in, 1);

		if ( l_bit < 0 )
			return	-1;

		l_code	|= l_bit;

		if ( (l_code - a_h->count [b]) < l_first )
			return	a_h->symbol [l_index + (l_code - l_first)];

		l_index	+= a_h->count [b];
		l_first	+= a_h->count [b];
		l_first	<<= 1;
		l_code	<<= 1;
		}

	return	-1;
}

/*
**  The symbols of a block with codes, up to the end of block
*/
static	int	s_vbk$codesin	(
		VBK$DFLIN *	a_in,
	const	VBK$DFLHUFF *	a_lit,
	const	VBK$DFLHUFF *	a_dist,
		uint8_t *	a_dst,
		uint32_t	a_rawlen,
		uint32_t *	a_op
			)
{
uint32_t	l_op = *a_op;
int		l_sym;
int32_t		l_e;

	for ( ;; )
		{
		if ( 0 > (l_sym = s_vbk$decode(a_in, a_lit)) )
			return	0;

		if ( l_sym < 256 )
			{
			if ( l_op >= a_rawlen )
				return	0;

			a_dst [l_op++] = (uint8_t) l_sym;
			continue;
			}

		if ( l_sym == 256 )
			break;

		{
		uint32_t	l_len, l_dist;

		if ( (l_sym -= 257) >= 29 )
			return	0;

		if ( 0 > (l_e = s_vbk$get(a_in, s_lext [l_sym])) )
			return	0;

		l_len	= s_lbase [l_sym] + (uint32_t) l_e;

		if ( (0 > (l_sym = s_vbk$decode(a_in, a_dist))) || (l_sym >= 30) || (0 > (l_e = s_vbk$get(a_in, s_dext [l_sym]))) )
			return	0;

		l_dist	= s_dbase [l_sym] + (uint32_t) l_e;

		if ( (l_dist > l_op) || (l_len > (a_rawlen - l_op)) )
			return	0;

		for ( ; l_len; l_len--, l_op++ )
			a_dst [l_op] = a_dst [l_op - l_dist];
		}
		}

	*a_op	= l_op;

	return	1;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Decompress a raw Deflate stream (codec 2): exactly <a_rawlen>
**	octets out of exactly <a_len> octets in.
**
**  FORMAL PARAMETERS:
**
**	a_src, a_len	The stream
**	a_dst		Receives the octets, <a_rawlen> of room
**	a_rawlen	How many it must give
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the octets;
**	STS$K_ERROR	- a bad stream: a code, a length, a distance out of
**			  the rules, too few or too many octets out, input
**			  left over or missing.
**--
*/
int	vbk$dfl_decompress	(
	const	uint8_t *	a_src,
		uint32_t	a_len,
		uint8_t *	a_dst,
		uint32_t	a_rawlen
			)
{
VBK$DFLIN	l_in = { .src = a_src, .len = a_len };
VBK$DFLHUFF	l_lit, l_dist, l_cl;
uint8_t		l_lens [320];
uint32_t	l_op = 0;
int32_t		l_last, l_type;

	do	{
		if ( (0 > (l_last = s_vbk$get(&l_in, 1))) || (0 > (l_type = s_vbk$get(&l_in, 2))) )
			return	STS$K_ERROR;

		switch ( l_type )
			{
			case	0:
				{
				/* Stored: to the octet, LEN and its complement, the octets */
				int32_t		l_n, l_nn;

				l_in.bits >>= (l_in.n & 7);
				l_in.n	   -= (l_in.n & 7);

				if ( (0 > (l_n = s_vbk$get(&l_in, 16))) || (0 > (l_nn = s_vbk$get(&l_in, 16))) || (l_n != (~l_nn & 0xFFFF)) )
					return	STS$K_ERROR;

				if ( (uint32_t) l_n > (a_rawlen - l_op) )
					return	STS$K_ERROR;

				for ( ; l_n && (l_in.n >= 8); l_n-- )
					a_dst [l_op++] = (uint8_t) s_vbk$get(&l_in, 8);

				if ( (uint32_t) l_n > (l_in.len - l_in.ip) )
					return	STS$K_ERROR;

				memcpy(a_dst + l_op, l_in.src + l_in.ip, (size_t) l_n);
				l_op	 += (uint32_t) l_n;
				l_in.ip	 += (uint32_t) l_n;
				break;
				}

			case	1:
				{
				uint8_t	l_fl [288], l_fd [32];

				s_vbk$fixed(l_fl, l_fd);

				if ( !s_vbk$build(&l_lit, l_fl, 288) || !s_vbk$build(&l_dist, l_fd, 32) )
					return	STS$K_ERROR;

				if ( !s_vbk$codesin(&l_in, &l_lit, &l_dist, a_dst, a_rawlen, &l_op) )
					return	STS$K_ERROR;

				break;
				}

			case	2:
				{
				int32_t	l_hlit, l_hdist, l_hclen;
				uint8_t	l_cll [19] = { 0 };

				if ( (0 > (l_hlit = s_vbk$get(&l_in, 5))) || (0 > (l_hdist = s_vbk$get(&l_in, 5))) || (0 > (l_hclen = s_vbk$get(&l_in, 4))) )
					return	STS$K_ERROR;

				l_hlit	+= 257;
				l_hdist	+= 1;
				l_hclen	+= 4;

				if ( (l_hlit > 286) || (l_hdist > 30) )
					return	STS$K_ERROR;

				for ( int i = 0; i < l_hclen; i++ )
					{
					int32_t	l_v = s_vbk$get(&l_in, 3);

					if ( l_v < 0 )
						return	STS$K_ERROR;

					l_cll [s_clorder [i]] = (uint8_t) l_v;
					}

				if ( !s_vbk$build(&l_cl, l_cll, 19) )
					return	STS$K_ERROR;

				for ( int i = 0; i < (l_hlit + l_hdist); )
					{
					int	l_sym = s_vbk$decode(&l_in, &l_cl), l_rep;
					uint8_t	l_val = 0;

					if ( l_sym < 0 )
						return	STS$K_ERROR;

					if ( l_sym < 16 )
						{
						l_lens [i++] = (uint8_t) l_sym;
						continue;
						}

					if ( l_sym == 16 )
						{
						if ( !i )
							return	STS$K_ERROR;

						l_val	= l_lens [i - 1];
						l_rep	= 3 + s_vbk$get(&l_in, 2);
						}
					else if ( l_sym == 17 )
						l_rep	= 3 + s_vbk$get(&l_in, 3);
					else	l_rep	= 11 + s_vbk$get(&l_in, 7);

					if ( (l_rep < 3) || ((i + l_rep) > (l_hlit + l_hdist)) )
						return	STS$K_ERROR;

					while ( l_rep-- )
						l_lens [i++] = l_val;
					}

				/* No end-of-block code: no block can end */
				if ( !l_lens [256] )
					return	STS$K_ERROR;

				if ( !s_vbk$build(&l_lit, l_lens, l_hlit) || !s_vbk$build(&l_dist, l_lens + l_hlit, l_hdist) )
					return	STS$K_ERROR;

				if ( !s_vbk$codesin(&l_in, &l_lit, &l_dist, a_dst, a_rawlen, &l_op) )
					return	STS$K_ERROR;

				break;
				}

			default:
				return	STS$K_ERROR;
			}
		}
	while ( !l_last );

	/* All of it: the octets wanted, the input to its last octet - only the padding bits of that one left */
	if ( (l_op != a_rawlen) || (l_in.ip != l_in.len) || (l_in.n >= 8) )
		return	STS$K_ERROR;

	return	STS$K_SUCCESS;
}
