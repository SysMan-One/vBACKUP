#define	__MODULE__	"VBKRD"
#define	__IDENT__	"X01-08"
#define	__REV__		"1.8.0"

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
**	X01-08		 5-OCT-2026	RRL
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
#include	"vbkos.h"

#define	VBK$K_RECINI	(VBK$K_MAXDATA + VBK$K_DATAHDR)	/* First allocation of the record buffer */


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

	if ( 0 > (l_fd = vbk$os_open(a_spec)) )
		{
		a_ctx->err	= errno;

		return	STS$K_ERROR;
		}

	/* The block size comes from the first header; nothing else is trusted before the checksum */
	if ( !(1 & s_vbk$pread(l_fd, l_hdr, sizeof(l_hdr), 0)) || !(1 & vbk$bhdr_peek(l_hdr, &l_bhdr))
		|| (l_bhdr.bsize < VBK$K_MINBSZ) || (l_bhdr.bsize > VBK$K_MAXBSZ) || (l_bhdr.bsize % VBK$K_BSZALIGN) )
		{
		vbk$os_close(l_fd);

		return	STS$K_WARN;
		}

	a_ctx->bsize	= l_bhdr.bsize;
	a_ctx->psize	= l_bhdr.bsize - VBK$K_HDRSZ;

	if ( !(a_ctx->vols = calloc(VBK$K_MAXVOL, sizeof(VBK$RVOL))) || !(l_blk = malloc(a_ctx->bsize)) )
		{
		vbk$os_close(l_fd);
		free(l_blk);
		a_ctx->err	= ENOMEM;

		return	STS$K_FATAL;
		}

	for ( uint32_t i = 0; i < VBK$K_MAXVOL; i++ )
		a_ctx->vols [i].fd = -1;

	if ( !(1 & s_vbk$vhdr(a_ctx, l_fd, 1, l_blk, &l_bhdr)) )
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

	if ( a_ctx->grpsz > VBK$K_MAXGRP )
		{
		vbk$os_close(l_fd);
		free(l_blk);

		return	STS$K_WARN;
		}

	vbk$os_fsize(l_fd, &l_size, &l_isreg);
	vbk$os_seq(l_fd);

	a_ctx->vols [0].fd	 = l_fd;
	a_ctx->vols [0].firstblk = l_bhdr.blkno;
	a_ctx->vols [0].nblk	 = l_size / a_ctx->bsize;
	a_ctx->nvols		 = 1;

	/*
	**  The further volumes.  A missing name does not end the search at
	**  once: with volume 3 lost, volumes 4 and on are still worth having.
	*/
	for ( uint32_t l_volno = 2; (l_volno <= VBK$K_MAXVOL) && (l_miss < VBK$K_VOLGAP); l_volno++ )
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

	if ( l_vol->nblk > 1 )
		{
		s_vbk$pread(l_vol->fd, l_blk, a_ctx->bsize, (l_vol->nblk - 1) * a_ctx->bsize);

		if ( 1 & vbk$blk_check(l_blk, a_ctx->bsize, a_ctx->ssuuid, &l_bhdr) )
			s_vbk$trailer(a_ctx, l_blk, &l_bhdr);
		}
	}

	free(l_blk);

	a_ctx->recsz	= VBK$K_RECINI;

	if ( !(a_ctx->gbuf = vbk$os_balloc((size_t) (a_ctx->grpsz + 1) * a_ctx->bsize)) || !(a_ctx->rec = malloc(a_ctx->recsz)) )
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
VBK$BHDR	l_bhdr;
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

	if ( a_ctx->trlraw && !a_ctx->trailer )
		{
		if ( (1 & vbk$blk_check(a_ctx->trlraw, a_ctx->bsize, a_ctx->ssuuid, &l_bhdr))
			&& (1 & vbk$crp_open(&a_ctx->keys, &l_bhdr, a_ctx->trlraw + VBK$K_HDRSZ, a_ctx->psize))
			&& (a_ctx->trailer = malloc(l_bhdr.paylen + 1)) )
			{
			memcpy(a_ctx->trailer, a_ctx->trlraw + VBK$K_HDRSZ, l_bhdr.paylen);
			a_ctx->trllen	= l_bhdr.paylen;
			}
		else	{
			s_vbk$event(a_ctx, VBK$K_EV_BADTAG, a_ctx->nvols, a_ctx->trlblk);
			l_status = STS$K_WARN;
			}
		}

	/*
	**  The first record of the stream is the complete SUMMARY.  It is read
	**  quietly: whatever is lost on the way is reported when the caller
	**  reads it for itself.
	*/
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
	if ( a_ctx->vols )
		for ( uint32_t i = 0; i < a_ctx->nvols; i++ )
			if ( a_ctx->vols [i].fd >= 0 )
				vbk$os_close(a_ctx->vols [i].fd);

	free(a_ctx->vols);
	free(a_ctx->summary);
	free(a_ctx->trailer);
	free(a_ctx->trlraw);
	free(a_ctx->gbuf);
	free(a_ctx->rec);

	vbk$crp_wipe(a_ctx, sizeof(*a_ctx));
}


