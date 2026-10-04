#define	__MODULE__	"VBKCRP"
#define	__IDENT__	"X01-06"
#define	__REV__		"1.6.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkcrp.c
**
**  ABSTRACT:	The encryption of a saveset (format.md, 6.10).
**
**  DESCRIPTION: Two primitives, written from their documents and nothing
**		else: ChaCha20 (RFC 8439, 2.3-2.4) and SHA-256 (FIPS 180-4);
**		HMAC (RFC 2104) and PBKDF2 (RFC 8018, 5.2) are built from the
**		second.  Plain portable C, byte by byte where the byte order
**		matters - the same bytes on every machine, a few hundred
**		lines a reader of the format can check by eye.  test/units.c
**		holds the vectors of the RFCs.
**
**		On top of them the three things of the format: the keys of a
**		saveset out of a passphrase and its SALT, the TAG of a block
**		over its header fields and its ciphertext, and the sealing
**		and opening of a payload.  The TAG is compared in constant
**		time, keys are wiped by the callers through VBK$CRP_WIPE.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  5-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-06		 5-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<string.h>

#include	"vbkcrp.h"
#include	"vbkos.h"


/*
**  SHA-256, FIPS 180-4
*/
static const uint32_t	s_vbk$k256 [64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	};

#define	ROR32(x, n)	(((x) >> (n)) | ((x) << (32 - (n))))

static	void	s_vbk$sha256_block	(
		uint32_t	a_h [8],
	const	uint8_t *	a_p
			)
{
uint32_t	l_w [64], l_a, l_b, l_c, l_d, l_e, l_f, l_g, l_hh, l_t1, l_t2;

	for ( int i = 0; i < 16; i++ )
		l_w [i] = ((uint32_t) a_p [4 * i] << 24) | ((uint32_t) a_p [4 * i + 1] << 16)
			| ((uint32_t) a_p [4 * i + 2] << 8) | (uint32_t) a_p [4 * i + 3];

	for ( int i = 16; i < 64; i++ )
		l_w [i] = (ROR32(l_w [i - 2], 17) ^ ROR32(l_w [i - 2], 19) ^ (l_w [i - 2] >> 10)) + l_w [i - 7]
			+ (ROR32(l_w [i - 15], 7) ^ ROR32(l_w [i - 15], 18) ^ (l_w [i - 15] >> 3)) + l_w [i - 16];

	l_a = a_h [0]; l_b = a_h [1]; l_c = a_h [2]; l_d = a_h [3];
	l_e = a_h [4]; l_f = a_h [5]; l_g = a_h [6]; l_hh = a_h [7];

	for ( int i = 0; i < 64; i++ )
		{
		l_t1	= l_hh + (ROR32(l_e, 6) ^ ROR32(l_e, 11) ^ ROR32(l_e, 25)) + ((l_e & l_f) ^ (~l_e & l_g))
			+ s_vbk$k256 [i] + l_w [i];
		l_t2	= (ROR32(l_a, 2) ^ ROR32(l_a, 13) ^ ROR32(l_a, 22)) + ((l_a & l_b) ^ (l_a & l_c) ^ (l_b & l_c));

		l_hh = l_g; l_g = l_f; l_f = l_e; l_e = l_d + l_t1;
		l_d = l_c; l_c = l_b; l_b = l_a; l_a = l_t1 + l_t2;
		}

	a_h [0] += l_a; a_h [1] += l_b; a_h [2] += l_c; a_h [3] += l_d;
	a_h [4] += l_e; a_h [5] += l_f; a_h [6] += l_g; a_h [7] += l_hh;
}

void	vbk$sha256_init		(
		VBK$SHA256 *	a_ctx
			)
{
static const uint32_t	l_iv [8] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

	memcpy(a_ctx->h, l_iv, sizeof(l_iv));
	a_ctx->nbuf	= 0;
	a_ctx->total	= 0;
}

