#define	__MODULE__	"VBKRD"
#define	__IDENT__	"X01-23"
#define	__REV__		"1.23.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkrd.c
**
**  ABSTRACT:	The reading side of the saveset core.  See doc/format.md,
**		sections 4 and 8.
**
**  DESCRIPTION: The volumes are read a group at a time.  The place of a
**		group in a volume follows from the group size alone - VHDR
**		at position 0, then groups of N + 1 blocks, the last group of
**		a volume possibly shorter - so a bad block never hides where
**		the next group begins.  Each block of the group is checked;
**		one bad DATA block is rebuilt from the XOR block, more are
**		reported lost.
**
**		The payloads of the good DATA blocks make the record stream.
**		After a loss the stream is picked up again at the first
**		record header of the next good block (its RECOFF), and the
**		record returned then is flagged RESYNC so that a caller can
**		mark the file it was restoring as damaged.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-23		 7-OCT-2026	RRL
**		VBK$RD_NEXT in two layers: S_VBK$RAW reads a record as it lies there,
**		the SOLID opened above it.  Batch mode (VBK$RD_BATCH): records read
**		ahead, up to 4 a thread, the DATAZ and SOLID among them decompressed at
**		once through the pool of the utility (VBK$CRP_PAR), handed out in their
**		order - a DATAZ as its DATA record.
**
**	X01-22		 7-OCT-2026	RRL
**		A SOLID cut by lost blocks: what is there of it before the gap is
**		decompressed, and its records whole in that are returned - the files
**		before the damage come back, each still checked by its FEND.
**
**	X01-21		 7-OCT-2026	RRL
**		Version 3: the SOLID record opened by VBK$RD_NEXT, its records returned
**		one by one as if they were in the stream; a SOLID that does not open
**		is a bad record, and what it held is a gap.
**
**	X01-14		 5-OCT-2026	RRL
**		Version 2: groups of GRPSZ + PARITY blocks, S_VBK$LOADGRP2 - up
**		to as many bad DATA blocks as good parity rows rebuilt by
**		Reed-Solomon (VBKRS.C), headers too; a row left over checks the
**		repair, a forged row is passed over; the event PARITY.
**
**	X01-11		 5-OCT-2026	RRL
**		A stream of several volumes: the VHDR of a further one ends the volume
**		in hand and begins the next (SBASE, where each begins); volumes
**		skipped are missing.
**
**	X01-08		 5-OCT-2026	RRL
**		"-": a saveset read from a pipe - forward only, block by block, its
**		TRAILER taken where it comes; no seek; VBK$RD_SETKEY puts the
**		reader back within the first group, which it still holds.
**		The TAGs and the decryption of a group on the cores of the pool.
**		No printf: the name of volume 1 by VBK$STRPUT.
**
**	X01-06		 5-OCT-2026	RRL
**		Encrypted savesets (format.md 6.10): EDATA blocks checked by
**		their TAG before the repair, the repaired one after it, then
**		decrypted; the ETRAILER opened by VBK$RD_SETKEY, which also
**		puts the SUMMARY of the stream in place of the short one of
**		the VHDR.
**
**	X01-05		 4-OCT-2026	RRL
**		VBK$RD_ADDVOL: a volume found by the user (a WCX plugin asks).
**
**	X01-04		 4-OCT-2026	RRL
**		The volumes are opened, read and measured through VBKOS.H
**		(VBK$OS_OPEN, _PREAD, _FSIZE, _CLOSE): the reader builds on
**		Windows too.
**
**	X01-03		 3-OCT-2026	RRL
**		The volumes are read SEQUENTIAL and the pages of a group are
**		dropped once it is read; VBK$RD_SEEK takes the group in hand
**		as it is; VBK$RD_PROBE leaves a cold file cold.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>

#include	"vbkrd.h"
#include	"vbklz4.h"
#include	"vbkos.h"

#define	VBK$K_RECINI	(VBK$K_MAXDATA + VBK$K_DATAHDR)	/* First allocation of the record buffer */


static	void	s_vbk$bfree	(VBK$RCTX *a_ctx);

static	void	s_vbk$event	(
		VBK$RCTX *	a_ctx,
		int		a_ev,
		uint32_t	a_vol,
		uint64_t	a_blk
			)
{
	if ( a_ctx->evcb )
		a_ctx->evcb(a_ctx->evarg, a_ev, a_vol, a_blk);
}


/*
**  Read exactly <a_len> octets at <a_off>; a short read is padded with
**  zeros - such a block then fails its check, which is what it is.
*/
static	int	s_vbk$pread	(
		int		a_fd,
		uint8_t *	a_buf,
		size_t		a_len,
		uint64_t	a_off
			)
{
size_t	l_got = 0;
int64_t	l_rc;

	while ( l_got < a_len )
		{
		if ( 0 > (l_rc = vbk$os_pread(a_fd, a_buf + l_got, a_len - l_got, a_off + l_got)) )
			{
			if ( errno == EINTR )
				continue;

			break;
			}

		if ( !l_rc )
			break;

		l_got	+= (size_t) l_rc;
		}

	if ( l_got < a_len )
		memset(a_buf + l_got, 0, a_len - l_got);

	return	(l_got == a_len) ? STS$K_SUCCESS : STS$K_WARN;
}


/*
**  The same from a stream: forward only.  A place behind what has been
**  read cannot be had again (STS$K_ERROR); one ahead is reached by
**  reading over what lies between.  A short read is padded with zeros,
**  and the end of the stream is then known.
*/
static	int	s_vbk$sread	(
		VBK$RCTX *	a_ctx,
		uint8_t *	a_buf,
		size_t		a_len,
		uint64_t	a_off
			)
{
size_t	l_got = 0;
int64_t	l_rc;
uint8_t	l_skip [4096];

	if ( a_off < a_ctx->spos )
		{
		memset(a_buf, 0, a_len);

		return	STS$K_ERROR;
		}

	while ( a_ctx->spos < a_off )
		{
		size_t	l_n = ((a_off - a_ctx->spos) < sizeof(l_skip)) ? (size_t) (a_off - a_ctx->spos) : sizeof(l_skip);

		if ( 0 >= (l_rc = vbk$os_read(a_ctx->vols [0].fd, l_skip, l_n)) )
			{
			if ( (l_rc < 0) && (errno == EINTR) )
				continue;

			memset(a_buf, 0, a_len);

			return	STS$K_WARN;
			}

		a_ctx->spos += (uint64_t) l_rc;
		}

	while ( l_got < a_len )
		{
		if ( 0 > (l_rc = vbk$os_read(a_ctx->vols [0].fd, a_buf + l_got, a_len - l_got)) )
			{
			if ( errno == EINTR )
				continue;

			break;
			}

		if ( !l_rc )
			break;

		l_got	+= (size_t) l_rc;
		}

	a_ctx->spos += l_got;

	if ( l_got < a_len )
		memset(a_buf + l_got, 0, a_len - l_got);

	return	(l_got == a_len) ? STS$K_SUCCESS : STS$K_WARN;
}