/*
**  Back to the first record of the saveset
*/
int	vbk$rd_rewind	(
		VBK$RCTX *	a_ctx
			)
{
	a_ctx->curvol	= 1;
	a_ctx->curpos	= 1;
	a_ctx->gdata	= a_ctx->gnext = 0;
	a_ctx->pay	= NULL;
	a_ctx->paylen	= a_ctx->payoff = 0;
	a_ctx->gap	= a_ctx->eof = a_ctx->pendrs = 0;

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

	l_n	= a_ctx->grpsz ? (a_ctx->grpsz + 1) : 1;
	l_n	= ((l_end - a_ctx->curpos) < l_n) ? (l_end - a_ctx->curpos) : l_n;

	s_vbk$pread(l_vol->fd, a_ctx->gbuf, (size_t) (l_n * a_ctx->bsize), a_ctx->curpos * a_ctx->bsize);

	/* A saveset is read once: its pages would only push working data out of the cache */
	vbk$os_drop(l_vol->fd, a_ctx->curpos * a_ctx->bsize, l_n * a_ctx->bsize);

	for ( uint32_t i = 0; i < l_n; i++ )
		{
		VBK$BHDR *	l_h = &a_ctx->ghdr [i];

		a_ctx->gok [i]	= (1 & vbk$blk_check(a_ctx->gbuf + (size_t) i * a_ctx->bsize, a_ctx->bsize, a_ctx->ssuuid, l_h))
				&& (l_h->blkno == (l_vol->firstblk + a_ctx->curpos + i)) && (l_h->volno == a_ctx->curvol);
		}

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
		{
		if ( a_ctx->gok [i] && (a_ctx->ghdr [i].type != a_ctx->dtype) )
			a_ctx->gok [i] = 0;

		/* A good CRC and a wrong TAG: changed on purpose - a bad block all the same, repairable as any */
		if ( a_ctx->gok [i] && a_ctx->crypt
			&& !vbk$crp_check(&a_ctx->keys, &a_ctx->ghdr [i], a_ctx->gbuf + (size_t) i * a_ctx->bsize + VBK$K_HDRSZ, a_ctx->psize) )
			{
			a_ctx->gok [i] = 0;
			s_vbk$event(a_ctx, VBK$K_EV_BADTAG, a_ctx->curvol, a_ctx->ghdr [i].blkno);
			}

		if ( !a_ctx->gok [i] )
			{
			l_bad++;
			l_badidx = i;
			}
		}

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

	/* Every check done, the repair too: now the good blocks are decrypted where they lie */
	if ( a_ctx->crypt )
		for ( uint32_t i = 0; i < a_ctx->gdata; i++ )
			if ( a_ctx->gok [i] )
				vbk$crp_decrypt(&a_ctx->keys, &a_ctx->ghdr [i], a_ctx->gbuf + (size_t) i * a_ctx->bsize + VBK$K_HDRSZ);

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
**	this record.
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
VBK$LOC		l_loc;
uint32_t	l_len, l_got, l_n;
uint16_t	l_type;
int		l_status, l_resync = a_ctx->pendrs;

	if ( a_ctx->crypt && !a_ctx->haskey )
		{
		a_ctx->err	= EACCES;

		return	STS$K_ERROR;
		}

	a_ctx->pendrs	= 0;

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
					l_resync = 1;
					continue;
					}

				a_ctx->resync	= l_resync;

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

		l_loc.vol	= a_ctx->payvol;
		l_loc.blk	= a_ctx->payblk;
		l_loc.off	= a_ctx->payoff;
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

		if ( l_status == STS$K_INFO )
			{
			/* Lost in the middle of the body: what is there is dropped, a new header is at hand */
			l_resync = 1;
			continue;
			}

		if ( l_status == STS$K_WARN )
			{
			/* The stream ends inside the record - a cut-short saveset */
			s_vbk$event(a_ctx, VBK$K_EV_BADREC, a_ctx->payvol, a_ctx->payblk);
			a_ctx->resync	= 1;

			return	STS$K_WARN;
			}

		*a_type		= l_type;
		*a_body		= a_ctx->rec;
		*a_len		= l_len;
		a_ctx->resync	= l_resync;

		if ( a_loc )
			*a_loc	= l_loc;

		return	STS$K_SUCCESS;
		}
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

	if ( !a_loc->vol || (a_loc->vol > a_ctx->nvols) || (a_ctx->crypt && !a_ctx->haskey) )
		return	STS$K_ERROR;

	l_vol	= &a_ctx->vols [a_loc->vol - 1];

	if ( (l_vol->fd < 0) || (a_loc->blk <= l_vol->firstblk) )
		return	STS$K_ERROR;

	l_pos	= a_loc->blk - l_vol->firstblk;

	if ( l_pos >= s_vbk$volend(a_ctx, a_loc->vol) )
		return	STS$K_ERROR;

	l_start	= a_ctx->grpsz ? (1 + ((l_pos - 1) / (a_ctx->grpsz + 1)) * (a_ctx->grpsz + 1)) : l_pos;

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