void	vbk$sha256_update	(
		VBK$SHA256 *	a_ctx,
	const	void *		a_data,
		size_t		a_len
			)
{
const uint8_t *	l_p = a_data;
size_t		l_n;

	a_ctx->total	+= a_len;

	if ( a_ctx->nbuf )
		{
		l_n	= 64 - a_ctx->nbuf;
		l_n	= (a_len < l_n) ? a_len : l_n;

		memcpy(a_ctx->buf + a_ctx->nbuf, l_p, l_n);
		a_ctx->nbuf	+= (uint32_t) l_n;
		l_p		+= l_n;
		a_len		-= l_n;

		if ( a_ctx->nbuf < 64 )
			return;

		s_vbk$sha256_block(a_ctx->h, a_ctx->buf);
		a_ctx->nbuf	= 0;
		}

	for ( ; a_len >= 64; l_p += 64, a_len -= 64 )
		s_vbk$sha256_block(a_ctx->h, l_p);

	memcpy(a_ctx->buf, l_p, a_len);
	a_ctx->nbuf	= (uint32_t) a_len;
}

void	vbk$sha256_final	(
		VBK$SHA256 *	a_ctx,
		uint8_t		a_dig [VBK$K_KEYSZ]
			)
{
uint64_t	l_bits = a_ctx->total * 8;
uint8_t		l_pad [72];
size_t		l_n;

	/* 0x80, zeros up to 56 modulo 64, the length in bits big-endian */
	l_n	= ((a_ctx->nbuf < 56) ? 56 : 120) - a_ctx->nbuf;

	memset(l_pad, 0, sizeof(l_pad));
	l_pad [0]	= 0x80;

	for ( int i = 0; i < 8; i++ )
		l_pad [l_n + i] = (uint8_t) (l_bits >> (56 - 8 * i));

	vbk$sha256_update(a_ctx, l_pad, l_n + 8);

	for ( int i = 0; i < 8; i++ )
		{
		a_dig [4 * i]	  = (uint8_t) (a_ctx->h [i] >> 24);
		a_dig [4 * i + 1] = (uint8_t) (a_ctx->h [i] >> 16);
		a_dig [4 * i + 2] = (uint8_t) (a_ctx->h [i] >> 8);
		a_dig [4 * i + 3] = (uint8_t) a_ctx->h [i];
		}

	vbk$crp_wipe(a_ctx, sizeof(*a_ctx));
}


/*
**  HMAC-SHA256, RFC 2104: the inner and outer pads are hashed once, at
**  the key setup; every MAC then starts from copies of the two states
*/
void	vbk$hmac_init		(
		VBK$HMAC *	a_ctx,
	const	void *		a_key,
		size_t		a_klen
			)
{
uint8_t		l_k [64], l_pad [64];
VBK$SHA256	l_s;

	memset(l_k, 0, sizeof(l_k));

	if ( a_klen > 64 )
		{
		vbk$sha256_init(&l_s);
		vbk$sha256_update(&l_s, a_key, a_klen);
		vbk$sha256_final(&l_s, l_k);
		}
	else	memcpy(l_k, a_key, a_klen);

	for ( int i = 0; i < 64; i++ )
		l_pad [i] = l_k [i] ^ 0x36;

	vbk$sha256_init(&a_ctx->inner);
	vbk$sha256_update(&a_ctx->inner, l_pad, 64);

	for ( int i = 0; i < 64; i++ )
		l_pad [i] = l_k [i] ^ 0x5c;

	vbk$sha256_init(&a_ctx->outer);
	vbk$sha256_update(&a_ctx->outer, l_pad, 64);

	vbk$crp_wipe(l_k, sizeof(l_k));
	vbk$crp_wipe(l_pad, sizeof(l_pad));
}

void	vbk$hmac		(
	const	VBK$HMAC *	a_key,
	const	void *		a_data,
		size_t		a_len,
		uint8_t		a_mac [VBK$K_KEYSZ]
			)
{
VBK$SHA256	l_s;
uint8_t		l_in [VBK$K_KEYSZ];

	l_s	= a_key->inner;
	vbk$sha256_update(&l_s, a_data, a_len);
	vbk$sha256_final(&l_s, l_in);

	l_s	= a_key->outer;
	vbk$sha256_update(&l_s, l_in, sizeof(l_in));
	vbk$sha256_final(&l_s, a_mac);

	vbk$crp_wipe(l_in, sizeof(l_in));
}


