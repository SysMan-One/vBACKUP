#ifndef	__VBKDEF$H__
#define	__VBKDEF$H__	1

#ifndef	__MODULE__
#define	__MODULE__	"VBKDEF"
#endif

#ifndef	__IDENT__
#define	__IDENT__	"X01-11"
#endif

#ifndef	__REV__
#define	__REV__		"1.11.0"
#endif

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkdef.h
**
**  ABSTRACT:	Definitions shared by the modules of the utility: the
**		condition values and the message macro, the parameters of
**		the command, the prototypes of the modules.  The format of
**		the saveset itself is lib/vbkfmt.h.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-11		 5-OCT-2026	RRL
**		VBACKUP$K_OP_TRANSFER; VBKXFR.C, VBKRSH.C; BLKCOPIED, XFRSUMM, REMOTE,
**		REMOTEERR.
**
**	X01-07		 5-OCT-2026	RRL
**		STARTED, COMPLETED; $VBKFAOB, $VBKFAOP, $VBKFAOD, VBK$NUMW,
**		VBK$PADW.
**
**	X01-06		 5-OCT-2026	RRL
**		/ENCRYPT, /KEY_FILE; the messages of the encryption, MAXPARM,
**		GLUED; VBKKEY.C.
**
**	X01-04		 4-OCT-2026	RRL
**		COMPRESS: /DATA_FORMAT=COMPRESSED.  PHYSICAL and its messages.
**		IMAGE and its messages.  ORIGINAL, DELETE and theirs.  VBK$ZP_*.
**
**	X01-03		 3-OCT-2026	RRL
**		NOPIPE: VBACKUP_PIPELINE=0; PRE and VBK$PRE_*: the read-ahead.
**		FILLOST, UNNAMED: the files a damaged restore could not name.
**		VBK$HASH_SORTED.
**
**	X01-02		 3-OCT-2026	RRL
**		Stage 2: the journal, incremental saves and restores, the copy;
**		the messages, parameters and prototypes they need.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#ifndef	__FAC__
#define	__FAC__		"VBACKUP"
#endif

#include	<stdint.h>
#include	<stddef.h>
#include	<stdio.h>
#include	<sys/types.h>
#include	<sys/stat.h>

#include	"utility_routines.h"
#include	"starlet.h"
#include	"cli_routines.h"

#include	"vbkfmt.h"
#include	"vbkwrt.h"
#include	"vbkrd.h"
#include	"vbkos.h"
#include	"vbklz4.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*
**  Facility number, see $DEFFAC/$DEFMSG/$DEFSEV of UTILITY_ROUTINES.H;
**  0x0110 is HELP, 0x0111 SEARCH
*/
#define	VBACKUP$K_FACNO		0x0112

#define	$VBKSTS(msg, sev)	( $DEFFAC(VBACKUP$K_FACNO) | $DEFMSG(msg) | $DEFSEV(sev) )

