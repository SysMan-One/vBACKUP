#define	__MODULE__	"VBKWLK"
#define	__IDENT__	"X01-07"
#define	__REV__		"1.7.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkwlk.c
**
**  ABSTRACT:	The walk of the input specifications of a save: the base
**		of a specification, its wildcards, the "..." recursion and
**		the filters /SELECT, /EXCLUDE, /SINCE, /BEFORE, /BY_OWNER,
**		/[NO]CROSS_DEVICE and the nodump flag.
**
**  DESCRIPTION: The base of a specification is the part of it before
**		the first wildcard or "..." - or, without any, the directory
**		the named file or directory lives in.  A stored name is the
**		path relative to the base, so a directory given by name is
**		stored with its own name in front:
**
**		    /home/rrl		base /home, names rrl, rrl/...
**		    /home/rrl/src/.../x*.c  base /home/rrl/src, names x.c, a/xy.c
**		    /etc/h*.conf	base /etc, names host.conf, hosts.conf
**
**		A wildcard matches one component; "..." stands for any
**		number of directory levels, none included.  A directory that
**		a specification selects is taken whole, as tar and cp -r do.
**		The entries of a directory are walked in the order of their
**		names (bytes, not the locale), so two saves of one tree make
**		the same saveset.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-07		 5-OCT-2026	RRL
**		Names made by FAO.
**
**	X01-04		 4-OCT-2026	RRL
**		"dir/." is the contents of dir under their own names (/IMAGE).
**
**	X01-03		 3-OCT-2026	RRL
**		The files of a directory are handed to the read-ahead before
**		the directory is gone through (VBK$PRE_PUSH, VBK$PRE_POP).
**
**	X01-02		 3-OCT-2026	RRL
**		Covered and saved apart (format.md, 6.6): a file the time filters
**		- /SINCE, /BEFORE, /SINCE=BACKUP - do not choose is handed over
**		as PRESENT rather than left out, so that an incremental catalog
**		describes the whole tree.  Every entry carries its absolute name,
**		the key of the journal.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<dirent.h>
#include	<sys/stat.h>
#include	<sys/ioctl.h>
#include	<linux/fs.h>

#include	"vbkdef.h"

#define	VBK$K_MAXCOMP	64			/* Components of a wildcard pattern		*/

/*
**  The state of one walk
*/
typedef struct vbk_walk_t
{
	const VBK$OPTS *opts;
	uint16_t	baseidx;
	dev_t		rootdev;		/* Device of the base: /NOCROSS_DEVICE		*/
	int		(*rtn) (const VBK$ENT *a_ent, void *a_arg);
	void *		arg;
	unsigned	nemitted;
	char		absbase [VBACKUP$K_SZ_PATH];	/* realpath of the base: the journal key	*/
	char *		comp [VBK$K_MAXCOMP];	/* The pattern, split				*/
	unsigned	ncomp;
} VBK$WALK;

static	int	s_vbk$tree	(VBK$WALK *a_w, char *a_path, size_t a_plen, char *a_rel, size_t a_rlen, unsigned a_depth);


static	int	s_vbk$iswild	(
	const	char *		a_s,
		size_t		a_len
			)
{
	for ( size_t i = 0; i < a_len; i++ )
		if ( (a_s [i] == '*') || (a_s [i] == '%') || (a_s [i] == '?') )
			return	1;

	return	(a_len == 3) && !memcmp(a_s, "...", 3);
}


