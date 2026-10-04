#define	__MODULE__	"VBACKUP"
#define	__IDENT__	"X01-04"
#define	__REV__		"1.4.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbackup.c
**
**  ABSTRACT:	The head module: the command, what it is made to mean,
**		and the dispatch to the operation.
**
**  DESCRIPTION: Command syntax:
**
**		    VBACKUP input-spec[,...] output-spec [/qualifiers]
**
**		What is done follows from the two sides, as with BACKUP:
**
**		    files    -> saveset		save (/SAVE_SET, a .bck name or "-")
**		    saveset  -> directory	restore
**		    saveset  /LIST[=file]	listing
**		    saveset  [directory] /COMPARE	comparison with the disk
**		    saveset  [file] /EXTRACT=name	one file out, to a file or stdout
**		    files    -> directory	copy, disk to disk
**		    saveset[,...] /RECORD	the journal rebuilt from catalogs
**		    /JOURNAL /LIST		the journal listed
**
**		A saveset on the input side is recognized by its contents.
**
**		This is the head module of the product: __IDENT__ above is the
**		identification of the PRODUCT.  The build reads it out of this
**		file and stamps it into the help libraries, the manual page and
**		the SUMMARY of every saveset.  __REV__ is the same number in
**		the Unix spelling: Xnn-mm[ECOkk] is nn.mm.kk.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-04		 4-OCT-2026	RRL
**		Stage 4: /DATA_FORMAT=COMPRESSED - the data of the files in DATAZ
**		records, LZ4 block format.  /PHYSICAL: a device block by block.
**
**	X01-03		 3-OCT-2026	RRL
**		Stage 3: the saveset is written by a thread of its own; hints
**		to the page cache.  VBACKUP_PIPELINE=0 for the trouble-shooting.
**
**	X01-02		 3-OCT-2026	RRL
**		Stage 2: /RECORD and the journal, /SINCE=BACKUP, incremental
**		savesets, a chain of savesets restored /INCREMENTAL, the journal
**		listed (/JOURNAL /LIST) and rebuilt (savesets /RECORD), the copy
**		disk to disk (an output that is no saveset).
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version: save, restore, /LIST, /COMPARE, /VERIFY,
**		/EXTRACT; saveset format version 1 (doc/format.md).
**
**--
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<strings.h>
#include	<errno.h>
#include	<unistd.h>
#include	<pwd.h>
#include	<grp.h>

#include	"vbkdef.h"

/*
**  Indices of the qualifier table; the table is kept in exactly this order
*/
enum	{
	VBACKUP$K_QUAL_SAVE_SET	= 0,
	VBACKUP$K_QUAL_BLOCK_SIZE,
	VBACKUP$K_QUAL_GROUP_SIZE,
	VBACKUP$K_QUAL_VOLUME_SIZE,
	VBACKUP$K_QUAL_COMMENT,
	VBACKUP$K_QUAL_SELECT,
	VBACKUP$K_QUAL_EXCLUDE,
	VBACKUP$K_QUAL_SINCE,
	VBACKUP$K_QUAL_BEFORE,
	VBACKUP$K_QUAL_MODIFIED,
	VBACKUP$K_QUAL_CREATED,
	VBACKUP$K_QUAL_CHANGED,
	VBACKUP$K_QUAL_BY_OWNER,
	VBACKUP$K_QUAL_CROSS_DEVICE,
	VBACKUP$K_QUAL_IGNORE,
	VBACKUP$K_QUAL_XATTRS,
	VBACKUP$K_QUAL_VERIFY,
	VBACKUP$K_QUAL_LOG,
	VBACKUP$K_QUAL_CONFIRM,
	VBACKUP$K_QUAL_REPLACE,
	VBACKUP$K_QUAL_OWNER,
	VBACKUP$K_QUAL_LIST,
	VBACKUP$K_QUAL_FULL,
	VBACKUP$K_QUAL_BRIEF,
	VBACKUP$K_QUAL_FORMAT,
	VBACKUP$K_QUAL_EXTRACT,
	VBACKUP$K_QUAL_COMPARE,
	VBACKUP$K_QUAL_RECORD,
	VBACKUP$K_QUAL_JOURNAL,
	VBACKUP$K_QUAL_INCREMENTAL,
	VBACKUP$K_QUAL_HELP,
	VBACKUP$K_QUAL_DATA_FORMAT,
	VBACKUP$K_QUAL_PHYSICAL,

	VBACKUP$K_QUAL_MAX
	};

