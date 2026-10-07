#ifndef	__VBKFMT$H__
#define	__VBKFMT$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKFMT"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-21"
#endif

#ifndef	__REV__
#define	__REV__		"1.21.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkfmt.h
**
**  ABSTRACT:	Constants and encoding helpers of the saveset format,
**		version 1.  The bytes on the medium are described by
**		doc/format.md; every constant here is a constant there.
**
**  DESCRIPTION: All fields are little-endian and are encoded byte by
**		byte by vbk$putNN/vbk$getNN - no structure is ever written
**		or read as it is, so the layout does not depend on the
**		compiler or on the byte order of the host.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-21		 7-OCT-2026	RRL
**		Version 3 and the SOLID record (format.md 5, 6.12): the records of
**		several small files compressed together; VBK$K_TAG_SOLID of a catalog
**		entry.
**
**	X01-18		 6-OCT-2026	RRL
**		VBK$K_TAG_WINATTR, VBK$K_TAG_NTSD: the attributes and the security
**		descriptor of a file of Windows (format.md 6.1).
**
**	X01-14		 5-OCT-2026	RRL
**		VBK$K_VERSION2, VBK$K_BT_PARITY, VBK$K_TAG_PARITY; VERSION in
**		VBK$BHDR.
**
**	X01-08		 5-OCT-2026	RRL
**		VBK$STRPUT.
**
**	X01-06		 5-OCT-2026	RRL
**		The block types EDATA, ETRAILER; the tags CIPHER, KDF, KDFITER,
**		SALT, KEYCHECK (format.md 6.10).
**
**	X01-04		 4-OCT-2026	RRL
**		DATAZ, the compressed DATA record; the SUMMARY tag COMPRESS;
**		PHYSICAL, DEVSIZE, SECTORSIZE; IMAGE, FSTYPE, FSLABEL, FSUUID,
**		FSUSED, ROOTATTR, MOUNTOPTS.
**		VBK$K_SZ_SPEC and VBK$VOLSPEC, from VBKWRT.H.
**
**	X01-02		 3-OCT-2026	RRL
**		Stage 2: SUMMARY tags KIND, FILTER; the catalog STATUS PRESENT;
**		the journal record types and tags (format.md, 6.4-6.6 and 9).
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdint.h>
#include	<stddef.h>
#include	<string.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
**  Block geometry, format.md section 2 and 3
*/
#define	VBK$K_VERSION	1			/* Format version				*/
#define	VBK$K_VERSION2	2			/* ... with PARITY blocks (format.md 4.1)	*/
#define	VBK$K_VERSION3	3			/* ... with SOLID records, PARITY by the tag	*/
#define	VBK$K_HDRSZ	64			/* Block header					*/
#define	VBK$K_SZ_SPEC	4096			/* Longest volume specification			*/
#define	VBK$K_MINBSZ	8192			/* Smallest block				*/
#define	VBK$K_MAXBSZ	1048576			/* Largest block				*/
#define	VBK$K_DEFBSZ	65536			/* Default block				*/
#define	VBK$K_BSZALIGN	512			/* A block size is a multiple of this		*/
#define	VBK$K_DEFGRP	10			/* Default group size				*/
#define	VBK$K_MAXGRP	100			/* Largest group size				*/
#define	VBK$K_RECHDR	8			/* Record header				*/
#define	VBK$K_DATAHDR	16			/* fileno, reserved, offset of a DATA record	*/
#define	VBK$K_MAXDATA	1048576			/* Data bytes in one DATA record		*/
#define	VBK$K_MAXCAT	1048576			/* Body of one CATALOG record			*/
#define	VBK$K_MAXREC	(16 * 1048576)		/* Sanity ceiling of a record body		*/
#define	VBK$K_SOLIDHDR	12			/* codec, rawlen, count of a SOLID		*/
#define	VBK$K_MAXSOLID	(1048576 + 65536)	/* The records of a SOLID, at most		*/
#define	VBK$K_SOLIDFILE	262144			/* A file of this size at most goes into one	*/
#define	VBK$K_SOLIDGRP	1048576			/* The records of a SOLID the writer aims at	*/
#define	VBK$K_MAXCMD	4096			/* CMDLINE of the SUMMARY			*/
#define	VBK$K_NONE	0xFFFFFFFFU		/* "No record begins here"			*/
#define	VBK$K_TLVHDR	6			/* u16 tag, u32 length				*/
#define	VBK$K_UUIDSZ	16
#define	VBK$K_JNLHDR	16			/* Journal header				*/