/*
**  Message numbers; the texts are the table S_MSGTAB of VBKMSG.C
*/
enum	{
	VBACKUP$K_MSG_NORMAL	= 1,
	VBACKUP$K_MSG_CREATED,			/* A volume has been created			*/
	VBACKUP$K_MSG_SAVED,			/* /LOG of a saved file				*/
	VBACKUP$K_MSG_RESTORED,			/* /LOG of a restored file			*/
	VBACKUP$K_MSG_COMPARED,			/* /LOG of a compared file			*/
	VBACKUP$K_MSG_SKIPPED,			/* A file left out, and why			*/
	VBACKUP$K_MSG_FILEEXISTS,		/* Not restored over an existing file		*/
	VBACKUP$K_MSG_FILCHANGED,		/* Changed while it was being saved		*/
	VBACKUP$K_MSG_OPENIN,			/* Cannot open an input file			*/
	VBACKUP$K_MSG_OPENOUT,			/* Cannot create an output file			*/
	VBACKUP$K_MSG_READERR,
	VBACKUP$K_MSG_WRITERR,
	VBACKUP$K_MSG_OPENDIR,
	VBACKUP$K_MSG_NOTSAVESET,		/* The input is not a saveset			*/
	VBACKUP$K_MSG_WRONGVOL,			/* A volume of another saveset			*/
	VBACKUP$K_MSG_MISSVOL,			/* A volume is missing				*/
	VBACKUP$K_MSG_NOTRAILER,		/* The save did not complete			*/
	VBACKUP$K_MSG_BLKFIXED,			/* A block has been rebuilt from its group	*/
	VBACKUP$K_MSG_BLKLOST,			/* A block is lost				*/
	VBACKUP$K_MSG_BADREC,			/* Garbage in the record stream			*/
	VBACKUP$K_MSG_FILDAMAGED,		/* A file lost data in lost blocks		*/
	VBACKUP$K_MSG_CRCERR,			/* Restored data differ from saved data		*/
	VBACKUP$K_MSG_COMPARERR,		/* A difference found by /COMPARE		*/
	VBACKUP$K_MSG_ATTRERR,			/* An attribute could not be restored		*/
	VBACKUP$K_MSG_UNSUPP,			/* A file type that is not restored		*/
	VBACKUP$K_MSG_NOTFOUND,			/* /EXTRACT: no such file in the saveset	*/
	VBACKUP$K_MSG_NOFILES,			/* A specification selected nothing		*/
	VBACKUP$K_MSG_TOODEEP,
	VBACKUP$K_MSG_NOMEM,
	VBACKUP$K_MSG_IVQUAL,
	VBACKUP$K_MSG_IVTIME,
	VBACKUP$K_MSG_CONFQUAL,
	VBACKUP$K_MSG_NOPARAM,			/* A parameter is missing			*/
	VBACKUP$K_MSG_IVOP,			/* What the command means cannot be told	*/
	VBACKUP$K_MSG_NOTOPIC,
	VBACKUP$K_MSG_SAVESUMM,			/* Totals of a save				*/
	VBACKUP$K_MSG_RESTSUMM,			/* Totals of a restore				*/
	VBACKUP$K_MSG_CMPSUMM,			/* Totals of a compare				*/
	VBACKUP$K_MSG_FATALSAVE,		/* The saveset could not be completed		*/
	VBACKUP$K_MSG_VERIFYING,		/* /VERIFY begins				*/
	VBACKUP$K_MSG_NOCATALOG,		/* Listing by a sequential scan			*/
	VBACKUP$K_MSG_TOOMANY,
	VBACKUP$K_MSG_RECORDED,			/* The journal has been updated			*/
	VBACKUP$K_MSG_JNLERR,			/* The journal cannot be read or written	*/
	VBACKUP$K_MSG_NOJOURNAL,		/* No journal yet: /SINCE=BACKUP saves all	*/
	VBACKUP$K_MSG_DELETED,			/* /INCREMENTAL removed a file			*/
	VBACKUP$K_MSG_MISSING,			/* A PRESENT file is not on the disk		*/
	VBACKUP$K_MSG_NOTINCR,			/* /INCREMENTAL refused: no catalog, no KIND	*/
	VBACKUP$K_MSG_COPIED,			/* /LOG of a copied file			*/
	VBACKUP$K_MSG_CPYSUMM,			/* Totals of a copy				*/
	VBACKUP$K_MSG_INCRSUMM,			/* Totals of the present-only entries		*/
	VBACKUP$K_MSG_NOINODE,			/* /RECORD of a saveset older than X01-02	*/
	VBACKUP$K_MSG_FILLOST,			/* A file of the catalog lost with its records	*/
	VBACKUP$K_MSG_UNNAMED,			/* Files lost, and no catalog to name them	*/
	VBACKUP$K_MSG_PHYSMOUNTED,		/* /PHYSICAL: the device is mounted		*/
	VBACKUP$K_MSG_PHYSHELD,			/* ... in use: LVM, RAID, dm-crypt, swap	*/
	VBACKUP$K_MSG_PHYSNOTDEV,		/* ... neither a block device nor a file	*/
	VBACKUP$K_MSG_PHYSNOTPHYS,		/* ... the saveset is not a /PHYSICAL one	*/
	VBACKUP$K_MSG_PHYSSMALL,		/* ... the output is smaller than the device	*/
	VBACKUP$K_MSG_PHYSLARGER,		/* ... the output is larger: the rest is kept	*/
	VBACKUP$K_MSG_PHYSREPLACE,		/* ... a device is overwritten with /REPLACE only */
	VBACKUP$K_MSG_PHYSABORT,		/* ... the answer was not YES			*/
	VBACKUP$K_MSG_PHYSUUID,			/* ... the copy has the UUIDs of the original	*/
	VBACKUP$K_MSG_PHYSSIZE,			/* ... the device changed its size meanwhile	*/
	VBACKUP$K_MSG_PHYSSUMM,			/* ... the totals				*/
	VBACKUP$K_MSG_IMGNOTVOL,		/* /IMAGE: not a mount point, not a device	*/
	VBACKUP$K_MSG_IMGNOTMNT,		/* ... the device is not mounted		*/
	VBACKUP$K_MSG_IMGNOTIMG,		/* ... the saveset is not an /IMAGE one		*/
	VBACKUP$K_MSG_IMGUNSUPP,		/* ... no mkfs for this file system type	*/
	VBACKUP$K_MSG_IMGMKFS,			/* ... mkfs failed				*/
	VBACKUP$K_MSG_IMGMOUNT,			/* ... the new file system cannot be mounted	*/
	VBACKUP$K_MSG_IMGSMALL,			/* ... the device cannot hold the files		*/
	VBACKUP$K_MSG_IMGNOID,			/* ... label or UUID not known			*/
	VBACKUP$K_MSG_IMGCMD,			/* ... the mkfs command, /LOG			*/
	VBACKUP$K_MSG_IMGSUMM,			/* ... the totals				*/
	VBACKUP$K_MSG_ORIGNOBASE,		/* /ORIGINAL: the saveset does not say where	*/
	VBACKUP$K_MSG_ORIGTARGET,		/* ... where the files go back to		*/
	VBACKUP$K_MSG_SRCDELETED,		/* /DELETE: a file saved and verified, deleted	*/
	VBACKUP$K_MSG_SRCKEPT,			/* ... kept: changed since, or not verified	*/
	VBACKUP$K_MSG_DELSUMM,			/* ... the totals				*/
	VBACKUP$K_MSG_QUALUSE,			/* A qualifier used where it cannot be		*/
	VBACKUP$K_MSG_MAXPARM,			/* More words than input and output		*/
	VBACKUP$K_MSG_NOKEY,			/* Encrypted, and no passphrase to be had	*/
	VBACKUP$K_MSG_WRONGKEY,			/* ... the passphrase does not open it		*/
	VBACKUP$K_MSG_KEYFILE,			/* ... the key file cannot be used		*/
	VBACKUP$K_MSG_KEYMATCH,			/* ... the two passphrases of a save differ	*/
	VBACKUP$K_MSG_BLKFORGED,		/* ... a block whose CRC is right, its TAG not	*/
	VBACKUP$K_MSG_ENCRYPTED,		/* ... a saveset made encrypted, /LOG		*/
	VBACKUP$K_MSG_GLUED,			/* Qualifiers glued to a parameter, taken apart	*/
	VBACKUP$K_MSG_STARTED,			/* The work begins: what, from where, to where	*/
	VBACKUP$K_MSG_COMPLETED,		/* ... it is done: how, in how long		*/
	VBACKUP$K_MSG_BLKCOPIED,		/* A saveset copied: a bad block copied as it is */
	VBACKUP$K_MSG_XFRSUMM,			/* ... the totals				*/
	VBACKUP$K_MSG_REMOTE,			/* node::file: the pipe to the node cannot be made */
	VBACKUP$K_MSG_REMOTEERR,		/* ... VBACKUP there failed			*/