#define	VBACKUP$K_IGN_NOBACKUP	1
#define	VBACKUP$K_FMT_VMS	0
#define	VBACKUP$K_FMT_LS	1

static	CLI_KEYWORD	s_ignkwd [] = {
	{ .name = {$ASCINI("NOBACKUP")},	.val = VBACKUP$K_IGN_NOBACKUP },
	{ .name = { .len = 0 } }
	};

static	CLI_KEYWORD	s_fmtkwd [] = {
	{ .name = {$ASCINI("VMS")},	.val = VBACKUP$K_FMT_VMS },
	{ .name = {$ASCINI("LS")},	.val = VBACKUP$K_FMT_LS },
	{ .name = { .len = 0 } }
	};

static	CLI_KEYWORD	s_dfmkwd [] = {
	{ .name = {$ASCINI("COMPRESSED")},	.val = 1 },
	{ .name = {$ASCINI("UNCOMPRESSED")},	.val = 0 },
	{ .name = { .len = 0 } }
	};

static	CLI_PQDESC	s_quals [] = {
	{ .name = {$ASCINI("SAVE_SET")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("BLOCK_SIZE")},	.type = CLI$K_NUM,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("GROUP_SIZE")},	.type = CLI$K_NUM,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("VOLUME_SIZE")},	.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("COMMENT")},		.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("SELECT")},		.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL,	.flag = CLI$M_LIST },
	{ .name = {$ASCINI("EXCLUDE")},		.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL,	.flag = CLI$M_LIST },
	{ .name = {$ASCINI("SINCE")},		.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("BEFORE")},		.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("MODIFIED")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("CREATED")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("CHANGED")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("BY_OWNER")},	.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("CROSS_DEVICE")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("IGNORE")},		.type = CLI$K_KWD,	.pn = CLI$K_QUAL,	.flag = CLI$M_LIST,	.kwd = s_ignkwd },
	{ .name = {$ASCINI("XATTRS")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("VERIFY")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("LOG")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("CONFIRM")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("REPLACE")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("OWNER")},		.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("LIST")},		.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("FULL")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("BRIEF")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("FORMAT")},		.type = CLI$K_KWD,	.pn = CLI$K_QUAL,	.kwd = s_fmtkwd },
	{ .name = {$ASCINI("EXTRACT")},		.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("COMPARE")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("RECORD")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
	{ .name = {$ASCINI("JOURNAL")},		.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("INCREMENTAL")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("HELP")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("DATA_FORMAT")},	.type = CLI$K_KWD,	.pn = CLI$K_QUAL,	.kwd = s_dfmkwd },
	{ .name = {$ASCINI("PHYSICAL")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = { .len = 0 } }
	};

/*
**  The verb the CLI service is shown; the words of the command are taken
**  apart here, the service sees the qualifiers only
*/
static	char		s_clivrb [] = { "VBACKUP$CMD" };

static	CLI_VERB	s_verbs [] = {
	{ .name = {$ASCINI("VBACKUP$CMD")}, .quals = s_quals },
	{ .name = { .len = 0 } }
	};

static	const char	s_usage [] = {
	"Usage: vbackup input-spec[,...] output-spec [/qualifiers]\n"
	"\n"
	"    files   -> saveset      save: output is /SAVE_SET, a .bck name, or -\n"
	"    saveset -> directory    restore\n"
	"    saveset /LIST[=file]    list the saveset (/BRIEF, /FULL, /FORMAT=LS)\n"
	"    saveset [dir] /COMPARE  compare the saveset with the disk\n"
	"    saveset [file] /EXTRACT=name   one file out, to a file or stdout\n"
	"    files   -> directory    copy, disk to disk\n"
	"    saveset[,...] /RECORD   rebuild the journal from the catalogs\n"
	"    /JOURNAL /LIST [/FULL]  list the journal\n"
	"\n"
	"  Save:     /BLOCK_SIZE=n /GROUP_SIZE=n /VOLUME_SIZE=size /COMMENT=\"...\"\n"
	"            /SINCE=time /BEFORE=time /MODIFIED /CREATED /CHANGED\n"
	"            /BY_OWNER=user /[NO]CROSS_DEVICE /IGNORE=NOBACKUP /VERIFY\n"
	"            /RECORD /SINCE=BACKUP /JOURNAL=file /DATA_FORMAT=COMPRESSED\n"
	"  Restore:  /REPLACE /OWNER=ORIGINAL|DEFAULT|user /INCREMENTAL\n"
	"  Device:   /dev/sdb1 disk.bck /PHYSICAL      disk.bck /dev/sdc1 /PHYSICAL /REPLACE\n"
	"  Common:   /SELECT=(pat,...) /EXCLUDE=(pat,...) /[NO]XATTRS /LOG /CONFIRM\n"
	"\n"
	"Wildcards are expanded by the utility itself - quote them: '/home/.../*.c'\n"
	"The full description is a help library:  vbackup /HELP [topic]\n"
	};


