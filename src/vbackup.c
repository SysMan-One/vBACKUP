#define	__MODULE__	"VBACKUP"
#define	__IDENT__	"X01-21"
#define	__REV__		"1.21.0"

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
**	X01-21		 7-OCT-2026	RRL
**		/SOLID, negatable: the small files of a compressed save in SOLID
**		records (format.md 6.12), the default; /NOSOLID - each in its own.
**
**	X01-20		 6-OCT-2026	RRL
**		Stage 16: /LEVEL=8, 9 by the optimal parse of LZMA; the pool of the
**		compression keeps its cores busy on small files.
**
**	X01-19		 6-OCT-2026	RRL
**		Stage 15: /LEVEL=n - 1 LZ4, 2 .. 5 Deflate, 6 .. 9 LZMA; it compresses
**		by itself.
**
**	X01-18		 6-OCT-2026	RRL
**		Stage 14: Windows - /OWNER=ORIGINAL by default for a backup operator
**		(the restore privilege held), the security descriptors put back then;
**		/XATTRS by default again: the streams of NTFS are the user. attributes;
**		/OWNER=user and /BY_OWNER refused there (QUALUSE).
**
**	X01-17		 6-OCT-2026	RRL
**		Stage 13: vbackup.exe, the utility on Windows (src/vbkosw.c,
**		win/vbkwin.h): the command line in UTF-8; /PHYSICAL and /IMAGE
**		refused there; the extended attributes not by default - there
**		are none on Windows.
**
**	X01-16		 6-OCT-2026	RRL
**		A word whose part before what looks like qualifiers is an existing
**		directory is a name in it: /backup/restore/full is not cut into
**		/backup/restore and /FULL, nor /work/r into /work and "/r".
**
**	X01-15		 5-OCT-2026	RRL
**		Stage 11: the parity of /PARITY by the vector instructions of the
**		CPU (VBKRS.C); VBACKUP_NOSIMD.
**
**	X01-14		 5-OCT-2026	RRL
**		/PARITY=m: m parity blocks a group (format.md 4.1); the smallest
**		volume (N + m + 2) blocks.
**
**	X01-13		 5-OCT-2026	RRL
**		A saveset of OpenVMS BACKUP on the input (VBKVMS.C): /LIST,
**		the restore, /EXTRACT.  Qualifiers glued to one another and to
**		nothing - /LIST/FULL - are all qualifiers: the word was cut
**		into a parameter "/LIST", and the saveset restored into /LIST.
**
**	X01-12		 5-OCT-2026	RRL
**		/RECORD of a save onto another node names it node::file (OUTNODE);
**		the reference manual brought to X01-11.
**
**	X01-11		 5-OCT-2026	RRL
**		Stage 8: volumes through a pipe; a saveset to a saveset block for
**		block (/TRANSFER); node::file - a saveset on another node through
**		ssh, /VERIFY read back from there; the pipeline of an encrypted
**		save.  A save of an input that is not there makes no saveset.
**
**	X01-10		 5-OCT-2026	RRL
**		MC knows a saveset by .sav as by .bck and by its contents (the
**		magic of file(1) in /etc/magic); the kit by git archive.
**
**	X01-09		 5-OCT-2026	RRL
**		Every message in the form "Label: value - words" (vbkx and the
**		geeks' extractors too); a save to the standard output refuses
**		/VERIFY and /LIST; STARTED names every input; /LIST honours
**		/SELECT and /EXCLUDE; the reference manual (doc/ref).
**
**	X01-08		 5-OCT-2026	RRL
**		Stage 7: "-" as the input - a saveset from a pipe; the pool of the
**		encryption started (VBK$PAR_INIT); lib/ without printf.
**		/VOLUME_SIZE refused with the standard output: one stream, one
**		volume.
**
**	X01-07		 5-OCT-2026	RRL
**		STARTED and COMPLETED (how it went, how long): a command that has
**		worked says so without /LOG too.  The text of the usage, the
**		filter and the command line made by FAO.
**
**	X01-06		 5-OCT-2026	RRL
**		Stage 6: encrypted savesets - /ENCRYPT, /KEY_FILE (format.md 6.10).
**		The command line: a third parameter is refused (MAXPARM: ".log"
**		meant "/LOG"), qualifiers glued to a parameter are taken apart
**		as DCL does (TTR3.SAV/SAVE), an input with wildcards is not
**		"an input that does not exist", .SAV names a saveset as .BCK.
**
**	X01-05		 4-OCT-2026	RRL
**		Stage 5: the plugins of the file managers - Midnight Commander
**		(extfs uvbk), far2l and Far3 (MultiArc), Total Commander and
**		Double Commander (WCX); compression on several cores.
**
**	X01-04		 4-OCT-2026	RRL
**		Stage 4: /DATA_FORMAT=COMPRESSED - the data of the files in DATAZ
**		records, LZ4 block format.  /PHYSICAL: a device block by block.
**		/IMAGE: a whole file system, made again by a restore.  /ORIGINAL,
**		/DELETE.
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
	VBACKUP$K_QUAL_PARITY,
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
	VBACKUP$K_QUAL_LEVEL,
	VBACKUP$K_QUAL_PHYSICAL,
	VBACKUP$K_QUAL_IMAGE,
	VBACKUP$K_QUAL_ORIGINAL,
	VBACKUP$K_QUAL_DELETE,
	VBACKUP$K_QUAL_ENCRYPT,
	VBACKUP$K_QUAL_KEY_FILE,
	VBACKUP$K_QUAL_TRANSFER,
	VBACKUP$K_QUAL_SOLID,

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
	{ .name = {$ASCINI("PARITY")},		.type = CLI$K_NUM,	.pn = CLI$K_QUAL },
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
	{ .name = {$ASCINI("LEVEL")},		.type = CLI$K_NUM,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("PHYSICAL")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("IMAGE")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("ORIGINAL")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("DELETE")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("ENCRYPT")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("KEY_FILE")},	.type = CLI$K_QSTRING,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("TRANSFER")},	.type = CLI$K_OPT,	.pn = CLI$K_QUAL },
	{ .name = {$ASCINI("SOLID")},		.type = CLI$K_OPT,	.pn = CLI$K_QUAL,	.flag = CLI$M_NEGATABLE },
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
	"  Save:     /BLOCK_SIZE=n /GROUP_SIZE=n /PARITY=m /VOLUME_SIZE=size /COMMENT=\"...\"\n"
	"            /SINCE=time /BEFORE=time /MODIFIED /CREATED /CHANGED\n"
	"            /BY_OWNER=user /[NO]CROSS_DEVICE /IGNORE=NOBACKUP /VERIFY\n"
	"            /RECORD /SINCE=BACKUP /JOURNAL=file /DATA_FORMAT=COMPRESSED /LEVEL=1..9\n"
	"            /NOSOLID (compressed: small files in records of their own, version 1)\n"
	"            /DELETE (with /VERIFY: the files saved and verified are deleted)\n"
	"            /ENCRYPT (asks a passphrase twice, or /KEY_FILE=file)\n"
	"  Saveset:  x.bck y.bck | x.bck - | - y.bck  block for block (/TRANSFER)\n"
	"  Network:  /home node::/backup/home.bck     node::/backup/home.bck /restore\n"
	"  Restore:  /REPLACE /OWNER=ORIGINAL|DEFAULT|user /INCREMENTAL /ORIGINAL (no output)\n"
	"  Device:   /dev/sdb1 disk.bck /PHYSICAL      disk.bck /dev/sdc1 /PHYSICAL /REPLACE\n"
	"  Volume:   /mnt/data vol.bck /IMAGE          vol.bck /dev/sdc1 /IMAGE /REPLACE\n"
	"  Common:   /SELECT=(pat,...) /EXCLUDE=(pat,...) /[NO]XATTRS /LOG /CONFIRM\n"
	"            /KEY_FILE=file - the passphrase of an encrypted saveset, its first line\n"
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

	$VBKFAOB(a_buf, a_bufsz, "!AD", $ASCLEN(&l_val), $ASCPTR(&l_val));

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

	/* /PARITY=m: m parity blocks a group, any m lost blocks of it rebuilt (format.md 4.1) */
	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_PARITY, l_str, sizeof(l_str)) )
		{
		l_v	= strtoull(l_str, NULL, 0);

		if ( (l_v < 1) || (l_v > VBK$K_MAXPAR) )
			return	$VBKMSG(VBACKUP$_IVQUAL, l_str, "PARITY");

		if ( (l_v > 1) && !a_opts->grpsz )
			return	$VBKMSG(VBACKUP$_QUALUSE, "PARITY", "needs groups: not with /GROUP_SIZE=0");

		a_opts->parity	= (uint32_t) l_v;
		}

	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_VOLUME_SIZE, l_str, sizeof(l_str)) )
		{
		if ( !(1 & s_vbk$cvtsize(l_str, &l_v)) || (l_v < ((uint64_t) (a_opts->grpsz + a_opts->parity + 2) * a_opts->bsize)) )
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

		{
		const char *	l_s = a_opts->sincebackup ? "BACKUP" : l_str;

		$VBKFAOB(a_opts->filter, sizeof(a_opts->filter), "/SINCE=!AD", (strlen(l_s) > 200) ? 200 : strlen(l_s), l_s);
		}
		}

	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_BEFORE, l_str, sizeof(l_str)) )
		{
		size_t	l_len = strlen(a_opts->filter);

		if ( !(1 & vbk$cvttim(l_str, &a_opts->before)) )
			return	$VBKMSG(VBACKUP$_IVTIME, l_str, "BEFORE");

		a_opts->hasbefore = 1;
		$VBKFAOB(a_opts->filter + l_len, sizeof(a_opts->filter) - l_len, "!AZ/BEFORE=!AD", l_len ? " " : "",
			(strlen(l_str) > 200) ? 200 : strlen(l_str), l_str);
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

#ifdef	_WIN32
	/* No owner by number on Windows: a SID is no uid */
	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_BY_OWNER, NULL) )
		return	$VBKMSG(VBACKUP$_QUALUSE, "BY_OWNER", "not on Windows");
