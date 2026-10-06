#define	__MODULE__	"VBKLZM"
#define	__IDENT__	"X01-19"
#define	__REV__		"1.19.0"

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
**		The writer.  Hash chains of three octets and a table of the
**		last position of every two octets; the four repeated
**		distances.  At each position the "fast" choice of the
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

static	const struct { uint16_t chain, nice; } s_effort [VBK$K_LZMLEVELS] = { { 16, 32 }, { 48, 64 }, { 128, 128 }, { 512, 273 } };


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
uint32_t	l_n = 31;

	if ( a_dist < 4 )
		return	a_dist;

	while ( !(a_dist >> l_n) )
		l_n--;

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
	int32_t *	head2;			/* 65536: the last position of two octets	*/
	int32_t *	head3;			/* 1 << VBK$K_LZMH3				*/
	int32_t *	prev;			/* LEN: the one before, of the same hash	*/
	uint32_t	chain, nice;
} VBK$LZMMF;

static	uint32_t	s_vbk$h3	(
	const	uint8_t *	a_p
			)
{
	return	(((uint32_t) a_p [0] | ((uint32_t) a_p [1] << 8) | ((uint32_t) a_p [2] << 16)) * 2654435761U) >> (32 - VBK$K_LZMH3);
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
			a_m->head2 [l_p [0] | (l_p [1] << 8)] = (int32_t) a_m->next;

		if ( (a_m->next + 2) < a_m->len )
			{
			uint32_t	l_h = s_vbk$h3(l_p);

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

	if ( 0 <= (l_c = a_m->head2 [l_q [0] | (l_q [1] << 8)]) )
		{
		l_best	= s_vbk$mlen(a_m->src + l_c, l_q, l_avail);
		*a_dist	= a_pos - (uint32_t) l_c - 1;
		}

	if ( l_avail >= 3 )
		{
		for ( l_c = a_m->head3 [s_vbk$h3(l_q)]; (l_c >= 0) && l_chain-- && (l_best < l_avail); l_c = a_m->prev [l_c] )
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
**	a_effort	1 .. VBK$K_LZMLEVELS
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
VBK$RCENC	l_rc = { .dst = a_dst, .cap = a_cap, .range = 0xFFFFFFFFU, .cachesize = 1 };
VBK$LZMMF	l_m = { .src = a_src, .len = a_len };
VBK$LZMPROBS *	l_p;
uint32_t	l_reps [4] = { 0 }, l_state = 0, l_pos = 0;
uint32_t	l_cpos = 0xFFFFFFFFU, l_clen = 0, l_cdist = 0;
void *		l_mem;

	a_effort  = (a_effort < 1) ? 1 : (a_effort > VBK$K_LZMLEVELS) ? VBK$K_LZMLEVELS : a_effort;
	l_m.chain = s_effort [a_effort - 1].chain;
	l_m.nice  = s_effort [a_effort - 1].nice;

	/* The model, then the tables of positions - aligned for their int32_t */
	if ( !(l_mem = malloc(VBK$LZMPSIZE + ((size_t) 65536 + (1U << VBK$K_LZMH3) + a_len + 1) * sizeof(int32_t))) )
		return	STS$K_FATAL;

	l_p	  = (VBK$LZMPROBS *) l_mem;
	l_m.head2 = (int32_t *) ((uint8_t *) l_mem + VBK$LZMPSIZE);
	l_m.head3 = l_m.head2 + 65536;
	l_m.prev  = l_m.head3 + (1U << VBK$K_LZMH3);
	memset(l_m.head2, 0xFF, ((size_t) 65536 + (1U << VBK$K_LZMH3)) * sizeof(int32_t));
	s_vbk$probs(l_p);

	while ( (l_pos < a_len) && !l_rc.over )
		{
		uint32_t	l_ps = l_pos & (VBK$K_LZMPOS - 1), l_avail = a_len - l_pos;
		uint32_t	l_replen = 0, l_repidx = 0, l_mlen = 0, l_mdist = 0, l_len = 1;
		int		l_kind = 0;		/* 0 literal, 1 match, 2 rep, 3 short rep	*/

		/* The choice of the "fast" parse */
		if ( l_avail >= VBK$K_LZMMIN )
			{
			for ( uint32_t i = 0; i < 4; i++ )
				{
				uint32_t	l_n = s_vbk$replen(a_src, a_len, l_pos, l_reps [i]);

				if ( l_n > l_replen )
					l_replen = l_n, l_repidx = i;
				}

			if ( l_replen >= l_m.nice )
				l_kind	= 2;
			else	{
				if ( l_cpos == l_pos )
					l_mlen = l_clen, l_mdist = l_cdist;
				else	l_mlen = s_vbk$find(&l_m, l_pos, &l_mdist);

				if ( (l_mlen == 2) && (l_mdist >= 0x80) )
					l_mlen	= 0;

				if ( l_mlen >= l_m.nice )
					l_kind	= 1;
				else if ( (l_replen >= 2) && (((l_replen + 1) >= l_mlen) || (((l_replen + 2) >= l_mlen) && (l_mdist >= (1U << 9)))
						|| (((l_replen + 3) >= l_mlen) && (l_mdist >= (1U << 15)))) )
					l_kind	= 2;
				else if ( l_mlen >= VBK$K_LZMMIN )
					{
					/* One octet ahead: a better match there makes this one a literal */
					uint32_t	l_nlen = 0, l_ndist = 0, l_lim;

					l_kind	= 1;

					if ( (l_pos + 1) < a_len )
						{
						l_nlen	= s_vbk$find(&l_m, l_pos + 1, &l_ndist);
						l_cpos	= l_pos + 1;
						l_clen	= l_nlen;
						l_cdist	= l_ndist;
						}

					if ( (l_nlen >= 2) && (((l_nlen >= l_mlen) && (l_ndist < l_mdist))
						|| ((l_nlen == (l_mlen + 1)) && !s_vbk$changepair(l_mdist, l_ndist)) || (l_nlen > (l_mlen + 1))
						|| (((l_nlen + 1) >= l_mlen) && (l_mlen >= 3) && s_vbk$changepair(l_ndist, l_mdist))) )
						l_kind	= 0;

					l_lim	= (l_mlen > 3) ? (l_mlen - 1) : 2;

					for ( uint32_t i = 0; l_kind && (i < 4); i++ )
						if ( ((l_pos + 1) < a_len) && (s_vbk$replen(a_src, a_len, l_pos + 1, l_reps [i]) >= l_lim) )
							l_kind	= 0;
					}
				}
			}

		/* A literal where the octet at rep0 is the same: a short rep */
		if ( !l_kind && (l_reps [0] < l_pos) && (a_src [l_pos] == a_src [l_pos - l_reps [0] - 1]) )
			l_kind	= 3;

		switch ( l_kind )
			{
			case	0:
				{
				VBK$PROB *	l_lit = l_p->literal + 0x300 * ((l_pos ? a_src [l_pos - 1] : 0) >> (8 - VBK$K_LZMLC));
				uint32_t	l_sym = 1, l_byte = a_src [l_pos];

				s_vbk$ebit(&l_rc, &l_p->ismatch [l_state][l_ps], 0);

				if ( l_state >= 7 )
					{
					/* After a match: the octet at rep0 guides the bits, while they agree */
					uint32_t	l_mb = a_src [l_pos - l_reps [0] - 1];
					int		l_same = 1;

					for ( int i = 7; i >= 0; i-- )
						{
						uint32_t	l_b = (l_byte >> i) & 1;

						if ( l_same )
							{
							uint32_t	l_mbit = (l_mb >> i) & 1;

							s_vbk$ebit(&l_rc, &l_lit [0x100 + (l_mbit << 8) + l_sym], l_b);
							l_same	= (l_mbit == l_b);
							}
						else	s_vbk$ebit(&l_rc, &l_lit [l_sym], l_b);

						l_sym	= (l_sym << 1) | l_b;
						}
					}
				else	s_vbk$etree(&l_rc, l_lit, 8, l_byte);

				l_state	= s_vbk$litstate(l_state);
				break;
				}

			case	1:
				s_vbk$ebit(&l_rc, &l_p->ismatch [l_state][l_ps], 1);
				s_vbk$ebit(&l_rc, &l_p->isrep [l_state], 0);
				s_vbk$elen(&l_rc, &l_p->len, l_mlen, l_ps);
				s_vbk$edist(&l_rc, l_p, l_mdist, l_mlen);
				l_reps [3] = l_reps [2];
				l_reps [2] = l_reps [1];
				l_reps [1] = l_reps [0];
				l_reps [0] = l_mdist;
				l_state	= (l_state < 7) ? 7 : 10;
				l_len	= l_mlen;
				break;

			case	2:
				{
				uint32_t	l_d = l_reps [l_repidx];

				s_vbk$ebit(&l_rc, &l_p->ismatch [l_state][l_ps], 1);
				s_vbk$ebit(&l_rc, &l_p->isrep [l_state], 1);

				if ( !l_repidx )
					{
					s_vbk$ebit(&l_rc, &l_p->isrepg0 [l_state], 0);
					s_vbk$ebit(&l_rc, &l_p->isrep0long [l_state][l_ps], 1);
					}
				else	{
					s_vbk$ebit(&l_rc, &l_p->isrepg0 [l_state], 1);

					if ( l_repidx == 1 )
						s_vbk$ebit(&l_rc, &l_p->isrepg1 [l_state], 0);
					else	{
						s_vbk$ebit(&l_rc, &l_p->isrepg1 [l_state], 1);
						s_vbk$ebit(&l_rc, &l_p->isrepg2 [l_state], l_repidx == 3);
						}
					}

				for ( uint32_t k = l_repidx; k; k-- )
					l_reps [k] = l_reps [k - 1];

				l_reps [0] = l_d;
				s_vbk$elen(&l_rc, &l_p->replen, l_replen, l_ps);
				l_state	= (l_state < 7) ? 8 : 11;
				l_len	= l_replen;
				break;
				}

			default:
				s_vbk$ebit(&l_rc, &l_p->ismatch [l_state][l_ps], 1);
				s_vbk$ebit(&l_rc, &l_p->isrep [l_state], 1);
				s_vbk$ebit(&l_rc, &l_p->isrepg0 [l_state], 0);
				s_vbk$ebit(&l_rc, &l_p->isrep0long [l_state][l_ps], 0);
				l_state	= (l_state < 7) ? 9 : 11;
			}

		l_pos	+= l_len;
		}

	/* The end marker: a match of two at the distance 0xFFFFFFFF; then the range coder flushed */
	if ( !l_rc.over )
		{
		uint32_t	l_ps = l_pos & (VBK$K_LZMPOS - 1);

		s_vbk$ebit(&l_rc, &l_p->ismatch [l_state][l_ps], 1);
		s_vbk$ebit(&l_rc, &l_p->isrep [l_state], 0);
		s_vbk$elen(&l_rc, &l_p->len, VBK$K_LZMMIN, l_ps);
		s_vbk$edist(&l_rc, l_p, VBK$K_LZMEND, VBK$K_LZMMIN);

		for ( int i = 0; i < 5; i++ )
			s_vbk$shiftlow(&l_rc);
		}

	free(l_mem);

	if ( l_rc.over )
		return	STS$K_ERROR;

	*a_outlen = l_rc.op;

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
int	vbk$lzm_decompress	(
	const	uint8_t *	a_src,
		uint32_t	a_len,
		uint8_t *	a_dst,
		uint32_t	a_rawlen
			)
{
VBK$RCDEC	l_rc = { .src = a_src, .len = a_len, .range = 0xFFFFFFFFU };
VBK$LZMPROBS *	l_p;
uint32_t	l_reps [4] = { 0 }, l_state = 0, l_op = 0, l_len;
int		l_status = STS$K_ERROR;

	/* The range coder: a zero, then the code in four octets */
	if ( (a_len < 5) || a_src [0] )
		return	STS$K_ERROR;

	l_rc.code = ((uint32_t) a_src [1] << 24) | ((uint32_t) a_src [2] << 16) | ((uint32_t) a_src [3] << 8) | a_src [4];
	l_rc.ip	  = 5;

	if ( l_rc.code == l_rc.range )
		return	STS$K_ERROR;

	if ( !(l_p = malloc(sizeof(*l_p))) )
		return	STS$K_FATAL;

	s_vbk$probs(l_p);

	while ( !l_rc.bad )
		{
		uint32_t	l_ps = l_op & (VBK$K_LZMPOS - 1);

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

	return	l_status;
}