/*
**  Is a word of the command a qualifier?  A slash begins an absolute file
**  specification too, so it is one only when it names one; the long form
**  --NAME is never anything else.  As SEARCH X01-31 does it.
*/
static	int	s_vbk$isqual	(
	const	char *		a_word
			)
{
const char *	l_name;
char		l_ups [VBACKUP$K_SZ_STR];
size_t		l_len;

	if ( !a_word [0] || !a_word [1] )
		return	STS$K_WARN;

	if ( (a_word [0] == '-') && (a_word [1] == '-') )
		return	a_word [2] ? STS$K_SUCCESS : STS$K_WARN;

	if ( a_word [0] != '/' )
		return	STS$K_WARN;

	l_name	= a_word + 1;

	for ( l_len = 0; l_name [l_len] && (l_name [l_len] != '=') && (l_name [l_len] != ':'); l_len++ )
		if ( l_name [l_len] == '/' )
			return	STS$K_WARN;

	if ( !l_len || (l_len >= sizeof(l_ups)) )
		return	STS$K_WARN;

	vbk$upcase(l_len, l_name, l_ups);

	for ( int i = 0; $ASCLEN(&s_quals[i].name); i++ )
		{
		const char *	l_q = (const char *) $ASCPTR(&s_quals[i].name);
		size_t		l_qlen = $ASCLEN(&s_quals[i].name);

		if ( (l_len <= l_qlen) && !strncmp(l_ups, l_q, l_len) )
			return	STS$K_SUCCESS;

		if ( (s_quals[i].flag & CLI$M_NEGATABLE) && (l_len > 2) && (l_len <= (l_qlen + 2))
			&& !strncmp(l_ups, "NO", 2) && !strncmp(l_ups + 2, l_q, l_len - 2) )
			return	STS$K_SUCCESS;
		}

	return	STS$K_WARN;
}


/*
**  Split a comma separated list in place; an element in double quotes is
**  taken as it stands
*/
static	int	s_vbk$split	(
		char *		a_str,
		unsigned	a_max,
		char **		a_elem,
		unsigned *	a_cnt
			)
{
char *		l_p = a_str;
unsigned	l_cnt = 0;

	while ( *l_p )
		{
		if ( l_cnt >= a_max )
			{
			*a_cnt	= l_cnt;

			return	STS$K_WARN;
			}

		if ( *l_p == '"' )
			{
			a_elem [l_cnt++] = ++l_p;

			while ( *l_p && (*l_p != '"') )
				l_p++;

			if ( *l_p )
				*l_p++ = '\0';

			while ( *l_p && (*l_p != ',') )
				l_p++;
			}
		else	{
			a_elem [l_cnt++] = l_p;

			while ( *l_p && (*l_p != ',') )
				l_p++;
			}

		if ( *l_p == ',' )
			*l_p++ = '\0';
		}

	*a_cnt	= l_cnt;

	return	STS$K_SUCCESS;
}


/*
**  The value of a qualifier as a string; STS$K_WARN - none was given
*/
static	int	s_vbk$getstr	(
		CLI_CTX *	a_clictx,
		int		a_qual,
		char *		a_buf,
		size_t		a_bufsz
			)
{
ASC	l_val = {0};

	if ( !(1 & __cli$get_value(a_clictx, &s_quals [a_qual], &l_val)) || !$ASCLEN(&l_val) )
		return	STS$K_WARN;

	snprintf(a_buf, a_bufsz, "%.*s", (int) $ASCLEN(&l_val), (char *) $ASCPTR(&l_val));

	return	STS$K_SUCCESS;
}

static	int	s_vbk$present	(
		CLI_CTX *	a_clictx,
		int		a_qual,
		int *		a_negated
			)
{
unsigned	l_state = 0;

	if ( !(1 & __cli$present(a_clictx, &s_quals [a_qual], &l_state)) )
		return	0;

	if ( a_negated )
		*a_negated = (l_state & CLI$M_NEGATED) ? 1 : 0;

	return	1;
}


