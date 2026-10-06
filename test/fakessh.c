#define	__MODULE__	"FAKESSH"
#define	__IDENT__	"X01-18"
#define	__REV__		"1.18.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	fakessh.c
**
**  ABSTRACT:	A stand-in for ssh.exe in test/winutil.sh: "fakessh node
**		command" runs the command here - the words of a shell, in
**		single quotes as VBACKUP quotes them - with "vbackup" taken
**		for FAKESSH_VBACKUP, the standard handles inherited.  Its
**		completion code is that of the command, as with ssh.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  6-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-18		 6-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<windows.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#define	FAKESSH$K_LINE	16384

/*
**  Append a word to a command line of Windows, quoted
*/
static	size_t	s_fakessh$quote	(
		char *		a_out,
		size_t		a_pos,
	const	char *		a_word,
		size_t		a_len
			)
{
	a_out [a_pos++] = '"';

	for ( size_t i = 0; (i < a_len) && (a_pos < (FAKESSH$K_LINE - 4)); i++ )
		{
		if ( a_word [i] == '"' )
			a_out [a_pos++] = '\\';

		a_out [a_pos++] = a_word [i];
		}

	a_out [a_pos++] = '"';
	a_out [a_pos++] = ' ';
	a_out [a_pos]	= '\0';

	return	a_pos;
}

int	main	(
		int		a_argc,
		char **		a_argv
			)
{
static	char		l_line [FAKESSH$K_LINE], l_word [FAKESSH$K_LINE];
static	wchar_t		l_wline [FAKESSH$K_LINE];
const char *		l_image = getenv("FAKESSH_VBACKUP"), *l_p;
STARTUPINFOW		l_si = { .cb = sizeof(l_si) };
PROCESS_INFORMATION	l_pi;
DWORD			l_code = 255;
size_t			l_pos = 0, l_wl;
int			l_first = 1;

	if ( (a_argc != 3) || !l_image )
		return	fprintf(stderr, "usage: fakessh node command (FAKESSH_VBACKUP=vbackup.exe)\n"), 255;

	/* The words of the command, as a shell takes them apart: blanks between, '...' quoted, '\'' a quote */
	for ( l_p = a_argv [2]; *l_p; )
		{
		while ( *l_p == ' ' )
			l_p++;

		if ( !*l_p )
			break;

		for ( l_wl = 0; *l_p && (*l_p != ' '); )
			{
			if ( *l_p == '\'' )
				{
				for ( l_p++; *l_p && (*l_p != '\''); )
					l_word [l_wl++] = *l_p++;

				if ( *l_p )
					l_p++;
				}
			else if ( (*l_p == '\\') && l_p [1] )
				l_word [l_wl++] = l_p [1], l_p += 2;
			else	l_word [l_wl++] = *l_p++;
			}

		if ( l_first && (l_wl == 7) && !memcmp(l_word, "vbackup", 7) )
			l_pos = s_fakessh$quote(l_line, l_pos, l_image, strlen(l_image));
		else	l_pos = s_fakessh$quote(l_line, l_pos, l_word, l_wl);

		l_first	= 0;
		}

	if ( !MultiByteToWideChar(CP_UTF8, 0, l_line, -1, l_wline, FAKESSH$K_LINE) )
		return	255;

	if ( !CreateProcessW(NULL, l_wline, NULL, NULL, TRUE, 0, NULL, NULL, &l_si, &l_pi) )
		return	fprintf(stderr, "fakessh: cannot run %s, error %lu\n", l_line, GetLastError()), 255;

	WaitForSingleObject(l_pi.hProcess, INFINITE);
	GetExitCodeProcess(l_pi.hProcess, &l_code);
	CloseHandle(l_pi.hProcess);
	CloseHandle(l_pi.hThread);

	return	(int) l_code;
}
