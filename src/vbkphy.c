#define	__MODULE__	"VBKPHY"
#define	__IDENT__	"X01-07"
#define	__REV__		"1.7.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkphy.c
**
**  ABSTRACT:	/PHYSICAL: a block device - or an image file - saved and
**		restored block by block, as BACKUP/PHYSICAL does.
**
**  DESCRIPTION: In the saveset a device is one regular file (format.md,
**		6.8): its data at their offsets, the runs of zeros left out
**		as holes, the SUMMARY saying PHYSICAL, DEVSIZE and SECTORSIZE.
**		So every reader, the older ones included, makes a sparse
**		image file of it; only a restore /PHYSICAL writes a device.
**
**		The save itself is in VBKSAV.C (the records, the catalog,
**		/VERIFY are the ones of any save); here are the guards and
**		the restore.
**
**		A device written to while it is read gives an image that is
**		torn - and looks sound until it is restored.  So a device
**		that is mounted read-write, or one of whose partitions is,
**		is refused; so is one in use as a physical volume of LVM, a
**		member of a RAID, a dm-crypt container (anything under
**		holders/) or swap.  Mounted read-only, it is taken.
**
**		A restore overwrites a whole device.  It is done with
**		/REPLACE only, never to a device that is mounted or in use,
**		never to one smaller than the one saved; from a terminal the
**		word YES must be typed.  The holes - the runs of zeros - are
**		written as zeros: a device, unlike a file, keeps its old
**		bytes where nothing is written.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  4-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-07		 5-OCT-2026	RRL
**		Texts made by FAO; PHYSSUMM always.
**
**	X01-06		 5-OCT-2026	RRL
**		An encrypted saveset: VBK$KEY_UNLOCK before anything is read.
**
**	X01-04		 4-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<dirent.h>
#include	<sys/stat.h>
#include	<sys/ioctl.h>
#include	<sys/sysmacros.h>
#include	<linux/fs.h>

#include	"vbkdef.h"

#define	VBK$K_PHYMAXDEV	256			/* A device and its partitions			*/


/*
**  The device numbers to look at: the device, and - a whole disk - each
**  of its partitions (/sys/dev/block/M:m/<part>/partition)
*/
static	unsigned	s_vbk$devs	(
		dev_t		a_dev,
		dev_t *		a_out
			)
{
char		l_dir [64], l_path [VBACKUP$K_SZ_PATH];
DIR *		l_d;
struct dirent *	l_de;
unsigned	l_n = 0;

	a_out [l_n++] = a_dev;

	$VBKFAOB(l_dir, sizeof(l_dir), "/sys/dev/block/!UL:!UL", major(a_dev), minor(a_dev));

	if ( !(l_d = opendir(l_dir)) )
		return	l_n;

	while ( (l_de = readdir(l_d)) && (l_n < VBK$K_PHYMAXDEV) )
		{
		FILE *		l_f;
		unsigned	l_ma, l_mi;

		if ( l_de->d_name [0] == '.' )
			continue;

		if ( ($VBKFAOB(l_path, sizeof(l_path), "!AZ/!AZ/partition", l_dir, l_de->d_name) >= (int) sizeof(l_path)) || access(l_path, F_OK) )
			continue;

		$VBKFAOB(l_path, sizeof(l_path), "!AZ/!AZ/dev", l_dir, l_de->d_name);

		if ( (l_f = fopen(l_path, "re")) )
			{
			if ( 2 == fscanf(l_f, "%u:%u", &l_ma, &l_mi) )
				a_out [l_n++] = makedev(l_ma, l_mi);

			fclose(l_f);
			}
		}

	closedir(l_d);

	return	l_n;
}


/*
**  Is the device mounted - read-write, or at all?  The mount options of
**  /proc/self/mountinfo (field 6, the ones of this mount), not those of
**  the super block.
**
**  RETURN VALUE:
**	0 - not mounted; 1 - read-only only; 2 - read-write somewhere.
*/
static	int	s_vbk$mounted	(
		dev_t		a_dev,
		char *		a_where,
		size_t		a_wsz
			)
{
FILE *		l_f;
char		l_line [8192];
int		l_state = 0;

	if ( !(l_f = fopen("/proc/self/mountinfo", "re")) )
		return	0;

	while ( fgets(l_line, sizeof(l_line), l_f) )
		{
		unsigned	l_ma, l_mi;
		char		l_mnt [4096], l_opts [1024];

		if ( 4 != sscanf(l_line, "%*s %*s %u:%u %*s %4095s %1023s", &l_ma, &l_mi, l_mnt, l_opts) )
			continue;

		if ( makedev(l_ma, l_mi) != a_dev )
			continue;

		if ( !strncmp(l_opts, "rw", 2) && ((l_opts [2] == ',') || !l_opts [2]) )
			{
			l_state	= 2;
			vbk$strcpy(a_wsz, a_where, l_mnt);
			break;
			}

		if ( !l_state )
			vbk$strcpy(a_wsz, a_where, l_mnt);

		l_state	= 1;
		}

	fclose(l_f);

	return	l_state;
}


