#ifndef	__VBKWRT$H__
#define	__VBKWRT$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKWRT"
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
**  MODULE:	vbkwrt.h
**
**  ABSTRACT:	The writing side of the saveset core: a stream of records
**		is cut into blocks, the blocks are sealed, grouped under XOR
**		blocks and laid out over volumes (format.md, sections 2-7).
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-04		 4-OCT-2026	RRL
**		VBK$K_SZ_SPEC and VBK$VOLSPEC moved to VBKFMT.H.
**
**	X01-03		 3-OCT-2026	RRL
**		The blocks are written by a thread of their own, behind a
**		queue; writeback of the volume in windows.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdint.h>
#include	<pthread.h>

#include	"vbkfmt.h"

#ifdef	__cplusplus
extern "C" {
#endif


#define	VBK$M_WRT_REPLACE	1		/* An existing volume may be overwritten	*/
#define	VBK$M_WRT_SYNC		2		/* No writer thread: every block written at once */

#define	VBK$K_WBWIN	(8 * 1048576)		/* Writeback window of a volume			*/

/*
**  The context of a saveset being written.  The members are the business
**  of VBKWRT.C; a caller reads <volno>, <blkno>, <err> and <volspec> only.
*/
typedef struct vbk_wctx_t
{
	char		spec [VBK$K_SZ_SPEC];	/* Name of volume 1, "-" - standard output	*/
	char		volspec [VBK$K_SZ_SPEC];/* Name of the current volume			*/
	int		fd;
	int		isstdout;
	unsigned	opts;			/* VBK$M_WRT_*					*/

	uint32_t	bsize;			/* Block size					*/
	uint32_t	psize;			/* Payload area, bsize - VBK$K_HDRSZ		*/
	uint32_t	grpsz;			/* DATA blocks under one XOR, 0 - no XOR	*/
	uint64_t	maxvolblk;		/* Blocks in a volume, 0 - one volume		*/

	uint8_t		ssuuid [VBK$K_UUIDSZ];
	uint32_t	volno;			/* Current volume				*/
	uint64_t	volblk;			/* Blocks emitted into the current volume	*/
	uint64_t	blkno;			/* Number the next block will get		*/

	uint8_t **	bufs;			/* All the block buffers			*/
	uint32_t	nbufs;
	uint8_t **	freel;			/* The free ones				*/
	uint32_t	nfree;
	uint8_t **	ring;			/* The sealed ones, waiting to be written	*/
	uint32_t	qhead, qcnt;

	int		thron;			/* The writer thread runs			*/
	pthread_t	thr;
	pthread_mutex_t	mtx;
	pthread_cond_t	cvfull, cvfree;		/* Something to write; a buffer has come back	*/
	int		busy;			/* The thread is writing a block		*/
	int		stop;
	int		werr;			/* errno of a write of the thread, 0 - none	*/

	int		isreg;			/* The volume is a regular file: writeback	*/
	uint64_t	wroff;			/* Octets written into the volume		*/
	uint64_t	wbsync;			/* Writeback started up to here ...		*/
	uint64_t	wbdrop;			/* ... finished and dropped up to here		*/

	uint8_t *	cur;			/* The DATA block being filled			*/
	uint32_t	fill;			/* Octets of its payload in use			*/
	uint32_t	recoff;			/* First record header in it, VBK$K_NONE	*/
	int		curopen;

	uint8_t *	pend;			/* The block written behind - its LASTINVOL	*/
	int		haspend;		/* flag is known only when the next one comes	*/
	uint8_t *	aux;			/* XOR, VHDR and TRAILER blocks are built here	*/

	uint8_t *	xor;			/* XOR of the payloads of the current group	*/
	uint32_t	gcnt;			/* DATA blocks in the current group		*/
	uint32_t	prvrecoff, prvpaylen;	/* Of the previous DATA block of the group	*/

	uint8_t *	vhdr;			/* The SUMMARY record every VHDR carries	*/
	uint32_t	vhdrlen;

	int		(*volcb) (void *a_arg, uint32_t a_volno, const char *a_spec);
	void *		volarg;			/* Called when a volume has been created	*/

	int		err;			/* errno of the failure				*/
} VBK$WCTX;

int	vbk$wrt_open	(VBK$WCTX *a_ctx, const char *a_spec, uint32_t a_bsize, uint32_t a_grpsz, uint64_t a_volsize, unsigned a_opts,
			const uint8_t *a_sumbody, uint32_t a_sumlen);
int	vbk$wrt_rechdr	(VBK$WCTX *a_ctx, uint16_t a_type, uint32_t a_len, VBK$LOC *a_loc);
int	vbk$wrt_bytes	(VBK$WCTX *a_ctx, const void *a_data, uint32_t a_len);
int	vbk$wrt_record	(VBK$WCTX *a_ctx, uint16_t a_type, const void *a_body, uint32_t a_len, VBK$LOC *a_loc);
int	vbk$wrt_finish	(VBK$WCTX *a_ctx, uint64_t *a_nblocks, uint32_t *a_nvols);
int	vbk$wrt_close	(VBK$WCTX *a_ctx, const uint8_t *a_trlbody, uint32_t a_trllen);
void	vbk$wrt_abort	(VBK$WCTX *a_ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKWRT$H__ */
