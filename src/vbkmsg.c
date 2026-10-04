#define	__MODULE__	"VBKMSG"
#define	__IDENT__	"X01-03"
#define	__REV__		"1.3.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkmsg.c
**
**  ABSTRACT:	The message catalogue of the facility and the services
**		every module shares: the count of the conditions signalled,
**		the completion code, strings, times, patterns, /CONFIRM and
**		the joining of a stored name to a directory.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-03		 3-OCT-2026	RRL
**		FILLOST and UNNAMED: the files a damaged restore lost.  The
**		messages of /PHYSICAL, /IMAGE, /ORIGINAL and /DELETE.
**
**	X01-02		 3-OCT-2026	RRL
**		The messages of stage 2: the journal, /INCREMENTAL, the copy.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<time.h>

#include	"vbkdef.h"

/*
**  The catalogue.  Each text is "NAME, FAO control string"; the envelope -
**  %VBACKUP-W-NAME - is put on by UTIL$PUTMSG_FAO.  The array is not const:
**  UTIL$INIMSG sorts it in place.
**
**  The parameters of the macro are named A_ on purpose: a designated
**  initializer is macro-expanded, and a parameter spelled like a member
**  of EMSG_RECORD would replace the member.
*/
#define	$VBKREC(a_sts, a_txt)	{ .sts = (int) (a_sts), .textl = (unsigned char) (sizeof(a_txt) - 1), .text = a_txt }

