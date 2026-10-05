#define	__MODULE__	"VBKVMS"
#define	__IDENT__	"X01-13"
#define	__REV__		"1.13.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkvms.c
**
**  ABSTRACT:	A saveset of OpenVMS BACKUP on the input: listed as BACKUP
**		lists it, restored into a directory, one file extracted.
**
**  DESCRIPTION: vbackup VMS.BCK /LIST[/FULL]	what BACKUP/LIST[/FULL] shows
**		vbackup VMS.BCK dir		the files under dir
**		vbackup VMS.BCK /EXTRACT=n out	one file
**
**		The saveset is known by its first block (VBK$VMS_PROBE), and
**		read by LIB/VBKVMS.C from its first block to its last.  The
**		names become Linux names - [A.B]C.TXT;3 is A/B/C.TXT, the older
**		versions keep ";n"; the text files become texts with LF;
**		the mode comes from the protection, the times from the
**		revision and the access dates; the owner (a UIC) is not put
**		back.  doc/vmsbackup.md tells it all.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  5-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-13		 5-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<strings.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<time.h>
#include	<unistd.h>
#include	<sys/stat.h>

#include	"vbkdef.h"
#include	"vbkvms.h"

/*
**  The file being restored or extracted
*/
typedef struct vbk_vmsout_t
{
	VBK$OPTS *	opts;
	VBK$VMSFILE	file;
	char		name [VBACKUP$K_SZ_PATH];	/* Its Linux name			*/
	char		path [VBACKUP$K_SZ_PATH];	/* ... where it goes			*/
	int		fd;			/* -1 - its data is not wanted			*/
	int		tostd;			/* ... the standard output			*/
	int		mode;			/* VBK$K_VMSCNV_*				*/
	VBK$VMSCNV	cnv;
	uint32_t	nextvbn;		/* The VBN its data should go on with		*/
	int		damaged;
	uint64_t	nbytes;			/* Octets written				*/
	int		failed;
} VBK$VMSOUT_T;


static	void	s_vbk$vts	(uint64_t a_vtime, struct timespec *a_ts);


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Is the file a saveset of OpenVMS BACKUP?  The header of its first
**	block is judged - not its CRC: a bad first block is the business of
**	the repair.  A pipe is not looked into.
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- it is one;
**	STS$K_WARN	- it is not.
**--
*/
int	vbk$vms_isss	(
	const	char *		a_spec
			)
{
uint8_t		*l_blk;
int		l_fd, l_status = STS$K_WARN;
ssize_t		l_n;
struct stat	l_st;

	/* A regular file only: a FIFO would hang the open, a device be read from */
	if ( !strcmp(a_spec, "-") || stat(a_spec, &l_st) || !S_ISREG(l_st.st_mode) || (0 > (l_fd = open(a_spec, O_RDONLY | O_CLOEXEC))) )
		return	STS$K_WARN;

	if ( (l_blk = malloc(VBK$K_VMSMAXBSZ)) )
		{
		l_n	= pread(l_fd, l_blk, VBK$K_VMSMAXBSZ, 0);

		/* By its header: a first block whose CRC fails is rebuilt from its group, not taken for a file */
		if ( (l_n >= VBK$K_VMSHDR) && (1 & vbk$vms_probe(l_blk, VBK$K_VMSHDR)) )
			l_status = STS$K_SUCCESS;

		free(l_blk);
		}

	close(l_fd);

	return	l_status;
}


/*
**  Open the saveset for reading: a diagnostic when it cannot be
*/
static	int	s_vbk$vopen	(
		VBK$VMS *	a_vms,
	const	char *		a_spec
			)
{
int	l_fd, l_status;

	if ( 0 > (l_fd = open(a_spec, O_RDONLY | O_CLOEXEC)) )
		return	$VBKMSG(VBACKUP$_OPENIN, a_spec, errno, strerror(errno));

	if ( !(1 & (l_status = vbk$vms_open(a_vms, l_fd, vbk$rdevent, (void *) a_spec))) )
		{
		close(l_fd);

		if ( l_status == STS$K_FATAL )
			return	$VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));

		if ( a_vms->err )
			return	$VBKMSG(VBACKUP$_READERR, a_spec, a_vms->err, strerror(a_vms->err));

		return	$VBKMSG(VBACKUP$_NOTSAVESET, a_spec);
		}

	if ( a_vms->nocrc )
		$VBKMSG(VBACKUP$_VMSNOCRC, a_spec);

	return	STS$K_SUCCESS;
}


static	void	s_vbk$vclose	(
		VBK$VMS *	a_vms
			)
{
	close(a_vms->fd);
	vbk$vms_close(a_vms);
}


