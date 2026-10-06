#define	__MODULE__	"VBKKEY"
#define	__IDENT__	"X01-17"
#define	__REV__		"1.17.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkkey.c
**
**  ABSTRACT:	The passphrase of an encrypted saveset (format.md 6.10):
**		where it comes from, and the opening of a saveset with it.
**
**  DESCRIPTION: A passphrase never comes from the command line - the
**		command is stored in the SUMMARY and shown by ps.  It comes
**		from a key file (/KEY_FILE=file, else VBACKUP_KEY_FILE), its
**		first line, the file readable by its owner only; else from
**		the terminal, /dev/tty, without echo - twice for a save, so
**		that a slip of a finger does not make a saveset nobody can
**		open.  With neither, a saveset that needs one is not read
**		and none is made: the command never waits on a terminal it
**		does not have - nor on one it must not use: VBACKUP_NOPROMPT=1,
**		set by the plugins of the file managers.
**
**		The passphrase is asked for once per command and kept for
**		the savesets that follow (a /VERIFY after the save, the
**		chain of an /INCREMENTAL restore); it is wiped at exit.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  5-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-17		 6-OCT-2026	RRL
**		Windows: the passphrase from the console - ReadConsoleW, no echo, in
**		UTF-8 as on Linux, or a saveset made on one would not open on the other;
**		the mode of a key file is not looked at there - its ACL rules it.
**
**	X01-07		 5-OCT-2026	RRL
**		The prompt made by FAO.
**
**	X01-06		 5-OCT-2026	RRL
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
#include	<sys/stat.h>

#ifndef	_WIN32
#include	<termios.h>
#endif

#include	"vbkdef.h"
#include	"vbkrd.h"
#include	"vbkcrp.h"

static	char		s_pass [VBK$K_PASSMAX + 2];	/* The passphrase of this command	*/
static	size_t		s_plen;
static	int		s_have;

#ifndef	_WIN32
static	int		s_ttyfd = -1;		/* The terminal while its echo is off		*/
static	struct termios	s_ttysave;
#endif


/*
**  Wipe the passphrase - at exit, whatever the way out
*/
void	vbk$key_wipe	(void)
{
	vbk$crp_wipe(s_pass, sizeof(s_pass));
	s_plen	= 0;
	s_have	= 0;
}


#ifdef	_WIN32
/*
**  One line from the console of Windows, no echo: <a_fd> is CONIN$.
**  Read in UTF-16 and made UTF-8 - the passphrase must be the same octets
**  as the one typed on Linux.  Returns its length, -1 - none.
*/
static	int	s_vbk$ttyread	(
		int		a_fd,
	const	char *		a_prompt,
		char *		a_buf,
		size_t		a_size
			)
{
HANDLE	l_in = (HANDLE) _get_osfhandle(a_fd), l_out;
DWORD	l_mode, l_n = 0;
wchar_t	l_w [1100];
int	l_len;

	if ( (l_in == INVALID_HANDLE_VALUE) || !GetConsoleMode(l_in, &l_mode) )
		return	-1;

	if ( INVALID_HANDLE_VALUE != (l_out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL)) )
		WriteConsoleA(l_out, a_prompt, (DWORD) strlen(a_prompt), NULL, NULL);

	SetConsoleMode(l_in, (l_mode | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT) & ~ENABLE_ECHO_INPUT);

	if ( !ReadConsoleW(l_in, l_w, (DWORD) (sizeof(l_w) / sizeof(l_w [0]) - 1), &l_n, NULL) )
		l_n	= 0;

	SetConsoleMode(l_in, l_mode);

	if ( l_out != INVALID_HANDLE_VALUE )
		{
		WriteConsoleA(l_out, "\n", 1, NULL, NULL);
		CloseHandle(l_out);
		}

	/* No end of line read: longer than the buffer, or ^Z */
	if ( !l_n || (l_w [l_n - 1] != L'\n') )
		return	vbk$crp_wipe(l_w, sizeof(l_w)), -1;

	while ( l_n && ((l_w [l_n - 1] == L'\n') || (l_w [l_n - 1] == L'\r')) )
		l_n--;

	l_len	= l_n ? WideCharToMultiByte(CP_UTF8, 0, l_w, (int) l_n, a_buf, (int) a_size - 1, NULL, NULL) : 0;
	vbk$crp_wipe(l_w, sizeof(l_w));

	if ( l_n && (l_len <= 0) )
		return	-1;

	a_buf [l_len] = '\0';

	return	l_len;
}