/*
**  PBKDF2-HMAC-SHA256, RFC 8018 5.2: T_i = U_1 ^ ... ^ U_c,
**  U_1 = PRF(P, S || INT(i)), U_j = PRF(P, U_{j-1})
*/
void	vbk$pbkdf2		(
	const	void *		a_pass,
		size_t		a_plen,
	const	uint8_t *	a_salt,
		size_t		a_slen,
		uint32_t	a_iter,
		uint8_t *	a_out,
		size_t		a_olen
			)
{
VBK$HMAC	l_prf;
VBK$SHA256	l_s;
uint8_t		l_u [VBK$K_KEYSZ], l_t [VBK$K_KEYSZ], l_ctr [4];
size_t		l_n;

	vbk$hmac_init(&l_prf, a_pass, a_plen);

	for ( uint32_t l_i = 1; a_olen; l_i++ )
		{
		l_ctr [0] = (uint8_t) (l_i >> 24);
		l_ctr [1] = (uint8_t) (l_i >> 16);
		l_ctr [2] = (uint8_t) (l_i >> 8);
		l_ctr [3] = (uint8_t) l_i;

		/* U_1: the salt and the block number go through the MAC as one message */
		l_s	= l_prf.inner;
		vbk$sha256_update(&l_s, a_salt, a_slen);
		vbk$sha256_update(&l_s, l_ctr, 4);
		vbk$sha256_final(&l_s, l_u);
		l_s	= l_prf.outer;
		vbk$sha256_update(&l_s, l_u, sizeof(l_u));
		vbk$sha256_final(&l_s, l_u);

		memcpy(l_t, l_u, sizeof(l_t));

		for ( uint32_t j = 1; j < a_iter; j++ )
			{
			vbk$hmac(&l_prf, l_u, sizeof(l_u), l_u);

			for ( int k = 0; k < VBK$K_KEYSZ; k++ )
				l_t [k] ^= l_u [k];
			}

		l_n	= (a_olen < sizeof(l_t)) ? a_olen : sizeof(l_t);
		memcpy(a_out, l_t, l_n);
		a_out	+= l_n;
		a_olen	-= l_n;
		}

	vbk$crp_wipe(&l_prf, sizeof(l_prf));
	vbk$crp_wipe(l_u, sizeof(l_u));
	vbk$crp_wipe(l_t, sizeof(l_t));
}


/*
**  ChaCha20, RFC 8439 2.3-2.4: the key stream XORed into the data in place
*/
#define	ROL32(x, n)	(((x) << (n)) | ((x) >> (32 - (n))))
#define	QR(a, b, c, d)	a += b; d ^= a; d = ROL32(d, 16); c += d; b ^= c; b = ROL32(b, 12); \
			a += b; d ^= a; d = ROL32(d, 8);  c += d; b ^= c; b = ROL32(b, 7)

static	uint32_t	s_vbk$le32	(
	const	uint8_t *	a_p
			)
{
	return	(uint32_t) a_p [0] | ((uint32_t) a_p [1] << 8) | ((uint32_t) a_p [2] << 16) | ((uint32_t) a_p [3] << 24);
}

