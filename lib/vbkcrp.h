#ifndef	__VBKCRP$H__
#define	__VBKCRP$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKCRP"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-06"
#endif

#ifndef	__REV__
#define	__REV__		"1.6.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkcrp.h
**
**  ABSTRACT:	The encryption of a saveset (format.md, 6.10): ChaCha20,
**		SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256 - written here from
**		their RFCs, no library - and the keys and the TAG of a block.
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

#include	<stddef.h>
#include	<stdint.h>

#include	"vbkfmt.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	VBK$K_KEYSZ	32			/* ChaCha20 key, HMAC key, SHA-256 digest	*/
#define	VBK$K_SALTSZ	32			/* SALT of the VHDR				*/
#define	VBK$K_TAGSZ	32			/* TAG at the end of an EDATA/ETRAILER payload	*/
#define	VBK$K_KDFITER	600000			/* Default PBKDF2 iterations			*/
#define	VBK$K_KDFMIN	1000			/* Fewest a reader accepts			*/
#define	VBK$K_PASSMAX	1024			/* Longest passphrase				*/

enum	{					/* CIPHER and KDF of the VHDR			*/
	VBK$K_CIPHER_CC20HS = 1,		/* ChaCha20 + HMAC-SHA256			*/
	VBK$K_KDF_PBKDF2 = 1			/* PBKDF2-HMAC-SHA256				*/
	};

typedef struct vbk_sha256_t
{
	uint32_t	h [8];
	uint8_t		buf [64];
	uint32_t	nbuf;
	uint64_t	total;
} VBK$SHA256;

typedef struct vbk_hmac_t			/* HMAC-SHA256 with its two pads done once	*/
{
	VBK$SHA256	inner, outer;
} VBK$HMAC;

typedef struct vbk_keys_t			/* The keys of one saveset			*/
{
	uint8_t		enc [VBK$K_KEYSZ];
	uint8_t		check [VBK$K_KEYSZ];
	VBK$HMAC	mac;
} VBK$KEYS;

void	vbk$sha256_init		(VBK$SHA256 *a_ctx);
void	vbk$sha256_update	(VBK$SHA256 *a_ctx, const void *a_data, size_t a_len);
void	vbk$sha256_final	(VBK$SHA256 *a_ctx, uint8_t a_dig [VBK$K_KEYSZ]);

void	vbk$hmac_init		(VBK$HMAC *a_ctx, const void *a_key, size_t a_klen);
void	vbk$hmac		(const VBK$HMAC *a_key, const void *a_data, size_t a_len, uint8_t a_mac [VBK$K_KEYSZ]);
void	vbk$pbkdf2		(const void *a_pass, size_t a_plen, const uint8_t *a_salt, size_t a_slen, uint32_t a_iter,
				uint8_t *a_out, size_t a_olen);
void	vbk$chacha20		(const uint8_t a_key [VBK$K_KEYSZ], const uint8_t a_nonce [12], uint32_t a_ctr,
				uint8_t *a_data, size_t a_len);

void	vbk$crp_derive		(VBK$KEYS *a_keys, const void *a_pass, size_t a_plen, const uint8_t a_salt [VBK$K_SALTSZ],
				uint32_t a_iter);
void	vbk$crp_tag		(const VBK$KEYS *a_keys, const VBK$BHDR *a_hdr, const uint8_t *a_ct, uint8_t a_tag [VBK$K_TAGSZ]);
void	vbk$crp_seal		(const VBK$KEYS *a_keys, const VBK$BHDR *a_hdr, uint8_t *a_pay, uint32_t a_psize);
int	vbk$crp_check		(const VBK$KEYS *a_keys, const VBK$BHDR *a_hdr, const uint8_t *a_pay, uint32_t a_psize);
void	vbk$crp_decrypt		(const VBK$KEYS *a_keys, const VBK$BHDR *a_hdr, uint8_t *a_pay);
int	vbk$crp_open		(const VBK$KEYS *a_keys, const VBK$BHDR *a_hdr, uint8_t *a_pay, uint32_t a_psize);
int	vbk$crp_equal		(const uint8_t *a_a, const uint8_t *a_b, size_t a_len);
void	vbk$crp_wipe		(void *a_p, size_t a_len);
int	vbk$crp_hw		(int a_on);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKCRP$H__ */
