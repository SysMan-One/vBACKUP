#define	__MODULE__	"VBKZPL"
#define	__IDENT__	"X01-20"
#define	__REV__		"1.20.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkzpl.c
**
**  ABSTRACT:	/DATA_FORMAT=COMPRESSED on several cores: the records of a
**		save go through a ring in their order, the data among them
**		is compressed by worker threads, and the records leave the
**		ring - into the writer - in the order they came in.
**
**  DESCRIPTION: On a small ARM core the compressor makes some 35 MB/s,
**		less than a disk takes.  So the save hands each record over
**		instead of writing it:
**
**		    DATA	the octets of a file at an offset; a worker
**				packs them (VBK$LZ4_PACK) into a DATAZ, or
**				leaves them for a DATA record;
**		    RECORD	any other record, copied as it is (FILE, FEND);
**		    CALL	a routine to run when everything before it is
**				written - a catalog entry wants to know where
**				its FILE record went.
**
**		Only the caller's thread writes: it takes the slots off the
**		head of the ring as they are done, so the saveset is the one
**		a save without workers makes, octet for octet.  The CRC of a
**		file is the caller's business, on the raw octets, as before.
**
**		VBACKUP_ZTHREADS=n sets the workers (default: the cores, at
**		most 8); 1 or less - no ring, the save compresses itself.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  4-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-20		 6-OCT-2026	RRL
**		More slots - 16 a thread - their buffers by need, and a budget of
**		octets in the ring as before (2n + 2 records of 1 MB): a small file takes
**		three or four slots, and with 2n + 2 of them the slow levels compressed
**		few records at once.
**
**	X01-19		 6-OCT-2026	RRL
**		The codec of /LEVEL (VBK$DATA_PACK), every record checked by the worker
**		before it is written; ZCHECK when one did not come back.
**
**	X01-04		 4-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<unistd.h>
#include	<pthread.h>

#include	"vbkdef.h"

#define	VBK$K_ZPMAXTHR	8			/* Workers at most				*/
#define	VBK$K_ZPMAXSLOT	128			/* Slots of the ring at most			*/

enum	{					/* A slot					*/
	VBK$K_ZS_FREE = 0,
	VBK$K_ZS_READY,				/* DATA, to be packed				*/
	VBK$K_ZS_BUSY,				/* ... a worker packs it			*/
	VBK$K_ZS_DONE				/* To be written				*/
	};

enum	{
	VBK$K_ZK_DATA = 0,
	VBK$K_ZK_RECORD,
	VBK$K_ZK_CALL
	};

typedef struct vbk_zslot_t
{
	int		state, kind;
	uint32_t	fileno;
	uint64_t	off;
	uint8_t *	buf;			/* The octets, or the record body		*/
	uint32_t	len, bufsz, zbufsz, checksz;
	uint8_t *	zbuf;			/* DATAZ body: header and packed octets		*/
	uint8_t *	check;			/* The record decompressed again, the check	*/
	uint32_t	zlen;			/* 0 - stored as DATA				*/
	int		zcheck;			/* It did not come back the same: ZCHECK	*/
	uint16_t	type;			/* RECORD					*/
	VBK$LOC *	loc;			/* RECORD: where it lands, NULL - not wanted	*/
	void		(*fn) (void *a_arg);	/* CALL						*/
	void *		arg;
} VBK$ZSLOT;

typedef struct vbk_zp_t
{
	VBK$WCTX *	wctx;
	pthread_mutex_t	mtx;
	pthread_cond_t	cvwork, cvdone;
	VBK$ZSLOT *	slot;
	uint32_t	nslot, head, count;	/* The ring: COUNT slots from HEAD		*/
	int		stop, failed;
	unsigned	nthr;
	pthread_t	thr [VBK$K_ZPMAXTHR];
	uint64_t	nin, nout;
	int		level;			/* /LEVEL					*/
	uint64_t	inuse, budget;		/* Octets of data in the ring, at most		*/
} VBK$ZP;