	VBACKUP$K_MSG_MAX
	};

#define	VBACKUP$_NORMAL		$VBKSTS(VBACKUP$K_MSG_NORMAL,		STS$K_SUCCESS)
#define	VBACKUP$_CREATED	$VBKSTS(VBACKUP$K_MSG_CREATED,		STS$K_INFO)
#define	VBACKUP$_SAVED		$VBKSTS(VBACKUP$K_MSG_SAVED,		STS$K_INFO)
#define	VBACKUP$_RESTORED	$VBKSTS(VBACKUP$K_MSG_RESTORED,		STS$K_INFO)
#define	VBACKUP$_COMPARED	$VBKSTS(VBACKUP$K_MSG_COMPARED,		STS$K_INFO)
#define	VBACKUP$_SKIPPED	$VBKSTS(VBACKUP$K_MSG_SKIPPED,		STS$K_INFO)
#define	VBACKUP$_FILEEXISTS	$VBKSTS(VBACKUP$K_MSG_FILEEXISTS,	STS$K_WARN)
#define	VBACKUP$_FILCHANGED	$VBKSTS(VBACKUP$K_MSG_FILCHANGED,	STS$K_WARN)
#define	VBACKUP$_OPENIN		$VBKSTS(VBACKUP$K_MSG_OPENIN,		STS$K_ERROR)
#define	VBACKUP$_OPENOUT	$VBKSTS(VBACKUP$K_MSG_OPENOUT,		STS$K_ERROR)
#define	VBACKUP$_READERR	$VBKSTS(VBACKUP$K_MSG_READERR,		STS$K_ERROR)
#define	VBACKUP$_WRITERR	$VBKSTS(VBACKUP$K_MSG_WRITERR,		STS$K_ERROR)
#define	VBACKUP$_OPENDIR	$VBKSTS(VBACKUP$K_MSG_OPENDIR,		STS$K_ERROR)
#define	VBACKUP$_NOTSAVESET	$VBKSTS(VBACKUP$K_MSG_NOTSAVESET,	STS$K_ERROR)
#define	VBACKUP$_WRONGVOL	$VBKSTS(VBACKUP$K_MSG_WRONGVOL,		STS$K_WARN)
#define	VBACKUP$_MISSVOL	$VBKSTS(VBACKUP$K_MSG_MISSVOL,		STS$K_ERROR)
#define	VBACKUP$_NOTRAILER	$VBKSTS(VBACKUP$K_MSG_NOTRAILER,	STS$K_WARN)
#define	VBACKUP$_BLKFIXED	$VBKSTS(VBACKUP$K_MSG_BLKFIXED,		STS$K_INFO)
#define	VBACKUP$_BLKLOST	$VBKSTS(VBACKUP$K_MSG_BLKLOST,		STS$K_ERROR)
#define	VBACKUP$_BADREC		$VBKSTS(VBACKUP$K_MSG_BADREC,		STS$K_WARN)
#define	VBACKUP$_FILDAMAGED	$VBKSTS(VBACKUP$K_MSG_FILDAMAGED,	STS$K_ERROR)
#define	VBACKUP$_CRCERR		$VBKSTS(VBACKUP$K_MSG_CRCERR,		STS$K_ERROR)
#define	VBACKUP$_COMPARERR	$VBKSTS(VBACKUP$K_MSG_COMPARERR,	STS$K_ERROR)
#define	VBACKUP$_ATTRERR	$VBKSTS(VBACKUP$K_MSG_ATTRERR,		STS$K_WARN)
#define	VBACKUP$_UNSUPP		$VBKSTS(VBACKUP$K_MSG_UNSUPP,		STS$K_WARN)
#define	VBACKUP$_NOTFOUND	$VBKSTS(VBACKUP$K_MSG_NOTFOUND,		STS$K_ERROR)
#define	VBACKUP$_NOFILES	$VBKSTS(VBACKUP$K_MSG_NOFILES,		STS$K_WARN)
#define	VBACKUP$_TOODEEP	$VBKSTS(VBACKUP$K_MSG_TOODEEP,		STS$K_WARN)
#define	VBACKUP$_NOMEM		$VBKSTS(VBACKUP$K_MSG_NOMEM,		STS$K_FATAL)
#define	VBACKUP$_IVQUAL		$VBKSTS(VBACKUP$K_MSG_IVQUAL,		STS$K_ERROR)
#define	VBACKUP$_IVTIME		$VBKSTS(VBACKUP$K_MSG_IVTIME,		STS$K_ERROR)
#define	VBACKUP$_CONFQUAL	$VBKSTS(VBACKUP$K_MSG_CONFQUAL,		STS$K_ERROR)
#define	VBACKUP$_NOPARAM	$VBKSTS(VBACKUP$K_MSG_NOPARAM,		STS$K_ERROR)
#define	VBACKUP$_IVOP		$VBKSTS(VBACKUP$K_MSG_IVOP,		STS$K_ERROR)
#define	VBACKUP$_NOTOPIC	$VBKSTS(VBACKUP$K_MSG_NOTOPIC,		STS$K_ERROR)
#define	VBACKUP$_SAVESUMM	$VBKSTS(VBACKUP$K_MSG_SAVESUMM,		STS$K_INFO)
#define	VBACKUP$_RESTSUMM	$VBKSTS(VBACKUP$K_MSG_RESTSUMM,		STS$K_INFO)
#define	VBACKUP$_CMPSUMM	$VBKSTS(VBACKUP$K_MSG_CMPSUMM,		STS$K_INFO)
#define	VBACKUP$_FATALSAVE	$VBKSTS(VBACKUP$K_MSG_FATALSAVE,	STS$K_FATAL)
#define	VBACKUP$_VERIFYING	$VBKSTS(VBACKUP$K_MSG_VERIFYING,	STS$K_INFO)
#define	VBACKUP$_NOCATALOG	$VBKSTS(VBACKUP$K_MSG_NOCATALOG,	STS$K_WARN)
#define	VBACKUP$_TOOMANY	$VBKSTS(VBACKUP$K_MSG_TOOMANY,		STS$K_WARN)
#define	VBACKUP$_RECORDED	$VBKSTS(VBACKUP$K_MSG_RECORDED,		STS$K_INFO)
#define	VBACKUP$_JNLERR		$VBKSTS(VBACKUP$K_MSG_JNLERR,		STS$K_ERROR)
#define	VBACKUP$_NOJOURNAL	$VBKSTS(VBACKUP$K_MSG_NOJOURNAL,	STS$K_INFO)
#define	VBACKUP$_DELETED	$VBKSTS(VBACKUP$K_MSG_DELETED,		STS$K_INFO)
#define	VBACKUP$_MISSING	$VBKSTS(VBACKUP$K_MSG_MISSING,		STS$K_WARN)
#define	VBACKUP$_NOTINCR	$VBKSTS(VBACKUP$K_MSG_NOTINCR,		STS$K_ERROR)
#define	VBACKUP$_COPIED		$VBKSTS(VBACKUP$K_MSG_COPIED,		STS$K_INFO)
#define	VBACKUP$_CPYSUMM	$VBKSTS(VBACKUP$K_MSG_CPYSUMM,		STS$K_INFO)
#define	VBACKUP$_INCRSUMM	$VBKSTS(VBACKUP$K_MSG_INCRSUMM,		STS$K_INFO)
#define	VBACKUP$_NOINODE	$VBKSTS(VBACKUP$K_MSG_NOINODE,		STS$K_WARN)
#define	VBACKUP$_FILLOST	$VBKSTS(VBACKUP$K_MSG_FILLOST,		STS$K_ERROR)
#define	VBACKUP$_UNNAMED	$VBKSTS(VBACKUP$K_MSG_UNNAMED,		STS$K_WARN)
#define	VBACKUP$_PHYSMOUNTED	$VBKSTS(VBACKUP$K_MSG_PHYSMOUNTED,	STS$K_ERROR)
#define	VBACKUP$_PHYSHELD	$VBKSTS(VBACKUP$K_MSG_PHYSHELD,		STS$K_ERROR)
#define	VBACKUP$_PHYSNOTDEV	$VBKSTS(VBACKUP$K_MSG_PHYSNOTDEV,	STS$K_ERROR)
#define	VBACKUP$_PHYSNOTPHYS	$VBKSTS(VBACKUP$K_MSG_PHYSNOTPHYS,	STS$K_ERROR)
#define	VBACKUP$_PHYSSMALL	$VBKSTS(VBACKUP$K_MSG_PHYSSMALL,	STS$K_ERROR)
#define	VBACKUP$_PHYSLARGER	$VBKSTS(VBACKUP$K_MSG_PHYSLARGER,	STS$K_INFO)
#define	VBACKUP$_PHYSREPLACE	$VBKSTS(VBACKUP$K_MSG_PHYSREPLACE,	STS$K_ERROR)
#define	VBACKUP$_PHYSABORT	$VBKSTS(VBACKUP$K_MSG_PHYSABORT,	STS$K_ERROR)
#define	VBACKUP$_PHYSUUID	$VBKSTS(VBACKUP$K_MSG_PHYSUUID,		STS$K_INFO)
#define	VBACKUP$_PHYSSIZE	$VBKSTS(VBACKUP$K_MSG_PHYSSIZE,		STS$K_ERROR)
#define	VBACKUP$_PHYSSUMM	$VBKSTS(VBACKUP$K_MSG_PHYSSUMM,		STS$K_INFO)
#define	VBACKUP$_IMGNOTVOL	$VBKSTS(VBACKUP$K_MSG_IMGNOTVOL,	STS$K_ERROR)
#define	VBACKUP$_IMGNOTMNT	$VBKSTS(VBACKUP$K_MSG_IMGNOTMNT,	STS$K_ERROR)
#define	VBACKUP$_IMGNOTIMG	$VBKSTS(VBACKUP$K_MSG_IMGNOTIMG,	STS$K_ERROR)
#define	VBACKUP$_IMGUNSUPP	$VBKSTS(VBACKUP$K_MSG_IMGUNSUPP,	STS$K_ERROR)
#define	VBACKUP$_IMGMKFS	$VBKSTS(VBACKUP$K_MSG_IMGMKFS,		STS$K_ERROR)
#define	VBACKUP$_IMGMOUNT	$VBKSTS(VBACKUP$K_MSG_IMGMOUNT,		STS$K_ERROR)
#define	VBACKUP$_IMGSMALL	$VBKSTS(VBACKUP$K_MSG_IMGSMALL,		STS$K_ERROR)
#define	VBACKUP$_IMGNOID	$VBKSTS(VBACKUP$K_MSG_IMGNOID,		STS$K_WARN)
#define	VBACKUP$_IMGCMD		$VBKSTS(VBACKUP$K_MSG_IMGCMD,		STS$K_INFO)
#define	VBACKUP$_IMGSUMM	$VBKSTS(VBACKUP$K_MSG_IMGSUMM,		STS$K_INFO)
#define	VBACKUP$_ORIGNOBASE	$VBKSTS(VBACKUP$K_MSG_ORIGNOBASE,	STS$K_ERROR)
#define	VBACKUP$_ORIGTARGET	$VBKSTS(VBACKUP$K_MSG_ORIGTARGET,	STS$K_INFO)
#define	VBACKUP$_SRCDELETED	$VBKSTS(VBACKUP$K_MSG_SRCDELETED,	STS$K_INFO)
#define	VBACKUP$_SRCKEPT	$VBKSTS(VBACKUP$K_MSG_SRCKEPT,		STS$K_WARN)
#define	VBACKUP$_DELSUMM	$VBKSTS(VBACKUP$K_MSG_DELSUMM,		STS$K_INFO)
#define	VBACKUP$_QUALUSE	$VBKSTS(VBACKUP$K_MSG_QUALUSE,		STS$K_ERROR)
#define	VBACKUP$_MAXPARM	$VBKSTS(VBACKUP$K_MSG_MAXPARM,		STS$K_ERROR)
#define	VBACKUP$_NOKEY		$VBKSTS(VBACKUP$K_MSG_NOKEY,		STS$K_ERROR)
#define	VBACKUP$_WRONGKEY	$VBKSTS(VBACKUP$K_MSG_WRONGKEY,		STS$K_ERROR)
#define	VBACKUP$_KEYFILE	$VBKSTS(VBACKUP$K_MSG_KEYFILE,		STS$K_ERROR)
#define	VBACKUP$_KEYMATCH	$VBKSTS(VBACKUP$K_MSG_KEYMATCH,		STS$K_ERROR)
#define	VBACKUP$_BLKFORGED	$VBKSTS(VBACKUP$K_MSG_BLKFORGED,	STS$K_WARN)
#define	VBACKUP$_ENCRYPTED	$VBKSTS(VBACKUP$K_MSG_ENCRYPTED,	STS$K_INFO)
#define	VBACKUP$_GLUED		$VBKSTS(VBACKUP$K_MSG_GLUED,		STS$K_INFO)
#define	VBACKUP$_STARTED	$VBKSTS(VBACKUP$K_MSG_STARTED,		STS$K_INFO)
#define	VBACKUP$_COMPLETED	$VBKSTS(VBACKUP$K_MSG_COMPLETED,	STS$K_INFO)
#define	VBACKUP$_BLKCOPIED	$VBKSTS(VBACKUP$K_MSG_BLKCOPIED,	STS$K_WARN)
#define	VBACKUP$_XFRSUMM	$VBKSTS(VBACKUP$K_MSG_XFRSUMM,		STS$K_INFO)
#define	VBACKUP$_REMOTE		$VBKSTS(VBACKUP$K_MSG_REMOTE,		STS$K_ERROR)
#define	VBACKUP$_REMOTEERR	$VBKSTS(VBACKUP$K_MSG_REMOTEERR,	STS$K_ERROR)