static	EMSG_RECORD	s_msgtab [] = {
	$VBKREC(VBACKUP$_NORMAL,	"NORMAL, normal successful completion"),
	$VBKREC(VBACKUP$_CREATED,	"CREATED, !AZ created"),
	$VBKREC(VBACKUP$_SAVED,		"SAVED, !AZ saved"),
	$VBKREC(VBACKUP$_RESTORED,	"RESTORED, !AZ restored"),
	$VBKREC(VBACKUP$_COMPARED,	"COMPARED, !AZ compared"),
	$VBKREC(VBACKUP$_SKIPPED,	"SKIPPED, !AZ skipped, !AZ"),
	$VBKREC(VBACKUP$_FILEEXISTS,	"FILEEXISTS, !AZ already exists, not restored"),
	$VBKREC(VBACKUP$_FILCHANGED,	"FILCHANGED, !AZ changed while it was being saved"),
	$VBKREC(VBACKUP$_OPENIN,	"OPENIN, error opening !AZ as input, errno=!SL (!AZ)"),
	$VBKREC(VBACKUP$_OPENOUT,	"OPENOUT, error creating !AZ as output, errno=!SL (!AZ)"),
	$VBKREC(VBACKUP$_READERR,	"READERR, error reading !AZ, errno=!SL (!AZ)"),
	$VBKREC(VBACKUP$_WRITERR,	"WRITERR, error writing !AZ, errno=!SL (!AZ)"),
	$VBKREC(VBACKUP$_OPENDIR,	"OPENDIR, error reading the directory !AZ, errno=!SL (!AZ)"),
	$VBKREC(VBACKUP$_NOTSAVESET,	"NOTSAVESET, !AZ is not a saveset"),
	$VBKREC(VBACKUP$_WRONGVOL,	"WRONGVOL, volume !UL of !AZ belongs to another saveset, ignored"),
	$VBKREC(VBACKUP$_MISSVOL,	"MISSVOL, volume !UL of !AZ is missing"),
	$VBKREC(VBACKUP$_NOTRAILER,	"NOTRAILER, !AZ has no trailer: the save did not complete, or its last volume is missing"),
	$VBKREC(VBACKUP$_BLKFIXED,	"BLKFIXED, block !UQ of volume !UL was bad and has been rebuilt from its group"),
	$VBKREC(VBACKUP$_BLKLOST,	"BLKLOST, block !UQ of volume !UL is bad and cannot be rebuilt"),
	$VBKREC(VBACKUP$_BADREC,	"BADREC, invalid record in block !UQ of volume !UL, skipped"),
	$VBKREC(VBACKUP$_FILDAMAGED,	"FILDAMAGED, !AZ is incomplete: its data was lost in bad blocks"),
	$VBKREC(VBACKUP$_CRCERR,	"CRCERR, !AZ: checksum mismatch, the data differ from what was saved"),
	$VBKREC(VBACKUP$_COMPARERR,	"COMPARERR, !AZ: !AZ"),
	$VBKREC(VBACKUP$_ATTRERR,	"ATTRERR, !AZ: !AZ not restored, errno=!SL (!AZ)"),
	$VBKREC(VBACKUP$_UNSUPP,	"UNSUPP, !AZ: !AZ not restored, errno=!SL (!AZ)"),
	$VBKREC(VBACKUP$_NOTFOUND,	"NOTFOUND, !AZ is not in the saveset"),
	$VBKREC(VBACKUP$_NOFILES,	"NOFILES, no file matches !AZ"),
	$VBKREC(VBACKUP$_TOODEEP,	"TOODEEP, !AZ, nesting exceeds !UL levels, not descended"),
	$VBKREC(VBACKUP$_NOMEM,		"NOMEM, cannot allocate memory, errno=!SL (!AZ)"),
	$VBKREC(VBACKUP$_IVQUAL,	"IVQUAL, illegal value !AZ for the qualifier /!AZ"),
	$VBKREC(VBACKUP$_IVTIME,	"IVTIME, illformed time value !AZ for the qualifier /!AZ"),
	$VBKREC(VBACKUP$_CONFQUAL,	"CONFQUAL, /!AZ and /!AZ cannot be given together"),
	$VBKREC(VBACKUP$_NOPARAM,	"NOPARAM, missing parameter: !AZ"),
	$VBKREC(VBACKUP$_IVOP,		"IVOP, cannot tell what to do: !AZ"),
	$VBKREC(VBACKUP$_NOTOPIC,	"NOTOPIC, sorry, no documentation on !AZ"),
	$VBKREC(VBACKUP$_SAVESUMM,	"SAVESUMM, !UQ file!%S, !UQ byte!%S saved in !UQ block!%S and !UL volume!%S"),
	$VBKREC(VBACKUP$_RESTSUMM,	"RESTSUMM, !UQ file!%S, !UQ byte!%S restored"),
	$VBKREC(VBACKUP$_CMPSUMM,	"CMPSUMM, !UQ file!%S compared, !UQ difference!%S"),
	$VBKREC(VBACKUP$_FATALSAVE,	"FATALSAVE, the saveset !AZ could not be completed"),
	$VBKREC(VBACKUP$_VERIFYING,	"VERIFYING, verifying !AZ"),
	$VBKREC(VBACKUP$_NOCATALOG,	"NOCATALOG, !AZ has no catalog, it is listed by reading it whole"),
	$VBKREC(VBACKUP$_TOOMANY,	"TOOMANY, too many !AZ, only the first !UL are used"),
	$VBKREC(VBACKUP$_RECORDED,	"RECORDED, !UQ file!%S recorded in the journal !AZ"),
	$VBKREC(VBACKUP$_JNLERR,	"JNLERR, journal !AZ: !AZ, errno=!SL (!AZ)"),
	$VBKREC(VBACKUP$_NOJOURNAL,	"NOJOURNAL, the journal !AZ is not there yet: /SINCE=BACKUP saves everything"),
	$VBKREC(VBACKUP$_DELETED,	"DELETED, !AZ deleted: it is not in the incremental saveset"),
	$VBKREC(VBACKUP$_MISSING,	"MISSING, !AZ should be there from an earlier saveset, and is not"),
	$VBKREC(VBACKUP$_NOTINCR,	"NOTINCR, !AZ: !AZ, nothing restored with /INCREMENTAL"),
	$VBKREC(VBACKUP$_COPIED,	"COPIED, !AZ copied"),
	$VBKREC(VBACKUP$_CPYSUMM,	"CPYSUMM, !UQ file!%S, !UQ byte!%S copied"),
	$VBKREC(VBACKUP$_INCRSUMM,	"INCRSUMM, !UQ unchanged file!%S listed as present, not saved"),
	$VBKREC(VBACKUP$_NOINODE,	"NOINODE, !AZ: !UL file!%S not recorded - the catalog has no inode data (written before X01-02)"),
	$VBKREC(VBACKUP$_FILLOST,	"FILLOST, !AZ was not restored: its records were lost in bad blocks"),
	$VBKREC(VBACKUP$_UNNAMED,	"UNNAMED, !AZ: blocks were lost and !AZ - files missing from the restore cannot all be named"),
	$VBKREC(VBACKUP$_PHYSMOUNTED,	"PHYSMOUNTED, !AZ is mounted!AZ on !AZ: !AZ"),
	$VBKREC(VBACKUP$_PHYSHELD,	"PHYSHELD, !AZ is in use (!AZ): free it first, or save what uses it"),
	$VBKREC(VBACKUP$_PHYSNOTDEV,	"PHYSNOTDEV, !AZ is neither a block device nor a file"),
	$VBKREC(VBACKUP$_PHYSNOTPHYS,	"PHYSNOTPHYS, !AZ was not made with /PHYSICAL: restore it without /PHYSICAL"),
	$VBKREC(VBACKUP$_PHYSSMALL,	"PHYSSMALL, !AZ holds !UQ bytes, the device saved held !UQ: nothing written"),
	$VBKREC(VBACKUP$_PHYSLARGER,	"PHYSLARGER, !AZ holds !UQ bytes, the device saved held !UQ: the rest stays as it is, the file system keeps its old size"),
	$VBKREC(VBACKUP$_PHYSREPLACE,	"PHYSREPLACE, !AZ is a device: everything on it is overwritten - give /REPLACE to do so"),
	$VBKREC(VBACKUP$_PHYSABORT,	"PHYSABORT, !AZ not overwritten: the answer was not YES"),
	$VBKREC(VBACKUP$_PHYSUUID,	"PHYSUUID, !AZ now carries the labels and UUIDs of the device saved: never mount it beside the original"),
	$VBKREC(VBACKUP$_PHYSSIZE,	"PHYSSIZE, !AZ changed its size while it was read (!UQ, then !UQ bytes): the copy is not to be trusted"),
	$VBKREC(VBACKUP$_PHYSSUMM,	"PHYSSUMM, !AZ: !UQ bytes, !UQ of them data, the rest zeros"),
	$VBKREC(VBACKUP$_IMGNOTVOL,	"IMGNOTVOL, !AZ is neither the mount point of a file system nor a device: /IMAGE saves a whole volume"),
	$VBKREC(VBACKUP$_IMGNOTMNT,	"IMGNOTMNT, !AZ is not mounted: mount it (read-only is enough) and give the mount point or the device"),
	$VBKREC(VBACKUP$_IMGNOTIMG,	"IMGNOTIMG, !AZ was not made with /IMAGE: restore it without /IMAGE"),
	$VBKREC(VBACKUP$_IMGUNSUPP,	"IMGUNSUPP, !AZ: VBACKUP does not make a file system of type !AZ - use /PHYSICAL for it"),
	$VBKREC(VBACKUP$_IMGMKFS,	"IMGMKFS, !AZ failed: !AZ"),
	$VBKREC(VBACKUP$_IMGMOUNT,	"IMGMOUNT, the new !AZ file system on !AZ cannot be mounted, errno=!UL (!AZ)"),
	$VBKREC(VBACKUP$_IMGSMALL,	"IMGSMALL, !AZ holds !UQ bytes, the files need about !UQ: nothing written"),
	$VBKREC(VBACKUP$_IMGNOID,	"IMGNOID, !AZ: the !AZ of the file system is not known - the new one gets a new one"),
	$VBKREC(VBACKUP$_IMGCMD,	"IMGCMD, !AZ"),
	$VBKREC(VBACKUP$_IMGSUMM,	"IMGSUMM, !AZ: a !AZ file system made, !UQ file!%S, !UQ byte!%S restored"),
	$VBKREC(VBACKUP$_ORIGNOBASE,	"ORIGNOBASE, !AZ does not say where its files came from (made before X01-02): give an output directory"),
	$VBKREC(VBACKUP$_ORIGTARGET,	"ORIGTARGET, the files of !AZ go back to !AZ"),
	$VBKREC(VBACKUP$_SRCDELETED,	"SRCDELETED, !AZ deleted: it is in the saveset and verified"),
	$VBKREC(VBACKUP$_SRCKEPT,	"SRCKEPT, !AZ not deleted: !AZ"),
	$VBKREC(VBACKUP$_DELSUMM,	"DELSUMM, !UQ file!%S deleted, !UQ kept"),
	$VBKREC(VBACKUP$_QUALUSE,	"QUALUSE, /!AZ: !AZ")
	};