static	void *	s_vbk$zworker	(
		void *		a_arg
			)
{
VBK$ZP *	l_zp = (VBK$ZP *) a_arg;
VBK$ZSLOT *	l_s;
uint32_t	l_zlen, l_codec;
int		l_rc;

	pthread_mutex_lock(&l_zp->mtx);

	for ( ;; )
		{
		l_s	= NULL;

		/* The oldest slot waiting: the head is wanted first */
		for ( uint32_t i = 0; !l_s && (i < l_zp->count); i++ )
			if ( l_zp->slot [(l_zp->head + i) % l_zp->nslot].state == VBK$K_ZS_READY )
				l_s = &l_zp->slot [(l_zp->head + i) % l_zp->nslot];

		if ( !l_s )
			{
			if ( l_zp->stop )
				break;

			pthread_cond_wait(&l_zp->cvwork, &l_zp->mtx);
			continue;
			}

		l_s->state	= VBK$K_ZS_BUSY;
		pthread_mutex_unlock(&l_zp->mtx);

		l_s->zlen	= 0;
		l_s->zcheck	= 0;
		l_rc		= vbk$data_pack(l_zp->level, l_s->buf, l_s->len, l_s->zbuf + VBK$K_DATAZHDR, l_s->zbufsz - VBK$K_DATAZHDR,
						&l_zlen, &l_codec, l_s->check);

		l_s->zcheck	= (l_rc == STS$K_ERROR);

		if ( l_rc == STS$K_SUCCESS )
			{
			vbk$put32(l_s->zbuf, l_s->fileno);
			vbk$put32(l_s->zbuf + 4, l_codec);
			vbk$put64(l_s->zbuf + 8, l_s->off);
			vbk$put32(l_s->zbuf + 16, l_s->len);
			l_s->zlen = VBK$K_DATAZHDR + l_zlen;
			}

		pthread_mutex_lock(&l_zp->mtx);
		l_s->state	= VBK$K_ZS_DONE;
		pthread_cond_broadcast(&l_zp->cvdone);
		}

	pthread_mutex_unlock(&l_zp->mtx);

	return	NULL;
}


/*
**  Write the slot at the head - done - into the saveset, in the caller's
**  thread; the slot is free again
*/
static	int	s_vbk$zwrite	(
		VBK$ZP *	a_zp,
		VBK$ZSLOT *	a_s
			)
{
uint8_t	l_hdr [VBK$K_DATAHDR];
int	l_status = STS$K_SUCCESS;

	if ( a_zp->failed )
		return	STS$K_ERROR;

	switch ( a_s->kind )
		{
		case	VBK$K_ZK_DATA:
			a_zp->nin += a_s->len;

			if ( a_s->zcheck )
				$VBKMSG(VBACKUP$_ZCHECK, a_s->off);

			if ( a_s->zlen )
				{
				a_zp->nout += a_s->zlen;
				l_status = vbk$wrt_record(a_zp->wctx, VBK$K_RT_DATAZ, a_s->zbuf, a_s->zlen, NULL);
				}
			else	{
				vbk$put32(l_hdr, a_s->fileno);
				vbk$put32(l_hdr + 4, 0);
				vbk$put64(l_hdr + 8, a_s->off);

				a_zp->nout += VBK$K_DATAHDR + (uint64_t) a_s->len;

				if ( 1 & (l_status = vbk$wrt_rechdr(a_zp->wctx, VBK$K_RT_DATA, VBK$K_DATAHDR + a_s->len, NULL)) )
					if ( 1 & (l_status = vbk$wrt_bytes(a_zp->wctx, l_hdr, sizeof(l_hdr))) )
						l_status = vbk$wrt_bytes(a_zp->wctx, a_s->buf, a_s->len);
				}
			break;

		case	VBK$K_ZK_RECORD:
			l_status = vbk$wrt_record(a_zp->wctx, a_s->type, a_s->buf, a_s->len, a_s->loc);
			break;

		default:
			a_s->fn(a_s->arg);
		}

	if ( !(1 & l_status) )
		a_zp->failed = 1;

	return	l_status;
}


/*
**  Write what is done at the head; <a_wait>: wait for it, as long as
**  slots are taken (a_wait 1 - until one is free, 2 - until all are)
*/
static	int	s_vbk$zdrain	(
		VBK$ZP *	a_zp,
		int		a_wait
			)
{
VBK$ZSLOT *	l_s;
int		l_status = STS$K_SUCCESS, l_written = 0;

	pthread_mutex_lock(&a_zp->mtx);

	while ( a_zp->count )
		{
		l_s	= &a_zp->slot [a_zp->head];

		if ( l_s->state != VBK$K_ZS_DONE )
			{
			/* 3: until one more is written - room for the octets of the next one */
			if ( !a_wait || ((a_wait == 1) && (a_zp->count < a_zp->nslot)) || ((a_wait == 3) && l_written) )
				break;

			pthread_cond_wait(&a_zp->cvdone, &a_zp->mtx);
			continue;
			}

		pthread_mutex_unlock(&a_zp->mtx);
		l_status = s_vbk$zwrite(a_zp, l_s);
		pthread_mutex_lock(&a_zp->mtx);

		if ( l_s->kind == VBK$K_ZK_DATA )
			a_zp->inuse -= l_s->len;

		l_written	= 1;
		l_s->state	= VBK$K_ZS_FREE;
		a_zp->head	= (a_zp->head + 1) % a_zp->nslot;
		a_zp->count--;

		if ( !(1 & l_status) )
			break;
		}

	pthread_mutex_unlock(&a_zp->mtx);

	return	a_zp->failed ? STS$K_ERROR : STS$K_SUCCESS;
}