#endif

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
#ifdef	_WIN32
		else	return	(void) l_pw, $VBKMSG(VBACKUP$_QUALUSE, "OWNER", "only ORIGINAL and DEFAULT on Windows");
#else
		else if ( (l_pw = getpwnam(l_str)) )
			{
			a_opts->ownmode	= VBACKUP$K_OWN_USER;
			a_opts->ownuid	= l_pw->pw_uid;
			a_opts->owngid	= l_pw->pw_gid;
			}
		else	return	$VBKMSG(VBACKUP$_IVQUAL, l_str, "OWNER");
#endif
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

#ifdef	_WIN32
		return	$VBKMSG(VBACKUP$_QUALUSE, "PHYSICAL", "not on Windows");
#endif

		if ( a_opts->timefilter || a_opts->record || a_opts->incremental || a_opts->nselect || a_opts->nexclude || a_opts->hasowner )
			return	$VBKMSG(VBACKUP$_CONFQUAL, "PHYSICAL", a_opts->timefilter ? "SINCE, /BEFORE" : a_opts->record ? "RECORD"
				: a_opts->incremental ? "INCREMENTAL" : a_opts->hasowner ? "BY_OWNER" : "SELECT, /EXCLUDE");
		}

	/* /IMAGE: a whole volume - every file of it, no choice among them */
	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_IMAGE, NULL) )
		{
		a_opts->image	= 1;

#ifdef	_WIN32
		return	$VBKMSG(VBACKUP$_QUALUSE, "IMAGE", "not on Windows");
#endif

		if ( a_opts->physical || a_opts->incremental || a_opts->nselect || a_opts->nexclude || a_opts->hasowner )
			return	$VBKMSG(VBACKUP$_CONFQUAL, "IMAGE", a_opts->physical ? "PHYSICAL" : a_opts->incremental ? "INCREMENTAL"
				: a_opts->hasowner ? "BY_OWNER" : "SELECT, /EXCLUDE");
		}

	/* /ORIGINAL: back where the files came from - no output directory, nothing of a device */
	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_ORIGINAL, NULL) )
		{
		a_opts->original = 1;

		if ( a_opts->physical || a_opts->image )
			return	$VBKMSG(VBACKUP$_CONFQUAL, "ORIGINAL", a_opts->physical ? "PHYSICAL" : "IMAGE");
		}

	/* /DELETE: never without /VERIFY - a file goes only once its copy has been read back and compared */
	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_DELETE, NULL) )
		{
		a_opts->delete	= 1;

		if ( !a_opts->verify )
			return	$VBKMSG(VBACKUP$_QUALUSE, "DELETE", "a file is deleted only once its copy has been read back and compared - give /VERIFY too");

		if ( a_opts->physical || a_opts->image || a_opts->timefilter )
			return	$VBKMSG(VBACKUP$_CONFQUAL, "DELETE", a_opts->physical ? "PHYSICAL" : a_opts->image ? "IMAGE" : "SINCE, /BEFORE");
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

	/* /LEVEL=n: how hard - 1 LZ4 (fast, the default), 2 .. 5 Deflate, 6 .. 9 LZMA (format.md 6.7); compressed by it alone */
	a_opts->zlevel	= 1;

	if ( 1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_LEVEL, l_str, sizeof(l_str)) )
		{
		l_v	= strtoull(l_str, NULL, 0);

		if ( (l_v < 1) || (l_v > VBK$K_ZLEVELS) )
			return	$VBKMSG(VBACKUP$_IVQUAL, l_str, "LEVEL");

		if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_DATA_FORMAT, NULL) && !a_opts->compress )
			return	$VBKMSG(VBACKUP$_CONFQUAL, "LEVEL", "DATA_FORMAT=UNCOMPRESSED");

		a_opts->zlevel	 = (int) l_v;
		a_opts->compress = 1;
		}

	/* /NOSOLID: every file in records of its own, as before X01-21; /SOLID alone does not compress */
	a_opts->solid	= 1;

	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_SOLID, &l_neg) )
		a_opts->solid = !l_neg;

	/* /ENCRYPT: format.md 6.10; the passphrase is asked for when the saveset is made, never taken from the command */
	if ( s_vbk$present(a_clictx, VBACKUP$K_QUAL_ENCRYPT, NULL) )
		a_opts->encrypt	= 1;

	s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_KEY_FILE, a_opts->keyfile, sizeof(a_opts->keyfile));

	if ( (1 & s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_FORMAT, l_str, sizeof(l_str))) && !strncasecmp(l_str, "LS", strlen(l_str)) )
		a_opts->lstfmt	= VBACKUP$K_LST_LS;

	s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_LIST, a_opts->lstfile, sizeof(a_opts->lstfile));
	s_vbk$getstr(a_clictx, VBACKUP$K_QUAL_EXTRACT, a_opts->extract, sizeof(a_opts->extract));

	return	STS$K_SUCCESS;
}