/*
**  A size with an optional binary suffix K, M, G, T
*/
static	int	s_vbk$cvtsize	(
	const	char *		a_str,
		uint64_t *	a_size
			)
{
char *		l_end;
uint64_t	l_v;

	errno	= 0;
	l_v	= strtoull(a_str, &l_end, 10);

	if ( errno || (l_end == a_str) )
		return	STS$K_ERROR;

	switch ( *l_end )
		{
		case	'k':	case	'K':	l_v <<= 10;	l_end++;	break;
		case	'm':	case	'M':	l_v <<= 20;	l_end++;	break;
		case	'g':	case	'G':	l_v <<= 30;	l_end++;	break;
		case	't':	case	'T':	l_v <<= 40;	l_end++;	break;
		}

	if ( *l_end && strcasecmp(l_end, "B") )
		return	STS$K_ERROR;

	*a_size	= l_v;

	return	STS$K_SUCCESS;
}


/*
**  Patterns of /SELECT or /EXCLUDE; '?' is the '%' of OpenVMS
*/
static	int	s_vbk$patterns	(
		CLI_CTX *	a_clictx,
		int		a_qual,
		char **		a_pat,
		unsigned *	a_npat,
	const	char *		a_what
			)
{
ASC	l_val;

	while ( 1 & __cli$get_value(a_clictx, &s_quals [a_qual], &l_val) )
		{
		if ( *a_npat >= VBACKUP$K_MAXPAT )
			{
			$VBKMSG(VBACKUP$_TOOMANY, a_what, VBACKUP$K_MAXPAT);
			break;
			}

		if ( !(a_pat [*a_npat] = strndup((char *) $ASCPTR(&l_val), $ASCLEN(&l_val))) )
			return	$VBKMSG(VBACKUP$_NOMEM, errno, strerror(errno));

		for ( char *l_p = a_pat [*a_npat]; *l_p; l_p++ )
			if ( *l_p == '?' )
				*l_p = '%';

		(*a_npat)++;
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Turn the qualifiers into the parameters of the operation, checking
**	every value.
**
**  FORMAL PARAMETERS:
**
**	a_clictx	The parsed command
**	a_opts		Receives the parameters; the defaults are set beforehand
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the condition that has been signalled.
**--
*/
static	int	s_vbk$options	(
		CLI_CTX *	a_clictx,
		VBK$OPTS *	a_opts
			)
{
char		l_str [VBACKUP$K_SZ_PATH];
uint64_t	l_v;
int		l_neg;
ASC		l_val;

	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_BLOCK_SIZE, l_str, sizeof(l_str)) )
		{
		l_v	= strtoull(l_str, NULL, 0);

		if ( (l_v < VBK$K_MINBSZ) || (l_v > VBK$K_MAXBSZ) || (l_v % VBK$K_BSZALIGN) )
			return	$VBKMSG(VBACKUP$_IVQUAL, l_str, "BLOCK_SIZE");

		a_opts->bsize	= (uint32_t) l_v;
		}

	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_GROUP_SIZE, l_str, sizeof(l_str)) )
		{
		l_v	= strtoull(l_str, NULL, 0);

		if ( l_v > VBK$K_MAXGRP )
			return	$VBKMSG(VBACKUP$_IVQUAL, l_str, "GROUP_SIZE");

		a_opts->grpsz	= (uint32_t) l_v;
		}

	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_VOLUME_SIZE, l_str, sizeof(l_str)) )
		{
		if ( !(1 & s_vbk$cvtsize(l_str, &l_v)) || (l_v < ((uint64_t) (a_opts->grpsz + 3) * a_opts->bsize)) )
			return	$VBKMSG(VBACKUP$_IVQUAL, l_str, "VOLUME_SIZE");

		a_opts->volsize	= (l_v / a_opts->bsize) * a_opts->bsize;
		}

	s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_COMMENT, a_opts->comment, sizeof(a_opts->comment));

	if ( !(1 & s_vbk$patterns(a_clictx, VBACKUP$K_QUAL_SELECT, a_opts->select, &a_opts->nselect, "/SELECT patterns")) )
		return	STS$K_ERROR;

	if ( !(1 & s_vbk$patterns(a_clictx, VBACKUP$K_QUAL_EXCLUDE, a_opts->exclude, &a_opts->nexclude, "/EXCLUDE patterns")) )
		return	STS$K_ERROR;

	/*
	**  The time filters.  /SINCE=BACKUP leaves the choice to the journal;
	**  any of them makes a save INCREMENTAL, and the SUMMARY tells which
	**  one it was, as it was given.
	*/
	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_SINCE, l_str, sizeof(l_str)) )
		{
		if ( !strncasecmp(l_str, "BACKUP", strlen(l_str)) )
			a_opts->sincebackup = 1;
		else if ( !(1 & vbk$cvttim(l_str, &a_opts->since)) )
			return	$VBKMSG(VBACKUP$_IVTIME, l_str, "SINCE");
		else	a_opts->hassince = 1;

		snprintf(a_opts->filter, sizeof(a_opts->filter), "/SINCE=%.200s", a_opts->sincebackup ? "BACKUP" : l_str);
		}

	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_BEFORE, l_str, sizeof(l_str)) )
		{
		size_t	l_len = strlen(a_opts->filter);

		if ( !(1 & vbk$cvttim(l_str, &a_opts->before)) )
			return	$VBKMSG(VBACKUP$_IVTIME, l_str, "BEFORE");

		a_opts->hasbefore = 1;
		snprintf(a_opts->filter + l_len, sizeof(a_opts->filter) - l_len, "%s/BEFORE=%.200s", l_len ? " " : "", l_str);
		}

	a_opts->timefilter = a_opts->hassince || a_opts->hasbefore || a_opts->sincebackup;

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_RECORD, &l_neg) )
		a_opts->record	= !l_neg;

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_INCREMENTAL, NULL) )
		{
		/*
		**  The removal cleans every directory of the saveset, whatever was
		**  selected: with a selection it would clean directories the user
		**  did not mean to touch, so the two are not given together
		*/
		if ( a_opts->nselect || a_opts->nexclude )
			return	$VBKMSG(VBACKUP$_CONFQUAL, "INCREMENTAL", a_opts->nselect ? "SELECT" : "EXCLUDE");

		/* What an incremental saveset holds replaces what an earlier one put there */
		a_opts->incremental = 1;
		a_opts->replace	= 1;
		}

	s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_JOURNAL, a_opts->jnlspec, sizeof(a_opts->jnlspec));

	/* A switch for the trouble-shooting, not a qualifier: the saveset written without the writer thread */
	{
	const char *	l_env = getenv("VBACKUP_PIPELINE");

	a_opts->nopipe	= l_env && !strcmp(l_env, "0");
	}

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_MODIFIED, NULL) + s_vbk$present(a_clictx, VBACKUP$K_QUAL_CREATED, NULL)
		+ s_vbk$present(a_clictx, VBACKUP$K_QUAL_CHANGED, NULL) > 1 )
		return	$VBKMSG(VBACKUP$_CONFQUAL, "MODIFIED, /CREATED", "CHANGED");

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_CREATED, NULL) )
		a_opts->timsrc	= VBACKUP$K_TIM_CREATED;
	else if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_CHANGED, NULL) )
		a_opts->timsrc	= VBACKUP$K_TIM_CHANGED;

	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_BY_OWNER, l_str, sizeof(l_str)) )
		{
		struct passwd	*l_pw = getpwnam(l_str);
		char		*l_end;

		if ( l_pw )
			a_opts->byowner	= l_pw->pw_uid;
		else	{
			a_opts->byowner	= (uid_t) strtoul(l_str, &l_end, 10);

			if ( *l_end || (l_end == l_str) )
				return	$VBKMSG(VBACKUP$_IVQUAL, l_str, "BY_OWNER");
			}

		a_opts->hasowner = 1;
		}

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_CROSS_DEVICE, &l_neg) )
		a_opts->crossdev = !l_neg;

	while ( 1 & __cli$get_value(a_clictx, &s_quals [VBACKUP$K_QUAL_IGNORE], &l_val) )
		if ( !strncasecmp((char *) $ASCPTR(&l_val), "NOBACKUP", $ASCLEN(&l_val)) )
			a_opts->nobackup = 1;

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_XATTRS, &l_neg) )
		a_opts->xattrs	= !l_neg;

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_VERIFY, &l_neg) )
		a_opts->verify	= !l_neg;

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_LOG, &l_neg) )
		a_opts->log	= !l_neg;

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_CONFIRM, &l_neg) )
		a_opts->confirm	= !l_neg;

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_REPLACE, &l_neg) )
		a_opts->replace	= !l_neg;

	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_OWNER, l_str, sizeof(l_str)) )
		{
		size_t		l_len = strlen(l_str);
		struct passwd	*l_pw;

		if ( !strncasecmp(l_str, "ORIGINAL", l_len) )
			a_opts->ownmode	= VBACKUP$K_OWN_ORIGINAL;
		else if ( !strncasecmp(l_str, "DEFAULT", l_len) )
			a_opts->ownmode	= VBACKUP$K_OWN_DEFAULT;
		else if ( (l_pw = getpwnam(l_str)) )
			{
			a_opts->ownmode	= VBACKUP$K_OWN_USER;
			a_opts->ownuid	= l_pw->pw_uid;
			a_opts->owngid	= l_pw->pw_gid;
			}
		else	return	$VBKMSG(VBACKUP$_IVQUAL, l_str, "OWNER");
		}

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_FULL, NULL) && s_vbk$present(a_clictx, VBACKUP$K_QUAL_BRIEF, NULL) )
		return	$VBKMSG(VBACKUP$_CONFQUAL, "FULL", "BRIEF");

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_FULL, NULL) )
		a_opts->lstfmt	= VBACKUP$K_LST_FULL;

	/*
	**  /PHYSICAL: a device block by block - a tree, a time, a journal mean
	**  nothing for it, and those qualifiers are refused rather than ignored
	*/
	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_PHYSICAL, NULL) )
		{
		a_opts->physical = 1;

		if ( a_opts->timefilter || a_opts->record || a_opts->incremental || a_opts->nselect || a_opts->nexclude || a_opts->hasowner )
			return	$VBKMSG(VBACKUP$_CONFQUAL, "PHYSICAL", a_opts->timefilter ? "SINCE, /BEFORE" : a_opts->record ? "RECORD"
				: a_opts->incremental ? "INCREMENTAL" : a_opts->hasowner ? "BY_OWNER" : "SELECT, /EXCLUDE");
		}

	/* /DATA_FORMAT=COMPRESSED: the data in DATAZ records, LZ4 (format.md, 6.7) */
	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_DATA_FORMAT, l_str, sizeof(l_str)) )
		{
		if ( l_str [0] && !strncasecmp(l_str, "COMPRESSED", strlen(l_str)) )
			a_opts->compress = 1;
		else if ( l_str [0] && !strncasecmp(l_str, "UNCOMPRESSED", strlen(l_str)) )
			a_opts->compress = 0;
		else	return	$VBKMSG(VBACKUP$_IVQUAL, l_str, "DATA_FORMAT");
		}

	if ( (1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_FORMAT, l_str, sizeof(l_str))) && !strncasecmp(l_str, "LS", strlen(l_str)) )
		a_opts->lstfmt	= VBACKUP$K_LST_LS;

	s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_LIST, a_opts->lstfile, sizeof(a_opts->lstfile));
	s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_EXTRACT, a_opts->extract, sizeof(a_opts->extract));

	return	STS$K_SUCCESS;
}