static	EMSG_RECORD_DESC	s_msgdsc = {
	.msgnr		= (unsigned) $ARRSZ(s_msgtab),
	.facno		= VBACKUP$K_FACNO,
	.msgrec		= s_msgtab,
	.facname	= __FAC__
	};

static	int	s_errors, s_warnings;		/* Conditions E/F and W signalled so far	*/
static	int	s_confall;			/* /CONFIRM answered ALL			*/


int	vbk$inimsg	(void)
{
	__util$setlogfd(STDERR_FILENO);

	return	(int) __util$inimsg(&s_msgdsc);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Note a condition that has just been signalled by $VBKMSG: E and F
**	are counted as errors, W as warnings.  The completion code of the
**	image is decided by the counts.
**
**  FORMAL PARAMETERS:
**
**	a_sts		The condition value
**
**  RETURN VALUE:
**	<a_sts>, unchanged, so that $VBKMSG still yields the condition.
**--
*/
int	vbk$note	(
		int		a_sts
			)
{
	switch ( $SEV(a_sts) )
		{
		case	STS$K_ERROR:
		case	STS$K_FATAL:
			s_errors++;
			break;

		case	STS$K_WARN:
			s_warnings++;
			break;
		}

	return	a_sts;
}

void	vbk$warned	(void)
{
	s_warnings++;
}

int	vbk$errors	(void)
{
	return	s_errors;
}

int	vbk$exitcode	(void)
{
	return	s_errors ? VBACKUP$K_EXIT_ERROR : (s_warnings ? VBACKUP$K_EXIT_WARN : VBACKUP$K_EXIT_OK);
}


/*
**  Copy a string, cut to the buffer; STS$K_WARN - it has been cut
*/
int	vbk$strcpy	(
		size_t		a_bufsz,
		char *		a_buf,
	const	char *		a_src
			)
{
size_t	l_len = strnlen(a_src, a_bufsz);

	if ( !a_bufsz )
		return	STS$K_WARN;

	if ( l_len >= a_bufsz )
		{
		memcpy(a_buf, a_src, a_bufsz - 1);
		a_buf [a_bufsz - 1] = '\0';

		return	STS$K_WARN;
		}

	memcpy(a_buf, a_src, l_len + 1);

	return	STS$K_SUCCESS;
}

int	vbk$upcase	(
		size_t		a_len,
	const	char *		a_src,
		char *		a_dst
			)
{
	for ( size_t i = 0; i < a_len; i++ )
		a_dst [i] = (char) toupper((unsigned char) a_src [i]);

	a_dst [a_len] = '\0';

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Convert a time string of /SINCE or /BEFORE into a binary time:
**	the keywords TODAY, YESTERDAY, TOMORROW, NOW - an unambiguous
**	abbreviation will do - and everything UTIL$BINTIM understands,
**	a delta being taken back from now.
**
**  FORMAL PARAMETERS:
**
**	a_str		The string, ASCIZ
**	a_time		Receives the binary time
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- converted;
**	STS$K_ERROR	- the string cannot be converted.
**--
*/
int	vbk$cvttim	(
	const	char *		a_str,
		fao_time_t *	a_time
			)
{
static	const struct	vbk_kwtim_t
	{
	const char *	name;
	int		days;
	int		midnight;
	} l_kwtab [] = {
	{ "TODAY",	0,	1 },
	{ "YESTERDAY",	-1,	1 },
	{ "TOMORROW",	1,	1 },
	{ "NOW",	0,	0 }
	};

fao_time_t	l_now, l_tim = 0;
struct tm	l_tm = {0};
char		l_ups [VBACKUP$K_SZ_STR];
size_t		l_len;
time_t		l_t;

	if ( !a_str || !*a_str )
		return	STS$K_ERROR;

	l_len	= strnlen(a_str, sizeof(l_ups) - 1);
	vbk$upcase(l_len, a_str, l_ups);

	if ( !(1 & __util$gettim(&l_now)) )
		return	STS$K_ERROR;

	for ( int i = 0; i < (int) $ARRSZ(l_kwtab); i++ )
		{
		if ( strncmp(l_ups, l_kwtab[i].name, l_len) )
			continue;

		l_t	= (time_t) l_now;

		if ( !localtime_r(&l_t, &l_tm) )
			return	STS$K_ERROR;

		l_tm.tm_mday	+= l_kwtab[i].days;

		if ( l_kwtab[i].midnight )
			l_tm.tm_hour = l_tm.tm_min = l_tm.tm_sec = 0;

		l_tm.tm_isdst	= -1;

		if ( (l_t = mktime(&l_tm)) == (time_t) -1 )
			return	STS$K_ERROR;

		*a_time	= (fao_time_t) l_t;

		return	STS$K_SUCCESS;
		}

	if ( !(1 & __util$bintim(a_str, &l_tim)) )
		return	STS$K_ERROR;

	*a_time	= (l_tim < 0) ? (l_now + l_tim) : l_tim;

	return	STS$K_SUCCESS;
}


/*
**  Does a stored name match any of the patterns?  '*' takes any run, the
**  slash included, '%' and '?' one octet.  No patterns - no match.
*/
int	vbk$match	(
	const	char *		a_name,
		char * const *	a_pat,
		unsigned	a_npat
			)
{
	for ( unsigned i = 0; i < a_npat; i++ )
		if ( __util$pattern_match((char *) a_name, a_pat [i]) )
			return	STS$K_SUCCESS;

	return	STS$K_WARN;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Ask about one file under /CONFIRM, at the terminal: YES, NO, QUIT
**	or ALL, as BACKUP asks; an empty answer is NO.
**
**  FORMAL PARAMETERS:
**
**	a_what		The action, "Save", "Restore", ...
**	a_name		The file
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- go ahead (YES, or ALL now or before);
**	STS$K_WARN	- skip this one;
**	STS$K_FATAL	- stop (QUIT, or no terminal to ask at).
**--
*/
int	vbk$confirm	(
	const	char *		a_what,
	const	char *		a_name
			)
{
char	l_ans [32];
int	l_fd;
ssize_t	l_n;

	if ( s_confall )
		return	STS$K_SUCCESS;

	if ( 0 > (l_fd = open("/dev/tty", O_RDWR | O_CLOEXEC)) )
		return	STS$K_FATAL;

	for ( ;; )
		{
		dprintf(l_fd, "%s %s ? [N]: ", a_what, a_name);

		if ( 0 >= (l_n = read(l_fd, l_ans, sizeof(l_ans) - 1)) )
			{
			close(l_fd);

			return	STS$K_FATAL;
			}

		l_ans [l_n] = '\0';

		switch ( toupper((unsigned char) l_ans [0]) )
			{
			case	'Y':
				close(l_fd);
				return	STS$K_SUCCESS;

			case	'A':
				s_confall = 1;
				close(l_fd);
				return	STS$K_SUCCESS;

			case	'Q':
				close(l_fd);
				return	STS$K_FATAL;

			case	'N':
			case	'\n':
				close(l_fd);
				return	STS$K_WARN;
			}

		dprintf(l_fd, "  YES, NO, QUIT or ALL\n");
		}
}


/*
**  The event callback of the reader: the events become diagnostics.
**  The argument is the name of the saveset.
*/
void	vbk$rdevent	(
		void *		a_arg,
		int		a_ev,
		uint32_t	a_vol,
		uint64_t	a_blk
			)
{
const char *	l_spec = (const char *) a_arg;

	switch ( a_ev )
		{
		case	VBK$K_EV_REPAIRED:
			$VBKMSG(VBACKUP$_BLKFIXED, a_blk, a_vol);
			break;

		case	VBK$K_EV_LOST:
			$VBKMSG(VBACKUP$_BLKLOST, a_blk, a_vol);
			break;

		case	VBK$K_EV_MISSVOL:
			$VBKMSG(VBACKUP$_MISSVOL, a_vol, l_spec);
			break;

		case	VBK$K_EV_WRONGVOL:
			$VBKMSG(VBACKUP$_WRONGVOL, a_vol, l_spec);
			break;

		case	VBK$K_EV_BADREC:
			$VBKMSG(VBACKUP$_BADREC, a_blk, a_vol);
			break;
		}
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Join a stored name to a directory.  A stored name comes out of a
**	saveset, and a saveset is a file anybody may have made: a name
**	that is absolute, carries a NUL, or climbs with ".." is refused,
**	so that a restore never writes outside the directory it was given.
**
**  FORMAL PARAMETERS:
**
**	a_dir		The directory, "" - the current one
**	a_name		The stored name, not terminated
**	a_namelen	Its length
**	a_out		Receives the specification
**	a_outsz		Size of <a_out>
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- <a_out> holds it;
**	STS$K_ERROR	- the name is refused, or too long.
**--
*/
int	vbk$mkpath	(
	const	char *		a_dir,
	const	char *		a_name,
		uint32_t	a_namelen,
		char *		a_out,
		size_t		a_outsz
			)
{
size_t	l_dirlen = strlen(a_dir);

	if ( !a_namelen || (a_name [0] == '/') || memchr(a_name, '\0', a_namelen) )
		return	STS$K_ERROR;

	/* No component may be ".." */
	for ( uint32_t i = 0; i < a_namelen; )
		{
		uint32_t	j = i;

		while ( (j < a_namelen) && (a_name [j] != '/') )
			j++;

		if ( ((j - i) == 2) && (a_name [i] == '.') && (a_name [i + 1] == '.') )
			return	STS$K_ERROR;

		i	= j + 1;
		}

	while ( l_dirlen && (a_dir [l_dirlen - 1] == '/') && (l_dirlen > 1) )
		l_dirlen--;

	if ( (l_dirlen + 1 + a_namelen + 1) > a_outsz )
		return	STS$K_ERROR;

	if ( l_dirlen )
		{
		memcpy(a_out, a_dir, l_dirlen);

		if ( a_dir [l_dirlen - 1] != '/' )
			a_out [l_dirlen++] = '/';
		}

	memcpy(a_out + l_dirlen, a_name, a_namelen);
	a_out [l_dirlen + a_namelen] = '\0';

	return	STS$K_SUCCESS;
}
