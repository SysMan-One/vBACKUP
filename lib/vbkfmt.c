#define	__MODULE__	"VBKFMT"
#define	__IDENT__	"X01-14"
#define	__REV__		"1.14.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkfmt.c
**
**  ABSTRACT:	Encoding and decoding of the pieces of the saveset format:
**		the block header with its checksum, and the TLV items the
**		record bodies are made of.  See doc/format.md.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-14		 5-OCT-2026	RRL
**		Version 2 (format.md 4.1): the version of a header kept and
**		written; type 7 PARITY; the parity blocks of version 2 carry the
**		header parity in RECOFF and PAYLEN, not checked as lengths.
**
**	X01-08		 5-OCT-2026	RRL
**		No printf in the core: VBK$VOLSPEC writes the number of a volume
**		itself, VBK$STRPUT copies a string.
**
**	X01-06		 5-OCT-2026	RRL
**		VBK$BLK_CHECK takes the block types EDATA and ETRAILER.
**
**	X01-04		 4-OCT-2026	RRL
**		VBK$VOLSPEC, from VBKWRT.C: the names of the volumes are part
**		of the format (section 2), not of the writer.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>

#include	"vbkfmt.h"
#include	"vbkos.h"

#define	VBK$K_CRCOFF	60			/* Offset of the checksum in the header		*/
#define	VBK$K_TLVINI	4096			/* First allocation of a TLV buffer		*/


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Encode a block header into the first 64 octets of a block.  The
**	checksum field is left zero; VBK$BLK_SEAL computes it once the
**	payload is in place.
**
**  FORMAL PARAMETERS:
**
**	a_hdr		The header, decoded
**	a_blk		The block, at least VBK$K_HDRSZ octets
**
**  RETURN VALUE:
**	None.
**--
*/
void	vbk$bhdr_put	(
	const	VBK$BHDR *	a_hdr,
		uint8_t *	a_blk
			)
{
	memcpy(a_blk, vbk$t_magic, sizeof(vbk$t_magic));
	vbk$put16(a_blk + 4, VBK$K_HDRSZ);
	vbk$put16(a_blk + 6, a_hdr->version ? a_hdr->version : VBK$K_VERSION);
	vbk$put32(a_blk + 8, a_hdr->bsize);
	a_blk [12]	= a_hdr->type;
	a_blk [13]	= a_hdr->flags;
	vbk$put16(a_blk + 14, a_hdr->gindex);
	memcpy(a_blk + 16, a_hdr->ssuuid, VBK$K_UUIDSZ);
	vbk$put64(a_blk + 32, a_hdr->blkno);
	vbk$put32(a_blk + 40, a_hdr->volno);
	vbk$put32(a_blk + 44, a_hdr->recoff);
	vbk$put32(a_blk + 48, a_hdr->paylen);
	vbk$put32(a_blk + 52, a_hdr->prvrecoff);
	vbk$put32(a_blk + 56, a_hdr->prvpaylen);
	vbk$put32(a_blk + VBK$K_CRCOFF, 0);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Compute the checksum of a complete block - the header with the
**	checksum field zero, then the whole payload area - and store it.
**
**  FORMAL PARAMETERS:
**
**	a_blk		The block, header already encoded
**	a_bsize		Size of the block
**
**  RETURN VALUE:
**	None.
**--
*/
void	vbk$blk_seal	(
		uint8_t *	a_blk,
		uint32_t	a_bsize
			)
{
	vbk$put32(a_blk + VBK$K_CRCOFF, 0);
	vbk$put32(a_blk + VBK$K_CRCOFF, $VBK_CRC(0, a_blk, a_bsize));
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Decode a block header without judging it: magic and version are
**	checked, nothing else.  Used to learn the block size of a volume
**	from its first octets.
**
**  FORMAL PARAMETERS:
**
**	a_blk		At least VBK$K_HDRSZ octets
**	a_hdr		Receives the decoded header
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the octets look like a block header of version 1;
**	STS$K_ERROR	- they do not.
**--
*/
int	vbk$bhdr_peek	(
	const	uint8_t *	a_blk,
		VBK$BHDR *	a_hdr
			)
{
	if ( memcmp(a_blk, vbk$t_magic, sizeof(vbk$t_magic)) )
		return	STS$K_ERROR;

	if ( (vbk$get16(a_blk + 4) != VBK$K_HDRSZ) || ((vbk$get16(a_blk + 6) != VBK$K_VERSION) && (vbk$get16(a_blk + 6) != VBK$K_VERSION2)) )
		return	STS$K_ERROR;

	a_hdr->version	= vbk$get16(a_blk + 6);
	a_hdr->bsize	= vbk$get32(a_blk + 8);
	a_hdr->type	= a_blk [12];
	a_hdr->flags	= a_blk [13];
	a_hdr->gindex	= vbk$get16(a_blk + 14);
	memcpy(a_hdr->ssuuid, a_blk + 16, VBK$K_UUIDSZ);
	a_hdr->blkno	= vbk$get64(a_blk + 32);
	a_hdr->volno	= vbk$get32(a_blk + 40);
	a_hdr->recoff	= vbk$get32(a_blk + 44);
	a_hdr->paylen	= vbk$get32(a_blk + 48);
	a_hdr->prvrecoff = vbk$get32(a_blk + 52);
	a_hdr->prvpaylen = vbk$get32(a_blk + 56);
	a_hdr->crc	= vbk$get32(a_blk + VBK$K_CRCOFF);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Judge a block read from a volume: it is valid when its header is
**	one of version 1 or 2 for this block size and this saveset, the
**	lengths in it are sane and the checksum is right.  That every block
**	of a saveset has the version of its VHDR is the reader's to check.
**
**  FORMAL PARAMETERS:
**
**	a_blk		The block
**	a_bsize		The block size of the saveset
**	a_ssuuid	UUID of the saveset, NULL - any
**	a_hdr		Receives the decoded header
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the block is valid;
**	STS$K_ERROR	- it is not, <a_hdr> is not to be trusted.
**--
*/
int	vbk$blk_check	(
	const	uint8_t *	a_blk,
		uint32_t	a_bsize,
	const	uint8_t *	a_ssuuid,
		VBK$BHDR *	a_hdr
			)
{
uint8_t		l_hdr [VBK$K_HDRSZ];
uint32_t	l_crc, l_psize = a_bsize - VBK$K_HDRSZ;

	if ( !(1 & vbk$bhdr_peek(a_blk, a_hdr)) )
		return	STS$K_ERROR;

	if ( (a_hdr->bsize != a_bsize) || (a_ssuuid && memcmp(a_hdr->ssuuid, a_ssuuid, VBK$K_UUIDSZ)) )
		return	STS$K_ERROR;

	if ( (a_hdr->type < VBK$K_BT_DATA) || (a_hdr->type > VBK$K_BT_PARITY)
		|| ((a_hdr->type == VBK$K_BT_PARITY) && (a_hdr->version != VBK$K_VERSION2)) )
		return	STS$K_ERROR;

	/* The parity blocks of version 2 carry the header parity in RECOFF and PAYLEN (format.md 4.1) */
	if ( (a_hdr->version == VBK$K_VERSION) || ((a_hdr->type != VBK$K_BT_XOR) && (a_hdr->type != VBK$K_BT_PARITY)) )
		{
		if ( a_hdr->paylen > l_psize )
			return	STS$K_ERROR;

		if ( (a_hdr->recoff != VBK$K_NONE) && (a_hdr->recoff >= l_psize) )
			return	STS$K_ERROR;
		}

	/* The checksum is taken with its own field zero: the header is copied, not patched */
	memcpy(l_hdr, a_blk, VBK$K_HDRSZ);
	vbk$put32(l_hdr + VBK$K_CRCOFF, 0);

	l_crc	= $VBK_CRC(0, l_hdr, VBK$K_HDRSZ);
	l_crc	= $VBK_CRC(l_crc, a_blk + VBK$K_HDRSZ, l_psize);

	return	(l_crc == a_hdr->crc) ? STS$K_SUCCESS : STS$K_ERROR;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Append a TLV item to a record body, growing the buffer as needed.
**
**  FORMAL PARAMETERS:
**
**	a_tlvb		The buffer
**	a_tag		Tag of the item
**	a_len		Length of the value
**	a_val		The value, may be NULL when <a_len> is 0
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- appended;
**	STS$K_FATAL	- no memory, errno says why.
**--
*/
int	vbk$tlv_put	(
		VBK$TLVB *	a_tlvb,
		uint16_t	a_tag,
		uint32_t	a_len,
	const	void *		a_val
			)
{
uint32_t	l_need = a_tlvb->len + VBK$K_TLVHDR + a_len;
uint8_t *	l_p;

	if ( l_need > a_tlvb->sz )
		{
		uint32_t	l_sz = a_tlvb->sz ? a_tlvb->sz : VBK$K_TLVINI;

		while ( l_sz < l_need )
			l_sz *= 2;

		if ( !(l_p = realloc(a_tlvb->buf, l_sz)) )
			return	STS$K_FATAL;

		a_tlvb->buf	= l_p;
		a_tlvb->sz	= l_sz;
		}

	l_p	= a_tlvb->buf + a_tlvb->len;

	vbk$put16(l_p, a_tag);
	vbk$put32(l_p + 2, a_len);

	if ( a_len )
		memcpy(l_p + VBK$K_TLVHDR, a_val, a_len);

	a_tlvb->len	= l_need;

	return	STS$K_SUCCESS;
}

int	vbk$tlv_u8	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint8_t a_val)
{
	return	vbk$tlv_put(a_tlvb, a_tag, 1, &a_val);
}

int	vbk$tlv_u16	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint16_t a_val)
{
uint8_t	l_v [2];

	vbk$put16(l_v, a_val);

	return	vbk$tlv_put(a_tlvb, a_tag, sizeof(l_v), l_v);
}

int	vbk$tlv_u32	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint32_t a_val)
{
uint8_t	l_v [4];

	vbk$put32(l_v, a_val);

	return	vbk$tlv_put(a_tlvb, a_tag, sizeof(l_v), l_v);
}

int	vbk$tlv_u64	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint64_t a_val)
{
uint8_t	l_v [8];

	vbk$put64(l_v, a_val);

	return	vbk$tlv_put(a_tlvb, a_tag, sizeof(l_v), l_v);
}

