#define	__MODULE__	"VBKRS"
#define	__IDENT__	"X01-14"
#define	__REV__		"1.14.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkrs.c
**
**  ABSTRACT:	Reed-Solomon parity of a group of blocks (format.md 4.1).
**
**  DESCRIPTION: GF(2^8) with the polynomial 0x11D, addition the XOR.  The
**		coefficient of DATA block i in parity row j is
**
**		    a(j, i) = y_i / (j + y_i),	y_i = 128 + i
**
**		a Cauchy matrix 1 / (x_j + y_i), x_j = j, each column scaled
**		so that row 0 is all ones - the XOR block of version 1 is
**		row 0.  Every square submatrix of a Cauchy matrix is regular,
**		the scaling keeps it so: any n of the n + m blocks of a group
**		give the group back (proved for every erasure pattern of
**		n <= 10, m <= 4 by test/units.c).
**
**		The repair takes e good parity rows for e lost DATA blocks,
**		subtracts what the good DATA blocks give to them, and solves
**		the e x e system by Gauss-Jordan once for the group, then
**		applies its inverse to every byte.  Rows left over check the
**		result: a block whose CRC is right and whose bytes are not
**		is found, not delivered.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  5-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-14		 5-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>

#include	"vbkrs.h"
#include	"vbkos.h"

static	uint8_t		s_exp [512];		/* alpha^i, twice over: no modulo in a product	*/
static	uint8_t		s_log [256];
static	int		s_made;


/*
**  The tables of the field: alpha = 2, the polynomial 0x11D
*/
void	vbk$rs_init	(void)
{
uint32_t	l_x = 1;

	if ( s_made )
		return;

	for ( uint32_t i = 0; i < 255; i++ )
		{
		s_exp [i] = s_exp [i + 255] = (uint8_t) l_x;
		s_log [l_x] = (uint8_t) i;

		l_x	<<= 1;

		if ( l_x & 0x100 )
			l_x	^= 0x11D;
		}

	s_exp [510] = s_exp [511] = s_exp [0];
	s_made	= 1;
}


static	inline	uint8_t	s_vbk$mul	(
		uint8_t		a_a,
		uint8_t		a_b
			)
{
	return	(a_a && a_b) ? s_exp [s_log [a_a] + s_log [a_b]] : 0;
}


static	inline	uint8_t	s_vbk$inv	(
		uint8_t		a_a
			)
{
	return	s_exp [255 - s_log [a_a]];
}


/*
**  The coefficient of DATA block <a_col> in parity row <a_row>
*/
uint8_t	vbk$rs_coef	(
		uint32_t	a_row,
		uint32_t	a_col
			)
{
uint8_t	l_y = (uint8_t) (128 + a_col);

	if ( !a_row )
		return	1;

	return	s_vbk$mul(l_y, s_vbk$inv((uint8_t) (a_row ^ l_y)));
}


/*
**  dst += c * src, octet by octet; c = 1 is the XOR, a quadword at a time
**  where both are aligned
*/
void	vbk$rs_muladd	(
		uint8_t *	a_dst,
	const	uint8_t *	a_src,
		size_t		a_len,
		uint8_t		a_c
			)
{
uint8_t	l_t [256];
size_t	i = 0;

	if ( !a_c )
		return;

	if ( a_c == 1 )
		{
		if ( !(((uintptr_t) a_dst | (uintptr_t) a_src) & 7) )
			for ( ; i + 8 <= a_len; i += 8 )
				*(uint64_t *) (a_dst + i) ^= *(const uint64_t *) (a_src + i);

		for ( ; i < a_len; i++ )
			a_dst [i] ^= a_src [i];

		return;
		}

	/* The products of c, once: a lookup an octet */
	l_t [0] = 0;

	for ( uint32_t x = 1; x < 256; x++ )
		l_t [x] = s_exp [s_log [a_c] + s_log [x]];

	for ( ; i < a_len; i++ )
		a_dst [i] ^= l_t [a_src [i]];
}