/*
**  Is the output of a save a saveset?  /SAVE_SET, a .bck name, or "-"
*/
static	int	s_vbk$issaveset	(
		CLI_CTX *	a_clictx,
	const	char *		a_spec
			)
{
size_t	l_len = strlen(a_spec);

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_SAVE_SET, NULL) || !strcmp(a_spec, "-") )
		return	1;

	return	(l_len > 4) && !strcasecmp(a_spec + l_len - 4, ".bck");
}


int	main	(
		int		a_argc,
		char **		a_argv
		)
{
static	VBK$OPTS	l_opts;
CLI_CTX *	l_clictx = NULL;
char *		l_argv [1 + 64], *l_words [2 + VBACKUP$K_MAXSPEC];
int		l_argc = 1, l_wordcnt = 0, l_sep = a_argc, l_usage = 0, l_status, l_list = 0;
size_t		l_cmdlen = 0;

	vbk$inimsg();

	/* The defaults */
	l_opts.bsize	= VBK$K_DEFBSZ;
	l_opts.grpsz	= VBK$K_DEFGRP;
	l_opts.xattrs	= 1;
	l_opts.ownmode	= geteuid() ? VBACKUP$K_OWN_DEFAULT : VBACKUP$K_OWN_ORIGINAL;

	/* The command as it was given, for the SUMMARY; cut at VBK$K_MAXCMD */
	for ( int i = 0; (i < a_argc) && (l_cmdlen < (sizeof(l_opts.cmdline) - 1)); i++ )
		{
		int	l_n = snprintf(l_opts.cmdline + l_cmdlen, sizeof(l_opts.cmdline) - l_cmdlen, "%s%s", i ? " " : "", a_argv [i]);

		l_cmdlen += (l_n > 0) ? (size_t) l_n : 0;
		}

	for ( int i = 1; i < a_argc; i++ )
		if ( !strcmp(a_argv [i], "--") )
			{
			l_sep	= i;
			break;
			}

	/* The words and the qualifiers apart: the CLI service sees the latter only */
	l_argv [0]	= s_clivrb;

	for ( int i = 1; i < l_sep; i++ )
		{
		if ( !strcasecmp(a_argv [i], "-h") )
			{
			l_usage	= 1;
			continue;
			}

		/* After /HELP every word names the topic, a qualifier too: vbackup /HELP /VERIFY */
		if ( l_usage )
			{
			if ( l_wordcnt < (int) $ARRSZ(l_words) )
				l_words [l_wordcnt++] = a_argv [i];

			continue;
			}

		if ( 1 & s_vbk$isqual(a_argv [i]) )
			{
			const char *	l_p = a_argv [i] + ((a_argv [i][0] == '-') ? 2 : 1);
			size_t		l_len = strlen(l_p);

			if ( (l_len >= 3) && (l_len <= 4) && !strncasecmp(l_p, "HELP", l_len) )
				{
				l_usage	  = 1;
				l_wordcnt = 0;
				continue;
				}

			if ( l_argc < (int) $ARRSZ(l_argv) )
				l_argv [l_argc++] = a_argv [i];

			continue;
			}

		if ( l_wordcnt < (int) $ARRSZ(l_words) )
			l_words [l_wordcnt++] = a_argv [i];
		}

	for ( int i = l_sep + 1; (i < a_argc) && (l_wordcnt < (int) $ARRSZ(l_words)); i++ )
		l_words [l_wordcnt++] = a_argv [i];

	if ( l_usage || (a_argc < 2) )
		{
		l_status = l_usage ? vbk$help(l_wordcnt, l_words, isatty(STDOUT_FILENO)) : STS$K_WARN;

		if ( l_status == STS$K_WARN )
			fputs(s_usage, stdout);

		return	(l_status == STS$K_ERROR) ? VBACKUP$K_EXIT_ERROR : VBACKUP$K_EXIT_OK;
		}

	if ( !(1 & __cli$parse(s_verbs, CLI$M_OPSIGNAL, l_argc, l_argv, (void **) &l_clictx)) )
		return	VBACKUP$K_EXIT_ERROR;

	/* Every lookup from here on is an optional one: the signalling is dropped */
	l_clictx->opts	&= ~CLI$M_OPSIGNAL;

	if ( !(1 & s_vbk$options(l_clictx, &l_opts)) )
		{
		__cli$cleanup(l_clictx);

		return	VBACKUP$K_EXIT_ERROR;
		}

	/* No parameter at all: only the listing of the journal does without one */
	if ( !l_wordcnt )
		{
		if ( s_vbk$present(l_clictx, VBACKUP$K_QUAL_JOURNAL, NULL) && s_vbk$present(l_clictx, VBACKUP$K_QUAL_LIST, NULL) )
			l_status = vbk$jnl_list(&l_opts);
		else	$VBKMSG(VBACKUP$_NOPARAM, "input specification");

		__cli$cleanup(l_clictx);

		return	vbk$exitcode();
		}

	if ( !(1 & s_vbk$split(l_words [0], VBACKUP$K_MAXSPEC, l_opts.input, &l_opts.ninput)) )
		$VBKMSG(VBACKUP$_TOOMANY, "input specifications", VBACKUP$K_MAXSPEC);

	if ( l_wordcnt > 1 )
		{
		size_t	l_len;

		vbk$strcpy(sizeof(l_opts.output), l_opts.output, l_words [1]);

		/* Trailing slashes say nothing, and a restore measures the output directory by its length */
		for ( l_len = strlen(l_opts.output); (l_len > 1) && (l_opts.output [l_len - 1] == '/'); l_len-- )
			l_opts.output [l_len - 1] = '\0';
		}

	l_list	= s_vbk$present(l_clictx, VBACKUP$K_QUAL_LIST, NULL);

	/*
	**  What the command means.  A saveset on the input side is known by
	**  its contents - every input must be one; on the output side by
	**  /SAVE_SET, its name or "-".  An output that is no saveset, beside
	**  an input that is none either, asks for a copy.
	*/
	l_status = STS$K_SUCCESS;

	for ( unsigned i = 0; (i < l_opts.ninput) && (l_status == STS$K_SUCCESS); i++ )
		l_status = vbk$rd_probe(l_opts.input [i]);

	if ( l_opts.extract [0] )
		l_opts.op	= VBACKUP$K_OP_EXTRACT;
	else if ( s_vbk$present(l_clictx, VBACKUP$K_QUAL_COMPARE, NULL) )
		l_opts.op	= VBACKUP$K_OP_COMPARE;
	else if ( l_status == STS$K_SUCCESS )
		{
		if ( l_list && !l_opts.output [0] )
			l_opts.op	= VBACKUP$K_OP_LIST;
		else if ( l_opts.record && !l_opts.output [0] )
			l_opts.op	= VBACKUP$K_OP_RECORD;
		else	l_opts.op	= VBACKUP$K_OP_RESTORE;
		}
	else if ( l_opts.output [0] && s_vbk$issaveset(l_clictx, l_opts.output) )
		l_opts.op	= VBACKUP$K_OP_SAVE;
	else if ( l_opts.output [0] && (l_status != STS$K_ERROR) )
		l_opts.op	= VBACKUP$K_OP_COPY;

	if ( (l_opts.op == VBACKUP$K_OP_EXTRACT) || (l_opts.op == VBACKUP$K_OP_COMPARE) || (l_opts.op == VBACKUP$K_OP_LIST) )
		if ( (l_status != STS$K_SUCCESS) || (l_opts.ninput != 1) )
			{
			$VBKMSG(VBACKUP$_NOTSAVESET, l_opts.input [0]);
			__cli$cleanup(l_clictx);

			return	VBACKUP$K_EXIT_ERROR;
			}

	switch ( l_opts.op )
		{
		case	VBACKUP$K_OP_SAVE:
			if ( l_opts.physical && (l_opts.ninput != 1) )
				{
				$VBKMSG(VBACKUP$_CONFQUAL, "PHYSICAL", "several inputs: one device a saveset");
				break;
				}

			/* The journal: written under /RECORD, read under /SINCE=BACKUP */
			if ( l_opts.record || l_opts.sincebackup )
				{
				static	VBK$JNL	l_jnl;

				if ( !(1 & vbk$jnl_open(&l_jnl, l_opts.jnlspec, l_opts.record)) )
					break;

				l_opts.jnl	= &l_jnl;

				if ( l_opts.sincebackup && !l_jnl.exists )
					$VBKMSG(VBACKUP$_NOJOURNAL, l_jnl.spec);
				}

			if ( 1 & (l_status = vbk$save(&l_opts)) && l_list && strcmp(l_opts.output, "-") )
				{
				/* /LIST of a save: the saveset just written is listed */
				l_opts.input [0] = l_opts.output;
				l_opts.ninput	 = 1;
				vbk$list(&l_opts);
				}

			if ( l_opts.jnl )
				vbk$jnl_close(l_opts.jnl);
			break;

		case	VBACKUP$K_OP_RESTORE:
			if ( !l_opts.output [0] )
				$VBKMSG(VBACKUP$_NOPARAM, l_opts.physical ? "output device" : "output directory");
			else if ( l_opts.physical )
				l_status = (l_opts.ninput == 1) ? vbk$phy_restore(&l_opts) : $VBKMSG(VBACKUP$_CONFQUAL, "PHYSICAL", "several savesets");
			else	l_status = vbk$restore(&l_opts);
			break;

		case	VBACKUP$K_OP_LIST:
			l_status = vbk$list(&l_opts);
			break;

		case	VBACKUP$K_OP_COMPARE:
			l_status = vbk$compare(&l_opts, l_opts.input [0]);
			break;

		case	VBACKUP$K_OP_EXTRACT:
			l_status = vbk$extract(&l_opts);
			break;

		case	VBACKUP$K_OP_COPY:
			l_status = vbk$copy(&l_opts);
			break;

		case	VBACKUP$K_OP_RECORD:
			l_status = vbk$jnl_rebuild(&l_opts);
			break;

		default:
			if ( !l_opts.output [0] )
				$VBKMSG(VBACKUP$_NOPARAM, (l_status == STS$K_ERROR) ? "input specification - it does not exist" : "output specification");
			else	$VBKMSG(VBACKUP$_IVOP, "the input does not exist");
		}

	__cli$cleanup(l_clictx);

	return	vbk$exitcode();
}