/*
**  Qualifiers glued to a parameter, as DCL takes them: TTR3.SAV/SAVE/LOG.
**  The word is cut at the first slash from which on every piece is a
**  qualifier - and only when the word is not the name of a file as a
**  whole.  A value with a slash in it (/JOURNAL=/var/...) is not taken
**  from a glued word: it is given apart.  A word that is qualifiers
**  from its first octet on - /LIST/FULL - is all qualifiers; a word whose
**  part before the qualifiers is an existing directory is a name in it.  Returns the
**  length of the parameter, 0 - no parameter in it, (size_t) -1 - the
**  word is not cut.
*/
static	size_t	s_vbk$glued	(
	const	char *		a_word
			)
{
struct stat	l_st;
char		l_piece [VBACKUP$K_SZ_STR + 2];
const char *	l_p, *l_q;
size_t		l_n;
int		l_ok;

	if ( a_word [0] == '/' ? !strchr(a_word + 1, '/') : !strchr(a_word, '/') )
		return	(size_t) -1;

	if ( !lstat(a_word, &l_st) )
		return	(size_t) -1;

	/* From the first octet: /LIST/FULL - before X01-13 it was cut into the parameter "/LIST" and /FULL */
	for ( l_p = (a_word [0] == '/') ? a_word : strchr(a_word + 1, '/'); l_p; l_p = strchr(l_p + 1, '/') )
		{
		for ( l_ok = 1, l_q = l_p; l_ok && *l_q; l_q += l_n )
			{
			l_n	= 1 + strcspn(l_q + 1, "/");

			if ( l_n >= sizeof(l_piece) )
				l_ok = 0;
			else	{
				memcpy(l_piece, l_q, l_n);
				l_piece [l_n] = '\0';
				l_ok	= (l_n > 1) && (1 & s_vbk$isqual(l_piece));
				}
			}

		if ( l_ok )
			{
			/*
			**  What comes before is an existing directory: the word is a name
			**  in it, to be made - /backup/restore/full is no restore into
			**  /backup/restore with /FULL (X01-15)
			*/
			if ( l_p != a_word )
				{
				char	l_dir [VBACKUP$K_SZ_PATH];

				if ( (size_t) (l_p - a_word) < sizeof(l_dir) )
					{
					memcpy(l_dir, a_word, (size_t) (l_p - a_word));
					l_dir [l_p - a_word] = '\0';

					if ( !stat(l_dir, &l_st) && S_ISDIR(l_st.st_mode) )
						return	(size_t) -1;
					}
				}

			return	(size_t) (l_p - a_word);
			}
		}

	return	(size_t) -1;
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

	/* .BCK and .SAV: the names savesets have on OpenVMS */
	return	(l_len > 4) && (!strcasecmp(a_spec + l_len - 4, ".bck") || !strcasecmp(a_spec + l_len - 4, ".sav"));
}


