#define	__MODULE__	"VBKWRT"
#define	__IDENT__	"X01-03"
#define	__REV__		"1.3.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkwrt.c
**
**  ABSTRACT:	The writing side of the saveset core.  See doc/format.md,
**		sections 2-7, for the layout this module produces.
**
**  DESCRIPTION: A caller hands records over - VBK$WRT_RECHDR for the
**		header, VBK$WRT_BYTES for the body in as many pieces as it
**		likes - and the module cuts the stream into DATA blocks,
**		closes a group with its XOR block every <grpsz> blocks and
**		opens a new volume when the next block and its XOR block
**		would no longer fit.
**
**		Every block is written one step behind: whether a block is
**		the last of its volume (LASTINVOL) is known only when the
**		next one is about to be emitted, and a block that has been
**		written already would have to be read back, patched and
**		resealed.  So the newest block waits in PEND until the next
**		one comes.
**
**		A block that is final goes into a queue, and a thread of its
**		own writes it out, so the reading of the files and the writing
**		of the saveset overlap.  Nothing else is done there: the
**		layout, the volume changes and the messages stay with the
**		caller's thread, which waits the queue empty before it closes
**		a volume - an error is so always that of the current volume.
**		The queue holds 4 MB at least: one read of a file (1 MB) is
**		cut into many blocks, and a short queue would stall in each.
**
**		The thread also keeps the dirty pages of the volume few: the
**		writeback of each window of VBK$K_WBWIN octets is started at
**		once and waited for one window later, and those pages are
**		then dropped.  A saveset is so written at the steady pace of
**		the device instead of in one long fsync at the end, and it
**		does not push the working data of the system out of the cache.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-03		 3-OCT-2026	RRL
**		The writer thread and its queue; writeback in windows; every
**		volume is fsync'ed when closed.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<sys/stat.h>

#include	"vbkwrt.h"
#include	"vbkos.h"


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Build the name of volume <a_volno>: volume 1 is the name as given,
**	volume k is the name followed by ".kkk".
**
**  FORMAL PARAMETERS:
**
**	a_spec		Name of volume 1
**	a_volno		Volume number, from 1
**	a_out		Receives the name
**	a_outsz		Size of <a_out>
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- built;
**	STS$K_ERROR	- it does not fit into <a_out>.
**--
*/
int	vbk$volspec	(
	const	char *		a_spec,
		uint32_t	a_volno,
		char *		a_out,
		size_t		a_outsz
			)
{
int	l_n;

	if ( a_volno <= 1 )
		l_n	= snprintf(a_out, a_outsz, "%s", a_spec);
	else	l_n	= snprintf(a_out, a_outsz, "%s.%03u", a_spec, a_volno);

	return	((l_n < 0) || ((size_t) l_n >= a_outsz)) ? STS$K_ERROR : STS$K_SUCCESS;
}


/*
**  Write one block into the volume, a short write being retried, and
**  keep the writeback going.  Runs in the writer thread; it touches the
**  descriptor and the writeback counters only.
**
**  RETURN VALUE:
**	0, or the errno of the failure.
*/
static	int	s_vbk$write	(
		VBK$WCTX *	a_ctx,
	const	uint8_t *	a_buf,
		uint32_t	a_len
			)
{
ssize_t	l_rc;

	while ( a_len )
		{
		if ( 0 > (l_rc = write(a_ctx->fd, a_buf, a_len)) )
			{
			if ( errno == EINTR )
				continue;

			return	errno;
			}

		a_buf		+= l_rc;
		a_len		-= (uint32_t) l_rc;
		a_ctx->wroff	+= (uint64_t) l_rc;
		}

	if ( a_ctx->isreg && ((a_ctx->wroff - a_ctx->wbsync) >= VBK$K_WBWIN) )
		{
		sync_file_range(a_ctx->fd, (off_t) a_ctx->wbsync, (off_t) (a_ctx->wroff - a_ctx->wbsync), SYNC_FILE_RANGE_WRITE);

		if ( a_ctx->wbsync > a_ctx->wbdrop )
			{
			sync_file_range(a_ctx->fd, (off_t) a_ctx->wbdrop, (off_t) (a_ctx->wbsync - a_ctx->wbdrop),
				SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER);
			vbk$os_drop(a_ctx->fd, a_ctx->wbdrop, a_ctx->wbsync - a_ctx->wbdrop);

			a_ctx->wbdrop	= a_ctx->wbsync;
			}

		a_ctx->wbsync	= a_ctx->wroff;
		}

	return	0;
}


