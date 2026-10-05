#define	__MODULE__	"VBKIMG"
#define	__IDENT__	"X01-07"
#define	__REV__		"1.7.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkimg.c
**
**  ABSTRACT:	/IMAGE: a whole file system - every file of the volume, and
**		what makes it that volume: its type, label, UUID, the
**		attributes of its root - as BACKUP/IMAGE saves a disk.
**
**  DESCRIPTION: A save /IMAGE is an ordinary save of the files of one
**		mounted file system, from its mount point, under names of
**		their own, everything taken (no nodump, no other file
**		systems), with the identity of the volume in the SUMMARY
**		(format.md, 6.9).  The input is the mount point, or the
**		device when it is mounted - a subdirectory is not a volume.
**
**		A restore /IMAGE onto a device makes a file system of the
**		same type, label and UUID - the one place VBACKUP runs
**		another program, mkfs.<type> - mounts it on a directory of
**		its own (nosuid, nodev, noexec: nothing of what is restored
**		runs from there), restores the files into it, gives its root
**		the saved attributes, and unmounts it, whatever happened.
**		The guards are those of /PHYSICAL: /REPLACE, not mounted, not
**		in use, YES at a terminal; and the device must hold the files.
**
**		Restored without /IMAGE, such a saveset is a tree of files
**		like any other - for vbkx and the extractors of last resort too.
**
**		What /IMAGE is not: a copy of the boot sectors or of the
**		partition table (that is /PHYSICAL of the whole disk), nor
**		of the inode numbers.  A volume written to while it is saved
**		is consistent file by file, not across files: save a snapshot
**		when that matters.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  4-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-07		 5-OCT-2026	RRL
**		Every text made by FAO; the UUID of an ext superblock by !%U.
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
#include	<ctype.h>
#include	<sys/stat.h>
#include	<sys/wait.h>
#include	<sys/mount.h>
#include	<sys/statvfs.h>
#include	<sys/sysmacros.h>

#include	"vbkdef.h"

#define	VBK$K_IMGMKFSOUT	2048		/* What is kept of the talk of mkfs		*/

/*
**  One line of /proc/self/mountinfo, what /IMAGE wants of it
*/
typedef struct vbk_mnt_t
{
	dev_t		dev;
	char		root [VBACKUP$K_SZ_PATH];
	char		point [VBACKUP$K_SZ_PATH];
	char		opts [1024];
	char		fstype [64];
	char		source [VBACKUP$K_SZ_PATH];
} VBK$MNT;


/*
**  The octal escapes of mountinfo (\040 for a space) and the \xHH ones of
**  /dev/disk/by-label, undone in place
*/
static	void	s_vbk$unescape	(
		char *		a_s
			)
{
char *	l_o = a_s;

	for ( ; *a_s; a_s++ )
		{
		if ( (a_s [0] == '\\') && (a_s [1] == 'x') && isxdigit((unsigned char) a_s [2]) && isxdigit((unsigned char) a_s [3]) )
			{
			char	l_h [3] = { a_s [2], a_s [3], 0 };

			*l_o++	= (char) strtol(l_h, NULL, 16);
			a_s	+= 3;
			}
		else if ( (a_s [0] == '\\') && (a_s [1] >= '0') && (a_s [1] <= '3') && (a_s [2] >= '0') && (a_s [2] <= '7') && (a_s [3] >= '0') && (a_s [3] <= '7') )
			{
			*l_o++	= (char) (((a_s [1] - '0') << 6) | ((a_s [2] - '0') << 3) | (a_s [3] - '0'));
			a_s	+= 3;
			}
		else	*l_o++ = *a_s;
		}

	*l_o	= '\0';
}