static const uint8_t	vbk$t_jmagic [4] = { 'V', 'B', 'K', 'J' };

static const uint8_t	vbk$t_magic [4] = { 'V', 'B', 'K', 'B' };

enum	{					/* Block types					*/
	VBK$K_BT_DATA	= 1,
	VBK$K_BT_XOR,
	VBK$K_BT_VHDR,
	VBK$K_BT_TRAILER,
	VBK$K_BT_EDATA,				/* DATA of an encrypted saveset (X01-06)	*/
	VBK$K_BT_ETRAILER,			/* TRAILER of an encrypted saveset		*/
	VBK$K_BT_PARITY				/* A parity row >= 1, version 2 (X01-14)	*/
	};

#define	VBK$M_LASTINVOL	1			/* Last block of a volume			*/
#define	VBK$M_LASTINSET	2			/* Last block of the saveset			*/

enum	{					/* Record types					*/
	VBK$K_RT_SUMMARY = 1,
	VBK$K_RT_FILE,
	VBK$K_RT_DATA,
	VBK$K_RT_FEND,
	VBK$K_RT_CATALOG,
	VBK$K_RT_END,
	VBK$K_RT_DATAZ,				/* DATA, compressed (X01-04)			*/
	VBK$K_RT_SOLID,				/* FILE, DATA, FEND of small files, compressed (X01-21) */

	VBK$K_RT_SSET	= 16,			/* Journal: a saveset				*/
	VBK$K_RT_FSTATE				/* Journal: the state of a saved file		*/
	};

enum	{					/* TLV tags, one space for all records		*/
	VBK$K_TAG_FILENO = 1,
	VBK$K_TAG_PATH,
	VBK$K_TAG_FTYPE,
	VBK$K_TAG_MODE,
	VBK$K_TAG_UID,
	VBK$K_TAG_GID,
	VBK$K_TAG_UNAME,
	VBK$K_TAG_GNAME,
	VBK$K_TAG_SIZE,
	VBK$K_TAG_MTIME,
	VBK$K_TAG_ATIME,
	VBK$K_TAG_CTIME,
	VBK$K_TAG_BTIME,
	VBK$K_TAG_RDEV,
	VBK$K_TAG_LINK,
	VBK$K_TAG_XATTR,
	VBK$K_TAG_FSFLAGS,
	VBK$K_TAG_DEVINO,
	VBK$K_TAG_BASEIDX,
	VBK$K_TAG_NLINK,
	VBK$K_TAG_WINATTR,			/* FILE: FILE_ATTRIBUTE_* of Windows, u32	*/
	VBK$K_TAG_NTSD,				/* FILE: the security descriptor, self-relative	*/
	VBK$K_TAG_SOLID,			/* CATALOG: u8 1 - LOC is the SOLID that holds it */

	VBK$K_TAG_CRC	= 32,
	VBK$K_TAG_STATUS,
	VBK$K_TAG_LOCVOL,
	VBK$K_TAG_LOCBLK,
	VBK$K_TAG_LOCOFF,