void	vbk$chacha20		(
	const	uint8_t		a_key [VBK$K_KEYSZ],
	const	uint8_t		a_nonce [12],
		uint32_t	a_ctr,
		uint8_t *	a_data,
		size_t		a_len
			)
{
uint32_t	l_in [16], l_x [16];
uint8_t		l_ks [64];
size_t		l_n;

	l_in [0] = 0x61707865; l_in [1] = 0x3320646e; l_in [2] = 0x79622d32; l_in [3] = 0x6b206574;

	for ( int i = 0; i < 8; i++ )
		l_in [4 + i] = s_vbk$le32(a_key + 4 * i);

	l_in [12] = a_ctr;
	l_in [13] = s_vbk$le32(a_nonce);
	l_in [14] = s_vbk$le32(a_nonce + 4);
	l_in [15] = s_vbk$le32(a_nonce + 8);

	while ( a_len )
		{
		memcpy(l_x, l_in, sizeof(l_x));

		for ( int i = 0; i < 10; i++ )
			{
			QR(l_x [0], l_x [4], l_x [8], l_x [12]);
			QR(l_x [1], l_x [5], l_x [9], l_x [13]);
			QR(l_x [2], l_x [6], l_x [10], l_x [14]);
			QR(l_x [3], l_x [7], l_x [11], l_x [15]);
			QR(l_x [0], l_x [5], l_x [10], l_x [15]);
			QR(l_x [1], l_x [6], l_x [11], l_x [12]);
			QR(l_x [2], l_x [7], l_x [8], l_x [13]);
			QR(l_x [3], l_x [4], l_x [9], l_x [14]);
			}

		for ( int i = 0; i < 16; i++ )
			{
			uint32_t	l_v = l_x [i] + l_in [i];

			l_ks [4 * i]	 = (uint8_t) l_v;
			l_ks [4 * i + 1] = (uint8_t) (l_v >> 8);
			l_ks [4 * i + 2] = (uint8_t) (l_v >> 16);
			l_ks [4 * i + 3] = (uint8_t) (l_v >> 24);
			}

		l_n	= (a_len < 64) ? a_len : 64;

		for ( size_t i = 0; i < l_n; i++ )
			a_data [i] ^= l_ks [i];

		a_data	+= l_n;
		a_len	-= l_n;
		l_in [12]++;
		}

	vbk$crp_wipe(l_x, sizeof(l_x));
	vbk$crp_wipe(l_ks, sizeof(l_ks));
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The keys of a saveset: MK out of the passphrase and the SALT, then
**	KENC, KMAC and CHECK out of MK (format.md, 6.10).
**
**  FORMAL PARAMETERS:
**
**	a_keys		Receives the keys; the caller wipes it after use
**	a_pass		The passphrase, its bytes as they are
**	a_plen		Its length
**	a_salt		SALT of the VHDR
**	a_iter		KDFITER
**--
*/
void	vbk$crp_derive		(
		VBK$KEYS *	a_keys,
	const	void *		a_pass,
		size_t		a_plen,
	const	uint8_t		a_salt [VBK$K_SALTSZ],
		uint32_t	a_iter
			)
{
uint8_t		l_mk [VBK$K_KEYSZ], l_kmac [VBK$K_KEYSZ];
VBK$HMAC	l_h;

	vbk$pbkdf2(a_pass, a_plen, a_salt, VBK$K_SALTSZ, a_iter, l_mk, sizeof(l_mk));

	vbk$hmac_init(&l_h, l_mk, sizeof(l_mk));
	vbk$hmac(&l_h, "VBACKUP ENC", 11, a_keys->enc);
	vbk$hmac(&l_h, "VBACKUP MAC", 11, l_kmac);
	vbk$hmac(&l_h, "VBACKUP CHECK", 13, a_keys->check);

	vbk$hmac_init(&a_keys->mac, l_kmac, sizeof(l_kmac));

	vbk$crp_wipe(l_mk, sizeof(l_mk));
	vbk$crp_wipe(l_kmac, sizeof(l_kmac));
	vbk$crp_wipe(&l_h, sizeof(l_h));
}


/*
**  The TAG of a block: the header fields of format.md 6.10, then the ciphertext
*/
void	vbk$crp_tag		(
	const	VBK$KEYS *	a_keys,
	const	VBK$BHDR *	a_hdr,
	const	uint8_t *	a_ct,
		uint8_t		a_tag [VBK$K_TAGSZ]
			)
{
uint8_t		l_m [VBK$K_UUIDSZ + 4 + 8 + 4 + 1 + 2 + 4 + 4], *l_p = l_m, l_in [VBK$K_KEYSZ];
VBK$SHA256	l_s;

	memcpy(l_p, a_hdr->ssuuid, VBK$K_UUIDSZ);	l_p += VBK$K_UUIDSZ;
	vbk$put32(l_p, a_hdr->bsize);			l_p += 4;
	vbk$put64(l_p, a_hdr->blkno);			l_p += 8;
	vbk$put32(l_p, a_hdr->volno);			l_p += 4;
	*l_p++	= a_hdr->type;
	vbk$put16(l_p, a_hdr->gindex);			l_p += 2;
	vbk$put32(l_p, a_hdr->recoff);			l_p += 4;
	vbk$put32(l_p, a_hdr->paylen);

	l_s	= a_keys->mac.inner;
	vbk$sha256_update(&l_s, l_m, sizeof(l_m));
	vbk$sha256_update(&l_s, a_ct, a_hdr->paylen);
	vbk$sha256_final(&l_s, l_in);

	l_s	= a_keys->mac.outer;
	vbk$sha256_update(&l_s, l_in, sizeof(l_in));
	vbk$sha256_final(&l_s, a_tag);
}

static	void	s_vbk$nonce	(
		uint64_t	a_blkno,
		uint8_t		a_nonce [12]
			)
{
	vbk$put32(a_nonce, 0);
	vbk$put64(a_nonce + 4, a_blkno);
}


/*
**  Encrypt <paylen> octets of the payload area in place and put the TAG
**  into its last 32 octets; the header fields are final already
*/
void	vbk$crp_seal		(
	const	VBK$KEYS *	a_keys,
	const	VBK$BHDR *	a_hdr,
		uint8_t *	a_pay,
		uint32_t	a_psize
			)
{
uint8_t		l_nonce [12];

	s_vbk$nonce(a_hdr->blkno, l_nonce);
	vbk$chacha20(a_keys->enc, l_nonce, 0, a_pay, a_hdr->paylen);
	vbk$crp_tag(a_keys, a_hdr, a_pay, a_pay + a_psize - VBK$K_TAGSZ);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Check the TAG of a payload and, when it is right, decrypt the
**	payload in place.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- decrypted;
**	STS$K_ERROR	- the TAG is wrong or PAYLEN does not fit: nothing done.
**--
*/
int	vbk$crp_open		(
	const	VBK$KEYS *	a_keys,
	const	VBK$BHDR *	a_hdr,
		uint8_t *	a_pay,
		uint32_t	a_psize
			)
{
uint8_t		l_tag [VBK$K_TAGSZ], l_nonce [12];

	if ( (a_psize < VBK$K_TAGSZ) || (a_hdr->paylen > (a_psize - VBK$K_TAGSZ)) )
		return	STS$K_ERROR;

	vbk$crp_tag(a_keys, a_hdr, a_pay, l_tag);

	if ( !vbk$crp_equal(l_tag, a_pay + a_psize - VBK$K_TAGSZ, VBK$K_TAGSZ) )
		return	STS$K_ERROR;

	s_vbk$nonce(a_hdr->blkno, l_nonce);
	vbk$chacha20(a_keys->enc, l_nonce, 0, a_pay, a_hdr->paylen);

	return	STS$K_SUCCESS;
}


/*
**  Equal or not, in a time that does not depend on where they differ
*/
int	vbk$crp_equal		(
	const	uint8_t *	a_a,
	const	uint8_t *	a_b,
		size_t		a_len
			)
{
uint8_t	l_d = 0;

	for ( size_t i = 0; i < a_len; i++ )
		l_d |= a_a [i] ^ a_b [i];

	return	l_d == 0;
}


/*
**  Zero a secret so that the compiler cannot drop the stores as dead
*/
void	vbk$crp_wipe		(
		void *		a_p,
		size_t		a_len
			)
{
volatile uint8_t *	l_p = a_p;

	while ( a_len-- )
		*l_p++ = 0;
}
