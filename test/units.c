#define	__MODULE__	"UNITS"
#define	__IDENT__	"X01-22"
#define	__REV__		"1.22.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	units.c
**
**  ABSTRACT:	Unit tests of the saveset core on synthetic records: the
**		layout over blocks, groups and volumes, sequential and
**		catalog-style reading, the repair of one lost block, the
**		pick-up of the stream after an unrepairable loss, a missing
**		volume, a cut-short saveset.
**
**  DESCRIPTION: Usage:  units <scratch-directory>
**
**		Record i has a type, a length and a content that are all
**		functions of i, so whatever comes back can be checked
**		without keeping a copy.  Exit status 0 - all tests passed.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-22		 7-OCT-2026	RRL
**		A stream cut short: what VBK$DATA_SALVAGE gives is the beginning of the
**		data, never an octet more; a SOLID cut by lost blocks gives its first
**		records.
**
**	X01-21		 7-OCT-2026	RRL
**		SOLID records: opened by VBK$RD_NEXT in version 3, a forged one a
**		gap and never a wrong record, a seek leaves the one being read.
**
**	X01-19		 6-OCT-2026	RRL
**		Codecs 2 and 3: every effort round trip and the same octets twice,
**		streams of zlib and liblzma read, damage never read past the end,
**		VBK$DATA_PACK by /LEVEL.
**
**	X01-15		 5-OCT-2026	RRL
**		The products of the vector instructions against the portable code.
**
**	X01-14		 5-OCT-2026	RRL
**		Reed-Solomon: every erasure pattern of n <= 10, m <= 4; m + 1
**		refused; a forged surplus row found.
**
**	X01-08		 5-OCT-2026	RRL
**		The pool: an encrypted saveset written and read on four threads, a
**		zapped and a forged block; stripes against one pass both ways.
**
**	X01-06		 5-OCT-2026	RRL
**		The encryption: SHA-256, HMAC, PBKDF2, ChaCha20 by the vectors
**		of their documents; a block sealed, opened, tampered with.
**
**	X01-04		 4-OCT-2026	RRL
**		The LZ4 block codec: round trips, determinism, damaged blocks.
**		TAP=1: the Test Anything Protocol.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<sys/stat.h>

#include	"vbkfmt.h"
#include	"vbkwrt.h"
#include	"vbkrd.h"
#include	"vbkos.h"
#include	"vbklz4.h"
#include	"vbkdfl.h"
#include	"vbklzm.h"
#include	"vbkrs.h"
#include	"vbkcrp.h"

#define	UNITS$K_NREC	400			/* Records of the synthetic saveset		*/
#define	UNITS$K_BSZ	8192			/* Small blocks: many of them, many volumes	*/

static	int	s_fail, s_ntest, s_nchk, s_tap;
static	const char *	s_title = "";
static	char	s_dir [1024];
static	VBK$LOC	s_loc [UNITS$K_NREC];
static	int	s_ev [8];
static	VBK$KEYS	s_keys;			/* s_write encrypts with them when S_CRYPT is set */
static	int	s_crypt;
static	uint8_t		s_salt [VBK$K_SALTSZ];
static	const char *	s_pass = "units passphrase";