/*
**  A diagnostic is signalled by $VBKMSG: $PUTMSG_FAO of StarLet under the
**  name of this facility, the parameters cast and counted by the macro.
**  An E or F condition is counted - the completion code is decided by
**  the count, see VBK$EXITCODE.
**
**	$VBKMSG(VBACKUP$_OPENIN, l_spec, errno, strerror(errno));
*/
#ifndef	$PUTMSG_FAO
#error	"UTILITY_ROUTINES.H of StarLet 1.6 or later is required: $PUTMSG_FAO is missing"
#endif

#define	$VBKMSG(a_sts, ...)	vbk$note((int) $PUTMSG_FAO((a_sts), ##__VA_ARGS__))

/*
**  Every other text the utility makes goes through FAO too - no printf:
**
**	$VBKFAOB(buf, size, ctl, ...)	into a buffer, always ended by a NUL;
**					returns its length, or <size> when cut
**					(as snprintf tells a cut)
**	$VBKFAOP(fp, ctl, ...)		onto a stream
**	$VBKFAOD(fd, ctl, ...)		onto a file descriptor (a terminal)
**
**  The parameters are cast and counted as for $PUTMSG_FAO.  A field of FAO
**  that is too narrow is filled with '*' (a number) or cut (a string), as
**  on OpenVMS; where a value may outgrow its column the width is given
**  by '#' as VBK$NUMW / VBK$PADW reckon it, so nothing is ever lost.
*/
#define	$VBKFAOB(a_buf, a_size, a_ctl, ...)	vbk$faob((a_buf), (a_size), (a_ctl), FAO$_NARG(__VA_ARGS__) FAO$_EACH(__VA_ARGS__))
#define	$VBKFAOP(a_fp, a_ctl, ...)		vbk$faop((a_fp), (a_ctl), FAO$_NARG(__VA_ARGS__) FAO$_EACH(__VA_ARGS__))
#define	$VBKFAOD(a_fd, a_ctl, ...)		vbk$faod((a_fd), (a_ctl), FAO$_NARG(__VA_ARGS__) FAO$_EACH(__VA_ARGS__))

