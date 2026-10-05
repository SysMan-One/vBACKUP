#define	__MODULE__	"VBKVMS"
#define	__IDENT__	"X01-13"
#define	__REV__		"1.13.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkvms.c
**
**  ABSTRACT:	The savesets of OpenVMS BACKUP, read (doc/vmsbackup.md).
**
**  DESCRIPTION: A saveset of BACKUP is a row of blocks of one size, each
**		with a header of 256 octets (BBH) and records after it, each
**		with a header of 16 (BRH): the SUMMARY, then for every file
**		its FILE record - the attributes - and VBN records, its blocks
**		of 512 as they are on the disk.  /GROUP_SIZE=n puts after every
**		n blocks one more, the XOR of them: a block whose CRC fails is
**		rebuilt from the others.  There is no catalog and no trailer:
**		the saveset is read from its first block to its last, once.
**
**		CRC: AUTODIN-II, that is CRC-32/IEEE - the CRC of VBACKUP's
**		own blocks - over the whole block, the fields of the CRC and
**		of the checksum taken as zeroes.
**
**		The records of a file become the octets of a Linux file as
**		doc/vmsbackup.md, section 5 says: a text file - a carriage
**		control - is made a text with LF; what has none is copied as
**		it is on the disk.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  5-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-13		 5-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>

#include	"vbkfmt.h"
#include	"vbkrd.h"
#include	"vbkvms.h"
#include	"vbkos.h"

/*
**  The block header, BBH: the offsets of what is read of it
*/
#define	BBH$W_SIZE	0
#define	BBH$W_OPSYS	2
#define	BBH$W_APPLIC	6
#define	BBH$L_NUMBER	8
#define	BBH$W_STRUCLEV	32
#define	BBH$W_VOLNUM	34
#define	BBH$L_CRC	36
#define	BBH$L_BLOCKSIZE	40
#define	BBH$L_FLAGS	44
#define	BBH$W_CHECKSUM	254

#define	BBH$M_NOCRC	0x00000001

#define	VBK$K_VMSSTRUC	0x0101			/* Structure level 1, version 1			*/

#define	VBK$K_VMSEPOCH	35067168000000000ULL	/* 1-JAN-1970 in VMS time			*/
#define	VBK$K_VMSDAY	864000000000ULL		/* A day in VMS time				*/

/*
**  The attributes of a FILE record and of the SUMMARY (BSA$K_*)
*/
enum	{
	BSA$K_SSNAME = 1, BSA$K_COMMAND, BSA$K_COMMENT, BSA$K_USERNAME, BSA$K_USERUIC, BSA$K_DATE, BSA$K_OPSYS,
	BSA$K_SYSVER, BSA$K_NODENAME, BSA$K_SIR, BSA$K_DRIVEID, BSA$K_BACKVER, BSA$K_BLOCKSIZE, BSA$K_XORSIZE,
	BSA$K_BUFFERS
	};

enum	{
	BSA$K_FILENAME = 42, BSA$K_STRUCLEV, BSA$K_FID, BSA$K_BACKLINK, BSA$K_FILESIZE, BSA$K_UIC, BSA$K_FPRO,
	BSA$K_RPRO, BSA$K_ACLEVEL, BSA$K_UCHAR, BSA$K_RECATTR, BSA$K_REVISION, BSA$K_CREDATE, BSA$K_REVDATE,
	BSA$K_EXPDATE, BSA$K_BAKDATE,
	BSA$K_ACCDATE = 93, BSA$K_ATTDATE
	};