/*
**  The next free slot at the tail - written out what must be first
*/
static	int	s_vbk$zgrow	(
		uint8_t **	a_buf,
		uint32_t *	a_size,
		uint32_t	a_need
			)
{
uint8_t *	l_p;

	if ( a_need <= *a_size )
		return	1;

	if ( !(l_p = realloc(*a_buf, a_need)) )
		return	0;

	*a_buf	= l_p;
	*a_size	= a_need;

	return	1;
}

static	VBK$ZSLOT *	s_vbk$ztail	(
		VBK$ZP *	a_zp,
		uint32_t	a_need,
		int		a_data
			)
{
VBK$ZSLOT *	l_s;

	if ( !(1 & s_vbk$zdrain(a_zp, 0)) || !(1 & s_vbk$zdrain(a_zp, 1)) )
		return	NULL;

	/* The octets of data in the ring within the budget: what is at the head written first */
	while ( a_data && a_zp->count && ((a_zp->inuse + a_need) > a_zp->budget) )
		if ( !(1 & s_vbk$zdrain(a_zp, 3)) )
			return	NULL;

	l_s	= &a_zp->slot [(a_zp->head + a_zp->count) % a_zp->nslot];

	/* The buffers of a slot by what it is to hold: a small file takes little */
	if ( !s_vbk$zgrow(&l_s->buf, &l_s->bufsz, a_need ? a_need : 1)
		|| (a_data && (!s_vbk$zgrow(&l_s->zbuf, &l_s->zbufsz, VBK$K_DATAZHDR + VBK$LZ4_BOUND(a_need))
			|| !s_vbk$zgrow(&l_s->check, &l_s->checksz, a_need ? a_need : 1))) )
		{
		a_zp->failed = 1;
		errno	= ENOMEM;

		return	NULL;
		}

	return	l_s;
}