#define	VBK$K_FAOBUF	8192			/* A line of $VBKFAOP / $VBKFAOD		*/

/* The width of a right-justified number: <a_w>, or its digits when they are more */
#define	VBK$NUMW(a_w, a_v)	vbk$numw((a_w), (uint64_t) (a_v))

/* The blanks after a string of <a_len> octets in a column of <a_w>: 0 when it fills it */
#define	VBK$PADW(a_w, a_len)	((size_t) (a_len) < (size_t) (a_w) ? (uint32_t) ((size_t) (a_w) - (size_t) (a_len)) : 0U)

/*
**  Completion codes of the image
*/
#define	VBACKUP$K_EXIT_OK	0		/* Everything done				*/
#define	VBACKUP$K_EXIT_WARN	1		/* Done, with warnings				*/
#define	VBACKUP$K_EXIT_ERROR	2		/* Errors: something was not done		*/

/*
**  Sizes and limits
*/
#define	VBACKUP$K_SZ_PATH	4096		/* Longest file specification			*/
#define	VBACKUP$K_SZ_STR	256		/* Longest value of a qualifier, not a path	*/
#define	VBACKUP$K_MAXSPEC	64		/* Input specifications				*/
#define	VBACKUP$K_MAXPAT	64		/* /SELECT, /EXCLUDE patterns			*/
#define	VBACKUP$K_MAXDEPTH	256		/* Nesting of directories walked		*/
#define	VBACKUP$K_IOBUF		VBK$K_MAXDATA	/* Read and write unit of file data		*/

