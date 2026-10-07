#define	__MODULE__	"VBKLZM"
#define	__IDENT__	"X01-22"
#define	__REV__		"1.22.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbklzm.c
**
**  ABSTRACT:	Codec 3 of the DATAZ record: raw LZMA1 (format.md 6.7.3),
**		writer and reader, from the specification of the LZMA SDK.
**
**  DESCRIPTION: The parameters are fixed: lc=3, lp=0, pb=2; the
**		dictionary is the record itself (1 MB at most); the stream
**		begins with the 5 octets of the range coder and ends with the
**		end marker (a match of distance 0xFFFFFFFF), so that any LZMA1
**		decoder - liblzma, python's lzma in FORMAT_RAW - reads it.
**
**		The writer, /LEVEL=8, 9 (efforts 3, 4): the optimal parse -
**		the prices of the model in 1/16 of a bit, a window of up to
**		2048 positions, the cheapest way through it over literals,
**		short reps, the repeated matches and every length of every
**		match the binary trees (BT of the LZMA SDK) give; the prices
**		of lengths and distances follow the model every 512 octets.
**
**		The writer, /LEVEL=6, 7: hash chains of three octets and a
**		table of the last position of every two octets; the four
**		repeated distances.  At each position the "fast" choice of the
**		reference encoder: a repeated match good enough, else the
**		longest match unless it is short and far, else a short rep or
**		a literal; a match is put off when the next position has a
**		better one.  Ties go to the lower index: the same octets in,
**		the same octets out.
**
**		The reader.  The range decoder of the specification; every
**		distance against what has been output, every length against
**		what is still wanted, exactly <rawlen> octets, then the end
**		marker with the code of the range coder at 0, the input used
**		up - anything else is a bad stream.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  6-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-22		 7-OCT-2026	RRL
**		VBK$LZM_DECODE: as VBK$LZM_DECOMPRESS, and how many octets out are right
**		when it fails: those of the symbols decoded before the input ran out.
**
**	X01-20		 6-OCT-2026	RRL
**		Stage 16: the optimal parse for /LEVEL=8, 9 - the prices of the model in
**		1/16 of a bit, a window of 2048 positions, the cheapest way over
**		literals, short reps, reps and every length of every match - over the
**		binary trees of the LZMA SDK (BT); the hash tables by the length of the
**		record; VBK$EMIT, VBK$FAST.
**
**	X01-19		 6-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>

#include	"vbklzm.h"
#include	"vbkos.h"

#define	VBK$K_LZMLC	3			/* Literal context bits			*/
#define	VBK$K_LZMPB	2			/* Position bits: 4 position states	*/
#define	VBK$K_LZMPOS	(1 << VBK$K_LZMPB)
#define	VBK$K_LZMMIN	2			/* Shortest match			*/
#define	VBK$K_LZMMAX	273			/* Longest match			*/
#define	VBK$K_LZMEND	0xFFFFFFFFU		/* The distance of the end marker	*/
#define	VBK$K_LZMH3	16			/* Bits of the hash of three octets	*/
#define	VBK$K_LZMPROB0	1024			/* A probability at its start: one half	*/

typedef	uint16_t	VBK$PROB;

typedef struct vbk_lzmlen_t
{
	VBK$PROB	choice, choice2;
	VBK$PROB	low [VBK$K_LZMPOS][8], mid [VBK$K_LZMPOS][8];
	VBK$PROB	high [256];
} VBK$LZMLEN;

/*
**  The probabilities of the model: the same for the writer and the
**  reader.  A bit tree is indexed from 1.
*/
typedef struct vbk_lzmprobs_t
{
	VBK$PROB	literal [0x300 << VBK$K_LZMLC];
	VBK$PROB	ismatch [12][VBK$K_LZMPOS], isrep0long [12][VBK$K_LZMPOS];
	VBK$PROB	isrep [12], isrepg0 [12], isrepg1 [12], isrepg2 [12];
	VBK$PROB	posslot [4][64];
	VBK$PROB	posspec [115];		/* 1 + 128 - 14					*/
	VBK$PROB	align [16];
	VBK$LZMLEN	len, replen;
} VBK$LZMPROBS;

#define	VBK$LZMPSIZE	((sizeof(VBK$LZMPROBS) + 15) & ~(size_t) 15)

static	const struct { uint16_t chain, nice; } s_effort [VBK$K_LZMLEVELS] = { { 16, 32 }, { 48, 64 }, { 32, 64 }, { 128, 273 } };


static	void	s_vbk$probs	(
		VBK$LZMPROBS *	a_p
			)
{
VBK$PROB *	l_p = (VBK$PROB *) a_p;

	for ( size_t i = 0; i < (sizeof(*a_p) / sizeof(VBK$PROB)); i++ )
		l_p [i] = VBK$K_LZMPROB0;
}

static	uint32_t	s_vbk$slot	(
		uint32_t	a_dist
			)
{
uint32_t	l_n;

	if ( a_dist < 4 )
		return	a_dist;

	l_n	= 31 - (uint32_t) __builtin_clz(a_dist);	/* The highest bit set: the optimal parse asks it often */

	return	(l_n << 1) | ((a_dist >> (l_n - 1)) & 1);
}

static	uint32_t	s_vbk$litstate	(
		uint32_t	a_s
			)
{
	return	(a_s < 4) ? 0 : (a_s < 10) ? (a_s - 3) : (a_s - 6);
}


/*
**  The range encoder of the specification: LOW of 33 bits, a carry kept
**  in CACHE and CACHESIZE
*/
typedef struct vbk_rcenc_t
{
	uint8_t *	dst;
	uint32_t	cap, op;
	int		over;
	uint64_t	low;
	uint32_t	range;
	uint8_t		cache;
	uint64_t	cachesize;
} VBK$RCENC;

static	void	s_vbk$shiftlow	(
		VBK$RCENC *	a_rc
			)
{
	if ( ((uint32_t) a_rc->low < 0xFF000000U) || (a_rc->low >> 32) )
		{
		uint8_t	l_t = a_rc->cache;

		do	{
			if ( a_rc->op < a_rc->cap )
				a_rc->dst [a_rc->op++] = (uint8_t) (l_t + (uint8_t) (a_rc->low >> 32));
			else	a_rc->over = 1;

			l_t	= 0xFF;
			}
		while ( --a_rc->cachesize );

		a_rc->cache = (uint8_t) (a_rc->low >> 24);
		}

	a_rc->cachesize++;
	a_rc->low = (a_rc->low & 0x00FFFFFFU) << 8;
}

static	void	s_vbk$ebit	(
		VBK$RCENC *	a_rc,
		VBK$PROB *	a_p,
		uint32_t	a_bit
			)
{
uint32_t	l_bound = (a_rc->range >> 11) * *a_p;

	if ( !a_bit )
		{
		a_rc->range = l_bound;
		*a_p	   += (VBK$PROB) ((2048 - *a_p) >> 5);
		}
	else	{
		a_rc->low   += l_bound;
		a_rc->range -= l_bound;
		*a_p	    -= (VBK$PROB) (*a_p >> 5);
		}

	while ( a_rc->range < (1U << 24) )
		{
		a_rc->range <<= 8;
		s_vbk$shiftlow(a_rc);
		}
}

static	void	s_vbk$edirect	(
		VBK$RCENC *	a_rc,
		uint32_t	a_v,
		int		a_n
			)
{
	while ( a_n-- )
		{
		a_rc->range >>= 1;
		a_rc->low   += a_rc->range & (0U - ((a_v >> a_n) & 1));

		if ( a_rc->range < (1U << 24) )
			{
			a_rc->range <<= 8;
			s_vbk$shiftlow(a_rc);
			}
		}
}

static	void	s_vbk$etree	(
		VBK$RCENC *	a_rc,
		VBK$PROB *	a_p,
		int		a_bits,
		uint32_t	a_sym
			)
{
uint32_t	l_m = 1;

	for ( int i = a_bits - 1; i >= 0; i-- )
		{
		uint32_t	l_b = (a_sym >> i) & 1;

		s_vbk$ebit(a_rc, &a_p [l_m], l_b);
		l_m	= (l_m << 1) | l_b;
		}
}

static	void	s_vbk$erevtree	(
		VBK$RCENC *	a_rc,
		VBK$PROB *	a_p,
		int		a_bits,
		uint32_t	a_sym
			)
{
uint32_t	l_m = 1;

	for ( int i = 0; i < a_bits; i++, a_sym >>= 1 )
		{
		uint32_t	l_b = a_sym & 1;

		s_vbk$ebit(a_rc, &a_p [l_m], l_b);
		l_m	= (l_m << 1) | l_b;
		}
}

