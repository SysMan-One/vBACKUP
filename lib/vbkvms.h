#ifndef	__VBKVMS$H__
#define	__VBKVMS$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKVMS"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-13"
#endif

#ifndef	__REV__
#define	__REV__		"1.13.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkvms.h
**
**  ABSTRACT:	The savesets of OpenVMS BACKUP, read: blocks, groups and
**		their XOR repair, records, the attributes of a file, its name
**		and dates, its records made into the bytes of a Linux file
**		(doc/vmsbackup.md).
**
**  DESCRIPTION: Like VBKRD.H, no stdio and nothing of StarLet: the
**		utility, vbkx and vbkx.exe link it alike.  What happens on
**		the way - a block rebuilt, lost, a record that makes no sense
**		- is reported through the callback of VBKRD.H, its events and
**		its arguments (the volume is always 1).
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

#include	<stdint.h>
#include	<stddef.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	VBK$K_VMSHDR	256			/* The block header (BBH)			*/
#define	VBK$K_VMSBRH	16			/* The record header (BRH)			*/
#define	VBK$K_VMSMINBSZ	2048			/* /BLOCK_SIZE of BACKUP: 2048 ...		*/
#define	VBK$K_VMSMAXBSZ	65535			/* ... 65535					*/
#define	VBK$K_VMSMAXGRP	100			/* /GROUP_SIZE: 0 ... 100			*/
#define	VBK$K_VMSNAME	1024			/* A file specification, as it is stored	*/
#define	VBK$K_VMSSTR	256			/* A string of the SUMMARY			*/

enum	{					/* BBH$W_APPLIC					*/
	VBK$K_VMSAPP_DATA = 1,
	VBK$K_VMSAPP_XOR = 2
	};

enum	{					/* BRH$W_RTYPE					*/
	VBK$K_VMSRT_NULL = 0,
	VBK$K_VMSRT_SUMMARY,
	VBK$K_VMSRT_VOLUME,
	VBK$K_VMSRT_FILE,
	VBK$K_VMSRT_VBN,
	VBK$K_VMSRT_PHYSVOL,
	VBK$K_VMSRT_LBN,
	VBK$K_VMSRT_FID
	};

enum	{					/* FAT$V_FILEORG, the high nibble of RTYPE	*/
	VBK$K_VMSORG_SEQ = 0,
	VBK$K_VMSORG_REL,
	VBK$K_VMSORG_IDX,
	VBK$K_VMSORG_DIRECT
	};

enum	{					/* FAT$V_RTYPE, the low nibble			*/
	VBK$K_VMSRFM_UDF = 0,
	VBK$K_VMSRFM_FIX,
	VBK$K_VMSRFM_VAR,
	VBK$K_VMSRFM_VFC,
	VBK$K_VMSRFM_STM,
	VBK$K_VMSRFM_STMLF,
	VBK$K_VMSRFM_STMCR
	};

#define	VBK$M_VMSRAT_FTN	0x01		/* FAT$B_RATTR					*/
#define	VBK$M_VMSRAT_CR		0x02
#define	VBK$M_VMSRAT_PRN	0x04
#define	VBK$M_VMSRAT_BLK	0x08		/* ... records do not cross a block		*/

#define	VBK$M_VMSCH_CONTIGB	0x00000020	/* FCH: contiguous-best-try			*/
#define	VBK$M_VMSCH_LOCKED	0x00000040
#define	VBK$M_VMSCH_CONTIG	0x00000080
#define	VBK$M_VMSCH_NOBACKUP	0x00000002
#define	VBK$M_VMSCH_DIRECTORY	0x00002000

/*
**  The SUMMARY of the saveset: what BACKUP/LIST shows at the head
*/
typedef struct vbk_vmssum_t
{
	char		ssname [VBK$K_VMSSTR];
	char		command [VBK$K_VMSSTR * 4];
	char		user [VBK$K_VMSSTR];
	uint32_t	uic;			/* Group in the high word, member in the low	*/
	uint64_t	date;			/* VMS time: 100 ns since 17-NOV-1858		*/
	uint16_t	opsys;
	char		sysver [VBK$K_VMSSTR];
	uint32_t	cpuid;
	char		node [VBK$K_VMSSTR];	/* "Written on"					*/
	char		bckver [VBK$K_VMSSTR];
	uint32_t	bsize, grpsz, bufcnt;
	int		hascpuid;
} VBK$VMSSUM;

/*
**  The attributes of a file, from its FILE record
*/
typedef struct vbk_vmsfile_t
{
	char		spec [VBK$K_VMSNAME];	/* [DIR.SUB]NAME.TYPE;VERSION, as stored	*/
	uint16_t	speclen;
	uint16_t	fid [3], did [3];	/* The file and its directory (BACKLINK)	*/
	uint32_t	uic;
	uint16_t	fpro;			/* 4 bits a class, S O G W from the low: set - denied */
	uint32_t	uchar;
	uint16_t	revision;
	uint64_t	credate, revdate, expdate, bakdate, accdate, attdate;
	int		hasrecattr;

	uint8_t		org, rfm, rattr;	/* FAT, the record attributes			*/
	uint16_t	rsize;
	uint32_t	hiblk, efblk;
	uint16_t	ffbyte;
	uint8_t		bktsize, vfcsize;
	uint16_t	maxrec, defext, gbc;

	uint32_t	used;			/* Blocks in use: what the listing shows	*/
	uint64_t	bytes;			/* Octets up to the end of file			*/
	int		isdir;
} VBK$VMSFILE;

