#define	__MODULE__	"UNITS"
#define	__IDENT__	"X01-04"
#define	__REV__		"1.4.0"

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

#define	UNITS$K_NREC	400			/* Records of the synthetic saveset		*/
#define	UNITS$K_BSZ	8192			/* Small blocks: many of them, many volumes	*/

static	int	s_fail, s_ntest, s_nchk, s_tap;
static	const char *	s_title = "";
static	char	s_dir [1024];
static	VBK$LOC	s_loc [UNITS$K_NREC];
static	int	s_ev [8];

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

	if ( s_tap )
		printf("# %d test%s, %d check%s, %d failure%s\n1..%d\n", s_ntest, (s_ntest == 1) ? "" : "s", s_nchk, (s_nchk == 1) ? "" : "s",
			s_fail, (s_fail == 1) ? "" : "s", s_nchk);
	else	printf("\n%d test%s, %d failure%s\n", s_ntest, (s_ntest == 1) ? "" : "s", s_fail, (s_fail == 1) ? "" : "s");

	return	s_fail ? 1 : 0;
}