/*
**  What the command is to do
*/
enum	{
	VBACKUP$K_OP_NONE = 0,
	VBACKUP$K_OP_SAVE,			/* Files -> saveset				*/
	VBACKUP$K_OP_RESTORE,			/* Saveset -> directory				*/
	VBACKUP$K_OP_LIST,			/* Saveset -> listing				*/
	VBACKUP$K_OP_COMPARE,			/* Saveset <-> directory			*/
	VBACKUP$K_OP_EXTRACT,			/* One file of a saveset -> file or stdout	*/
	VBACKUP$K_OP_COPY,			/* Files -> directory				*/
	VBACKUP$K_OP_RECORD,			/* Savesets -> journal (rebuild)		*/
	VBACKUP$K_OP_JNLLIST,			/* Journal -> listing				*/
	VBACKUP$K_OP_TRANSFER			/* Saveset -> saveset, block for block (X01-11)	*/
	};

enum	{					/* /LIST format					*/
	VBACKUP$K_LST_BRIEF = 0,
	VBACKUP$K_LST_FULL,
	VBACKUP$K_LST_LS
	};

enum	{					/* Which time /SINCE and /BEFORE apply to	*/
	VBACKUP$K_TIM_MODIFIED = 0,
	VBACKUP$K_TIM_CREATED,
	VBACKUP$K_TIM_CHANGED
	};

enum	{					/* /OWNER of a restore				*/
	VBACKUP$K_OWN_ORIGINAL = 0,
	VBACKUP$K_OWN_DEFAULT,
	VBACKUP$K_OWN_USER
	};

/*
**  The command, as it has been made to mean
*/
typedef struct vbk_opts_t
{
	int		op;			/* VBACKUP$K_OP_*				*/
	char *		input [VBACKUP$K_MAXSPEC];
	unsigned	ninput;
	char		output [VBACKUP$K_SZ_PATH];

	uint32_t	bsize;			/* /BLOCK_SIZE					*/
	uint32_t	grpsz;			/* /GROUP_SIZE					*/
	uint64_t	volsize;		/* /VOLUME_SIZE, 0 - one volume			*/
	char		comment [VBACKUP$K_SZ_STR];

	char *		select [VBACKUP$K_MAXPAT];
	unsigned	nselect;
	char *		exclude [VBACKUP$K_MAXPAT];
	unsigned	nexclude;

	int		hassince, hasbefore;
	fao_time_t	since, before;
	int		sincebackup;		/* /SINCE=BACKUP: the journal decides		*/
	int		timefilter;		/* A time filter: the save is INCREMENTAL	*/
	char		filter [VBACKUP$K_SZ_STR];	/* ... as it was given, for the SUMMARY	*/
	int		timsrc;			/* VBACKUP$K_TIM_*				*/
	int		hasowner;		/* /BY_OWNER					*/
	uid_t		byowner;

	int		crossdev;		/* /CROSS_DEVICE				*/
	int		nobackup;		/* /IGNORE=NOBACKUP				*/
	int		xattrs;			/* /[NO]XATTRS					*/
	int		verify;
	int		log;
	int		confirm;
	int		replace;
	int		record;			/* /RECORD					*/
	int		incremental;		/* /INCREMENTAL (restore)			*/
	int		nopipe;			/* VBACKUP_PIPELINE=0: no writer thread		*/
	int		compress;		/* /DATA_FORMAT=COMPRESSED			*/
	int		physical;		/* /PHYSICAL: a device, block by block		*/
	uint64_t	physsize;		/* ... its size, its sector			*/
	uint32_t	physsector;
	int		physfd;			/* ... open for the save			*/
	int		image;			/* /IMAGE: a whole file system			*/
	char		imgmnt [VBACKUP$K_SZ_PATH];	/* ... its mount point			*/
	char		imgdev [VBACKUP$K_SZ_PATH];	/* ... its device			*/
	char		imgfstype [64];		/* ... its type, label, UUID, options		*/
	char		imglabel [256];
	char		imguuid [64];
	char		imgopts [1024];
	uint64_t	imgused;		/* ... bytes in use (statvfs)			*/
	VBK$TLVB	imgroot;		/* ... the attributes of its root directory	*/
	uint64_t	rstfiles, rstbytes;	/* The totals of the last restore		*/
	int		original;		/* /ORIGINAL: back where the files came from	*/
	int		delete;			/* /DELETE: the files saved and verified go	*/
	int		encrypt;		/* /ENCRYPT: the saveset is made encrypted	*/
	char		keyfile [VBACKUP$K_SZ_PATH];	/* /KEY_FILE=file: the passphrase	*/
	struct vbk_pre_t *pre;			/* The read-ahead of files, NULL - none		*/
	char		jnlspec [VBACKUP$K_SZ_PATH];	/* /JOURNAL=file, "" - the default	*/
	struct vbk_jnl_t *jnl;			/* The journal, when one is open		*/

	int		ownmode;		/* VBACKUP$K_OWN_*				*/
	uid_t		ownuid;
	gid_t		owngid;

	int		lstfmt;			/* VBACKUP$K_LST_*				*/
	char		lstfile [VBACKUP$K_SZ_PATH];
	char		extract [VBACKUP$K_SZ_PATH];

	char		cmdline [VBK$K_MAXCMD];
} VBK$OPTS;