/*
**  Where the wildcard part of a specification begins: the offset of the
**  first component that has a wildcard or is "...", or the length of the
**  specification when none has
*/
static	size_t	s_vbk$wildpos	(
	const	char *		a_spec
			)
{
size_t	l_i = 0, l_j, l_len = strlen(a_spec);

	while ( l_i < l_len )
		{
		for ( l_j = l_i; (l_j < l_len) && (a_spec [l_j] != '/'); l_j++ )
			;

		if ( s_vbk$iswild(a_spec + l_i, l_j - l_i) )
			return	l_i;

		l_i	= l_j + 1;
		}

	return	l_len;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The base of an input specification, see the module description.
**
**  FORMAL PARAMETERS:
**
**	a_spec		The specification
**	a_base		Receives the base
**	a_basesz	Size of <a_base>
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- a plain name: the base is its directory;
**	STS$K_INFO	- a wildcard specification;
**	STS$K_WARN	- a directory standing for its own contents ("/", ".").
**--
*/
int	vbk$base	(
	const	char *		a_spec,
		char *		a_base,
		size_t		a_basesz
			)
{
size_t	l_wp = s_vbk$wildpos(a_spec), l_len = strlen(a_spec);
char	l_tmp [VBACKUP$K_SZ_PATH];
char *	l_slash;

	if ( l_wp < l_len )
		{
		/* Up to the slash before the wildcard component; none - the current directory */
		if ( !l_wp )
			return	vbk$strcpy(a_basesz, a_base, "."), STS$K_INFO;

		$VBKFAOB(l_tmp, sizeof(l_tmp), "!AD", (l_wp > 1) ? (l_wp - 1) : 1, a_spec);
		vbk$strcpy(a_basesz, a_base, l_tmp);

		return	STS$K_INFO;
		}

	vbk$strcpy(sizeof(l_tmp), l_tmp, a_spec);

	/* Trailing slashes say nothing */
	for ( l_len = strlen(l_tmp); (l_len > 1) && (l_tmp [l_len - 1] == '/'); l_len-- )
		l_tmp [l_len - 1] = '\0';

	/* "dir/." too: what the directory holds, under names of their own - /IMAGE gives the mount point so */
	if ( !strcmp(l_tmp, "/") || !strcmp(l_tmp, ".") || !strcmp(l_tmp, "..") || ((l_len >= 3) && !strcmp(l_tmp + l_len - 3, "/.."))
		|| ((l_len >= 2) && !strcmp(l_tmp + l_len - 2, "/.")) )
		{
		vbk$strcpy(a_basesz, a_base, l_tmp);

		return	STS$K_WARN;
		}

	if ( !(l_slash = strrchr(l_tmp, '/')) )
		vbk$strcpy(a_basesz, a_base, ".");
	else if ( l_slash == l_tmp )
		vbk$strcpy(a_basesz, a_base, "/");
	else	{
		*l_slash = '\0';
		vbk$strcpy(a_basesz, a_base, l_tmp);
		}

	return	STS$K_SUCCESS;
}


/*
**  Append "/name" to a path held in a buffer of VBACKUP$K_SZ_PATH; the
**  old length is to be put back by the caller
*/
static	int	s_vbk$append	(
		char *		a_buf,
		size_t		a_len,
	const	char *		a_name,
		size_t *	a_newlen
			)
{
size_t	l_nlen = strlen(a_name);
int	l_sep = a_len && (a_buf [a_len - 1] != '/');

	if ( (a_len + (size_t) l_sep + l_nlen + 1) > VBACKUP$K_SZ_PATH )
		return	STS$K_ERROR;

	if ( l_sep )
		a_buf [a_len++] = '/';

	memcpy(a_buf + a_len, a_name, l_nlen + 1);
	*a_newlen = a_len + l_nlen;

	return	STS$K_SUCCESS;
}


/*
**  The entries of a directory sorted by the bytes of their names
*/
static	int	s_vbk$cmpent	(const struct dirent **a_a, const struct dirent **a_b)
{
	return	strcmp((*a_a)->d_name, (*a_b)->d_name);
}

static	int	s_vbk$noddot	(const struct dirent *a_d)
{
	return	strcmp(a_d->d_name, ".") && strcmp(a_d->d_name, "..");
}


/*
**  Hand the files of a listed directory to the read-ahead, in the order
**  they will be walked: those that may be regular files (the type, when
**  the file system tells it) and that the name filters let through.
**  Every call is matched by a VBK$PRE_POP, the read-ahead or not.
*/
static	void	s_vbk$prepush	(
		VBK$WALK *	a_w,
	const	char *		a_path,
		char *		a_rel,
		size_t		a_rlen,
		struct dirent **a_list,
		int		a_n
			)
{
const VBK$OPTS *l_o = a_w->opts;
const char **	l_names;
uint32_t	l_cnt = 0;
size_t		l_nr;

	if ( !l_o->pre )
		return;

	if ( (l_names = malloc(((size_t) a_n + 1) * sizeof(char *))) )
		{
		for ( int i = 0; i < a_n; i++ )
			{
			if ( (a_list [i]->d_type != DT_REG) && (a_list [i]->d_type != DT_UNKNOWN) )
				continue;

			if ( !(1 & s_vbk$append(a_rel, a_rlen, a_list [i]->d_name, &l_nr)) )
				continue;

			if ( !(l_o->nexclude && (1 & vbk$match(a_rel, l_o->exclude, l_o->nexclude)))
				&& !(l_o->nselect && !(1 & vbk$match(a_rel, l_o->select, l_o->nselect))) )
				l_names [l_cnt++] = a_list [i]->d_name;

			a_rel [a_rlen] = '\0';
			}
		}

	vbk$pre_push(l_o->pre, a_path, l_names, l_cnt);
	free(l_names);
}


/*
**  Hand one entry to the action routine, after the filters that apply to
**  files.  Directories are always handed over: the tree needs them.
*/
static	int	s_vbk$emit	(
		VBK$WALK *	a_w,
	const	char *		a_path,
	const	char *		a_rel,
	const	struct stat *	a_st
			)
{
const VBK$OPTS *l_o = a_w->opts;
char		l_abs [VBACKUP$K_SZ_PATH];
VBK$ENT		l_ent = { .path = a_path, .name = a_rel, .abspath = l_abs, .baseidx = a_w->baseidx };

	if ( $VBKFAOB(l_abs, sizeof(l_abs), "!AZ!AZ!AZ", a_w->absbase, strcmp(a_w->absbase, "/") ? "/" : "", a_rel) >= (int) sizeof(l_abs) )
		return	$VBKMSG(VBACKUP$_OPENIN, a_path, ENAMETOOLONG, strerror(ENAMETOOLONG)), STS$K_SUCCESS;

	if ( !S_ISDIR(a_st->st_mode) )
		{
		/* The name filters and the owner: what is not covered is not handed over at all */
		if ( l_o->nselect && !(1 & vbk$match(a_rel, l_o->select, l_o->nselect)) )
			return	STS$K_SUCCESS;

		if ( l_o->hasowner && (a_st->st_uid != l_o->byowner) )
			return	STS$K_SUCCESS;

		/* The time filters: what they do not choose is covered all the same - PRESENT */
		if ( l_o->timefilter )
			{
			struct statx	l_stx;
			fao_time_t	l_t;
			int		l_chosen = 1;

			if ( statx(AT_FDCWD, a_path, AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_BTIME, &l_stx) )
				return	STS$K_SUCCESS;

			switch ( l_o->timsrc )
				{
				case	VBACKUP$K_TIM_CREATED:
					/* No creation time on this file system: no time to choose it by */
					l_t	= (l_stx.stx_mask & STATX_BTIME) ? (fao_time_t) l_stx.stx_btime.tv_sec : 0;

					if ( !(l_stx.stx_mask & STATX_BTIME) && (l_o->hassince || l_o->hasbefore) )
						l_chosen = 0;
					break;

				case	VBACKUP$K_TIM_CHANGED:
					l_t	= (fao_time_t) l_stx.stx_ctime.tv_sec;
					break;

				default:
					l_t	= (fao_time_t) l_stx.stx_mtime.tv_sec;
				}

			if ( l_o->hassince && (l_t < l_o->since) )
				l_chosen = 0;

			if ( l_o->hasbefore && (l_t >= l_o->before) )
				l_chosen = 0;

			if ( l_o->sincebackup && l_o->jnl && !vbk$jnl_changed(l_o->jnl, l_abs, &l_stx) )
				l_chosen = 0;

			l_ent.present = !l_chosen;
			}
		}

	a_w->nemitted++;

	return	a_w->rtn(&l_ent, a_w->arg);
}


/*
**  Is the directory flagged nodump (chattr +d)?  Then, unless
**  /IGNORE=NOBACKUP, it is left out with all it holds.
*/
static	int	s_vbk$nodump	(
		VBK$WALK *	a_w,
		int		a_fd
			)
{
int	l_flags = 0;

	if ( a_w->opts->nobackup )
		return	0;

	return	!ioctl(a_fd, FS_IOC_GETFLAGS, &l_flags) && (l_flags & FS_NODUMP_FL);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Hand over an entry and, when it is a directory, everything below
**	it.  The two buffers hold the specification on the disk and the
**	stored name, each with its current length, and are extended in
**	place on the way down.
**
**  FORMAL PARAMETERS:
**
**	a_w		The walk
**	a_path, a_plen	The specification of the entry, its length
**	a_rel, a_rlen	The stored name, its length
**	a_depth		Nesting so far
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_FATAL when the walk is to stop.
**--
*/
static	int	s_vbk$tree	(
		VBK$WALK *	a_w,
		char *		a_path,
		size_t		a_plen,
		char *		a_rel,
		size_t		a_rlen,
		unsigned	a_depth
			)
{
const VBK$OPTS *l_o = a_w->opts;
struct dirent **l_list = NULL;
struct stat	l_st;
size_t		l_plen, l_rlen;
int		l_n, l_fd, l_status;

	if ( lstat(a_path, &l_st) )
		return	$VBKMSG(VBACKUP$_OPENIN, a_path, errno, strerror(errno)), STS$K_SUCCESS;

	if ( l_o->nexclude && (1 & vbk$match(a_rel, l_o->exclude, l_o->nexclude)) )
		return	STS$K_SUCCESS;

	if ( !S_ISDIR(l_st.st_mode) )
		return	(STS$K_FATAL == (l_status = s_vbk$emit(a_w, a_path, a_rel, &l_st))) ? l_status : STS$K_SUCCESS;

	if ( 0 > (l_fd = open(a_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) )
		return	$VBKMSG(VBACKUP$_OPENDIR, a_path, errno, strerror(errno)), STS$K_SUCCESS;

	if ( s_vbk$nodump(a_w, l_fd) )
		{
		close(l_fd);

		if ( l_o->log )
			$VBKMSG(VBACKUP$_SKIPPED, a_rel, "nodump flag set");

		return	STS$K_SUCCESS;
		}

	close(l_fd);

	/* STS$K_WARN for a directory: the action routine does not want what it holds (a copy into it) */
	if ( STS$K_FATAL == (l_status = s_vbk$emit(a_w, a_path, a_rel, &l_st)) )
		return	l_status;

	if ( l_status == STS$K_WARN )
		return	STS$K_SUCCESS;

	/* A mount point is saved, what is mounted on it is not, unless /CROSS_DEVICE */
	if ( !l_o->crossdev && (l_st.st_dev != a_w->rootdev) )
		return	STS$K_SUCCESS;

	if ( a_depth >= VBACKUP$K_MAXDEPTH )
		return	$VBKMSG(VBACKUP$_TOODEEP, a_path, VBACKUP$K_MAXDEPTH), STS$K_SUCCESS;

	if ( 0 > (l_n = scandir(a_path, &l_list, s_vbk$noddot, s_vbk$cmpent)) )
		return	$VBKMSG(VBACKUP$_OPENDIR, a_path, errno, strerror(errno)), STS$K_SUCCESS;

	s_vbk$prepush(a_w, a_path, a_rel, a_rlen, l_list, l_n);

	for ( int i = 0; i < l_n; i++ )
		{
		if ( l_status != STS$K_FATAL )
			{
			if ( (1 & s_vbk$append(a_path, a_plen, l_list [i]->d_name, &l_plen)) && (1 & s_vbk$append(a_rel, a_rlen, l_list [i]->d_name, &l_rlen)) )
				l_status = s_vbk$tree(a_w, a_path, l_plen, a_rel, l_rlen, a_depth + 1);
			else	$VBKMSG(VBACKUP$_OPENIN, l_list [i]->d_name, ENAMETOOLONG, strerror(ENAMETOOLONG));
			}

		a_path [a_plen] = '\0';
		a_rel [a_rlen]	= '\0';
		free(l_list [i]);
		}

	free(l_list);

	if ( l_o->pre )
		vbk$pre_pop(l_o->pre);

	return	(l_status == STS$K_FATAL) ? STS$K_FATAL : STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Walk a directory against the components of a wildcard pattern,
**	from component <a_ci> on.
**
**  FORMAL PARAMETERS:
**
**	a_w		The walk
**	a_path, a_plen	The directory, its length
**	a_rel, a_rlen	Its stored name ("" for the base), its length
**	a_ci		Index of the component to match
**	a_depth		Nesting so far
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_FATAL when the walk is to stop.
**--
*/
static	int	s_vbk$pattern	(
		VBK$WALK *	a_w,
		char *		a_path,
		size_t		a_plen,
		char *		a_rel,
		size_t		a_rlen,
		unsigned	a_ci,
		unsigned	a_depth
			)
{
const char *	l_comp = a_w->comp [a_ci];
struct dirent **l_list = NULL;
struct stat	l_st;
size_t		l_plen, l_rlen;
int		l_n, l_isdots = !strcmp(l_comp, "..."), l_status = STS$K_SUCCESS;

	if ( a_depth >= VBACKUP$K_MAXDEPTH )
		return	$VBKMSG(VBACKUP$_TOODEEP, a_path, VBACKUP$K_MAXDEPTH), STS$K_SUCCESS;

	/* "..." as nothing: the rest of the pattern right here */
	if ( l_isdots )
		{
		if ( (a_ci + 1) >= a_w->ncomp )
			return	STS$K_SUCCESS;

		if ( STS$K_FATAL == s_vbk$pattern(a_w, a_path, a_plen, a_rel, a_rlen, a_ci + 1, a_depth) )
			return	STS$K_FATAL;
		}

	if ( 0 > (l_n = scandir(a_path, &l_list, s_vbk$noddot, s_vbk$cmpent)) )
		return	STS$K_SUCCESS;

	for ( int i = 0; i < l_n; i++ )
		{
		const char *	l_name = l_list [i]->d_name;

		if ( (l_status != STS$K_FATAL) && (1 & s_vbk$append(a_path, a_plen, l_name, &l_plen))
			&& (1 & s_vbk$append(a_rel, a_rlen, l_name, &l_rlen)) && !lstat(a_path, &l_st) )
			{
			if ( l_isdots )
				{
				/* "..." as one level more: the same component, one directory down */
				if ( S_ISDIR(l_st.st_mode) && (a_w->opts->crossdev || (l_st.st_dev == a_w->rootdev))
					&& !(a_w->opts->nexclude && (1 & vbk$match(a_rel, a_w->opts->exclude, a_w->opts->nexclude))) )
					l_status = s_vbk$pattern(a_w, a_path, l_plen, a_rel, l_rlen, a_ci, a_depth + 1);
				}
			else if ( __util$pattern_match((char *) l_name, (char *) l_comp) )
				{
				if ( (a_ci + 1) >= a_w->ncomp )
					l_status = s_vbk$tree(a_w, a_path, l_plen, a_rel, l_rlen, a_depth + 1);
				else if ( S_ISDIR(l_st.st_mode) )
					l_status = s_vbk$pattern(a_w, a_path, l_plen, a_rel, l_rlen, a_ci + 1, a_depth + 1);
				}
			}

		a_path [a_plen] = '\0';
		a_rel [a_rlen]	= '\0';
		free(l_list [i]);
		}

	free(l_list);

	return	(l_status == STS$K_FATAL) ? STS$K_FATAL : STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Walk one input specification and hand every selected entry to the
**	action routine.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command
**	a_spec		The specification
**	a_baseidx	Its index among the inputs
**	a_base		Receives its base
**	a_basesz	Size of <a_base>
**	a_rtn		Action routine; STS$K_FATAL from it stops the walk
**	a_arg		Its argument
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- something has been handed over;
**	STS$K_WARN	- the specification selected nothing;
**	STS$K_FATAL	- the walk was stopped.
**--
*/
int	vbk$walk	(
	const	VBK$OPTS *	a_opts,
	const	char *		a_spec,
		uint16_t	a_baseidx,
		char *		a_base,
		size_t		a_basesz,
		int		(*a_rtn) (const VBK$ENT *a_ent, void *a_arg),
		void *		a_arg
			)
{
VBK$WALK	l_w = { .opts = a_opts, .baseidx = a_baseidx, .rtn = a_rtn, .arg = a_arg };
char		l_path [VBACKUP$K_SZ_PATH], l_rel [VBACKUP$K_SZ_PATH], l_pat [VBACKUP$K_SZ_PATH];
struct stat	l_st;
struct dirent **l_list = NULL;
size_t		l_plen, l_rlen;
int		l_kind, l_status = STS$K_SUCCESS, l_n;

	l_kind	= vbk$base(a_spec, a_base, a_basesz);

	if ( stat(a_base, &l_st) )
		return	$VBKMSG(VBACKUP$_OPENIN, a_base, errno, strerror(errno)), STS$K_WARN;

	l_w.rootdev	= l_st.st_dev;

	if ( !realpath(a_base, l_w.absbase) )
		vbk$strcpy(sizeof(l_w.absbase), l_w.absbase, a_base);

	vbk$strcpy(sizeof(l_path), l_path, a_base);
	l_plen	= strlen(l_path);
	l_rel [0] = '\0';
	l_rlen	= 0;

	switch ( l_kind )
		{
		case	STS$K_INFO:
			/* The wildcard part - from the component the base stops before - split into its components */
			vbk$strcpy(sizeof(l_pat), l_pat, a_spec + s_vbk$wildpos(a_spec));

			for ( char *l_tok = strtok(l_pat, "/"); l_tok && (l_w.ncomp < VBK$K_MAXCOMP); l_tok = strtok(NULL, "/") )
				l_w.comp [l_w.ncomp++] = l_tok;

			if ( l_w.ncomp )
				l_status = s_vbk$pattern(&l_w, l_path, l_plen, l_rel, l_rlen, 0, 0);
			break;

		case	STS$K_WARN:
			/* "/" or ".": what the directory holds, under names of their own */
			if ( 0 > (l_n = scandir(l_path, &l_list, s_vbk$noddot, s_vbk$cmpent)) )
				return	$VBKMSG(VBACKUP$_OPENDIR, l_path, errno, strerror(errno)), STS$K_WARN;

			s_vbk$prepush(&l_w, l_path, l_rel, 0, l_list, l_n);

			for ( int i = 0; i < l_n; i++ )
				{
				size_t	l_np, l_nr;

				if ( (l_status != STS$K_FATAL) && (1 & s_vbk$append(l_path, l_plen, l_list [i]->d_name, &l_np))
					&& (1 & s_vbk$append(l_rel, 0, l_list [i]->d_name, &l_nr)) )
					l_status = s_vbk$tree(&l_w, l_path, l_np, l_rel, l_nr, 1);

				l_path [l_plen]	= '\0';
				free(l_list [i]);
				}

			free(l_list);

			if ( a_opts->pre )
				vbk$pre_pop(a_opts->pre);
			break;

		default:
			/* A plain name: the entry itself under its last component */
			{
			const char *	l_last;

			vbk$strcpy(sizeof(l_path), l_path, a_spec);

			for ( l_plen = strlen(l_path); (l_plen > 1) && (l_path [l_plen - 1] == '/'); l_plen-- )
				l_path [l_plen - 1] = '\0';

			l_last	= strrchr(l_path, '/') ? (strrchr(l_path, '/') + 1) : l_path;
			vbk$strcpy(sizeof(l_rel), l_rel, l_last);

			l_status = s_vbk$tree(&l_w, l_path, l_plen, l_rel, strlen(l_rel), 0);
			}
		}

	if ( l_status == STS$K_FATAL )
		return	STS$K_FATAL;

	return	l_w.nemitted ? STS$K_SUCCESS : STS$K_WARN;
}