/*
**  The writer thread: the queue is written out in order until STOP; after
**  a failure what comes is given back unwritten
*/
static	void *	s_vbk$writer	(
		void *		a_arg
			)
{
VBK$WCTX *	l_ctx = (VBK$WCTX *) a_arg;
uint8_t *	l_buf;
int		l_err;

	pthread_mutex_lock(&l_ctx->mtx);

	for ( ;; )
		{
		while ( !l_ctx->qcnt && !l_ctx->stop )
			pthread_cond_wait(&l_ctx->cvfull, &l_ctx->mtx);

		if ( !l_ctx->qcnt )
			break;

		l_buf		= l_ctx->ring [l_ctx->qhead];
		l_ctx->qhead	= (l_ctx->qhead + 1) % l_ctx->nbufs;
		l_ctx->qcnt--;

		l_err		= 0;

		if ( !l_ctx->werr )
			{
			l_ctx->busy	= 1;
			pthread_mutex_unlock(&l_ctx->mtx);

			l_err	= s_vbk$write(l_ctx, l_buf, l_ctx->bsize);

			pthread_mutex_lock(&l_ctx->mtx);
			l_ctx->busy	= 0;
			}

		if ( l_err )
			l_ctx->werr	= l_err;

		l_ctx->freel [l_ctx->nfree++] = l_buf;
		pthread_cond_signal(&l_ctx->cvfree);
		}

	pthread_mutex_unlock(&l_ctx->mtx);

	return	NULL;
}


/*
**  Hand a final block over to be written: to the queue, or - no thread -
**  written at once.  The buffer is not the caller's any more.
*/
static	int	s_vbk$put	(
		VBK$WCTX *	a_ctx,
		uint8_t *	a_buf
			)
{
int	l_err;

	if ( !a_ctx->thron )
		{
		l_err	= s_vbk$write(a_ctx, a_buf, a_ctx->bsize);
		a_ctx->freel [a_ctx->nfree++] = a_buf;

		if ( l_err )
			{
			a_ctx->err	= l_err;

			return	STS$K_ERROR;
			}

		return	STS$K_SUCCESS;
		}

	pthread_mutex_lock(&a_ctx->mtx);

	if ( (l_err = a_ctx->werr) )
		a_ctx->freel [a_ctx->nfree++] = a_buf;
	else	{
		a_ctx->ring [(a_ctx->qhead + a_ctx->qcnt) % a_ctx->nbufs] = a_buf;
		a_ctx->qcnt++;
		pthread_cond_signal(&a_ctx->cvfull);
		}

	pthread_mutex_unlock(&a_ctx->mtx);

	if ( l_err )
		{
		a_ctx->err	= l_err;

		return	STS$K_ERROR;
		}

	return	STS$K_SUCCESS;
}


/*
**  A free block buffer, waited for while the queue is full; NULL - the
**  writer thread has failed
*/
static	uint8_t *	s_vbk$getbuf	(
		VBK$WCTX *	a_ctx
			)
{
uint8_t *	l_buf = NULL;

	if ( !a_ctx->thron )
		return	a_ctx->nfree ? a_ctx->freel [--a_ctx->nfree] : NULL;

	pthread_mutex_lock(&a_ctx->mtx);

	while ( !a_ctx->nfree && !a_ctx->werr )
		pthread_cond_wait(&a_ctx->cvfree, &a_ctx->mtx);

	if ( a_ctx->werr )
		a_ctx->err	= a_ctx->werr;
	else	l_buf		= a_ctx->freel [--a_ctx->nfree];

	pthread_mutex_unlock(&a_ctx->mtx);

	return	l_buf;
}