int	vbk$tlv_u64x2	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint64_t a_val1, uint64_t a_val2)
{
uint8_t	l_v [16];

	vbk$put64(l_v, a_val1);
	vbk$put64(l_v + 8, a_val2);

	return	vbk$tlv_put(a_tlvb, a_tag, sizeof(l_v), l_v);
}

int	vbk$tlv_time	(VBK$TLVB *a_tlvb, uint16_t a_tag, const VBK$TIME *a_tim)
{
uint8_t	l_v [12];

	vbk$put64(l_v, (uint64_t) a_tim->sec);
	vbk$put32(l_v + 8, a_tim->nsec);

	return	vbk$tlv_put(a_tlvb, a_tag, sizeof(l_v), l_v);
}

int	vbk$tlv_str	(VBK$TLVB *a_tlvb, uint16_t a_tag, const char *a_str)
{
	return	vbk$tlv_put(a_tlvb, a_tag, (uint32_t) strlen(a_str), a_str);
}

void	vbk$tlv_reset	(VBK$TLVB *a_tlvb)
{
	a_tlvb->len	= 0;
}

void	vbk$tlv_free	(VBK$TLVB *a_tlvb)
{
	free(a_tlvb->buf);

	a_tlvb->buf	= NULL;
	a_tlvb->len	= a_tlvb->sz = 0;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Step to the next TLV item of a record body.
**
**  FORMAL PARAMETERS:
**
**	a_body		The body
**	a_len		Length of the body
**	a_pos		Position, 0 to begin with; advanced past the item
**	a_tag		Receives the tag
**	a_vlen		Receives the length of the value
**	a_val		Receives the address of the value
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- an item has been returned;
**	STS$K_WARN	- the end of the body;
**	STS$K_ERROR	- the item runs past the end of the body.
**--
*/
int	vbk$tlv_next	(
	const	uint8_t *	a_body,
		uint32_t	a_len,
		uint32_t *	a_pos,
		uint16_t *	a_tag,
		uint32_t *	a_vlen,
	const	uint8_t **	a_val
			)
{
uint32_t	l_pos = *a_pos, l_vlen;

	if ( l_pos >= a_len )
		return	STS$K_WARN;

	if ( (a_len - l_pos) < VBK$K_TLVHDR )
		return	STS$K_ERROR;

	l_vlen	= vbk$get32(a_body + l_pos + 2);

	if ( l_vlen > (a_len - l_pos - VBK$K_TLVHDR) )
		return	STS$K_ERROR;

	*a_tag	= vbk$get16(a_body + l_pos);
	*a_vlen	= l_vlen;
	*a_val	= a_body + l_pos + VBK$K_TLVHDR;
	*a_pos	= l_pos + VBK$K_TLVHDR + l_vlen;

	return	STS$K_SUCCESS;
}


/*
**  An unsigned value of any of the widths 1, 2, 4, 8; anything else is 0
*/
uint64_t vbk$tlv_getu	(
		uint32_t	a_vlen,
	const	uint8_t *	a_val
			)
{
	switch ( a_vlen )
		{
		case	1:	return	a_val [0];
		case	2:	return	vbk$get16(a_val);
		case	4:	return	vbk$get32(a_val);
		case	8:	return	vbk$get64(a_val);
		}

	return	0;
}

void	vbk$tlv_gettime	(
		uint32_t	a_vlen,
	const	uint8_t *	a_val,
		VBK$TIME *	a_tim
			)
{
	a_tim->sec	= 0;
	a_tim->nsec	= 0;

	if ( a_vlen != 12 )
		return;

	a_tim->sec	= (int64_t) vbk$get64(a_val);
	a_tim->nsec	= vbk$get32(a_val + 8);
}


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
char	l_num [16];
size_t	l_len = strlen(a_spec), l_nd = 0;

	/* The name, then for k >= 2 "." and k in decimal, three digits at least - no printf in the core */
	if ( a_volno > 1 )
		{
		char	l_rev [12];
		size_t	l_r = 0;

		for ( uint32_t l_v = a_volno; l_v; l_v /= 10 )
			l_rev [l_r++] = (char) ('0' + (l_v % 10));

		while ( l_r < 3 )
			l_rev [l_r++] = '0';

		l_num [l_nd++] = '.';

		while ( l_r )
			l_num [l_nd++] = l_rev [--l_r];
		}

	if ( (l_len + l_nd) >= a_outsz )
		{
		if ( a_outsz )
			a_out [0] = '\0';

		return	STS$K_ERROR;
		}

	memcpy(a_out, a_spec, l_len);
	memcpy(a_out + l_len, l_num, l_nd);
	a_out [l_len + l_nd] = '\0';

	return	STS$K_SUCCESS;
}


/*
**  Copy a string into a buffer of <a_size>, cut to it, always ended by a NUL
*/
void	vbk$strput	(
		char *		a_buf,
		size_t		a_size,
	const	char *		a_src
			)
{
size_t	l_n = strlen(a_src);

	if ( !a_size )
		return;

	l_n	= (l_n < a_size) ? l_n : (a_size - 1);
	memcpy(a_buf, a_src, l_n);
	a_buf [l_n] = '\0';
}