/*
**  The context of a saveset being read
*/
typedef struct vbk_vms_t
{
	int		fd;
	uint32_t	bsize, grpsz;
	int		nocrc;			/* BACKUP/NOCRC: the blocks carry no CRC	*/
	VBK$VMSSUM	sum;
	int		hassum;

	uint8_t *	gbuf;			/* The group in hand, (grpsz + 1) blocks	*/
	uint8_t		gok [VBK$K_VMSMAXGRP + 1];
	uint32_t	gn;			/* Blocks of it read				*/
	uint32_t	gnext;			/* The next of them to give out			*/
	uint64_t	gbase;			/* The number of its first block (1 ...)	*/
	uint64_t	nread;			/* Blocks read from the saveset			*/
	int		eof;
	uint8_t *	pre;			/* Blocks read ahead by the open		*/
	uint32_t	npre, ipre;		/* ... how many, the next to take		*/

	const uint8_t *	blk;			/* The block whose records are being read	*/
	uint64_t	blkno;			/* ... its number				*/
	uint32_t	roff;
	int		gap;			/* A block lost since the last record		*/

	uint64_t	nblocks, nrepaired, nlost, nbadrec;

	void		(*evcb) (void *a_arg, int a_ev, uint32_t a_vol, uint64_t a_blk);
	void *		evarg;

	int		err;			/* errno of a failure				*/
} VBK$VMS;

/*
**  One record of the saveset, as VBK$VMS_NEXT gives it
*/
typedef struct vbk_vmsrec_t
{
	uint16_t	rtype;
	uint32_t	flags;
	uint32_t	address;		/* VBN: the first block of the file it carries	*/
	const uint8_t *	body;
	uint32_t	len;
	int		resync;			/* Blocks were lost before it			*/
	uint64_t	blkno;			/* The block it is in				*/
} VBK$VMSREC;

/*
**  The records of a file into the octets of a Linux file
*/
typedef	int	(*VBK$VMSOUT) (void *a_arg, const uint8_t *a_buf, size_t a_len);

enum	{					/* What becomes of the records			*/
	VBK$K_VMSCNV_RAW = 0,			/* As they are on the disk			*/
	VBK$K_VMSCNV_FIX,			/* Fixed, a carriage control: record + LF	*/
	VBK$K_VMSCNV_VAR,			/* Variable: the count away, record + LF	*/
	VBK$K_VMSCNV_VFC,			/* ... the fixed control too			*/
	VBK$K_VMSCNV_STM,			/* Stream: CR LF -> LF				*/
	VBK$K_VMSCNV_STMCR			/* Stream_CR: CR -> LF				*/
	};

typedef struct vbk_vmscnv_t
{
	int		mode;
	int		ftn;			/* Fortran carriage control: the first octet	*/
	int		blk;			/* Records do not cross a block			*/
	uint32_t	rsize, vfc;
	uint64_t	off;			/* Octets of the file taken			*/
	int		state;
	uint32_t	left, skip;
	uint8_t		cnt [2];
	uint32_t	ncnt;
	int		pad, cr, first;
	VBK$VMSOUT	out;
	void *		arg;
	size_t		olen;
	int		failed;
	uint8_t		obuf [16384];		/* Last: VBK$VMS_CNVINIT clears what is before it */
} VBK$VMSCNV;

int	vbk$vms_probe	(const uint8_t *a_hdr, size_t a_len);
int	vbk$vms_open	(VBK$VMS *a_ctx, int a_fd, void (*a_evcb) (void *, int, uint32_t, uint64_t), void *a_evarg);
void	vbk$vms_close	(VBK$VMS *a_ctx);
int	vbk$vms_next	(VBK$VMS *a_ctx, VBK$VMSREC *a_rec);
int	vbk$vms_file	(const uint8_t *a_body, uint32_t a_len, VBK$VMSFILE *a_file);
int	vbk$vms_summary	(const uint8_t *a_body, uint32_t a_len, VBK$VMSSUM *a_sum);
size_t	vbk$vms_unix	(const char *a_spec, size_t a_len, int a_dir, int a_withver, char *a_out, size_t a_size);
int	vbk$vms_same	(const char *a_spec1, size_t a_len1, const char *a_spec2, size_t a_len2);
int	vbk$vms_time	(uint64_t a_vtime, int64_t *a_sec, uint32_t *a_nsec);
size_t	vbk$vms_date	(uint64_t a_vtime, char *a_out, size_t a_size);
uint32_t vbk$vms_mode	(uint16_t a_fpro, int a_dir);
const char *vbk$vms_rfmname (uint8_t a_rfm);
const char *vbk$vms_orgname (uint8_t a_org);
int	vbk$vms_cnvmode	(const VBK$VMSFILE *a_file);
void	vbk$vms_cnvinit	(VBK$VMSCNV *a_cnv, const VBK$VMSFILE *a_file, int a_mode, VBK$VMSOUT a_out, void *a_arg);
int	vbk$vms_cnv	(VBK$VMSCNV *a_cnv, const uint8_t *a_in, size_t a_len);
int	vbk$vms_cnvend	(VBK$VMSCNV *a_cnv);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKVMS$H__ */