/*
**  The Linux name of a file: the version when an older one - the same
**  name as the file before it (BACKUP writes the highest version first)
*/
static	size_t	s_vbk$vname	(
	const	VBK$VMSFILE *	a_file,
		char *		a_prev,
		size_t		a_prevsz,
		char *		a_out,
		size_t		a_size
			)
{
int	l_older = !a_file->isdir && vbk$vms_same(a_file->spec, a_file->speclen, a_prev, strlen(a_prev));

	vbk$strcpy(a_prevsz, a_prev, a_file->spec);

	return	vbk$vms_unix(a_file->spec, a_file->speclen, a_file->isdir, l_older, a_out, a_size);
}


/*
**  Is the file wanted: /SELECT, /EXCLUDE on its Linux name
*/
static	int	s_vbk$vwanted	(
	const	VBK$OPTS *	a_opts,
	const	char *		a_name
			)
{
	if ( a_opts->nexclude && (1 & vbk$match(a_name, a_opts->exclude, a_opts->nexclude)) )
		return	0;

	if ( a_opts->nselect && !(1 & vbk$match(a_name, a_opts->select, a_opts->nselect)) )
		return	0;

	return	1;
}


/*
**  A date of the listing, " 5-OCT-2026 18:44:13" - the hundredths away
*/
static	const char *	s_vbk$vdate	(
		uint64_t	a_vtime,
		char *		a_buf,
		size_t		a_size,
		size_t		a_len
			)
{
	if ( vbk$vms_date(a_vtime, a_buf, a_size) && (a_len < a_size) )
		a_buf [a_len] = '\0';

	return	a_buf;
}


/*
**  A protection: "System:RWED, Owner:RWED, Group:RE, World:"
*/
static	void	s_vbk$vprot	(
		uint16_t	a_fpro,
		char *		a_buf,
		size_t		a_size
			)
{
static	const char *	l_cls [] = { "System:", ", Owner:", ", Group:", ", World:" };
size_t	l_n = 0;

	for ( int i = 0; i < 4; i++ )
		{
		unsigned	l_deny = (a_fpro >> (4 * i)) & 0x0F;

		l_n	+= (size_t) $VBKFAOB(a_buf + l_n, a_size - l_n, "!AZ!AZ!AZ!AZ!AZ", l_cls [i], (l_deny & 1) ? "" : "R",
			(l_deny & 2) ? "" : "W", (l_deny & 4) ? "" : "E", (l_deny & 8) ? "" : "D");
		}
}


/*
**  The UIC as BACKUP shows it: [group,member] in octal
*/
static	void	s_vbk$vuic	(
		uint32_t	a_uic,
		char *		a_buf,
		size_t		a_size
			)
{
	$VBKFAOB(a_buf, a_size, "[!6ZL,!6ZL]", 0, 0);

	/* FAO has no octal of six digits: made here */
	for ( int l_part = 0; l_part < 2; l_part++ )
		{
		uint32_t	l_v = l_part ? (a_uic & 0xFFFF) : ((a_uic >> 16) & 0xFFFF);
		char *		l_p = a_buf + (l_part ? 14 : 7);

		for ( int i = 0; i < 6; i++, l_v >>= 3 )
			*--l_p	= (char) ('0' + (l_v & 7));
		}
}


/*
**  The heading of the listing: the SUMMARY, as BACKUP/LIST shows it
*/
static	void	s_vbk$vheading	(
		FILE *		a_out,
	const	char *		a_spec,
	const	VBK$VMSSUM *	a_sum
			)
{
char		l_date [32], l_uic [32];
const char *	l_os;

	switch ( a_sum->opsys )
		{
		case	0x0400:	l_os = "OpenVMS VAX";		break;
		case	0x0800:	l_os = "OpenVMS Alpha";		break;
		case	0x1000:	l_os = "OpenVMS I64";		break;
		case	0x2000:	l_os = "OpenVMS x86-64";	break;
		default:	l_os = NULL;
		}

	$VBKFAOP(a_out, "Listing of save set(s)\n\n");
	$VBKFAOP(a_out, "Save set:          !AZ\n", a_sum->ssname [0] ? a_sum->ssname : a_spec);
	$VBKFAOP(a_out, "Written by:        !12AZ\n", a_sum->user);
	s_vbk$vuic(a_sum->uic, l_uic, sizeof(l_uic));
	$VBKFAOP(a_out, "UIC:               !AZ\n", l_uic);

	if ( a_sum->date )
		$VBKFAOP(a_out, "Date:              !AZ\n", s_vbk$vdate(a_sum->date, l_date, sizeof(l_date), 23));

	$VBKFAOP(a_out, "Command:           !AZ\n", a_sum->command);

	if ( l_os )
		$VBKFAOP(a_out, "Operating system:  !AZ version !AZ\n", l_os, a_sum->sysver);
	else	$VBKFAOP(a_out, "Operating system:  !XW version !AZ\n", a_sum->opsys, a_sum->sysver);

	$VBKFAOP(a_out, "BACKUP version:    !AZ\n", a_sum->bckver);

	if ( a_sum->hascpuid )
		$VBKFAOP(a_out, "CPU ID register:   !XL\n", a_sum->cpuid);

	$VBKFAOP(a_out, "Written on:        !AZ\n", a_sum->node);
	$VBKFAOP(a_out, "Block size:        !UL\n", a_sum->bsize);

	if ( a_sum->grpsz )
		$VBKFAOP(a_out, "Group size:        !UL\n", a_sum->grpsz);

	$VBKFAOP(a_out, "Buffer count:      !UL\n\n", a_sum->bufcnt);
}


