#define	__MODULE__	"VBKPRE"
#define	__IDENT__	"X01-03"
#define	__REV__		"1.3.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkpre.c
**
**  ABSTRACT:	The read-ahead of files: while a file is saved (or copied),
**		threads of their own open the next files of the walk and
**		read their heads, so that the save finds them in the cache.
**
**  DESCRIPTION: A save of many small files waits on the device for each
**		of them - the inode, the open, the first block - one after
**		the other.  On a disk with a short queue, and on NFS where
**		every step is a round trip, that waiting is most of the time.
**
**		The walk hands every directory it lists over (VBK$PRE_PUSH)
**		before it goes through it, and takes it back (VBK$PRE_POP)
**		when it is done with it.  The workers take the files of the
**		newest directory first - the walk is depth first, so the files
**		of a subdirectory come before the rest of its parent - and do
**		nothing but system calls: lstat, open, read into a buffer of
**		their own, close.  The save itself goes on as before, in its
**		own order, and simply finds the pages there.  So nothing of
**		the saveset depends on the read-ahead, and a file that changes
**		in between is read by the save as it then is.
**
**		One thing the save must know from here: whether a file was
**		"cold" before it was read (VBK$OS_COLD) - a worker that has
**		read it has made it warm.  So the worker judges that before
**		its read, and the save asks for the verdict (VBK$PRE_TAKE).
**		A file no worker has started is taken away from them there,
**		and judged by the save itself.
**
**		How far ahead: VBK$K_PREMAX files or VBK$K_PREBYTES octets
**		read and not yet saved; of a file only its first VBK$K_PRECAP
**		octets - the read-ahead of the kernel carries the rest once
**		the save reads it.  Under a time filter most files are only
**		listed (PRESENT), so the workers look the files up and read
**		nothing.
**
**		VBACKUP_PREFETCH=n sets the number of workers, 0 - none.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-03		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<pthread.h>
#include	<sys/stat.h>
#include	<sys/uio.h>

#include	"vbkdef.h"

#define	VBK$K_PREDEF	8			/* Workers by default				*/
#define	VBK$K_PREMAXTHR	64			/* ... at most					*/
#define	VBK$K_PREMAX	128			/* Files read ahead and not yet saved		*/
#define	VBK$K_PREBYTES	(64 * 1048576)		/* ... octets of them				*/
#define	VBK$K_PRECAP	1048576			/* Octets of one file read ahead		*/

enum	{					/* The state of a file				*/
	VBK$K_PS_QUEUED = 0,			/* Nobody has touched it			*/
	VBK$K_PS_BUSY,				/* A worker reads it				*/
	VBK$K_PS_DONE,				/* Read, the verdict is there			*/
	VBK$K_PS_GONE				/* Taken by the save, or passed by the walk	*/
	};

typedef struct vbk_pitem_t
{
	char *		path;
	uint32_t	bytes;			/* Read ahead					*/
	uint8_t		state;			/* VBK$K_PS_*					*/
	int8_t		cold;			/* -1 - not known				*/
} VBK$PITEM;

typedef struct vbk_pgrp_t			/* The files of one directory, in walk order	*/
{
	VBK$PITEM *	it;
	uint32_t	n;
	uint32_t	cursor;			/* Those before it have been passed		*/
	char *		strs;			/* The names, all in one allocation		*/
} VBK$PGRP;

typedef struct vbk_pre_t
{
	pthread_mutex_t	mtx;
	pthread_cond_t	cvwork;			/* Something to read, or room to read it	*/
	pthread_cond_t	cvdone;			/* A file has been read				*/

	VBK$PGRP *	stk [VBACKUP$K_MAXDEPTH + 2];	/* The directories of the walk, newest last */
	uint32_t	ngrp;

	uint32_t	ahead;			/* Files BUSY or DONE				*/
	uint64_t	aheadbytes;		/* ... octets read of them			*/
	int		nodata;			/* A time filter: look up, do not read		*/
	int		stop;

	unsigned	nthr;
	pthread_t	thr [VBK$K_PREMAXTHR];
} VBK$PRE;

static	VBK$PGRP	s_vbk$empty;		/* Pushed when there is no memory for a group	*/