/*
**  The inverse of the e x e matrix <a_m> into <a_inv>; 0 - it is singular
*/
static	int	s_vbk$invert	(
		uint8_t		a_m [VBK$K_MAXPAR] [VBK$K_MAXPAR],
		uint8_t		a_inv [VBK$K_MAXPAR] [VBK$K_MAXPAR],
		uint32_t	a_e
			)
{
	for ( uint32_t r = 0; r < a_e; r++ )
		for ( uint32_t c = 0; c < a_e; c++ )
			a_inv [r] [c] = (uint8_t) (r == c);

	for ( uint32_t c = 0; c < a_e; c++ )
		{
		uint32_t	l_p = c;
		uint8_t		l_f;

		while ( (l_p < a_e) && !a_m [l_p] [c] )
			l_p++;

		if ( l_p == a_e )
			return	0;

		if ( l_p != c )
			for ( uint32_t k = 0; k < a_e; k++ )
				{
				uint8_t	l_t = a_m [c] [k];	a_m [c] [k] = a_m [l_p] [k];	a_m [l_p] [k] = l_t;

				l_t = a_inv [c] [k];	a_inv [c] [k] = a_inv [l_p] [k];	a_inv [l_p] [k] = l_t;
				}

		l_f	= s_vbk$inv(a_m [c] [c]);

		for ( uint32_t k = 0; k < a_e; k++ )
			{
			a_m [c] [k]	= s_vbk$mul(a_m [c] [k], l_f);
			a_inv [c] [k]	= s_vbk$mul(a_inv [c] [k], l_f);
			}

		for ( uint32_t r = 0; r < a_e; r++ )
			{
			if ( (r == c) || !(l_f = a_m [r] [c]) )
				continue;

			for ( uint32_t k = 0; k < a_e; k++ )
				{
				a_m [r] [k]	^= s_vbk$mul(l_f, a_m [c] [k]);
				a_inv [r] [k]	^= s_vbk$mul(l_f, a_inv [c] [k]);
				}
			}
		}

	return	1;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Rebuild the bad DATA blocks of a group from its good blocks and its
**	good parity rows (format.md 4.1).
**
**  FORMAL PARAMETERS:
**
**	a_n		DATA blocks of the group
**	a_m		Parity rows of the group, row 0 the XOR block
**	a_data		The DATA vectors, <a_len> octets each; a bad one is
**			overwritten with what it was
**	a_dok		... good (1) or bad (0)
**	a_par		The parity vectors
**	a_pok		... good or bad
**	a_len		Octets of a vector
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- every bad DATA vector rebuilt (or none was bad);
**	STS$K_ERROR	- more bad DATA vectors than good parity rows: none
**			  is touched;
**	STS$K_WARN	- rebuilt, but a parity row left over disagrees: a
**			  block whose CRC is right holds other bytes - the
**			  rebuilt vectors are not to be trusted;
**	STS$K_FATAL	- no memory.
**--
*/
int	vbk$rs_repair	(
		uint32_t	a_n,
		uint32_t	a_m,
		uint8_t * const *a_data,
	const	uint8_t *	a_dok,
	const	uint8_t * const *a_par,
	const	uint8_t *	a_pok,
		size_t		a_len
			)
{
uint32_t	l_lost [VBK$K_MAXPAR], l_rows [VBK$K_MAXPAR], l_e = 0, l_nr = 0, l_extra [VBK$K_MAXPAR], l_nx = 0;
uint8_t		l_mat [VBK$K_MAXPAR] [VBK$K_MAXPAR], l_inv [VBK$K_MAXPAR] [VBK$K_MAXPAR];
uint8_t *	l_syn;
int		l_status = STS$K_SUCCESS;

	vbk$rs_init();

	for ( uint32_t i = 0; i < a_n; i++ )
		if ( !a_dok [i] )
			{
			if ( l_e == a_m )
				return	STS$K_ERROR;

			l_lost [l_e++] = i;
			}

	for ( uint32_t j = 0; j < a_m; j++ )
		if ( a_pok [j] )
			{
			if ( l_nr < l_e )
				l_rows [l_nr++] = j;
			else	l_extra [l_nx++] = j;
			}

	if ( l_nr < l_e )
		return	STS$K_ERROR;

	/* Nothing lost: a surplus row still checks the group - the caller asks only when it wants that */
	if ( !l_e && !l_nx )
		return	STS$K_SUCCESS;

	if ( !(l_syn = malloc((size_t) (l_e ? l_e : 1) * a_len)) )
		return	STS$K_FATAL;

	if ( l_e )
		{
		/* The syndromes: each row less what the good DATA vectors give it */
		for ( uint32_t k = 0; k < l_e; k++ )
			{
			uint8_t *	l_s = l_syn + (size_t) k * a_len;

			memcpy(l_s, a_par [l_rows [k]], a_len);

			for ( uint32_t i = 0; i < a_n; i++ )
				if ( a_dok [i] )
					vbk$rs_muladd(l_s, a_data [i], a_len, vbk$rs_coef(l_rows [k], i));

			for ( uint32_t t = 0; t < l_e; t++ )
				l_mat [k] [t] = vbk$rs_coef(l_rows [k], l_lost [t]);
			}

		if ( !s_vbk$invert(l_mat, l_inv, l_e) )
			{
			free(l_syn);

			return	STS$K_ERROR;
			}

		for ( uint32_t t = 0; t < l_e; t++ )
			{
			memset(a_data [l_lost [t]], 0, a_len);

			for ( uint32_t k = 0; k < l_e; k++ )
				vbk$rs_muladd(a_data [l_lost [t]], l_syn + (size_t) k * a_len, a_len, l_inv [t] [k]);
			}
		}

	/* The rows left over: each made again from the whole group, it must be what is on the medium */
	for ( uint32_t x = 0; (x < l_nx) && (l_status == STS$K_SUCCESS); x++ )
		{
		memset(l_syn, 0, a_len);

		for ( uint32_t i = 0; i < a_n; i++ )
			vbk$rs_muladd(l_syn, a_data [i], a_len, vbk$rs_coef(l_extra [x], i));

		if ( memcmp(l_syn, a_par [l_extra [x]], a_len) )
			l_status = STS$K_WARN;
		}

	free(l_syn);

	return	l_status;
}