static	void	s_vbk$elen	(
		VBK$RCENC *	a_rc,
		VBK$LZMLEN *	a_l,
		uint32_t	a_len,
		uint32_t	a_ps
			)
{
uint32_t	l_v = a_len - VBK$K_LZMMIN;

	if ( l_v < 8 )
		{
		s_vbk$ebit(a_rc, &a_l->choice, 0);
		s_vbk$etree(a_rc, a_l->low [a_ps], 3, l_v);
		}
	else if ( l_v < 16 )
		{
		s_vbk$ebit(a_rc, &a_l->choice, 1);
		s_vbk$ebit(a_rc, &a_l->choice2, 0);
		s_vbk$etree(a_rc, a_l->mid [a_ps], 3, l_v - 8);
		}
	else	{
		s_vbk$ebit(a_rc, &a_l->choice, 1);
		s_vbk$ebit(a_rc, &a_l->choice2, 1);
		s_vbk$etree(a_rc, a_l->high, 8, l_v - 16);
		}
}

/*
**  A distance - the value of the specification, the distance less one
*/
static	void	s_vbk$edist	(
		VBK$RCENC *	a_rc,
		VBK$LZMPROBS *	a_p,
		uint32_t	a_dist,
		uint32_t	a_len
			)
{
uint32_t	l_ls = ((a_len - VBK$K_LZMMIN) < 3) ? (a_len - VBK$K_LZMMIN) : 3, l_slot = s_vbk$slot(a_dist);

	s_vbk$etree(a_rc, a_p->posslot [l_ls], 6, l_slot);

	if ( l_slot >= 4 )
		{
		int		l_foot = (int) (l_slot >> 1) - 1;
		uint32_t	l_base = (2 | (l_slot & 1)) << l_foot, l_red = a_dist - l_base;

		if ( l_slot < 14 )
			s_vbk$erevtree(a_rc, a_p->posspec + l_base - l_slot, l_foot, l_red);
		else	{
			s_vbk$edirect(a_rc, l_red >> 4, l_foot - 4);
			s_vbk$erevtree(a_rc, a_p->align, 4, l_red & 15);
			}
		}
}


/*
**  The finder of matches over the record
*/
typedef struct vbk_lzmmf_t
{
	const uint8_t *	src;
	uint32_t	len, next;		/* NEXT: the first position not inserted	*/
	int32_t *	head2;			/* 1 << HBITS: the last position of two octets	*/
	int32_t *	head3;			/* 1 << HBITS: of three				*/
	int32_t *	prev;			/* LEN: the one before, of the same hash	*/
	int32_t *	son;			/* 2 * LEN: the binary trees, NULL - chains	*/
	uint32_t	chain, nice;
	uint32_t	hbits;			/* Of the hash tables: by the length of the record */
} VBK$LZMMF;

static	uint32_t	s_vbk$btwalk	(VBK$LZMMF *a_m, uint32_t a_pos, uint32_t a_best, uint32_t *a_lens, uint32_t *a_dists);

/*
**  The hashes of two and three octets, of HBITS bits: a record of a few
**  KB gets small tables - their setting up costs as much as the record
*/
static	uint32_t	s_vbk$h3	(
	const	uint8_t *	a_p,
		uint32_t	a_bits
			)
{
	return	(((uint32_t) a_p [0] | ((uint32_t) a_p [1] << 8) | ((uint32_t) a_p [2] << 16)) * 2654435761U) >> (32 - a_bits);
}

static	uint32_t	s_vbk$h2	(
	const	uint8_t *	a_p,
		uint32_t	a_bits
			)
{
	return	(((uint32_t) a_p [0] | ((uint32_t) a_p [1] << 8)) * 2246822519U) >> (32 - a_bits);
}

static	void	s_vbk$insertto	(
		VBK$LZMMF *	a_m,
		uint32_t	a_to
			)
{
	for ( ; a_m->next < a_to; a_m->next++ )
		{
		const uint8_t *	l_p = a_m->src + a_m->next;

		if ( (a_m->next + 1) < a_m->len )
			a_m->head2 [s_vbk$h2(l_p, a_m->hbits)] = (int32_t) a_m->next;

		/* The trees: a position skipped is put in its tree all the same, nothing collected */
		if ( a_m->son )
			{
			if ( (a_m->next + 2) < a_m->len )
				s_vbk$btwalk(a_m, a_m->next, VBK$K_LZMMAX + 1, NULL, NULL);

			continue;
			}

		if ( (a_m->next + 2) < a_m->len )
			{
			uint32_t	l_h = s_vbk$h3(l_p, a_m->hbits);

			a_m->prev [a_m->next] = a_m->head3 [l_h];
			a_m->head3 [l_h]      = (int32_t) a_m->next;
			}
		}
}

static	uint32_t	s_vbk$mlen	(
	const	uint8_t *	a_a,
	const	uint8_t *	a_b,
		uint32_t	a_max
			)
{
uint32_t	l_n = 0;

	while ( (l_n < a_max) && (a_a [l_n] == a_b [l_n]) )
		l_n++;

	return	l_n;
}

/*
**  The longest match at <a_pos>, the positions before it inserted, it
**  not; 0 - none of two octets or more.  <a_dist> - the distance less one.
*/
static	uint32_t	s_vbk$find	(
		VBK$LZMMF *	a_m,
		uint32_t	a_pos,
		uint32_t *	a_dist
			)
{
const uint8_t *	l_q = a_m->src + a_pos;
uint32_t	l_avail = a_m->len - a_pos, l_best = 1, l_chain = a_m->chain, l_n;
int32_t		l_c;

	s_vbk$insertto(a_m, a_pos);

	if ( l_avail > VBK$K_LZMMAX )
		l_avail	= VBK$K_LZMMAX;

	if ( l_avail < VBK$K_LZMMIN )
		return	0;

	if ( 0 <= (l_c = a_m->head2 [s_vbk$h2(l_q, a_m->hbits)]) )
		{
		l_best	= s_vbk$mlen(a_m->src + l_c, l_q, l_avail);
		*a_dist	= a_pos - (uint32_t) l_c - 1;
		}

	if ( l_avail >= 3 )
		{
		for ( l_c = a_m->head3 [s_vbk$h3(l_q, a_m->hbits)]; (l_c >= 0) && l_chain-- && (l_best < l_avail); l_c = a_m->prev [l_c] )
			{
			const uint8_t *	l_p = a_m->src + l_c;

			if ( (l_p [l_best] != l_q [l_best]) || (l_p [0] != l_q [0]) )
				continue;

			if ( (l_n = s_vbk$mlen(l_p, l_q, l_avail)) > l_best )
				{
				l_best	= l_n;
				*a_dist	= a_pos - (uint32_t) l_c - 1;

				if ( l_n >= a_m->nice )
					break;
				}
			}
		}

	return	(l_best >= VBK$K_LZMMIN) ? l_best : 0;
}

/*
**  The length of the match at <a_pos> with a repeated distance
*/
static	uint32_t	s_vbk$replen	(
	const	uint8_t *	a_src,
		uint32_t	a_len,
		uint32_t	a_pos,
		uint32_t	a_rep
			)
{
uint32_t	l_avail = a_len - a_pos;

	if ( a_rep >= a_pos )
		return	0;

	return	s_vbk$mlen(a_src + a_pos - a_rep - 1, a_src + a_pos, (l_avail > VBK$K_LZMMAX) ? VBK$K_LZMMAX : l_avail);
}

static	int	s_vbk$changepair	(
		uint32_t	a_small,
		uint32_t	a_big
			)
{
	return	(a_big >> 7) > a_small;
}


/*
**  The state of the writer: the range coder, the model, the octets, where
**  it is, the state and the four repeated distances
*/
typedef struct vbk_lzmenc_t
{
	VBK$RCENC	rc;
	VBK$LZMPROBS *	p;
	const uint8_t *	src;
	uint32_t	len, pos, state;
	uint32_t	reps [4];
} VBK$LZMENC;

enum	{					/* What is written at a position		*/
	VBK$K_LZMLIT = 0,
	VBK$K_LZMMATCH,
	VBK$K_LZMREP,				/* ... DIST is the index of the rep, 0 .. 3	*/
	VBK$K_LZMSHORT				/* A short rep: one octet at rep0		*/
	};

