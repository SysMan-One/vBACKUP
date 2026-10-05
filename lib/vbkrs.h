#ifndef	__VBKRS$H__
#define	__VBKRS$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKRS"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-15"
#endif

#ifndef	__REV__
#define	__REV__		"1.15.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkrs.h
**
**  ABSTRACT:	Reed-Solomon parity of a group of blocks (format.md 4.1):
**		GF(2^8), the coefficients of the scaled Cauchy matrix, the
**		encoding and the repair of up to VBK$K_MAXPAR lost blocks.
**
**  DESCRIPTION: No stdio, no StarLet, no threads: the utility, vbkx,
**		vbkx.exe and the WCX plugin link it alike.  VBK$RS_INIT makes
**		the tables; it is called before the first use, while one
**		thread runs (VBK$WRT_OPEN, VBK$RD_OPEN).
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  5-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-15		 5-OCT-2026	RRL
**		VBK$RS_SIMD.
**
**	X01-14		 5-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdint.h>
#include	<stddef.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	VBK$K_MAXPAR	8			/* Parity blocks of a group at most (/PARITY)	*/
#define	VBK$K_HPARSZ	8			/* The header parity: recoff, paylen		*/

void	vbk$rs_init	(void);
uint8_t	vbk$rs_coef	(uint32_t a_row, uint32_t a_col);
const char *vbk$rs_simd	(void);
void	vbk$rs_muladd	(uint8_t *a_dst, const uint8_t *a_src, size_t a_len, uint8_t a_c);
int	vbk$rs_repair	(uint32_t a_n, uint32_t a_m, uint8_t * const *a_data, const uint8_t *a_dok,
			 const uint8_t * const *a_par, const uint8_t *a_pok, size_t a_len);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKRS$H__ */
