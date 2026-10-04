#define	__MODULE__	"VBKHSH"
#define	__IDENT__	"X01-03"
#define	__REV__		"1.3.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkhsh.c
**
**  ABSTRACT:	A table of strings with a value each: the files of the
**		journal, the names of a catalog for /INCREMENTAL.
**
**  DESCRIPTION: Open addressing, linear probing, FNV-1a; the table grows
**		to twice its size at 70% fill.  The keys are copied, the
**		values are the caller's.  Nothing is ever removed one by one.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-03		 3-OCT-2026	RRL
**		VBK$HASH_SORTED: the entries in the order of their keys.
**
**	X01-02		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>

#include	"vbkdef.h"

#define	VBK$K_HASHINI	1024			/* First size of a table			*/


static	uint64_t	s_vbk$fnv	(
	const	char *		a_key
			)
{
uint64_t	l_h = 0xcbf29ce484222325ULL;

	for ( ; *a_key; a_key++ )
		l_h = (l_h ^ (uint8_t) *a_key) * 0x100000001b3ULL;

	return	l_h;
}


/*
**  The slot of a key: where it is, or the empty one where it would go
*/
static	VBK$HENT *	s_vbk$slot	(
		VBK$HENT *	a_ent,
		size_t		a_sz,
	const	char *		a_key
			)
{
size_t	l_i = (size_t) s_vbk$fnv(a_key) & (a_sz - 1);

	while ( a_ent [l_i].key && strcmp(a_ent [l_i].key, a_key) )
		l_i = (l_i + 1) & (a_sz - 1);

	return	&a_ent [l_i];
}


static	int	s_vbk$grow	(
		VBK$HASH *	a_h
			)
{
size_t		l_sz = a_h->sz ? (a_h->sz * 2) : VBK$K_HASHINI;
VBK$HENT *	l_ent;

	if ( !(l_ent = calloc(l_sz, sizeof(VBK$HENT))) )
		return	STS$K_FATAL;

	for ( size_t i = 0; i < a_h->sz; i++ )
		if ( a_h->ent [i].key )
			*s_vbk$slot(l_ent, l_sz, a_h->ent [i].key) = a_h->ent [i];

	free(a_h->ent);

	a_h->ent	= l_ent;
	a_h->sz		= l_sz;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Put a key into the table, or give an existing one a new value.
**
**  FORMAL PARAMETERS:
**
**	a_h		The table, zeroed before the first use
**	a_key		The key, copied
**	a_val		The value
**	a_old		Receives the value the key had, NULL - the key is new
**			(may be NULL itself - not wanted)
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_FATAL - no memory.
**--
*/
int	vbk$hash_put	(
		VBK$HASH *	a_h,
	const	char *		a_key,
		void *		a_val,
		void **		a_old
			)
{
VBK$HENT *	l_e;

	if ( ((a_h->cnt + 1) * 10) > (a_h->sz * 7) )
		if ( !(1 & s_vbk$grow(a_h)) )
			return	STS$K_FATAL;

	l_e	= s_vbk$slot(a_h->ent, a_h->sz, a_key);

	if ( a_old )
		*a_old	= l_e->key ? l_e->val : NULL;

	if ( !l_e->key )
		{
		if ( !(l_e->key = strdup(a_key)) )
			return	STS$K_FATAL;

		a_h->cnt++;
		}

	l_e->val	= a_val;

	return	STS$K_SUCCESS;
}


/*
**  The value of a key; NULL - not there (or there with a NULL value)
*/
void *	vbk$hash_get	(
	const	VBK$HASH *	a_h,
	const	char *		a_key
			)
{
VBK$HENT *	l_e;

	if ( !a_h->sz )
		return	NULL;

	l_e	= s_vbk$slot(a_h->ent, a_h->sz, a_key);

	return	l_e->key ? l_e->val : NULL;
}


void	vbk$hash_free	(
		VBK$HASH *	a_h,
		int		a_freeval
			)
{
	for ( size_t i = 0; i < a_h->sz; i++ )
		if ( a_h->ent [i].key )
			{
			free(a_h->ent [i].key);

			if ( a_freeval )
				free(a_h->ent [i].val);
			}

	free(a_h->ent);
	memset(a_h, 0, sizeof(*a_h));
}


static	int	s_vbk$cmpent	(
	const	void *		a_a,
	const	void *		a_b
			)
{
	return	strcmp((*(const VBK$HENT * const *) a_a)->key, (*(const VBK$HENT * const *) a_b)->key);
}


/*
**  The entries in the order of the bytes of their keys - not in the one
**  of the slots, which depends on the order they came in: what is written
**  or done from a table is so the same for the same contents.  The array
**  is the caller's to free; NULL - the table is empty, or no memory.
*/
const VBK$HENT **	vbk$hash_sorted	(
	const	VBK$HASH *	a_h
			)
{
const VBK$HENT **	l_v;
size_t			l_n = 0;

	if ( !a_h->cnt || !(l_v = malloc(a_h->cnt * sizeof(*l_v))) )
		return	NULL;

	for ( size_t i = 0; i < a_h->sz; i++ )
		if ( a_h->ent [i].key )
			l_v [l_n++] = &a_h->ent [i];

	qsort(l_v, l_n, sizeof(*l_v), s_vbk$cmpent);

	return	l_v;
}