/*
**  Write one decision at the position of the writer and move on
*/
static	void	s_vbk$emit	(
		VBK$LZMENC *	a_e,
		int		a_kind,
		uint32_t	a_len,
		uint32_t	a_dist
			)
{
VBK$RCENC *	l_rc = &a_e->rc;
VBK$LZMPROBS *	l_p = a_e->p;
uint32_t	l_ps = a_e->pos & (VBK$K_LZMPOS - 1), l_st = a_e->state;

	switch ( a_kind )
		{
		case	VBK$K_LZMLIT:
			{
			VBK$PROB *	l_lit = l_p->literal + 0x300 * ((a_e->pos ? a_e->src [a_e->pos - 1] : 0) >> (8 - VBK$K_LZMLC));
			uint32_t	l_sym = 1, l_byte = a_e->src [a_e->pos];

			s_vbk$ebit(l_rc, &l_p->ismatch [l_st][l_ps], 0);

			if ( l_st >= 7 )
				{
				/* After a match: the octet at rep0 guides the bits, while they agree */
				uint32_t	l_mb = a_e->src [a_e->pos - a_e->reps [0] - 1];
				int		l_same = 1;

				for ( int i = 7; i >= 0; i-- )
					{
					uint32_t	l_b = (l_byte >> i) & 1;

					if ( l_same )
						{
						uint32_t	l_mbit = (l_mb >> i) & 1;

						s_vbk$ebit(l_rc, &l_lit [0x100 + (l_mbit << 8) + l_sym], l_b);
						l_same	= (l_mbit == l_b);
						}
					else	s_vbk$ebit(l_rc, &l_lit [l_sym], l_b);

					l_sym	= (l_sym << 1) | l_b;
					}
				}
			else	s_vbk$etree(l_rc, l_lit, 8, l_byte);

			a_e->state = s_vbk$litstate(l_st);
			a_len	   = 1;
			break;
			}

		case	VBK$K_LZMMATCH:
			s_vbk$ebit(l_rc, &l_p->ismatch [l_st][l_ps], 1);
			s_vbk$ebit(l_rc, &l_p->isrep [l_st], 0);
			s_vbk$elen(l_rc, &l_p->len, a_len, l_ps);
			s_vbk$edist(l_rc, l_p, a_dist, a_len);
			a_e->reps [3] = a_e->reps [2];
			a_e->reps [2] = a_e->reps [1];
			a_e->reps [1] = a_e->reps [0];
			a_e->reps [0] = a_dist;
			a_e->state    = (l_st < 7) ? 7 : 10;
			break;

		case	VBK$K_LZMREP:
			{
			uint32_t	l_d = a_e->reps [a_dist];

			s_vbk$ebit(l_rc, &l_p->ismatch [l_st][l_ps], 1);
			s_vbk$ebit(l_rc, &l_p->isrep [l_st], 1);

			if ( !a_dist )
				{
				s_vbk$ebit(l_rc, &l_p->isrepg0 [l_st], 0);
				s_vbk$ebit(l_rc, &l_p->isrep0long [l_st][l_ps], 1);
				}
			else	{
				s_vbk$ebit(l_rc, &l_p->isrepg0 [l_st], 1);

				if ( a_dist == 1 )
					s_vbk$ebit(l_rc, &l_p->isrepg1 [l_st], 0);
				else	{
					s_vbk$ebit(l_rc, &l_p->isrepg1 [l_st], 1);
					s_vbk$ebit(l_rc, &l_p->isrepg2 [l_st], a_dist == 3);
					}
				}

			for ( uint32_t k = a_dist; k; k-- )
				a_e->reps [k] = a_e->reps [k - 1];

			a_e->reps [0] = l_d;
			s_vbk$elen(l_rc, &l_p->replen, a_len, l_ps);
			a_e->state    = (l_st < 7) ? 8 : 11;
			break;
			}

		default:
			s_vbk$ebit(l_rc, &l_p->ismatch [l_st][l_ps], 1);
			s_vbk$ebit(l_rc, &l_p->isrep [l_st], 1);
			s_vbk$ebit(l_rc, &l_p->isrepg0 [l_st], 0);
			s_vbk$ebit(l_rc, &l_p->isrep0long [l_st][l_ps], 0);
			a_e->state = (l_st < 7) ? 9 : 11;
			a_len	   = 1;
		}

	a_e->pos += a_len;
}


/*
**  One step of the "fast" parse of the reference encoder (/LEVEL=6, 7):
**  a repeated match good enough, else the longest match unless it is
**  short and far, else a short rep or a literal; a match put off when
**  the next position has a better one
*/
static	void	s_vbk$fast	(
		VBK$LZMENC *	a_e,
		VBK$LZMMF *	a_m,
		uint32_t *	a_cpos,
		uint32_t *	a_clen,
		uint32_t *	a_cdist
			)
{
const uint8_t *	l_src = a_e->src;
uint32_t	l_pos = a_e->pos, l_len = a_e->len, l_avail = l_len - l_pos;
uint32_t	l_replen = 0, l_repidx = 0, l_mlen = 0, l_mdist = 0;
int		l_kind = VBK$K_LZMLIT;

	if ( l_avail >= VBK$K_LZMMIN )
		{
		for ( uint32_t i = 0; i < 4; i++ )
			{
			uint32_t	l_n = s_vbk$replen(l_src, l_len, l_pos, a_e->reps [i]);

			if ( l_n > l_replen )
				l_replen = l_n, l_repidx = i;
			}

		if ( l_replen >= a_m->nice )
			l_kind	= VBK$K_LZMREP;
		else	{
			if ( *a_cpos == l_pos )
				l_mlen = *a_clen, l_mdist = *a_cdist;
			else	l_mlen = s_vbk$find(a_m, l_pos, &l_mdist);

			if ( (l_mlen == 2) && (l_mdist >= 0x80) )
				l_mlen	= 0;

			if ( l_mlen >= a_m->nice )
				l_kind	= VBK$K_LZMMATCH;
			else if ( (l_replen >= 2) && (((l_replen + 1) >= l_mlen) || (((l_replen + 2) >= l_mlen) && (l_mdist >= (1U << 9)))
					|| (((l_replen + 3) >= l_mlen) && (l_mdist >= (1U << 15)))) )
				l_kind	= VBK$K_LZMREP;
			else if ( l_mlen >= VBK$K_LZMMIN )
				{
				/* One octet ahead: a better match there makes this one a literal */
				uint32_t	l_nlen = 0, l_ndist = 0, l_lim;

				l_kind	= VBK$K_LZMMATCH;

				if ( (l_pos + 1) < l_len )
					{
					l_nlen	 = s_vbk$find(a_m, l_pos + 1, &l_ndist);
					*a_cpos	 = l_pos + 1;
					*a_clen	 = l_nlen;
					*a_cdist = l_ndist;
					}

				if ( (l_nlen >= 2) && (((l_nlen >= l_mlen) && (l_ndist < l_mdist))
					|| ((l_nlen == (l_mlen + 1)) && !s_vbk$changepair(l_mdist, l_ndist)) || (l_nlen > (l_mlen + 1))
					|| (((l_nlen + 1) >= l_mlen) && (l_mlen >= 3) && s_vbk$changepair(l_ndist, l_mdist))) )
					l_kind	= VBK$K_LZMLIT;

				l_lim	= (l_mlen > 3) ? (l_mlen - 1) : 2;

				for ( uint32_t i = 0; (l_kind != VBK$K_LZMLIT) && (i < 4); i++ )
					if ( ((l_pos + 1) < l_len) && (s_vbk$replen(l_src, l_len, l_pos + 1, a_e->reps [i]) >= l_lim) )
						l_kind	= VBK$K_LZMLIT;
				}
			}
		}

	/* A literal where the octet at rep0 is the same: a short rep */
	if ( (l_kind == VBK$K_LZMLIT) && (a_e->reps [0] < l_pos) && (l_src [l_pos] == l_src [l_pos - a_e->reps [0] - 1]) )
		l_kind	= VBK$K_LZMSHORT;

	if ( l_kind == VBK$K_LZMMATCH )
		s_vbk$emit(a_e, l_kind, l_mlen, l_mdist);
	else if ( l_kind == VBK$K_LZMREP )
		s_vbk$emit(a_e, l_kind, l_replen, l_repidx);
	else	s_vbk$emit(a_e, l_kind, 1, 0);
}