/*
**  Put a filled slot into the ring
*/
static	void	s_vbk$zpush	(
		VBK$ZP *	a_zp,
		VBK$ZSLOT *	a_s,
		int		a_state
			)
{
	pthread_mutex_lock(&a_zp->mtx);
	a_s->state	= a_state;
	a_zp->count++;

	if ( a_state == VBK$K_ZS_READY )
		pthread_cond_signal(&a_zp->cvwork);

	pthread_mutex_unlock(&a_zp->mtx);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Start the ring for a save /DATA_FORMAT=COMPRESSED, when there are
**	cores to share the work.
**
**  FORMAL PARAMETERS:
**
**	a_wctx		The saveset being written
**
**  RETURN VALUE:
**	The ring, or NULL - no workers wanted or to be had: the save
**	compresses by itself.
**--
*/
struct vbk_zp_t *	vbk$zp_start	(
		VBK$WCTX *	a_wctx,
		int		a_level
			)
{
VBK$ZP *	l_zp;
const char *	l_env = getenv("VBACKUP_ZTHREADS");
long		l_n = sysconf(_SC_NPROCESSORS_ONLN);

	if ( l_env && *l_env )
		l_n	= strtol(l_env, NULL, 10);

	if ( l_n <= 1 )
		return	NULL;

	if ( l_n > VBK$K_ZPMAXTHR )
		l_n	= VBK$K_ZPMAXTHR;

	if ( !(l_zp = calloc(1, sizeof(VBK$ZP))) )
		return	NULL;

	l_zp->wctx	= a_wctx;
	l_zp->level	= a_level;
	/*
	**  Many slots - a small file takes three or four: its FILE, its data,
	**  its FEND, its catalog entry - and their buffers grown by need; the
	**  octets of data in the ring kept within 2n + 2 records of 1 MB
	*/
	l_zp->nslot	= ((16 * (uint32_t) l_n) < VBK$K_ZPMAXSLOT) ? (16 * (uint32_t) l_n) : VBK$K_ZPMAXSLOT;
	l_zp->budget	= (2 * (uint64_t) l_n + 2) * VBK$K_MAXDATA;

	if ( !(l_zp->slot = calloc(l_zp->nslot, sizeof(VBK$ZSLOT))) )
		{
		free(l_zp);

		return	NULL;
		}

	pthread_mutex_init(&l_zp->mtx, NULL);
	pthread_cond_init(&l_zp->cvwork, NULL);
	pthread_cond_init(&l_zp->cvdone, NULL);

	for ( ; l_zp->nthr < (unsigned) l_n; l_zp->nthr++ )
		if ( pthread_create(&l_zp->thr [l_zp->nthr], NULL, s_vbk$zworker, l_zp) )
			break;

	if ( !l_zp->nthr )
		{
		vbk$zp_stop(l_zp, NULL, NULL);

		return	NULL;
		}

	return	l_zp;
}


/*
**  The octets of a file at an offset: to be packed by a worker
*/
int	vbk$zp_data	(
	struct	vbk_zp_t *	a_zp,
		uint32_t	a_fileno,
		uint64_t	a_off,
	const	uint8_t *	a_data,
		uint32_t	a_n
			)
{
VBK$ZSLOT *	l_s;

	if ( !(l_s = s_vbk$ztail(a_zp, a_n, 1)) )
		return	STS$K_ERROR;

	pthread_mutex_lock(&a_zp->mtx);
	a_zp->inuse	+= a_n;
	pthread_mutex_unlock(&a_zp->mtx);

	l_s->kind	= VBK$K_ZK_DATA;
	l_s->fileno	= a_fileno;
	l_s->off	= a_off;
	l_s->len	= a_n;
	memcpy(l_s->buf, a_data, a_n);

	s_vbk$zpush(a_zp, l_s, VBK$K_ZS_READY);

	return	STS$K_SUCCESS;
}


/*
**  Any other record, copied, written in its turn; <a_loc> receives where
**  it lands, when it is written
*/
int	vbk$zp_record	(
	struct	vbk_zp_t *	a_zp,
		uint16_t	a_type,
	const	void *		a_body,
		uint32_t	a_len,
		VBK$LOC *	a_loc
			)
{
VBK$ZSLOT *	l_s;

	if ( !(l_s = s_vbk$ztail(a_zp, a_len ? a_len : 1, 0)) )
		return	STS$K_ERROR;

	l_s->kind	= VBK$K_ZK_RECORD;
	l_s->type	= a_type;
	l_s->len	= a_len;
	l_s->loc	= a_loc;
	memcpy(l_s->buf, a_body, a_len);

	s_vbk$zpush(a_zp, l_s, VBK$K_ZS_DONE);

	return	STS$K_SUCCESS;
}


/*
**  A routine to run in its turn - once everything before it is written
*/
int	vbk$zp_call	(
	struct	vbk_zp_t *	a_zp,
		void		(*a_fn) (void *),
		void *		a_arg
			)
{
VBK$ZSLOT *	l_s;

	if ( !(l_s = s_vbk$ztail(a_zp, 1, 0)) )
		return	STS$K_ERROR;

	l_s->kind	= VBK$K_ZK_CALL;
	l_s->fn		= a_fn;
	l_s->arg	= a_arg;

	s_vbk$zpush(a_zp, l_s, VBK$K_ZS_DONE);

	return	STS$K_SUCCESS;
}


/*
**  Everything handed over written
*/
int	vbk$zp_flush	(
	struct	vbk_zp_t *	a_zp
			)
{
	return	s_vbk$zdrain(a_zp, 2);
}


/*
**  Stop the workers, release the ring; the octets in and out of the
**  compression to the caller's counters
*/
void	vbk$zp_stop	(
	struct	vbk_zp_t *	a_zp,
		uint64_t *	a_nin,
		uint64_t *	a_nout
			)
{
	if ( !a_zp )
		return;

	pthread_mutex_lock(&a_zp->mtx);
	a_zp->stop	= 1;
	pthread_cond_broadcast(&a_zp->cvwork);
	pthread_mutex_unlock(&a_zp->mtx);

	for ( unsigned i = 0; i < a_zp->nthr; i++ )
		pthread_join(a_zp->thr [i], NULL);

	/* What was handed over and not written (a failed saveset) is not run either: CALL arguments are freed */
	for ( uint32_t i = 0; i < a_zp->count; i++ )
		{
		VBK$ZSLOT *	l_s = &a_zp->slot [(a_zp->head + i) % a_zp->nslot];

		if ( l_s->kind == VBK$K_ZK_CALL )
			free(l_s->arg);
		}

	for ( uint32_t i = 0; i < a_zp->nslot; i++ )
		{
		free(a_zp->slot [i].buf);
		free(a_zp->slot [i].zbuf);
		free(a_zp->slot [i].check);
		}

	if ( a_nin )
		*a_nin	+= a_zp->nin;

	if ( a_nout )
		*a_nout	+= a_zp->nout;

	pthread_cond_destroy(&a_zp->cvdone);
	pthread_cond_destroy(&a_zp->cvwork);
	pthread_mutex_destroy(&a_zp->mtx);
	free(a_zp->slot);
	free(a_zp);
}