/*
**  The mount that answers: by the device (<a_dev>, its root mounted, the
**  first such) or by the mount point (<a_point>, the last such - the one
**  on top)
*/
static	int	s_vbk$findmnt	(
		dev_t		a_dev,
	const	char *		a_point,
		VBK$MNT *	a_mnt
			)
{
FILE *		l_f;
char		l_line [8192];
VBK$MNT		l_m;
int		l_found = 0;

	if ( !(l_f = fopen("/proc/self/mountinfo", "re")) )
		return	0;

	while ( fgets(l_line, sizeof(l_line), l_f) )
		{
		unsigned	l_ma, l_mi;
		char *		l_dash;

		if ( 5 != sscanf(l_line, "%*s %*s %u:%u %4095s %4095s %1023s", &l_ma, &l_mi, l_m.root, l_m.point, l_m.opts) )
			continue;

		/* After the optional fields, " - ": the type and the source */
		if ( !(l_dash = strstr(l_line, " - ")) || (2 != sscanf(l_dash + 3, "%63s %4095s", l_m.fstype, l_m.source)) )
			continue;

		l_m.dev	= makedev(l_ma, l_mi);
		s_vbk$unescape(l_m.root);
		s_vbk$unescape(l_m.point);
		s_vbk$unescape(l_m.source);

		if ( a_point ? !strcmp(l_m.point, a_point) : ((l_m.dev == a_dev) && !strcmp(l_m.root, "/")) )
			{
			*a_mnt	= l_m;
			l_found	= 1;

			if ( !a_point )
				break;
			}
		}

	fclose(l_f);

	return	l_found;
}


/*
**  The name a device has in /dev/disk/<a_kind> - by-uuid, by-label
*/
static	int	s_vbk$byname	(
	const	char *		a_kind,
		dev_t		a_dev,
		char *		a_out,
		size_t		a_outsz
			)
{
char		l_dir [64], l_path [VBACKUP$K_SZ_PATH];
DIR *		l_d;
struct dirent *	l_de;
struct stat	l_st;
int		l_found = 0;

	$VBKFAOB(l_dir, sizeof(l_dir), "/dev/disk/!AZ", a_kind);

	if ( !(l_d = opendir(l_dir)) )
		return	0;

	while ( !l_found && (l_de = readdir(l_d)) )
		{
		if ( l_de->d_name [0] == '.' )
			continue;

		if ( ($VBKFAOB(l_path, sizeof(l_path), "!AZ/!AZ", l_dir, l_de->d_name) < (int) sizeof(l_path))
			&& !stat(l_path, &l_st) && S_ISBLK(l_st.st_mode) && (l_st.st_rdev == a_dev) )
			{
			vbk$strcpy(a_outsz, a_out, l_de->d_name);
			s_vbk$unescape(a_out);
			l_found	= 1;
			}
		}

	closedir(l_d);

	return	l_found;
}