/*
**  Has a specification a wildcard or "..." in it?  Such an input names
**  files the walk finds - it does not exist as one file, and is no saveset.
*/
static	int	s_vbk$haswild	(
	const	char *		a_spec
			)
{
	if ( strpbrk(a_spec, "*?%") )
		return	1;

	for ( const char *l_p = a_spec; (l_p = strstr(l_p, "...")); l_p += 3 )
		if ( ((l_p == a_spec) || (l_p [-1] == '/')) && ((l_p [3] == '/') || !l_p [3]) )
			return	1;

	return	0;
}


int	main	(
		int		a_argc,
		char **		a_argv
		)
{
static	VBK$OPTS	l_opts;
CLI_CTX *	l_clictx = NULL;
char *		l_argv [1 + 64], *l_words [2 + VBACKUP$K_MAXSPEC];
const char *	l_glued [8], *l_opname = "";
int		l_nglued = 0, l_announce = 0;
struct timespec	l_t0;
static	char	l_inwords [VBK$K_MAXCMD], l_outwords [VBACKUP$K_SZ_PATH], l_dash [] = "-";
char		l_rinnode [256] = "", l_routnode [256] = "";
const char *	l_rinfile = NULL, *l_routfile = NULL;
pid_t		l_rinpid = 0, l_routpid = 0;
int		l_rverify = 0;
int		l_vms = 0, l_argc = 1, l_wordcnt = 0, l_sep = a_argc, l_usage = 0, l_status, l_list = 0;
size_t		l_cmdlen = 0;

#ifdef	_WIN32
	vbk$w_init(&a_argc, &a_argv);
#endif

	vbk$inimsg();

	/* The cores for the encryption: the stripes of a block, the blocks of a group (VBACKUP_CTHREADS) */
	vbk$par_init();

	/* The defaults */
	l_opts.bsize	= VBK$K_DEFBSZ;
	l_opts.grpsz	= VBK$K_DEFGRP;
	l_opts.parity	= 1;
	l_opts.xattrs	= 1;
#ifdef	_WIN32
	l_opts.ownmode	= vbk$w_privileged() ? VBACKUP$K_OWN_ORIGINAL : VBACKUP$K_OWN_DEFAULT;
#else
	l_opts.ownmode	= geteuid() ? VBACKUP$K_OWN_DEFAULT : VBACKUP$K_OWN_ORIGINAL;
#endif

	/* The command as it was given, for the SUMMARY; cut at VBK$K_MAXCMD */
	for ( int i = 0; (i < a_argc) && (l_cmdlen < (sizeof(l_opts.cmdline) - 1)); i++ )
		{
		int	l_n = $VBKFAOB(l_opts.cmdline + l_cmdlen, sizeof(l_opts.cmdline) - l_cmdlen, "!AZ!AZ", i ? " " : "", a_argv [i]);

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

		/* A parameter with qualifiers glued to it: the qualifiers go to the CLI, the rest is the word */
		{
		size_t	l_cut = s_vbk$glued(a_argv [i]);

		if ( l_cut != (size_t) -1 )
			{
			char *	l_w = strdup(a_argv [i]), *l_p, *l_e;

			if ( !l_w )
				return	VBACKUP$K_EXIT_ERROR;

			/* Qualifiers only, glued to one another: nothing to say, no parameter */
			if ( l_cut )
				l_glued [l_nglued++ % $ARRSZ(l_glued)] = a_argv [i];

			for ( l_p = l_w + l_cut; *l_p; l_p = l_e )
				{
				l_e	= strchr(l_p + 1, '/');
				l_e	= l_e ? l_e : l_p + strlen(l_p);

				if ( l_argc < (int) $ARRSZ(l_argv) )
					l_argv [l_argc++] = strndup(l_p, (size_t) (l_e - l_p));
				}

			l_w [l_cut] = '\0';

			if ( l_cut && (l_wordcnt < (int) $ARRSZ(l_words)) )
				l_words [l_wordcnt++] = l_w;

			continue;
			}
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
			$VBKFAOP(stdout, "!AZ", s_usage);

		return	(l_status == STS$K_ERROR) ? VBACKUP$K_EXIT_ERROR : VBACKUP$K_EXIT_OK;
		}

	if ( !(1 & __cli$parse(s_verbs, CLI$M_OPSIGNAL, l_argc, l_argv, (void **) &l_clictx)) )
		return	VBACKUP$K_EXIT_ERROR;

	/* Said once, so that a mistyped name of a file is not taken for a qualifier unseen */
	for ( int i = 0; (i < l_nglued) && (i < (int) $ARRSZ(l_glued)); i++ )
		$VBKMSG(VBACKUP$_GLUED, l_glued [i]);

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

	/* A third word: most often a qualifier mistyped - ".log" for "/LOG" - and never to be ignored */
	if ( l_wordcnt > 2 )
		{
		$VBKMSG(VBACKUP$_MAXPARM, l_words [2]);
		__cli$cleanup(l_clictx);

		return	VBACKUP$K_EXIT_ERROR;
		}

	/* As it was given, for STARTED: the split below cuts it at its commas */
	vbk$strcpy(sizeof(l_inwords), l_inwords, l_words [0]);

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
	**  node::file - a saveset on another node (VBKRSH.C).  Here it is the
	**  standard input or output, joined to VBACKUP there by a pipe.
	*/
	vbk$strcpy(sizeof(l_outwords), l_outwords, l_opts.output);

	if ( (l_opts.ninput == 1) && vbk$rsh_parse(l_opts.input [0], l_rinnode, sizeof(l_rinnode), &l_rinfile) )
		l_opts.input [0] = l_dash;

	if ( vbk$rsh_parse(l_opts.output, l_routnode, sizeof(l_routnode), &l_routfile) )
		{
		/* The journal names the saveset where it is: node::file */
		vbk$strcpy(sizeof(l_opts.outnode), l_opts.outnode, l_opts.output);
		l_routfile = strdup(l_routfile);
		vbk$strcpy(sizeof(l_opts.output), l_opts.output, "-");
		}

	/*
	**  What the command means.  A saveset on the input side is known by
	**  its contents - every input must be one; on the output side by
	**  /SAVE_SET, its name or "-".  An output that is no saveset, beside
	**  an input that is none either, asks for a copy.
	*/
	l_status = STS$K_SUCCESS;

	/* A saveset of OpenVMS BACKUP: known by its first block, read by VBKVMS.C */
	l_vms = (l_opts.ninput == 1) && !s_vbk$haswild(l_opts.input [0]) && (1 & vbk$vms_isss(l_opts.input [0]));

	for ( unsigned i = 0; (i < l_opts.ninput) && (l_status == STS$K_SUCCESS); i++ )
		l_status = l_vms ? STS$K_SUCCESS : s_vbk$haswild(l_opts.input [i]) ? STS$K_WARN : vbk$rd_probe(l_opts.input [i]);

	if ( l_opts.extract [0] )
		l_opts.op	= VBACKUP$K_OP_EXTRACT;
	else if ( s_vbk$present(l_clictx, VBACKUP$K_QUAL_COMPARE, NULL) )
		l_opts.op	= VBACKUP$K_OP_COMPARE;
	else if ( (l_status == STS$K_SUCCESS) && l_opts.output [0] && (l_opts.ninput == 1) && !l_list
		&& (s_vbk$issaveset(l_clictx, l_opts.output) || s_vbk$present(l_clictx, VBACKUP$K_QUAL_TRANSFER, NULL)) )
		l_opts.op	= VBACKUP$K_OP_TRANSFER;
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

	/* /LIST of a file that is no saveset, and no output: that is what it is, not a missing output */
	if ( (l_opts.op == VBACKUP$K_OP_NONE) && l_list && !l_opts.output [0] && (l_status == STS$K_WARN) )
		l_opts.op	= VBACKUP$K_OP_LIST;

	if ( (l_opts.op == VBACKUP$K_OP_EXTRACT) || (l_opts.op == VBACKUP$K_OP_COMPARE) || (l_opts.op == VBACKUP$K_OP_LIST) )
		if ( (l_status != STS$K_SUCCESS) || (l_opts.ninput != 1) )
			{
			$VBKMSG(VBACKUP$_NOTSAVESET, l_opts.input [0]);
			__cli$cleanup(l_clictx);

			return	VBACKUP$K_EXIT_ERROR;
			}

	/* A save of what is not there makes no saveset: said by its name, nothing written */
	for ( unsigned i = 0; (l_opts.op == VBACKUP$K_OP_SAVE) && !l_opts.physical && (i < l_opts.ninput); i++ )
		{
		struct stat	l_st;

		if ( !s_vbk$haswild(l_opts.input [i]) && lstat(l_opts.input [i], &l_st) )
			{
			$VBKMSG(VBACKUP$_OPENIN, l_opts.input [i], errno, strerror(errno));
			__cli$cleanup(l_clictx);

			return	VBACKUP$K_EXIT_ERROR;
			}
		}

	/* /TRANSFER: a saveset to a saveset, block for block - and nothing else, whatever the words would mean */
	if ( s_vbk$present(l_clictx, VBACKUP$K_QUAL_TRANSFER, NULL) && (l_opts.op != VBACKUP$K_OP_TRANSFER) )
		{
		$VBKMSG(VBACKUP$_QUALUSE, "TRANSFER", "the input must be one saveset (or -), and there must be an output");
		__cli$cleanup(l_clictx);

		return	VBACKUP$K_EXIT_ERROR;
		}

	if ( l_opts.encrypt && (l_opts.op != VBACKUP$K_OP_SAVE) )
		{
		$VBKMSG(VBACKUP$_QUALUSE, "ENCRYPT", "a saveset is made encrypted by a save; one that is, is known by itself - give /KEY_FILE or nothing");
		__cli$cleanup(l_clictx);

		return	VBACKUP$K_EXIT_ERROR;
		}

	/*
	**  The standard output is gone once written - it cannot be read back
	**  for /VERIFY (nor /DELETE after it), nor listed by /LIST.  Said,
	**  rather than passed over.  Its volumes go back to back.
	*/
	if ( (l_opts.op == VBACKUP$K_OP_SAVE) && l_routnode [0] && (l_opts.delete || l_list) )
		{
		$VBKMSG(VBACKUP$_QUALUSE, l_opts.delete ? "DELETE" : "LIST", "not with a saveset made on another node");
		__cli$cleanup(l_clictx);

		return	VBACKUP$K_EXIT_ERROR;
		}

	/* A saveset on another node is verified once it is there: read back from it, compared here */
	if ( l_routnode [0] )
		{
		l_rverify	= l_opts.verify;
		l_opts.verify	= 0;
		}

	if ( (l_opts.op == VBACKUP$K_OP_SAVE) && !strcmp(l_opts.output, "-") && (l_opts.verify || l_list) )
		{
		$VBKMSG(VBACKUP$_QUALUSE, l_opts.verify ? "VERIFY" : "LIST",
			"a saveset written to the standard output is gone once written: it cannot be read back here");
		__cli$cleanup(l_clictx);

		return	VBACKUP$K_EXIT_ERROR;
		}

	/*
	**  The beginning said, and at the end how it went and how long it took
	**  - a command that has worked says so, without /LOG too.  A listing
	**  is its own answer and is left as it is.
	*/
	{
	static const char *	l_what [] = { "?", "save", "restore", "listing", "compare", "extract", "copy", "rebuild of the journal", "listing",
				"copy of a saveset" };

	l_opname = l_what [(l_opts.op < (int) $ARRSZ(l_what)) ? l_opts.op : 0];
	l_announce = (l_opts.op != VBACKUP$K_OP_LIST) && (l_opts.op != VBACKUP$K_OP_NONE) && (l_opts.op != VBACKUP$K_OP_JNLLIST);
	clock_gettime(CLOCK_MONOTONIC, &l_t0);

	if ( l_announce )
		$VBKMSG(VBACKUP$_STARTED, l_opname, strcmp(l_inwords, "-") ? l_inwords : "(standard input)",
			l_opts.output [0] || l_opts.original ? ", Output: " : "",
			l_routnode [0] ? l_outwords : !strcmp(l_opts.output, "-") ? "(standard output)" : l_opts.output [0] ? l_opts.output
			: (l_opts.op == VBACKUP$K_OP_RESTORE) && l_opts.original ? "(where its files came from)" : "");
	}

	/* The other node joined: VBACKUP there reads, or writes, the saveset through a pipe */
	if ( l_rinnode [0] && !(1 & vbk$rsh_open(l_rinnode, l_rinfile, 0, 0, &l_rinpid)) )
		l_opts.op	= VBACKUP$K_OP_NONE, l_rinnode [0] = '\0';

	if ( l_routnode [0] && (l_opts.op != VBACKUP$K_OP_NONE) && !(1 & vbk$rsh_open(l_routnode, l_routfile, 1, l_opts.replace, &l_routpid)) )
		l_opts.op	= VBACKUP$K_OP_NONE, l_routnode [0] = '\0';

	if ( l_vms && (l_opts.op != VBACKUP$K_OP_NONE) )
		{
		/* OpenVMS BACKUP: listed, restored, one file extracted - nothing else */
		if ( l_opts.op == VBACKUP$K_OP_LIST )
			l_status = vbk$vms_list(&l_opts);
		else if ( l_opts.op == VBACKUP$K_OP_EXTRACT )
			l_status = vbk$vms_extract(&l_opts);
		else if ( (l_opts.op == VBACKUP$K_OP_RESTORE) && (l_opts.physical || l_opts.image || l_opts.original || l_opts.incremental) )
			$VBKMSG(VBACKUP$_QUALUSE, l_opts.physical ? "PHYSICAL" : l_opts.image ? "IMAGE" : l_opts.original ? "ORIGINAL" : "INCREMENTAL",
				"not with a saveset of OpenVMS BACKUP: its files are restored into a directory");
		else if ( (l_opts.op == VBACKUP$K_OP_RESTORE) && !l_opts.output [0] )
			$VBKMSG(VBACKUP$_NOPARAM, "output directory");
		else if ( l_opts.op == VBACKUP$K_OP_RESTORE )
			l_status = vbk$vms_restore(&l_opts);
		else	$VBKMSG(VBACKUP$_QUALUSE, (l_opts.op == VBACKUP$K_OP_COMPARE) ? "COMPARE" : (l_opts.op == VBACKUP$K_OP_RECORD) ? "RECORD" : "TRANSFER",
				"not with a saveset of OpenVMS BACKUP: it is listed, restored or extracted - copied as a file");
		}
	else switch ( l_opts.op )
		{
		case	VBACKUP$K_OP_SAVE:
			if ( (l_opts.physical || l_opts.image) && (l_opts.ninput != 1) )
				{
				$VBKMSG(VBACKUP$_QUALUSE, l_opts.physical ? "PHYSICAL" : "IMAGE", "one input - one device or volume - a saveset");
				break;
				}

			/* /IMAGE: the volume behind the input, its identity; the input becomes its mount point */
			if ( l_opts.image && !(1 & vbk$img_prepare(&l_opts)) )
				break;

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
			if ( l_opts.original && l_opts.output [0] )
				$VBKMSG(VBACKUP$_QUALUSE, "ORIGINAL", "no output is given: the files go back where they came from");
			else if ( l_opts.original )
				l_status = vbk$restore(&l_opts);
			else if ( !l_opts.output [0] )
				$VBKMSG(VBACKUP$_NOPARAM, l_opts.physical ? "output device" : "output directory");
			else if ( l_opts.physical )
				l_status = (l_opts.ninput == 1) ? vbk$phy_restore(&l_opts) : $VBKMSG(VBACKUP$_QUALUSE, "PHYSICAL", "one saveset at a time");
			else if ( l_opts.image )
				l_status = (l_opts.ninput == 1) ? vbk$img_restore(&l_opts) : $VBKMSG(VBACKUP$_QUALUSE, "IMAGE", "one saveset at a time");
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

		case	VBACKUP$K_OP_TRANSFER:
			l_status = vbk$transfer(&l_opts);
			break;

		default:
			/* An input given that is not there: said by its name, not as a missing parameter */
			if ( !l_opts.output [0] && (l_status == STS$K_ERROR) )
				$VBKMSG(VBACKUP$_OPENIN, l_opts.input [0], ENOENT, strerror(ENOENT));
			else if ( !l_opts.output [0] )
				$VBKMSG(VBACKUP$_NOPARAM, "output specification");
			else	$VBKMSG(VBACKUP$_IVOP, "the input does not exist - and for a save the output must be named .bck or .sav, or /SAVE_SET given");
		}

	if ( l_rinnode [0] )
		vbk$rsh_close(l_rinnode, 0, l_opts.op == VBACKUP$K_OP_EXTRACT, l_rinpid);

	if ( l_routnode [0] && (1 & vbk$rsh_close(l_routnode, 1, 0, l_routpid)) && l_rverify && !vbk$errors() )
		{
		/* /VERIFY of a saveset on another node: read back from there, compared with the disk here */
		pid_t	l_vpid;

		$VBKMSG(VBACKUP$_VERIFYING, l_outwords);

		if ( 1 & vbk$rsh_open(l_routnode, l_routfile, 0, 0, &l_vpid) )
			{
			vbk$compare(&l_opts, "-");
			vbk$rsh_close(l_routnode, 0, 0, l_vpid);
			}
		}

	if ( l_announce )
		{
		struct timespec	l_t1;
		uint64_t	l_cs;

		clock_gettime(CLOCK_MONOTONIC, &l_t1);
		l_cs	= (uint64_t) ((l_t1.tv_sec - l_t0.tv_sec) * 100 + (l_t1.tv_nsec - l_t0.tv_nsec) / 10000000);

		$VBKMSG(VBACKUP$_COMPLETED, l_opname, (uint32_t) (l_cs / 100), (uint32_t) (l_cs % 100),
			vbk$errors() ? "completed with errors" : vbk$warnings() ? "completed with warnings" : "completed");
		}

	__cli$cleanup(l_clictx);

	return	vbk$exitcode();
}