/*
**  Read the head of one file.  Runs in a worker: system calls only.
**
**  RETURN VALUE:
**	1 - it was cold, 0 - warm, -1 - not known; <*a_bytes> - read.
*/
static	int	s_vbk$prefile	(
		VBK$PRE *	a_pre,
	const	char *		a_path,
		uint8_t *	a_buf,
		uint32_t *	a_bytes
			)
{
struct stat	l_st;
struct iovec	l_iov;
ssize_t		l_n;
size_t		l_len, l_got = 0, l_pg, l_one;
int		l_fd, l_cold = -1, l_oflags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;

	*a_bytes = 0;

	if ( lstat(a_path, &l_st) || !S_ISREG(l_st.st_mode) || a_pre->nodata || !l_st.st_size )
		return	-1;

	if ( (0 > (l_fd = open(a_path, l_oflags | O_NOATIME))) && (errno == EPERM) )
		l_fd	= open(a_path, l_oflags);

	if ( l_fd < 0 )
		return	-1;

	l_len	= ((uint64_t) l_st.st_size < VBK$K_PRECAP) ? (size_t) l_st.st_size : VBK$K_PRECAP;

	/*
	**  The verdict first, and without a mapping (a mapping per file in
	**  several threads costs more than it tells): a read of the first
	**  page that must not wait.  Not there - cold.  There, and the last
	**  page of the head too - warm, nothing to read: copying what is in
	**  the cache would only take the memory bus from the save.  A file
	**  system that cannot say: the mapping.
	*/
	l_pg	= (size_t) sysconf(_SC_PAGESIZE);
	l_one	= (l_len < l_pg) ? l_len : l_pg;

	l_iov.iov_base	= a_buf;
	l_iov.iov_len	= l_one;

	if ( 0 <= (l_n = preadv2(l_fd, &l_iov, 1, 0, RWF_NOWAIT)) )
		{
		l_cold	= 0;

		if ( (size_t) l_n == l_one )
			{
			off_t	l_last = (off_t) ((l_len - 1) & ~(l_pg - 1));

			l_iov.iov_len	= 1;

			if ( !l_last || (1 == preadv2(l_fd, &l_iov, 1, l_last, RWF_NOWAIT)) )
				l_len	= 0;
			}
		}
	else if ( errno == EAGAIN )
		l_cold	= 1;
	else	l_cold	= vbk$os_cold(l_fd, (uint64_t) l_st.st_size);

	while ( l_got < l_len )
		{
		if ( 0 >= (l_n = pread(l_fd, a_buf + l_got, l_len - l_got, (off_t) l_got)) )
			{
			if ( (l_n < 0) && (errno == EINTR) )
				continue;

			break;
			}

		l_got	+= (size_t) l_n;
		}

	close(l_fd);

	*a_bytes = (uint32_t) l_got;

	return	l_cold;
}


/*
**  The next file to read ahead: the first nobody has touched in the
**  newest directory that has one, while there is room ahead
*/
static	VBK$PITEM *	s_vbk$pick	(
		VBK$PRE *	a_pre
			)
{
	if ( (a_pre->ahead >= VBK$K_PREMAX) || (a_pre->aheadbytes >= VBK$K_PREBYTES) )
		return	NULL;

	for ( uint32_t g = a_pre->ngrp; g--; )
		{
		VBK$PGRP *	l_g = a_pre->stk [g];

		for ( uint32_t i = l_g->cursor; i < l_g->n; i++ )
			if ( l_g->it [i].state == VBK$K_PS_QUEUED )
				return	&l_g->it [i];
		}

	return	NULL;
}


static	void *	s_vbk$worker	(
		void *		a_arg
			)
{
VBK$PRE *	l_pre = (VBK$PRE *) a_arg;
VBK$PITEM *	l_it;
uint8_t *	l_buf;
uint32_t	l_bytes;
int		l_cold;

	if ( !(l_buf = malloc(VBK$K_PRECAP)) )
		return	NULL;

	pthread_mutex_lock(&l_pre->mtx);

	for ( ;; )
		{
		while ( !l_pre->stop && !(l_it = s_vbk$pick(l_pre)) )
			pthread_cond_wait(&l_pre->cvwork, &l_pre->mtx);

		if ( l_pre->stop )
			break;

		l_it->state	= VBK$K_PS_BUSY;
		l_pre->ahead++;

		pthread_mutex_unlock(&l_pre->mtx);

		l_cold	= s_vbk$prefile(l_pre, l_it->path, l_buf, &l_bytes);

		pthread_mutex_lock(&l_pre->mtx);

		/* Passed by the walk while it was read: of no use, no room taken */
		if ( l_it->state == VBK$K_PS_GONE )
			l_pre->ahead--;
		else	{
			l_it->state	= VBK$K_PS_DONE;
			l_it->cold	= (int8_t) l_cold;
			l_it->bytes	= l_bytes;
			l_pre->aheadbytes += l_bytes;
			}

		pthread_cond_broadcast(&l_pre->cvdone);
		}

	pthread_mutex_unlock(&l_pre->mtx);
	free(l_buf);

	return	NULL;
}