/*
**  The CRC of a block: CRC-32/IEEE, its own field and the checksum as zeroes
*/
static	uint32_t	s_vbk$vmscrc	(
	const	uint8_t *	a_blk,
		uint32_t	a_bsize
			)
{
static	const uint8_t	l_zero [4];
uint32_t	l_crc;

	l_crc	= $VBK_CRC(0, a_blk, BBH$L_CRC);
	l_crc	= $VBK_CRC(l_crc, l_zero, 4);
	l_crc	= $VBK_CRC(l_crc, a_blk + BBH$L_CRC + 4, BBH$W_CHECKSUM - BBH$L_CRC - 4);
	l_crc	= $VBK_CRC(l_crc, l_zero, 2);
	l_crc	= $VBK_CRC(l_crc, a_blk + VBK$K_VMSHDR, a_bsize - VBK$K_VMSHDR);

	return	l_crc;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Is it the first block of a saveset of BACKUP?  There is no magic:
**	the header is judged by its size, structure level, block size, the
**	number of the block, and the CRC when the whole block is given.
**
**  FORMAL PARAMETERS:
**
**	a_hdr		The first octets of the file
**	a_len		How many: 256 at least, the whole block for the CRC
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- it is one;
**	STS$K_ERROR	- it is not.
**--
*/
int	vbk$vms_probe	(
	const	uint8_t *	a_hdr,
		size_t		a_len
			)
{
uint32_t	l_bsize;

	if ( a_len < VBK$K_VMSHDR )
		return	STS$K_ERROR;

	l_bsize	= vbk$get32(a_hdr + BBH$L_BLOCKSIZE);

	if ( (vbk$get16(a_hdr + BBH$W_SIZE) != VBK$K_VMSHDR) || (vbk$get16(a_hdr + BBH$W_STRUCLEV) != VBK$K_VMSSTRUC)
		|| (vbk$get16(a_hdr + BBH$W_APPLIC) != VBK$K_VMSAPP_DATA) || (vbk$get32(a_hdr + BBH$L_NUMBER) != 1)
		|| (l_bsize < VBK$K_VMSMINBSZ) || (l_bsize > VBK$K_VMSMAXBSZ) )
		return	STS$K_ERROR;

	if ( (a_len >= l_bsize) && !(vbk$get32(a_hdr + BBH$L_FLAGS) & BBH$M_NOCRC)
		&& (s_vbk$vmscrc(a_hdr, l_bsize) != vbk$get32(a_hdr + BBH$L_CRC)) )
		return	STS$K_ERROR;

	return	STS$K_SUCCESS;
}


/*
**  Is the block at <a_blk> good: its header of this saveset, its CRC right.
**  The header of an XOR block is the XOR of those of its group: of an even
**  number of blocks its structure level and block size are zeroes.
*/
static	int	s_vbk$vmsgood	(
		VBK$VMS *	a_ctx,
	const	uint8_t *	a_blk
			)
{
int	l_xor = vbk$get16(a_blk + BBH$W_APPLIC) == VBK$K_VMSAPP_XOR;

	if ( (vbk$get16(a_blk + BBH$W_SIZE) != VBK$K_VMSHDR) || (!l_xor && ((vbk$get16(a_blk + BBH$W_STRUCLEV) != VBK$K_VMSSTRUC)
		|| (vbk$get32(a_blk + BBH$L_BLOCKSIZE) != a_ctx->bsize))) )
		return	0;

	if ( a_ctx->nocrc || (vbk$get32(a_blk + BBH$L_FLAGS) & BBH$M_NOCRC) )
		return	1;

	return	s_vbk$vmscrc(a_blk, a_ctx->bsize) == vbk$get32(a_blk + BBH$L_CRC);
}


/*
**  One whole block; 0 - the end (a part of a block at the end is none),
**  -1 - an error, errno in ERR
*/
static	int	s_vbk$vmsread	(
		VBK$VMS *	a_ctx,
		uint8_t *	a_buf
			)
{
size_t	l_got = 0;
int64_t	l_rc;

	while ( l_got < a_ctx->bsize )
		{
		if ( 0 > (l_rc = vbk$os_read(a_ctx->fd, a_buf + l_got, a_ctx->bsize - l_got)) )
			{
			if ( errno == EINTR )
				continue;

			a_ctx->err	= errno;

			return	-1;
			}

		if ( !l_rc )
			break;

		l_got	+= (size_t) l_rc;
		}

	return	(l_got == a_ctx->bsize) ? 1 : 0;
}


/*
**  The next group: up to GRPSZ + 1 blocks read, checked, a bad one rebuilt
**  from the others when it can be.  STS$K_WARN - the end.
*/
static	int	s_vbk$vmsgroup	(
		VBK$VMS *	a_ctx
			)
{
uint32_t	l_want = a_ctx->grpsz ? a_ctx->grpsz + 1 : 1, l_nbad = 0, l_bad = 0, l_xor;
int		l_rc;

	a_ctx->gbase	= a_ctx->nread + 1;
	a_ctx->gn	= 0;
	a_ctx->gnext	= 0;

	for ( ; a_ctx->gn < l_want; a_ctx->gn++ )
		{
		uint8_t *	l_b = a_ctx->gbuf + (size_t) a_ctx->gn * a_ctx->bsize;

		if ( a_ctx->ipre < a_ctx->npre )
			{
			/* Read ahead by VBK$VMS_OPEN */
			memcpy(l_b, a_ctx->pre + (size_t) a_ctx->ipre++ * a_ctx->bsize, a_ctx->bsize);
			l_rc	= 1;
			}
		else if ( a_ctx->eof || (0 >= (l_rc = s_vbk$vmsread(a_ctx, l_b))) )
			{
			a_ctx->eof	= 1;
			break;
			}

		a_ctx->nread++;
		a_ctx->nblocks++;

		if ( !(a_ctx->gok [a_ctx->gn] = (uint8_t) s_vbk$vmsgood(a_ctx, l_b)) )
			{
			l_nbad++;
			l_bad	= a_ctx->gn;
			}
		}

	if ( !a_ctx->gn )
		return	STS$K_WARN;

	/* The XOR block is the last of a group, also of the short one at the end */
	l_xor	= a_ctx->grpsz ? a_ctx->gn - 1 : (uint32_t) -1;

	if ( a_ctx->grpsz && a_ctx->gok [l_xor] && (vbk$get16(a_ctx->gbuf + (size_t) l_xor * a_ctx->bsize + BBH$W_APPLIC) != VBK$K_VMSAPP_XOR) )
		l_xor	= (uint32_t) -1;	/* A group cut short: its XOR block never written	*/

	if ( (l_nbad == 1) && (l_xor != (uint32_t) -1) && (l_bad != l_xor) && (a_ctx->gn > 1) )
		{
		/* One bad: the XOR of all the others is that block; its own fields made again */
		uint8_t *	l_d = a_ctx->gbuf + (size_t) l_bad * a_ctx->bsize;
		uint8_t *	l_src = a_ctx->gbuf + (size_t) (l_bad ? 0 : 1) * a_ctx->bsize;

		memcpy(l_d, l_src, a_ctx->bsize);

		for ( uint32_t i = 0; i < a_ctx->gn; i++ )
			{
			const uint8_t *	l_s = a_ctx->gbuf + (size_t) i * a_ctx->bsize;

			if ( (i == l_bad) || (l_s == l_src) )
				continue;

			for ( uint32_t k = 0; k < a_ctx->bsize; k++ )
				l_d [k] ^= l_s [k];
			}

		vbk$put16(l_d + BBH$W_APPLIC, VBK$K_VMSAPP_DATA);
		vbk$put32(l_d + BBH$L_NUMBER, (uint32_t) (a_ctx->gbase + l_bad));

		if ( vbk$get16(l_d + BBH$W_SIZE) == VBK$K_VMSHDR )
			{
			a_ctx->gok [l_bad] = 1;
			a_ctx->nrepaired++;

			if ( a_ctx->evcb )
				a_ctx->evcb(a_ctx->evarg, VBK$K_EV_REPAIRED, 1, a_ctx->gbase + l_bad);
			}
		}

	/* What is still bad is lost; a bad XOR block costs nothing */
	for ( uint32_t i = 0; i < a_ctx->gn; i++ )
		if ( !a_ctx->gok [i] && (i != l_xor) )
			{
			a_ctx->nlost++;

			if ( a_ctx->evcb )
				a_ctx->evcb(a_ctx->evarg, VBK$K_EV_LOST, 1, a_ctx->gbase + i);
			}

	if ( l_xor != (uint32_t) -1 )
		a_ctx->gok [l_xor] = 0, a_ctx->gn = l_xor ? l_xor : a_ctx->gn;

	return	STS$K_SUCCESS;
}


/*
**  The next data block of the saveset into BLK; STS$K_WARN - the end
*/
static	int	s_vbk$vmsblock	(
		VBK$VMS *	a_ctx
			)
{
	for ( ;; )
		{
		while ( a_ctx->gnext < a_ctx->gn )
			{
			uint32_t	l_i = a_ctx->gnext++;
			const uint8_t *	l_b = a_ctx->gbuf + (size_t) l_i * a_ctx->bsize;

			if ( !a_ctx->gok [l_i] )
				{
				a_ctx->gap	= 1;
				continue;
				}

			/* An XOR block where none was looked for: a group of another size */
			if ( vbk$get16(l_b + BBH$W_APPLIC) != VBK$K_VMSAPP_DATA )
				continue;

			a_ctx->blk	= l_b;
			a_ctx->blkno	= a_ctx->gbase + l_i;
			a_ctx->roff	= VBK$K_VMSHDR;

			return	STS$K_SUCCESS;
			}

		if ( a_ctx->eof && (a_ctx->ipre >= a_ctx->npre) )
			return	STS$K_WARN;

		if ( !(1 & s_vbk$vmsgroup(a_ctx)) )
			return	STS$K_WARN;
		}
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Begin to read a saveset: its first block read and judged, the
**	SUMMARY in it taken for the group size.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The context, filled
**	a_fd		The saveset, at its first octet (a file or a pipe)
**	a_evcb		The callback of the events, or NULL
**	a_evarg		... its argument
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- open;
**	STS$K_ERROR	- not a saveset of BACKUP (ERR 0), or it cannot be
**			  read (ERR the errno);
**	STS$K_FATAL	- no memory.
**--
*/
int	vbk$vms_open	(
		VBK$VMS *	a_ctx,
		int		a_fd,
		void		(*a_evcb) (void *, int, uint32_t, uint64_t),
		void *		a_evarg
			)
{
uint8_t		l_hdr [VBK$K_VMSHDR];
size_t		l_got = 0;
int64_t		l_rc;

	memset(a_ctx, 0, sizeof(*a_ctx));
	a_ctx->fd	= a_fd;
	a_ctx->evcb	= a_evcb;
	a_ctx->evarg	= a_evarg;

	while ( l_got < sizeof(l_hdr) )
		{
		if ( 0 > (l_rc = vbk$os_read(a_fd, l_hdr + l_got, sizeof(l_hdr) - l_got)) )
			{
			if ( errno == EINTR )
				continue;

			a_ctx->err	= errno;

			return	STS$K_ERROR;
			}

		if ( !l_rc )
			break;

		l_got	+= (size_t) l_rc;
		}

	if ( !(1 & vbk$vms_probe(l_hdr, l_got)) )
		return	STS$K_ERROR;

	a_ctx->bsize	= vbk$get32(l_hdr + BBH$L_BLOCKSIZE);
	a_ctx->nocrc	= !!(vbk$get32(l_hdr + BBH$L_FLAGS) & BBH$M_NOCRC);

	/* The group, and the blocks read ahead: the largest group each */
	if ( !(a_ctx->gbuf = malloc((size_t) (VBK$K_VMSMAXGRP + 1) * a_ctx->bsize))
		|| !(a_ctx->pre = malloc((size_t) (VBK$K_VMSMAXGRP + 1) * a_ctx->bsize)) )
		{
		vbk$vms_close(a_ctx);

		return	STS$K_FATAL;
		}

	memcpy(a_ctx->pre, l_hdr, sizeof(l_hdr));

	for ( l_got = sizeof(l_hdr); l_got < a_ctx->bsize; )
		{
		if ( 0 > (l_rc = vbk$os_read(a_fd, a_ctx->pre + l_got, a_ctx->bsize - l_got)) )
			{
			if ( errno == EINTR )
				continue;

			a_ctx->err	= errno;
			vbk$vms_close(a_ctx);

			return	STS$K_ERROR;
			}

		if ( !l_rc )
			break;

		l_got	+= (size_t) l_rc;
		}

	if ( l_got < a_ctx->bsize )
		{
		vbk$vms_close(a_ctx);

		return	STS$K_ERROR;
		}

	a_ctx->npre	= 1;

	/* The group size: from the SUMMARY of the first block, when it is good */
	if ( s_vbk$vmsgood(a_ctx, a_ctx->pre) )
		{
		uint32_t	l_off = VBK$K_VMSHDR;

		while ( l_off + VBK$K_VMSBRH <= a_ctx->bsize )
			{
			uint16_t	l_rs = vbk$get16(a_ctx->pre + l_off), l_rt = vbk$get16(a_ctx->pre + l_off + 2);

			if ( (l_rt == VBK$K_VMSRT_NULL) || (l_off + VBK$K_VMSBRH + l_rs > a_ctx->bsize) )
				break;

			if ( l_rt == VBK$K_VMSRT_SUMMARY )
				{
				a_ctx->hassum	= 1 & vbk$vms_summary(a_ctx->pre + l_off + VBK$K_VMSBRH, l_rs, &a_ctx->sum);
				break;
				}

			l_off	+= VBK$K_VMSBRH + l_rs;
			}
		}

	if ( a_ctx->hassum )
		a_ctx->grpsz	= (a_ctx->sum.grpsz <= VBK$K_VMSMAXGRP) ? a_ctx->sum.grpsz : 0;
	else	{
		/* The first block is bad: the group size from the first good XOR block - its number */
		while ( a_ctx->npre <= VBK$K_VMSMAXGRP )
			{
			uint8_t *	l_b = a_ctx->pre + (size_t) a_ctx->npre * a_ctx->bsize;

			if ( 0 >= s_vbk$vmsread(a_ctx, l_b) )
				{
				a_ctx->eof	= 1;
				break;
				}

			a_ctx->npre++;

			if ( s_vbk$vmsgood(a_ctx, l_b) && (vbk$get16(l_b + BBH$W_APPLIC) == VBK$K_VMSAPP_XOR)
				&& (vbk$get32(l_b + BBH$L_NUMBER) == a_ctx->npre) )
				{
				a_ctx->grpsz	= a_ctx->npre - 1;
				break;
				}
			}
		}

	return	STS$K_SUCCESS;
}


void	vbk$vms_close	(
		VBK$VMS *	a_ctx
			)
{
	free(a_ctx->gbuf);
	free(a_ctx->pre);
	a_ctx->gbuf	= NULL;
	a_ctx->pre	= NULL;
	a_ctx->blk	= NULL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The next record of the saveset.  The NULL records - the rest of a
**	block - are passed over.
**
**  FORMAL PARAMETERS:
**
**	a_ctx		The saveset
**	a_rec		Receives the record; its body is good until the next call
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- a record;
**	STS$K_WARN	- the end of the saveset.
**--
*/
int	vbk$vms_next	(
		VBK$VMS *	a_ctx,
		VBK$VMSREC *	a_rec
			)
{
	for ( ;; )
		{
		uint16_t	l_rs, l_rt;

		if ( !a_ctx->blk || (a_ctx->roff + VBK$K_VMSBRH > a_ctx->bsize) )
			{
			if ( !(1 & s_vbk$vmsblock(a_ctx)) )
				return	STS$K_WARN;
			}

		l_rs	= vbk$get16(a_ctx->blk + a_ctx->roff);
		l_rt	= vbk$get16(a_ctx->blk + a_ctx->roff + 2);

		if ( l_rt == VBK$K_VMSRT_NULL )
			{
			a_ctx->roff	= a_ctx->bsize;
			continue;
			}

		if ( a_ctx->roff + VBK$K_VMSBRH + l_rs > a_ctx->bsize )
			{
			/* A record that leaves its block: the rest of the block is given up */
			a_ctx->nbadrec++;

			if ( a_ctx->evcb )
				a_ctx->evcb(a_ctx->evarg, VBK$K_EV_BADREC, 1, a_ctx->blkno);

			a_ctx->roff	= a_ctx->bsize;
			a_ctx->gap	= 1;
			continue;
			}

		a_rec->rtype	= l_rt;
		a_rec->flags	= vbk$get32(a_ctx->blk + a_ctx->roff + 4);
		a_rec->address	= vbk$get32(a_ctx->blk + a_ctx->roff + 8);
		a_rec->body	= a_ctx->blk + a_ctx->roff + VBK$K_VMSBRH;
		a_rec->len	= l_rs;
		a_rec->resync	= a_ctx->gap;
		a_rec->blkno	= a_ctx->blkno;

		a_ctx->gap	= 0;
		a_ctx->roff	+= VBK$K_VMSBRH + l_rs;

		return	STS$K_SUCCESS;
		}
}


/*
**  A string attribute into a buffer, NUL-ended, its trailing blanks away
*/
static	void	s_vbk$vmsstr	(
		char *		a_out,
		size_t		a_size,
	const	uint8_t *	a_val,
		uint16_t	a_len,
		int		a_trim
			)
{
size_t	l_n = (a_len < a_size) ? a_len : a_size - 1;

	memcpy(a_out, a_val, l_n);

	while ( a_trim && l_n && (a_out [l_n - 1] == ' ') )
		l_n--;

	a_out [l_n] = '\0';
}


/*
**  An integer attribute of 1, 2, 4 or 8 octets
*/
static	uint64_t	s_vbk$vmsint	(
	const	uint8_t *	a_val,
		uint16_t	a_len
			)
{
uint64_t	l_v = 0;

	for ( uint16_t i = 0; (i < a_len) && (i < 8); i++ )
		l_v	|= (uint64_t) a_val [i] << (8 * i);

	return	l_v;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The SUMMARY record: what BACKUP/LIST shows at the head.
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_ERROR - its attributes make no sense.
**--
*/
int	vbk$vms_summary	(
	const	uint8_t *	a_body,
		uint32_t	a_len,
		VBK$VMSSUM *	a_sum
			)
{
uint32_t	l_off = 2;

	memset(a_sum, 0, sizeof(*a_sum));

	if ( (a_len < 2) || (vbk$get16(a_body) != VBK$K_VMSSTRUC) )
		return	STS$K_ERROR;

	while ( l_off + 4 <= a_len )
		{
		uint16_t	l_n = vbk$get16(a_body + l_off), l_t = vbk$get16(a_body + l_off + 2);
		const uint8_t *	l_v = a_body + l_off + 4;

		if ( !l_n && !l_t )
			break;

		if ( l_off + 4 + l_n > a_len )
			return	STS$K_ERROR;

		switch ( l_t )
			{
			case	BSA$K_SSNAME:	s_vbk$vmsstr(a_sum->ssname, sizeof(a_sum->ssname), l_v, l_n, 0);	break;
			case	BSA$K_COMMAND:	s_vbk$vmsstr(a_sum->command, sizeof(a_sum->command), l_v, l_n, 0);	break;
			case	BSA$K_USERNAME:	s_vbk$vmsstr(a_sum->user, sizeof(a_sum->user), l_v, l_n, 1);		break;
			case	BSA$K_USERUIC:	a_sum->uic = (uint32_t) s_vbk$vmsint(l_v, l_n);				break;
			case	BSA$K_DATE:	a_sum->date = s_vbk$vmsint(l_v, l_n);					break;
			case	BSA$K_OPSYS:	a_sum->opsys = (uint16_t) s_vbk$vmsint(l_v, l_n);			break;
			case	BSA$K_SYSVER:	s_vbk$vmsstr(a_sum->sysver, sizeof(a_sum->sysver), l_v, l_n, 1);	break;
			case	BSA$K_SIR:	a_sum->cpuid = (uint32_t) s_vbk$vmsint(l_v, l_n), a_sum->hascpuid = 1;	break;
			case	BSA$K_DRIVEID:	s_vbk$vmsstr(a_sum->node, sizeof(a_sum->node), l_v, l_n, 1);		break;
			case	BSA$K_BACKVER:	s_vbk$vmsstr(a_sum->bckver, sizeof(a_sum->bckver), l_v, l_n, 1);	break;
			case	BSA$K_BLOCKSIZE: a_sum->bsize = (uint32_t) s_vbk$vmsint(l_v, l_n);			break;
			case	BSA$K_XORSIZE:	a_sum->grpsz = (uint32_t) s_vbk$vmsint(l_v, l_n);			break;
			case	BSA$K_BUFFERS:	a_sum->bufcnt = (uint32_t) s_vbk$vmsint(l_v, l_n);			break;
			}

		l_off	+= 4 + l_n;
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The FILE record: the name and the attributes of a file, its size
**	in blocks and in octets from the end of file of its FAT.
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_ERROR - no name, or attributes that make
**	no sense.
**--
*/
int	vbk$vms_file	(
	const	uint8_t *	a_body,
		uint32_t	a_len,
		VBK$VMSFILE *	a_file
			)
{
uint32_t	l_off = 2;

	memset(a_file, 0, sizeof(*a_file));

	if ( (a_len < 2) || (vbk$get16(a_body) != VBK$K_VMSSTRUC) )
		return	STS$K_ERROR;

	while ( l_off + 4 <= a_len )
		{
		uint16_t	l_n = vbk$get16(a_body + l_off), l_t = vbk$get16(a_body + l_off + 2);
		const uint8_t *	l_v = a_body + l_off + 4;

		if ( !l_n && !l_t )
			break;

		if ( l_off + 4 + l_n > a_len )
			return	STS$K_ERROR;

		switch ( l_t )
			{
			case	BSA$K_FILENAME:
				if ( l_n >= sizeof(a_file->spec) )
					return	STS$K_ERROR;

				memcpy(a_file->spec, l_v, l_n);
				a_file->spec [l_n] = '\0';
				a_file->speclen	= l_n;
				break;

			case	BSA$K_FID:
			case	BSA$K_BACKLINK:
				for ( int i = 0; (i < 3) && (2 * i + 2 <= l_n); i++ )
					(l_t == BSA$K_FID ? a_file->fid : a_file->did) [i] = vbk$get16(l_v + 2 * i);
				break;

			case	BSA$K_UIC:	a_file->uic = (uint32_t) s_vbk$vmsint(l_v, l_n);	break;
			case	BSA$K_FPRO:	a_file->fpro = (uint16_t) s_vbk$vmsint(l_v, l_n);	break;
			case	BSA$K_UCHAR:	a_file->uchar = (uint32_t) s_vbk$vmsint(l_v, l_n);	break;
			case	BSA$K_REVISION:	a_file->revision = (uint16_t) s_vbk$vmsint(l_v, l_n);	break;
			case	BSA$K_CREDATE:	a_file->credate = s_vbk$vmsint(l_v, l_n);		break;
			case	BSA$K_REVDATE:	a_file->revdate = s_vbk$vmsint(l_v, l_n);		break;
			case	BSA$K_EXPDATE:	a_file->expdate = s_vbk$vmsint(l_v, l_n);		break;
			case	BSA$K_BAKDATE:	a_file->bakdate = s_vbk$vmsint(l_v, l_n);		break;
			case	BSA$K_ACCDATE:	a_file->accdate = s_vbk$vmsint(l_v, l_n);		break;
			case	BSA$K_ATTDATE:	a_file->attdate = s_vbk$vmsint(l_v, l_n);		break;

			case	BSA$K_RECATTR:
				/* FAT: the block numbers are two words, the high one first */
				if ( l_n < 22 )
					return	STS$K_ERROR;

				a_file->hasrecattr = 1;
				a_file->rfm	= l_v [0] & 0x0F;
				a_file->org	= (l_v [0] >> 4) & 0x0F;
				a_file->rattr	= l_v [1];
				a_file->rsize	= vbk$get16(l_v + 2);
				a_file->hiblk	= ((uint32_t) vbk$get16(l_v + 4) << 16) | vbk$get16(l_v + 6);
				a_file->efblk	= ((uint32_t) vbk$get16(l_v + 8) << 16) | vbk$get16(l_v + 10);
				a_file->ffbyte	= vbk$get16(l_v + 12);
				a_file->bktsize	= l_v [14];
				a_file->vfcsize	= l_v [15];
				a_file->maxrec	= vbk$get16(l_v + 16);
				a_file->defext	= vbk$get16(l_v + 18);
				a_file->gbc	= vbk$get16(l_v + 20);
				break;
			}

		l_off	+= 4 + l_n;
		}

	if ( !a_file->speclen )
		return	STS$K_ERROR;

	/* The end of file: block EFBLK, its first free octet FFBYTE; 0 - the block before it is full */
	if ( a_file->efblk )
		{
		a_file->used	= a_file->ffbyte ? a_file->efblk : a_file->efblk - 1;
		a_file->bytes	= (uint64_t) (a_file->efblk - 1) * 512 + a_file->ffbyte;
		}

	a_file->isdir	= !!(a_file->uchar & VBK$M_VMSCH_DIRECTORY);

	return	STS$K_SUCCESS;
}


/*
**  An octet of a name into UTF-8: the 8-bit names of ODS-5 are ISO Latin-1
*/
static	size_t	s_vbk$vmsput	(
		char *		a_out,
		size_t		a_size,
		size_t		a_n,
		uint32_t	a_c
			)
{
	/* What a Linux name cannot hold */
	if ( !a_c || (a_c == '/') )
		a_c	= '_';

	if ( a_c < 0x80 )
		{
		if ( a_n + 1 < a_size )
			a_out [a_n++] = (char) a_c;
		}
	else if ( a_c < 0x800 )
		{
		if ( a_n + 2 < a_size )
			{
			a_out [a_n++] = (char) (0xC0 | (a_c >> 6));
			a_out [a_n++] = (char) (0x80 | (a_c & 0x3F));
			}
		}
	else if ( a_n + 3 < a_size )
		{
		a_out [a_n++] = (char) (0xE0 | (a_c >> 12));
		a_out [a_n++] = (char) (0x80 | ((a_c >> 6) & 0x3F));
		a_out [a_n++] = (char) (0x80 | (a_c & 0x3F));
		}

	return	a_n;
}


static	int	s_vbk$hex	(
		char		a_c
			)
{
	if ( (a_c >= '0') && (a_c <= '9') )
		return	a_c - '0';

	if ( (a_c >= 'A') && (a_c <= 'F') )
		return	a_c - 'A' + 10;

	if ( (a_c >= 'a') && (a_c <= 'f') )
		return	a_c - 'a' + 10;

	return	-1;
}


/*
**  One component of a name, its ODS-5 escapes undone: ^_ a blank, ^xx an
**  octet in hex, ^Uxxxx a character of UCS-2, ^c the character c
*/
static	size_t	s_vbk$vmscomp	(
	const	char *		a_p,
		size_t		a_len,
		char *		a_out,
		size_t		a_size,
		size_t		a_n
			)
{
	for ( size_t i = 0; i < a_len; i++ )
		{
		uint32_t	l_c = (uint8_t) a_p [i];

		if ( (l_c == '^') && (i + 1 < a_len) )
			{
			int	l_h1, l_h2;

			i++;
			l_c	= (uint8_t) a_p [i];

			if ( l_c == '_' )
				l_c	= ' ';
			else if ( (l_c == 'U') && (i + 4 < a_len) && (0 <= s_vbk$hex(a_p [i + 1])) && (0 <= s_vbk$hex(a_p [i + 2]))
				&& (0 <= s_vbk$hex(a_p [i + 3])) && (0 <= s_vbk$hex(a_p [i + 4])) )
				{
				l_c	= (uint32_t) ((s_vbk$hex(a_p [i + 1]) << 12) | (s_vbk$hex(a_p [i + 2]) << 8)
					| (s_vbk$hex(a_p [i + 3]) << 4) | s_vbk$hex(a_p [i + 4]));
				i	+= 4;
				}
			else if ( (i + 1 < a_len) && (0 <= (l_h1 = s_vbk$hex(a_p [i]))) && (0 <= (l_h2 = s_vbk$hex(a_p [i + 1]))) )
				{
				l_c	= (uint32_t) ((l_h1 << 4) | l_h2);
				i++;
				}
			}

		a_n	= s_vbk$vmsput(a_out, a_size, a_n, l_c);
		}

	return	a_n;
}


/*
**  Where an unescaped <a_c> is in <a_p>, from <a_from>; <a_last> - the last one
*/
static	ptrdiff_t	s_vbk$vmsfind	(
	const	char *		a_p,
		size_t		a_len,
		char		a_c,
		int		a_last
			)
{
ptrdiff_t	l_at = -1;

	for ( size_t i = 0; i < a_len; i++ )
		{
		if ( a_p [i] == '^' )
			{
			i++;
			continue;
			}

		if ( a_p [i] == a_c )
			{
			l_at	= (ptrdiff_t) i;

			if ( !a_last )
				break;
			}
		}

	return	l_at;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	A file specification of OpenVMS made the relative name of a Linux
**	file: [LAISHEV.VBKS.SUB]V.TXT;2 - LAISHEV/VBKS/SUB/V.TXT, with
**	";2" when the version is asked for.  The escapes of ODS-5 are
**	undone, its 8-bit names made UTF-8; an empty type loses its dot; a
**	directory file (<a_dir>) becomes the name of the directory, without
**	.DIR and the version; [000000] is the top.
**
**  FORMAL PARAMETERS:
**
**	a_spec, a_len	The specification, as the FILE record holds it
**	a_dir		It is a directory file
**	a_withver	Add ";version"
**	a_out, a_size	The buffer
**
**  RETURN VALUE:
**	The length of the name; 0 - it does not fit, or makes no sense.
**--
*/
size_t	vbk$vms_unix	(
	const	char *		a_spec,
		size_t		a_len,
		int		a_dir,
		int		a_withver,
		char *		a_out,
		size_t		a_size
			)
{
const char *	l_p = a_spec, *l_name;
size_t		l_n = 0, l_nlen;
ptrdiff_t	l_close, l_semi, l_dot;
char		l_cb;

	if ( !a_size )
		return	0;

	/* A device or a node before the directory is not a part of the name */
	if ( 0 <= (l_dot = s_vbk$vmsfind(l_p, a_len, ':', 1)) )
		l_p += l_dot + 1, a_len -= (size_t) l_dot + 1;

	if ( a_len && ((*l_p == '[') || (*l_p == '<')) )
		{
		const char *	l_d;
		size_t		l_dlen;

		l_cb	= (*l_p == '[') ? ']' : '>';

		if ( 0 > (l_close = s_vbk$vmsfind(l_p, a_len, l_cb, 0)) )
			return	0;

		l_d	= l_p + 1;
		l_dlen	= (size_t) l_close - 1;

		/* The directories, component by component; 000000 is the top, nothing */
		while ( l_dlen )
			{
			ptrdiff_t	l_e = s_vbk$vmsfind(l_d, l_dlen, '.', 0);
			size_t		l_clen = (l_e < 0) ? l_dlen : (size_t) l_e;

			if ( l_clen && !((l_clen == 6) && !memcmp(l_d, "000000", 6)) )
				{
				l_n	= s_vbk$vmscomp(l_d, l_clen, a_out, a_size, l_n);

				if ( l_n + 1 < a_size )
					a_out [l_n++] = '/';
				}

			if ( l_e < 0 )
				break;

			l_d	+= l_clen + 1;
			l_dlen	-= l_clen + 1;
			}

		l_p	+= l_close + 1;
		a_len	-= (size_t) l_close + 1;
		}

	/* NAME.TYPE;VERSION - the version after the last unescaped ";", the type after the last "." */
	l_name	= l_p;
	l_semi	= s_vbk$vmsfind(l_p, a_len, ';', 1);
	l_nlen	= (l_semi < 0) ? a_len : (size_t) l_semi;
	l_dot	= s_vbk$vmsfind(l_name, l_nlen, '.', 1);

	if ( !l_nlen )
		return	0;

	if ( a_dir && (l_dot >= 0) )
		l_nlen	= (size_t) l_dot;
	else if ( (l_dot >= 0) && ((size_t) l_dot + 1 == l_nlen) )
		l_nlen--;

	if ( !l_nlen )
		return	0;

	l_n	= s_vbk$vmscomp(l_name, l_nlen, a_out, a_size, l_n);

	if ( a_withver && !a_dir && (l_semi >= 0) )
		for ( size_t i = (size_t) l_semi; (i < a_len) && (l_n + 1 < a_size); i++ )
			a_out [l_n++] = l_p [i];

	if ( l_n + 1 >= a_size )
		return	0;

	a_out [l_n] = '\0';

	return	l_n;
}


/*
**  Two specifications the same file but for the version?
*/
int	vbk$vms_same	(
	const	char *		a_spec1,
		size_t		a_len1,
	const	char *		a_spec2,
		size_t		a_len2
			)
{
ptrdiff_t	l_s1 = s_vbk$vmsfind(a_spec1, a_len1, ';', 1), l_s2 = s_vbk$vmsfind(a_spec2, a_len2, ';', 1);

	if ( l_s1 >= 0 )
		a_len1	= (size_t) l_s1;

	if ( l_s2 >= 0 )
		a_len2	= (size_t) l_s2;

	return	(a_len1 == a_len2) && !memcmp(a_spec1, a_spec2, a_len1);
}


/*
**  VMS time into Unix time: 0 - it is none (0, or before 1970)
*/
int	vbk$vms_time	(
		uint64_t	a_vtime,
		int64_t *	a_sec,
		uint32_t *	a_nsec
			)
{
	if ( a_vtime <= VBK$K_VMSEPOCH )
		return	0;

	*a_sec	= (int64_t) ((a_vtime - VBK$K_VMSEPOCH) / 10000000ULL);
	*a_nsec	= (uint32_t) ((a_vtime - VBK$K_VMSEPOCH) % 10000000ULL) * 100;

	return	1;
}


/*
**  Two digits, or a blank and one
*/
static	char *	s_vbk$dd	(
		char *		a_p,
		unsigned	a_v,
		char		a_lead
			)
{
	*a_p++	= (a_v >= 10) ? (char) ('0' + a_v / 10) : a_lead;
	*a_p++	= (char) ('0' + a_v % 10);

	return	a_p;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	VMS time as $ASCTIM shows it: " 5-OCT-2026 18:44:13.19" - the
**	time of the system that wrote it (VMS keeps no time zone), the
**	day in the calendar of today back to 17-NOV-1858.
**
**  RETURN VALUE:
**	The length, 23; 0 - the buffer is too small.
**--
*/
size_t	vbk$vms_date	(
		uint64_t	a_vtime,
		char *		a_out,
		size_t		a_size
			)
{
static	const char	l_mon [] = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";
int64_t		l_z = (int64_t) (a_vtime / VBK$K_VMSDAY) - 40587 + 719468, l_era, l_doe, l_yoe, l_doy, l_mp;
uint64_t	l_t = a_vtime % VBK$K_VMSDAY;
unsigned	l_d, l_m;
int64_t		l_y;
char *		l_p = a_out;

	if ( a_size < 24 )
		return	0;

	/* The civil day from the days since 1-JAN-1970 (H. Hinnant) */
	l_era	= (l_z >= 0 ? l_z : l_z - 146096) / 146097;
	l_doe	= l_z - l_era * 146097;
	l_yoe	= (l_doe - l_doe / 1460 + l_doe / 36524 - l_doe / 146096) / 365;
	l_y	= l_yoe + l_era * 400;
	l_doy	= l_doe - (365 * l_yoe + l_yoe / 4 - l_yoe / 100);
	l_mp	= (5 * l_doy + 2) / 153;
	l_d	= (unsigned) (l_doy - (153 * l_mp + 2) / 5 + 1);
	l_m	= (unsigned) (l_mp < 10 ? l_mp + 3 : l_mp - 9);
	l_y	+= (l_m <= 2);

	l_p	= s_vbk$dd(l_p, l_d, ' ');
	*l_p++	= '-';
	memcpy(l_p, l_mon + 3 * (l_m - 1), 3);
	l_p	+= 3;
	*l_p++	= '-';
	l_p	= s_vbk$dd(l_p, (unsigned) (l_y / 100), '0');
	l_p	= s_vbk$dd(l_p, (unsigned) (l_y % 100), '0');
	*l_p++	= ' ';
	l_p	= s_vbk$dd(l_p, (unsigned) (l_t / 36000000000ULL), '0');
	*l_p++	= ':';
	l_p	= s_vbk$dd(l_p, (unsigned) (l_t / 600000000ULL % 60), '0');
	*l_p++	= ':';
	l_p	= s_vbk$dd(l_p, (unsigned) (l_t / 10000000ULL % 60), '0');
	*l_p++	= '.';
	l_p	= s_vbk$dd(l_p, (unsigned) (l_t / 100000ULL % 100), '0');
	*l_p	= '\0';

	return	(size_t) (l_p - a_out);
}


/*
**  The protection of a file into the mode bits: R - r, W - w for the
**  owner, the group and the world; E - x of a directory only (an image of
**  VMS does not run here, and RWED would make every text executable);
**  SYSTEM and D have no place
*/
uint32_t	vbk$vms_mode	(
		uint16_t	a_fpro,
		int		a_dir
			)
{
uint32_t	l_mode = 0;

	for ( int l_cls = 1; l_cls <= 3; l_cls++ )
		{
		unsigned	l_deny = (a_fpro >> (4 * l_cls)) & 0x0F;

		l_mode	= (l_mode << 3) | (!(l_deny & 1) ? 4 : 0) | (!(l_deny & 2) ? 2 : 0) | (a_dir && !(l_deny & 4) ? 1 : 0);
		}

	return	l_mode;
}


const char *	vbk$vms_rfmname	(
		uint8_t		a_rfm
			)
{
static	const char *	l_names [] = { "Undefined", "Fixed", "Variable", "VFC", "Stream", "Stream_LF", "Stream_CR" };

	return	(a_rfm < sizeof(l_names) / sizeof(l_names [0])) ? l_names [a_rfm] : "Unknown";
}


const char *	vbk$vms_orgname	(
		uint8_t		a_org
			)
{
static	const char *	l_names [] = { "Sequential", "Relative", "Indexed", "Direct" };

	return	(a_org < sizeof(l_names) / sizeof(l_names [0])) ? l_names [a_org] : "Unknown";
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	What the records of a file become (doc/vmsbackup.md, section 5):
**	sequential files with a carriage control are text, made a text of
**	Linux; the others - no carriage control, relative, indexed - are
**	copied as they are on the disk.
**--
*/
int	vbk$vms_cnvmode	(
	const	VBK$VMSFILE *	a_file
			)
{
int	l_cc = !!(a_file->rattr & (VBK$M_VMSRAT_CR | VBK$M_VMSRAT_FTN | VBK$M_VMSRAT_PRN));

	if ( !a_file->hasrecattr || (a_file->org != VBK$K_VMSORG_SEQ) )
		return	VBK$K_VMSCNV_RAW;

	switch ( a_file->rfm )
		{
		case	VBK$K_VMSRFM_FIX:	return	(l_cc && a_file->rsize) ? VBK$K_VMSCNV_FIX : VBK$K_VMSCNV_RAW;
		case	VBK$K_VMSRFM_VAR:	return	l_cc ? VBK$K_VMSCNV_VAR : VBK$K_VMSCNV_RAW;
		case	VBK$K_VMSRFM_VFC:	return	l_cc ? VBK$K_VMSCNV_VFC : VBK$K_VMSCNV_RAW;
		case	VBK$K_VMSRFM_STM:	return	VBK$K_VMSCNV_STM;
		case	VBK$K_VMSRFM_STMCR:	return	VBK$K_VMSCNV_STMCR;
		default:			return	VBK$K_VMSCNV_RAW;
		}
}


void	vbk$vms_cnvinit	(
		VBK$VMSCNV *	a_cnv,
	const	VBK$VMSFILE *	a_file,
		int		a_mode,
		VBK$VMSOUT	a_out,
		void *		a_arg
			)
{
	memset(a_cnv, 0, sizeof(*a_cnv) - sizeof(a_cnv->obuf));
	a_cnv->olen	= 0;
	a_cnv->mode	= a_mode;
	a_cnv->ftn	= !!(a_file->rattr & VBK$M_VMSRAT_FTN);
	a_cnv->blk	= !!(a_file->rattr & VBK$M_VMSRAT_BLK);
	a_cnv->rsize	= a_file->rsize;
	a_cnv->vfc	= a_file->vfcsize ? a_file->vfcsize : 2;
	a_cnv->out	= a_out;
	a_cnv->arg	= a_arg;
}


static	void	s_vbk$cflush	(
		VBK$VMSCNV *	a_cnv
			)
{
	if ( a_cnv->olen && !a_cnv->failed && !(1 & a_cnv->out(a_cnv->arg, a_cnv->obuf, a_cnv->olen)) )
		a_cnv->failed	= 1;

	a_cnv->olen	= 0;
}


static	inline	void	s_vbk$cput	(
		VBK$VMSCNV *	a_cnv,
		uint8_t		a_c
			)
{
	if ( a_cnv->olen == sizeof(a_cnv->obuf) )
		s_vbk$cflush(a_cnv);

	a_cnv->obuf [a_cnv->olen++] = a_c;
}


/*
**  A record begins: the first octet of a Fortran carriage control said
*/
static	void	s_vbk$crecend	(
		VBK$VMSCNV *	a_cnv
			)
{
	s_vbk$cput(a_cnv, '\n');
}


/*
**  The octets to the next block of 512 are passed over
*/
static	void	s_vbk$ctoblock	(
		VBK$VMSCNV *	a_cnv
			)
{
	a_cnv->skip	= (uint32_t) ((512 - (a_cnv->off % 512)) % 512);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The next octets of a file, in their order, made what its records
**	become; the end of file is the caller's to keep - no octet after it
**	is given.  What is made goes to the output function in pieces.
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_ERROR - the output function failed.
**--
*/
int	vbk$vms_cnv	(
		VBK$VMSCNV *	a_cnv,
	const	uint8_t *	a_in,
		size_t		a_len
			)
{
	if ( a_cnv->mode == VBK$K_VMSCNV_RAW )
		{
		s_vbk$cflush(a_cnv);

		if ( a_len && !a_cnv->failed && !(1 & a_cnv->out(a_cnv->arg, a_in, a_len)) )
			a_cnv->failed	= 1;

		a_cnv->off	+= a_len;

		return	a_cnv->failed ? STS$K_ERROR : STS$K_SUCCESS;
		}

	for ( size_t i = 0; i < a_len; i++, a_cnv->off++ )
		{
		uint8_t	l_c = a_in [i];

		if ( a_cnv->skip )
			{
			a_cnv->skip--;
			continue;
			}

		switch ( a_cnv->mode )
			{
			case	VBK$K_VMSCNV_STM:
				/* CR LF ends a record: LF; a CR alone stays */
				if ( a_cnv->cr && (l_c != '\n') )
					s_vbk$cput(a_cnv, '\r');

				if ( !(a_cnv->cr = (l_c == '\r')) )
					s_vbk$cput(a_cnv, l_c);
				continue;

			case	VBK$K_VMSCNV_STMCR:
				s_vbk$cput(a_cnv, (l_c == '\r') ? '\n' : l_c);
				continue;
			}

		/* FIX, VAR, VFC: state 0 - a record begins, 1 - its octets, 2 - the pad after an odd one */
		if ( a_cnv->state == 2 )
			{
			a_cnv->state	= 0;
			continue;
			}

		if ( a_cnv->state == 0 )
			{
			if ( a_cnv->mode == VBK$K_VMSCNV_FIX )
				{
				/* Records that do not cross a block: one that does not fit begins in the next */
				if ( a_cnv->blk && (a_cnv->rsize <= 512) && ((a_cnv->off % 512) + a_cnv->rsize > 512) )
					{
					s_vbk$ctoblock(a_cnv);

					if ( a_cnv->skip )
						{
						a_cnv->skip--;
						continue;
						}
					}

				a_cnv->left	= a_cnv->rsize;
				a_cnv->pad	= a_cnv->rsize & 1;
				a_cnv->first	= 1;
				a_cnv->state	= 1;
				}
			else	{
				a_cnv->cnt [a_cnv->ncnt++] = l_c;

				if ( a_cnv->ncnt < 2 )
					continue;

				a_cnv->ncnt	= 0;
				a_cnv->left	= vbk$get16(a_cnv->cnt);

				/* 0xFFFF: nothing more in this block */
				if ( a_cnv->left == 0xFFFF )
					{
					a_cnv->off++;
					s_vbk$ctoblock(a_cnv);
					a_cnv->off--;
					continue;
					}

				a_cnv->pad	= a_cnv->left & 1;
				a_cnv->first	= 1;
				a_cnv->skip	= (a_cnv->mode == VBK$K_VMSCNV_VFC) ? ((a_cnv->vfc < a_cnv->left) ? a_cnv->vfc : a_cnv->left) : 0;
				a_cnv->left	-= a_cnv->skip;
				a_cnv->state	= 1;

				if ( !a_cnv->left && !a_cnv->skip )
					{
					s_vbk$crecend(a_cnv);
					a_cnv->state	= a_cnv->pad ? 2 : 0;
					}
				else if ( !a_cnv->left )
					{
					/* The control octets alone: the record ends when they are passed */
					a_cnv->state	= 3;
					}

				continue;
				}
			}

		if ( a_cnv->state == 3 )
			{
			/* After the control octets of an empty VFC record (the octet in hand is past them) */
			s_vbk$crecend(a_cnv);
			a_cnv->state	= a_cnv->pad ? 2 : 0;
			i--, a_cnv->off--;
			continue;
			}

		/* State 1: an octet of the record */
		if ( a_cnv->first && a_cnv->ftn )
			{
			/* Fortran: the first octet is the control - 0 a line more, 1 a new page */
			if ( l_c == '0' )
				s_vbk$cput(a_cnv, '\n');
			else if ( l_c == '1' )
				s_vbk$cput(a_cnv, '\f');
			}
		else	s_vbk$cput(a_cnv, l_c);

		a_cnv->first	= 0;

		if ( !--a_cnv->left )
			{
			s_vbk$crecend(a_cnv);
			a_cnv->state	= a_cnv->pad ? 2 : 0;
			}
		}

	return	a_cnv->failed ? STS$K_ERROR : STS$K_SUCCESS;
}


/*
**  The end of the file: what is held is given
*/
int	vbk$vms_cnvend	(
		VBK$VMSCNV *	a_cnv
			)
{
	if ( (a_cnv->mode == VBK$K_VMSCNV_STM) && a_cnv->cr )
		s_vbk$cput(a_cnv, '\r');

	/* A record cut by the end of file still ends its line; an empty VFC record too */
	if ( (a_cnv->state == 1) && (a_cnv->mode != VBK$K_VMSCNV_RAW) && !a_cnv->first )
		s_vbk$crecend(a_cnv);
	else if ( a_cnv->state == 3 )
		s_vbk$crecend(a_cnv);

	a_cnv->state	= 0;
	s_vbk$cflush(a_cnv);

	return	a_cnv->failed ? STS$K_ERROR : STS$K_SUCCESS;
}