/*
**  The optimal parse (/LEVEL=8, 9): the prices of the model in 1/16 of a
**  bit, a window of positions ahead and the cheapest way through it
*/
#define	VBK$K_LZMOPTS	2048			/* Positions of the window of the parse		*/
#define	VBK$K_LZMINF	0x7FFFFFFFU
#define	VBK$K_LZMMAXM	(VBK$K_LZMMAX + 2)	/* Matches of distinct lengths at a position	*/

typedef struct vbk_lzmopt_t
{
	uint32_t	price;			/* Of the cheapest way here			*/
	uint32_t	prev;			/* The position it comes from			*/
	uint32_t	dist;			/* MATCH: the distance; REP: its index		*/
	uint16_t	len;
	uint8_t		kind;
	uint8_t		state;			/* The state and the reps here, once it is final */
	uint32_t	reps [4];
} VBK$LZMOPT;

typedef struct vbk_lzmprice_t
{
	uint16_t	bit [128];		/* The price of a 0 of probability (p >> 4)	*/
	uint32_t	len [2][VBK$K_LZMPOS][VBK$K_LZMMAX + 1];	/* Match, rep: length, posState	*/
	uint32_t	slot [4][64];		/* A position slot, by the state of the length	*/
	uint32_t	full [4][128];		/* A whole distance below 128			*/
	uint32_t	align [16];		/* The four low bits of a far distance		*/
	uint32_t	next;			/* The position from which they are made again	*/
} VBK$LZMPRICE;

#define	VBK$K_LZMREFRESH	512		/* The prices of lengths and distances: so often */

/*
**  The price of a bit by the integer method of the reference encoder:
**  -log2(p / 2048) in 1/16 of a bit, from the 128 steps of p
*/
static	void	s_vbk$prices	(
		VBK$LZMPRICE *	a_pr
			)
{
	for ( uint32_t i = 8; i < 2048; i += 16 )
		{
		uint32_t	l_w = i, l_bits = 0;

		for ( int j = 0; j < 4; j++ )
			{
			l_w	= l_w * l_w;
			l_bits	<<= 1;

			while ( l_w >= (1U << 16) )
				{
				l_w	>>= 1;
				l_bits++;
				}
			}

		a_pr->bit [i >> 4] = (uint16_t) ((11U << 4) - 15 - l_bits);
		}
}

static	uint32_t	s_vbk$pbit	(
	const	VBK$LZMPRICE *	a_pr,
		VBK$PROB	a_p,
		uint32_t	a_bit
			)
{
	return	a_pr->bit [(a_bit ? (2048U - a_p) : a_p) >> 4];
}

static	uint32_t	s_vbk$ptree	(
	const	VBK$LZMPRICE *	a_pr,
	const	VBK$PROB *	a_p,
		int		a_bits,
		uint32_t	a_sym
			)
{
uint32_t	l_m = 1, l_price = 0;

	for ( int i = a_bits - 1; i >= 0; i-- )
		{
		uint32_t	l_b = (a_sym >> i) & 1;

		l_price	+= s_vbk$pbit(a_pr, a_p [l_m], l_b);
		l_m	= (l_m << 1) | l_b;
		}

	return	l_price;
}

static	uint32_t	s_vbk$prevtree	(
	const	VBK$LZMPRICE *	a_pr,
	const	VBK$PROB *	a_p,
		int		a_bits,
		uint32_t	a_sym
			)
{
uint32_t	l_m = 1, l_price = 0;

	for ( int i = 0; i < a_bits; i++, a_sym >>= 1 )
		{
		uint32_t	l_b = a_sym & 1;

		l_price	+= s_vbk$pbit(a_pr, a_p [l_m], l_b);
		l_m	= (l_m << 1) | l_b;
		}

	return	l_price;
}

/*
**  The prices of every length, for the matches and for the reps
*/
static	void	s_vbk$lenprices	(
		VBK$LZMPRICE *	a_pr,
	const	VBK$LZMPROBS *	a_p
			)
{
	for ( int k = 0; k < 2; k++ )
		{
		const VBK$LZMLEN *	l_l = k ? &a_p->replen : &a_p->len;
		uint32_t		l_c0 = s_vbk$pbit(a_pr, l_l->choice, 0), l_c1 = s_vbk$pbit(a_pr, l_l->choice, 1);
		uint32_t		l_c20 = s_vbk$pbit(a_pr, l_l->choice2, 0), l_c21 = s_vbk$pbit(a_pr, l_l->choice2, 1);

		for ( uint32_t l_ps = 0; l_ps < VBK$K_LZMPOS; l_ps++ )
			for ( uint32_t l_n = VBK$K_LZMMIN; l_n <= VBK$K_LZMMAX; l_n++ )
				{
				uint32_t	l_v = l_n - VBK$K_LZMMIN;

				a_pr->len [k][l_ps][l_n] = (l_v < 8) ? (l_c0 + s_vbk$ptree(a_pr, l_l->low [l_ps], 3, l_v))
							 : (l_v < 16) ? (l_c1 + l_c20 + s_vbk$ptree(a_pr, l_l->mid [l_ps], 3, l_v - 8))
							 : (l_c1 + l_c21 + s_vbk$ptree(a_pr, l_l->high, 8, l_v - 16));
				}
		}
}

/*
**  The prices of the distances, made with those of the lengths at the
**  start of a window: the slots, every distance below 128, the low bits
*/
static	void	s_vbk$distprices	(
		VBK$LZMPRICE *	a_pr,
	const	VBK$LZMPROBS *	a_p
			)
{
	for ( uint32_t i = 0; i < 16; i++ )
		a_pr->align [i] = s_vbk$prevtree(a_pr, a_p->align, 4, i);

	for ( uint32_t l_ls = 0; l_ls < 4; l_ls++ )
		{
		for ( uint32_t l_slot = 0; l_slot < 64; l_slot++ )
			a_pr->slot [l_ls][l_slot] = s_vbk$ptree(a_pr, a_p->posslot [l_ls], 6, l_slot);

		for ( uint32_t l_d = 0; l_d < 128; l_d++ )
			{
			uint32_t	l_slot = s_vbk$slot(l_d), l_price = a_pr->slot [l_ls][l_slot];

			if ( l_slot >= 4 )
				{
				int		l_foot = (int) (l_slot >> 1) - 1;
				uint32_t	l_base = (2 | (l_slot & 1)) << l_foot;

				l_price	+= s_vbk$prevtree(a_pr, a_p->posspec + l_base - l_slot, l_foot, l_d - l_base);
				}

			a_pr->full [l_ls][l_d] = l_price;
			}
		}
}

static	uint32_t	s_vbk$pdist	(
	const	VBK$LZMPRICE *	a_pr,
	const	VBK$LZMPROBS *	a_p,
		uint32_t	a_dist,
		uint32_t	a_len
			)
{
uint32_t	l_ls = ((a_len - VBK$K_LZMMIN) < 3) ? (a_len - VBK$K_LZMMIN) : 3, l_slot;
int		l_foot;

	(void) a_p;

	if ( a_dist < 128 )
		return	a_pr->full [l_ls][a_dist];

	/* A far distance: slot 14 and up - direct bits, then the four low bits */
	l_slot	= s_vbk$slot(a_dist);
	l_foot	= (int) (l_slot >> 1) - 1;

	return	a_pr->slot [l_ls][l_slot] + ((uint32_t) (l_foot - 4) << 4) + a_pr->align [a_dist & 15];
}

static	uint32_t	s_vbk$plit	(
	const	VBK$LZMPRICE *	a_pr,
	const	VBK$LZMPROBS *	a_p,
	const	uint8_t *	a_src,
		uint32_t	a_pos,
		uint32_t	a_state,
		uint32_t	a_rep0
			)
{
const VBK$PROB *l_lit = a_p->literal + 0x300 * ((a_pos ? a_src [a_pos - 1] : 0) >> (8 - VBK$K_LZMLC));
uint32_t	l_byte = a_src [a_pos], l_sym = 1, l_price = s_vbk$pbit(a_pr, a_p->ismatch [a_state][a_pos & (VBK$K_LZMPOS - 1)], 0);

	if ( a_state < 7 )
		return	l_price + s_vbk$ptree(a_pr, l_lit, 8, l_byte);

	{
	uint32_t	l_mb = a_src [a_pos - a_rep0 - 1];
	int		l_same = 1;

	for ( int i = 7; i >= 0; i-- )
		{
		uint32_t	l_b = (l_byte >> i) & 1;

		if ( l_same )
			{
			uint32_t	l_mbit = (l_mb >> i) & 1;

			l_price	+= s_vbk$pbit(a_pr, l_lit [0x100 + (l_mbit << 8) + l_sym], l_b);
			l_same	= (l_mbit == l_b);
			}
		else	l_price	+= s_vbk$pbit(a_pr, l_lit [l_sym], l_b);

		l_sym	= (l_sym << 1) | l_b;
		}
	}

	return	l_price;
}