/*
**  ext2/3/4 without udev: the label and the UUID from the super block
*/
static	void	s_vbk$extid	(
	const	char *		a_dev,
		char *		a_label,
		size_t		a_lsz,
		char *		a_uuid,
		size_t		a_usz
			)
{
uint8_t	l_sb [1024];
int	l_fd;

	if ( 0 > (l_fd = open(a_dev, O_RDONLY | O_CLOEXEC)) )
		return;

	if ( (pread(l_fd, l_sb, sizeof(l_sb), 1024) == (ssize_t) sizeof(l_sb)) && (vbk$get16(l_sb + 0x38) == 0xEF53) )
		{
		const uint8_t *	u = l_sb + 0x68;

		if ( !a_uuid [0] )
			$VBKFAOB(a_uuid, a_usz, "!%U", u);

		if ( !a_label [0] )
			$VBKFAOB(a_label, a_lsz, "!AD", strnlen((const char *) l_sb + 0x78, 16), l_sb + 0x78);
		}

	close(l_fd);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Before a save /IMAGE: the volume behind the input - a mount point,
**	or a mounted device - its identity into OPTS, and the input turned
**	into "<mount point>/." so that the files are stored under names of
**	their own.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or STS$K_ERROR - said why.
**--
*/
int	vbk$img_prepare	(
		VBK$OPTS *	a_opts
			)
{
static	char	s_input [VBACKUP$K_SZ_PATH + 4];
VBK$MNT		l_mnt;
VBK$ATTR	l_attr;
VBK$TLVB	l_xbuf = {0};
struct stat	l_st;
struct statvfs	l_vfs;
char		l_real [VBACKUP$K_SZ_PATH];
const char *	l_spec = a_opts->input [0];
int		l_status;

	if ( stat(l_spec, &l_st) )
		return	$VBKMSG(VBACKUP$_OPENIN, l_spec, errno, strerror(errno));

	if ( S_ISBLK(l_st.st_mode) )
		{
		if ( !s_vbk$findmnt(l_st.st_rdev, NULL, &l_mnt) )
			return	$VBKMSG(VBACKUP$_IMGNOTMNT, l_spec);
		}
	else if ( !S_ISDIR(l_st.st_mode) || !realpath(l_spec, l_real) || !s_vbk$findmnt(0, l_real, &l_mnt) )
		return	$VBKMSG(VBACKUP$_IMGNOTVOL, l_spec);

	vbk$strcpy(sizeof(a_opts->imgmnt), a_opts->imgmnt, l_mnt.point);
	vbk$strcpy(sizeof(a_opts->imgfstype), a_opts->imgfstype, l_mnt.fstype);
	$VBKFAOB(a_opts->imgopts, sizeof(a_opts->imgopts), "!AZ", l_mnt.opts);

	/* The device node: the source when it is one, else the kernel's own name for the numbers */
	if ( !stat(l_mnt.source, &l_st) && S_ISBLK(l_st.st_mode) && (l_st.st_rdev == l_mnt.dev) )
		vbk$strcpy(sizeof(a_opts->imgdev), a_opts->imgdev, l_mnt.source);
	else	$VBKFAOB(a_opts->imgdev, sizeof(a_opts->imgdev), "/dev/block/!UL:!UL", major(l_mnt.dev), minor(l_mnt.dev));

	s_vbk$byname("by-uuid", l_mnt.dev, a_opts->imguuid, sizeof(a_opts->imguuid));
	s_vbk$byname("by-label", l_mnt.dev, a_opts->imglabel, sizeof(a_opts->imglabel));

	if ( !strncmp(a_opts->imgfstype, "ext", 3) && (!a_opts->imguuid [0] || !a_opts->imglabel [0]) )
		s_vbk$extid(a_opts->imgdev, a_opts->imglabel, sizeof(a_opts->imglabel), a_opts->imguuid, sizeof(a_opts->imguuid));

	if ( !a_opts->imguuid [0] )
		$VBKMSG(VBACKUP$_IMGNOID, a_opts->imgmnt, "UUID");

	if ( !statvfs(a_opts->imgmnt, &l_vfs) )
		a_opts->imgused = (uint64_t) (l_vfs.f_blocks - l_vfs.f_bfree) * l_vfs.f_frsize;

	/* The root of the volume: its owner, mode, times, ACLs - it has no FILE record of its own */
	/* WARN: an extended attribute could not be read - the rest is there all the same */
	l_status = vbk$atr_get(a_opts->imgmnt, -1, a_opts->xattrs, &l_attr, &l_xbuf);

	if ( (l_status == STS$K_SUCCESS) || (l_status == STS$K_WARN) )
		{
		l_attr.path	= ".";
		l_attr.pathlen	= 1;
		l_attr.fileno	= 1;			/* Refers to nothing, but a reader wants one */
		vbk$atr_tlv(&l_attr, &a_opts->imgroot);
		}

	vbk$tlv_free(&l_xbuf);

	/* Everything of this file system and of nothing else, under names of their own */
	$VBKFAOB(s_input, sizeof(s_input), "!AZ!AZ.", a_opts->imgmnt, strcmp(a_opts->imgmnt, "/") ? "/" : "");
	a_opts->input [0] = s_input;
	a_opts->crossdev  = 0;
	a_opts->nobackup  = 1;

	return	STS$K_SUCCESS;
}


/*
**  The command line that makes the file system: per type, nothing else;
**  an unknown type is refused (/PHYSICAL takes any)
*/
static	int	s_vbk$mkfsargs	(
	const	char *		a_fstype,
	const	char *		a_label,
	const	char *		a_uuid,
	const	char *		a_dev,
		char *		a_argv [],
		char		a_buf [][VBACKUP$K_SZ_PATH]
			)
{
int	n = 0, b = 0;

	$VBKFAOB(a_buf [b], VBACKUP$K_SZ_PATH, "mkfs.!AZ", a_fstype);
	a_argv [n++] = a_buf [b++];

	if ( !strcmp(a_fstype, "ext2") || !strcmp(a_fstype, "ext3") || !strcmp(a_fstype, "ext4") )
		{
		a_argv [n++] = (char *) "-F";
		a_argv [n++] = (char *) "-q";

		if ( a_label [0] )
			a_argv [n++] = (char *) "-L", a_argv [n++] = (char *) a_label;

		if ( a_uuid [0] )
			a_argv [n++] = (char *) "-U", a_argv [n++] = (char *) a_uuid;
		}
	else if ( !strcmp(a_fstype, "xfs") )
		{
		a_argv [n++] = (char *) "-f";
		a_argv [n++] = (char *) "-q";

		if ( a_label [0] )
			a_argv [n++] = (char *) "-L", a_argv [n++] = (char *) a_label;

		if ( a_uuid [0] )
			{
			$VBKFAOB(a_buf [b], VBACKUP$K_SZ_PATH, "uuid=!AZ", a_uuid);
			a_argv [n++] = (char *) "-m", a_argv [n++] = a_buf [b++];
			}
		}
	else if ( !strcmp(a_fstype, "btrfs") )
		{
		a_argv [n++] = (char *) "-f";
		a_argv [n++] = (char *) "-q";

		if ( a_label [0] )
			a_argv [n++] = (char *) "-L", a_argv [n++] = (char *) a_label;

		if ( a_uuid [0] )
			a_argv [n++] = (char *) "-U", a_argv [n++] = (char *) a_uuid;
		}
	else if ( !strcmp(a_fstype, "vfat") || !strcmp(a_fstype, "msdos") )
		{
		/* A FAT label is 11 characters, upper case; its "UUID" is the volume serial XXXX-XXXX */
		$VBKFAOB(a_buf [0], VBACKUP$K_SZ_PATH, "mkfs.vfat");

		if ( a_label [0] )
			{
			size_t	i;

			for ( i = 0; a_label [i] && (i < 11); i++ )
				a_buf [b] [i] = (char) toupper((unsigned char) a_label [i]);

			a_buf [b] [i] = '\0';
			a_argv [n++] = (char *) "-n", a_argv [n++] = a_buf [b++];
			}

		if ( (strlen(a_uuid) == 9) && (a_uuid [4] == '-') )
			{
			$VBKFAOB(a_buf [b], VBACKUP$K_SZ_PATH, "!AD!AD", strnlen(a_uuid, 4), a_uuid, strnlen(a_uuid + 5, 4), a_uuid + 5);
			a_argv [n++] = (char *) "-i", a_argv [n++] = a_buf [b++];
			}
		}
	else	return	0;

	a_argv [n++] = (char *) a_dev;
	a_argv [n]   = NULL;

	return	n;
}


/*
**  Run mkfs: its talk is kept for the message; not installed is said so
*/
static	int	s_vbk$mkfs	(
		char *		a_argv [],
	const	VBK$OPTS *	a_opts
			)
{
char	l_cmd [VBACKUP$K_SZ_PATH * 2] = "", l_out [VBK$K_IMGMKFSOUT] = "";
size_t	l_len = 0;
int	l_pipe [2], l_wst = 0;
pid_t	l_pid;
ssize_t	l_n;

	for ( int i = 0; a_argv [i]; i++ )
		{
		size_t	l_c = strlen(l_cmd);

		$VBKFAOB(l_cmd + l_c, sizeof(l_cmd) - l_c, "!AZ!AZ", i ? " " : "", a_argv [i]);
		}

	if ( a_opts->log )
		$VBKMSG(VBACKUP$_IMGCMD, l_cmd);

	if ( pipe(l_pipe) )
		return	$VBKMSG(VBACKUP$_IMGMKFS, l_cmd, strerror(errno));

	if ( 0 > (l_pid = fork()) )
		return	$VBKMSG(VBACKUP$_IMGMKFS, l_cmd, strerror(errno));

	if ( !l_pid )
		{
		const char *	l_path = getenv("PATH");
		char		l_np [8192];

		$VBKFAOB(l_np, sizeof(l_np), "!AZ:/sbin:/usr/sbin", l_path ? l_path : "/bin:/usr/bin");
		setenv("PATH", l_np, 1);

		dup2(l_pipe [1], STDOUT_FILENO);
		dup2(l_pipe [1], STDERR_FILENO);
		close(l_pipe [0]);
		close(l_pipe [1]);

		execvp(a_argv [0], a_argv);
		_exit((errno == ENOENT) ? 127 : 126);
		}

	close(l_pipe [1]);

	while ( 0 < (l_n = read(l_pipe [0], l_out + l_len, sizeof(l_out) - 1 - l_len)) )
		if ( (l_len += (size_t) l_n) >= (sizeof(l_out) - 1) )
			break;

	l_out [l_len] = '\0';
	close(l_pipe [0]);

	while ( (0 > waitpid(l_pid, &l_wst, 0)) && (errno == EINTR) )
		;

	if ( WIFEXITED(l_wst) && (WEXITSTATUS(l_wst) == 127) )
		return	$VBKMSG(VBACKUP$_IMGMKFS, l_cmd, "the program is not installed");

	if ( !WIFEXITED(l_wst) || WEXITSTATUS(l_wst) )
		{
		/* The last line it said is the reason, mostly */
		while ( l_len && ((l_out [l_len - 1] == '\n') || (l_out [l_len - 1] == ' ')) )
			l_out [--l_len] = '\0';

		return	$VBKMSG(VBACKUP$_IMGMKFS, l_cmd, l_len ? (strrchr(l_out, '\n') ? strrchr(l_out, '\n') + 1 : l_out) : "it failed");
		}

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Restore an /IMAGE saveset onto a device: a new file system of the
**	saved type, label and UUID, the files in it, its root as saved.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: INPUT [0] the saveset, OUTPUT the device
**
**  RETURN VALUE:
**	The status of the restore, or STS$K_ERROR - refused or failed.
**--
*/
int	vbk$img_restore	(
		VBK$OPTS *	a_opts
			)
{
VBK$RCTX	l_rctx = {0};
VBK$ATTR	l_root;
struct stat	l_st;
char		l_fstype [64] = "", l_label [256] = "", l_uuid [64] = "", l_dir [] = "/tmp/vbackup-image.XXXXXX";
char		l_out [VBACKUP$K_SZ_PATH], l_buf [4][VBACKUP$K_SZ_PATH];
char *		l_argv [16];
uint8_t *	l_rootb = NULL;
uint32_t	l_pos = 0, l_vlen, l_rootlen = 0;
uint64_t	l_used = 0, l_size = 0, l_need;
uint16_t	l_tag;
const uint8_t *	l_val;
const char *	l_spec = a_opts->input [0];
int		l_image = 0, l_status, l_fd;

	if ( !(1 & (l_status = vbk$rd_open(&l_rctx, l_spec, vbk$rdevent, (void *) l_spec))) )
		return	(l_status == STS$K_WARN) ? $VBKMSG(VBACKUP$_NOTSAVESET, l_spec) : $VBKMSG(VBACKUP$_OPENIN, l_spec, l_rctx.err, strerror(l_rctx.err));

	/* Encrypted: the passphrase first - nothing of it can be read before */
	if ( !(1 & vbk$key_unlock(a_opts, &l_rctx, l_spec)) )
		{
		vbk$rd_close(&l_rctx);

		return	STS$K_ERROR;
		}


	while ( 1 & vbk$tlv_next(l_rctx.summary, l_rctx.sumlen, &l_pos, &l_tag, &l_vlen, &l_val) )
		switch ( l_tag )
			{
			case	VBK$K_TAG_IMAGE:	l_image = 1;								break;
			case	VBK$K_TAG_FSTYPE:	$VBKFAOB(l_fstype, sizeof(l_fstype), "!AD", l_vlen, l_val);		break;
			case	VBK$K_TAG_FSLABEL:	$VBKFAOB(l_label, sizeof(l_label), "!AD", l_vlen, l_val);		break;
			case	VBK$K_TAG_FSUUID:	$VBKFAOB(l_uuid, sizeof(l_uuid), "!AD", l_vlen, l_val);		break;
			case	VBK$K_TAG_FSUSED:	l_used = vbk$tlv_getu(l_vlen, l_val);					break;
			case	VBK$K_TAG_ROOTATTR:
				if ( !l_rootb && (l_rootb = malloc(l_vlen ? l_vlen : 1)) )
					{
					memcpy(l_rootb, l_val, l_vlen);
					l_rootlen = l_vlen;
					}
				break;
			}

	vbk$rd_close(&l_rctx);

	if ( !l_image || !l_fstype [0] )
		{
		free(l_rootb);

		return	$VBKMSG(VBACKUP$_IMGNOTIMG, l_spec);
		}

	vbk$strcpy(sizeof(l_out), l_out, a_opts->output);

	/* The device: as for /PHYSICAL - /REPLACE, not mounted, not busy, YES; and it must hold the files */
	if ( !s_vbk$mkfsargs(l_fstype, l_label, l_uuid, l_out, l_argv, l_buf) )
		l_status = $VBKMSG(VBACKUP$_IMGUNSUPP, l_spec, l_fstype);
	else if ( a_opts->confirm )
		l_status = $VBKMSG(VBACKUP$_CONFQUAL, "IMAGE", "CONFIRM");
	else if ( stat(l_out, &l_st) )
		l_status = $VBKMSG(VBACKUP$_OPENOUT, l_out, errno, strerror(errno));
	else if ( !S_ISBLK(l_st.st_mode) )
		l_status = $VBKMSG(VBACKUP$_PHYSNOTDEV, l_out);
	else if ( !a_opts->replace )
		l_status = $VBKMSG(VBACKUP$_PHYSREPLACE, l_out);
	else if ( !(1 & vbk$phy_check(l_out, &l_st, 1)) )
		l_status = STS$K_ERROR;
	else if ( 0 > (l_fd = open(l_out, O_RDONLY | O_EXCL | O_CLOEXEC)) )
		l_status = (errno == EBUSY) ? $VBKMSG(VBACKUP$_PHYSHELD, l_out, "the kernel says it is busy") : $VBKMSG(VBACKUP$_OPENOUT, l_out, errno, strerror(errno));
	else	{
		l_status = (1 & vbk$phy_devsize(l_fd, &l_st, &l_size)) ? STS$K_SUCCESS : $VBKMSG(VBACKUP$_OPENOUT, l_out, errno, strerror(errno));
		close(l_fd);

		/* The files, a twentieth more, and room for the metadata of the new file system */
		l_need	= l_used + (l_used / 20) + (16 * 1048576);

		if ( (1 & l_status) && (l_size < l_need) )
			l_status = $VBKMSG(VBACKUP$_IMGSMALL, l_out, l_size, l_need);
		else if ( (1 & l_status) && !vbk$phy_yes(l_out, l_size, l_spec) )
			l_status = $VBKMSG(VBACKUP$_PHYSABORT, l_out);
		}

	if ( (1 & l_status) && !(1 & s_vbk$mkfs(l_argv, a_opts)) )
		l_status = STS$K_ERROR;

	if ( (1 & l_status) && !mkdtemp(l_dir) )
		l_status = $VBKMSG(VBACKUP$_OPENOUT, l_dir, errno, strerror(errno));

	if ( (1 & l_status) && mount(l_out, l_dir, l_fstype, MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) )
		{
		l_status = $VBKMSG(VBACKUP$_IMGMOUNT, l_fstype, l_out, errno, strerror(errno));
		rmdir(l_dir);
		}

	if ( !(1 & l_status) )
		{
		free(l_rootb);

		return	STS$K_ERROR;
		}

	/* The files, as any restore puts them - into the new file system; lost+found is there already */
	vbk$strcpy(sizeof(a_opts->output), a_opts->output, l_dir);
	a_opts->replace	= 1;

	l_status = vbk$restore(a_opts);

	/* Its root last: a read-only mode would have kept the files out */
	if ( l_rootb && (1 & vbk$atr_parse(l_rootb, l_rootlen, &l_root)) )
		{
		vbk$atr_apply(a_opts, l_dir, -1, &l_root);
		vbk$atr_settimes(l_dir, &l_root);
		}

	free(l_rootb);

	/* Unmounted whatever happened: nothing is left mounted behind */
	sync();

	for ( int i = 0; umount2(l_dir, 0) && (errno == EBUSY) && (i < 10); i++ )
		sleep(1);

	rmdir(l_dir);
	vbk$strcpy(sizeof(a_opts->output), a_opts->output, l_out);

	if ( l_uuid [0] )
		$VBKMSG(VBACKUP$_PHYSUUID, l_out);

	$VBKMSG(VBACKUP$_IMGSUMM, l_out, l_fstype, a_opts->rstfiles, a_opts->rstbytes);

	return	l_status;
}