#else
/*
**  ^C or ^Z while the echo is off: the terminal is given back as it was
*/
static	void	s_vbk$ttysig	(
		int		a_sig
			)
{
	if ( s_ttyfd >= 0 )
		tcsetattr(s_ttyfd, TCSAFLUSH, &s_ttysave);

	vbk$crp_wipe(s_pass, sizeof(s_pass));
	signal(a_sig, SIG_DFL);
	raise(a_sig);
}


/*
**  One line from the terminal, no echo.  Returns its length, -1 - none.
*/
static	int	s_vbk$ttyread	(
		int		a_fd,
	const	char *		a_prompt,
		char *		a_buf,
		size_t		a_size
			)
{
struct termios	l_t;
struct sigaction l_sa = {0}, l_oint, l_oterm, l_otstp, l_ohup;
size_t		l_n = 0;
char		l_c;
ssize_t		l_rc;

	if ( tcgetattr(a_fd, &s_ttysave) )
		return	-1;

	l_t		= s_ttysave;
	l_t.c_lflag	&= ~(tcflag_t) (ECHO | ECHOE | ECHOK | ECHONL);
	l_t.c_lflag	|= ICANON;

	l_sa.sa_handler	= s_vbk$ttysig;
	sigemptyset(&l_sa.sa_mask);
	sigaction(SIGINT, &l_sa, &l_oint);
	sigaction(SIGTERM, &l_sa, &l_oterm);
	sigaction(SIGTSTP, &l_sa, &l_otstp);
	sigaction(SIGHUP, &l_sa, &l_ohup);

	s_ttyfd	= a_fd;

	if ( write(a_fd, a_prompt, strlen(a_prompt)) < 0 )
		l_n = 0;

	tcsetattr(a_fd, TCSAFLUSH, &l_t);

	for ( ;; )
		{
		if ( 1 != (l_rc = read(a_fd, &l_c, 1)) )
			{
			if ( (l_rc < 0) && (errno == EINTR) )
				continue;

			break;
			}

		if ( l_c == '\n' )
			break;

		if ( l_n < (a_size - 1) )
			a_buf [l_n++] = l_c;
		else	l_n = a_size;		/* Too long: refused below */
		}

	tcsetattr(a_fd, TCSAFLUSH, &s_ttysave);
	s_ttyfd	= -1;

	if ( write(a_fd, "\n", 1) < 0 )
		l_rc = -1;

	sigaction(SIGINT, &l_oint, NULL);
	sigaction(SIGTERM, &l_oterm, NULL);
	sigaction(SIGTSTP, &l_otstp, NULL);
	sigaction(SIGHUP, &l_ohup, NULL);

	if ( l_n >= a_size )
		return	-1;

	if ( l_n && (a_buf [l_n - 1] == '\r') )
		l_n--;

	a_buf [l_n] = '\0';

	return	(int) l_n;
}
#endif


