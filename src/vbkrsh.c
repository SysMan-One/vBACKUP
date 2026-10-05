#define	__MODULE__	"VBKRSH"
#define	__IDENT__	"X01-11"
#define	__REV__		"1.11.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkrsh.c
**
**  ABSTRACT:	A saveset on another node: node::file, as DECnet wrote it.
**
**  DESCRIPTION: The other node runs VBACKUP itself, through ssh (or what
**		VBACKUP_RSH names), and the saveset goes through a pipe:
**
**		    vbackup /home host::/backup/home.bck
**			here: the save to the standard output, into
**			    ssh host vbackup - '/backup/home.bck'
**			there: the stream into its volume files, every block
**			checked (VBKXFR.C)
**
**		    vbackup host::/backup/home.bck /restore
**			there: ssh host vbackup '/backup/home.bck' -
**			here: the restore from the standard input
**
**		The messages of the other side come to the standard error
**		as they are; how it ended comes back as its completion code.
**		The other node must have VBACKUP X01-11 or later on the PATH
**		of a non-interactive ssh, and let this one in by its keys.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  5-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-11		 5-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<signal.h>
#include	<unistd.h>
#include	<sys/wait.h>

#include	"vbkdef.h"


/*
**  Is a specification node::file?  The node and the file are given back.
*/
int	vbk$rsh_parse	(
	const	char *		a_spec,
		char *		a_node,
		size_t		a_nsz,
	const	char **		a_file
			)
{
const char *	l_p = strstr(a_spec, "::");

	if ( !l_p || (l_p == a_spec) || !l_p [2] || ((size_t) (l_p - a_spec) >= a_nsz) || memchr(a_spec, '/', (size_t) (l_p - a_spec)) )
		return	0;

	memcpy(a_node, a_spec, (size_t) (l_p - a_spec));
	a_node [l_p - a_spec] = '\0';

	if ( a_file )
		*a_file	= l_p + 2;

	return	1;
}


/*
**  A word for the shell of the other node: in single quotes, a quote as '\''
*/
static	size_t	s_vbk$quote	(
		char *		a_out,
		size_t		a_size,
	const	char *		a_word
			)
{
size_t	l_n = 0;

	if ( l_n < a_size )
		a_out [l_n++] = '\'';

	for ( ; *a_word && (l_n + 5 < a_size); a_word++ )
		{
		if ( *a_word == '\'' )
			{
			memcpy(a_out + l_n, "'\\''", 4);
			l_n	+= 4;
			}
		else	a_out [l_n++] = *a_word;
		}

	if ( l_n < a_size )
		a_out [l_n++] = '\'';

	a_out [(l_n < a_size) ? l_n : (a_size - 1)] = '\0';

	return	l_n;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Start VBACKUP on the other node and join it to this one by a pipe:
**	an output - what this image writes to its standard output goes to the
**	other one's standard input, a saveset made there; an input - the
**	other one writes the saveset to its standard output, and this image
**	reads it from its standard input.
**
**  FORMAL PARAMETERS:
**
**	a_node		The node, user@host as ssh takes it
**	a_file		The saveset there
**	a_output	1 - the saveset is made there, 0 - read from there
**	a_replace	/REPLACE, passed on
**	a_pid		Receives the process of ssh
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the condition reported.
**--
*/
int	vbk$rsh_open	(
	const	char *		a_node,
	const	char *		a_file,
		int		a_output,
		int		a_replace,
		pid_t *		a_pid
			)
{
const char *	l_rsh = getenv("VBACKUP_RSH");
char		l_cmd [VBACKUP$K_SZ_PATH * 2], l_q [VBACKUP$K_SZ_PATH * 2];
int		l_pipe [2];

	l_rsh	= (l_rsh && *l_rsh) ? l_rsh : "ssh";
	s_vbk$quote(l_q, sizeof(l_q), a_file);

	/* /TRANSFER: a VBACKUP there before X01-11 refuses it - it would take "- file" for a restore into a directory */
	if ( a_output )
		$VBKFAOB(l_cmd, sizeof(l_cmd), "vbackup - !AZ /TRANSFER!AZ", l_q, a_replace ? " /REPLACE" : "");
	else	$VBKFAOB(l_cmd, sizeof(l_cmd), "vbackup !AZ - /TRANSFER", l_q);

	if ( pipe(l_pipe) )
		return	$VBKMSG(VBACKUP$_REMOTE, a_node, errno, strerror(errno));

	if ( 0 > (*a_pid = fork()) )
		return	$VBKMSG(VBACKUP$_REMOTE, a_node, errno, strerror(errno));

	if ( !*a_pid )
		{
		/* The child: ssh, its standard input (an output) or output (an input) the pipe */
		dup2(l_pipe [a_output ? 0 : 1], a_output ? STDIN_FILENO : STDOUT_FILENO);
		close(l_pipe [0]);
		close(l_pipe [1]);
		execlp(l_rsh, l_rsh, a_node, l_cmd, (char *) NULL);
		_exit(127);
		}

	/* Here: the other end of the pipe becomes the standard output, or input */
	if ( 0 > dup2(l_pipe [a_output ? 1 : 0], a_output ? STDOUT_FILENO : STDIN_FILENO) )
		return	$VBKMSG(VBACKUP$_REMOTE, a_node, errno, strerror(errno));

	close(l_pipe [0]);
	close(l_pipe [1]);

	/* An input read only in part (/EXTRACT found its file): the other side may stop on a broken pipe */
	signal(SIGPIPE, SIG_IGN);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The pipe to the other node closed, and how it ended there: its
**	completion code is this one's too (0, 1 a warning, else an error).
**
**  RETURN VALUE:
**	STS$K_SUCCESS - it completed; STS$K_WARN - with warnings; STS$K_ERROR
**	- it failed, reported.
**--
*/
int	vbk$rsh_close	(
	const	char *		a_node,
		int		a_output,
		int		a_early,
		pid_t		a_pid
			)
{
int	l_wst = 0, l_code, l_null;

	/* The pipe closed - the other side sees its end - and the descriptor kept taken: /dev/null in its place */
	if ( 0 <= (l_null = open("/dev/null", a_output ? O_WRONLY : O_RDONLY)) )
		{
		dup2(l_null, a_output ? STDOUT_FILENO : STDIN_FILENO);
		close(l_null);
		}
	else	close(a_output ? STDOUT_FILENO : STDIN_FILENO);

	while ( (0 > waitpid(a_pid, &l_wst, 0)) && (errno == EINTR) )
		;

	if ( WIFSIGNALED(l_wst) )
		{
		/* An input this side stopped reading early: the other end was cut off, and that is no fault */
		if ( !a_output && a_early && (WTERMSIG(l_wst) == SIGPIPE) )
			return	STS$K_SUCCESS;

		return	$VBKMSG(VBACKUP$_REMOTEERR, a_node, 128 + WTERMSIG(l_wst));
		}

	l_code	= WIFEXITED(l_wst) ? WEXITSTATUS(l_wst) : 255;

	if ( !l_code )
		return	STS$K_SUCCESS;

	if ( l_code == VBACKUP$K_EXIT_WARN )
		{
		vbk$warned();

		return	STS$K_WARN;
		}

	/* An input stopped early: the other side may have failed on the pipe it lost */
	if ( !a_output && a_early )
		return	STS$K_SUCCESS;

	return	$VBKMSG(VBACKUP$_REMOTEERR, a_node, l_code);
}