/*
**  Wait until every block handed over has been written
*/
static	int	s_vbk$drain	(
		VBK$WCTX *	a_ctx
			)
{
int	l_err;

	if ( !a_ctx->thron )
		return	STS$K_SUCCESS;

	pthread_mutex_lock(&a_ctx->mtx);

	while ( (a_ctx->qcnt || a_ctx->busy) && !a_ctx->werr )
		pthread_cond_wait(&a_ctx->cvfree, &a_ctx->mtx);

	l_err	= a_ctx->werr;

	pthread_mutex_unlock(&a_ctx->mtx);

	if ( l_err )
		{
		a_ctx->err	= l_err;

		return	STS$K_ERROR;
		}

	return	STS$K_SUCCESS;
}


/*
**  Close the current volume: everything written, on the disk, and its
**  pages out of the cache
*/
static	int	s_vbk$closevol	(
		VBK$WCTX *	a_ctx
			)
{
int	l_status, l_fd = a_ctx->fd;

	if ( l_fd < 0 )
		return	STS$K_SUCCESS;

	l_status	= s_vbk$drain(a_ctx);
	a_ctx->fd	= -1;

	if ( a_ctx->isstdout )
		return	l_status;

	if ( (1 & l_status) && (a_ctx->isreg && fsync(l_fd)) )
		{
		a_ctx->err	= errno;
		l_status	= STS$K_ERROR;
		}

	if ( a_ctx->isreg )
		vbk$os_drop(l_fd, 0, 0);

	if ( close(l_fd) && (1 & l_status) )
		{
		a_ctx->err	= errno;
		l_status	= STS$K_ERROR;
		}

	return	l_status;
}


/*
**  Write the block waiting in PEND, with the extra <a_flags> put into its
**  header first.  Nothing is waiting - nothing to do.
*/
static	int	s_vbk$flushpend	(
		VBK$WCTX *	a_ctx,
		uint8_t		a_flags
			)
{
uint8_t *	l_buf = a_ctx->pend;

	if ( !a_ctx->haspend )
		return	STS$K_SUCCESS;

	if ( a_flags )
		{
		l_buf [13] |= a_flags;
		vbk$blk_seal(l_buf, a_ctx->bsize);
		}

	a_ctx->haspend	= 0;
	a_ctx->pend	= NULL;

	return	s_vbk$put(a_ctx, l_buf);
}


/*
**  Emit a sealed block: the one waiting goes out, this one takes its
**  place, and a free buffer is handed back in <*a_buf>.
*/
static	int	s_vbk$emit	(
		VBK$WCTX *	a_ctx,
		uint8_t **	a_buf
			)
{
uint8_t *	l_new;
int		l_status;

	if ( !(1 & (l_status = s_vbk$flushpend(a_ctx, 0))) )
		return	l_status;

	if ( !(l_new = s_vbk$getbuf(a_ctx)) )
		return	STS$K_ERROR;

	a_ctx->pend	= *a_buf;
	*a_buf		= l_new;
	a_ctx->haspend	= 1;

	a_ctx->volblk++;

	return	STS$K_SUCCESS;
}


/*
**  The common part of a header: the constants and the next block number
*/
static	void	s_vbk$hdrini	(
		VBK$WCTX *	a_ctx,
		VBK$BHDR *	a_hdr,
		uint8_t		a_type
			)
{
	memset(a_hdr, 0, sizeof(*a_hdr));

	a_hdr->bsize	= a_ctx->bsize;
	a_hdr->type	= a_type;
	a_hdr->blkno	= a_ctx->blkno++;
	a_hdr->volno	= a_ctx->volno;
	a_hdr->recoff	= 0;
	a_hdr->prvrecoff = VBK$K_NONE;

	memcpy(a_hdr->ssuuid, a_ctx->ssuuid, VBK$K_UUIDSZ);
}