/*
**  The price of choosing the rep of <a_i> (the length apart)
*/
static	uint32_t	s_vbk$prep	(
	const	VBK$LZMPRICE *	a_pr,
	const	VBK$LZMPROBS *	a_p,
		uint32_t	a_i,
		uint32_t	a_state,
		uint32_t	a_ps
			)
{
uint32_t	l_price = s_vbk$pbit(a_pr, a_p->ismatch [a_state][a_ps], 1) + s_vbk$pbit(a_pr, a_p->isrep [a_state], 1);

	if ( !a_i )
		return	l_price + s_vbk$pbit(a_pr, a_p->isrepg0 [a_state], 0) + s_vbk$pbit(a_pr, a_p->isrep0long [a_state][a_ps], 1);

	l_price	+= s_vbk$pbit(a_pr, a_p->isrepg0 [a_state], 1);

	if ( a_i == 1 )
		return	l_price + s_vbk$pbit(a_pr, a_p->isrepg1 [a_state], 0);

	return	l_price + s_vbk$pbit(a_pr, a_p->isrepg1 [a_state], 1) + s_vbk$pbit(a_pr, a_p->isrepg2 [a_state], a_i == 3);
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The binary tree of the match finder of the LZMA SDK (BT): the
**	positions of a hash of three octets kept in a tree ordered by what
**	follows them, so that a walk from the root finds the longest
**	matches, nearest first, and puts the position in the tree on its
**	way - the walk is the insertion.  A match as long as the limit
**	(NICE, or what is left) takes over its node, and the walk ends.
**
**  FORMAL PARAMETERS:
**
**	a_m		The finder: SON, two links a position
**	a_pos		The position, which becomes the root of its tree
**	a_best		Matches longer than this are collected
**	a_lens, a_dists	Receive them, longer and longer; NULL - none wanted
**
**  RETURN VALUE:
**	The count of matches collected.
**--
*/
static	uint32_t	s_vbk$btwalk	(
		VBK$LZMMF *	a_m,
		uint32_t	a_pos,
		uint32_t	a_best,
		uint32_t *	a_lens,
		uint32_t *	a_dists
			)
{
const uint8_t *	l_cur = a_m->src + a_pos;
uint32_t	l_h = s_vbk$h3(l_cur, a_m->hbits), l_limit = a_m->len - a_pos, l_len0 = 0, l_len1 = 0, l_cnt = 0, l_cut = a_m->chain;
int32_t *	l_ptr0 = a_m->son + 2 * (size_t) a_pos + 1, *l_ptr1 = a_m->son + 2 * (size_t) a_pos;
int32_t		l_match = a_m->head3 [l_h];

	a_m->head3 [l_h] = (int32_t) a_pos;

	if ( l_limit > a_m->nice )
		l_limit	= a_m->nice;

	for ( ;; )
		{
		int32_t *	l_pair;
		const uint8_t *	l_pb;
		uint32_t	l_len;

		if ( (l_match < 0) || !l_cut-- )
			{
			*l_ptr0 = *l_ptr1 = -1;

			return	l_cnt;
			}

		l_pair	= a_m->son + 2 * (size_t) l_match;
		l_pb	= a_m->src + l_match;
		l_len	= (l_len0 < l_len1) ? l_len0 : l_len1;

		if ( l_pb [l_len] == l_cur [l_len] )
			{
			while ( (++l_len < l_limit) && (l_pb [l_len] == l_cur [l_len]) )
				;

			if ( a_lens && (l_len > a_best) )
				{
				a_best		 = l_len;
				a_lens [l_cnt]	 = l_len;
				a_dists [l_cnt++] = a_pos - (uint32_t) l_match - 1;
				}

			if ( l_len >= l_limit )
				{
				/* As long as can be: the node is taken over */
				*l_ptr1	= l_pair [0];
				*l_ptr0	= l_pair [1];

				return	l_cnt;
				}
			}

		if ( l_pb [l_len] < l_cur [l_len] )
			{
			*l_ptr1	= l_match;
			l_ptr1	= l_pair + 1;
			l_match	= *l_ptr1;
			l_len1	= l_len;
			}
		else	{
			*l_ptr0	= l_match;
			l_ptr0	= l_pair;
			l_match	= *l_ptr0;
			l_len0	= l_len;
			}
		}
}


/*
**  Every match at <a_pos> worth having: one for each length that grows,
**  with the nearest distance that gives it; the count of them
*/
static	uint32_t	s_vbk$findall	(
		VBK$LZMMF *	a_m,
		uint32_t	a_pos,
		uint32_t *	a_lens,
		uint32_t *	a_dists
			)
{
const uint8_t *	l_q = a_m->src + a_pos;
uint32_t	l_avail = a_m->len - a_pos, l_best = 1, l_chain = a_m->chain, l_n, l_cnt = 0;
int32_t		l_c;

	s_vbk$insertto(a_m, a_pos);

	if ( l_avail > VBK$K_LZMMAX )
		l_avail	= VBK$K_LZMMAX;

	if ( l_avail < VBK$K_LZMMIN )
		return	0;

	if ( 0 <= (l_c = a_m->head2 [s_vbk$h2(l_q, a_m->hbits)]) )
		{
		l_best	= s_vbk$mlen(a_m->src + l_c, l_q, l_avail);
		a_lens [l_cnt]	 = l_best;
		a_dists [l_cnt++] = a_pos - (uint32_t) l_c - 1;
		}

	/* The trees: the walk collects and inserts; the position is done, its pair noted too */
	if ( a_m->son )
		{
		a_m->head2 [s_vbk$h2(l_q, a_m->hbits)] = (int32_t) a_pos;

		if ( (a_pos + 2) < a_m->len )
			l_cnt += s_vbk$btwalk(a_m, a_pos, l_cnt ? a_lens [0] : 1, a_lens + l_cnt, a_dists + l_cnt);

		a_m->next = a_pos + 1;
		}
	else if ( l_avail >= 3 )
		{
		for ( l_c = a_m->head3 [s_vbk$h3(l_q, a_m->hbits)]; (l_c >= 0) && l_chain-- && (l_best < l_avail); l_c = a_m->prev [l_c] )
			{
			const uint8_t *	l_p = a_m->src + l_c;

			if ( (l_p [l_best] != l_q [l_best]) || (l_p [0] != l_q [0]) )
				continue;

			if ( (l_n = s_vbk$mlen(l_p, l_q, l_avail)) > l_best )
				{
				l_best	= l_n;
				a_lens [l_cnt]	 = l_n;
				a_dists [l_cnt++] = a_pos - (uint32_t) l_c - 1;

				if ( l_n >= a_m->nice )
					break;
				}
			}
		}

	/* The match of two from the table of pairs may be no longer than one */
	if ( l_cnt && (a_lens [0] < VBK$K_LZMMIN) )
		{
		memmove(a_lens, a_lens + 1, (l_cnt - 1) * sizeof(uint32_t));
		memmove(a_dists, a_dists + 1, (l_cnt - 1) * sizeof(uint32_t));
		l_cnt--;
		}

	return	l_cnt;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	One window of the optimal parse: from the position of the writer,
**	the cheapest way - by the prices of the model as it is now - over
**	literals, short reps, repeated matches and every length of every
**	match, up to where the longest of them ends; then the decisions of
**	that way written.  A match or a rep at least NICE long is taken at
**	once, as the reference encoder does.
**
**  FORMAL PARAMETERS:
**
**	a_e		The writer
**	a_m		The finder of matches
**	a_opt		VBK$K_LZMOPTS + VBK$K_LZMMAX + 1 nodes
**	a_pr		The prices: of a bit filled in; of the lengths made here
**	a_lens, a_dists	Room for VBK$K_LZMMAXM matches
**
**  RETURN VALUE:
**	None.
**--
*/
static	void	s_vbk$optimal	(
		VBK$LZMENC *	a_e,
		VBK$LZMMF *	a_m,
		VBK$LZMOPT *	a_opt,
		VBK$LZMPRICE *	a_pr,
		uint32_t *	a_lens,
		uint32_t *	a_dists
			)
{
const uint8_t *	l_src = a_e->src;
VBK$LZMPROBS *	l_p = a_e->p;
uint32_t	l_pos = a_e->pos, l_len = a_e->len, l_end, l_nm, l_cur;
uint32_t	l_path [VBK$K_LZMOPTS + VBK$K_LZMMAX + 2], l_npath = 0;
int		l_stop = 0;			/* A long match was found: the window closes	*/

	if ( (l_len - l_pos) < VBK$K_LZMMIN )
		{
		s_vbk$emit(a_e, VBK$K_LZMLIT, 1, 0);
		return;
		}

	/* What is at the start: a match or a rep long enough is taken at once */
	{
	uint32_t	l_rl = 0, l_ri = 0;

	for ( uint32_t i = 0; i < 4; i++ )
		{
		uint32_t	l_n = s_vbk$replen(l_src, l_len, l_pos, a_e->reps [i]);

		if ( l_n > l_rl )
			l_rl = l_n, l_ri = i;
		}

	if ( l_rl >= a_m->nice )
		{
		s_vbk$emit(a_e, VBK$K_LZMREP, l_rl, l_ri);
		return;
		}

	l_nm	= s_vbk$findall(a_m, l_pos, a_lens, a_dists);

	if ( l_nm && (a_lens [l_nm - 1] >= a_m->nice) )
		{
		s_vbk$emit(a_e, VBK$K_LZMMATCH, a_lens [l_nm - 1], a_dists [l_nm - 1]);
		return;
		}

	if ( !l_nm && (l_rl < VBK$K_LZMMIN) )
		{
		/* Nothing to choose from: a short rep where it fits, else a literal */
		if ( (a_e->reps [0] < l_pos) && (l_src [l_pos] == l_src [l_pos - a_e->reps [0] - 1])
			&& ((s_vbk$prep(a_pr, l_p, 0, a_e->state, l_pos & (VBK$K_LZMPOS - 1)) - s_vbk$pbit(a_pr, l_p->isrep0long [a_e->state][l_pos & (VBK$K_LZMPOS - 1)], 1)
				+ s_vbk$pbit(a_pr, l_p->isrep0long [a_e->state][l_pos & (VBK$K_LZMPOS - 1)], 0))
			    < s_vbk$plit(a_pr, l_p, l_src, l_pos, a_e->state, a_e->reps [0])) )
			s_vbk$emit(a_e, VBK$K_LZMSHORT, 1, 0);
		else	s_vbk$emit(a_e, VBK$K_LZMLIT, 1, 0);

		return;
		}
	}

	/* The tables of the lengths and distances follow the model every VBK$K_LZMREFRESH octets, not every window */
	if ( l_pos >= a_pr->next )
		{
		s_vbk$lenprices(a_pr, l_p);
		s_vbk$distprices(a_pr, l_p);
		a_pr->next = l_pos + VBK$K_LZMREFRESH;
		}

	a_opt [0].price	= 0;
	a_opt [0].state	= (uint8_t) a_e->state;
	memcpy(a_opt [0].reps, a_e->reps, sizeof(a_e->reps));
	l_end	= 0;

	/* A node is relaxed when a cheaper way to it is found; the window grows with what is reachable */
#define	$LZM_RELAX(t, pr, k, ln, ds)	do { uint32_t l_t = (t), l_pr = (pr);						\
		while ( l_end < l_t ) a_opt [++l_end].price = VBK$K_LZMINF;						\
		if ( l_pr < a_opt [l_t].price ) { a_opt [l_t].price = l_pr; a_opt [l_t].prev = l_cur; a_opt [l_t].kind = (uint8_t) (k);	\
			a_opt [l_t].len = (uint16_t) (ln); a_opt [l_t].dist = (ds); } } while ( 0 )

	for ( l_cur = 0; ; l_cur++ )
		{
		VBK$LZMOPT *	l_o = &a_opt [l_cur];
		uint32_t	l_at = l_pos + l_cur, l_ps = l_at & (VBK$K_LZMPOS - 1), l_st, l_avail, l_base;
		uint32_t	l_maxreach;

		/* The node is final now: its state and reps from where its cheapest way comes */
		if ( l_cur )
			{
			const VBK$LZMOPT *	l_f = &a_opt [l_o->prev];

			memcpy(l_o->reps, l_f->reps, sizeof(l_o->reps));

			switch ( l_o->kind )
				{
				case	VBK$K_LZMLIT:
					l_o->state = (uint8_t) s_vbk$litstate(l_f->state);
					break;

				case	VBK$K_LZMSHORT:
					l_o->state = (l_f->state < 7) ? 9 : 11;
					break;

				case	VBK$K_LZMMATCH:
					l_o->reps [3] = l_f->reps [2];
					l_o->reps [2] = l_f->reps [1];
					l_o->reps [1] = l_f->reps [0];
					l_o->reps [0] = l_o->dist;
					l_o->state    = (l_f->state < 7) ? 7 : 10;
					break;

				default:
					{
					uint32_t	l_d = l_f->reps [l_o->dist];

					for ( uint32_t k = l_o->dist; k; k-- )
						l_o->reps [k] = l_o->reps [k - 1];

					l_o->reps [0] = l_d;
					l_o->state    = (l_f->state < 7) ? 8 : 11;
					}
				}
			}

		if ( (l_cur == l_end) && l_cur )
			break;

		if ( l_at >= l_len )
			break;

		l_st	= l_o->state;
		l_base	= l_o->price;
		l_avail	= l_len - l_at;

		/* Past the window, or after a match long enough, no more is reached for: the ways already open are finished */
		l_maxreach = (!l_stop && (l_cur < (VBK$K_LZMOPTS - VBK$K_LZMMAX))) ? (l_cur + VBK$K_LZMMAX) : l_end;

		/* A literal; a short rep */
		$LZM_RELAX(l_cur + 1, l_base + s_vbk$plit(a_pr, l_p, l_src, l_at, l_st, l_o->reps [0]), VBK$K_LZMLIT, 1, 0);

		if ( (l_o->reps [0] < l_at) && (l_src [l_at] == l_src [l_at - l_o->reps [0] - 1]) )
			$LZM_RELAX(l_cur + 1, l_base + s_vbk$pbit(a_pr, l_p->ismatch [l_st][l_ps], 1) + s_vbk$pbit(a_pr, l_p->isrep [l_st], 1)
				+ s_vbk$pbit(a_pr, l_p->isrepg0 [l_st], 0) + s_vbk$pbit(a_pr, l_p->isrep0long [l_st][l_ps], 0), VBK$K_LZMSHORT, 1, 0);

		if ( l_avail < VBK$K_LZMMIN )
			continue;

		/* The repeated matches, every length of them */
		for ( uint32_t i = 0; i < 4; i++ )
			{
			uint32_t	l_rl = s_vbk$replen(l_src, l_len, l_at, l_o->reps [i]), l_rp;

			if ( l_rl < VBK$K_LZMMIN )
				continue;

			l_rp	= l_base + s_vbk$prep(a_pr, l_p, i, l_st, l_ps);

			for ( uint32_t l_n = VBK$K_LZMMIN; (l_n <= l_rl) && ((l_cur + l_n) <= l_maxreach); l_n++ )
				$LZM_RELAX(l_cur + l_n, l_rp + a_pr->len [1][l_ps][l_n], VBK$K_LZMREP, l_n, i);
			}

		/* The matches, every length of each up to the next one's */
		if ( l_cur )
			l_nm	= s_vbk$findall(a_m, l_at, a_lens, a_dists);

		/* A match long enough here: taken whole, and the window ends with it (as the reference encoder does) - once */
		if ( !l_stop && l_nm && (a_lens [l_nm - 1] >= a_m->nice) )
			{
			uint32_t	l_n = a_lens [l_nm - 1];

			if ( (l_cur + l_n) > l_end )
				$LZM_RELAX(l_cur + l_n, l_base + s_vbk$pbit(a_pr, l_p->ismatch [l_st][l_ps], 1) + s_vbk$pbit(a_pr, l_p->isrep [l_st], 0)
					+ a_pr->len [0][l_ps][l_n] + s_vbk$pdist(a_pr, l_p, a_dists [l_nm - 1], l_n), VBK$K_LZMMATCH, l_n, a_dists [l_nm - 1]);

			l_nm	= 0;
			l_stop	= 1;
			}

		{
		uint32_t	l_mp = l_base + s_vbk$pbit(a_pr, l_p->ismatch [l_st][l_ps], 1) + s_vbk$pbit(a_pr, l_p->isrep [l_st], 0);
		uint32_t	l_from = VBK$K_LZMMIN;

		for ( uint32_t j = 0; j < l_nm; j++ )
			{
			/* The price of the distance depends on the length only by min(len - 2, 3) */
			uint32_t	l_dp [4];

			for ( uint32_t k = 0; k < 4; k++ )
				l_dp [k] = s_vbk$pdist(a_pr, l_p, a_dists [j], VBK$K_LZMMIN + k);

			for ( uint32_t l_n = l_from; (l_n <= a_lens [j]) && ((l_cur + l_n) <= l_maxreach); l_n++ )
				$LZM_RELAX(l_cur + l_n, l_mp + a_pr->len [0][l_ps][l_n] + l_dp [((l_n - VBK$K_LZMMIN) < 3) ? (l_n - VBK$K_LZMMIN) : 3],
					VBK$K_LZMMATCH, l_n, a_dists [j]);

			l_from	= a_lens [j] + 1;
			}
		}
		}

#undef	$LZM_RELAX

	/* The way back from the end, then its decisions written in order */
	for ( uint32_t t = l_cur; t; t = a_opt [t].prev )
		l_path [l_npath++] = t;

	while ( l_npath-- )
		{
		const VBK$LZMOPT *	l_o = &a_opt [l_path [l_npath]];

		s_vbk$emit(a_e, l_o->kind, l_o->len, l_o->dist);
		}
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Compress octets into a raw LZMA1 stream (codec 3).
**
**  FORMAL PARAMETERS:
**
**	a_src, a_len	The octets, 1 MB at most
**	a_dst, a_cap	Where the stream goes, its size
**	a_outlen	Receives the length of the stream
**	a_effort	1 .. VBK$K_LZMLEVELS: 1, 2 the fast parse, 3, 4 the
**			optimal one
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- compressed;
**	STS$K_ERROR	- it does not fit in <a_cap>;
**	STS$K_FATAL	- no memory.
**--
*/
int	vbk$lzm_compress	(
	const	uint8_t *	a_src,
		uint32_t	a_len,
		uint8_t *	a_dst,
		uint32_t	a_cap,
		uint32_t *	a_outlen,
		int		a_effort
			)
{
VBK$LZMENC	l_e = { .rc = { .dst = a_dst, .cap = a_cap, .range = 0xFFFFFFFFU, .cachesize = 1 }, .src = a_src, .len = a_len };
VBK$LZMMF	l_m = { .src = a_src, .len = a_len };
VBK$LZMOPT *	l_opt = NULL;
VBK$LZMPRICE *	l_pr = NULL;
uint32_t *	l_lens = NULL, *l_dists = NULL;
uint32_t	l_cpos = 0xFFFFFFFFU, l_clen = 0, l_cdist = 0;
int		l_optimal;
void *		l_mem;

	a_effort  = (a_effort < 1) ? 1 : (a_effort > VBK$K_LZMLEVELS) ? VBK$K_LZMLEVELS : a_effort;
	l_m.chain = s_effort [a_effort - 1].chain;
	l_m.nice  = s_effort [a_effort - 1].nice;
	l_optimal = (a_effort >= 3);

	/* The hash tables by the length: twice as many entries as octets, 256 .. 65536 */
	for ( l_m.hbits = 8; (l_m.hbits < VBK$K_LZMH3) && ((1U << l_m.hbits) < (2 * a_len)); l_m.hbits++ )
		;

	/* The model, then the tables of positions - aligned for their int32_t */
	if ( !(l_mem = malloc(VBK$LZMPSIZE + ((size_t) 2 * (1U << l_m.hbits) + a_len + 1) * sizeof(int32_t))) )
		return	STS$K_FATAL;

	if ( l_optimal && (!(l_opt = malloc((VBK$K_LZMOPTS + VBK$K_LZMMAX + 2) * sizeof(VBK$LZMOPT))) || !(l_pr = malloc(sizeof(VBK$LZMPRICE)))
		|| !(l_lens = malloc(2 * VBK$K_LZMMAXM * sizeof(uint32_t))) || !(l_m.son = malloc(2 * ((size_t) a_len + 1) * sizeof(int32_t)))) )
		{
		free(l_opt);
		free(l_pr);
		free(l_lens);
		free(l_m.son);
		free(l_mem);

		return	STS$K_FATAL;
		}

	l_e.p	  = (VBK$LZMPROBS *) l_mem;
	l_m.head2 = (int32_t *) ((uint8_t *) l_mem + VBK$LZMPSIZE);
	l_m.head3 = l_m.head2 + (1U << l_m.hbits);
	l_m.prev  = l_m.head3 + (1U << l_m.hbits);
	memset(l_m.head2, 0xFF, (size_t) 2 * (1U << l_m.hbits) * sizeof(int32_t));
	s_vbk$probs(l_e.p);

	if ( l_optimal )
		{
		l_dists	= l_lens + VBK$K_LZMMAXM;
		s_vbk$prices(l_pr);
		l_pr->next = 0;
		}

	while ( (l_e.pos < a_len) && !l_e.rc.over )
		{
		if ( l_optimal )
			s_vbk$optimal(&l_e, &l_m, l_opt, l_pr, l_lens, l_dists);
		else	s_vbk$fast(&l_e, &l_m, &l_cpos, &l_clen, &l_cdist);
		}

	/* The end marker: a match of two at the distance 0xFFFFFFFF; then the range coder flushed */
	if ( !l_e.rc.over )
		{
		uint32_t	l_ps = l_e.pos & (VBK$K_LZMPOS - 1);

		s_vbk$ebit(&l_e.rc, &l_e.p->ismatch [l_e.state][l_ps], 1);
		s_vbk$ebit(&l_e.rc, &l_e.p->isrep [l_e.state], 0);
		s_vbk$elen(&l_e.rc, &l_e.p->len, VBK$K_LZMMIN, l_ps);
		s_vbk$edist(&l_e.rc, l_e.p, VBK$K_LZMEND, VBK$K_LZMMIN);

		for ( int i = 0; i < 5; i++ )
			s_vbk$shiftlow(&l_e.rc);
		}

	free(l_opt);
	free(l_pr);
	free(l_lens);
	free(l_m.son);
	free(l_mem);

	if ( l_e.rc.over )
		return	STS$K_ERROR;

	*a_outlen = l_e.rc.op;

	return	STS$K_SUCCESS;
}


/*
**  The range decoder of the specification
*/
typedef struct vbk_rcdec_t
{
	const uint8_t *	src;
	uint32_t	len, ip;
	uint32_t	range, code;
	int		bad;			/* The input ended, or a corrupted code		*/
} VBK$RCDEC;

static	void	s_vbk$norm	(
		VBK$RCDEC *	a_rc
			)
{
	if ( a_rc->range < (1U << 24) )
		{
		a_rc->range <<= 8;

		if ( a_rc->ip < a_rc->len )
			a_rc->code = (a_rc->code << 8) | a_rc->src [a_rc->ip++];
		else	a_rc->bad = 1, a_rc->code <<= 8;
		}
}

static	uint32_t	s_vbk$dbit	(
		VBK$RCDEC *	a_rc,
		VBK$PROB *	a_p
			)
{
uint32_t	l_bound = (a_rc->range >> 11) * *a_p, l_bit;

	if ( a_rc->code < l_bound )
		{
		a_rc->range = l_bound;
		*a_p	   += (VBK$PROB) ((2048 - *a_p) >> 5);
		l_bit	    = 0;
		}
	else	{
		a_rc->range -= l_bound;
		a_rc->code  -= l_bound;
		*a_p	    -= (VBK$PROB) (*a_p >> 5);
		l_bit	     = 1;
		}

	s_vbk$norm(a_rc);

	return	l_bit;
}

static	uint32_t	s_vbk$ddirect	(
		VBK$RCDEC *	a_rc,
		int		a_n
			)
{
uint32_t	l_res = 0;

	while ( a_n-- )
		{
		uint32_t	l_t;

		a_rc->range >>= 1;
		a_rc->code  -= a_rc->range;
		l_t	     = 0U - (a_rc->code >> 31);
		a_rc->code  += a_rc->range & l_t;

		if ( a_rc->code == a_rc->range )
			a_rc->bad = 1;

		s_vbk$norm(a_rc);
		l_res	= (l_res << 1) + (l_t + 1);
		}

	return	l_res;
}

static	uint32_t	s_vbk$dtree	(
		VBK$RCDEC *	a_rc,
		VBK$PROB *	a_p,
		int		a_bits
			)
{
uint32_t	l_m = 1;

	for ( int i = 0; i < a_bits; i++ )
		l_m	= (l_m << 1) | s_vbk$dbit(a_rc, &a_p [l_m]);

	return	l_m - (1U << a_bits);
}

static	uint32_t	s_vbk$drevtree	(
		VBK$RCDEC *	a_rc,
		VBK$PROB *	a_p,
		int		a_bits
			)
{
uint32_t	l_m = 1, l_sym = 0;

	for ( int i = 0; i < a_bits; i++ )
		{
		uint32_t	l_b = s_vbk$dbit(a_rc, &a_p [l_m]);

		l_m	 = (l_m << 1) | l_b;
		l_sym	|= l_b << i;
		}

	return	l_sym;
}

static	uint32_t	s_vbk$dlen	(
		VBK$RCDEC *	a_rc,
		VBK$LZMLEN *	a_l,
		uint32_t	a_ps
			)
{
	if ( !s_vbk$dbit(a_rc, &a_l->choice) )
		return	VBK$K_LZMMIN + s_vbk$dtree(a_rc, a_l->low [a_ps], 3);

	if ( !s_vbk$dbit(a_rc, &a_l->choice2) )
		return	VBK$K_LZMMIN + 8 + s_vbk$dtree(a_rc, a_l->mid [a_ps], 3);

	return	VBK$K_LZMMIN + 16 + s_vbk$dtree(a_rc, a_l->high, 8);
}

static	uint32_t	s_vbk$ddist	(
		VBK$RCDEC *	a_rc,
		VBK$LZMPROBS *	a_p,
		uint32_t	a_len
			)
{
uint32_t	l_ls = ((a_len - VBK$K_LZMMIN) < 3) ? (a_len - VBK$K_LZMMIN) : 3, l_slot, l_dist;
int		l_foot;

	if ( (l_slot = s_vbk$dtree(a_rc, a_p->posslot [l_ls], 6)) < 4 )
		return	l_slot;

	l_foot	= (int) (l_slot >> 1) - 1;
	l_dist	= (2 | (l_slot & 1)) << l_foot;

	if ( l_slot < 14 )
		return	l_dist + s_vbk$drevtree(a_rc, a_p->posspec + l_dist - l_slot, l_foot);

	l_dist	+= s_vbk$ddirect(a_rc, l_foot - 4) << 4;

	return	l_dist + s_vbk$drevtree(a_rc, a_p->align, 4);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Decompress a raw LZMA1 stream (codec 3): exactly <a_rawlen> octets,
**	then the end marker, out of exactly <a_len> octets.
**
**  FORMAL PARAMETERS:
**
**	a_src, a_len	The stream
**	a_dst		Receives the octets, <a_rawlen> of room
**	a_rawlen	How many it must give
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the octets;
**	STS$K_ERROR	- a bad stream;
**	STS$K_FATAL	- no memory.
**--
*/
int	vbk$lzm_decode	(
	const	uint8_t *	a_src,
		uint32_t	a_len,
		uint8_t *	a_dst,
		uint32_t	a_rawlen,
		uint32_t *	a_got
			)
{
VBK$RCDEC	l_rc = { .src = a_src, .len = a_len, .range = 0xFFFFFFFFU };
VBK$LZMPROBS *	l_p;
uint32_t	l_reps [4] = { 0 }, l_state = 0, l_op = 0, l_len;
uint32_t	l_good = 0;
int		l_status = STS$K_ERROR;

	/* The range coder: a zero, then the code in four octets */
	if ( (a_len < 5) || a_src [0] )
		return	*a_got = l_good, STS$K_ERROR;

	l_rc.code = ((uint32_t) a_src [1] << 24) | ((uint32_t) a_src [2] << 16) | ((uint32_t) a_src [3] << 8) | a_src [4];
	l_rc.ip	  = 5;

	if ( l_rc.code == l_rc.range )
		return	*a_got = l_good, STS$K_ERROR;

	if ( !(l_p = malloc(sizeof(*l_p))) )
		return	*a_got = l_good, STS$K_FATAL;

	s_vbk$probs(l_p);

	while ( !l_rc.bad )
		{
		uint32_t	l_ps = l_op & (VBK$K_LZMPOS - 1);

		/* Every octet before this symbol came of the input as it is: the past end of it is not trusted */
		l_good	= l_op;

		if ( !s_vbk$dbit(&l_rc, &l_p->ismatch [l_state][l_ps]) )
			{
			VBK$PROB *	l_lit = l_p->literal + 0x300 * ((l_op ? a_dst [l_op - 1] : 0) >> (8 - VBK$K_LZMLC));
			uint32_t	l_sym = 1;

			if ( l_op >= a_rawlen )
				break;

			if ( l_state >= 7 )
				{
				uint32_t	l_mb = a_dst [l_op - l_reps [0] - 1];

				do	{
					uint32_t	l_mbit = (l_mb >> 7) & 1, l_b;

					l_mb	<<= 1;
					l_b	= s_vbk$dbit(&l_rc, &l_lit [0x100 + (l_mbit << 8) + l_sym]);
					l_sym	= (l_sym << 1) | l_b;

					if ( l_mbit != l_b )
						break;
					}
				while ( l_sym < 0x100 );
				}

			while ( l_sym < 0x100 )
				l_sym	= (l_sym << 1) | s_vbk$dbit(&l_rc, &l_lit [l_sym]);

			a_dst [l_op++] = (uint8_t) l_sym;
			l_state	= s_vbk$litstate(l_state);
			continue;
			}

		if ( s_vbk$dbit(&l_rc, &l_p->isrep [l_state]) )
			{
			if ( !l_op )
				break;

			if ( !s_vbk$dbit(&l_rc, &l_p->isrepg0 [l_state]) )
				{
				if ( !s_vbk$dbit(&l_rc, &l_p->isrep0long [l_state][l_ps]) )
					{
					/* A short rep: one octet at rep0 */
					if ( l_op >= a_rawlen )
						break;

					l_state	= (l_state < 7) ? 9 : 11;
					a_dst [l_op] = a_dst [l_op - l_reps [0] - 1];
					l_op++;
					continue;
					}
				}
			else	{
				uint32_t	l_d;

				if ( !s_vbk$dbit(&l_rc, &l_p->isrepg1 [l_state]) )
					l_d	= l_reps [1];
				else	{
					if ( !s_vbk$dbit(&l_rc, &l_p->isrepg2 [l_state]) )
						l_d	= l_reps [2];
					else	{
						l_d	   = l_reps [3];
						l_reps [3] = l_reps [2];
						}

					l_reps [2] = l_reps [1];
					}

				l_reps [1] = l_reps [0];
				l_reps [0] = l_d;
				}

			l_len	= s_vbk$dlen(&l_rc, &l_p->replen, l_ps);
			l_state	= (l_state < 7) ? 8 : 11;
			}
		else	{
			l_reps [3] = l_reps [2];
			l_reps [2] = l_reps [1];
			l_reps [1] = l_reps [0];
			l_len	   = s_vbk$dlen(&l_rc, &l_p->len, l_ps);
			l_state	   = (l_state < 7) ? 7 : 10;
			l_reps [0] = s_vbk$ddist(&l_rc, l_p, l_len);

			if ( l_reps [0] == VBK$K_LZMEND )
				{
				/* The end: all the octets out, the code at 0, the input used up */
				if ( !l_rc.bad && (l_op == a_rawlen) && !l_rc.code && (l_rc.ip == l_rc.len) )
					l_status = STS$K_SUCCESS;

				break;
				}
			}

		if ( (l_reps [0] >= l_op) || (l_len > (a_rawlen - l_op)) )
			break;

		for ( ; l_len; l_len--, l_op++ )
			a_dst [l_op] = a_dst [l_op - l_reps [0] - 1];
		}

	free(l_p);

	/* The loop ended without the input running out: what came out of its last symbol is right too */
	if ( (l_status == STS$K_SUCCESS) || !l_rc.bad )
		l_good	= l_op;

	return	*a_got = l_good, l_status;
}

/*
**  The same, when only the whole of it is wanted
*/
int	vbk$lzm_decompress	(
	const	uint8_t *	a_src,
		uint32_t	a_len,
		uint8_t *	a_dst,
		uint32_t	a_rawlen
			)
{
uint32_t	l_got;

	return	vbk$lzm_decode(a_src, a_len, a_dst, a_rawlen, &l_got);
}