	VBK$K_TAG_PRODUCT = 64,
	VBK$K_TAG_HOST,
	VBK$K_TAG_USER,
	VBK$K_TAG_CMDLINE,
	VBK$K_TAG_CREATED,
	VBK$K_TAG_BASE,
	VBK$K_TAG_BLOCKSIZE,
	VBK$K_TAG_GROUPSIZE,
	VBK$K_TAG_VOLSIZE,
	VBK$K_TAG_COMMENT,
	VBK$K_TAG_SYSTEM,
	VBK$K_TAG_KIND,
	VBK$K_TAG_FILTER,
	VBK$K_TAG_COMPRESS,			/* SUMMARY: the codec of the DATAZ records	*/
	VBK$K_TAG_PHYSICAL,			/* SUMMARY, FILE, CATALOG: a device, /PHYSICAL	*/
	VBK$K_TAG_DEVSIZE,			/* SUMMARY: its size in bytes			*/
	VBK$K_TAG_SECTORSIZE,			/* SUMMARY: its logical sector			*/
	VBK$K_TAG_IMAGE,			/* SUMMARY: a whole file system, /IMAGE		*/
	VBK$K_TAG_FSTYPE,			/* ... its type, as mount knows it		*/
	VBK$K_TAG_FSLABEL,			/* ... its label				*/
	VBK$K_TAG_FSUUID,			/* ... its UUID, as text			*/
	VBK$K_TAG_FSUSED,			/* ... bytes in use				*/
	VBK$K_TAG_ROOTATTR,			/* ... the per-file tags of its root directory	*/
	VBK$K_TAG_MOUNTOPTS,			/* ... the options it was mounted with		*/
	VBK$K_TAG_CIPHER,			/* SUMMARY, VHDR: encrypted, 1 = ChaCha20+HMAC	*/
	VBK$K_TAG_KDF,				/* ... the key derivation, 1 = PBKDF2-SHA256	*/
	VBK$K_TAG_KDFITER,			/* ... its iterations				*/
	VBK$K_TAG_SALT,				/* ... its salt, 32 octets			*/
	VBK$K_TAG_KEYCHECK,			/* ... CHECK, 32 octets: the passphrase is right */
	VBK$K_TAG_PARITY,			/* SUMMARY, VHDR: parity blocks a group, 2 .. 8	*/

	VBK$K_TAG_NFILES = 96,
	VBK$K_TAG_NBYTES,
	VBK$K_TAG_NERRORS,
	VBK$K_TAG_NBLOCKS,
	VBK$K_TAG_CATVOL,
	VBK$K_TAG_CATBLK,
	VBK$K_TAG_CATOFF,
	VBK$K_TAG_NVOLS,
	VBK$K_TAG_NENTRIES,

	VBK$K_TAG_SSUUID = 128,			/* Journal					*/
	VBK$K_TAG_SPEC,
	VBK$K_TAG_RECORDED
	};

enum	{					/* KIND of a saveset				*/
	VBK$K_KIND_FULL	= 0,
	VBK$K_KIND_INCR
	};

enum	{					/* FTYPE					*/
	VBK$K_FT_REG	= 1,
	VBK$K_FT_DIR,
	VBK$K_FT_SYMLINK,
	VBK$K_FT_HARDLINK,
	VBK$K_FT_CHR,
	VBK$K_FT_BLK,
	VBK$K_FT_FIFO,
	VBK$K_FT_SOCK
	};

/*
**  WINATTR: the attributes of Windows a saveset keeps - the values of
**  FILE_ATTRIBUTE_*; the others (directory, reparse point, sparse,
**  compressed, offline) say how a file is stored, not what it is
*/
#define	VBK$M_WA_READONLY	0x00000001
#define	VBK$M_WA_HIDDEN		0x00000002
#define	VBK$M_WA_SYSTEM		0x00000004
#define	VBK$M_WA_ARCHIVE	0x00000020
#define	VBK$M_WA_TEMPORARY	0x00000100
#define	VBK$M_WA_NOINDEX	0x00002000
#define	VBK$M_WA_KEPT		(VBK$M_WA_READONLY | VBK$M_WA_HIDDEN | VBK$M_WA_SYSTEM | VBK$M_WA_ARCHIVE | VBK$M_WA_TEMPORARY | VBK$M_WA_NOINDEX)

enum	{					/* STATUS of FEND				*/
	VBK$K_FS_OK	= 0,
	VBK$K_FS_CHANGED,
	VBK$K_FS_READERR,
	VBK$K_FS_PRESENT			/* Catalog only: covered, not saved here	*/
	};

/*
**  The decoded block header, format.md section 3.  A working copy only:
**  it is turned into bytes by VBK$BHDR_PUT and back by VBK$BLK_CHECK.
*/
typedef struct vbk_bhdr_t
{
	uint16_t	version;		/* 0 - VBK$K_VERSION, when it is written	*/
	uint32_t	bsize;
	uint8_t		type;
	uint8_t		flags;
	uint16_t	gindex;
	uint8_t		ssuuid [VBK$K_UUIDSZ];
	uint64_t	blkno;
	uint32_t	volno;
	uint32_t	recoff;
	uint32_t	paylen;
	uint32_t	prvrecoff;
	uint32_t	prvpaylen;
	uint32_t	crc;
} VBK$BHDR;