/*
**  Emit a block that carries a body of its own - VHDR or TRAILER
*/
static	int	s_vbk$special	(
		VBK$WCTX *	a_ctx,
		uint8_t		a_type,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
VBK$BHDR	l_hdr;

	if ( a_len > a_ctx->psize )
		{
		a_ctx->err	= EOVERFLOW;

		return	STS$K_ERROR;
		}

	s_vbk$hdrini(a_ctx, &l_hdr, a_type);
	l_hdr.paylen	= a_len;

	memset(a_ctx->aux, 0, a_ctx->bsize);
	vbk$bhdr_put(&l_hdr, a_ctx->aux);
	memcpy(a_ctx->aux + VBK$K_HDRSZ, a_body, a_len);
	vbk$blk_seal(a_ctx->aux, a_ctx->bsize);

	return	s_vbk$emit(a_ctx, &a_ctx->aux);
}


/*
**  Close the current group with its XOR block
*/
static	int	s_vbk$xor	(
		VBK$WCTX *	a_ctx
			)
{
VBK$BHDR	l_hdr;
int		l_status;

	if ( !a_ctx->grpsz || !a_ctx->gcnt )
		return	STS$K_SUCCESS;

	s_vbk$hdrini(a_ctx, &l_hdr, VBK$K_BT_XOR);
	l_hdr.gindex	= (uint16_t) a_ctx->gcnt;
	l_hdr.recoff	= VBK$K_NONE;
	l_hdr.paylen	= a_ctx->psize;
	l_hdr.prvrecoff	= a_ctx->prvrecoff;
	l_hdr.prvpaylen	= a_ctx->prvpaylen;

	vbk$bhdr_put(&l_hdr, a_ctx->aux);
	memcpy(a_ctx->aux + VBK$K_HDRSZ, a_ctx->xor, a_ctx->psize);
	vbk$blk_seal(a_ctx->aux, a_ctx->bsize);

	if ( !(1 & (l_status = s_vbk$emit(a_ctx, &a_ctx->aux))) )
		return	l_status;

	memset(a_ctx->xor, 0, a_ctx->psize);

	a_ctx->gcnt	= 0;
	a_ctx->prvrecoff = VBK$K_NONE;
	a_ctx->prvpaylen = 0;

	return	STS$K_SUCCESS;
}


/*
**  Open volume <a_ctx->volno> and write its VHDR
*/
static	int	s_vbk$openvol	(
		VBK$WCTX *	a_ctx
			)
{
int		l_flags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC;
struct stat	l_st;

	a_ctx->volblk	= 0;
	a_ctx->wroff	= a_ctx->wbsync = a_ctx->wbdrop = 0;

	if ( a_ctx->isstdout )
		{
		a_ctx->fd	= STDOUT_FILENO;
		snprintf(a_ctx->volspec, sizeof(a_ctx->volspec), "%s", "(standard output)");
		}
	else	{
		if ( !(1 & vbk$volspec(a_ctx->spec, a_ctx->volno, a_ctx->volspec, sizeof(a_ctx->volspec))) )
			{
			a_ctx->err	= ENAMETOOLONG;

			return	STS$K_ERROR;
			}

		if ( !(a_ctx->opts & VBK$M_WRT_REPLACE) )
			l_flags	|= O_EXCL;

		if ( 0 > (a_ctx->fd = open(a_ctx->volspec, l_flags, 0644)) )
			{
			a_ctx->err	= errno;

			return	STS$K_ERROR;
			}
		}

	a_ctx->isreg	= !fstat(a_ctx->fd, &l_st) && S_ISREG(l_st.st_mode);

	if ( a_ctx->volcb )
		a_ctx->volcb(a_ctx->volarg, a_ctx->volno, a_ctx->volspec);

	return	s_vbk$special(a_ctx, VBK$K_BT_VHDR, a_ctx->vhdr, a_ctx->vhdrlen);
}