/*
**  One entry of the walk, handed to the save
*/
typedef struct vbk_ent_t
{
	const char *	path;			/* Specification on the disk			*/
	const char *	name;			/* Stored name, relative to the base		*/
	const char *	abspath;		/* Absolute name: the key of the journal	*/
	uint16_t	baseidx;
	int		present;		/* Covered, not chosen by the time filter	*/
} VBK$ENT;

/*
**  A table of strings with a value each: the journal, the names of a
**  catalog.  VBKHSH.C.
*/
typedef struct vbk_hent_t
{
	char *		key;
	void *		val;
} VBK$HENT;

typedef struct vbk_hash_t
{
	VBK$HENT *	ent;
	size_t		sz;			/* A power of 2					*/
	size_t		cnt;
} VBK$HASH;

/*
**  The journal, VBKJNL.C
*/
typedef struct vbk_fstate_t
{
	uint64_t	dev, ino, size;
	VBK$TIME	ctime, mtime, recorded;
	uint8_t		ftype;
	uint8_t		ssuuid [VBK$K_UUIDSZ];
} VBK$FSTATE;

typedef struct vbk_jnl_t
{
	char		spec [VBACKUP$K_SZ_PATH];
	int		lockfd;			/* flock held for a writer, -1 - reader		*/
	VBK$HASH	files;			/* PATH -> VBK$FSTATE				*/
	VBK$TLVB *	sset;			/* SSET bodies					*/
	size_t		nsset;
	int		exists;			/* The file was there when opened		*/
} VBK$JNL;

/*
**  The attributes of a file as they are collected and as they are put
**  back.  The variable parts point into the record body or into the
**  buffers of the collector; they live as long as either.
*/
typedef struct vbk_attr_t
{
	uint8_t		ftype;			/* VBK$K_FT_*					*/
	uint32_t	mode, uid, gid, nlink, fsflags;
	uint64_t	size, rdev, dev, ino;
	VBK$TIME	mtime, atime, ctime, btime;
	int		hasbtime, hasflags;
	int		hasctime, hasdevino;	/* Catalogs of X01-01 have neither		*/
	const char *	uname;
	const char *	gname;
	const char *	link;
	uint32_t	linklen;
	const char *	path;
	uint32_t	pathlen;
	uint32_t	fileno;
	uint16_t	baseidx;
	uint32_t	crc;			/* CATALOG and FEND				*/
	uint8_t		status;
	VBK$LOC		loc;			/* CATALOG					*/
	const uint8_t *	xattr;			/* The record body, the XATTR items within	*/
	uint32_t	xattrlen;
} VBK$ATTR;

/*
**  VBKMSG.C
*/
int	vbk$inimsg	(void);
int	vbk$note	(int a_sts);
void	vbk$warned	(void);
int	vbk$exitcode	(void);
int	vbk$warnings	(void);
int	vbk$faob	(char *a_buf, size_t a_size, const char *a_ctl, int a_prmcnt, ...);
int	vbk$faop	(FILE *a_fp, const char *a_ctl, int a_prmcnt, ...);
int	vbk$faod	(int a_fd, const char *a_ctl, int a_prmcnt, ...);
uint32_t vbk$numw	(uint32_t a_w, uint64_t a_v);
int	vbk$errors	(void);
int	vbk$strcpy	(size_t a_bufsz, char *a_buf, const char *a_src);
int	vbk$upcase	(size_t a_len, const char *a_src, char *a_dst);
int	vbk$cvttim	(const char *a_str, fao_time_t *a_time);
int	vbk$match	(const char *a_name, char * const *a_pat, unsigned a_npat);
int	vbk$confirm	(const char *a_what, const char *a_name);
void	vbk$rdevent	(void *a_arg, int a_ev, uint32_t a_vol, uint64_t a_blk);
int	vbk$mkpath	(const char *a_dir, const char *a_name, uint32_t a_namelen, char *a_out, size_t a_outsz);

int	vbk$hash_put	(VBK$HASH *a_h, const char *a_key, void *a_val, void **a_old);
void *	vbk$hash_get	(const VBK$HASH *a_h, const char *a_key);
void	vbk$hash_free	(VBK$HASH *a_h, int a_freeval);
const VBK$HENT **	vbk$hash_sorted	(const VBK$HASH *a_h);

/*
**  VBKJNL.C
*/
int	vbk$jnl_open	(VBK$JNL *a_jnl, const char *a_spec, int a_write);
int	vbk$jnl_changed	(const VBK$JNL *a_jnl, const char *a_abspath, const struct statx *a_stx);
int	vbk$jnl_put	(VBK$JNL *a_jnl, const char *a_abspath, const VBK$FSTATE *a_st);
int	vbk$jnl_sset	(VBK$JNL *a_jnl, const uint8_t *a_body, uint32_t a_len);
int	vbk$jnl_commit	(VBK$JNL *a_jnl);
void	vbk$jnl_close	(VBK$JNL *a_jnl);
int	vbk$jnl_list	(VBK$OPTS *a_opts);
int	vbk$jnl_rebuild	(VBK$OPTS *a_opts);

/*
**  VBKATR.C
*/
int	vbk$atr_get	(const char *a_path, int a_fd, int a_xattrs, VBK$ATTR *a_attr, VBK$TLVB *a_xbuf);
int	vbk$atr_tlv	(const VBK$ATTR *a_attr, VBK$TLVB *a_tlvb);
int	vbk$atr_parse	(const uint8_t *a_body, uint32_t a_len, VBK$ATTR *a_attr);
int	vbk$atr_owner	(const VBK$OPTS *a_opts, const VBK$ATTR *a_attr, uid_t *a_uid, gid_t *a_gid);
int	vbk$atr_apply	(const VBK$OPTS *a_opts, const char *a_path, int a_fd, const VBK$ATTR *a_attr);
int	vbk$atr_settimes(const char *a_path, const VBK$ATTR *a_attr);
int	vbk$atr_setflags(const char *a_path, uint32_t a_flags);
const char *	vbk$atr_uname	(uint32_t a_uid);
const char *	vbk$atr_gname	(uint32_t a_gid);
int	vbk$atr_xtime	(const VBK$ATTR *a_attr, int a_timsrc, fao_time_t *a_time);

