#ifndef	__VBKRD$H__
#define	__VBKRD$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKRD"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-08"
#endif

#ifndef	__REV__
#define	__REV__		"1.8.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkrd.h
**
**  ABSTRACT:	The reading side of the saveset core (libvbkrd): volumes,
**		validation and repair of blocks, the record stream in both
**		access modes of format.md section 8.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-08		 5-OCT-2026	RRL
**		ISSTREAM, SPOS; VBK$K_STREAMBLK.
**
**	X01-06		 5-OCT-2026	RRL
**		Encrypted savesets: CRYPT, VBK$RD_SETKEY, the event BADTAG.
**
**	X01-05		 4-OCT-2026	RRL
**		VBK$RD_ADDVOL.
**
**	X01-04		 4-OCT-2026	RRL
**		No more of VBKWRT.H: the reader stands without the writer.
**
**	X01-03		 3-OCT-2026	RRL
**		GEND: where the group in hand ends, for VBK$RD_SEEK.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdint.h>

#include	"vbkfmt.h"
#include	"vbkcrp.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	VBK$K_MAXVOL	9999			/* Volumes looked for				*/
#define	VBK$K_VOLGAP	16			/* Missing names in a row that end the probe	*/
#define	VBK$K_STREAMBLK	(1ULL << 40)		/* The blocks a stream may have, its end unknown */

enum	{					/* Events reported through the callback		*/
	VBK$K_EV_REPAIRED = 1,			/* A block has been rebuilt from its group	*/
	VBK$K_EV_LOST,				/* A block is bad and cannot be rebuilt		*/
	VBK$K_EV_MISSVOL,			/* A volume is not there			*/
	VBK$K_EV_WRONGVOL,			/* A volume of another saveset, or no saveset	*/
	VBK$K_EV_BADREC,			/* A record header that makes no sense		*/
	VBK$K_EV_BADTAG				/* A block whose CRC is right, its TAG not	*/
	};

typedef struct vbk_rvol_t
{
	int		fd;			/* -1 - the volume is missing			*/
	uint64_t	firstblk;		/* blkno of its VHDR				*/
	uint64_t	nblk;			/* Whole blocks in it				*/
} VBK$RVOL;

/*
**  The context of a saveset being read.  A caller reads <bsize>, <grpsz>,
**  <nvols>, <summary>, <trailer>, <resync> and the counters; the rest is
**  the business of VBKRD.C.
*/
typedef struct vbk_rctx_t
{
	char		spec [VBK$K_SZ_SPEC];	/* Name of volume 1				*/
	VBK$RVOL *	vols;			/* Indexed by volno - 1				*/
	uint32_t	nvols;
	uint32_t	bsize, psize, grpsz;
	uint8_t		ssuuid [VBK$K_UUIDSZ];

	uint8_t *	summary;		/* Body of the SUMMARY of volume 1		*/
	uint32_t	sumlen;
	uint8_t *	trailer;		/* TLV body of the TRAILER, NULL - none		*/
	uint32_t	trllen;
	uint64_t	trlblk;			/* blkno of the TRAILER				*/
	uint8_t *	trlraw;			/* The ETRAILER block, until the key opens it	*/

	int		crypt;			/* Encrypted (format.md 6.10): CIPHER in VHDR	*/
	int		haskey;			/* ... and the keys are right			*/
	uint32_t	kdfiter;
	uint8_t		salt [VBK$K_SALTSZ];
	uint8_t		keycheck [VBK$K_KEYSZ];
	VBK$KEYS	keys;
	uint8_t		dtype;			/* Type of its DATA blocks: DATA or EDATA	*/
	int		isstream;		/* "-": a pipe, read once, forward only		*/
	uint64_t	spos;			/* ... octets of it consumed			*/
	int		rewound;		/* ... the group in hand given out once more	*/
	uint32_t	cap;			/* The most PAYLEN of a DATA block may be	*/

	uint8_t *	gbuf;			/* The group being read, (grpsz + 1) blocks	*/
	uint8_t		gok [VBK$K_MAXGRP + 1];
	VBK$BHDR	ghdr [VBK$K_MAXGRP + 1];
	uint32_t	gdata;			/* DATA blocks in the group			*/
	uint32_t	gnext;			/* Next of them to deliver			*/
	uint32_t	gvol;			/* Volume of the group				*/
	uint64_t	gpos;			/* Position of its first block in the volume	*/
	uint64_t	gend;			/* ... and of the block after it		*/
	uint32_t	curvol;			/* Where the next group is read from		*/
	uint64_t	curpos;

	const uint8_t *	pay;			/* Payload being consumed			*/
	uint32_t	paylen, payoff;
	uint32_t	payvol;
	uint64_t	payblk;
	int		gap;			/* A block has been lost since the last record	*/
	int		eof;

	uint8_t *	rec;			/* The record being assembled			*/
	uint32_t	recsz;
	int		resync;			/* The record returned follows a gap		*/
	int		pendrs;			/* A gap before the next record, from a seek	*/

	uint64_t	nrepaired, nlost;

	void		(*evcb) (void *a_arg, int a_ev, uint32_t a_vol, uint64_t a_blk);
	void *		evarg;

	int		err;			/* errno of a failure				*/
} VBK$RCTX;

int	vbk$rd_probe	(const char *a_spec);
int	vbk$rd_open	(VBK$RCTX *a_ctx, const char *a_spec, void (*a_evcb) (void *, int, uint32_t, uint64_t), void *a_evarg);
void	vbk$rd_close	(VBK$RCTX *a_ctx);
int	vbk$rd_addvol	(VBK$RCTX *a_ctx, uint32_t a_volno, const char *a_spec);
int	vbk$rd_setkey	(VBK$RCTX *a_ctx, const void *a_pass, size_t a_plen);
int	vbk$rd_rewind	(VBK$RCTX *a_ctx);
int	vbk$rd_seek	(VBK$RCTX *a_ctx, const VBK$LOC *a_loc);
int	vbk$rd_next	(VBK$RCTX *a_ctx, uint16_t *a_type, const uint8_t **a_body, uint32_t *a_len, VBK$LOC *a_loc);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKRD$H__ */