/*
**  A file no longer wanted: what it held ahead is given back.  A BUSY one
**  is marked, its worker gives the room back when it is through.
*/
static	void	s_vbk$pass	(
		VBK$PRE *	a_pre,
		VBK$PITEM *	a_it
			)
{
	if ( a_it->state == VBK$K_PS_DONE )
		{
		a_pre->ahead--;
		a_pre->aheadbytes -= a_it->bytes;
		}

	a_it->state	= VBK$K_PS_GONE;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Start the read-ahead for a save or a copy: the workers are made,
**	OPTS->PRE points to it.  No workers wanted, or none to be had -
**	OPTS->PRE stays NULL and everything else works as without.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command
**
**  RETURN VALUE:
**	None.
**--
*/
void	vbk$pre_start	(
		VBK$OPTS *	a_opts
			)
{
VBK$PRE *	l_pre;
const char *	l_env = getenv("VBACKUP_PREFETCH");
long		l_n = VBK$K_PREDEF;

	a_opts->pre	= NULL;

	if ( l_env && *l_env )
		l_n	= strtol(l_env, NULL, 10);

	if ( l_n <= 0 )
		return;

	if ( l_n > VBK$K_PREMAXTHR )
		l_n	= VBK$K_PREMAXTHR;

	if ( !(l_pre = calloc(1, sizeof(VBK$PRE))) )
		return;

	l_pre->nodata	= a_opts->timefilter;

	pthread_mutex_init(&l_pre->mtx, NULL);
	pthread_cond_init(&l_pre->cvwork, NULL);
	pthread_cond_init(&l_pre->cvdone, NULL);

	for ( ; l_pre->nthr < (unsigned) l_n; l_pre->nthr++ )
		if ( pthread_create(&l_pre->thr [l_pre->nthr], NULL, s_vbk$worker, l_pre) )
			break;

	if ( !l_pre->nthr )
		{
		pthread_cond_destroy(&l_pre->cvdone);
		pthread_cond_destroy(&l_pre->cvwork);
		pthread_mutex_destroy(&l_pre->mtx);
		free(l_pre);

		return;
		}

	a_opts->pre	= l_pre;
}


/*
**  Stop the read-ahead: the workers are gone before anything is freed
*/
void	vbk$pre_stop	(
		VBK$OPTS *	a_opts
			)
{
VBK$PRE *	l_pre = a_opts->pre;

	if ( !l_pre )
		return;

	pthread_mutex_lock(&l_pre->mtx);
	l_pre->stop	= 1;
	pthread_cond_broadcast(&l_pre->cvwork);
	pthread_mutex_unlock(&l_pre->mtx);

	for ( unsigned i = 0; i < l_pre->nthr; i++ )
		pthread_join(l_pre->thr [i], NULL);

	for ( uint32_t g = 0; g < l_pre->ngrp; g++ )
		if ( l_pre->stk [g] != &s_vbk$empty )
			{
			free(l_pre->stk [g]->it);
			free(l_pre->stk [g]->strs);
			free(l_pre->stk [g]);
			}

	pthread_cond_destroy(&l_pre->cvdone);
	pthread_cond_destroy(&l_pre->cvwork);
	pthread_mutex_destroy(&l_pre->mtx);
	free(l_pre);

	a_opts->pre	= NULL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The walk is about to go through a directory: its files, in the
**	order the walk will take them, are there to be read ahead.  Every
**	PUSH is matched by a POP, an empty list included.
**
**  FORMAL PARAMETERS:
**
**	a_pre		The read-ahead, NULL - none
**	a_dir		The specification of the directory
**	a_names		The names of the files in it
**	a_n		How many
**
**  RETURN VALUE:
**	None.  No memory: the directory is pushed empty.
**--
*/
void	vbk$pre_push	(
	struct	vbk_pre_t *	a_pre,
	const	char *		a_dir,
	const	char * const *	a_names,
		uint32_t	a_n
			)
{
VBK$PGRP *	l_g;
size_t		l_dlen = strlen(a_dir), l_sz = 0, l_off = 0;
int		l_sep = l_dlen && (a_dir [l_dlen - 1] != '/');

	if ( !a_pre )
		return;

	for ( uint32_t i = 0; i < a_n; i++ )
		l_sz	+= l_dlen + (size_t) l_sep + strlen(a_names [i]) + 1;

	if ( (l_g = calloc(1, sizeof(VBK$PGRP))) && a_n && (l_g->it = calloc(a_n, sizeof(VBK$PITEM))) && (l_g->strs = malloc(l_sz)) )
		{
		for ( uint32_t i = 0; i < a_n; i++ )
			{
			size_t	l_nlen = strlen(a_names [i]);

			l_g->it [i].path = l_g->strs + l_off;
			l_g->it [i].cold = -1;

			memcpy(l_g->strs + l_off, a_dir, l_dlen);
			l_off	+= l_dlen;

			if ( l_sep )
				l_g->strs [l_off++] = '/';

			memcpy(l_g->strs + l_off, a_names [i], l_nlen + 1);
			l_off	+= l_nlen + 1;
			}

		l_g->n	= a_n;
		}
	else if ( l_g && a_n )
		{
		/* No memory for the list: pushed empty all the same, so that the POP finds it */
		free(l_g->it);
		free(l_g);
		l_g	= NULL;
		}

	pthread_mutex_lock(&a_pre->mtx);

	/* Deeper than the walk ever goes cannot be; were it so, the POP would match nothing */
	if ( a_pre->ngrp < (sizeof(a_pre->stk) / sizeof(a_pre->stk [0])) )
		a_pre->stk [a_pre->ngrp++] = l_g ? l_g : &s_vbk$empty;
	else if ( l_g )
		{
		free(l_g->it);
		free(l_g->strs);
		free(l_g);
		}

	pthread_cond_broadcast(&a_pre->cvwork);
	pthread_mutex_unlock(&a_pre->mtx);
}


/*
**  The walk is through with the newest directory: it goes, once no worker
**  reads a file of it any more
*/
void	vbk$pre_pop	(
	struct	vbk_pre_t *	a_pre
			)
{
VBK$PGRP *	l_g;
int		l_busy;

	if ( !a_pre )
		return;

	pthread_mutex_lock(&a_pre->mtx);

	if ( !a_pre->ngrp )
		{
		pthread_mutex_unlock(&a_pre->mtx);

		return;
		}

	l_g	= a_pre->stk [a_pre->ngrp - 1];

	for ( uint32_t i = 0; i < l_g->n; i++ )
		if ( l_g->it [i].state != VBK$K_PS_BUSY )
			s_vbk$pass(a_pre, &l_g->it [i]);

	for ( ;; )
		{
		l_busy	= 0;

		for ( uint32_t i = 0; i < l_g->n; i++ )
			if ( l_g->it [i].state == VBK$K_PS_BUSY )
				l_busy	= 1;

		if ( !l_busy )
			break;

		pthread_cond_wait(&a_pre->cvdone, &a_pre->mtx);
		}

	/* A file that came DONE while the others were waited for */
	for ( uint32_t i = 0; i < l_g->n; i++ )
		s_vbk$pass(a_pre, &l_g->it [i]);

	a_pre->ngrp--;

	pthread_cond_broadcast(&a_pre->cvwork);
	pthread_mutex_unlock(&a_pre->mtx);

	if ( l_g != &s_vbk$empty )
		{
		free(l_g->it);
		free(l_g->strs);
		free(l_g);
		}
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The save is about to read a file: whether it was cold before the
**	read-ahead touched it.  The files of its directory before it are
**	passed - they will not be read any more.  A file a worker reads is
**	waited for; one nobody has started is taken away from the workers.
**
**  FORMAL PARAMETERS:
**
**	a_pre		The read-ahead, NULL - none
**	a_path		The specification of the file, as the walk made it
**
**  RETURN VALUE:
**	1 - cold, 0 - warm, -1 - not known: the caller judges it itself.
**--
*/
int	vbk$pre_take	(
	struct	vbk_pre_t *	a_pre,
	const	char *		a_path
			)
{
VBK$PGRP *	l_g;
VBK$PITEM *	l_it;
uint32_t	l_i;
int		l_cold = -1;

	if ( !a_pre )
		return	-1;

	pthread_mutex_lock(&a_pre->mtx);

	if ( !a_pre->ngrp )
		{
		pthread_mutex_unlock(&a_pre->mtx);

		return	-1;
		}

	l_g	= a_pre->stk [a_pre->ngrp - 1];

	for ( l_i = l_g->cursor; (l_i < l_g->n) && strcmp(l_g->it [l_i].path, a_path); l_i++ )
		;

	if ( l_i >= l_g->n )
		{
		pthread_mutex_unlock(&a_pre->mtx);

		return	-1;
		}

	for ( uint32_t j = l_g->cursor; j < l_i; j++ )
		if ( l_g->it [j].state != VBK$K_PS_GONE )
			s_vbk$pass(a_pre, &l_g->it [j]);

	l_it		= &l_g->it [l_i];
	l_g->cursor	= l_i + 1;

	while ( l_it->state == VBK$K_PS_BUSY )
		pthread_cond_wait(&a_pre->cvdone, &a_pre->mtx);

	if ( l_it->state == VBK$K_PS_DONE )
		l_cold	= l_it->cold;

	s_vbk$pass(a_pre, l_it);

	pthread_cond_broadcast(&a_pre->cvwork);
	pthread_mutex_unlock(&a_pre->mtx);

	return	l_cold;
}
