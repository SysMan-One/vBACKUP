#define	__MODULE__	"VBKPAR"
#define	__IDENT__	"X01-08"
#define	__REV__		"1.8.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkpar.c
**
**  ABSTRACT:	A fork-join pool for the encryption: the stripes of one
**		block sealed on several cores, the blocks of one group checked
**		and decrypted on several cores.
**
**  DESCRIPTION: VBK$PAR_INIT starts the workers and hands VBK$CRP_SETPAR
**		the runner; the cryptography (VBKCRP.C) then splits its work
**		through it.  The runner gives jobs 0..n-1 to the workers and to
**		the caller and returns when all are done: nothing outlives a
**		call, so the order of what is written and of what is reported
**		is the order of the caller, as without the pool.
**
**		Only the utility links this module.  vbkx, vbkx.exe and the WCX
**		plugin never call VBK$CRP_SETPAR: their cryptography runs in
**		the one thread they have, the same bytes.
**
**		VBACKUP_CTHREADS=n sets the number of threads (the caller
**		included), 1 - none; the cores by default, at most 8.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  5-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-08		 5-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>
#include	<pthread.h>
#include	<unistd.h>

#include	"vbkcrp.h"
#include	"vbkos.h"

#define	VBK$K_PARMAX	8			/* Threads at most, the caller included		*/

static	pthread_mutex_t	s_mtx = PTHREAD_MUTEX_INITIALIZER;
static	pthread_cond_t	s_cvwork = PTHREAD_COND_INITIALIZER, s_cvdone = PTHREAD_COND_INITIALIZER;
static	pthread_mutex_t	s_callmtx = PTHREAD_MUTEX_INITIALIZER;	/* One call at a time		*/
static	pthread_t	s_thr [VBK$K_PARMAX];
static	uint32_t	s_nthr;			/* Workers started, the caller not counted	*/

static	uint64_t	s_gen;			/* A new call: the workers wake			*/
static	VBK$PARFN	s_fn;
static	void *		s_arg;
static	uint32_t	s_n, s_next, s_left;	/* Jobs, the next to take, those not finished	*/


/*
**  Take jobs while there are any; the caller takes them too
*/
static	void	s_vbk$take	(void)
{
uint32_t	l_i;

	for ( ;; )
		{
		pthread_mutex_lock(&s_mtx);

		if ( s_next >= s_n )
			{
			pthread_mutex_unlock(&s_mtx);

			return;
			}

		l_i	= s_next++;
		pthread_mutex_unlock(&s_mtx);

		s_fn(s_arg, l_i);

		pthread_mutex_lock(&s_mtx);

		if ( !--s_left )
			pthread_cond_signal(&s_cvdone);

		pthread_mutex_unlock(&s_mtx);
		}
}

static	void *	s_vbk$worker	(
		void *		a_arg
			)
{
uint64_t	l_seen = 0;

	(void) a_arg;
	vbk$crp_inpool(1);

	for ( ;; )
		{
		pthread_mutex_lock(&s_mtx);

		while ( s_gen == l_seen )
			pthread_cond_wait(&s_cvwork, &s_mtx);

		l_seen	= s_gen;
		pthread_mutex_unlock(&s_mtx);

		s_vbk$take();
		}

	return	NULL;
}


/*
**  Run jobs 0..<a_n>-1 of <a_fn> on the workers and here; back when all are done
*/
static	void	s_vbk$run	(
		uint32_t	a_n,
		VBK$PARFN	a_fn,
		void *		a_arg
			)
{
	pthread_mutex_lock(&s_callmtx);

	pthread_mutex_lock(&s_mtx);
	s_fn	= a_fn;
	s_arg	= a_arg;
	s_n	= a_n;
	s_next	= 0;
	s_left	= a_n;
	s_gen++;
	pthread_cond_broadcast(&s_cvwork);
	pthread_mutex_unlock(&s_mtx);

	vbk$crp_inpool(1);
	s_vbk$take();
	vbk$crp_inpool(0);

	pthread_mutex_lock(&s_mtx);

	while ( s_left )
		pthread_cond_wait(&s_cvdone, &s_mtx);

	pthread_mutex_unlock(&s_mtx);

	pthread_mutex_unlock(&s_callmtx);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Start the pool and give it to the cryptography.  Of no effect with
**	one core or VBACKUP_CTHREADS=1: the work stays in the thread that
**	asks for it.
**
**  RETURN VALUE:
**	The number of threads that will share the work, the caller included.
**--
*/
uint32_t	vbk$par_init	(void)
{
const char *	l_env = getenv("VBACKUP_CTHREADS");
long		l_n = sysconf(_SC_NPROCESSORS_ONLN);

	/* Started once; a second call gives the same pool back to the cryptography */
	if ( s_nthr )
		{
		vbk$crp_setpar(s_vbk$run, s_nthr + 1);

		return	s_nthr + 1;
		}

	if ( l_env && *l_env )
		l_n	= strtol(l_env, NULL, 10);

	l_n	= (l_n < 1) ? 1 : (l_n > VBK$K_PARMAX) ? VBK$K_PARMAX : l_n;

	for ( s_nthr = 0; s_nthr < (uint32_t) (l_n - 1); s_nthr++ )
		if ( pthread_create(&s_thr [s_nthr], NULL, s_vbk$worker, NULL) )
			break;

	/* The workers are left to run until the image ends: they wait, and hold nothing */
	for ( uint32_t i = 0; i < s_nthr; i++ )
		pthread_detach(s_thr [i]);

	if ( s_nthr )
		vbk$crp_setpar(s_vbk$run, s_nthr + 1);

	return	s_nthr + 1;
}