/*
**  Close the current volume - its partial group first - and open the next
*/
static	int	s_vbk$nextvol	(
		VBK$WCTX *	a_ctx
			)
{
int	l_status;

	if ( !(1 & (l_status = s_vbk$xor(a_ctx))) )
		return	l_status;

	if ( !(1 & (l_status = s_vbk$flushpend(a_ctx, VBK$M_LASTINVOL))) )
		return	l_status;

	if ( !(1 & (l_status = s_vbk$closevol(a_ctx))) )
		return	l_status;

	a_ctx->volno++;

	return	s_vbk$openvol(a_ctx);
}


/*
**  Make room for <a_nblk> more blocks in the current volume
*/
static	int	s_vbk$need	(
		VBK$WCTX *	a_ctx,
		uint32_t	a_nblk
			)
{
	if ( !a_ctx->maxvolblk || ((a_ctx->volblk + a_nblk) <= a_ctx->maxvolblk) )
		return	STS$K_SUCCESS;

	return	s_vbk$nextvol(a_ctx);
}


/*
**  Begin a new DATA block: it and the XOR block of its group must fit
*/
static	int	s_vbk$opendata	(
		VBK$WCTX *	a_ctx
			)
{
int	l_status;

	if ( !(1 & (l_status = s_vbk$need(a_ctx, a_ctx->grpsz ? 2 : 1))) )
		return	l_status;

	memset(a_ctx->cur, 0, a_ctx->bsize);

	a_ctx->fill	= 0;
	a_ctx->recoff	= VBK$K_NONE;
	a_ctx->curopen	= 1;

	return	STS$K_SUCCESS;
}