/*
**  Read the VHDR of an open volume and judge it: version 1, the expected
**  volume number, and - but for volume 1 - the UUID of this saveset.
*/
static	int	s_vbk$vhdr	(
		VBK$RCTX *	a_ctx,
		int		a_fd,
		uint32_t	a_volno,
		uint8_t *	a_blk,
		VBK$BHDR *	a_hdr
			)
{
	s_vbk$pread(a_fd, a_blk, a_ctx->bsize, 0);

	if ( !(1 & vbk$blk_check(a_blk, a_ctx->bsize, (a_volno > 1) ? a_ctx->ssuuid : NULL, a_hdr)) )
		return	STS$K_ERROR;

	if ( (a_hdr->type != VBK$K_BT_VHDR) || (a_hdr->volno != a_volno) )
		return	STS$K_ERROR;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Tell whether a file is volume 1 of a saveset: magic, version and
**	a VHDR as its first block.  The checksum is not looked at.
**
**  FORMAL PARAMETERS:
**
**	a_spec		The file
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- it is;
**	STS$K_WARN	- it is a file, but not a saveset;
**	STS$K_ERROR	- it cannot be opened (errno says why).
**--
*/
int	vbk$rd_probe	(
	const	char *		a_spec
			)
{
uint8_t		l_hdr [VBK$K_HDRSZ];
VBK$BHDR	l_bhdr;
uint64_t	l_size;
int		l_fd, l_isreg, l_status = STS$K_WARN;

	/* The standard input: taken for a saveset unseen - a look would eat what it is */
	if ( !strcmp(a_spec, "-") )
		return	STS$K_SUCCESS;

	if ( 0 > (l_fd = vbk$os_open(a_spec)) )
		return	STS$K_ERROR;

	if ( !vbk$os_fsize(l_fd, &l_size, &l_isreg) && l_isreg )
		{
		/* The look at the head must not make a cold file look warm to the save (VBK$OS_COLD) */
		int	l_cold = vbk$os_cold(l_fd, l_size);

		if ( (1 & s_vbk$pread(l_fd, l_hdr, sizeof(l_hdr), 0)) && (1 & vbk$bhdr_peek(l_hdr, &l_bhdr))
			&& (l_bhdr.type == VBK$K_BT_VHDR) && (l_bhdr.volno == 1) )
			l_status = STS$K_SUCCESS;

		if ( l_cold )
			vbk$os_drop(l_fd, 0, 0);
		}

	vbk$os_close(l_fd);

	return	l_status;
}


/*
**  The last block of the last volume, checked already: a TRAILER is taken
**  as it is, an ETRAILER kept whole until VBK$RD_SETKEY opens it
*/
static	void	s_vbk$trailer	(
		VBK$RCTX *	a_ctx,
	const	uint8_t *	a_blk,
	const	VBK$BHDR *	a_hdr
			)
{
	if ( !a_ctx->crypt && (a_hdr->type == VBK$K_BT_TRAILER) && (a_ctx->trailer = malloc(a_hdr->paylen + 1)) )
		{
		memcpy(a_ctx->trailer, a_blk + VBK$K_HDRSZ, a_hdr->paylen);
		a_ctx->trllen	= a_hdr->paylen;
		a_ctx->trlblk	= a_hdr->blkno;
		}
	else if ( a_ctx->crypt && (a_hdr->type == VBK$K_BT_ETRAILER) && (a_ctx->trlraw = malloc(a_ctx->bsize)) )
		{
		memcpy(a_ctx->trlraw, a_blk, a_ctx->bsize);
		a_ctx->trlblk	= a_hdr->blkno;
		}
}


/*
**  The end of the groups of a volume: the TRAILER, when there is one, is
**  not part of them
*/
static	uint64_t	s_vbk$volend	(
		VBK$RCTX *	a_ctx,
		uint32_t	a_volno
			)
{
VBK$RVOL *	l_vol = &a_ctx->vols [a_volno - 1];

	if ( (a_ctx->trailer || a_ctx->trlraw) && (a_volno == a_ctx->nvols) && l_vol->nblk )
		return	l_vol->nblk - 1;

	return	l_vol->nblk;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Open a saveset: volume 1, every further volume that can be found,
**	the TRAILER of the last one.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context, zeroed by the caller beforehand
**	a_spec		Name of volume 1
**	a_evcb		Event callback, NULL - none
**	a_evarg		Its argument
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Allocates the buffers of the context; VBK$RD_CLOSE releases them.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the saveset is open and positioned at its start;
**	STS$K_WARN	- the file is not a saveset of version 1;
**	STS$K_ERROR	- volume 1 cannot be read, <a_ctx->err> is the errno;
**	STS$K_FATAL	- no memory.
**--
*/
int	vbk$rd_open	(
		VBK$RCTX *	a_ctx,
	const	char *		a_spec,
		void		(*a_evcb) (void *, int, uint32_t, uint64_t),
		void *		a_evarg
			)
{
uint8_t		l_hdr [VBK$K_HDRSZ], *l_blk = NULL;
char		l_volspec [VBK$K_SZ_SPEC];
VBK$BHDR	l_bhdr;
uint64_t	l_size = 0;
uint32_t	l_pos = 0, l_vlen, l_reclen, l_miss = 0;
uint16_t	l_tag;
const uint8_t *	l_val;
int		l_fd, l_isreg = 0;

	vbk$strput(a_ctx->spec, sizeof(a_ctx->spec), a_spec);

	a_ctx->evcb	= a_evcb;
	a_ctx->evarg	= a_evarg;
	a_ctx->isstream	= !strcmp(a_spec, "-");

	if ( 0 > (l_fd = a_ctx->isstream ? vbk$os_stdin() : vbk$os_open(a_spec)) )
		{
		a_ctx->err	= errno;

		return	STS$K_ERROR;
		}

	/* A stream is read through VOLS [0] from the start: it needs its descriptor at once */
	if ( a_ctx->isstream && !(a_ctx->vols = calloc(VBK$K_MAXVOL, sizeof(VBK$RVOL))) )
		{
		a_ctx->err	= ENOMEM;

		return	STS$K_FATAL;
		}

	if ( a_ctx->isstream )
		a_ctx->vols [0].fd = l_fd;

	/* The block size comes from the first header; nothing else is trusted before the checksum */
	if ( !(1 & (a_ctx->isstream ? s_vbk$sread(a_ctx, l_hdr, sizeof(l_hdr), 0) : s_vbk$pread(l_fd, l_hdr, sizeof(l_hdr), 0)))
		|| !(1 & vbk$bhdr_peek(l_hdr, &l_bhdr))
		|| (l_bhdr.bsize < VBK$K_MINBSZ) || (l_bhdr.bsize > VBK$K_MAXBSZ) || (l_bhdr.bsize % VBK$K_BSZALIGN) )
		{
		vbk$os_close(l_fd);

		return	STS$K_WARN;
		}

	a_ctx->bsize	= l_bhdr.bsize;
	a_ctx->psize	= l_bhdr.bsize - VBK$K_HDRSZ;

	if ( (!a_ctx->vols && !(a_ctx->vols = calloc(VBK$K_MAXVOL, sizeof(VBK$RVOL)))) || !(l_blk = malloc(a_ctx->bsize)) )
		{
		vbk$os_close(l_fd);
		free(l_blk);
		a_ctx->err	= ENOMEM;

		return	STS$K_FATAL;
		}

	for ( uint32_t i = 0; i < VBK$K_MAXVOL; i++ )
		a_ctx->vols [i].fd = -1;

	a_ctx->vols [0].fd = a_ctx->isstream ? l_fd : -1;

	/* A stream: the header read is the head of the VHDR, the rest follows it */
	if ( a_ctx->isstream )
		{
		memcpy(l_blk, l_hdr, sizeof(l_hdr));
		s_vbk$sread(a_ctx, l_blk + sizeof(l_hdr), a_ctx->bsize - sizeof(l_hdr), sizeof(l_hdr));

		if ( !(1 & vbk$blk_check(l_blk, a_ctx->bsize, NULL, &l_bhdr)) || (l_bhdr.type != VBK$K_BT_VHDR) || (l_bhdr.volno != 1) )
			{
			a_ctx->vols [0].fd = -1;
			free(l_blk);

			return	STS$K_WARN;
			}
		}
	else if ( !(1 & s_vbk$vhdr(a_ctx, l_fd, 1, l_blk, &l_bhdr)) )
		{
		vbk$os_close(l_fd);
		free(l_blk);

		return	STS$K_WARN;
		}

	memcpy(a_ctx->ssuuid, l_bhdr.ssuuid, VBK$K_UUIDSZ);

	/* The VHDR payload is a whole SUMMARY record */
	l_reclen	= vbk$get32(l_blk + VBK$K_HDRSZ + 4);

	if ( (vbk$get16(l_blk + VBK$K_HDRSZ) != VBK$K_RT_SUMMARY) || ((VBK$K_RECHDR + l_reclen) > l_bhdr.paylen)
		|| !(a_ctx->summary = malloc(l_reclen + 1)) )
		{
		vbk$os_close(l_fd);
		free(l_blk);

		return	STS$K_WARN;
		}

	memcpy(a_ctx->summary, l_blk + VBK$K_HDRSZ + VBK$K_RECHDR, l_reclen);
	a_ctx->sumlen	= l_reclen;

	{
	uint32_t	l_cipher = 0, l_kdf = 0, l_nsalt = 0, l_ncheck = 0;

	while ( 1 & vbk$tlv_next(a_ctx->summary, a_ctx->sumlen, &l_pos, &l_tag, &l_vlen, &l_val) )
		switch ( l_tag )
			{
			case VBK$K_TAG_GROUPSIZE:
				a_ctx->grpsz = (uint32_t) vbk$tlv_getu(l_vlen, l_val);
				break;

			case VBK$K_TAG_PARITY:
				a_ctx->parity = (uint32_t) vbk$tlv_getu(l_vlen, l_val);
				break;

			case VBK$K_TAG_CIPHER:
				a_ctx->crypt	= 1;
				l_cipher	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);
				break;

			case VBK$K_TAG_KDF:
				l_kdf	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);
				break;

			case VBK$K_TAG_KDFITER:
				a_ctx->kdfiter = (uint32_t) vbk$tlv_getu(l_vlen, l_val);
				break;

			case VBK$K_TAG_SALT:
				if ( (l_nsalt = l_vlen) == VBK$K_SALTSZ )
					memcpy(a_ctx->salt, l_val, VBK$K_SALTSZ);
				break;

			case VBK$K_TAG_KEYCHECK:
				if ( (l_ncheck = l_vlen) == VBK$K_KEYSZ )
					memcpy(a_ctx->keycheck, l_val, VBK$K_KEYSZ);
				break;
			}

	a_ctx->dtype	= a_ctx->crypt ? VBK$K_BT_EDATA : VBK$K_BT_DATA;
	a_ctx->cap	= a_ctx->crypt ? (a_ctx->psize - VBK$K_TAGSZ) : a_ctx->psize;

	/* An algorithm not known here: nothing of the saveset can be read, and that is said at once */
	if ( a_ctx->crypt && ((l_cipher != VBK$K_CIPHER_CC20HS) || (l_kdf != VBK$K_KDF_PBKDF2) || (a_ctx->kdfiter < VBK$K_KDFMIN)
		|| (l_nsalt != VBK$K_SALTSZ) || (l_ncheck != VBK$K_KEYSZ)) )
		{
		vbk$os_close(l_fd);
		free(l_blk);
		a_ctx->err	= ENOTSUP;

		return	STS$K_ERROR;
		}
	}

	/* Version 2: parity blocks, as many as the SUMMARY says (format.md 4.1); version 1: the XOR block; version 3: either */
	a_ctx->version	= l_bhdr.version;

	if ( (a_ctx->version == VBK$K_VERSION) || ((a_ctx->version == VBK$K_VERSION3) && (a_ctx->parity < 2)) )
		a_ctx->parity	= 1;
	else if ( (a_ctx->parity < 2) || (a_ctx->parity > VBK$K_MAXPAR) || !a_ctx->grpsz )
		a_ctx->grpsz	= VBK$K_MAXGRP + 1;

	vbk$rs_init();

	if ( a_ctx->grpsz > VBK$K_MAXGRP )
		{
		vbk$os_close(l_fd);
		free(l_blk);

		return	STS$K_WARN;
		}

	/* A stream has no size: its end is where its TRAILER comes, or where it stops */
	if ( a_ctx->isstream )
		l_size	= (uint64_t) VBK$K_STREAMBLK * a_ctx->bsize;
	else	{
		vbk$os_fsize(l_fd, &l_size, &l_isreg);
		vbk$os_seq(l_fd);
		}

	a_ctx->vols [0].fd	 = l_fd;
	a_ctx->vols [0].firstblk = l_bhdr.blkno;
	a_ctx->vols [0].nblk	 = l_size / a_ctx->bsize;
	a_ctx->nvols		 = 1;

	/*
	**  The further volumes.  A missing name does not end the search at
	**  once: with volume 3 lost, volumes 4 and on are still worth having.
	*/
	for ( uint32_t l_volno = 2; !a_ctx->isstream && (l_volno <= VBK$K_MAXVOL) && (l_miss < VBK$K_VOLGAP); l_volno++ )
		{
		if ( !(1 & vbk$volspec(a_spec, l_volno, l_volspec, sizeof(l_volspec))) || (0 > (l_fd = vbk$os_open(l_volspec))) )
			{
			l_miss++;
			continue;
			}

		if ( !(1 & s_vbk$vhdr(a_ctx, l_fd, l_volno, l_blk, &l_bhdr)) )
			{
			s_vbk$event(a_ctx, VBK$K_EV_WRONGVOL, l_volno, 0);
			vbk$os_close(l_fd);
			l_miss++;
			continue;
			}

		l_size	= 0;
		vbk$os_fsize(l_fd, &l_size, &l_isreg);
		vbk$os_seq(l_fd);

		a_ctx->vols [l_volno - 1].fd	   = l_fd;
		a_ctx->vols [l_volno - 1].firstblk = l_bhdr.blkno;
		a_ctx->vols [l_volno - 1].nblk	   = l_size / a_ctx->bsize;
		a_ctx->nvols			   = l_volno;
		l_miss				   = 0;
		}

	/* The TRAILER, last block of the last volume */
	{
	VBK$RVOL *	l_vol = &a_ctx->vols [a_ctx->nvols - 1];

	/* A stream gives its TRAILER at its end, when the groups are read (S_VBK$LOADGRP) */
	if ( !a_ctx->isstream && (l_vol->nblk > 1) )
		{
		s_vbk$pread(l_vol->fd, l_blk, a_ctx->bsize, (l_vol->nblk - 1) * a_ctx->bsize);

		if ( 1 & vbk$blk_check(l_blk, a_ctx->bsize, a_ctx->ssuuid, &l_bhdr) )
			s_vbk$trailer(a_ctx, l_blk, &l_bhdr);
		}
	}

	free(l_blk);

	a_ctx->recsz	= VBK$K_RECINI;

	if ( !(a_ctx->gbuf = vbk$os_balloc((size_t) (a_ctx->grpsz + a_ctx->parity) * a_ctx->bsize)) || !(a_ctx->rec = malloc(a_ctx->recsz)) )
		{
		a_ctx->err	= ENOMEM;

		return	STS$K_FATAL;
		}

	return	vbk$rd_rewind(a_ctx);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Take in a volume that was not found beside volume 1 - a file
**	manager asked the user for it.  It must be volume <a_volno> of this
**	saveset; a volume that becomes the last one gives the TRAILER, if
**	the saveset has none yet.  To be called before the first record is
**	read (it rewinds).
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The saveset, open
**	a_volno		The number of the volume, from 2
**	a_spec		Its file
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- taken in;
**	STS$K_ERROR	- it cannot be opened, or is not that volume.
**--
*/
int	vbk$rd_addvol	(
		VBK$RCTX *	a_ctx,
		uint32_t	a_volno,
	const	char *		a_spec
			)
{
VBK$BHDR	l_bhdr;
uint8_t *	l_blk;
uint64_t	l_size = 0;
int		l_fd, l_isreg = 0;

	if ( (a_volno < 2) || (a_volno > VBK$K_MAXVOL) || (a_ctx->vols [a_volno - 1].fd >= 0) )
		return	STS$K_ERROR;

	if ( !(l_blk = malloc(a_ctx->bsize)) )
		return	STS$K_ERROR;

	if ( 0 > (l_fd = vbk$os_open(a_spec)) )
		{
		free(l_blk);

		return	STS$K_ERROR;
		}

	if ( !(1 & s_vbk$vhdr(a_ctx, l_fd, a_volno, l_blk, &l_bhdr)) )
		{
		vbk$os_close(l_fd);
		free(l_blk);

		return	STS$K_ERROR;
		}

	vbk$os_fsize(l_fd, &l_size, &l_isreg);

	a_ctx->vols [a_volno - 1].fd	   = l_fd;
	a_ctx->vols [a_volno - 1].firstblk = l_bhdr.blkno;
	a_ctx->vols [a_volno - 1].nblk	   = l_size / a_ctx->bsize;

	if ( a_volno > a_ctx->nvols )
		a_ctx->nvols	= a_volno;

	/* The last volume now, and no TRAILER yet: its last block may be it */
	if ( !a_ctx->trailer && !a_ctx->trlraw && (a_volno == a_ctx->nvols) && (a_ctx->vols [a_volno - 1].nblk > 1) )
		{
		s_vbk$pread(l_fd, l_blk, a_ctx->bsize, (a_ctx->vols [a_volno - 1].nblk - 1) * a_ctx->bsize);

		if ( 1 & vbk$blk_check(l_blk, a_ctx->bsize, a_ctx->ssuuid, &l_bhdr) )
			s_vbk$trailer(a_ctx, l_blk, &l_bhdr);
		}

	free(l_blk);

	return	vbk$rd_rewind(a_ctx);
}


/*
**  The ETRAILER kept by S_VBK$TRAILER, opened with the keys; its TAG
**  failing is said (BADTAG) and the saveset goes on without a catalog
*/
static	int	s_vbk$opentrl	(
		VBK$RCTX *	a_ctx
			)
{
VBK$BHDR	l_bhdr;

	if ( (1 & vbk$blk_check(a_ctx->trlraw, a_ctx->bsize, a_ctx->ssuuid, &l_bhdr))
		&& (1 & vbk$crp_open(&a_ctx->keys, &l_bhdr, a_ctx->trlraw + VBK$K_HDRSZ, a_ctx->psize))
		&& (a_ctx->trailer = malloc(l_bhdr.paylen + 1)) )
		{
		memcpy(a_ctx->trailer, a_ctx->trlraw + VBK$K_HDRSZ, l_bhdr.paylen);
		a_ctx->trllen	= l_bhdr.paylen;

		return	STS$K_SUCCESS;
		}

	s_vbk$event(a_ctx, VBK$K_EV_BADTAG, a_ctx->nvols, a_ctx->trlblk);

	return	STS$K_ERROR;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The passphrase of an encrypted saveset: the keys are derived from
**	it and the SALT of the VHDR and judged by its KEYCHECK; then the
**	ETRAILER is opened and the SUMMARY of the stream - the complete
**	one - is put in place of the short SUMMARY of the VHDR.  To be
**	called after VBK$RD_OPEN, before the first record is read.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The saveset, open, CRYPT set
**	a_pass		The passphrase, its bytes as they are
**	a_plen		Its length
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the passphrase is right; the saveset is at its start;
**	STS$K_ERROR	- it is not;
**	STS$K_WARN	- it is right, but the TRAILER fails its TAG: the
**			  saveset is read in sequential mode only.
**--
*/
int	vbk$rd_setkey	(
		VBK$RCTX *	a_ctx,
	const	void *		a_pass,
		size_t		a_plen
			)
{
VBK$KEYS	l_keys;
int		l_status = STS$K_SUCCESS;

	if ( !a_ctx->crypt )
		return	STS$K_SUCCESS;

	vbk$crp_derive(&l_keys, a_pass, a_plen, a_ctx->salt, a_ctx->kdfiter);

	if ( !vbk$crp_equal(l_keys.check, a_ctx->keycheck, VBK$K_KEYSZ) )
		{
		vbk$crp_wipe(&l_keys, sizeof(l_keys));

		return	STS$K_ERROR;
		}

	a_ctx->keys	= l_keys;
	a_ctx->haskey	= 1;
	vbk$crp_wipe(&l_keys, sizeof(l_keys));

	if ( a_ctx->trlraw && !a_ctx->trailer && !(1 & s_vbk$opentrl(a_ctx)) )
		l_status = STS$K_WARN;

	/*
	**  The first record of the stream is the complete SUMMARY.  It is read
	**  quietly: whatever is lost on the way is reported when the caller
	**  reads it for itself.
	*/
	if ( a_ctx->isstream )
		{
		/*
		**  A stream cannot be read twice: the events of the first group are
		**  reported now, once, and the reader is put back to the start of
		**  that group, which it still holds - decrypted.
		*/
		const uint8_t *	l_body;
		uint32_t	l_len;
		uint16_t	l_type;
		uint8_t *	l_sum;

		if ( (STS$K_SUCCESS == vbk$rd_next(a_ctx, &l_type, &l_body, &l_len, NULL)) && (l_type == VBK$K_RT_SUMMARY)
			&& !a_ctx->resync && (l_sum = malloc(l_len + 1)) )
			{
			memcpy(l_sum, l_body, l_len);
			free(a_ctx->summary);
			a_ctx->summary	= l_sum;
			a_ctx->sumlen	= l_len;
			}

		vbk$rd_rewind(a_ctx);

		return	l_status;
		}

	{
	void		(*l_evcb) (void *, int, uint32_t, uint64_t) = a_ctx->evcb;
	uint64_t	l_nrep = a_ctx->nrepaired, l_nlost = a_ctx->nlost;
	const uint8_t *	l_body;
	uint32_t	l_len;
	uint16_t	l_type;
	uint8_t *	l_sum;

	a_ctx->evcb	= NULL;

	if ( (STS$K_SUCCESS == vbk$rd_next(a_ctx, &l_type, &l_body, &l_len, NULL)) && (l_type == VBK$K_RT_SUMMARY)
		&& !a_ctx->resync && (l_sum = malloc(l_len + 1)) )
		{
		memcpy(l_sum, l_body, l_len);
		free(a_ctx->summary);
		a_ctx->summary	= l_sum;
		a_ctx->sumlen	= l_len;
		}

	a_ctx->evcb	= l_evcb;
	a_ctx->nrepaired = l_nrep;
	a_ctx->nlost	= l_nlost;
	a_ctx->gdata	= 0;
	}

	vbk$rd_rewind(a_ctx);

	return	l_status;
}


void	vbk$rd_close	(
		VBK$RCTX *	a_ctx
			)
{
	/* A stream: all its volumes are the one descriptor */
	if ( a_ctx->vols )
		for ( uint32_t i = 0; i < (a_ctx->isstream ? 1 : a_ctx->nvols); i++ )
			if ( a_ctx->vols [i].fd >= 0 )
				vbk$os_close(a_ctx->vols [i].fd);

	free(a_ctx->vols);
	free(a_ctx->summary);
	free(a_ctx->trailer);
	free(a_ctx->trlraw);
	free(a_ctx->gbuf);
	free(a_ctx->rec);
	free(a_ctx->solbuf);
	s_vbk$bfree(a_ctx);
	a_ctx->solgap	= 0;

	vbk$crp_wipe(a_ctx, sizeof(*a_ctx));
}


/*
**  Back to the first record of the saveset
*/
int	vbk$rd_rewind	(
		VBK$RCTX *	a_ctx
			)
{
	/* A SOLID being read is left behind, and what was read ahead */
	a_ctx->sollen = a_ctx->solpos = 0;
	a_ctx->solgap = 0;
	a_ctx->bn = a_ctx->bpos = 0;
	a_ctx->bend = 0;

	/*
	**  A stream goes back only within the first group, while it is still
	**  in hand: the SUMMARY taken by VBK$RD_SETKEY; before anything has
	**  been read beyond the VHDR it is at its start anyway.
	*/
	if ( a_ctx->isstream && (a_ctx->spos > a_ctx->bsize) )
		{
		if ( !a_ctx->gdata || (a_ctx->gpos != 1) || (a_ctx->gvol != 1) )
			return	STS$K_ERROR;

		a_ctx->gnext	= 0;
		a_ctx->pay	= NULL;
		a_ctx->paylen	= a_ctx->payoff = 0;
		a_ctx->gap	= a_ctx->eof = a_ctx->pendrs = 0;
		a_ctx->rewound	= 1;

		return	STS$K_SUCCESS;
		}

	a_ctx->curvol	= 1;
	a_ctx->curpos	= 1;
	a_ctx->gdata	= a_ctx->gnext = 0;
	a_ctx->pay	= NULL;
	a_ctx->paylen	= a_ctx->payoff = 0;
	a_ctx->gap	= a_ctx->eof = a_ctx->pendrs = 0;

	return	STS$K_SUCCESS;
}


/*
**  One block of the group in hand, a job of the pool: its TAG checked
**  (into GTAG) or, after the repair, the block decrypted.  Jobs touch
**  only their own block; events are left to the caller.
*/
typedef struct vbk_grpjob_t
{
	VBK$RCTX *	ctx;
	int		decrypt;
} VBK$GRPJOB;

static	void	s_vbk$tagjob	(
		void *		a_arg,
		uint32_t	a_i
			)
{
VBK$GRPJOB *	l_j = (VBK$GRPJOB *) a_arg;
VBK$RCTX *	l_c = l_j->ctx;
uint8_t *	l_pay = l_c->gbuf + (size_t) a_i * l_c->bsize + VBK$K_HDRSZ;

	if ( !l_c->gok [a_i] )
		return;

	if ( l_j->decrypt )
		vbk$crp_decrypt(&l_c->keys, &l_c->ghdr [a_i], l_pay);
	else	l_c->gtag [a_i] = (uint8_t) vbk$crp_check(&l_c->keys, &l_c->ghdr [a_i], l_pay, l_c->psize);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The rest of S_VBK$LOADGRP for a saveset of version 2: a group of d
**	DATA blocks and m parity blocks (format.md 4.1) - where its parity
**	blocks are, the TAGs, the repair of up to as many bad DATA blocks as
**	there are good parity rows, the payloads and the header parity
**	alike, the rebuilt headers checked as good ones are.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context, the group read into GBUF and judged
**	a_vol		Its volume
**	a_n		Blocks of the group read
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- a group is in GBUF, GDATA DATA blocks of it.
**--
*/
static	int	s_vbk$loadgrp2	(VBK$RCTX *a_ctx, VBK$RVOL *a_vol, uint32_t a_n);

/*
**  The good DATA blocks of a group as the writer made them past PAYLEN:
**  zeros up to the TAG.  What lies there carries nothing, no TAG covers
**  it; a byte changed there would make the parity disagree, and nothing
**  of the group be rebuilt.
*/
static	void	s_vbk$canon	(
		VBK$RCTX *	a_ctx,
		uint32_t	a_n
			)
{
uint32_t	l_end = a_ctx->psize - (a_ctx->crypt ? VBK$K_TAGSZ : 0);

	for ( uint32_t i = 0; i < a_n; i++ )
		if ( a_ctx->gok [i] && (a_ctx->ghdr [i].type == a_ctx->dtype) && (a_ctx->ghdr [i].paylen < l_end) )
			memset(a_ctx->gbuf + (size_t) i * a_ctx->bsize + VBK$K_HDRSZ + a_ctx->ghdr [i].paylen, 0, l_end - a_ctx->ghdr [i].paylen);
}

static	int	s_vbk$loadgrp2	(
		VBK$RCTX *	a_ctx,
		VBK$RVOL *	a_vol,
		uint32_t	a_n
			)
{
uint32_t	l_m = a_ctx->parity, l_d = (uint32_t) -1, l_bad = 0, l_npok = 0;
uint8_t *	l_dp [VBK$K_MAXGRP];
const uint8_t *	l_pp [VBK$K_MAXPAR];
uint8_t *	l_hdp [VBK$K_MAXGRP];
const uint8_t *	l_hpp [VBK$K_MAXPAR];
uint8_t		l_hv [VBK$K_MAXGRP + VBK$K_MAXPAR] [VBK$K_HPARSZ], l_dok [VBK$K_MAXGRP], l_pok [VBK$K_MAXPAR];
int		l_rc;

	/*
	**  How many DATA blocks: any good parity block says it (n in GINDEX,
	**  its row in the high octet, so it stands at n + row).  None good: a
	**  full group has GRPSZ; a short one - the last of a volume - ends with
	**  m parity blocks, unless the save was cut short before them and its
	**  last block is a good DATA block.
	*/
	for ( uint32_t i = 0; i < a_n; i++ )
		{
		VBK$BHDR *	l_h = &a_ctx->ghdr [i];
		uint32_t	l_row = l_h->gindex >> 8, l_cnt = l_h->gindex & 0xFF;

		if ( a_ctx->gok [i] && (((l_h->type == VBK$K_BT_XOR) && !l_row) || ((l_h->type == VBK$K_BT_PARITY) && l_row && (l_row < l_m)))
			&& l_cnt && (l_cnt <= a_ctx->grpsz) && ((l_cnt + l_row) == i) )
			{
			l_d	= l_cnt;
			break;
			}
		}

	if ( l_d == (uint32_t) -1 )
		{
		if ( a_n == (a_ctx->grpsz + l_m) )
			l_d	= a_ctx->grpsz;
		else if ( a_ctx->gok [a_n - 1] && (a_ctx->ghdr [a_n - 1].type == a_ctx->dtype) )
			l_d	= a_n;
		else	l_d	= (a_n > l_m) ? (a_n - l_m) : a_n;
		}

	if ( l_d > a_n )
		l_d	= a_n;

	/* The parity rows there are, each where its row says */
	for ( uint32_t j = 0; j < l_m; j++ )
		{
		uint32_t	l_i = l_d + j;
		VBK$BHDR *	l_h = &a_ctx->ghdr [l_i < a_n ? l_i : 0];

		l_pok [j] = (l_i < a_n) && a_ctx->gok [l_i] && (l_h->type == (j ? VBK$K_BT_PARITY : VBK$K_BT_XOR))
			&& (l_h->gindex == (uint16_t) (l_d | (j << 8)));

		if ( l_i < a_n )
			a_ctx->gok [l_i] = 0;

		l_pp [j]  = a_ctx->gbuf + (size_t) (l_i < a_n ? l_i : 0) * a_ctx->bsize + VBK$K_HDRSZ;
		l_hpp [j] = l_hv [a_ctx->grpsz + j];

		vbk$put32(l_hv [a_ctx->grpsz + j], l_pok [j] ? l_h->recoff : 0);
		vbk$put32(l_hv [a_ctx->grpsz + j] + 4, l_pok [j] ? l_h->paylen : 0);

		l_npok	+= l_pok [j];
		}

	a_ctx->gdata	= l_d;

	for ( uint32_t i = 0; i < l_d; i++ )
		if ( a_ctx->gok [i] && ((a_ctx->ghdr [i].type != a_ctx->dtype) || (a_ctx->ghdr [i].gindex != i)) )
			a_ctx->gok [i] = 0;

	/* A good CRC and a wrong TAG: a bad block all the same (S_VBK$LOADGRP) */
	if ( a_ctx->crypt )
		{
		VBK$GRPJOB	l_job = { a_ctx, 0 };

		vbk$crp_par(l_d, s_vbk$tagjob, &l_job);

		for ( uint32_t i = 0; i < l_d; i++ )
			if ( a_ctx->gok [i] && !a_ctx->gtag [i] )
				{
				a_ctx->gok [i] = 0;
				s_vbk$event(a_ctx, VBK$K_EV_BADTAG, a_ctx->curvol, a_ctx->ghdr [i].blkno);
				}
		}

	for ( uint32_t i = 0; i < l_d; i++ )
		{
		l_dok [i] = a_ctx->gok [i];
		l_dp [i]  = a_ctx->gbuf + (size_t) i * a_ctx->bsize + VBK$K_HDRSZ;
		l_hdp [i] = l_hv [i];

		vbk$put32(l_hv [i], l_dok [i] ? a_ctx->ghdr [i].recoff : 0);
		vbk$put32(l_hv [i] + 4, l_dok [i] ? a_ctx->ghdr [i].paylen : 0);

		l_bad	+= !l_dok [i];
		}

	if ( l_bad )
		s_vbk$canon(a_ctx, l_d);

	/*
	**  The repair: all the good rows first; when the result does not hold
	**  - a surplus row disagrees, a rebuilt header makes no sense, a TAG
	**  fails - once more without each good row in turn, so that one
	**  parity block whose CRC is right and whose bytes are not is passed
	**  over while enough rows are left.
	*/
	for ( int l_skip = -1, l_done = 0, l_forged = 0; l_bad && (l_bad <= l_npok) && !l_done && (l_skip < (int) l_m); l_skip++ )
		{
		uint8_t		l_try [VBK$K_MAXPAR];
		uint32_t	l_nt = 0;
		int		l_allok = 1;

		if ( (l_skip >= 0) && !l_pok [l_skip] )
			continue;

		memcpy(l_try, l_pok, sizeof(l_try));

		if ( l_skip >= 0 )
			l_try [l_skip] = 0;

		for ( uint32_t j = 0; j < l_m; j++ )
			l_nt	+= l_try [j];

		if ( l_nt >= l_bad )
			{
			/* The header parity first: it is small, and a group it cannot give back is not worth the payloads */
			l_rc	= vbk$rs_repair(l_d, l_m, l_hdp, l_dok, l_hpp, l_try, VBK$K_HPARSZ);

			if ( l_rc == STS$K_SUCCESS )
				l_rc	= vbk$rs_repair(l_d, l_m, l_dp, l_dok, l_pp, l_try, a_ctx->psize);

			l_forged |= (l_rc == STS$K_WARN);
			l_allok	= (l_rc == STS$K_SUCCESS);

			for ( uint32_t i = 0; l_allok && (i < l_d); i++ )
				{
				VBK$BHDR *	l_h = &a_ctx->ghdr [i];
				uint8_t *	l_blk = a_ctx->gbuf + (size_t) i * a_ctx->bsize;

				if ( l_dok [i] )
					continue;

				memset(l_h, 0, sizeof(*l_h));

				l_h->version	= a_ctx->version;
				l_h->bsize	= a_ctx->bsize;
				l_h->type	= a_ctx->dtype;
				l_h->gindex	= (uint16_t) i;
				l_h->blkno	= a_vol->firstblk + a_ctx->curpos + i;
				l_h->volno	= a_ctx->curvol;
				l_h->recoff	= vbk$get32(l_hv [i]);
				l_h->paylen	= vbk$get32(l_hv [i] + 4);
				l_h->prvrecoff	= i ? a_ctx->ghdr [i - 1].recoff : VBK$K_NONE;
				l_h->prvpaylen	= i ? a_ctx->ghdr [i - 1].paylen : 0;

				memcpy(l_h->ssuuid, a_ctx->ssuuid, VBK$K_UUIDSZ);

				if ( (l_h->paylen > a_ctx->cap) || ((l_h->recoff != VBK$K_NONE) && (l_h->recoff >= l_h->paylen))
					|| (a_ctx->crypt && !vbk$crp_check(&a_ctx->keys, l_h, l_blk + VBK$K_HDRSZ, a_ctx->psize)) )
					l_allok	= 0;
				}

			if ( l_allok )
				{
				l_done	= 1;

				for ( uint32_t i = 0; i < l_d; i++ )
					{
					if ( l_dok [i] )
						continue;

					vbk$bhdr_put(&a_ctx->ghdr [i], a_ctx->gbuf + (size_t) i * a_ctx->bsize);

					a_ctx->gok [i] = 1;
					a_ctx->nrepaired++;
					l_bad--;

					s_vbk$event(a_ctx, VBK$K_EV_REPAIRED, a_ctx->curvol, a_ctx->ghdr [i].blkno);
					}
				}
			}

		/* Every way tried, and a surplus row disagreed somewhere: said once for the group */
		if ( !l_done && l_forged && ((l_skip + 1) == (int) l_m) )
			{
			a_ctx->nforged++;
			s_vbk$event(a_ctx, VBK$K_EV_PARITY, a_ctx->curvol, a_vol->firstblk + a_ctx->curpos);
			}
		}

	for ( uint32_t i = 0; l_bad && (i < l_d); i++ )
		if ( !a_ctx->gok [i] )
			{
			a_ctx->nlost++;
			s_vbk$event(a_ctx, VBK$K_EV_LOST, a_ctx->curvol, a_vol->firstblk + a_ctx->curpos + i);
			}

	/* Every check done, the repair too: the good blocks decrypted where they lie */
	if ( a_ctx->crypt )
		{
		VBK$GRPJOB	l_job = { a_ctx, 1 };

		vbk$crp_par(l_d, s_vbk$tagjob, &l_job);
		}

	a_ctx->gvol	= a_ctx->curvol;
	a_ctx->gpos	= a_ctx->curpos;
	a_ctx->curpos	+= a_n;
	a_ctx->gend	= a_ctx->curpos;
	a_ctx->gnext	= 0;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Read the group that begins at CURVOL/CURPOS, check every block of
**	it, rebuild one bad DATA block from the XOR block when that can be
**	done, and step CURPOS past it.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- a group is in GBUF, GDATA DATA blocks of it;
**	STS$K_WARN	- the end of the saveset.
**--
*/
static	int	s_vbk$loadgrp	(
		VBK$RCTX *	a_ctx
			)
{
VBK$RVOL *	l_vol;
uint64_t	l_end, l_n;
uint32_t	l_bad = 0, l_badidx = 0, l_xor = 0, l_hasxor = 0;
uint8_t *	l_blk;

	for ( ;; )
		{
		if ( a_ctx->curvol > a_ctx->nvols )
			{
			a_ctx->eof	= 1;

			return	STS$K_WARN;
			}

		l_vol	= &a_ctx->vols [a_ctx->curvol - 1];

		if ( l_vol->fd < 0 )
			{
			s_vbk$event(a_ctx, VBK$K_EV_MISSVOL, a_ctx->curvol, 0);

			a_ctx->gap	= 1;
			a_ctx->curvol++;
			a_ctx->curpos	= 1;
			continue;
			}

		l_end	= s_vbk$volend(a_ctx, a_ctx->curvol);

		if ( a_ctx->curpos >= l_end )
			{
			a_ctx->curvol++;
			a_ctx->curpos	= 1;
			continue;
			}

		break;
		}

	l_n	= a_ctx->grpsz ? (a_ctx->grpsz + a_ctx->parity) : 1;
	l_n	= ((l_end - a_ctx->curpos) < l_n) ? (l_end - a_ctx->curpos) : l_n;

	if ( a_ctx->isstream )
		{
		/*
		**  A stream, a block at a time: its end is not known before it
		**  comes.  A good TRAILER ends the groups - it is not taken into
		**  the one in hand - and so does the end of the stream.
		*/
		VBK$BHDR	l_th;
		uint64_t	l_i;
		int		l_newvol = 0;

		for ( l_i = 0; l_i < l_n; l_i++ )
			{
			uint8_t *	l_b = a_ctx->gbuf + (size_t) l_i * a_ctx->bsize;

			if ( STS$K_SUCCESS != s_vbk$sread(a_ctx, l_b, a_ctx->bsize, l_vol->sbase + (a_ctx->curpos + l_i) * a_ctx->bsize) )
				{
				l_vol->nblk = a_ctx->curpos + l_i;
				break;
				}

			/*
			**  The VHDR of a further volume: a stream carries the volumes back
			**  to back, and a volume begins at a group boundary (writer rule
			**  4).  It ends the volume in hand; volumes skipped are missing.
			*/
			if ( (1 & vbk$blk_check(l_b, a_ctx->bsize, a_ctx->ssuuid, &l_th)) && (l_th.type == VBK$K_BT_VHDR)
				&& (l_th.volno > a_ctx->curvol) && (l_th.volno <= VBK$K_MAXVOL) )
				{
				uint64_t	l_base = l_vol->sbase + (a_ctx->curpos + l_i) * a_ctx->bsize;

				l_vol->nblk = a_ctx->curpos + l_i;

				for ( uint32_t j = a_ctx->curvol + 1; j < l_th.volno; j++ )
					{
					a_ctx->vols [j - 1].fd	 = -1;
					a_ctx->vols [j - 1].nblk = 0;
					}

				a_ctx->vols [l_th.volno - 1].fd	      = l_vol->fd;
				a_ctx->vols [l_th.volno - 1].firstblk = l_th.blkno;
				a_ctx->vols [l_th.volno - 1].nblk     = VBK$K_STREAMBLK;
				a_ctx->vols [l_th.volno - 1].sbase    = l_base;
				a_ctx->nvols	= l_th.volno;
				l_newvol	= 1;
				break;
				}

			if ( (1 & vbk$blk_check(l_b, a_ctx->bsize, a_ctx->ssuuid, &l_th))
				&& ((l_th.type == VBK$K_BT_TRAILER) || (l_th.type == VBK$K_BT_ETRAILER))
				&& (l_th.blkno == (l_vol->firstblk + a_ctx->curpos + l_i)) )
				{
				s_vbk$trailer(a_ctx, l_b, &l_th);

				/* Encrypted, and the keys at hand: the TRAILER is opened now, there is no going back to it */
				if ( a_ctx->trlraw && a_ctx->haskey && !a_ctx->trailer )
					s_vbk$opentrl(a_ctx);

				l_vol->nblk = a_ctx->curpos + l_i + 1;
				break;
				}
			}

		l_n	= l_i;

		/* The volume in hand ended right where the next one begins: on to it */
		if ( !l_n && l_newvol )
			return	s_vbk$loadgrp(a_ctx);

		if ( !l_n )
			{
			a_ctx->curvol	= a_ctx->nvols + 1;
			a_ctx->eof	= 1;

			return	STS$K_WARN;
			}
		}
	else	{
		s_vbk$pread(l_vol->fd, a_ctx->gbuf, (size_t) (l_n * a_ctx->bsize), a_ctx->curpos * a_ctx->bsize);

		/* A saveset is read once: its pages would only push working data out of the cache */
		vbk$os_drop(l_vol->fd, a_ctx->curpos * a_ctx->bsize, l_n * a_ctx->bsize);
		}

	for ( uint32_t i = 0; i < l_n; i++ )
		{
		VBK$BHDR *	l_h = &a_ctx->ghdr [i];

		a_ctx->gok [i]	= (1 & vbk$blk_check(a_ctx->gbuf + (size_t) i * a_ctx->bsize, a_ctx->bsize, a_ctx->ssuuid, l_h))
				&& (l_h->blkno == (l_vol->firstblk + a_ctx->curpos + i)) && (l_h->volno == a_ctx->curvol)
				&& (l_h->version == a_ctx->version);
		}

	/* Version 2: groups of several parity blocks, repaired by Reed-Solomon */
	if ( a_ctx->grpsz && (a_ctx->parity > 1) )
		return	s_vbk$loadgrp2(a_ctx, l_vol, (uint32_t) l_n);

	/*
	**  Where the XOR block is.  A full group ends with it; a short one -
	**  the last of a volume - too, unless the save was cut short and the
	**  group never got one: then its last block is a good DATA block.
	*/
	if ( a_ctx->grpsz )
		{
		l_xor	= (uint32_t) l_n - 1;

		if ( l_n == (a_ctx->grpsz + 1) )
			l_hasxor = 1;
		else if ( a_ctx->gok [l_xor] )
			l_hasxor = (a_ctx->ghdr [l_xor].type == VBK$K_BT_XOR);
		else	l_hasxor = (l_n >= 2);

		if ( l_hasxor && a_ctx->gok [l_xor] && (a_ctx->ghdr [l_xor].type != VBK$K_BT_XOR) )
			a_ctx->gok [l_xor] = 0;
		}

	a_ctx->gdata	= (uint32_t) l_n - l_hasxor;

	for ( uint32_t i = 0; i < a_ctx->gdata; i++ )
		if ( a_ctx->gok [i] && (a_ctx->ghdr [i].type != a_ctx->dtype) )
			a_ctx->gok [i] = 0;

	/*
	**  A good CRC and a wrong TAG: changed on purpose - a bad block all the
	**  same, repairable as any.  The TAGs of the group are checked on the
	**  cores of the pool, when the utility has one; what failed is then
	**  reported here, in the order of the blocks.
	*/
	if ( a_ctx->crypt )
		{
		VBK$GRPJOB	l_job = { a_ctx, 0 };

		vbk$crp_par(a_ctx->gdata, s_vbk$tagjob, &l_job);

		for ( uint32_t i = 0; i < a_ctx->gdata; i++ )
			if ( a_ctx->gok [i] && !a_ctx->gtag [i] )
				{
				a_ctx->gok [i] = 0;
				s_vbk$event(a_ctx, VBK$K_EV_BADTAG, a_ctx->curvol, a_ctx->ghdr [i].blkno);
				}
		}

	for ( uint32_t i = 0; i < a_ctx->gdata; i++ )
		{

		if ( !a_ctx->gok [i] )
			{
			l_bad++;
			l_badidx = i;
			}
		}

	if ( l_bad )
		s_vbk$canon(a_ctx, a_ctx->gdata);

	/*
	**  One bad DATA block and a good XOR block that covers exactly this
	**  group: the payload is the XOR of all the others, the two fields of
	**  the header that cannot be derived are kept by the block after it.
	*/
	if ( (l_bad == 1) && l_hasxor && a_ctx->gok [l_xor] && (a_ctx->ghdr [l_xor].gindex == a_ctx->gdata) )
		{
		VBK$BHDR *	l_h = &a_ctx->ghdr [l_badidx], *l_s = &a_ctx->ghdr [l_badidx + 1];
		uint64_t *	l_d, *l_p;

		l_blk	= a_ctx->gbuf + (size_t) l_badidx * a_ctx->bsize;
		l_d	= (uint64_t *) (l_blk + VBK$K_HDRSZ);

		memcpy(l_d, a_ctx->gbuf + (size_t) l_xor * a_ctx->bsize + VBK$K_HDRSZ, a_ctx->psize);

		for ( uint32_t i = 0; i < a_ctx->gdata; i++ )
			{
			if ( i == l_badidx )
				continue;

			l_p	= (uint64_t *) (a_ctx->gbuf + (size_t) i * a_ctx->bsize + VBK$K_HDRSZ);

			for ( uint32_t j = 0; j < (a_ctx->psize / sizeof(uint64_t)); j++ )
				l_d [j] ^= l_p [j];
			}

		memset(l_h, 0, sizeof(*l_h));

		l_h->bsize	= a_ctx->bsize;
		l_h->type	= a_ctx->dtype;
		l_h->gindex	= (uint16_t) l_badidx;
		l_h->blkno	= l_vol->firstblk + a_ctx->curpos + l_badidx;
		l_h->volno	= a_ctx->curvol;
		l_h->recoff	= l_s->prvrecoff;
		l_h->paylen	= l_s->prvpaylen;
		l_h->prvrecoff	= VBK$K_NONE;

		memcpy(l_h->ssuuid, a_ctx->ssuuid, VBK$K_UUIDSZ);

		if ( (l_h->paylen <= a_ctx->cap) && ((l_h->recoff == VBK$K_NONE) || (l_h->recoff < l_h->paylen))
			&& (!a_ctx->crypt || vbk$crp_check(&a_ctx->keys, l_h, l_blk + VBK$K_HDRSZ, a_ctx->psize)) )
			{
			vbk$bhdr_put(l_h, l_blk);

			a_ctx->gok [l_badidx] = 1;
			a_ctx->nrepaired++;
			l_bad	= 0;

			s_vbk$event(a_ctx, VBK$K_EV_REPAIRED, a_ctx->curvol, l_h->blkno);
			}
		}

	for ( uint32_t i = 0; l_bad && (i < a_ctx->gdata); i++ )
		if ( !a_ctx->gok [i] )
			{
			a_ctx->nlost++;
			s_vbk$event(a_ctx, VBK$K_EV_LOST, a_ctx->curvol, l_vol->firstblk + a_ctx->curpos + i);
			}

	/* Every check done, the repair too: now the good blocks are decrypted where they lie, on the cores of the pool */
	if ( a_ctx->crypt )
		{
		VBK$GRPJOB	l_job = { a_ctx, 1 };

		vbk$crp_par(a_ctx->gdata, s_vbk$tagjob, &l_job);
		}

	a_ctx->gvol	= a_ctx->curvol;
	a_ctx->gpos	= a_ctx->curpos;
	a_ctx->curpos	+= l_n;
	a_ctx->gend	= a_ctx->curpos;
	a_ctx->gnext	= 0;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Make the payload of the next good DATA block the current one.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the stream goes on in it, PAYOFF is 0;
**	STS$K_INFO	- blocks have been lost before it: PAYOFF is its
**			  first record header, what was being assembled is
**			  to be abandoned;
**	STS$K_WARN	- the end of the saveset.
**--
*/
static	int	s_vbk$nextpay	(
		VBK$RCTX *	a_ctx
			)
{
VBK$BHDR *	l_h;
uint32_t	l_i;
int		l_status;

	for ( ;; )
		{
		if ( a_ctx->gnext >= a_ctx->gdata )
			{
			if ( !(1 & (l_status = s_vbk$loadgrp(a_ctx))) )
				return	l_status;

			continue;
			}

		l_i	= a_ctx->gnext++;

		if ( !a_ctx->gok [l_i] )
			{
			a_ctx->gap	= 1;
			continue;
			}

		l_h		= &a_ctx->ghdr [l_i];
		a_ctx->pay	= a_ctx->gbuf + (size_t) l_i * a_ctx->bsize + VBK$K_HDRSZ;
		a_ctx->paylen	= l_h->paylen;
		a_ctx->payoff	= 0;
		a_ctx->payvol	= a_ctx->gvol;
		a_ctx->payblk	= l_h->blkno;

		if ( !a_ctx->gap )
			return	STS$K_SUCCESS;

		/* After a loss: a block in which no record begins is of no use */
		if ( l_h->recoff == VBK$K_NONE )
			continue;

		a_ctx->payoff	= l_h->recoff;
		a_ctx->gap	= 0;

		return	STS$K_INFO;
		}
}


/*
**  A SOLID record (format.md 6.12) opened: its records decompressed into
**  SOL, to be returned one by one.  STS$K_ERROR - it does not open: an
**  unknown codec, a length out of bounds, a stream that is not right.
*/
static	int	s_vbk$solopen	(
		VBK$RCTX *	a_ctx,
		uint32_t	a_len,
	const	VBK$LOC *	a_loc
			)
{
uint32_t	l_raw;

	a_ctx->sollen = a_ctx->solpos = 0;

	if ( a_len < VBK$K_SOLIDHDR )
		return	STS$K_ERROR;

	if ( ((l_raw = vbk$get32(a_ctx->rec + 4)) > VBK$K_MAXSOLID) || (l_raw < VBK$K_RECHDR) )
		return	STS$K_ERROR;

	if ( l_raw > a_ctx->solsz )
		{
		uint8_t *	l_p;

		if ( !(l_p = realloc(a_ctx->solbuf, l_raw)) )
			return	a_ctx->err = ENOMEM, STS$K_FATAL;

		a_ctx->solbuf	= l_p;
		a_ctx->solsz	= l_raw;
		}

	a_ctx->sol	= a_ctx->solbuf;

	if ( STS$K_SUCCESS != vbk$data_unpack(vbk$get32(a_ctx->rec), a_ctx->rec + VBK$K_SOLIDHDR, a_len - VBK$K_SOLIDHDR, a_ctx->sol, l_raw) )
		return	STS$K_ERROR;

	a_ctx->sollen	= l_raw;
	a_ctx->solloc	= *a_loc;

	return	STS$K_SUCCESS;
}

/*
**  A SOLID of which only <a_got> octets of the body are there, a gap after
**  them: the beginning of its stream decompressed, as far as it is right.
**  STS$K_ERROR - nothing to be had of it.
*/
static	int	s_vbk$solcut	(
		VBK$RCTX *	a_ctx,
		uint32_t	a_got,
	const	VBK$LOC *	a_loc
			)
{
uint32_t	l_raw, l_good = 0;

	a_ctx->sollen = a_ctx->solpos = 0;

	if ( a_got <= VBK$K_SOLIDHDR )
		return	STS$K_ERROR;

	if ( ((l_raw = vbk$get32(a_ctx->rec + 4)) > VBK$K_MAXSOLID) || (l_raw < VBK$K_RECHDR) )
		return	STS$K_ERROR;

	if ( l_raw > a_ctx->solsz )
		{
		uint8_t *	l_p;

		if ( !(l_p = realloc(a_ctx->solbuf, l_raw)) )
			return	STS$K_ERROR;

		a_ctx->solbuf	= l_p;
		a_ctx->solsz	= l_raw;
		}

	a_ctx->sol	= a_ctx->solbuf;

	vbk$data_salvage(vbk$get32(a_ctx->rec), a_ctx->rec + VBK$K_SOLIDHDR, a_got - VBK$K_SOLIDHDR, a_ctx->sol, l_raw, &l_good);

	if ( l_good < VBK$K_RECHDR )
		return	STS$K_ERROR;

	a_ctx->sollen	= l_good;
	a_ctx->solloc	= *a_loc;

	return	STS$K_SUCCESS;
}

/*
**  The next record of the open SOLID: FILE, DATA or FEND only, whole in
**  it.  STS$K_ERROR - anything else: the rest of the SOLID is dropped.
*/
static	int	s_vbk$solnext	(
		VBK$RCTX *	a_ctx,
		uint16_t *	a_type,
	const	uint8_t **	a_body,
		uint32_t *	a_len,
		VBK$LOC *	a_loc
			)
{
uint32_t	l_left = a_ctx->sollen - a_ctx->solpos, l_len;
uint16_t	l_type;

	if ( l_left < VBK$K_RECHDR )
		return	a_ctx->sollen = a_ctx->solpos = 0, STS$K_ERROR;

	l_type	= vbk$get16(a_ctx->sol + a_ctx->solpos);
	l_len	= vbk$get32(a_ctx->sol + a_ctx->solpos + 4);

	if ( ((l_type != VBK$K_RT_FILE) && (l_type != VBK$K_RT_DATA) && (l_type != VBK$K_RT_FEND)) || (l_len > (l_left - VBK$K_RECHDR)) )
		return	a_ctx->sollen = a_ctx->solpos = 0, STS$K_ERROR;

	*a_type	= l_type;
	*a_body	= a_ctx->sol + a_ctx->solpos + VBK$K_RECHDR;
	*a_len	= l_len;

	if ( a_loc )
		*a_loc	= a_ctx->solloc;

	a_ctx->solpos += VBK$K_RECHDR + l_len;

	return	STS$K_SUCCESS;
}


/*
**  The next record of the stream as it lies there - a SOLID not opened.
**  <a_rs> - a gap before it already; R.RESYNC - a gap before what is
**  returned.  A SOLID of version 3 whose body a loss (R.CUT 1) or the end
**  of the stream (R.CUT 2) cut is returned with the R.GOT octets there
**  are; any other record so cut is dropped (a gap), or ends the stream.
*/
typedef struct vbk_rawrec_t
{
	uint16_t	type;
	uint32_t	len, got;
	VBK$LOC		loc;
	int		resync, cut;
} VBK$RAWREC;

static	int	s_vbk$raw	(
		VBK$RCTX *	a_ctx,
		VBK$RAWREC *	a_r,
		int		a_rs
			)
{
uint32_t	l_len, l_got, l_n;
uint16_t	l_type;
int		l_status;

	a_r->resync = a_rs;
	a_r->cut    = 0;

	for ( ;; )
		{
		/* A record header: never split, so it is wholly in one payload */
		while ( !a_ctx->pay || (a_ctx->payoff >= a_ctx->paylen) )
			{
			/* STS$K_INFO is of the success class, but means a loss here - so it is compared, not masked */
			if ( STS$K_SUCCESS != (l_status = s_vbk$nextpay(a_ctx)) )
				{
				if ( l_status == STS$K_INFO )
					{
					a_r->resync = 1;
					continue;
					}

				return	l_status;
				}
			}

		l_type	= 0;

		if ( (a_ctx->paylen - a_ctx->payoff) < VBK$K_RECHDR )
			l_len	= VBK$K_MAXREC + 1;
		else	{
			l_type	= vbk$get16(a_ctx->pay + a_ctx->payoff);
			l_len	= vbk$get32(a_ctx->pay + a_ctx->payoff + 4);
			}

		if ( !l_type || (l_len > VBK$K_MAXREC) )
			{
			/* Garbage where a header should be: treated as a loss, the stream is picked up again */
			s_vbk$event(a_ctx, VBK$K_EV_BADREC, a_ctx->payvol, a_ctx->payblk);

			a_ctx->gap	= 1;
			a_ctx->pay	= NULL;
			continue;
			}

		a_r->loc.vol	= a_ctx->payvol;
		a_r->loc.blk	= a_ctx->payblk;
		a_r->loc.off	= a_ctx->payoff;
		a_ctx->payoff	+= VBK$K_RECHDR;

		if ( l_len > a_ctx->recsz )
			{
			uint8_t *	l_p;

			if ( !(l_p = realloc(a_ctx->rec, l_len)) )
				{
				a_ctx->err	= ENOMEM;

				return	STS$K_FATAL;
				}

			a_ctx->rec	= l_p;
			a_ctx->recsz	= l_len;
			}

		/* The body, through as many payloads as it takes */
		for ( l_got = 0, l_status = STS$K_SUCCESS; l_got < l_len; )
			{
			if ( a_ctx->payoff >= a_ctx->paylen )
				if ( STS$K_SUCCESS != (l_status = s_vbk$nextpay(a_ctx)) )
					break;

			l_n	= a_ctx->paylen - a_ctx->payoff;
			l_n	= (l_n < (l_len - l_got)) ? l_n : (l_len - l_got);

			memcpy(a_ctx->rec + l_got, a_ctx->pay + a_ctx->payoff, l_n);

			l_got		+= l_n;
			a_ctx->payoff	+= l_n;
			}

		a_r->type = l_type;
		a_r->len  = l_len;
		a_r->got  = l_got;

		if ( (l_status == STS$K_INFO) || (l_status == STS$K_WARN) )
			{
			/* Of a SOLID cut short the records before the cut are good: returned so, to be read as far as they go */
			if ( (l_type == VBK$K_RT_SOLID) && (a_ctx->version == VBK$K_VERSION3) && (l_got > VBK$K_SOLIDHDR) )
				{
				a_r->cut = (l_status == STS$K_INFO) ? 1 : 2;

				return	STS$K_SUCCESS;
				}

			/* Lost in the middle of the body: what is there is dropped, a new header is at hand */
			if ( l_status == STS$K_INFO )
				{
				a_r->resync = 1;
				continue;
				}

			/* The stream ends inside the record - a cut-short saveset */
			s_vbk$event(a_ctx, VBK$K_EV_BADREC, a_ctx->payvol, a_ctx->payblk);
			a_r->resync = 1;

			return	STS$K_WARN;
			}

		return	STS$K_SUCCESS;
		}
}


/*
**  Batch mode (VBK$RD_BATCH): records read ahead, the DATAZ and SOLID
**  among them decompressed at once on the threads of the pool, handed
**  out in their order - a DATAZ as the DATA record it stands for.
*/
typedef struct vbk_bslot_t
{
	VBK$RAWREC	r;
	uint8_t *	raw;			/* The body as read				*/
	uint8_t *	dec;			/* ... decompressed: a DATA body, the records of a SOLID */
	size_t		rawsz, decsz;
	uint32_t	declen;
	int		decst;			/* STS$K_SUCCESS - DEC holds it			*/
} VBK$BSLOT;

#define	VBK$K_BATCHMAX	64			/* Records read ahead at most			*/
#define	VBK$K_BATCHOCT	(64 * 1048576)		/* ... octets of them at most			*/

static	void	s_vbk$bfree	(
		VBK$RCTX *	a_ctx
			)
{
VBK$BSLOT *	l_b = (VBK$BSLOT *) a_ctx->bslot;

	for ( uint32_t i = 0; l_b && (i < VBK$K_BATCHMAX); i++ )
		{
		free(l_b [i].raw);
		free(l_b [i].dec);
		}

	free(l_b);
	a_ctx->bslot = NULL;
	a_ctx->bn = a_ctx->bpos = 0;
	a_ctx->bend = 0;
}

/*
**  One job of the pool: a slot decompressed - a SOLID whole, or as far as
**  it goes when cut; a DATAZ into the DATA record it stands for
*/
static	void	s_vbk$bjob	(
		void *		a_arg,
		uint32_t	a_i
			)
{
VBK$RCTX *	l_ctx = (VBK$RCTX *) a_arg;
VBK$BSLOT *	l_s = &((VBK$BSLOT *) l_ctx->bslot) [l_ctx->bmap [a_i]];
uint32_t	l_raw, l_good = 0, l_need;

	l_s->decst = STS$K_ERROR;

	if ( l_s->r.type == VBK$K_RT_SOLID )
		{
		uint32_t	l_in = l_s->r.cut ? l_s->r.got : l_s->r.len;

		if ( (l_in < VBK$K_SOLIDHDR) || ((l_raw = vbk$get32(l_s->raw + 4)) > VBK$K_MAXSOLID) || (l_raw < VBK$K_RECHDR) )
			return;

		l_need	= l_raw;
		}
	else	{
		if ( (l_s->r.len < VBK$K_DATAZHDR) || ((l_raw = vbk$get32(l_s->raw + 16)) > VBK$K_MAXDATA) )
			return;

		l_need	= VBK$K_DATAHDR + l_raw;
		}

	if ( l_need > l_s->decsz )
		{
		uint8_t *	l_p;

		if ( !(l_p = realloc(l_s->dec, l_need)) )
			return;

		l_s->dec   = l_p;
		l_s->decsz = l_need;
		}

	if ( l_s->r.type == VBK$K_RT_SOLID )
		{
		if ( l_s->r.cut )
			{
			vbk$data_salvage(vbk$get32(l_s->raw), l_s->raw + VBK$K_SOLIDHDR, l_s->r.got - VBK$K_SOLIDHDR, l_s->dec, l_raw, &l_good);

			if ( l_good >= VBK$K_RECHDR )
				l_s->declen = l_good, l_s->decst = STS$K_SUCCESS;
			}
		else if ( STS$K_SUCCESS == vbk$data_unpack(vbk$get32(l_s->raw), l_s->raw + VBK$K_SOLIDHDR, l_s->r.len - VBK$K_SOLIDHDR, l_s->dec, l_raw) )
			l_s->declen = l_raw, l_s->decst = STS$K_SUCCESS;

		return;
		}

	/* DATAZ: fileno, codec, offset, rawlen - into fileno, 0, offset and the octets */
	if ( STS$K_SUCCESS == vbk$data_unpack(vbk$get32(l_s->raw + 4), l_s->raw + VBK$K_DATAZHDR, l_s->r.len - VBK$K_DATAZHDR, l_s->dec + VBK$K_DATAHDR, l_raw) )
		{
		memcpy(l_s->dec, l_s->raw, 4);
		vbk$put32(l_s->dec + 4, 0);
		memcpy(l_s->dec + 8, l_s->raw + 8, 8);
		l_s->declen = VBK$K_DATAHDR + l_raw;
		l_s->decst  = STS$K_SUCCESS;
		}
}

/*
**  The batch refilled: records read ahead up to its bounds or the end of
**  the stream, their decompression shared out.  STS$K_SUCCESS - at least
**  one slot, or BEND set; STS$K_FATAL - no memory.
*/
static	int	s_vbk$bfill	(
		VBK$RCTX *	a_ctx,
		int		a_rs
			)
{
VBK$BSLOT *	l_b;
VBK$RAWREC	l_r;
uint64_t	l_oct = 0;
uint32_t	l_ndec = 0;
int		l_status;

	if ( !a_ctx->bslot && !(a_ctx->bslot = calloc(VBK$K_BATCHMAX, sizeof(VBK$BSLOT))) )
		return	a_ctx->err = ENOMEM, STS$K_FATAL;

	l_b	= (VBK$BSLOT *) a_ctx->bslot;
	a_ctx->bn = a_ctx->bpos = 0;

	while ( (a_ctx->bn < a_ctx->batch) && (l_oct < VBK$K_BATCHOCT) )
		{
		VBK$BSLOT *	l_s = &l_b [a_ctx->bn];
		uint32_t	l_have;

		if ( STS$K_SUCCESS != (l_status = s_vbk$raw(a_ctx, &l_r, a_rs)) )
			{
			/* STS$K_WARN is 0: whether there is an end is a flag of its own */
			a_ctx->bend   = 1;
			a_ctx->bendst = l_status;
			a_ctx->bendrs = l_r.resync;
			break;
			}

		a_rs	= 0;
		l_have	= l_r.cut ? l_r.got : l_r.len;

		if ( l_have > l_s->rawsz )
			{
			uint8_t *	l_p;

			if ( !(l_p = realloc(l_s->raw, l_have ? l_have : 1)) )
				return	a_ctx->err = ENOMEM, STS$K_FATAL;

			l_s->raw   = l_p;
			l_s->rawsz = l_have;
			}

		memcpy(l_s->raw, a_ctx->rec, l_have);
		l_s->r	   = l_r;
		l_s->decst = STS$K_ERROR;
		l_oct	  += l_have;
		a_ctx->bn++;

		/* The END: nothing after it is wanted now */
		if ( l_r.type == VBK$K_RT_END )
			break;
		}

	/* The slots to decompress, one job each; the others are handed out as they are */
	for ( uint32_t i = 0; i < a_ctx->bn; i++ )
		{
		uint16_t	l_ty = l_b [i].r.type;

		if ( (l_ty == VBK$K_RT_DATAZ) || ((l_ty == VBK$K_RT_SOLID) && (a_ctx->version == VBK$K_VERSION3)) )
			a_ctx->bmap [l_ndec++] = i;
		}

	vbk$crp_par(l_ndec, s_vbk$bjob, a_ctx);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Return the next record of the stream.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context
**	a_type		Receives the record type
**	a_body		Receives the address of the body, valid up to the
**			next call
**	a_len		Receives the length of the body
**	a_loc		Receives where the record header is, NULL - not wanted
**
**  IMPLICIT INPUTS/OUTPUTS:
**	RESYNC of the context tells whether blocks have been lost before
**	this record.  The records of a SOLID come one by one, INSOLID set
**	(format.md 6.12).  In batch mode (VBK$RD_BATCH) a DATAZ comes as
**	the DATA record it stands for when it decompresses.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- a record has been returned;
**	STS$K_WARN	- the end of the stream;
**	STS$K_FATAL	- no memory.
**--
*/
int	vbk$rd_next	(
		VBK$RCTX *	a_ctx,
		uint16_t *	a_type,
	const	uint8_t **	a_body,
		uint32_t *	a_len,
		VBK$LOC *	a_loc
			)
{
VBK$RAWREC	l_r;
const uint8_t *	l_body;
int		l_status, l_resync = a_ctx->pendrs, l_dec = 0;

	if ( a_ctx->crypt && !a_ctx->haskey )
		{
		a_ctx->err	= EACCES;

		return	STS$K_ERROR;
		}

	a_ctx->pendrs	= 0;

	for ( ;; )
		{
		/* A SOLID cut short, its good records all returned: the gap after them */
		if ( a_ctx->solgap && (a_ctx->solpos >= a_ctx->sollen) )
			{
			int	l_eos = (a_ctx->solgap == 2);

			a_ctx->solgap	= 0;
			a_ctx->sollen	= a_ctx->solpos = 0;
			l_resync	= 1;

			/* The end of the stream cut it: that end now */
			if ( l_eos )
				{
				s_vbk$event(a_ctx, VBK$K_EV_BADREC, a_ctx->payvol, a_ctx->payblk);
				a_ctx->resync	= 1;

				return	STS$K_WARN;
				}
			}

		/* The records of an open SOLID come first, as if they were in the stream */
		if ( a_ctx->solpos < a_ctx->sollen )
			{
			if ( STS$K_SUCCESS == s_vbk$solnext(a_ctx, a_type, a_body, a_len, a_loc) )
				{
				a_ctx->resync	= l_resync;
				a_ctx->insolid	= 1;

				return	STS$K_SUCCESS;
				}

			/* A record in it that makes no sense: the rest is dropped, a gap - its files are named lost; cut short: the gap says it */
			if ( !a_ctx->solgap )
				s_vbk$event(a_ctx, VBK$K_EV_BADREC, a_ctx->solloc.vol, a_ctx->solloc.blk);

			l_resync = 1;

			if ( a_ctx->solgap == 2 )
				{
				a_ctx->solgap	= 0;
				s_vbk$event(a_ctx, VBK$K_EV_BADREC, a_ctx->payvol, a_ctx->payblk);
				a_ctx->resync	= 1;

				return	STS$K_WARN;
				}

			a_ctx->solgap	= 0;
			}

		/* The next record: of the batch, or of the stream itself */
		if ( a_ctx->batch )
			{
			VBK$BSLOT *	l_s;

			if ( a_ctx->bpos >= a_ctx->bn )
				{
				if ( a_ctx->bend )
					{
					a_ctx->resync	= l_resync | a_ctx->bendrs;

					return	a_ctx->bendst;
					}

				if ( STS$K_SUCCESS != (l_status = s_vbk$bfill(a_ctx, l_resync)) )
					return	l_status;

				l_resync = 0;
				continue;
				}

			l_s	= &((VBK$BSLOT *) a_ctx->bslot) [a_ctx->bpos++];
			l_r	= l_s->r;
			l_r.resync |= l_resync;
			l_body	= l_s->raw;
			l_dec	= (l_s->decst == STS$K_SUCCESS);

			if ( l_dec && (l_r.type == VBK$K_RT_DATAZ) )
				{
				*a_type		= VBK$K_RT_DATA;
				*a_body		= l_s->dec;
				*a_len		= l_s->declen;
				a_ctx->resync	= l_r.resync;
				a_ctx->insolid	= 0;

				if ( a_loc )
					*a_loc	= l_r.loc;

				return	STS$K_SUCCESS;
				}

			if ( l_r.type == VBK$K_RT_SOLID )
				{
				a_ctx->sol	= l_s->dec;
				a_ctx->sollen	= l_dec ? l_s->declen : 0;
				a_ctx->solpos	= 0;
				a_ctx->solloc	= l_r.loc;
				}
			}
		else	{
			if ( STS$K_SUCCESS != (l_status = s_vbk$raw(a_ctx, &l_r, l_resync)) )
				{
				a_ctx->resync	= l_r.resync;

				return	l_status;
				}

			l_body	= a_ctx->rec;

			/* Version 3: a SOLID is opened here, whole or as far as it is there */
			if ( (l_r.type == VBK$K_RT_SOLID) && (a_ctx->version == VBK$K_VERSION3) )
				{
				if ( l_r.cut )
					l_dec = (STS$K_SUCCESS == s_vbk$solcut(a_ctx, l_r.got, &l_r.loc));
				else if ( STS$K_FATAL == (l_status = s_vbk$solopen(a_ctx, l_r.len, &l_r.loc)) )
					return	STS$K_FATAL;
				else	l_dec = (l_status == STS$K_SUCCESS);
				}
			}

		l_resync = l_r.resync;

		/* Version 3: the records of the SOLID from the top of the loop; one cut short - the gap after them */
		if ( (l_r.type == VBK$K_RT_SOLID) && (a_ctx->version == VBK$K_VERSION3) )
			{
			if ( l_dec )
				{
				a_ctx->solgap	= l_r.cut;
				continue;
				}

			a_ctx->sollen = a_ctx->solpos = 0;

			/* Nothing to be had of it: what it held is a gap, its files are named lost */
			if ( l_r.cut != 2 )
				{
				s_vbk$event(a_ctx, VBK$K_EV_BADREC, l_r.loc.vol, l_r.loc.blk);
				l_resync = 1;
				continue;
				}

			s_vbk$event(a_ctx, VBK$K_EV_BADREC, a_ctx->payvol, a_ctx->payblk);
			a_ctx->resync	= 1;

			return	STS$K_WARN;
			}

		*a_type		= l_r.type;
		*a_body		= l_body;
		*a_len		= l_r.len;
		a_ctx->resync	= l_resync;
		a_ctx->insolid	= 0;

		if ( a_loc )
			*a_loc	= l_r.loc;

		return	STS$K_SUCCESS;
		}
}


/*
**  Batch mode on: the records read ahead, their decompression shared by
**  the threads of the pool (VBK$CRP_SETPAR).  Of no effect without one
**  (vbkx, the plugins) or on a stream; to be called when the reading
**  begins - after VBK$RD_SETKEY.
*/
void	vbk$rd_batch	(
		VBK$RCTX *	a_ctx
			)
{
uint32_t	l_n = vbk$crp_nthr();

	if ( (l_n < 2) || a_ctx->isstream )
		return;

	a_ctx->batch	= ((4 * l_n) < VBK$K_BATCHMAX) ? (4 * l_n) : VBK$K_BATCHMAX;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Position the stream at a record header whose place is known - from
**	the TRAILER or from a catalog entry.  The group of the block is
**	read whole, so a bad block there is rebuilt as in sequential mode.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context
**	a_loc		Volume, block and offset of the record header
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the next VBK$RD_NEXT returns that record;
**	STS$K_WARN	- that block is lost: the stream is positioned at
**			  the next record that can be had, flagged RESYNC;
**	STS$K_ERROR	- no such place in this saveset.
**--
*/
int	vbk$rd_seek	(
		VBK$RCTX *	a_ctx,
	const	VBK$LOC *	a_loc
			)
{
VBK$RVOL *	l_vol;
uint64_t	l_pos, l_start;
int		l_status;

	/* A SOLID being read is left, and what was read ahead: the seek goes elsewhere */
	a_ctx->sollen = a_ctx->solpos = 0;
	a_ctx->solgap = 0;
	a_ctx->bn = a_ctx->bpos = 0;
	a_ctx->bend = 0;

	/* A stream has no places to go to: it is read through, forward */
	if ( !a_loc->vol || (a_loc->vol > a_ctx->nvols) || (a_ctx->crypt && !a_ctx->haskey) || a_ctx->isstream )
		return	STS$K_ERROR;

	l_vol	= &a_ctx->vols [a_loc->vol - 1];

	if ( (l_vol->fd < 0) || (a_loc->blk <= l_vol->firstblk) )
		return	STS$K_ERROR;

	l_pos	= a_loc->blk - l_vol->firstblk;

	if ( l_pos >= s_vbk$volend(a_ctx, a_loc->vol) )
		return	STS$K_ERROR;

	l_start	= a_ctx->grpsz ? (1 + ((l_pos - 1) / (a_ctx->grpsz + a_ctx->parity)) * (a_ctx->grpsz + a_ctx->parity)) : l_pos;

	/*
	**  The group in hand is taken as it is - the next file of an EXTRACT
	**  is often in it - rather than read again: its pages have been
	**  dropped from the cache, and its events have been reported once.
	*/
	if ( a_ctx->gdata && (a_ctx->gvol == a_loc->vol) && (a_ctx->gpos == l_start) )
		{
		a_ctx->curvol	= a_ctx->gvol;
		a_ctx->curpos	= a_ctx->gend;
		a_ctx->pay	= NULL;
		a_ctx->paylen	= a_ctx->payoff = 0;
		a_ctx->gap	= a_ctx->eof = a_ctx->pendrs = 0;
		}
	else	{
		vbk$rd_rewind(a_ctx);

		a_ctx->curvol	= a_loc->vol;
		a_ctx->curpos	= l_start;

		if ( !(1 & (l_status = s_vbk$loadgrp(a_ctx))) )
			return	STS$K_ERROR;
		}

	if ( (l_pos - l_start) >= a_ctx->gdata )
		return	STS$K_ERROR;

	a_ctx->gnext	= (uint32_t) (l_pos - l_start);

	l_status	= s_vbk$nextpay(a_ctx);

	if ( (l_status == STS$K_SUCCESS) && (a_ctx->payblk == a_loc->blk) && (a_loc->off < a_ctx->paylen) )
		{
		a_ctx->payoff	= a_loc->off;

		return	STS$K_SUCCESS;
		}

	if ( l_status == STS$K_WARN )
		return	STS$K_ERROR;

	a_ctx->pendrs	= 1;

	return	STS$K_WARN;
}