/*
**  A place in the record stream: the block in which a record header
**  begins and the offset of the header in the payload of that block.
*/
typedef struct vbk_loc_t
{
	uint32_t	vol;
	uint32_t	off;
	uint64_t	blk;
} VBK$LOC;

/*
**  The time as it is kept on the medium, format.md section 1
*/
typedef struct vbk_time_t
{
	int64_t		sec;
	uint32_t	nsec;
} VBK$TIME;

/*
**  A growing buffer the TLV items of one record body are collected in
*/
typedef struct vbk_tlvb_t
{
	uint8_t *	buf;
	uint32_t	len;
	uint32_t	sz;
} VBK$TLVB;

/*
**  Little-endian put and get, byte by byte
*/
static inline void	vbk$put16 (uint8_t *a_p, uint16_t a_v) { a_p[0] = (uint8_t) a_v; a_p[1] = (uint8_t) (a_v >> 8); }
static inline void	vbk$put32 (uint8_t *a_p, uint32_t a_v) { for (int i = 0; i < 4; i++) a_p[i] = (uint8_t) (a_v >> (8 * i)); }
static inline void	vbk$put64 (uint8_t *a_p, uint64_t a_v) { for (int i = 0; i < 8; i++) a_p[i] = (uint8_t) (a_v >> (8 * i)); }
static inline uint16_t	vbk$get16 (const uint8_t *a_p) { return (uint16_t) (a_p[0] | (a_p[1] << 8)); }

static inline uint32_t	vbk$get32 (const uint8_t *a_p)
{
uint32_t	l_v = 0;

	for (int i = 3; i >= 0; i--)
		l_v = (l_v << 8) | a_p[i];

	return	l_v;
}

static inline uint64_t	vbk$get64 (const uint8_t *a_p)
{
uint64_t	l_v = 0;

	for (int i = 7; i >= 0; i--)
		l_v = (l_v << 8) | a_p[i];

	return	l_v;
}

/*
**  VBKFMT.C
*/
void	vbk$bhdr_put	(const VBK$BHDR *a_hdr, uint8_t *a_blk);
void	vbk$blk_seal	(uint8_t *a_blk, uint32_t a_bsize);
int	vbk$blk_check	(const uint8_t *a_blk, uint32_t a_bsize, const uint8_t *a_ssuuid, VBK$BHDR *a_hdr);
int	vbk$bhdr_peek	(const uint8_t *a_blk, VBK$BHDR *a_hdr);

int	vbk$tlv_put	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint32_t a_len, const void *a_val);
int	vbk$tlv_u8	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint8_t a_val);
int	vbk$tlv_u16	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint16_t a_val);
int	vbk$tlv_u32	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint32_t a_val);
int	vbk$tlv_u64	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint64_t a_val);
int	vbk$tlv_u64x2	(VBK$TLVB *a_tlvb, uint16_t a_tag, uint64_t a_val1, uint64_t a_val2);
int	vbk$tlv_time	(VBK$TLVB *a_tlvb, uint16_t a_tag, const VBK$TIME *a_tim);
int	vbk$tlv_str	(VBK$TLVB *a_tlvb, uint16_t a_tag, const char *a_str);
void	vbk$tlv_reset	(VBK$TLVB *a_tlvb);
void	vbk$tlv_free	(VBK$TLVB *a_tlvb);

int	vbk$tlv_next	(const uint8_t *a_body, uint32_t a_len, uint32_t *a_pos, uint16_t *a_tag, uint32_t *a_vlen, const uint8_t **a_val);
uint64_t vbk$tlv_getu	(uint32_t a_vlen, const uint8_t *a_val);
void	vbk$tlv_gettime	(uint32_t a_vlen, const uint8_t *a_val, VBK$TIME *a_tim);

void	vbk$strput	(char *a_buf, size_t a_size, const char *a_src);
int	vbk$volspec	(const char *a_spec, uint32_t a_volno, char *a_out, size_t a_outsz);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKFMT$H__ */