/*
**  Seal the current DATA block, account it in the group, emit it
*/
static	int	s_vbk$closedata	(
		VBK$WCTX *	a_ctx
			)
{
VBK$BHDR	l_hdr;
uint64_t *	l_x, *l_p;
int		l_status;

	if ( !a_ctx->curopen )
		return	STS$K_SUCCESS;

	s_vbk$hdrini(a_ctx, &l_hdr, VBK$K_BT_DATA);
	l_hdr.gindex	= (uint16_t) a_ctx->gcnt;
	l_hdr.recoff	= a_ctx->recoff;
	l_hdr.paylen	= a_ctx->fill;
	l_hdr.prvrecoff	= a_ctx->grpsz ? a_ctx->prvrecoff : VBK$K_NONE;
	l_hdr.prvpaylen	= a_ctx->grpsz ? a_ctx->prvpaylen : 0;

	vbk$bhdr_put(&l_hdr, a_ctx->cur);
	vbk$blk_seal(a_ctx->cur, a_ctx->bsize);

	a_ctx->curopen	= 0;

	if ( a_ctx->grpsz )
		{
		/* The payload area is a multiple of 64 octets, so it is XORed a quadword at a time */
		l_x	= (uint64_t *) a_ctx->xor;
		l_p	= (uint64_t *) (a_ctx->cur + VBK$K_HDRSZ);

		for ( uint32_t i = 0; i < (a_ctx->psize / sizeof(uint64_t)); i++ )
			l_x [i] ^= l_p [i];

		a_ctx->prvrecoff = a_ctx->recoff;
		a_ctx->prvpaylen = a_ctx->fill;
		a_ctx->gcnt++;
		}

	if ( !(1 & (l_status = s_vbk$emit(a_ctx, &a_ctx->cur))) )
		return	l_status;

	if ( a_ctx->grpsz && (a_ctx->gcnt >= a_ctx->grpsz) )
		return	s_vbk$xor(a_ctx);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Create a saveset: volume 1 is opened and its VHDR written.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context, zeroed by the caller beforehand
**	a_spec		Name of volume 1, "-" - the standard output
**	a_bsize		Block size, validated by the caller
**	a_grpsz		Group size, 0 - no XOR blocks
**	a_volsize	Volume size in octets, 0 - one volume
**	a_opts		VBK$M_WRT_*
**	a_sumbody	Body of the SUMMARY record
**	a_sumlen	Its length
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Allocates the buffers of the context; VBK$WRT_CLOSE or
**	VBK$WRT_ABORT release them.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- volume 1 is open;
**	STS$K_ERROR	- it could not be created, or the SUMMARY does not fit
**			  into one block; <a_ctx->err> is the errno;
**	STS$K_FATAL	- no memory.
**--
*/
int	vbk$wrt_open	(
		VBK$WCTX *	a_ctx,
	const	char *		a_spec,
		uint32_t	a_bsize,
		uint32_t	a_grpsz,
		uint64_t	a_volsize,
		unsigned	a_opts,
	const	uint8_t *	a_sumbody,
		uint32_t	a_sumlen
			)
{
int	l_status;

	snprintf(a_ctx->spec, sizeof(a_ctx->spec), "%s", a_spec);

	a_ctx->fd	= -1;
	a_ctx->isstdout	= !strcmp(a_spec, "-");
	a_ctx->opts	= a_opts;
	a_ctx->bsize	= a_bsize;
	a_ctx->psize	= a_bsize - VBK$K_HDRSZ;
	a_ctx->grpsz	= a_grpsz;
	a_ctx->maxvolblk = a_volsize / a_bsize;
	a_ctx->volno	= 1;
	a_ctx->prvrecoff = VBK$K_NONE;

	if ( a_ctx->isstdout )
		a_ctx->maxvolblk = 0;

	if ( !(1 & vbk$os_random(a_ctx->ssuuid, sizeof(a_ctx->ssuuid))) )
		{
		a_ctx->err	= errno;

		return	STS$K_ERROR;
		}

	a_ctx->ssuuid [6] = (uint8_t) ((a_ctx->ssuuid [6] & 0x0F) | 0x40);	/* RFC 4122: version 4	*/
	a_ctx->ssuuid [8] = (uint8_t) ((a_ctx->ssuuid [8] & 0x3F) | 0x80);	/* ... variant 1	*/

	/* The queue: 4 MB at least, 8 blocks at least; and CUR, AUX, PEND besides */
	a_ctx->nbufs	= (4 * VBK$K_MAXDATA) / a_bsize;
	a_ctx->nbufs	= ((a_ctx->nbufs < 8) ? 8 : a_ctx->nbufs) + 3;

	a_ctx->bufs	= calloc(a_ctx->nbufs, sizeof(uint8_t *));
	a_ctx->freel	= calloc(a_ctx->nbufs, sizeof(uint8_t *));
	a_ctx->ring	= calloc(a_ctx->nbufs, sizeof(uint8_t *));
	a_ctx->xor	= aligned_alloc(64, a_bsize);
	a_ctx->vhdr	= malloc(VBK$K_RECHDR + a_sumlen);

	for ( uint32_t i = 0; a_ctx->bufs && a_ctx->freel && (i < a_ctx->nbufs); i++ )
		if ( (a_ctx->bufs [i] = aligned_alloc(64, a_bsize)) )
			a_ctx->freel [a_ctx->nfree++] = a_ctx->bufs [i];

	if ( !a_ctx->bufs || !a_ctx->freel || !a_ctx->ring || !a_ctx->xor || !a_ctx->vhdr || (a_ctx->nfree != a_ctx->nbufs) )
		{
		a_ctx->err	= ENOMEM;
		vbk$wrt_abort(a_ctx);

		return	STS$K_FATAL;
		}

	a_ctx->cur	= a_ctx->freel [--a_ctx->nfree];
	a_ctx->aux	= a_ctx->freel [--a_ctx->nfree];

	/* No thread to be had: the blocks are written at once, as they come */
	if ( !(a_opts & VBK$M_WRT_SYNC) )
		{
		pthread_mutex_init(&a_ctx->mtx, NULL);
		pthread_cond_init(&a_ctx->cvfull, NULL);
		pthread_cond_init(&a_ctx->cvfree, NULL);

		if ( !(a_ctx->thron = !pthread_create(&a_ctx->thr, NULL, s_vbk$writer, a_ctx)) )
			{
			pthread_cond_destroy(&a_ctx->cvfree);
			pthread_cond_destroy(&a_ctx->cvfull);
			pthread_mutex_destroy(&a_ctx->mtx);
			}
		}

	memset(a_ctx->xor, 0, a_bsize);

	/* The VHDR payload is one complete SUMMARY record */
	vbk$put16(a_ctx->vhdr, VBK$K_RT_SUMMARY);
	vbk$put16(a_ctx->vhdr + 2, 0);
	vbk$put32(a_ctx->vhdr + 4, a_sumlen);
	memcpy(a_ctx->vhdr + VBK$K_RECHDR, a_sumbody, a_sumlen);

	a_ctx->vhdrlen	= VBK$K_RECHDR + a_sumlen;

	if ( a_ctx->vhdrlen > a_ctx->psize )
		{
		a_ctx->err	= EOVERFLOW;
		vbk$wrt_abort(a_ctx);

		return	STS$K_ERROR;
		}

	/* Volume 1 not to be had: the thread and the buffers go, the reason stays in ERR */
	if ( !(1 & (l_status = s_vbk$openvol(a_ctx))) )
		vbk$wrt_abort(a_ctx);

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Begin a record: its header goes into the stream, the body is to
**	follow through VBK$WRT_BYTES, exactly <a_len> octets of it.  A
**	header is never split over two blocks.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context
**	a_type		Record type, VBK$K_RT_*
**	a_len		Length of the body
**	a_loc		Receives where the header lands, NULL - not wanted
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the status of a failed write.
**--
*/
int	vbk$wrt_rechdr	(
		VBK$WCTX *	a_ctx,
		uint16_t	a_type,
		uint32_t	a_len,
		VBK$LOC *	a_loc
			)
{
uint8_t	l_hdr [VBK$K_RECHDR];
int	l_status;

	if ( a_ctx->curopen && ((a_ctx->psize - a_ctx->fill) < VBK$K_RECHDR) )
		if ( !(1 & (l_status = s_vbk$closedata(a_ctx))) )
			return	l_status;

	if ( !a_ctx->curopen )
		if ( !(1 & (l_status = s_vbk$opendata(a_ctx))) )
			return	l_status;

	if ( a_ctx->recoff == VBK$K_NONE )
		a_ctx->recoff	= a_ctx->fill;

	/* The block being filled is numbered when it is closed - and it is the next number */
	if ( a_loc )
		{
		a_loc->vol	= a_ctx->volno;
		a_loc->blk	= a_ctx->blkno;
		a_loc->off	= a_ctx->fill;
		}

	vbk$put16(l_hdr, a_type);
	vbk$put16(l_hdr + 2, 0);
	vbk$put32(l_hdr + 4, a_len);

	return	vbk$wrt_bytes(a_ctx, l_hdr, sizeof(l_hdr));
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Put octets of a record body into the stream.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context
**	a_data		The octets
**	a_len		How many
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the status of a failed write.
**--
*/
int	vbk$wrt_bytes	(
		VBK$WCTX *	a_ctx,
	const	void *		a_data,
		uint32_t	a_len
			)
{
const uint8_t *	l_p = (const uint8_t *) a_data;
uint32_t	l_n;
int		l_status;

	while ( a_len )
		{
		if ( !a_ctx->curopen )
			if ( !(1 & (l_status = s_vbk$opendata(a_ctx))) )
				return	l_status;

		l_n	= a_ctx->psize - a_ctx->fill;
		l_n	= (l_n < a_len) ? l_n : a_len;

		memcpy(a_ctx->cur + VBK$K_HDRSZ + a_ctx->fill, l_p, l_n);

		a_ctx->fill	+= l_n;
		l_p		+= l_n;
		a_len		-= l_n;

		if ( a_ctx->fill == a_ctx->psize )
			if ( !(1 & (l_status = s_vbk$closedata(a_ctx))) )
				return	l_status;
		}

	return	STS$K_SUCCESS;
}


/*
**  A whole record in one call
*/
int	vbk$wrt_record	(
		VBK$WCTX *	a_ctx,
		uint16_t	a_type,
	const	void *		a_body,
		uint32_t	a_len,
		VBK$LOC *	a_loc
			)
{
int	l_status;

	if ( !(1 & (l_status = vbk$wrt_rechdr(a_ctx, a_type, a_len, a_loc))) )
		return	l_status;

	return	vbk$wrt_bytes(a_ctx, a_body, a_len);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	End the record stream: the last DATA block and its group are
**	closed and room is made for the TRAILER.  The totals the TRAILER
**	is to carry are known from here on.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context
**	a_nblocks	Receives the number of all blocks, the TRAILER included
**	a_nvols		Receives the number of volumes
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the status of a failed write.
**--
*/
int	vbk$wrt_finish	(
		VBK$WCTX *	a_ctx,
		uint64_t *	a_nblocks,
		uint32_t *	a_nvols
			)
{
int	l_status;

	if ( !(1 & (l_status = s_vbk$closedata(a_ctx))) )
		return	l_status;

	if ( !(1 & (l_status = s_vbk$xor(a_ctx))) )
		return	l_status;

	if ( !(1 & (l_status = s_vbk$need(a_ctx, 1))) )
		return	l_status;

	*a_nblocks	= a_ctx->blkno + 1;
	*a_nvols	= a_ctx->volno;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Write the TRAILER and close the saveset.  VBK$WRT_FINISH must have
**	been called.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context
**	a_trlbody	The TLV body of the TRAILER
**	a_trllen	Its length
**
**  IMPLICIT INPUTS/OUTPUTS:
**	The buffers of the context are released, the volume is closed.
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the status of a failed write or close.
**--
*/
int	vbk$wrt_close	(
		VBK$WCTX *	a_ctx,
	const	uint8_t *	a_trlbody,
		uint32_t	a_trllen
			)
{
int	l_status;

	if ( 1 & (l_status = s_vbk$special(a_ctx, VBK$K_BT_TRAILER, a_trlbody, a_trllen)) )
		l_status = s_vbk$flushpend(a_ctx, VBK$M_LASTINVOL | VBK$M_LASTINSET);

	if ( 1 & l_status )
		l_status = s_vbk$closevol(a_ctx);

	vbk$wrt_abort(a_ctx);

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Give up a saveset being written: what has been written stays - it
**	is readable in sequential mode up to its last block - the rest is
**	released.  Also the common tail of VBK$WRT_CLOSE.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context
**
**  RETURN VALUE:
**	None.
**--
*/
void	vbk$wrt_abort	(
		VBK$WCTX *	a_ctx
			)
{
	if ( a_ctx->fd >= 0 )
		{
		if ( a_ctx->haspend && a_ctx->pend )
			s_vbk$flushpend(a_ctx, 0);

		s_vbk$drain(a_ctx);

		if ( !a_ctx->isstdout )
			close(a_ctx->fd);
		}

	a_ctx->fd	= -1;

	/* The thread is gone before a buffer is released: it may hold one */
	if ( a_ctx->thron )
		{
		pthread_mutex_lock(&a_ctx->mtx);
		a_ctx->stop	= 1;
		pthread_cond_signal(&a_ctx->cvfull);
		pthread_mutex_unlock(&a_ctx->mtx);

		pthread_join(a_ctx->thr, NULL);

		pthread_cond_destroy(&a_ctx->cvfree);
		pthread_cond_destroy(&a_ctx->cvfull);
		pthread_mutex_destroy(&a_ctx->mtx);

		a_ctx->thron	= 0;
		}

	for ( uint32_t i = 0; a_ctx->bufs && (i < a_ctx->nbufs); i++ )
		free(a_ctx->bufs [i]);

	free(a_ctx->bufs);
	free(a_ctx->freel);
	free(a_ctx->ring);
	free(a_ctx->xor);
	free(a_ctx->vhdr);

	a_ctx->bufs = a_ctx->freel = a_ctx->ring = NULL;
	a_ctx->cur = a_ctx->pend = a_ctx->aux = a_ctx->xor = a_ctx->vhdr = NULL;
	a_ctx->nbufs = a_ctx->nfree = a_ctx->qcnt = 0;
	a_ctx->haspend	= 0;
}