/*
**  Does something hold the device - LVM, RAID, dm-crypt: an entry in its
**  holders/ - or is it swap?  <a_what> receives the first such user.
*/
static	int	s_vbk$held	(
		dev_t		a_dev,
		char *		a_what,
		size_t		a_wsz
			)
{
char		l_dir [64], l_line [4096];
DIR *		l_d;
struct dirent *	l_de;
FILE *		l_f;
int		l_held = 0;

	$VBKFAOB(l_dir, sizeof(l_dir), "/sys/dev/block/!UL:!UL/holders", major(a_dev), minor(a_dev));

	if ( (l_d = opendir(l_dir)) )
		{
		while ( !l_held && (l_de = readdir(l_d)) )
			if ( l_de->d_name [0] != '.' )
				{
				$VBKFAOB(a_what, a_wsz, "held by !AZ", l_de->d_name);
				l_held	= 1;
				}

		closedir(l_d);
		}

	if ( !l_held && (l_f = fopen("/proc/swaps", "re")) )
		{
		while ( !l_held && fgets(l_line, sizeof(l_line), l_f) )
			{
			char		l_spec [4096];
			struct stat	l_st;

			if ( (1 == sscanf(l_line, "%4095s", l_spec)) && (l_spec [0] == '/') && !stat(l_spec, &l_st)
				&& S_ISBLK(l_st.st_mode) && (l_st.st_rdev == a_dev) )
				{
				vbk$strcpy(a_wsz, a_what, "swap");
				l_held	= 1;
				}
			}

		fclose(l_f);
		}

	return	l_held;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	May this device be read (saved) or written (restored) block by
**	block?  A file is always fine; a block device not when it, or a
**	partition of it, is mounted - read-write for a save, at all for a
**	restore - or is in use.
**
**  FORMAL PARAMETERS:
**
**	a_spec		The device, for the messages
**	a_st		Its stat
**	a_write		0 - to be read, 1 - to be written
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- it may;
**	STS$K_INFO	- it may, and it is mounted read-only (a save);
**	STS$K_ERROR	- it may not, and why has been said.
**--
*/
int	vbk$phy_check	(
	const	char *		a_spec,
	const	struct stat *	a_st,
		int		a_write
			)
{
dev_t		l_devs [VBK$K_PHYMAXDEV];
unsigned	l_n;
char		l_where [4096], l_what [VBACKUP$K_SZ_PATH + 64];
int		l_ro = 0, l_m;

	if ( !S_ISBLK(a_st->st_mode) )
		return	STS$K_SUCCESS;

	l_n	= s_vbk$devs(a_st->st_rdev, l_devs);

	for ( unsigned i = 0; i < l_n; i++ )
		{
		if ( (l_m = s_vbk$mounted(l_devs [i], l_where, sizeof(l_where))) && (a_write || (l_m == 2)) )
			{
			$VBKFAOB(l_what, sizeof(l_what), "!AZ!AZ", a_spec, i ? " (a partition of it)" : "");

			return	$VBKMSG(VBACKUP$_PHYSMOUNTED, l_what, a_write ? "" : " read-write", l_where,
				a_write ? "nothing is written to a mounted device, unmount it" : "unmount it, mount it read-only, or save a snapshot");
			}

		if ( l_m == 1 )
			l_ro	= 1;

		if ( s_vbk$held(l_devs [i], l_what, sizeof(l_what)) )
			return	$VBKMSG(VBACKUP$_PHYSHELD, a_spec, l_what);
		}

	return	l_ro ? STS$K_INFO : STS$K_SUCCESS;
}


/*
**  The size and the logical sector of an open device or file
*/
static	int	s_vbk$size	(
		int		a_fd,
	const	struct stat *	a_st,
		uint64_t *	a_size,
		uint32_t *	a_sector
			)
{
int	l_ssz = 512;

	*a_size		= (uint64_t) a_st->st_size;
	*a_sector	= 512;

	if ( !S_ISBLK(a_st->st_mode) )
		return	STS$K_SUCCESS;

	if ( ioctl(a_fd, BLKGETSIZE64, a_size) )
		return	STS$K_ERROR;

	if ( !ioctl(a_fd, BLKSSZGET, &l_ssz) && (l_ssz > 0) )
		*a_sector = (uint32_t) l_ssz;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Open the device of a /PHYSICAL save: the guards, the size, the
**	descriptor - all into OPTS before the saveset is begun, its
**	SUMMARY says the size.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: PHYSFD, PHYSSIZE, PHYSSECTOR are set
**	a_spec		The device or image file
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_ERROR - said why.
**--
*/
int	vbk$phy_open	(
		VBK$OPTS *	a_opts,
	const	char *		a_spec
			)
{
struct stat	l_st;
int		l_fd, l_status;

	a_opts->physfd	= -1;

	if ( stat(a_spec, &l_st) )
		return	$VBKMSG(VBACKUP$_OPENIN, a_spec, errno, strerror(errno));

	if ( !S_ISBLK(l_st.st_mode) && !S_ISREG(l_st.st_mode) )
		return	$VBKMSG(VBACKUP$_PHYSNOTDEV, a_spec);

	if ( !(1 & (l_status = vbk$phy_check(a_spec, &l_st, 0))) )
		return	STS$K_ERROR;

	/* O_EXCL on a device: the kernel's own refusal of one in use - but a read-only mount holds it too */
	l_fd	= open(a_spec, O_RDONLY | O_CLOEXEC | ((S_ISBLK(l_st.st_mode) && (l_status != STS$K_INFO)) ? O_EXCL : 0));

	if ( l_fd < 0 )
		return	(errno == EBUSY) ? $VBKMSG(VBACKUP$_PHYSHELD, a_spec, "the kernel says it is busy")
					 : $VBKMSG(VBACKUP$_OPENIN, a_spec, errno, strerror(errno));

	if ( !(1 & s_vbk$size(l_fd, &l_st, &a_opts->physsize, &a_opts->physsector)) )
		{
		int	l_err = errno;

		close(l_fd);

		return	$VBKMSG(VBACKUP$_OPENIN, a_spec, l_err, strerror(l_err));
		}

	a_opts->physfd	= l_fd;

	return	STS$K_SUCCESS;
}


/*
**  The size of an open device, or file - for /IMAGE too
*/
int	vbk$phy_devsize	(
		int		a_fd,
	const	struct stat *	a_st,
		uint64_t *	a_size
			)
{
uint32_t	l_sector;

	return	s_vbk$size(a_fd, a_st, a_size, &l_sector);
}


/*
**  Zeros from <a_from> up to <a_to> on the output: a device keeps its old
**  bytes where nothing is written.  BLKZEROOUT when the device takes it,
**  a buffer of zeros else.
*/
static	int	s_vbk$zero	(
		int		a_fd,
		int		a_isdev,
		uint64_t	a_from,
		uint64_t	a_to
			)
{
static	const uint8_t	l_zero [65536];
uint64_t		l_range [2];
ssize_t			l_rc;

	if ( a_to <= a_from )
		return	STS$K_SUCCESS;

	/* A file was made sparse by its ftruncate: nothing to do */
	if ( !a_isdev )
		return	STS$K_SUCCESS;

	l_range [0] = a_from;
	l_range [1] = a_to - a_from;

	if ( !(a_from % 512) && !(l_range [1] % 512) && !ioctl(a_fd, BLKZEROOUT, l_range) )
		return	STS$K_SUCCESS;

	while ( a_from < a_to )
		{
		size_t	l_n = ((a_to - a_from) < sizeof(l_zero)) ? (size_t) (a_to - a_from) : sizeof(l_zero);

		if ( 0 > (l_rc = pwrite(a_fd, l_zero, l_n, (off_t) a_from)) )
			{
			if ( errno == EINTR )
				continue;

			return	STS$K_ERROR;
			}

		a_from	+= (uint64_t) l_rc;
		}

	return	STS$K_SUCCESS;
}


/*
**  From a terminal, the word YES - y is too easy to give for a disk
*/
int	vbk$phy_yes	(
	const	char *		a_dev,
		uint64_t	a_size,
	const	char *		a_spec
			)
{
char	l_ans [64];

	if ( !isatty(STDIN_FILENO) )
		return	1;

	$VBKFAOP(stderr, "Everything on !AZ (!UQ bytes) is to be overwritten with the device saved in !AZ.\nType YES to go on: ",
		a_dev, a_size, a_spec);
	fflush(stderr);

	if ( !fgets(l_ans, sizeof(l_ans), stdin) )
		return	0;

	l_ans [strcspn(l_ans, "\r\n")] = '\0';

	return	!strcmp(l_ans, "YES");
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Restore a /PHYSICAL saveset onto a device or into an image file.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: INPUT [0] the saveset, OUTPUT the device
**			or file, /REPLACE
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- written;
**	STS$K_WARN	- written, damaged parts reported;
**	STS$K_ERROR	- refused or failed, said why.
**--
*/
int	vbk$phy_restore	(
		VBK$OPTS *	a_opts
			)
{
VBK$RCTX	l_rctx = {0};
VBK$ATTR	l_attr;
struct stat	l_st;
const char *	l_spec = a_opts->input [0], *l_out = a_opts->output;
const uint8_t *	l_body, *l_val, *l_data;
uint8_t *	l_zbuf = NULL;
uint64_t	l_devsize = 0, l_outsize = 0, l_pos = 0, l_off, l_nbytes = 0, l_fsize = 0;
uint32_t	l_len, l_pos2 = 0, l_vlen, l_sector, l_fileno = 0, l_dfno, l_n, l_crc = 0, l_fcrc = 0;
uint16_t	l_type, l_tag;
int		l_phys = 0, l_isdev = 0, l_fd = -1, l_damaged = 0, l_status, l_fend = 0, l_hascrc = 0;

	if ( !(1 & (l_status = vbk$rd_open(&l_rctx, l_spec, vbk$rdevent, (void *) l_spec))) )
		return	(l_status == STS$K_WARN) ? $VBKMSG(VBACKUP$_NOTSAVESET, l_spec) : $VBKMSG(VBACKUP$_OPENIN, l_spec, l_rctx.err, strerror(l_rctx.err));

	/* Encrypted: the passphrase first - nothing of it can be read before */
	if ( !(1 & vbk$key_unlock(a_opts, &l_rctx, l_spec)) )
		{
		vbk$rd_close(&l_rctx);

		return	STS$K_ERROR;
		}


	while ( 1 & vbk$tlv_next(l_rctx.summary, l_rctx.sumlen, &l_pos2, &l_tag, &l_vlen, &l_val) )
		if ( l_tag == VBK$K_TAG_PHYSICAL )
			l_phys	= 1;
		else if ( l_tag == VBK$K_TAG_DEVSIZE )
			l_devsize = vbk$tlv_getu(l_vlen, l_val);

	if ( !l_phys || !l_devsize )
		{
		vbk$rd_close(&l_rctx);

		return	$VBKMSG(VBACKUP$_PHYSNOTPHYS, l_spec);
		}

	/* The output: a device - every guard - or an image file */
	if ( !stat(l_out, &l_st) )
		{
		if ( !S_ISBLK(l_st.st_mode) && !S_ISREG(l_st.st_mode) )
			l_status = $VBKMSG(VBACKUP$_PHYSNOTDEV, l_out);
		else if ( !a_opts->replace )
			l_status = S_ISBLK(l_st.st_mode) ? $VBKMSG(VBACKUP$_PHYSREPLACE, l_out) : $VBKMSG(VBACKUP$_OPENOUT, l_out, EEXIST, strerror(EEXIST));
		else if ( !(1 & vbk$phy_check(l_out, &l_st, 1)) )
			l_status = STS$K_ERROR;
		else if ( 0 > (l_fd = open(l_out, O_WRONLY | O_CLOEXEC | (S_ISBLK(l_st.st_mode) ? O_EXCL : O_TRUNC))) )
			l_status = (errno == EBUSY) ? $VBKMSG(VBACKUP$_PHYSHELD, l_out, "the kernel says it is busy")
						    : $VBKMSG(VBACKUP$_OPENOUT, l_out, errno, strerror(errno));
		else	l_status = STS$K_SUCCESS;

		l_isdev	= S_ISBLK(l_st.st_mode);
		}
	else if ( 0 > (l_fd = open(l_out, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644)) )
		l_status = $VBKMSG(VBACKUP$_OPENOUT, l_out, errno, strerror(errno));
	else	{
		fstat(l_fd, &l_st);
		l_status = STS$K_SUCCESS;
		}

	if ( (1 & l_status) && l_isdev )
		{
		if ( !(1 & s_vbk$size(l_fd, &l_st, &l_outsize, &l_sector)) )
			l_status = $VBKMSG(VBACKUP$_OPENOUT, l_out, errno, strerror(errno));
		else if ( l_outsize < l_devsize )
			l_status = $VBKMSG(VBACKUP$_PHYSSMALL, l_out, l_outsize, l_devsize);
		else if ( !vbk$phy_yes(l_out, l_outsize, l_spec) )
			l_status = $VBKMSG(VBACKUP$_PHYSABORT, l_out);
		else if ( l_outsize > l_devsize )
			$VBKMSG(VBACKUP$_PHYSLARGER, l_out, l_outsize, l_devsize);
		}

	/* An image file: as long as the device, every byte a hole until written */
	if ( (1 & l_status) && !l_isdev && ftruncate(l_fd, (off_t) l_devsize) )
		l_status = $VBKMSG(VBACKUP$_WRITERR, l_out, errno, strerror(errno));

	if ( (1 & l_status) && !(l_zbuf = malloc(VBK$K_MAXDATA)) )
		l_status = $VBKMSG(VBACKUP$_NOMEM, ENOMEM, strerror(ENOMEM));

	if ( !(1 & l_status) )
		{
		if ( l_fd >= 0 )
			close(l_fd);

		free(l_zbuf);
		vbk$rd_close(&l_rctx);

		return	STS$K_ERROR;
		}

	/* The one file of the saveset: its data where it was, zeros between */
	while ( 1 & vbk$rd_next(&l_rctx, &l_type, &l_body, &l_len, NULL) )
		{
		if ( l_rctx.resync && l_fileno )
			l_damaged = 1;

		if ( l_type == VBK$K_RT_FILE )
			{
			if ( l_fileno )
				break;

			if ( 1 & vbk$atr_parse(l_body, l_len, &l_attr) )
				l_fileno = l_attr.fileno;
			}
		else if ( ((l_type == VBK$K_RT_DATA) || (l_type == VBK$K_RT_DATAZ)) && l_fileno )
			{
			if ( STS$K_SUCCESS != vbk$data_get(l_type, l_body, l_len, l_zbuf, &l_dfno, &l_off, &l_data, &l_n) )
				{
				l_damaged = 1;
				continue;
				}

			if ( (l_dfno != l_fileno) || (l_off < l_pos) || ((l_off + l_n) > l_devsize) )
				{
				l_damaged = 1;
				continue;
				}

			if ( !(1 & s_vbk$zero(l_fd, l_isdev, l_pos, l_off)) || (pwrite(l_fd, l_data, l_n, (off_t) l_off) != (ssize_t) l_n) )
				{
				l_status = $VBKMSG(VBACKUP$_WRITERR, l_out, errno, strerror(errno));
				break;
				}

			l_crc	 = $VBK_CRC(l_crc, l_data, l_n);
			l_pos	 = l_off + l_n;
			l_nbytes += l_n;
			}
		else if ( (l_type == VBK$K_RT_FEND) && l_fileno )
			{
			uint32_t	l_p = 0;

			while ( 1 & vbk$tlv_next(l_body, l_len, &l_p, &l_tag, &l_vlen, &l_val) )
				if ( l_tag == VBK$K_TAG_CRC )
					l_fcrc = (uint32_t) vbk$tlv_getu(l_vlen, l_val), l_hascrc = 1;
				else if ( l_tag == VBK$K_TAG_SIZE )
					l_fsize = vbk$tlv_getu(l_vlen, l_val);

			l_fend	= 1;
			break;
			}
		else if ( (l_type == VBK$K_RT_CATALOG) || (l_type == VBK$K_RT_END) )
			break;
		}

	/* The zeros after the last data, up to the size of the device saved - not beyond: the rest is not ours */
	if ( (1 & l_status) && !(1 & s_vbk$zero(l_fd, l_isdev, l_pos, l_devsize)) )
		l_status = $VBKMSG(VBACKUP$_WRITERR, l_out, errno, strerror(errno));

	if ( (1 & l_status) && fsync(l_fd) )
		l_status = $VBKMSG(VBACKUP$_WRITERR, l_out, errno, strerror(errno));

	if ( close(l_fd) && (1 & l_status) )
		l_status = $VBKMSG(VBACKUP$_WRITERR, l_out, errno, strerror(errno));

	free(l_zbuf);
	vbk$rd_close(&l_rctx);

	if ( !(1 & l_status) )
		return	STS$K_ERROR;

	if ( !l_fileno || !l_fend || l_damaged || (l_hascrc && (l_crc != l_fcrc)) || (l_fsize != l_devsize) )
		{
		$VBKMSG(VBACKUP$_FILDAMAGED, l_out);
		l_status = STS$K_WARN;
		}

	if ( l_isdev )
		$VBKMSG(VBACKUP$_PHYSUUID, l_out);

	$VBKMSG(VBACKUP$_PHYSSUMM, l_out, l_devsize, l_nbytes);

	return	l_status;
}