/*
**  One check.  TAP=1 in the environment: one "ok N" / "not ok N" line each (TAP 13), named by
**  the test and the condition; the details of a failure, and every other line, as "# " notes.
*/
#define	$CHECK(cond, ...)	do { s_nchk++;									\
				if ( !(cond) ) {								\
					s_fail++;								\
					if ( s_tap ) printf("not ok %d - %s: %s\n", s_nchk, s_title, #cond);	\
					printf("%s FAIL %s:%d: ", s_tap ? "#" : " ", __FILE__, __LINE__);	\
					printf(__VA_ARGS__); printf("\n"); }					\
				else if ( s_tap ) printf("ok %d - %s: %s\n", s_nchk, s_title, #cond);		\
				} while (0)

#define	$NOTE(...)		do { printf("%s ", s_tap ? "#" : " "); printf(__VA_ARGS__); printf("\n"); } while (0)

static	uint16_t	s_rtype	(uint32_t a_i)	{ return (uint16_t) (VBK$K_RT_FILE + (a_i % 3)); }

static	uint32_t	s_rlen	(uint32_t a_i)
{
	/* Mostly small, every seventh larger than a block, some empty */
	if ( !(a_i % 11) )
		return	0;

	return	(a_i % 7) ? (13 + (a_i * 37) % 1500) : (UNITS$K_BSZ + (a_i * 101) % 20000);
}

static	uint8_t		s_rbyte	(uint32_t a_i, uint32_t a_j)	{ return (uint8_t) ((a_i * 31) + a_j + (a_j >> 8)); }

static	void	s_evcb	(
		void *		a_arg,
		int		a_ev,
		uint32_t	a_vol,
		uint64_t	a_blk
			)
{
	if ( (a_ev > 0) && (a_ev < 8) )
		s_ev [a_ev]++;
}


/*
**  Write the synthetic saveset <a_name>
*/
static	int	s_write	(
	const	char *		a_name,
		uint32_t	a_grpsz,
		uint64_t	a_volsize,
		uint32_t *	a_nvols
			)
{
VBK$WCTX	l_wctx = {0};
VBK$TLVB	l_sum = {0}, l_trl = {0};
char		l_spec [1100];
uint8_t		l_buf [UNITS$K_BSZ + 20000];
uint64_t	l_nblocks;

	snprintf(l_spec, sizeof(l_spec), "%s/%s", s_dir, a_name);

	vbk$tlv_str(&l_sum, VBK$K_TAG_PRODUCT, "VBACKUP UNITS");
	vbk$tlv_u32(&l_sum, VBK$K_TAG_BLOCKSIZE, UNITS$K_BSZ);
	vbk$tlv_u32(&l_sum, VBK$K_TAG_GROUPSIZE, a_grpsz);

	if ( s_crypt )
		{
		vbk$tlv_u8(&l_sum, VBK$K_TAG_CIPHER, VBK$K_CIPHER_CC20HS);
		vbk$tlv_u8(&l_sum, VBK$K_TAG_KDF, VBK$K_KDF_PBKDF2);
		vbk$tlv_u32(&l_sum, VBK$K_TAG_KDFITER, VBK$K_KDFMIN);
		vbk$tlv_put(&l_sum, VBK$K_TAG_SALT, VBK$K_SALTSZ, s_salt);
		vbk$tlv_put(&l_sum, VBK$K_TAG_KEYCHECK, VBK$K_KEYSZ, s_keys.check);
		l_wctx.keys	= &s_keys;
		}

	if ( !(1 & vbk$wrt_open(&l_wctx, l_spec, UNITS$K_BSZ, a_grpsz, a_volsize, VBK$M_WRT_REPLACE, l_sum.buf, l_sum.len)) )
		{
		$NOTE("vbk$wrt_open(%s): errno=%d", l_spec, l_wctx.err);
		return	STS$K_ERROR;
		}

	for ( uint32_t i = 0; i < UNITS$K_NREC; i++ )
		{
		uint32_t	l_len = s_rlen(i);

		for ( uint32_t j = 0; j < l_len; j++ )
			l_buf [j] = s_rbyte(i, j);

		if ( !(1 & vbk$wrt_record(&l_wctx, s_rtype(i), l_buf, l_len, &s_loc [i])) )
			{
			$NOTE("vbk$wrt_record(%u): errno=%d", i, l_wctx.err);
			return	STS$K_ERROR;
			}
		}

	vbk$wrt_finish(&l_wctx, &l_nblocks, a_nvols);

	vbk$tlv_u64(&l_trl, VBK$K_TAG_NBLOCKS, l_nblocks);
	vbk$tlv_u32(&l_trl, VBK$K_TAG_NVOLS, *a_nvols);

	if ( !(1 & vbk$wrt_close(&l_wctx, l_trl.buf, l_trl.len)) )
		return	STS$K_ERROR;

	vbk$tlv_free(&l_sum);
	vbk$tlv_free(&l_trl);

	return	STS$K_SUCCESS;
}


/*
**  Is record <a_i> what came back?
*/
static	int	s_same	(
		uint32_t	a_i,
		uint16_t	a_type,
	const	uint8_t *	a_body,
		uint32_t	a_len
			)
{
	if ( (a_type != s_rtype(a_i)) || (a_len != s_rlen(a_i)) )
		return	0;

	for ( uint32_t j = 0; j < a_len; j++ )
		if ( a_body [j] != s_rbyte(a_i, j) )
			return	0;

	return	1;
}


/*
**  Read the saveset sequentially.  Records must come back in order;
**  after a resync some may be missing, but never a wrong one.  Returns
**  the number of records read, and the number of resyncs.
*/
static	int	s_readall	(
	const	char *		a_name,
		uint32_t *	a_nread,
		uint32_t *	a_nresync,
		uint32_t *	a_nbad,
		VBK$RCTX *	a_keep
			)
{
VBK$RCTX	l_rctx = {0};
char		l_spec [1100];
const uint8_t *	l_body;
uint32_t	l_len, l_next = 0;
uint16_t	l_type;
VBK$LOC		l_loc;

	snprintf(l_spec, sizeof(l_spec), "%s/%s", s_dir, a_name);
	memset(s_ev, 0, sizeof(s_ev));

	*a_nread = *a_nresync = *a_nbad = 0;

	if ( !(1 & vbk$rd_open(&l_rctx, l_spec, s_evcb, NULL)) )
		{
		$NOTE("vbk$rd_open(%s): errno=%d", l_spec, l_rctx.err);
		return	STS$K_ERROR;
		}

	if ( l_rctx.crypt && !(1 & vbk$rd_setkey(&l_rctx, s_pass, strlen(s_pass))) )
		$NOTE("vbk$rd_setkey(%s) refused the passphrase", l_spec);

	while ( 1 & vbk$rd_next(&l_rctx, &l_type, &l_body, &l_len, &l_loc) )
		{
		if ( l_rctx.resync )
			{
			(*a_nresync)++;

			/* Find which record this is: the first one, from l_next on, it matches */
			while ( (l_next < UNITS$K_NREC) && !s_same(l_next, l_type, l_body, l_len) )
				l_next++;
			}

		if ( (l_next >= UNITS$K_NREC) || !s_same(l_next, l_type, l_body, l_len) )
			{
			(*a_nbad)++;
			break;
			}

		if ( (l_loc.vol != s_loc [l_next].vol) || (l_loc.blk != s_loc [l_next].blk) || (l_loc.off != s_loc [l_next].off) )
			(*a_nbad)++;

		(*a_nread)++;
		l_next++;
		}

	if ( a_keep )
		*a_keep	= l_rctx;
	else	vbk$rd_close(&l_rctx);

	return	STS$K_SUCCESS;
}


/*
**  Change one octet of the payload of block <a_pos> and seal it again: a
**  CRC that is right over a block that is not - what only a TAG catches
*/
static	void	s_forge	(
	const	char *		a_name,
		uint32_t	a_vol,
		uint64_t	a_pos,
		uint32_t	a_off
			)
{
char		l_spec [1100], l_vs [1200];
uint8_t		l_blk [UNITS$K_BSZ];
int		l_fd;

	snprintf(l_spec, sizeof(l_spec), "%s/%s", s_dir, a_name);
	vbk$volspec(l_spec, a_vol, l_vs, sizeof(l_vs));

	if ( (0 > (l_fd = open(l_vs, O_RDWR)))
		|| (pread(l_fd, l_blk, sizeof(l_blk), (off_t) (a_pos * UNITS$K_BSZ)) != (ssize_t) sizeof(l_blk)) )
		{
		s_fail++;
		return;
		}

	l_blk [VBK$K_HDRSZ + a_off] ^= 0x20;
	vbk$blk_seal(l_blk, sizeof(l_blk));

	if ( pwrite(l_fd, l_blk, sizeof(l_blk), (off_t) (a_pos * UNITS$K_BSZ)) != (ssize_t) sizeof(l_blk) )
		s_fail++;

	close(l_fd);
}


/*
**  Overwrite block <a_pos> of volume <a_vol> of <a_name> with zeros
*/
static	void	s_zap	(
	const	char *		a_name,
		uint32_t	a_vol,
		uint64_t	a_pos
			)
{
char		l_spec [1100], l_vs [1200];
uint8_t		l_zero [UNITS$K_BSZ] = {0};
int		l_fd;

	snprintf(l_spec, sizeof(l_spec), "%s/%s", s_dir, a_name);
	vbk$volspec(l_spec, a_vol, l_vs, sizeof(l_vs));

	if ( 0 > (l_fd = open(l_vs, O_WRONLY)) )
		{
		$NOTE("open(%s): errno=%d", l_vs, errno);
		s_fail++;
		return;
		}

	if ( pwrite(l_fd, l_zero, sizeof(l_zero), (off_t) (a_pos * UNITS$K_BSZ)) != (ssize_t) sizeof(l_zero) )
		s_fail++;

	close(l_fd);
}


static	void	s_begin	(const char *a_what)
{
	s_ntest++;
	s_title	= a_what;
	printf("%s%2d. %s\n", s_tap ? "# " : "", s_ntest, a_what);
}



/*
**  A digest against its hexadecimal form
*/
static	int	s_hexeq	(
	const	uint8_t *	a_d,
		size_t		a_len,
	const	char *		a_hex
			)
{
char	l_x [3];

	if ( strlen(a_hex) != (2 * a_len) )
		return	0;

	for ( size_t i = 0; i < a_len; i++ )
		{
		snprintf(l_x, sizeof(l_x), "%02x", a_d [i]);

		if ( memcmp(l_x, a_hex + 2 * i, 2) )
			return	0;
		}

	return	1;
}

static	void	s_sha	(
	const	char *		a_m,
		size_t		a_len,
	const	char *		a_hex
			)
{
VBK$SHA256	l_s;
uint8_t		l_d [VBK$K_KEYSZ];

	vbk$sha256_init(&l_s);
	vbk$sha256_update(&l_s, a_m, a_len);
	vbk$sha256_final(&l_s, l_d);

	$CHECK(s_hexeq(l_d, sizeof(l_d), a_hex), "SHA-256 of %zu octets", a_len);
}

/*
**  The inner records of a SOLID: header and body, appended at <a_at>
*/
static	uint32_t	s_inner	(
		uint8_t *	a_buf,
		uint32_t	a_at,
		uint16_t	a_type,
		uint32_t	a_len,
		uint8_t		a_fill
			)
{
	vbk$put16(a_buf + a_at, a_type);
	vbk$put16(a_buf + a_at + 2, 0);
	vbk$put32(a_buf + a_at + 4, a_len);
	memset(a_buf + a_at + VBK$K_RECHDR, a_fill, a_len);

	return	a_at + VBK$K_RECHDR + a_len;
}

/*
**  A saveset of version 3 (<a_v3>, else 1): FILE "A", a SOLID of the
**  body <a_body> - or <a_raw> packed by LZMA when <a_body> is NULL - and
**  FEND "B".  Then read: the types that came, a resync per record ('!'),
**  'S' for one that came out of a SOLID; <a_seek> - a seek back to "A"
**  after the second record.
*/
static	int	s_solid	(
	const	char *		a_name,
		int		a_v3,
	const	uint8_t *	a_raw,
		uint32_t	a_rawlen,
	const	uint8_t *	a_body,
		uint32_t	a_blen,
		int		a_seek,
		char *		a_out,
		size_t		a_outsz
			)
{
static	uint8_t	l_rec [VBK$K_SOLIDHDR + VBK$LZ4_BOUND(VBK$K_MAXSOLID)], l_chk [VBK$K_MAXSOLID];
VBK$WCTX	l_wctx = {0};
VBK$RCTX	l_rctx = {0};
VBK$TLVB	l_sum = {0}, l_trl = {0};
VBK$LOC		l_aloc;
char		l_spec [1100];
const uint8_t *	l_body;
uint32_t	l_len, l_zlen = 0, l_codec = 0, l_nvols;
uint64_t	l_nblocks;
uint16_t	l_type;
size_t		l_o = 0;
int		l_n = 0;

	snprintf(l_spec, sizeof(l_spec), "%s/%s", s_dir, a_name);

	vbk$tlv_str(&l_sum, VBK$K_TAG_PRODUCT, "VBACKUP UNITS");
	vbk$tlv_u32(&l_sum, VBK$K_TAG_BLOCKSIZE, UNITS$K_BSZ);
	vbk$tlv_u32(&l_sum, VBK$K_TAG_GROUPSIZE, 8);

	if ( !a_body )
		{
		if ( STS$K_SUCCESS != vbk$data_pack(6, a_raw, a_rawlen, l_rec + VBK$K_SOLIDHDR, sizeof(l_rec) - VBK$K_SOLIDHDR, &l_zlen, &l_codec, l_chk) )
			return	STS$K_ERROR;

		vbk$put32(l_rec, l_codec);
		vbk$put32(l_rec + 4, a_rawlen);
		vbk$put32(l_rec + 8, 1);
		a_body	= l_rec;
		a_blen	= VBK$K_SOLIDHDR + l_zlen;
		}

	l_wctx.solid	= a_v3;

	if ( !(1 & vbk$wrt_open(&l_wctx, l_spec, UNITS$K_BSZ, 8, 0, VBK$M_WRT_REPLACE, l_sum.buf, l_sum.len))
		|| !(1 & vbk$wrt_record(&l_wctx, VBK$K_RT_FILE, "A", 1, &l_aloc))
		|| !(1 & vbk$wrt_record(&l_wctx, VBK$K_RT_SOLID, a_body, a_blen, NULL))
		|| !(1 & vbk$wrt_record(&l_wctx, VBK$K_RT_FEND, "B", 1, NULL)) )
		return	STS$K_ERROR;

	vbk$wrt_finish(&l_wctx, &l_nblocks, &l_nvols);
	vbk$tlv_u64(&l_trl, VBK$K_TAG_NBLOCKS, l_nblocks);
	vbk$tlv_u32(&l_trl, VBK$K_TAG_NVOLS, l_nvols);

	if ( !(1 & vbk$wrt_close(&l_wctx, l_trl.buf, l_trl.len)) )
		return	STS$K_ERROR;

	vbk$tlv_free(&l_sum);
	vbk$tlv_free(&l_trl);

	memset(s_ev, 0, sizeof(s_ev));

	if ( !(1 & vbk$rd_open(&l_rctx, l_spec, s_evcb, NULL)) )
		return	STS$K_ERROR;

	while ( (l_o + 4 < a_outsz) && (1 & vbk$rd_next(&l_rctx, &l_type, &l_body, &l_len, NULL)) )
		{
		if ( l_rctx.resync )
			a_out [l_o++] = '!';

		a_out [l_o++] = (char) ('0' + l_type);

		if ( l_rctx.insolid )
			a_out [l_o++] = 'S';

		if ( (++l_n == 2) && a_seek && !(1 & vbk$rd_seek(&l_rctx, &l_aloc)) )
			break;
		}

	a_out [l_o] = '\0';
	vbk$rd_close(&l_rctx);

	return	STS$K_SUCCESS;
}


int	main	(
		int		a_argc,
		char **		a_argv
		)
{
uint32_t	l_nvols, l_nread, l_nresync, l_nbad;
VBK$RCTX	l_rctx;
char		l_spec [1100];

	if ( a_argc < 2 )
		{
		fprintf(stderr, "Usage: units <scratch-directory>\n");
		return	2;
		}

	if ( (s_tap = getenv("TAP") && *getenv("TAP")) )
		printf("TAP version 13\n");

	snprintf(s_dir, sizeof(s_dir), "%s", a_argv [1]);
	mkdir(s_dir, 0755);

	s_begin("CRC-32/IEEE check value");
	$CHECK($VBK_CRC(0, "123456789", 9) == 0xCBF43926U, "CRC(\"123456789\") = %08X", $VBK_CRC(0, "123456789", 9));

	s_begin("write and read back, one volume, groups of 4");
	$CHECK(1 & s_write("one.bck", 4, 0, &l_nvols), "write");
	$CHECK(l_nvols == 1, "nvols %u", l_nvols);
	s_readall("one.bck", &l_nread, &l_nresync, &l_nbad, &l_rctx);
	$CHECK((l_nread == UNITS$K_NREC) && !l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK(l_rctx.trailer != NULL, "no TRAILER");

	s_begin("seek to every record by its location");
	{
	const uint8_t *	l_body;
	uint32_t	l_len, l_bad = 0;
	uint16_t	l_type;

	for ( uint32_t i = 0; i < UNITS$K_NREC; i += 7 )
		if ( (STS$K_SUCCESS != vbk$rd_seek(&l_rctx, &s_loc [i])) || !(1 & vbk$rd_next(&l_rctx, &l_type, &l_body, &l_len, NULL))
			|| !s_same(i, l_type, l_body, l_len) )
			l_bad++;

	$CHECK(!l_bad, "%u seeks failed", l_bad);
	vbk$rd_close(&l_rctx);
	}

	s_begin("several volumes, groups of 3");
	$CHECK(1 & s_write("multi.bck", 3, 12 * UNITS$K_BSZ, &l_nvols), "write");
	$CHECK(l_nvols > 3, "nvols %u", l_nvols);
	s_readall("multi.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread == UNITS$K_NREC) && !l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);

	s_begin("one DATA block zeroed in volume 2: rebuilt from XOR");
	s_zap("multi.bck", 2, 2);
	s_readall("multi.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread == UNITS$K_NREC) && !l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK((s_ev [VBK$K_EV_REPAIRED] == 1) && !s_ev [VBK$K_EV_LOST], "repaired %d lost %d", s_ev [VBK$K_EV_REPAIRED], s_ev [VBK$K_EV_LOST]);

	s_begin("the first DATA block of a group and its XOR block zeroed: lost, stream picked up");
	$CHECK(1 & s_write("lost.bck", 3, 0, &l_nvols), "write");
	s_zap("lost.bck", 1, 5);			/* Group 2 is positions 5..8, XOR at 8	*/
	s_zap("lost.bck", 1, 8);
	s_readall("lost.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread < UNITS$K_NREC) && (l_nread > UNITS$K_NREC - 20) && (l_nresync == 1) && !l_nbad,
		"read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK(s_ev [VBK$K_EV_LOST] == 1, "lost %d", s_ev [VBK$K_EV_LOST]);

	s_begin("two DATA blocks of one group zeroed: lost, stream picked up");
	$CHECK(1 & s_write("two.bck", 3, 0, &l_nvols), "write");
	s_zap("two.bck", 1, 9);
	s_zap("two.bck", 1, 10);
	s_readall("two.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread < UNITS$K_NREC) && (l_nresync == 1) && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK(s_ev [VBK$K_EV_LOST] == 2, "lost %d", s_ev [VBK$K_EV_LOST]);

	s_begin("only an XOR block zeroed: nothing lost");
	$CHECK(1 & s_write("xor.bck", 3, 0, &l_nvols), "write");
	s_zap("xor.bck", 1, 4);
	s_readall("xor.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread == UNITS$K_NREC) && !l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);

	s_begin("volume 2 missing: volumes 3 and on still read");
	$CHECK(1 & s_write("miss.bck", 3, 12 * UNITS$K_BSZ, &l_nvols), "write");
	snprintf(l_spec, sizeof(l_spec), "%s/miss.bck.002", s_dir);
	unlink(l_spec);
	s_readall("miss.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread < UNITS$K_NREC) && (l_nread > UNITS$K_NREC / 2) && (l_nresync == 1) && !l_nbad,
		"read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK(s_ev [VBK$K_EV_MISSVOL] == 1, "missvol %d", s_ev [VBK$K_EV_MISSVOL]);

	s_begin("saveset cut short, no TRAILER: read up to the cut");
	$CHECK(1 & s_write("cut.bck", 3, 0, &l_nvols), "write");
	snprintf(l_spec, sizeof(l_spec), "%s/cut.bck", s_dir);
	{
	struct stat	l_st;

	stat(l_spec, &l_st);
	$CHECK(!truncate(l_spec, (l_st.st_size / UNITS$K_BSZ / 2) * UNITS$K_BSZ + 100), "truncate");
	}
	s_readall("cut.bck", &l_nread, &l_nresync, &l_nbad, &l_rctx);
	$CHECK((l_nread > UNITS$K_NREC / 3) && (l_nread < UNITS$K_NREC) && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK(l_rctx.trailer == NULL, "a TRAILER in a cut saveset");
	vbk$rd_close(&l_rctx);

	s_begin("no groups (/GROUP_SIZE=0), several volumes");
	$CHECK(1 & s_write("nogrp.bck", 0, 10 * UNITS$K_BSZ, &l_nvols), "write");
	s_readall("nogrp.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread == UNITS$K_NREC) && !l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);

	s_begin("probe: a saveset and a file that is not one");
	snprintf(l_spec, sizeof(l_spec), "%s/one.bck", s_dir);
	$CHECK(vbk$rd_probe(l_spec) == STS$K_SUCCESS, "one.bck not a saveset");
	snprintf(l_spec, sizeof(l_spec), "%s/one.bck", a_argv [0]);
	$CHECK(vbk$rd_probe(a_argv [0]) == STS$K_WARN, "the test image taken for a saveset");

	s_begin("LZ4 block: round trip of runs, text, random, short and empty buffers");
	{
	static uint8_t	l_src [VBK$K_MAXDATA], l_z [VBK$LZ4_BOUND(VBK$K_MAXDATA)], l_out [VBK$K_MAXDATA];
	uint32_t	l_sizes [] = { 0, 1, 5, 12, 13, 17, 100, 4096, 65536, 65537, 300000, VBK$K_MAXDATA };
	uint32_t	l_zlen, l_bad = 0, l_seed = 12345;

	for ( int l_kind = 0; l_kind < 4; l_kind++ )
		for ( size_t s = 0; s < (sizeof(l_sizes) / sizeof(l_sizes [0])); s++ )
			{
			uint32_t	l_n = l_sizes [s];

			for ( uint32_t j = 0; j < l_n; j++ )
				{
				l_seed	= (l_seed * 1103515245U) + 12345U;

				switch ( l_kind )
					{
					case 0:	l_src [j] = 'A';						break;
					case 1:	l_src [j] = (uint8_t) "the quick brown fox "[j % 20];		break;
					case 2:	l_src [j] = (uint8_t) (l_seed >> 16);				break;
					default: l_src [j] = (uint8_t) ((j / 700) ^ ((l_seed >> 28) & 1));	break;
					}
				}

			if ( !(1 & vbk$lz4_compress(l_src, l_n, l_z, sizeof(l_z), &l_zlen))
				|| !(1 & vbk$lz4_decompress(l_z, l_zlen, l_out, l_n)) || memcmp(l_src, l_out, l_n) )
				l_bad++;

			/* The same input, the same output */
			{
			uint32_t	l_zlen2;
			static uint8_t	l_z2 [VBK$LZ4_BOUND(VBK$K_MAXDATA)];

			if ( !(1 & vbk$lz4_compress(l_src, l_n, l_z2, sizeof(l_z2), &l_zlen2)) || (l_zlen2 != l_zlen) || memcmp(l_z, l_z2, l_zlen) )
				l_bad++;
			}
			}

	$CHECK(!l_bad, "%u buffers did not come back the same", l_bad);

	/* A run of 1 MB is a few kilobytes; random data does not fit into less than itself */
	memset(l_src, 'A', VBK$K_MAXDATA);
	vbk$lz4_compress(l_src, VBK$K_MAXDATA, l_z, sizeof(l_z), &l_zlen);
	$CHECK(l_zlen < 8192, "a run of 1 MB is %u octets", l_zlen);
	$CHECK(STS$K_WARN == vbk$lz4_compress(l_src, VBK$K_MAXDATA, l_z, 100, &l_zlen), "a buffer too small was not refused");
	}

	s_begin("LZ4 block: damaged blocks are refused, nothing is written out of bounds");
	{
	static uint8_t	l_src [65536], l_z [VBK$LZ4_BOUND(65536)], l_bz [VBK$LZ4_BOUND(65536)], l_out [65536];
	uint32_t	l_zlen, l_ok = 0, l_seed = 777;

	for ( uint32_t j = 0; j < sizeof(l_src); j++ )
		l_src [j] = (uint8_t) "abcabcabd 0123456789"[(j * 7) % 20];

	vbk$lz4_compress(l_src, sizeof(l_src), l_z, sizeof(l_z), &l_zlen);

	/* Random octets spoiled, the block cut, garbage given: refused, or - by chance - exactly 64 KB */
	for ( int i = 0; i < 20000; i++ )
		{
		uint32_t	l_len = l_zlen;

		memcpy(l_bz, l_z, l_zlen);
		l_seed	= (l_seed * 1103515245U) + 12345U;

		if ( (i % 3) == 0 )
			l_len	= (l_seed >> 8) % (l_zlen + 1);
		else if ( (i % 3) == 1 )
			for ( int k = 0; k < 1 + (i % 5); k++ )
				{
				l_seed	= (l_seed * 1103515245U) + 12345U;
				l_bz [(l_seed >> 8) % l_zlen] = (uint8_t) (l_seed >> 3);
				}
		else
			for ( uint32_t k = 0; k < l_len; k++ )
				{
				l_seed	= (l_seed * 1103515245U) + 12345U;
				l_bz [k] = (uint8_t) (l_seed >> 16);
				}

		if ( STS$K_SUCCESS == vbk$lz4_decompress(l_bz, l_len, l_out, sizeof(l_out)) )
			l_ok++;
		}

	$CHECK(l_ok < 20000, "every damaged block decompressed");
	}



	s_begin("encrypted: several volumes, groups of 3, read back with the passphrase");
	vbk$os_random(s_salt, sizeof(s_salt));
	vbk$crp_derive(&s_keys, s_pass, strlen(s_pass), s_salt, VBK$K_KDFMIN);
	s_crypt	= 1;
	$CHECK(1 & s_write("enc.bck", 3, 12 * UNITS$K_BSZ, &l_nvols), "write");
	$CHECK(l_nvols > 3, "nvols %u", l_nvols);
	s_readall("enc.bck", &l_nread, &l_nresync, &l_nbad, &l_rctx);
	$CHECK((l_nread == UNITS$K_NREC) && !l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK(l_rctx.crypt && l_rctx.haskey && l_rctx.trailer, "crypt %d key %d trailer %p", l_rctx.crypt, l_rctx.haskey, l_rctx.trailer);

	s_begin("encrypted: seek to every record by its location");
	{
	const uint8_t *	l_body;
	uint32_t	l_len, l_bad = 0;
	uint16_t	l_type;

	for ( uint32_t i = 0; i < UNITS$K_NREC; i += 5 )
		if ( (STS$K_SUCCESS != vbk$rd_seek(&l_rctx, &s_loc [i])) || !(1 & vbk$rd_next(&l_rctx, &l_type, &l_body, &l_len, NULL))
			|| !s_same(i, l_type, l_body, l_len) )
			l_bad++;

	$CHECK(!l_bad, "%u seeks failed", l_bad);
	vbk$rd_close(&l_rctx);
	}

	s_begin("encrypted: a wrong passphrase is refused, nothing is read without the right one");
	{
	VBK$RCTX	l_r = {0};
	const uint8_t *	l_body;
	uint32_t	l_len;
	uint16_t	l_type;

	snprintf(l_spec, sizeof(l_spec), "%s/enc.bck", s_dir);
	$CHECK(1 & vbk$rd_open(&l_r, l_spec, NULL, NULL), "open");
	$CHECK(l_r.crypt && !l_r.trailer, "crypt %d trailer %p before the key", l_r.crypt, l_r.trailer);
	$CHECK(!(1 & vbk$rd_next(&l_r, &l_type, &l_body, &l_len, NULL)), "a record without the key");
	$CHECK(STS$K_ERROR == vbk$rd_setkey(&l_r, "units passphrasE", 16), "a wrong passphrase taken");
	$CHECK(!(1 & vbk$rd_next(&l_r, &l_type, &l_body, &l_len, NULL)), "a record after a wrong key");
	vbk$rd_close(&l_r);
	}

	s_begin("encrypted: a zapped block and a forged one (CRC right) are both rebuilt from their groups");
	$CHECK(1 & s_write("encf.bck", 4, 0, &l_nvols), "write");
	s_zap("encf.bck", 1, 2);
	s_forge("encf.bck", 1, 7, 100);		/* Group 2: positions 6..10, XOR at 10	*/
	s_readall("encf.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread == UNITS$K_NREC) && !l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK((s_ev [VBK$K_EV_REPAIRED] == 2) && (s_ev [VBK$K_EV_BADTAG] == 1) && !s_ev [VBK$K_EV_LOST],
		"repaired %d badtag %d lost %d", s_ev [VBK$K_EV_REPAIRED], s_ev [VBK$K_EV_BADTAG], s_ev [VBK$K_EV_LOST]);

	s_begin("encrypted: a forged block with no XOR to rebuild it is lost, never read");
	$CHECK(1 & s_write("encn.bck", 0, 0, &l_nvols), "write");
	s_forge("encn.bck", 1, 5, 3);
	s_readall("encn.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread < UNITS$K_NREC) && l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK((s_ev [VBK$K_EV_BADTAG] == 1) && (s_ev [VBK$K_EV_LOST] == 1), "badtag %d lost %d", s_ev [VBK$K_EV_BADTAG], s_ev [VBK$K_EV_LOST]);

	s_begin("encrypted: two blocks of a group forged, its XOR forged too - lost, never read");
	$CHECK(1 & s_write("enc2.bck", 4, 0, &l_nvols), "write");
	s_forge("enc2.bck", 1, 7, 9);
	s_forge("enc2.bck", 1, 10, 9);
	s_readall("enc2.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread < UNITS$K_NREC) && l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);

	/* All of it once more with the pool: the stripes of a block and the blocks of a group on several threads */
	{
	uint32_t	l_nthr;

	setenv("VBACKUP_CTHREADS", "4", 0);
	l_nthr	= vbk$par_init();

	s_begin("encrypted, with the pool: several volumes read back, a zapped and a forged block rebuilt");
	$NOTE("threads: %u", l_nthr);
	$CHECK(l_nthr > 1, "no pool started");
	$CHECK(1 & s_write("encp.bck", 3, 12 * UNITS$K_BSZ, &l_nvols), "write");
	s_readall("encp.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread == UNITS$K_NREC) && !l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK(1 & s_write("encpf.bck", 4, 0, &l_nvols), "write");
	s_zap("encpf.bck", 1, 2);
	s_forge("encpf.bck", 1, 7, 100);
	s_readall("encpf.bck", &l_nread, &l_nresync, &l_nbad, NULL);
	$CHECK((l_nread == UNITS$K_NREC) && !l_nresync && !l_nbad, "read %u resync %u bad %u", l_nread, l_nresync, l_nbad);
	$CHECK((s_ev [VBK$K_EV_REPAIRED] == 2) && (s_ev [VBK$K_EV_BADTAG] == 1) && !s_ev [VBK$K_EV_LOST],
		"repaired %d badtag %d lost %d", s_ev [VBK$K_EV_REPAIRED], s_ev [VBK$K_EV_BADTAG], s_ev [VBK$K_EV_LOST]);

	/* A large payload sealed in stripes opens in one pass, and the other way round */
	{
	static uint8_t	l_pay [262144 - VBK$K_HDRSZ], l_ref [sizeof(l_pay)];
	VBK$BHDR	l_h = {0};
	uint32_t	l_psz = sizeof(l_pay);

	l_h.bsize = 262144; l_h.type = 5; l_h.blkno = 9; l_h.volno = 1; l_h.paylen = l_psz - VBK$K_TAGSZ;

	for ( uint32_t i = 0; i < l_h.paylen; i++ )
		l_ref [i] = l_pay [i] = (uint8_t) (i * 13 + (i >> 9));

	vbk$crp_seal(&s_keys, &l_h, l_pay, l_psz);
	vbk$crp_setpar(NULL, 1);
	$CHECK(1 & vbk$crp_open(&s_keys, &l_h, l_pay, l_psz), "sealed in stripes, refused in one pass");
	$CHECK(!memcmp(l_pay, l_ref, l_h.paylen), "sealed in stripes, opened in one pass: other bytes");
	vbk$crp_seal(&s_keys, &l_h, l_pay, l_psz);
	vbk$par_init();
	$CHECK(1 & vbk$crp_open(&s_keys, &l_h, l_pay, l_psz), "sealed in one pass, refused in stripes");
	$CHECK(!memcmp(l_pay, l_ref, l_h.paylen), "sealed in one pass, opened in stripes: other bytes");
	}
	}

	s_crypt	= 0;
	vbk$crp_wipe(&s_keys, sizeof(s_keys));

	s_begin("SHA-256, FIPS 180-4 examples");
	{
	static uint8_t	l_m [1000000];
	uint8_t		l_d [VBK$K_KEYSZ];
	VBK$SHA256	l_s;

	s_sha("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	s_sha("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	s_sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	/* A million 'a', fed in pieces of every size from 1 up */
	memset(l_m, 'a', sizeof(l_m));
	vbk$sha256_init(&l_s);

	for ( size_t l_o = 0, l_n = 1; l_o < sizeof(l_m); l_o += l_n, l_n = (l_n % 131) + 1 )
		vbk$sha256_update(&l_s, l_m + l_o, ((sizeof(l_m) - l_o) < l_n) ? (sizeof(l_m) - l_o) : l_n);

	vbk$sha256_final(&l_s, l_d);
	$CHECK(s_hexeq(l_d, 32, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), "a million 'a'");
	}

	s_begin("HMAC-SHA256, RFC 4231 test cases 1, 2, 6");
	{
	uint8_t		l_k [131], l_d [VBK$K_KEYSZ];
	VBK$HMAC	l_h;

	memset(l_k, 0x0b, 20);
	vbk$hmac_init(&l_h, l_k, 20);
	vbk$hmac(&l_h, "Hi There", 8, l_d);
	$CHECK(s_hexeq(l_d, 32, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"), "case 1");

	vbk$hmac_init(&l_h, "Jefe", 4);
	vbk$hmac(&l_h, "what do ya want for nothing?", 28, l_d);
	$CHECK(s_hexeq(l_d, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"), "case 2");

	memset(l_k, 0xaa, 131);
	vbk$hmac_init(&l_h, l_k, 131);
	vbk$hmac(&l_h, "Test Using Larger Than Block-Size Key - Hash Key First", 54, l_d);
	$CHECK(s_hexeq(l_d, 32, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"), "case 6");
	}

	s_begin("PBKDF2-HMAC-SHA256, RFC 7914 section 11 and the usual 4096");
	{
	uint8_t		l_d [64];

	vbk$pbkdf2("passwd", 6, (const uint8_t *) "salt", 4, 1, l_d, 64);
	$CHECK(s_hexeq(l_d, 64, "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
		"49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783"), "passwd/salt/1");

	vbk$pbkdf2("Password", 8, (const uint8_t *) "NaCl", 4, 80000, l_d, 64);
	$CHECK(s_hexeq(l_d, 64, "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
		"a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d"), "Password/NaCl/80000");

	vbk$pbkdf2("password", 8, (const uint8_t *) "salt", 4, 4096, l_d, 32);
	$CHECK(s_hexeq(l_d, 32, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"), "password/salt/4096");
	}

	s_begin("SHA-256: the instructions of the CPU, where there are any, give the bytes of the portable code");
	{
	static uint8_t	l_m [70000];
	uint8_t		l_a [VBK$K_KEYSZ], l_b [VBK$K_KEYSZ], l_d [32];
	VBK$SHA256	l_s;
	uint32_t	l_bad = 0, l_seed = 7;
	int		l_hw = vbk$crp_hw(1);

	$NOTE("the SHA-256 instructions: %s", l_hw ? "in use" : "none here, or not passed - the portable code only");

	for ( size_t i = 0; i < sizeof(l_m); i++ )
		l_m [i] = (uint8_t) ((l_seed = l_seed * 1103515245U + 12345U) >> 16);

	for ( size_t l_n = 0; l_hw && (l_n < sizeof(l_m)); l_n += (l_n < 300) ? 1 : 4093 )
		{
		vbk$crp_hw(1);
		vbk$sha256_init(&l_s); vbk$sha256_update(&l_s, l_m, l_n); vbk$sha256_final(&l_s, l_a);
		vbk$crp_hw(0);
		vbk$sha256_init(&l_s); vbk$sha256_update(&l_s, l_m, l_n); vbk$sha256_final(&l_s, l_b);

		if ( memcmp(l_a, l_b, sizeof(l_a)) )
			l_bad++;
		}

	$CHECK(!l_bad, "%u lengths hashed otherwise by the instructions", l_bad);

	/* The vectors once more by each way */
	for ( int l_way = 0; l_way < 2; l_way++ )
		{
		vbk$crp_hw(l_way);
		vbk$pbkdf2("password", 8, (const uint8_t *) "salt", 4, 4096, l_d, 32);
		$CHECK(s_hexeq(l_d, 32, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"), "PBKDF2 by way %d", l_way);
		s_sha("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
		}

	vbk$crp_hw(1);
	}

	s_begin("ChaCha20, RFC 8439 2.4.2");
	{
	static const char	l_pt [] = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, "
					"sunscreen would be it.";
	uint8_t		l_key [32], l_nonce [12] = { 0, 0, 0, 0, 0, 0, 0, 0x4a, 0, 0, 0, 0 }, l_buf [sizeof(l_pt) - 1];

	for ( int i = 0; i < 32; i++ )
		l_key [i] = (uint8_t) i;

	memcpy(l_buf, l_pt, sizeof(l_buf));
	vbk$chacha20(l_key, l_nonce, 1, l_buf, sizeof(l_buf));
	$CHECK(s_hexeq(l_buf, sizeof(l_buf), "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"
		"f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc"
		"514d16ccf806818ce91ab77937365af90bbf74a35be6b40b8eedf2785e42874d"), "ciphertext");

	vbk$chacha20(l_key, l_nonce, 1, l_buf, sizeof(l_buf));
	$CHECK(!memcmp(l_buf, l_pt, sizeof(l_buf)), "decrypted back");
	}

	s_begin("a block sealed, opened, and refused when any octet of it or of its header changes");
	{
	static uint8_t	l_pay [UNITS$K_BSZ - VBK$K_HDRSZ], l_orig [sizeof(l_pay)];
	uint8_t		l_salt [VBK$K_SALTSZ] = { 1, 2, 3 };
	VBK$KEYS	l_keys, l_other;
	VBK$BHDR	l_h = {0}, l_h2;
	uint32_t	l_psz = sizeof(l_pay), l_bad = 0;

	vbk$crp_derive(&l_keys, "correct horse", 13, l_salt, 1000);
	vbk$crp_derive(&l_other, "correct horsf", 13, l_salt, 1000);
	$CHECK(!vbk$crp_equal(l_keys.check, l_other.check, 32), "two passphrases, one CHECK");

	l_h.bsize = UNITS$K_BSZ; l_h.type = 5; l_h.blkno = 77; l_h.volno = 2; l_h.gindex = 3;
	l_h.recoff = 10; l_h.paylen = l_psz - VBK$K_TAGSZ - 100;

	for ( uint32_t i = 0; i < l_h.paylen; i++ )
		l_pay [i] = (uint8_t) (i * 7);

	memcpy(l_orig, l_pay, sizeof(l_pay));
	vbk$crp_seal(&l_keys, &l_h, l_pay, l_psz);
	$CHECK(memcmp(l_pay, l_orig, l_h.paylen), "not encrypted");

	memcpy(l_orig, l_pay, sizeof(l_pay));
	$CHECK(!(1 & vbk$crp_open(&l_other, &l_h, l_pay, l_psz)), "opened with the wrong key");
	$CHECK(!memcmp(l_pay, l_orig, sizeof(l_pay)), "a refused block was changed");

	/* Every header field of the TAG, and a sample of the octets of the ciphertext and the TAG */
	for ( int f = 0; f < 7; f++ )
		{
		l_h2 = l_h;

		switch ( f )
			{
			case 0:	l_h2.ssuuid [5] ^= 1; break;
			case 1:	l_h2.blkno++; break;
			case 2:	l_h2.volno++; break;
			case 3:	l_h2.type = 1; break;
			case 4:	l_h2.gindex++; break;
			case 5:	l_h2.recoff++; break;
			case 6:	l_h2.paylen--; break;
			}

		if ( 1 & vbk$crp_open(&l_keys, &l_h2, l_pay, l_psz) )
			l_bad++;
		}

	for ( uint32_t i = 0; i < l_psz; i += (i < l_h.paylen) ? 997 : 1 )
		{
		if ( (i >= l_h.paylen) && (i < (l_psz - VBK$K_TAGSZ)) )
			continue;

		l_pay [i] ^= 0x40;

		if ( 1 & vbk$crp_open(&l_keys, &l_h, l_pay, l_psz) )
			l_bad++;

		l_pay [i] ^= 0x40;
		}

	$CHECK(!l_bad, "%u changes were not noticed", l_bad);
	$CHECK(!memcmp(l_pay, l_orig, sizeof(l_pay)), "the probes changed the block");

	/* flags, prvrecoff, prvpaylen are outside the TAG: the repair rebuilds them */
	l_h2 = l_h; l_h2.flags = 3; l_h2.prvrecoff = 5; l_h2.prvpaylen = 6;
	$CHECK(1 & vbk$crp_open(&l_keys, &l_h2, l_pay, l_psz), "the right key refused");

	for ( uint32_t i = 0; i < l_h.paylen; i++ )
		if ( l_pay [i] != (uint8_t) (i * 7) )
			{
			l_bad++;
			break;
			}

	$CHECK(!l_bad, "decrypted wrong");

	vbk$crp_wipe(&l_keys, sizeof(l_keys));
	}

	s_begin("Reed-Solomon: every erasure pattern of n <= 10, m <= 4 is rebuilt exactly");
	{
	enum	{ L_LEN = 37 };
	uint8_t		l_d [10] [L_LEN], l_o [10] [L_LEN], l_p [4] [L_LEN];
	uint8_t *	l_dp [10];
	const uint8_t *	l_pp [4];
	uint8_t		l_dok [10], l_pok [4];
	uint32_t	l_bad = 0, l_npat = 0;

	vbk$rs_init();

	for ( uint32_t i = 0; i < 10; i++ )
		{
		l_dp [i] = l_d [i];

		for ( uint32_t k = 0; k < L_LEN; k++ )
			l_o [i] [k] = (uint8_t) ((i * 131 + k * 17 + (i * k)) ^ 0x5A);
		}

	for ( uint32_t j = 0; j < 4; j++ )
		l_pp [j] = l_p [j];

	for ( uint32_t n = 1; n <= 10; n++ )
		for ( uint32_t m = 1; m <= 4; m++ )
			{
			/* The parity of the group */
			memset(l_p, 0, sizeof(l_p));

			for ( uint32_t j = 0; j < m; j++ )
				for ( uint32_t i = 0; i < n; i++ )
					vbk$rs_muladd(l_p [j], l_o [i], L_LEN, vbk$rs_coef(j, i));

			/* Every subset of the n + m blocks of at most m elements */
			for ( uint32_t l_set = 0; l_set < (1U << (n + m)); l_set++ )
				{
				if ( (uint32_t) __builtin_popcount(l_set) > m )
					continue;

				l_npat++;

				for ( uint32_t i = 0; i < n; i++ )
					{
					l_dok [i] = !(l_set & (1U << i));
					memcpy(l_d [i], l_dok [i] ? l_o [i] : (const uint8_t *) "garbage garbage garbage garbage garbag", L_LEN);
					}

				for ( uint32_t j = 0; j < m; j++ )
					l_pok [j] = !(l_set & (1U << (n + j)));

				if ( (STS$K_SUCCESS != vbk$rs_repair(n, m, l_dp, l_dok, l_pp, l_pok, L_LEN)) || memcmp(l_d, l_o, (size_t) n * L_LEN) )
					l_bad++;
				}
			}

	$CHECK(!l_bad, "%u of %u patterns not rebuilt", l_bad, l_npat);
	$CHECK(vbk$rs_coef(0, 7) == 1, "row 0 is not the XOR");

	/* m + 1 lost: refused, untouched; a forged parity row found by a row left over */
	memset(l_p, 0, sizeof(l_p));

	for ( uint32_t j = 0; j < 3; j++ )
		for ( uint32_t i = 0; i < 6; i++ )
			vbk$rs_muladd(l_p [j], l_o [i], L_LEN, vbk$rs_coef(j, i));

	for ( uint32_t i = 0; i < 6; i++ )
		memcpy(l_d [i], l_o [i], L_LEN), l_dok [i] = (i > 2);

	l_pok [0] = l_pok [1] = l_pok [2] = 1;
	$CHECK(STS$K_SUCCESS == vbk$rs_repair(6, 3, l_dp, l_dok, l_pp, l_pok, L_LEN), "three lost of three rows refused");

	l_dok [3] = 0;
	$CHECK(STS$K_ERROR == vbk$rs_repair(6, 3, l_dp, l_dok, l_pp, l_pok, L_LEN), "four lost of three rows not refused");

	for ( uint32_t i = 0; i < 6; i++ )
		l_dok [i] = (i != 1);

	l_p [2] [5] ^= 1;
	$CHECK(STS$K_WARN == vbk$rs_repair(6, 3, l_dp, l_dok, l_pp, l_pok, L_LEN), "a forged row left over not found");
	}

	s_begin("Reed-Solomon: the products of the vector instructions are those of the portable code");
	{
	static	uint8_t	l_src [1100], l_d1 [1100], l_d2 [1100];
	uint32_t	l_bad = 0;

	for ( uint32_t k = 0; k < sizeof(l_src); k++ )
		l_src [k] = (uint8_t) (k * 131 + (k >> 3));

	printf("%s  the products: %s\n", s_tap ? "#" : "", vbk$rs_simd());

	for ( uint32_t c = 0; c < 256; c += 5 )
		for ( uint32_t l_off = 0; l_off < 4; l_off++ )
			for ( uint32_t l_len = 0; l_len < 1090; l_len += 97 )
				{
				memset(l_d1, 0xA5, sizeof(l_d1));
				memset(l_d2, 0xA5, sizeof(l_d2));

				vbk$rs_muladd(l_d1 + l_off, l_src + 3 - (l_off & 1), l_len, (uint8_t) c);

				/* One octet at a time: below a vector, the portable code */
				for ( uint32_t k = 0; k < l_len; k++ )
					vbk$rs_muladd(l_d2 + l_off + k, l_src + 3 - (l_off & 1) + k, 1, (uint8_t) c);

				l_bad	+= !!memcmp(l_d1, l_d2, sizeof(l_d1));
				}

	$CHECK(!l_bad, "%u products differ (%s)", l_bad, vbk$rs_simd());
	}

	s_begin("Deflate and LZMA: every effort gives back what it was given, and the same data the same octets");
	{
	static	uint8_t	l_src [200000], l_z1 [VBK$LZM_BOUND(200000) + VBK$DFL_BOUND(200000)], l_z2 [sizeof(l_z1)], l_out [200000];
	static	const uint32_t	l_lens [] = { 1, 2, 3, 17, 258, 1000, 65535, 65536, 70001, 200000 };
	uint32_t	l_bad = 0, l_z1len, l_z2len;

	for ( int l_kind = 0; l_kind < 4; l_kind++ )
		{
		/* Text-like, zeros, noise, a run broken now and then */
		for ( uint32_t k = 0; k < sizeof(l_src); k++ )
			l_src [k] = (l_kind == 0) ? (uint8_t) "the quick brown fox jumps over the lazy dog\n" [(k * 7 + k / 91) % 44]
				  : (l_kind == 1) ? 0 : (l_kind == 2) ? (uint8_t) ((k * 2654435761U) >> 13) : (uint8_t) ((k % 977) ? 'A' : k);

		for ( size_t i = 0; i < $ARRSZ(l_lens); i++ )
			for ( int l_codec = 0; l_codec < 2; l_codec++ )
				for ( int e = 1; e <= 4; e++ )
					{
					int	l_s1 = l_codec ? vbk$lzm_compress(l_src, l_lens [i], l_z1, sizeof(l_z1), &l_z1len, e)
							       : vbk$dfl_compress(l_src, l_lens [i], l_z1, sizeof(l_z1), &l_z1len, e);
					int	l_s2 = l_codec ? vbk$lzm_compress(l_src, l_lens [i], l_z2, sizeof(l_z2), &l_z2len, e)
							       : vbk$dfl_compress(l_src, l_lens [i], l_z2, sizeof(l_z2), &l_z2len, e);
					int	l_d = l_codec ? vbk$lzm_decompress(l_z1, l_z1len, l_out, l_lens [i])
							      : vbk$dfl_decompress(l_z1, l_z1len, l_out, l_lens [i]);

					l_bad	+= !(1 & l_s1) || !(1 & l_s2) || (l_z1len != l_z2len) || memcmp(l_z1, l_z2, l_z1len)
						|| (l_d != STS$K_SUCCESS) || memcmp(l_out, l_src, l_lens [i]);
					}
		}

	$CHECK(!l_bad, "%u round trips failed or were not the same twice", l_bad);
	}

	s_begin("Deflate and LZMA: streams of zlib (fixed, dynamic, stored) and of liblzma are read");
	{
	static	const uint8_t	l_text [] = "hello hello hello hello, vBACKUP!\nhello hello hello hello, vBACKUP!\nhello hello hello hello, vBACKUP!\n";
	static	const uint8_t	l_fixed [] = { 0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0x57, 0xC8, 0x40, 0x27, 0x75, 0x14, 0xCA, 0x9C, 0x1C, 0x9D, 0xBD, 0x43,
					0x03, 0x14, 0xB9, 0x30, 0xE4, 0xC8, 0x50, 0x01, 0x00 };
	static	const uint8_t	l_dyn [] = { 0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0x57, 0xC8, 0x40, 0x27, 0x75, 0x14, 0xCA, 0x9C, 0x1C, 0x9D, 0xBD, 0x43,
					0x03, 0x14, 0xB9, 0x32, 0xA8, 0xA0, 0x02, 0x00 };
	static	const uint8_t	l_lzma [] = { 0x00, 0x34, 0x19, 0x49, 0xEE, 0x8D, 0xE9, 0x56, 0x0A, 0xE7, 0x79, 0x9C, 0xDF, 0x9A, 0x67, 0xBE, 0x49,
					0x2D, 0x51, 0x99, 0x12, 0xCF, 0x74, 0x7D, 0x3E, 0xB5, 0x6F, 0xFF, 0xFF, 0x4D, 0xA8, 0x00, 0x00 };
	uint8_t		l_stored [5 + 102], l_out [102];
	uint32_t	l_n = sizeof(l_text) - 1;

	/* A stored block, as zlib makes it at level 0 */
	l_stored [0] = 1; l_stored [1] = (uint8_t) l_n; l_stored [2] = 0; l_stored [3] = (uint8_t) ~l_n; l_stored [4] = 0xFF;
	memcpy(l_stored + 5, l_text, l_n);

	$CHECK((STS$K_SUCCESS == vbk$dfl_decompress(l_fixed, sizeof(l_fixed), l_out, l_n)) && !memcmp(l_out, l_text, l_n), "zlib, fixed codes");
	$CHECK((STS$K_SUCCESS == vbk$dfl_decompress(l_dyn, sizeof(l_dyn), l_out, l_n)) && !memcmp(l_out, l_text, l_n), "zlib, dynamic codes");
	$CHECK((STS$K_SUCCESS == vbk$dfl_decompress(l_stored, sizeof(l_stored), l_out, l_n)) && !memcmp(l_out, l_text, l_n), "zlib, stored");
	$CHECK((STS$K_SUCCESS == vbk$lzm_decompress(l_lzma, sizeof(l_lzma), l_out, l_n)) && !memcmp(l_out, l_text, l_n), "liblzma, raw LZMA1");
	$CHECK(STS$K_SUCCESS != vbk$dfl_decompress(l_dyn, sizeof(l_dyn), l_out, l_n - 1), "Deflate: a byte too many taken");
	$CHECK(STS$K_SUCCESS != vbk$lzm_decompress(l_lzma, sizeof(l_lzma) - 1, l_out, l_n), "LZMA: a stream cut short taken");
	}

	s_begin("Deflate and LZMA: a damaged stream never writes past its length");
	{
	static	uint8_t	l_src [50000], l_z [VBK$LZM_BOUND(50000) + VBK$DFL_BOUND(50000)], l_d [sizeof(l_z)], l_out [50000 + 64];
	uint32_t	l_zlen, l_over = 0, l_seed = 12345;

	for ( uint32_t k = 0; k < sizeof(l_src); k++ )
		l_src [k] = (uint8_t) "lorem ipsum dolor sit amet, consectetur adipiscing elit " [(k * 3 + k / 77) % 56];

	for ( int l_codec = 0; l_codec < 2; l_codec++ )
		{
		if ( l_codec )
			vbk$lzm_compress(l_src, sizeof(l_src), l_z, sizeof(l_z), &l_zlen, 2);
		else	vbk$dfl_compress(l_src, sizeof(l_src), l_z, sizeof(l_z), &l_zlen, 2);

		for ( int t = 0; t < 400; t++ )
			{
			int	l_s;

			memcpy(l_d, l_z, l_zlen);
			l_seed	= l_seed * 1103515245U + 12345U;
			l_d [(l_seed >> 8) % l_zlen] ^= (uint8_t) (1U << ((l_seed >> 4) & 7));
			memset(l_out + sizeof(l_src), 0xA5, 64);

			/*
			**  Refused, or other octets: neither codec has a checksum of its
			**  own - the CRC of the block and of the file (FEND) find it.
			**  What must never be is a write past the length.
			*/
			l_s	= l_codec ? vbk$lzm_decompress(l_d, l_zlen, l_out, sizeof(l_src)) : vbk$dfl_decompress(l_d, l_zlen, l_out, sizeof(l_src));
			(void) l_s;

			for ( int k = 0; k < 64; k++ )
				l_over	+= (l_out [sizeof(l_src) + k] != 0xA5);
			}
		}

	$CHECK(!l_over, "%u octets written past the end", l_over);
	}

	s_begin("VBK$DATA_PACK: the codec of each /LEVEL, checked; what does not compress is stored");
	{
	static	uint8_t	l_src [300000], l_z [VBK$LZ4_BOUND(300000)], l_chk [300000];
	uint32_t	l_zlen, l_codec, l_bad = 0;

	for ( uint32_t k = 0; k < sizeof(l_src); k++ )
		l_src [k] = (uint8_t) "records of a saveset, compressed, decompressed and compared " [(k * 5 + k / 61) % 60];

	for ( int l = 1; l <= VBK$K_ZLEVELS; l++ )
		l_bad += (STS$K_SUCCESS != vbk$data_pack(l, l_src, sizeof(l_src), l_z, sizeof(l_z), &l_zlen, &l_codec, l_chk)) || (l_codec != vbk$data_codec(l))
			|| (l_codec != (uint32_t) ((l == 1) ? VBK$K_CODEC_LZ4 : (l <= 5) ? VBK$K_CODEC_DEFLATE : VBK$K_CODEC_LZMA));

	$CHECK(!l_bad, "%u levels did not compress as they should", l_bad);

	/* Noise: xorshift */
	for ( uint32_t k = 0, l_x = 2463534242U; k < sizeof(l_src); k++ )
		{
		l_x ^= l_x << 13;
		l_x ^= l_x >> 17;
		l_x ^= l_x << 5;
		l_src [k] = (uint8_t) (l_x >> 24);
		}

	l_bad	= 0;

	for ( int l = 1; l <= VBK$K_ZLEVELS; l++ )
		l_bad += (STS$K_WARN != vbk$data_pack(l, l_src, sizeof(l_src), l_z, sizeof(l_z), &l_zlen, &l_codec, l_chk));

	$CHECK(!l_bad, "%u levels compressed noise", l_bad);
	}

	s_begin("SOLID: opened in version 3, a forged one a gap - never a wrong record");
	{
	static	uint8_t	l_raw [4096], l_bad [64];
	uint32_t	l_n;
	char		l_got [64];

	/* FILE 2, DATA 3, FEND 4 inside; "A" FILE 2 and "B" FEND 4 around it */
	l_n = s_inner(l_raw, 0, VBK$K_RT_FILE, 100, 'f');
	l_n = s_inner(l_raw, l_n, VBK$K_RT_DATA, 2000, 'd');
	l_n = s_inner(l_raw, l_n, VBK$K_RT_FEND, 40, 'e');

	$CHECK((1 & s_solid("sol1.bck", 1, l_raw, l_n, NULL, 0, 0, l_got, sizeof(l_got))) && !strcmp(l_got, "22S3S4S4"), "good SOLID: %s", l_got);
	$CHECK((1 & s_solid("sol2.bck", 0, l_raw, l_n, NULL, 0, 0, l_got, sizeof(l_got))) && !strcmp(l_got, "284"), "version 1: not opened, %s", l_got);
	$CHECK((1 & s_solid("sol3.bck", 1, l_raw, l_n, NULL, 0, 1, l_got, sizeof(l_got))) && !strcmp(l_got, "22S22S3S4S4"), "a seek leaves it: %s", l_got);

	/* A DATAZ inside, then a record running past the end: what is before them comes, the rest is a gap */
	l_n = s_inner(l_raw, 0, VBK$K_RT_FILE, 10, 'f');
	l_n = s_inner(l_raw, l_n, VBK$K_RT_DATAZ, 10, 'z');
	$CHECK((1 & s_solid("sol4.bck", 1, l_raw, l_n, NULL, 0, 0, l_got, sizeof(l_got))) && !strcmp(l_got, "22S!4"), "DATAZ inside: %s", l_got);

	l_n = s_inner(l_raw, 0, VBK$K_RT_FILE, 10, 'f');
	l_n = s_inner(l_raw, l_n, VBK$K_RT_DATA, 10, 'd');
	vbk$put32(l_raw + 18 + 4, 11);
	$CHECK((1 & s_solid("sol5.bck", 1, l_raw, l_n, NULL, 0, 0, l_got, sizeof(l_got))) && !strcmp(l_got, "22S!4"), "a record past its end: %s", l_got);

	/* The header: a codec unknown, RAWLEN out of bounds or not what comes out, a stream cut short */
	l_n = s_inner(l_raw, 0, VBK$K_RT_FILE, 10, 'f');
	memset(l_bad, 0, sizeof(l_bad));
	vbk$put32(l_bad, 9);
	vbk$put32(l_bad + 4, 18);
	$CHECK((1 & s_solid("sol6.bck", 1, NULL, 0, l_bad, sizeof(l_bad), 0, l_got, sizeof(l_got))) && !strcmp(l_got, "2!4") && s_ev [VBK$K_EV_BADREC],
		"codec 9: %s", l_got);
	vbk$put32(l_bad, VBK$K_CODEC_LZ4);
	vbk$put32(l_bad + 4, VBK$K_MAXSOLID + 1);
	$CHECK((1 & s_solid("sol7.bck", 1, NULL, 0, l_bad, sizeof(l_bad), 0, l_got, sizeof(l_got))) && !strcmp(l_got, "2!4"), "RAWLEN too big: %s", l_got);
	$CHECK((1 & s_solid("sol8.bck", 1, NULL, 0, l_bad, 8, 0, l_got, sizeof(l_got))) && !strcmp(l_got, "2!4"), "no header: %s", l_got);

	{
	static	uint8_t	l_z [VBK$K_SOLIDHDR + 8192], l_chk [4096];
	uint32_t	l_zlen, l_codec;

	l_n = s_inner(l_raw, 0, VBK$K_RT_FILE, 10, 'f');
	l_n = s_inner(l_raw, l_n, VBK$K_RT_DATA, 3000, 'd');
	vbk$data_pack(6, l_raw, l_n, l_z + VBK$K_SOLIDHDR, sizeof(l_z) - VBK$K_SOLIDHDR, &l_zlen, &l_codec, l_chk);
	vbk$put32(l_z, l_codec);
	vbk$put32(l_z + 4, l_n + 1);
	$CHECK((1 & s_solid("sol9.bck", 1, NULL, 0, l_z, VBK$K_SOLIDHDR + l_zlen, 0, l_got, sizeof(l_got))) && !strcmp(l_got, "2!4"), "RAWLEN not what comes out: %s", l_got);
	vbk$put32(l_z + 4, l_n);
	$CHECK((1 & s_solid("sol10.bck", 1, NULL, 0, l_z, VBK$K_SOLIDHDR + l_zlen - 3, 0, l_got, sizeof(l_got))) && !strcmp(l_got, "2!4"), "cut short: %s", l_got);
	$CHECK((1 & s_solid("sol11.bck", 1, NULL, 0, l_z, VBK$K_SOLIDHDR + l_zlen, 0, l_got, sizeof(l_got))) && !strcmp(l_got, "22S3S4"), "the same, whole: %s", l_got);
	}
	}

	s_begin("VBK$DATA_SALVAGE: a stream cut anywhere gives the beginning of the data, never a wrong octet");
	{
	static	uint8_t	l_src [200000], l_z [VBK$LZ4_BOUND(200000)], l_chk [200000], l_out [200000];
	uint32_t	l_zlen, l_codec, l_got, l_bad = 0, l_short = 0, l_n = 0;

	for ( uint32_t k = 0, l_x = 12345; k < sizeof(l_src); k++ )
		{
		l_x	= l_x * 1103515245U + 12345U;
		l_src [k] = (k % 7000 < 3500) ? (uint8_t) "some text of a file, again and again; " [(k * 3 + k / 37) % 38] : (uint8_t) (l_x >> 24);
		}

	for ( int l = 1; l <= VBK$K_ZLEVELS; l++ )
		{
		if ( STS$K_SUCCESS != vbk$data_pack(l, l_src, sizeof(l_src), l_z, sizeof(l_z), &l_zlen, &l_codec, l_chk) )
			{
			l_bad++;
			continue;
			}

		for ( uint32_t l_cut = 1; l_cut < l_zlen; l_cut += 1 + l_zlen / 97 )
			{
			memset(l_out, 0xA5, sizeof(l_out));
			vbk$data_salvage(l_codec, l_z, l_cut, l_out, sizeof(l_src), &l_got);
			l_n++;

			if ( (l_got > sizeof(l_src)) || memcmp(l_out, l_src, l_got) )
				{
				l_bad++;
				$NOTE("/LEVEL=%d, codec %u, cut at %u of %u: %u octets out, wrong", l, l_codec, l_cut, l_zlen, l_got);
				}

			/* Most of what was there comes back: well over half of its share */
			if ( (l_cut > l_zlen / 10) && ((uint64_t) l_got * l_zlen < (uint64_t) sizeof(l_src) * l_cut / 2) )
				l_short++;
			}

		vbk$data_salvage(l_codec, l_z, l_zlen, l_out, sizeof(l_src), &l_got);

		if ( (l_got != sizeof(l_src)) || memcmp(l_out, l_src, l_got) )
			{
			l_bad++;
			$NOTE("/LEVEL=%d, codec %u, the whole stream: %u octets out of %u", l, l_codec, l_got, (uint32_t) sizeof(l_src));
			}
		}

	$CHECK(!l_bad, "%u of %u cuts gave a wrong octet, or the whole stream not all", l_bad, l_n);
	$CHECK(!l_short, "%u of %u cuts gave back too little", l_short, l_n);
	}

	{
	static	uint8_t	l_raw [8192], l_z [VBK$K_SOLIDHDR + 16384], l_chk [8192];
	uint32_t	l_n, l_zlen, l_codec;
	char		l_got [64];

	/* Two files of a SOLID, the second a big DATA: the stream cut in the middle of it */
	l_n = s_inner(l_raw, 0, VBK$K_RT_FILE, 20, 'f');
	l_n = s_inner(l_raw, l_n, VBK$K_RT_FEND, 20, 'e');
	l_n = s_inner(l_raw, l_n, VBK$K_RT_FILE, 20, 'g');
	for ( uint32_t k = 0; k < 6000; k++ )
		l_raw [l_n + VBK$K_RECHDR + k] = (uint8_t) (k * 7 + k / 13);
	vbk$put16(l_raw + l_n, VBK$K_RT_DATA);
	vbk$put16(l_raw + l_n + 2, 0);
	vbk$put32(l_raw + l_n + 4, 6000);
	l_n += VBK$K_RECHDR + 6000;
	vbk$data_pack(6, l_raw, l_n, l_z + VBK$K_SOLIDHDR, sizeof(l_z) - VBK$K_SOLIDHDR, &l_zlen, &l_codec, l_chk);
	vbk$put32(l_z, l_codec);
	vbk$put32(l_z + 4, l_n);
	vbk$put32(l_z + 8, 2);
	$CHECK((1 & s_solid("sol12.bck", 1, NULL, 0, l_z, VBK$K_SOLIDHDR + l_zlen, 0, l_got, sizeof(l_got))) && !strcmp(l_got, "22S4S2S3S4"), "whole: %s", l_got);
	}

	if ( s_tap )
		printf("# %d test%s, %d check%s, %d failure%s\n1..%d\n", s_ntest, (s_ntest == 1) ? "" : "s", s_nchk, (s_nchk == 1) ? "" : "s",
			s_fail, (s_fail == 1) ? "" : "s", s_nchk);
	else	printf("\n%d test%s, %d failure%s\n", s_ntest, (s_ntest == 1) ? "" : "s", s_fail, (s_fail == 1) ? "" : "s");

	return	s_fail ? 1 : 0;
}