/*
**  One file of the listing, brief or full
*/
static	void	s_vbk$ventry	(
		FILE *		a_out,
	const	VBK$VMSFILE *	a_f,
		int		a_full
			)
{
char		l_d1 [32], l_d2 [32], l_s1 [64], l_s2 [64], l_prot [80], l_rat [96];
uint32_t	l_fidnum = a_f->fid [0] | ((uint32_t) (a_f->fid [2] >> 8) << 16);

	if ( !a_full )
		{
		/* As BACKUP/LIST: the name, its blocks in use, the creation date to the minute */
		if ( a_f->speclen <= 52 )
			$VBKFAOP(a_out, "!52AZ!9UL  !AZ\n", a_f->spec, a_f->used, s_vbk$vdate(a_f->credate, l_d1, sizeof(l_d1), 17));
		else	$VBKFAOP(a_out, "!AZ !UL  !AZ\n", a_f->spec, a_f->used, s_vbk$vdate(a_f->credate, l_d1, sizeof(l_d1), 17));

		return;
		}

	$VBKFAOP(a_out, "!AZ\n", a_f->spec);

	$VBKFAOB(l_s1, sizeof(l_s1), "!UL", a_f->hiblk);
	$VBKFAOP(a_out, "                      Size: !7UL/!10AZCreated: !AZ\n", a_f->used, l_s1,
		s_vbk$vdate(a_f->credate, l_d1, sizeof(l_d1), 20));

	s_vbk$vuic(a_f->uic, l_s1, sizeof(l_s1));
	$VBKFAOP(a_out, "                      Owner: !17AZRevised: !AZ (!UW)\n", l_s1,
		s_vbk$vdate(a_f->revdate, l_d1, sizeof(l_d1), 20), a_f->revision);

	$VBKFAOB(l_s2, sizeof(l_s2), "(!UL,!UW,!UB)", l_fidnum, a_f->fid [1], a_f->fid [2] & 0xFF);
	$VBKFAOP(a_out, "                      File ID: !15AZExpires: !AZ\n", l_s2,
		a_f->expdate ? s_vbk$vdate(a_f->expdate, l_d1, sizeof(l_d1), 20) : "<None specified>");

	$VBKFAOP(a_out, "                                              Backup:  !AZ\n",
		a_f->bakdate ? s_vbk$vdate(a_f->bakdate, l_d2, sizeof(l_d2), 20) : "<No backup done>");

	s_vbk$vprot(a_f->fpro, l_prot, sizeof(l_prot));
	$VBKFAOP(a_out, "  File protection:    !AZ\n", l_prot);
	$VBKFAOP(a_out, "  File organization:  !AZ\n", vbk$vms_orgname(a_f->org));

	if ( (a_f->org == VBK$K_VMSORG_IDX) || (a_f->org == VBK$K_VMSORG_REL) )
		$VBKFAOP(a_out, "  File attributes:    Allocation = !UL, Extend = !UW, maximum bucket size = !UB\n",
			a_f->hiblk, a_f->defext, a_f->bktsize);
	else	$VBKFAOP(a_out, "  File attributes:    Allocation = !UL, Extend = !UW\n", a_f->hiblk, a_f->defext);

	$VBKFAOP(a_out, "                      Global Buffer Count = !UW!AZ!AZ\n", a_f->gbc,
		(a_f->uchar & VBK$M_VMSCH_CONTIG) ? ", Contiguous" : (a_f->uchar & VBK$M_VMSCH_CONTIGB) ? ", Contiguous-best-try" : "",
		(a_f->uchar & VBK$M_VMSCH_LOCKED) ? ", Locked" : "");

	switch ( a_f->rfm )
		{
		case	VBK$K_VMSRFM_FIX:
			$VBKFAOP(a_out, "  Record format:      Fixed length !UW byte records\n", a_f->rsize);
			break;

		case	VBK$K_VMSRFM_VAR:
		case	VBK$K_VMSRFM_UDF:
			if ( a_f->maxrec )
				$VBKFAOP(a_out, "  Record format:      !AZ, maximum !UW bytes\n",
					(a_f->rfm == VBK$K_VMSRFM_VAR) ? "Variable length" : "Undefined", a_f->maxrec);
			else	$VBKFAOP(a_out, "  Record format:      !AZ\n", (a_f->rfm == VBK$K_VMSRFM_VAR) ? "Variable length" : "Undefined");
			break;

		case	VBK$K_VMSRFM_VFC:
			if ( a_f->maxrec )
				$VBKFAOP(a_out, "  Record format:      VFC, !UB byte header, maximum !UW bytes\n", a_f->vfcsize ? a_f->vfcsize : 2, a_f->maxrec);
			else	$VBKFAOP(a_out, "  Record format:      VFC, !UB byte header\n", a_f->vfcsize ? a_f->vfcsize : 2);
			break;

		default:
			$VBKFAOP(a_out, "  Record format:      !AZ\n", vbk$vms_rfmname(a_f->rfm));
		}

	l_rat [0] = '\0';

	if ( a_f->rattr & VBK$M_VMSRAT_FTN )
		strcat(l_rat, "Fortran carriage control");
	else if ( a_f->rattr & VBK$M_VMSRAT_CR )
		strcat(l_rat, "Carriage return");
	else if ( a_f->rattr & VBK$M_VMSRAT_PRN )
		strcat(l_rat, "Print file format");

	if ( a_f->rattr & VBK$M_VMSRAT_BLK )
		strcat(l_rat, l_rat [0] ? ", Non-spanned" : "Non-spanned");

	$VBKFAOP(a_out, "  Record attributes:  !AZ\n\n", l_rat [0] ? l_rat : "None");
}