/*
**  The first line of a key file, the file readable by its owner only
*/
static	int	s_vbk$keyfile	(
	const	char *		a_spec
			)
{
struct stat	l_st;
char		l_buf [VBK$K_PASSMAX + 2];
ssize_t		l_rc;
size_t		l_n;
int		l_fd;

	if ( 0 > (l_fd = open(a_spec, O_RDONLY | O_CLOEXEC)) )
		return	$VBKMSG(VBACKUP$_KEYFILE, a_spec, strerror(errno));

	if ( fstat(l_fd, &l_st) || !S_ISREG(l_st.st_mode) )
		{
		close(l_fd);

		return	$VBKMSG(VBACKUP$_KEYFILE, a_spec, "not a regular file");
		}

#ifndef	_WIN32
	/* On Windows its ACL says who may read it, not a mode: the profile of the user is the place for it */
	if ( l_st.st_mode & (S_IRWXG | S_IRWXO) )
		{
		close(l_fd);

		return	$VBKMSG(VBACKUP$_KEYFILE, a_spec, "others may read or change it - chmod 600 it");
		}
#endif

	l_rc	= read(l_fd, l_buf, sizeof(l_buf));
	close(l_fd);

	if ( l_rc < 0 )
		return	$VBKMSG(VBACKUP$_KEYFILE, a_spec, strerror(errno));

	/* The first line, without its end - LF or CR LF */
	for ( l_n = 0; (l_n < (size_t) l_rc) && (l_buf [l_n] != '\n'); l_n++ )
		;

	if ( l_n > VBK$K_PASSMAX )
		{
		vbk$crp_wipe(l_buf, sizeof(l_buf));

		return	$VBKMSG(VBACKUP$_KEYFILE, a_spec, "its first line is longer than 1024 bytes");
		}

	if ( l_n && (l_buf [l_n - 1] == '\r') )
		l_n--;

	if ( !l_n )
		return	$VBKMSG(VBACKUP$_KEYFILE, a_spec, "its first line is empty");

	memcpy(s_pass, l_buf, l_n);
	s_plen	= l_n;
	vbk$crp_wipe(l_buf, sizeof(l_buf));

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	The passphrase of this command: from the key file, else from the
**	terminal - asked twice when a saveset is to be made with it.
**
**  FORMAL PARAMETERS:
**
**	a_opts		/KEY_FILE
**	a_what		The saveset, for the prompt and the messages
**	a_confirm	A save: asked twice, the two must agree
**	a_pass, a_plen	Receive the passphrase - the module's copy, not to be freed
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the condition already reported.
**--
*/
int	vbk$key_get	(
		VBK$OPTS *	a_opts,
	const	char *		a_what,
		int		a_confirm,
	const	char **		a_pass,
		size_t *	a_plen
			)
{
const char *	l_kf = a_opts->keyfile [0] ? a_opts->keyfile : getenv("VBACKUP_KEY_FILE");
char		l_prompt [VBACKUP$K_SZ_PATH + 64], l_again [VBK$K_PASSMAX + 2];
int		l_fd, l_n, l_m;

	if ( !s_have )
		{
		atexit(vbk$key_wipe);

		if ( l_kf && *l_kf )
			{
			if ( !(1 & s_vbk$keyfile(l_kf)) )
				return	STS$K_ERROR;
			}
		else	{
			/* VBACKUP_NOPROMPT=1: a file manager runs the command - a question on its screen would hang it */
			if ( getenv("VBACKUP_NOPROMPT") && !strcmp(getenv("VBACKUP_NOPROMPT"), "1") )
				return	$VBKMSG(VBACKUP$_NOKEY, a_what);

			if ( 0 > (l_fd = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC)) )
				return	$VBKMSG(VBACKUP$_NOKEY, a_what);

			$VBKFAOB(l_prompt, sizeof(l_prompt), "Passphrase for !AZ: ", a_what);
			l_n	= s_vbk$ttyread(l_fd, l_prompt, s_pass, sizeof(s_pass));

			if ( (l_n > 0) && a_confirm )
				{
				l_m	= s_vbk$ttyread(l_fd, "The same passphrase again: ", l_again, sizeof(l_again));

				if ( (l_m != l_n) || memcmp(l_again, s_pass, (size_t) l_n) )
					l_n = -2;

				vbk$crp_wipe(l_again, sizeof(l_again));
				}

			close(l_fd);

			if ( l_n == -2 )
				return	vbk$key_wipe(), $VBKMSG(VBACKUP$_KEYMATCH);

			if ( l_n <= 0 )
				return	vbk$key_wipe(), $VBKMSG(VBACKUP$_KEYFILE, "/dev/tty", l_n ? "longer than 1024 bytes, or no line" : "an empty passphrase");

			s_plen	= (size_t) l_n;
			}

		s_have	= 1;
		}

	*a_pass	= s_pass;
	*a_plen	= s_plen;

	return	STS$K_SUCCESS;
}


/*
**  The PBKDF2 iterations of a save: VBACKUP_KDFITER, not below the
**  least a reader takes - for tests on slow machines; else the default
*/
uint32_t	vbk$key_iter	(void)
{
const char *	l_env = getenv("VBACKUP_KDFITER");
unsigned long	l_n;

	if ( l_env && *l_env && ((l_n = strtoul(l_env, NULL, 10)) >= VBK$K_KDFMIN) && (l_n <= 0xFFFFFFFFUL) )
		return	(uint32_t) l_n;

	return	VBK$K_KDFITER;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Open a saveset just opened by VBK$RD_OPEN, when it is encrypted:
**	the passphrase is had and judged.  Of no effect on a plain one.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- it can be read;
**	STS$K_WARN	- it can, sequentially only: its TRAILER fails;
**	STS$K_ERROR	- it cannot, reported.
**--
*/
int	vbk$key_unlock	(
		VBK$OPTS *	a_opts,
		VBK$RCTX *	a_rctx,
	const	char *		a_spec
			)
{
const char *	l_pass;
size_t		l_plen;
int		l_status;

	if ( !a_rctx->crypt )
		return	STS$K_SUCCESS;

	if ( !(1 & vbk$key_get(a_opts, a_spec, 0, &l_pass, &l_plen)) )
		return	STS$K_ERROR;

	if ( STS$K_ERROR == (l_status = vbk$rd_setkey(a_rctx, l_pass, l_plen)) )
		{
		/* Not kept: the next saveset of the command may be asked for again */
		vbk$key_wipe();

		return	$VBKMSG(VBACKUP$_WRONGKEY, a_spec);
		}

	return	l_status;
}