/*
**  VBKWLK.C
*/
int	vbk$walk	(const VBK$OPTS *a_opts, const char *a_spec, uint16_t a_baseidx, char *a_base, size_t a_basesz,
			int (*a_rtn) (const VBK$ENT *a_ent, void *a_arg), void *a_arg);
int	vbk$base	(const char *a_spec, char *a_base, size_t a_basesz);

/*
**  VBKPRE.C - the read-ahead of files for the save and the copy
*/
void	vbk$pre_start	(VBK$OPTS *a_opts);
void	vbk$pre_stop	(VBK$OPTS *a_opts);
void	vbk$pre_push	(struct vbk_pre_t *a_pre, const char *a_dir, const char * const *a_names, uint32_t a_n);
void	vbk$pre_pop	(struct vbk_pre_t *a_pre);
int	vbk$pre_take	(struct vbk_pre_t *a_pre, const char *a_path);

/*
**  Operations: VBKSAV.C, VBKRST.C, VBKLST.C, VBKCMP.C
*/
int	vbk$save	(VBK$OPTS *a_opts);
int	vbk$sav_open	(const VBK$OPTS *a_opts, const VBK$ENT *a_ent, int *a_fd, VBK$ATTR *a_attr, VBK$TLVB *a_xbuf);
int	vbk$restore	(VBK$OPTS *a_opts);
int	vbk$extract	(VBK$OPTS *a_opts);
int	vbk$list	(VBK$OPTS *a_opts);
int	vbk$compare	(VBK$OPTS *a_opts, const char *a_saveset);
int	vbk$copy	(VBK$OPTS *a_opts);

/*
**  VBKPHY.C - /PHYSICAL: a block device (or an image file), block by block
*/
int	vbk$phy_open	(VBK$OPTS *a_opts, const char *a_spec);
int	vbk$phy_check	(const char *a_spec, const struct stat *a_st, int a_write);
int	vbk$phy_restore	(VBK$OPTS *a_opts);
int	vbk$phy_yes	(const char *a_dev, uint64_t a_size, const char *a_spec);
int	vbk$phy_devsize	(int a_fd, const struct stat *a_st, uint64_t *a_size);

/*
**  VBKIMG.C - /IMAGE: a whole file system, its files and its identity
*/
int	vbk$img_prepare	(VBK$OPTS *a_opts);
int	vbk$img_restore	(VBK$OPTS *a_opts);

/*
**  VBKZPL.C - the compression of a save on several cores, the records in order
*/
struct vbk_zp_t *	vbk$zp_start	(VBK$WCTX *a_wctx);
int	vbk$zp_data	(struct vbk_zp_t *a_zp, uint32_t a_fileno, uint64_t a_off, const uint8_t *a_data, uint32_t a_n);
int	vbk$zp_record	(struct vbk_zp_t *a_zp, uint16_t a_type, const void *a_body, uint32_t a_len, VBK$LOC *a_loc);
int	vbk$zp_call	(struct vbk_zp_t *a_zp, void (*a_fn) (void *), void *a_arg);
int	vbk$zp_flush	(struct vbk_zp_t *a_zp);
void	vbk$zp_stop	(struct vbk_zp_t *a_zp, uint64_t *a_nin, uint64_t *a_nout);

/*
**  VBKRST.C - the creation of files, for the restore and for the copy
*/
struct	vbk_rest_t;

int	vbk$rst_begin	(VBK$OPTS *a_opts, struct vbk_rest_t **a_rst);
int	vbk$rst_file	(struct vbk_rest_t *a_rst, const uint8_t *a_body, uint32_t a_len);
int	vbk$rst_write	(struct vbk_rest_t *a_rst, uint64_t a_off, const uint8_t *a_data, uint32_t a_len);
int	vbk$rst_end	(struct vbk_rest_t *a_rst, uint64_t a_size, uint32_t a_crc, uint8_t a_status, int a_checkcrc);
int	vbk$rst_finish	(struct vbk_rest_t *a_rst, uint64_t *a_nfiles, uint64_t *a_nbytes);
const char *	vbk$rst_path	(const struct vbk_rest_t *a_rst);

/*
**  VBKXFR.C - a saveset to a saveset, block for block (pipes, volumes)
*/
int	vbk$transfer	(VBK$OPTS *a_opts);

/*
**  VBKRSH.C - a saveset on another node, node::file
*/
int	vbk$rsh_parse	(const char *a_spec, char *a_node, size_t a_nsz, const char **a_file);
int	vbk$rsh_open	(const char *a_node, const char *a_file, int a_output, int a_replace, pid_t *a_pid);
int	vbk$rsh_close	(const char *a_node, int a_output, int a_early, pid_t a_pid);

/*
**  VBKKEY.C - the passphrase of an encrypted saveset
*/
struct	vbk_rctx_t;

int	vbk$key_get	(VBK$OPTS *a_opts, const char *a_what, int a_confirm, const char **a_pass, size_t *a_plen);
int	vbk$key_unlock	(VBK$OPTS *a_opts, struct vbk_rctx_t *a_rctx, const char *a_spec);
uint32_t vbk$key_iter	(void);
void	vbk$key_wipe	(void);

/*
**  VBKHLP.C - /HELP out of the help library, a stand-in reporting
**  STS$K_WARN when HELP was not found at configuration time
*/
int	vbk$help	(int a_wordcnt, char **a_words, int a_page);

#ifdef	__cplusplus
}
#endif

#endif	/* __VBKDEF$H__ */