/*
**  /FORMAT=LS: one line as ls -l, for the extfs of MC and MultiArc - the
**  Linux name, the octets up to the end of file, the revision date; the
**  owner the UIC
*/
static	void	s_vbk$vls	(
		FILE *		a_out,
	const	VBK$VMSFILE *	a_f,
	const	char *		a_name
			)
{
char		l_perm [16], l_tim [32], l_uic [32];
uint32_t	l_mode = vbk$vms_mode(a_f->fpro, a_f->isdir) | (a_f->isdir ? 0700 : 0);
struct timespec	l_ts;
struct tm	l_tm;
time_t		l_t;

	l_perm [0] = a_f->isdir ? 'd' : '-';

	for ( int i = 0; i < 9; i++ )
		l_perm [1 + i] = (l_mode & (0400 >> i)) ? "rwxrwxrwx" [i] : '-';

	l_perm [10] = '\0';

	s_vbk$vts(a_f->revdate, &l_ts);
	l_t	= (l_ts.tv_nsec == UTIME_OMIT) ? 0 : l_ts.tv_sec;
	localtime_r(&l_t, &l_tm);
	$VBKFAOB(l_tim, sizeof(l_tim), "!2ZL-!2ZL-!4ZL !2ZL:!2ZL:!2ZL", l_tm.tm_mon + 1, l_tm.tm_mday, l_tm.tm_year + 1900,
		l_tm.tm_hour, l_tm.tm_min, l_tm.tm_sec);

	$VBKFAOB(l_uic, sizeof(l_uic), "!UL,!UL", (a_f->uic >> 16) & 0xFFFF, a_f->uic & 0xFFFF);

	$VBKFAOP(a_out, "!AZ   1 !AZ!#*  vms      !#UQ !AZ !AZ\n", l_perm, l_uic, VBK$PADW(8, strlen(l_uic)),
		VBK$NUMW(10, a_f->bytes), a_f->bytes, l_tim, a_name);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	/LIST of a saveset of BACKUP: as BACKUP/LIST shows it, brief or
**	/FULL, or /FORMAT=LS by the Linux names (MC); /SELECT and /EXCLUDE
**	on the Linux names.
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the condition reported.
**--
*/
int	vbk$vms_list	(
		VBK$OPTS *	a_opts
			)
{
const char *	l_spec = a_opts->input [0];
VBK$VMS		l_vms;
VBK$VMSREC	l_rec;
VBK$VMSFILE	l_f;
VBK$VMSSUM	l_sum;
FILE *		l_out = stdout;
char		l_prev [VBK$K_VMSNAME] = "", l_name [VBACKUP$K_SZ_PATH];
uint64_t	l_nfiles = 0, l_nblocks = 0;
int		l_full = (a_opts->lstfmt == VBACKUP$K_LST_FULL), l_ls = (a_opts->lstfmt == VBACKUP$K_LST_LS), l_head = l_ls;

	if ( !(1 & s_vbk$vopen(&l_vms, l_spec)) )
		return	STS$K_ERROR;

	if ( a_opts->lstfile [0] && !(l_out = fopen(a_opts->lstfile, "w")) )
		{
		s_vbk$vclose(&l_vms);

		return	$VBKMSG(VBACKUP$_OPENOUT, a_opts->lstfile, errno, strerror(errno));
		}

	while ( 1 & vbk$vms_next(&l_vms, &l_rec) )
		{
		if ( (l_rec.rtype == VBK$K_VMSRT_SUMMARY) && !l_head && (1 & vbk$vms_summary(l_rec.body, l_rec.len, &l_sum)) )
			{
			s_vbk$vheading(l_out, l_spec, &l_sum);
			l_head	= 1;
			continue;
			}

		if ( l_rec.rtype != VBK$K_VMSRT_FILE )
			continue;

		/* The SUMMARY lost: the heading from what the open knows */
		if ( !l_head )
			{
			memset(&l_sum, 0, sizeof(l_sum));
			l_sum.bsize	= l_vms.bsize;
			l_sum.grpsz	= l_vms.grpsz;
			s_vbk$vheading(l_out, l_spec, &l_sum);
			l_head	= 1;
			}

		if ( !(1 & vbk$vms_file(l_rec.body, l_rec.len, &l_f)) )
			{
			$VBKMSG(VBACKUP$_BADREC, l_rec.blkno, 1);
			continue;
			}

		if ( !s_vbk$vname(&l_f, l_prev, sizeof(l_prev), l_name, sizeof(l_name)) || !s_vbk$vwanted(a_opts, l_name) )
			continue;

		if ( l_ls )
			s_vbk$vls(l_out, &l_f, l_name);
		else	s_vbk$ventry(l_out, &l_f, l_full);

		l_nfiles++;
		l_nblocks += l_f.used;
		}

	if ( !l_head )
		s_vbk$vheading(l_out, l_spec, &l_vms.sum);

	if ( !l_ls )
		$VBKFAOP(l_out, "\nTotal of !UQ file!%S, !UQ block!%S\nEnd of save set\n\n", l_nfiles, l_nblocks);

	if ( (l_out != stdout) && fclose(l_out) )
		$VBKMSG(VBACKUP$_WRITERR, a_opts->lstfile, errno, strerror(errno));
	else if ( l_out == stdout )
		fflush(stdout);

	s_vbk$vclose(&l_vms);

	return	STS$K_SUCCESS;
}


/*
**  The output function of the conversion: octets to the file in hand
*/
static	int	s_vbk$vwrite	(
		void *		a_arg,
	const	uint8_t *	a_buf,
		size_t		a_len
			)
{
VBK$VMSOUT_T *	l_o = (VBK$VMSOUT_T *) a_arg;

	while ( a_len )
		{
		ssize_t	l_rc = write(l_o->fd, a_buf, a_len);

		if ( l_rc < 0 )
			{
			if ( errno == EINTR )
				continue;

			$VBKMSG(VBACKUP$_WRITERR, l_o->tostd ? "(standard output)" : l_o->path, errno, strerror(errno));

			return	STS$K_ERROR;
			}

		a_buf	+= l_rc;
		a_len	-= (size_t) l_rc;
		l_o->nbytes += (uint64_t) l_rc;
		}

	return	STS$K_SUCCESS;
}


/*
**  A VBN record of the file in hand: its blocks, up to the end of file.
**  A record that goes back belongs to another file - its FILE record was
**  lost: 0, the file is ended.
*/
static	int	s_vbk$vdata	(
		VBK$VMSOUT_T *	a_o,
	const	VBK$VMSREC *	a_rec
			)
{
uint64_t	l_off = (uint64_t) (a_rec->address - 1) * 512, l_n = a_rec->len;

	if ( !a_rec->address || (a_rec->address < a_o->nextvbn) )
		return	0;

	if ( a_rec->resync || (a_rec->address != a_o->nextvbn) )
		a_o->damaged	= 1;

	a_o->nextvbn	= a_rec->address + a_rec->len / 512;

	if ( (a_o->fd < 0) || a_o->failed || (l_off >= a_o->file.bytes) )
		return	1;

	if ( l_off + l_n > a_o->file.bytes )
		l_n	= a_o->file.bytes - l_off;

	if ( (a_o->mode == VBK$K_VMSCNV_RAW) && !a_o->tostd )
		{
		/* As it is: at its place, so that a lost block is a hole, not a shift */
		if ( pwrite(a_o->fd, a_rec->body, (size_t) l_n, (off_t) l_off) != (ssize_t) l_n )
			{
			$VBKMSG(VBACKUP$_WRITERR, a_o->path, errno, strerror(errno));
			a_o->failed	= 1;
			}

		a_o->nbytes += l_n;

		return	1;
		}

	if ( !(1 & vbk$vms_cnv(&a_o->cnv, a_rec->body, (size_t) l_n)) )
		a_o->failed	= 1;

	return	1;
}


/*
**  The VMS time of the system that wrote it is a time of its own zone:
**  taken as the local time here
*/
static	void	s_vbk$vts	(
		uint64_t	a_vtime,
		struct timespec *a_ts
			)
{
int64_t		l_sec;
uint32_t	l_nsec;
struct tm	l_tm;
time_t		l_t;

	if ( !vbk$vms_time(a_vtime, &l_sec, &l_nsec) )
		{
		a_ts->tv_sec	= 0;
		a_ts->tv_nsec	= UTIME_OMIT;

		return;
		}

	l_t	= (time_t) l_sec;
	gmtime_r(&l_t, &l_tm);
	l_tm.tm_isdst = -1;

	a_ts->tv_sec	= mktime(&l_tm);
	a_ts->tv_nsec	= l_nsec;
}


/*
**  The end of the file in hand: what is held written, the mode, the times
*/
static	int	s_vbk$vfend	(
		VBK$VMSOUT_T *	a_o
			)
{
struct timespec	l_ts [2];
int		l_status = STS$K_SUCCESS;

	if ( a_o->fd < 0 )
		return	STS$K_SUCCESS;

	if ( (a_o->mode != VBK$K_VMSCNV_RAW) || a_o->tostd )
		if ( !(1 & vbk$vms_cnvend(&a_o->cnv)) )
			a_o->failed	= 1;

	/* What did not come: the blocks up to the end of file */
	if ( (uint64_t) (a_o->nextvbn - 1) * 512 < a_o->file.bytes )
		a_o->damaged	= 1;

	if ( !a_o->tostd )
		{
		if ( (a_o->mode == VBK$K_VMSCNV_RAW) && ftruncate(a_o->fd, (off_t) a_o->file.bytes) )
			a_o->failed	= 1;

		fchmod(a_o->fd, (mode_t) vbk$vms_mode(a_o->file.fpro, 0));

		s_vbk$vts(a_o->file.accdate ? a_o->file.accdate : a_o->file.revdate, &l_ts [0]);
		s_vbk$vts(a_o->file.revdate, &l_ts [1]);
		futimens(a_o->fd, l_ts);

		if ( close(a_o->fd) && !a_o->failed )
			{
			$VBKMSG(VBACKUP$_WRITERR, a_o->path, errno, strerror(errno));
			a_o->failed	= 1;
			}
		}

	if ( a_o->damaged )
		l_status = $VBKMSG(VBACKUP$_FILDAMAGED, a_o->tostd ? a_o->name : a_o->path);

	a_o->fd	= -1;

	return	a_o->failed ? STS$K_ERROR : l_status;
}


/*
**  The directories over a file made, none of them a link
*/
static	int	s_vbk$vparents	(
		char *		a_path,
		size_t		a_from
			)
{
struct stat	l_st;

	for ( char *l_p = a_path + a_from + 1; (l_p = strchr(l_p, '/')); l_p++ )
		{
		*l_p	= '\0';

		if ( (mkdir(a_path, 0700) && (errno != EEXIST)) || lstat(a_path, &l_st) || !S_ISDIR(l_st.st_mode) )
			{
			if ( !errno || (errno == EEXIST) )
				errno	= ENOTDIR;

			*l_p	= '/';

			return	STS$K_ERROR;
			}

		*l_p	= '/';
		}

	return	STS$K_SUCCESS;
}


/*
**  A FILE record in a restore: the directory made, or the file created
*/
static	int	s_vbk$vbegin	(
		VBK$VMSOUT_T *	a_o
			)
{
VBK$OPTS *	l_opts = a_o->opts;
const char *	l_outdir = l_opts->output;
int		l_status;

	a_o->fd		= -1;
	a_o->damaged	= 0;
	a_o->failed	= 0;
	a_o->nbytes	= 0;
	a_o->nextvbn	= 1;
	a_o->mode	= vbk$vms_cnvmode(&a_o->file);

	if ( !(1 & vbk$mkpath(l_outdir, a_o->name, (uint32_t) strlen(a_o->name), a_o->path, sizeof(a_o->path))) )
		return	$VBKMSG(VBACKUP$_OPENOUT, a_o->name, EINVAL, "a name that leads out of the output directory");

	if ( l_opts->confirm )
		{
		if ( STS$K_FATAL == (l_status = vbk$confirm("Restore", a_o->path)) )
			return	STS$K_FATAL;

		if ( !(1 & l_status) )
			return	STS$K_SUCCESS;
		}

	if ( !(1 & s_vbk$vparents(a_o->path, strlen(l_outdir))) )
		return	$VBKMSG(VBACKUP$_OPENOUT, a_o->path, errno, strerror(errno));

	if ( a_o->file.isdir )
		{
		struct stat	l_st;

		if ( (mkdir(a_o->path, 0700) && (errno != EEXIST)) || lstat(a_o->path, &l_st) || !S_ISDIR(l_st.st_mode) )
			return	$VBKMSG(VBACKUP$_OPENOUT, a_o->path, errno ? errno : ENOTDIR, strerror(errno ? errno : ENOTDIR));

		/* The owner must still create in it: the rest of the protection is its own */
		chmod(a_o->path, (mode_t) (vbk$vms_mode(a_o->file.fpro, 1) | 0700));
		l_opts->rstfiles++;

		if ( l_opts->log )
			$VBKMSG(VBACKUP$_RESTORED, a_o->path);

		return	STS$K_SUCCESS;
		}

	for ( int l_try = 0; ; l_try++ )
		{
		if ( 0 <= (a_o->fd = open(a_o->path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)) )
			break;

		if ( (errno != EEXIST) || l_try )
			return	$VBKMSG(VBACKUP$_OPENOUT, a_o->path, errno, strerror(errno));

		if ( !l_opts->replace )
			return	$VBKMSG(VBACKUP$_FILEEXISTS, a_o->path);

		/* /REPLACE: removed and created anew, a link at its name never written through */
		if ( unlink(a_o->path) )
			return	$VBKMSG(VBACKUP$_OPENOUT, a_o->path, errno, strerror(errno));
		}

	if ( (a_o->file.org != VBK$K_VMSORG_SEQ) || (a_o->mode == VBK$K_VMSCNV_RAW && (a_o->file.rfm == VBK$K_VMSRFM_VAR
		|| a_o->file.rfm == VBK$K_VMSRFM_VFC)) )
		$VBKMSG(VBACKUP$_VMSRAW, a_o->path, vbk$vms_orgname(a_o->file.org), vbk$vms_rfmname(a_o->file.rfm));

	vbk$vms_cnvinit(&a_o->cnv, &a_o->file, a_o->mode, s_vbk$vwrite, a_o);

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	A saveset of BACKUP restored into the output directory.
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the condition reported.
**--
*/
int	vbk$vms_restore	(
		VBK$OPTS *	a_opts
			)
{
const char *	l_spec = a_opts->input [0];
VBK$VMS		l_vms;
VBK$VMSREC	l_rec;
static	VBK$VMSOUT_T	l_o;
char		l_prev [VBK$K_VMSNAME] = "";
int		l_infile = 0, l_status = STS$K_SUCCESS;

	if ( !(1 & s_vbk$vopen(&l_vms, l_spec)) )
		return	STS$K_ERROR;

	memset(&l_o, 0, sizeof(l_o));
	l_o.opts	= a_opts;
	l_o.fd		= -1;
	a_opts->rstfiles = a_opts->rstbytes = 0;

	$VBKMSG(VBACKUP$_VMSSAVESET, l_spec, l_vms.bsize, l_vms.grpsz);

	/* The output directory, made with what is missing over it */
	{
	char		l_dir [VBACKUP$K_SZ_PATH];
	struct stat	l_st;

	vbk$strcpy(sizeof(l_dir), l_dir, a_opts->output);

	for ( char *l_p = l_dir + 1; ; l_p++ )
		{
		if ( *l_p && (*l_p != '/') )
			continue;

		char	l_c = *l_p;

		*l_p	= '\0';

		if ( mkdir(l_dir, 0755) && (errno != EEXIST) )
			{
			s_vbk$vclose(&l_vms);

			return	$VBKMSG(VBACKUP$_OPENOUT, l_dir, errno, strerror(errno));
			}

		if ( !(*l_p = l_c) )
			break;
		}

	if ( stat(a_opts->output, &l_st) || !S_ISDIR(l_st.st_mode) )
		{
		s_vbk$vclose(&l_vms);

		return	$VBKMSG(VBACKUP$_OPENOUT, a_opts->output, ENOTDIR, strerror(ENOTDIR));
		}
	}

	while ( 1 & vbk$vms_next(&l_vms, &l_rec) )
		{
		if ( l_rec.rtype == VBK$K_VMSRT_FILE )
			{
			if ( l_infile )
				{
				if ( 1 & s_vbk$vfend(&l_o) )
					a_opts->rstfiles++, a_opts->rstbytes += l_o.nbytes;

				if ( a_opts->log )
					$VBKMSG(VBACKUP$_RESTORED, l_o.path);
				}

			l_infile = 0;

			if ( !(1 & vbk$vms_file(l_rec.body, l_rec.len, &l_o.file)) )
				{
				$VBKMSG(VBACKUP$_BADREC, l_rec.blkno, 1);
				continue;
				}

			if ( !s_vbk$vname(&l_o.file, l_prev, sizeof(l_prev), l_o.name, sizeof(l_o.name)) || !s_vbk$vwanted(a_opts, l_o.name) )
				continue;

			if ( STS$K_FATAL == (l_status = s_vbk$vbegin(&l_o)) )
				break;

			l_status = STS$K_SUCCESS;
			l_infile = (l_o.fd >= 0);
			continue;
			}

		if ( (l_rec.rtype == VBK$K_VMSRT_VBN) && l_infile && !s_vbk$vdata(&l_o, &l_rec) )
			{
			/* Data of a file whose FILE record was lost: the file in hand ends here */
			if ( 1 & s_vbk$vfend(&l_o) )
				a_opts->rstfiles++, a_opts->rstbytes += l_o.nbytes;

			l_infile = 0;
			}
		}

	if ( l_infile )
		{
		if ( 1 & s_vbk$vfend(&l_o) )
			a_opts->rstfiles++, a_opts->rstbytes += l_o.nbytes;

		if ( a_opts->log )
			$VBKMSG(VBACKUP$_RESTORED, l_o.path);
		}

	$VBKMSG(VBACKUP$_RESTSUMM, a_opts->rstfiles, a_opts->rstbytes);
	s_vbk$vclose(&l_vms);

	return	(l_status == STS$K_FATAL) ? STS$K_ERROR : STS$K_SUCCESS;
}


/*
**  Is it the file /EXTRACT asks for: its Linux name with or without the
**  version, or its specification of VMS (the case not minded)
*/
static	int	s_vbk$vis	(
	const	char *		a_want,
	const	VBK$VMSFILE *	a_f
			)
{
char	l_name [VBACKUP$K_SZ_PATH];

	if ( !strcasecmp(a_want, a_f->spec) )
		return	1;

	if ( vbk$vms_unix(a_f->spec, a_f->speclen, 0, 0, l_name, sizeof(l_name)) && !strcmp(a_want, l_name) )
		return	1;

	return	vbk$vms_unix(a_f->spec, a_f->speclen, 0, 1, l_name, sizeof(l_name)) && !strcmp(a_want, l_name);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	/EXTRACT=name of a saveset of BACKUP: the first file of that name -
**	the highest version - into the output, or to the standard output.
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the condition reported.
**--
*/
int	vbk$vms_extract	(
		VBK$OPTS *	a_opts
			)
{
const char *	l_spec = a_opts->input [0];
VBK$VMS		l_vms;
VBK$VMSREC	l_rec;
static	VBK$VMSOUT_T	l_o;
int		l_found = 0, l_status = STS$K_ERROR;

	if ( !(1 & s_vbk$vopen(&l_vms, l_spec)) )
		return	STS$K_ERROR;

	memset(&l_o, 0, sizeof(l_o));
	l_o.opts	= a_opts;
	l_o.fd		= -1;
	l_o.tostd	= !a_opts->output [0] || !strcmp(a_opts->output, "-");

	while ( 1 & vbk$vms_next(&l_vms, &l_rec) )
		{
		if ( l_rec.rtype == VBK$K_VMSRT_FILE )
			{
			if ( l_found )
				break;

			if ( !(1 & vbk$vms_file(l_rec.body, l_rec.len, &l_o.file)) || l_o.file.isdir || !s_vbk$vis(a_opts->extract, &l_o.file) )
				continue;

			l_found		= 1;
			l_o.nextvbn	= 1;
			l_o.mode	= vbk$vms_cnvmode(&l_o.file);
			vbk$strcpy(sizeof(l_o.name), l_o.name, a_opts->extract);
			vbk$strcpy(sizeof(l_o.path), l_o.path, l_o.tostd ? "(standard output)" : a_opts->output);

			if ( l_o.tostd )
				l_o.fd	= STDOUT_FILENO;
			else if ( !a_opts->replace && !access(a_opts->output, F_OK) )
				{
				$VBKMSG(VBACKUP$_FILEEXISTS, a_opts->output);
				break;
				}
			else if ( 0 > (l_o.fd = open(a_opts->output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)) )
				{
				$VBKMSG(VBACKUP$_OPENOUT, a_opts->output, errno, strerror(errno));
				break;
				}

			vbk$vms_cnvinit(&l_o.cnv, &l_o.file, l_o.mode, s_vbk$vwrite, &l_o);
			continue;
			}

		if ( (l_rec.rtype == VBK$K_VMSRT_VBN) && l_found && (l_o.fd >= 0) && !s_vbk$vdata(&l_o, &l_rec) )
			break;
		}

	if ( !l_found )
		$VBKMSG(VBACKUP$_NOTFOUND, a_opts->extract);
	else if ( l_o.fd >= 0 )
		l_status = s_vbk$vfend(&l_o);

	s_vbk$vclose(&l_vms);

	return	l_status;
}
