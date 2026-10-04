#define	__MODULE__	"VBKHLP"
#define	__IDENT__	"X01-01"
#define	__REV__		"1.1.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkhlp.c
**
**  ABSTRACT:	/HELP out of the help library of the product, through the
**		reading API of the HELP package (HELP::help).  Built only
**		when HELP was found at configuration time; otherwise a
**		stand-in reports STS$K_WARN and the summary built into the
**		image answers.
**
**		The library is looked for: the file VBACKUP_HELPLIB names;
**		<image>/../share/help/vbackup.hlb; vbackup.hlb beside the
**		image (a build tree); where the product was configured to
**		install it.
**
**		Taken over from SRCHHLP.C of SEARCH X01-31.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<sys/stat.h>

#include	"vbkdef.h"

#ifdef	VBACKUP_WITH_HLB

/*
**  HELPDEF.H of the HELP product defines __MODULE__ and the rest only
**  when they are absent, so the identification of this module stands.
*/
#include	"helpdef.h"

/*
**  The environment variable that overrides the search, and the name of
**  the library within a directory of it.
*/
#define	VBACKUP$K_HLBLOG	"VBACKUP_HELPLIB"
#define	VBACKUP$K_HLBNAME	"vbackup.hlb"

/*
**  The top level key of the library: everything the utility documents
**  hangs below it.
*/
#define	VBACKUP$K_HLBTOP	"VBACKUP"


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Tell whether a file specification names a readable regular file.
**
**  FORMAL PARAMETERS:
**
**	a_fspec		The specification, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Reads the file system.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the file is there and can be read;
**	STS$K_WARN	- it is not.
**--
*/
static	int	s_vbk$readable	(
	const	char *		a_fspec
			)
{
struct stat	l_st = {0};

	if ( !a_fspec || !*a_fspec )
		return	STS$K_WARN;

	if ( stat(a_fspec, &l_st) || !S_ISREG(l_st.st_mode) )
		return	STS$K_WARN;

	return	access(a_fspec, R_OK) ? STS$K_WARN : STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Locate the help library of the product.  The search is described in
**	the abstract of the module; the first candidate that is a readable
**	file wins.
**
**  FORMAL PARAMETERS:
**
**	a_bufsz		Capacity of the buffer
**	a_buf		Receives the specification, ASCIZ
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Reads the environment and the file system.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- <a_buf> holds the specification of a library;
**	STS$K_WARN	- no library can be found.
**--
*/
static	int	s_vbk$findlib	(
		size_t		a_bufsz,
		char *		a_buf
			)
{
const char *	l_env;
char		l_image [VBACKUP$K_SZ_PATH] = {0};
char *		l_slash;
ssize_t		l_len;

	/* 1. The environment variable: a library, taken as it stands */
	if ( (l_env = getenv(VBACKUP$K_HLBLOG)) && *l_env )
		{
		vbk$strcpy(a_bufsz, a_buf, l_env);

		if ( 1 & s_vbk$readable(a_buf) )
			return	STS$K_SUCCESS;
		}

	/*
	**  2. Beside the image.  The path of the running program is read
	**  from /proc/self/exe rather than taken from argv[0]: the latter
	**  is whatever the caller chose to pass, and a utility invoked
	**  through PATH does not carry a directory in it at all.
	*/
	if ( 0 < (l_len = readlink("/proc/self/exe", l_image, sizeof(l_image) - 1)) )
		{
		l_image [l_len]	= '\0';

		if ( (l_slash = strrchr(l_image, '/')) )
			{
			*l_slash	= '\0';

			/* <image>/../share/help/vbackup.hlb - an installed tree */
			vbk$strcpy(a_bufsz, a_buf, l_image);
			vbk$strcpy(a_bufsz - strlen(a_buf), a_buf + strlen(a_buf),
				"/../share/help/" VBACKUP$K_HLBNAME);

			if ( 1 & s_vbk$readable(a_buf) )
				return	STS$K_SUCCESS;

			/* <image>/vbackup.hlb - a build tree, where both are made */
			vbk$strcpy(a_bufsz, a_buf, l_image);
			vbk$strcpy(a_bufsz - strlen(a_buf), a_buf + strlen(a_buf),
				"/" VBACKUP$K_HLBNAME);

			if ( 1 & s_vbk$readable(a_buf) )
				return	STS$K_SUCCESS;
			}
		}

	/* 3. Where the product was configured to install the library */
#ifdef	VBACKUP_K_HLBSPEC
	vbk$strcpy(a_bufsz, a_buf, VBACKUP_K_HLBSPEC);

	if ( 1 & s_vbk$readable(a_buf) )
		return	STS$K_SUCCESS;
#endif

	a_buf [0]	= '\0';

	return	STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Descend one level of the topic tree.  The word, or the run of words
**	that a key carrying blanks is made of, is looked up as it has been
**	given; failing that it is looked up once more with a slash in front
**	of it, so that a qualifier is asked for as "verify" as well as as
**	"/verify".
**
**  FORMAL PARAMETERS:
**
**	a_lib		The library
**	a_parent	Index of the topic to descend from
**	a_wordcnt	Number of the words left
**	a_words		The words themselves
**	a_used		Receives the number of the words consumed
**	a_index		Receives the index of the topic found
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the topic has been found;
**	STS$K_WARN	- no subtopic of this name.
**--
*/
static	int	s_vbk$descend	(
		HLB$LIB *	a_lib,
		int		a_parent,
		int		a_wordcnt,
		char **		a_words,
		int *		a_used,
		int *		a_index
			)
{
char		l_slashed [VBACKUP$K_SZ_STR] = {'/'};
char *		l_word [1];

	if ( 1 & hlb$lookup_run(a_lib, a_parent, a_wordcnt, a_words, a_used, a_index) )
		return	STS$K_SUCCESS;

	/* The same word, spelled the way the qualifier is */
	if ( a_words[0][0] == '/' )
		return	STS$K_WARN;

	vbk$strcpy(sizeof(l_slashed) - 1, l_slashed + 1, a_words [0]);

	l_word [0]	= l_slashed;

	if ( !(1 & hlb$lookup_run(a_lib, a_parent, 1, l_word, a_used, a_index)) )
		return	STS$K_WARN;

	*a_used	= 1;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Answer /HELP out of the help library of the product: locate the
**	library, descend to the topic that has been asked for and display
**	its text, the path of the topic standing as the heading.
**
**  FORMAL PARAMETERS:
**
**	a_wordcnt	Number of the words naming the topic, 0 - the whole
**			description of the utility
**	a_words		The words themselves
**	a_page		Non-zero - stop at the end of a page, /PAGE
**
**  IMPLICIT INPUTS/OUTPUTS:
**	Reads the library, writes to the standard output.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- the topic has been displayed;
**	STS$K_WARN	- no library can be found, the caller is to display
**			  the summary built into the image;
**	STS$K_ERROR	- the library is there but names no such topic.
**--
*/
int	vbk$help	(
		int		a_wordcnt,
		char **		a_words,
		int		a_page
			)
{
HLB$LIB *	l_lib = NULL;
HELP$OUT	l_out;
const char *	l_txt = NULL;
char		l_fspec [VBACKUP$K_SZ_PATH] = {0}, l_path [VBACKUP$K_SZ_PATH] = {0};
unsigned	l_txtlen = 0, l_pathlen = 0;
int		l_parent, l_index, l_used, l_pos = 0;

	if ( !(1 & s_vbk$findlib(sizeof(l_fspec), l_fspec)) )
		return	STS$K_WARN;

	if ( !(1 & hlb$open(l_fspec, &l_lib)) )
		return	STS$K_WARN;

	/*
	**  Everything this library documents hangs below the one key of
	**  level one, so the descent starts there rather than at the root:
	**  the reader asks for a qualifier, not for "vbackup /verify".
	*/
	if ( !(1 & hlb$lookup(l_lib, -1, (unsigned) sizeof(VBACKUP$K_HLBTOP) - 1, VBACKUP$K_HLBTOP, &l_parent)) )
		{
		hlb$close(l_lib);

		return	STS$K_WARN;
		}

	while ( l_pos < a_wordcnt )
		{
		if ( !(1 & s_vbk$descend(l_lib, l_parent, a_wordcnt - l_pos, a_words + l_pos, &l_used, &l_index)) )
			{
			$VBKMSG(VBACKUP$_NOTOPIC, a_words [l_pos]);
			hlb$close(l_lib);

			return	STS$K_ERROR;
			}

		l_parent	= l_index;
		l_pos		+= l_used;
		}

	if ( !(1 & hlb$gettext(l_lib, l_parent, &l_txtlen, &l_txt)) )
		{
		hlb$close(l_lib);

		return	STS$K_WARN;
		}

	/*
	**  The display is the one of the HELP subsystem, so that a topic
	**  looks here exactly as it looks under the browser - the indent of
	**  a level, the width of the terminal and the pager included.
	*/
	help$out_init(&l_out, STDOUT_FILENO, STDIN_FILENO, a_page, 0);

	if ( 1 & hlb$path(l_lib, l_parent, (unsigned) sizeof(l_path), l_path, &l_pathlen) )
		help$put(&l_out, "\n  !AD\n\n", 2, (fao_prm_t) l_pathlen, (fao_prm_t) l_path);

	help$put_text(&l_out, 4, l_txtlen, l_txt);
	help$put(&l_out, "\n", 0);
	help$flush(&l_out);

	hlb$close(l_lib);

	return	STS$K_SUCCESS;
}

#else	/* VBACKUP_WITH_HLB */

/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The stand-in of the routine above, compiled when the HELP product
**	was not found: there is no library to read, so the caller displays
**	the summary built into the image.
**
**  FORMAL PARAMETERS:
**
**	a_wordcnt	Number of the words naming the topic
**	a_words		The words themselves
**	a_page		Non-zero - stop at the end of a page, /PAGE
**
**  IMPLICIT INPUTS/OUTPUTS:
**	None.
**
**  RETURN VALUE:
**	STS$K_WARN always.
**--
*/
int	vbk$help	(
		int		a_wordcnt,
		char **		a_words,
		int		a_page
			)
{
	return	STS$K_WARN;
}

#endif	/* VBACKUP_WITH_HLB */
