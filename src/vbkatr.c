#define	__MODULE__	"VBKATR"
#define	__IDENT__	"X01-03"
#define	__REV__		"1.3.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkatr.c
**
**  ABSTRACT:	The attributes of a file: collected from the disk for a
**		save, encoded into and decoded from a FILE record, put back
**		on a restore.
**
**  DESCRIPTION: Everything is asked of the kernel directly - statx for
**		the inode, the *xattr calls for extended attributes and the
**		POSIX ACLs (which are the system.posix_acl_* attributes),
**		FS_IOC_GETFLAGS for the chattr flags - so no library beyond
**		libc is needed.
**
**		Order matters on a restore (DESIGN.md, 4.3): owner before
**		mode, since chown drops the set-id bits; ACLs after the mode,
**		since chmod rewrites the ACL mask; times and chattr flags
**		last, and those two are the business of the caller, which
**		applies them once nothing more is to be written.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  3-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-03		 3-OCT-2026	RRL
**		XATTR: a counted name (ASCIC) instead of one ended by a NUL;
**		the attributes in the order of their names.
**
**	X01-02		 3-OCT-2026	RRL
**		VBK$ATR_PARSE tells whether CTIME and DEVINO were there: the
**		journal is rebuilt from catalog entries that carry them.
**
**	X01-01		 3-OCT-2026	RRL
**		Initial version.
**
**--
*/

#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<pwd.h>
#include	<grp.h>
#include	<sys/stat.h>
#include	<sys/sysmacros.h>
#include	<sys/xattr.h>
#include	<sys/ioctl.h>
#include	<linux/fs.h>

#include	"vbkdef.h"

#define	VBK$K_NAMCACHE	64			/* Owner and group names remembered		*/
#define	VBK$K_XLIST	65536			/* Room for the list of xattr names		*/
#define	VBK$K_XVALUE	65536			/* Largest xattr value (the VFS limit)		*/

/*
**  The chattr flags that are put back: those a user sets with chattr and
**  that mean the same on every file system that has them.  The others -
**  EXTENTS, INLINE_DATA and the like - describe how a file is stored, not
**  what it is, and a file system refuses to have them set.
*/
#define	VBK$M_FSFLAGS	(FS_APPEND_FL | FS_IMMUTABLE_FL | FS_NODUMP_FL | FS_NOATIME_FL | FS_SYNC_FL | FS_DIRSYNC_FL | FS_NOCOW_FL)

typedef struct vbk_namc_t
{
	uint32_t	id;
	int		valid;
	char		name [64];
} VBK$NAMC;

static	VBK$NAMC	s_ucache [VBK$K_NAMCACHE], s_gcache [VBK$K_NAMCACHE];
static	char		s_link [VBACKUP$K_SZ_PATH];


/*
**  The name of a user id, remembered; NULL - the id has no name
*/
const char *	vbk$atr_uname	(
		uint32_t	a_uid
			)
{
VBK$NAMC *	l_c = &s_ucache [a_uid % VBK$K_NAMCACHE];
struct passwd	l_pw, *l_res = NULL;
char		l_buf [4096];

	if ( l_c->valid && (l_c->id == a_uid) )
		return	l_c->name [0] ? l_c->name : NULL;

	l_c->id		= a_uid;
	l_c->valid	= 1;
	l_c->name [0]	= '\0';

	if ( !getpwuid_r(a_uid, &l_pw, l_buf, sizeof(l_buf), &l_res) && l_res )
		vbk$strcpy(sizeof(l_c->name), l_c->name, l_pw.pw_name);

	return	l_c->name [0] ? l_c->name : NULL;
}

const char *	vbk$atr_gname	(
		uint32_t	a_gid
			)
{
VBK$NAMC *	l_c = &s_gcache [a_gid % VBK$K_NAMCACHE];
struct group	l_gr, *l_res = NULL;
char		l_buf [4096];

	if ( l_c->valid && (l_c->id == a_gid) )
		return	l_c->name [0] ? l_c->name : NULL;

	l_c->id		= a_gid;
	l_c->valid	= 1;
	l_c->name [0]	= '\0';

	if ( !getgrgid_r(a_gid, &l_gr, l_buf, sizeof(l_buf), &l_res) && l_res )
		vbk$strcpy(sizeof(l_c->name), l_c->name, l_gr.gr_name);

	return	l_c->name [0] ? l_c->name : NULL;
}


static	uint8_t	s_vbk$ftype	(
		uint32_t	a_mode
			)
{
	switch ( a_mode & S_IFMT )
		{
		case	S_IFREG:	return	VBK$K_FT_REG;
		case	S_IFDIR:	return	VBK$K_FT_DIR;
		case	S_IFLNK:	return	VBK$K_FT_SYMLINK;
		case	S_IFCHR:	return	VBK$K_FT_CHR;
		case	S_IFBLK:	return	VBK$K_FT_BLK;
		case	S_IFIFO:	return	VBK$K_FT_FIFO;
		case	S_IFSOCK:	return	VBK$K_FT_SOCK;
		}

	return	0;
}


/*
**  The order of the names of the extended attributes: their bytes
*/
static	int	s_vbk$cmpname	(
	const	void *		a_a,
	const	void *		a_b
			)
{
	return	strcmp(*(const char * const *) a_a, *(const char * const *) a_b);
}


/*
**  The extended attributes of a file, as XATTR items appended to <a_xbuf>:
**  each a counted name - one octet of length, the name - and the value
**  (format.md, 6.1).  Taken in the order of their names, not in the one
**  the file system lists them in: the same file makes the same saveset.
*/
static	int	s_vbk$getxattrs	(
	const	char *		a_path,
		int		a_fd,
		VBK$TLVB *	a_xbuf
			)
{
char *		l_list, *l_val, **l_names = NULL;
ssize_t		l_llen, l_vlen;
size_t		l_nlen, l_n = 0;
int		l_status = STS$K_SUCCESS;

	if ( !(l_list = malloc(VBK$K_XLIST + VBK$K_XVALUE + 256)) )
		return	STS$K_FATAL;

	l_llen	= (a_fd >= 0) ? flistxattr(a_fd, l_list, VBK$K_XLIST) : llistxattr(a_path, l_list, VBK$K_XLIST);

	/* No support for them here, or none at all: not an error */
	if ( l_llen <= 0 )
		{
		free(l_list);

		return	((l_llen < 0) && (errno != ENOTSUP) && (errno != ENODATA)) ? STS$K_WARN : STS$K_SUCCESS;
		}

	/* The list the kernel gives is of NUL-ended names: made a sorted table first */
	if ( !(l_names = malloc((size_t) l_llen * sizeof(char *))) )
		{
		free(l_list);

		return	STS$K_FATAL;
		}

	for ( char *l_name = l_list; l_name < (l_list + l_llen); l_name += strlen(l_name) + 1 )
		if ( *l_name )
			l_names [l_n++] = l_name;

	qsort(l_names, l_n, sizeof(char *), s_vbk$cmpname);

	for ( size_t i = 0; i < l_n; i++ )
		{
		l_nlen	= strlen(l_names [i]);
		l_val	= l_list + VBK$K_XLIST + 256;

		/* A name longer than one octet can count is none Linux makes (XATTR_NAME_MAX is 255) */
		if ( l_nlen > 255 )
			{
			l_status = STS$K_WARN;
			continue;
			}

		/* The item: the length of the name, the name, the value - the first two put in front of the value */
		l_val [-(ssize_t) l_nlen - 1] = (char) l_nlen;
		memcpy(l_val - l_nlen, l_names [i], l_nlen);

		l_vlen	= (a_fd >= 0) ? fgetxattr(a_fd, l_names [i], l_val, VBK$K_XVALUE) : lgetxattr(a_path, l_names [i], l_val, VBK$K_XVALUE);

		if ( l_vlen < 0 )
			{
			l_status = STS$K_WARN;
			continue;
			}

		if ( !(1 & vbk$tlv_put(a_xbuf, VBK$K_TAG_XATTR, (uint32_t) (1 + l_nlen + (size_t) l_vlen), l_val - l_nlen - 1)) )
			{
			free(l_names);
			free(l_list);

			return	STS$K_FATAL;
			}
		}

	free(l_names);
	free(l_list);

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Collect the attributes of a file from the disk.
**
**  FORMAL PARAMETERS:
**
**	a_path		Specification of the file
**	a_fd		An open descriptor of it, -1 - none (symlinks, devices)
**	a_xattrs	Collect the extended attributes too
**	a_attr		Receives the attributes; LINK, UNAME, GNAME point into
**			module buffers valid up to the next call
**	a_xbuf		Receives the XATTR items, reset first
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- collected;
**	STS$K_WARN	- collected, but some extended attribute could not be read;
**	STS$K_ERROR	- the file cannot be looked at, errno says why;
**	STS$K_FATAL	- no memory.
**--
*/
int	vbk$atr_get	(
	const	char *		a_path,
		int		a_fd,
		int		a_xattrs,
		VBK$ATTR *	a_attr,
		VBK$TLVB *	a_xbuf
			)
{
struct statx	l_stx;
ssize_t		l_n;
int		l_flags, l_status = STS$K_SUCCESS;

	memset(a_attr, 0, sizeof(*a_attr));
	vbk$tlv_reset(a_xbuf);

	if ( a_fd >= 0 )
		l_n	= statx(a_fd, "", AT_EMPTY_PATH, STATX_BASIC_STATS | STATX_BTIME, &l_stx);
	else	l_n	= statx(AT_FDCWD, a_path, AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_BTIME, &l_stx);

	if ( l_n )
		return	STS$K_ERROR;

	a_attr->ftype		= s_vbk$ftype(l_stx.stx_mode);
	a_attr->mode		= l_stx.stx_mode & 07777;
	a_attr->uid		= l_stx.stx_uid;
	a_attr->gid		= l_stx.stx_gid;
	a_attr->nlink		= l_stx.stx_nlink;
	a_attr->size		= l_stx.stx_size;
	a_attr->dev		= makedev(l_stx.stx_dev_major, l_stx.stx_dev_minor);
	a_attr->ino		= l_stx.stx_ino;
	a_attr->mtime.sec	= l_stx.stx_mtime.tv_sec;
	a_attr->mtime.nsec	= l_stx.stx_mtime.tv_nsec;
	a_attr->atime.sec	= l_stx.stx_atime.tv_sec;
	a_attr->atime.nsec	= l_stx.stx_atime.tv_nsec;
	a_attr->ctime.sec	= l_stx.stx_ctime.tv_sec;
	a_attr->ctime.nsec	= l_stx.stx_ctime.tv_nsec;

	if ( l_stx.stx_mask & STATX_BTIME )
		{
		a_attr->hasbtime	= 1;
		a_attr->btime.sec	= l_stx.stx_btime.tv_sec;
		a_attr->btime.nsec	= l_stx.stx_btime.tv_nsec;
		}

	if ( (a_attr->ftype == VBK$K_FT_CHR) || (a_attr->ftype == VBK$K_FT_BLK) )
		a_attr->rdev	= ((uint64_t) l_stx.stx_rdev_major << 32) | l_stx.stx_rdev_minor;

	a_attr->uname	= vbk$atr_uname(a_attr->uid);
	a_attr->gname	= vbk$atr_gname(a_attr->gid);

	if ( a_attr->ftype == VBK$K_FT_SYMLINK )
		{
		if ( 0 > (l_n = readlink(a_path, s_link, sizeof(s_link))) )
			return	STS$K_ERROR;

		a_attr->link	= s_link;
		a_attr->linklen	= (uint32_t) l_n;
		}

	if ( (a_fd >= 0) && ((a_attr->ftype == VBK$K_FT_REG) || (a_attr->ftype == VBK$K_FT_DIR)) && !ioctl(a_fd, FS_IOC_GETFLAGS, &l_flags) )
		{
		a_attr->hasflags	= 1;
		a_attr->fsflags		= (uint32_t) l_flags;
		}

	if ( a_xattrs )
		{
		if ( STS$K_FATAL == (l_status = s_vbk$getxattrs(a_path, a_fd, a_xbuf)) )
			return	l_status;

		a_attr->xattr		= a_xbuf->buf;
		a_attr->xattrlen	= a_xbuf->len;
		}

	return	l_status;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Encode the attributes as the TLV items of a FILE record body.
**
**  FORMAL PARAMETERS:
**
**	a_attr		The attributes, PATH, FILENO and BASEIDX filled in
**	a_tlvb		The body, appended to
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- encoded;
**	STS$K_FATAL	- no memory.
**--
*/
int	vbk$atr_tlv	(
	const	VBK$ATTR *	a_attr,
		VBK$TLVB *	a_tlvb
			)
{
uint32_t	l_pos = 0, l_vlen;
uint16_t	l_tag;
const uint8_t *	l_val;
int		l_ok = 1;

	l_ok &= vbk$tlv_u32(a_tlvb, VBK$K_TAG_FILENO, a_attr->fileno);
	l_ok &= vbk$tlv_put(a_tlvb, VBK$K_TAG_PATH, a_attr->pathlen, a_attr->path);
	l_ok &= vbk$tlv_u8(a_tlvb, VBK$K_TAG_FTYPE, a_attr->ftype);
	l_ok &= vbk$tlv_u32(a_tlvb, VBK$K_TAG_MODE, a_attr->mode);
	l_ok &= vbk$tlv_u32(a_tlvb, VBK$K_TAG_UID, a_attr->uid);
	l_ok &= vbk$tlv_u32(a_tlvb, VBK$K_TAG_GID, a_attr->gid);

	if ( a_attr->uname )
		l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_UNAME, a_attr->uname);

	if ( a_attr->gname )
		l_ok &= vbk$tlv_str(a_tlvb, VBK$K_TAG_GNAME, a_attr->gname);

	l_ok &= vbk$tlv_u64(a_tlvb, VBK$K_TAG_SIZE, a_attr->size);
	l_ok &= vbk$tlv_time(a_tlvb, VBK$K_TAG_MTIME, &a_attr->mtime);
	l_ok &= vbk$tlv_time(a_tlvb, VBK$K_TAG_ATIME, &a_attr->atime);
	l_ok &= vbk$tlv_time(a_tlvb, VBK$K_TAG_CTIME, &a_attr->ctime);

	if ( a_attr->hasbtime )
		l_ok &= vbk$tlv_time(a_tlvb, VBK$K_TAG_BTIME, &a_attr->btime);

	if ( (a_attr->ftype == VBK$K_FT_CHR) || (a_attr->ftype == VBK$K_FT_BLK) )
		l_ok &= vbk$tlv_u64(a_tlvb, VBK$K_TAG_RDEV, a_attr->rdev);

	if ( a_attr->link )
		l_ok &= vbk$tlv_put(a_tlvb, VBK$K_TAG_LINK, a_attr->linklen, a_attr->link);

	if ( a_attr->hasflags )
		l_ok &= vbk$tlv_u32(a_tlvb, VBK$K_TAG_FSFLAGS, a_attr->fsflags);

	l_ok &= vbk$tlv_u64x2(a_tlvb, VBK$K_TAG_DEVINO, a_attr->dev, a_attr->ino);
	l_ok &= vbk$tlv_u16(a_tlvb, VBK$K_TAG_BASEIDX, a_attr->baseidx);
	l_ok &= vbk$tlv_u32(a_tlvb, VBK$K_TAG_NLINK, a_attr->nlink);

	while ( 1 & vbk$tlv_next(a_attr->xattr, a_attr->xattrlen, &l_pos, &l_tag, &l_vlen, &l_val) )
		if ( l_tag == VBK$K_TAG_XATTR )
			l_ok &= vbk$tlv_put(a_tlvb, VBK$K_TAG_XATTR, l_vlen, l_val);

	return	l_ok ? STS$K_SUCCESS : STS$K_FATAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Decode the TLV items of a FILE record or of a catalog entry.  The
**	strings are not terminated: their lengths are kept beside them, or
**	- UNAME, GNAME - they are copied into module buffers.
**
**  FORMAL PARAMETERS:
**
**	a_body		The body
**	a_len		Its length
**	a_attr		Receives the attributes, pointing into <a_body>
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- decoded;
**	STS$K_ERROR	- malformed, or FILENO, PATH or FTYPE is missing.
**--
*/
int	vbk$atr_parse	(
	const	uint8_t *	a_body,
		uint32_t	a_len,
		VBK$ATTR *	a_attr
			)
{
static	char	l_uname [64], l_gname [64];
uint32_t	l_pos = 0, l_vlen;
uint16_t	l_tag;
const uint8_t *	l_val;
int		l_status;

	memset(a_attr, 0, sizeof(*a_attr));

	a_attr->xattr		= a_body;
	a_attr->xattrlen	= a_len;

	while ( 1 & (l_status = vbk$tlv_next(a_body, a_len, &l_pos, &l_tag, &l_vlen, &l_val)) )
		{
		switch ( l_tag )
			{
			case	VBK$K_TAG_FILENO:	a_attr->fileno	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_FTYPE:	a_attr->ftype	= (uint8_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_MODE:		a_attr->mode	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_UID:		a_attr->uid	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_GID:		a_attr->gid	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_SIZE:		a_attr->size	= vbk$tlv_getu(l_vlen, l_val);			break;
			case	VBK$K_TAG_RDEV:		a_attr->rdev	= vbk$tlv_getu(l_vlen, l_val);			break;
			case	VBK$K_TAG_NLINK:	a_attr->nlink	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_BASEIDX:	a_attr->baseidx	= (uint16_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_CRC:		a_attr->crc	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_STATUS:	a_attr->status	= (uint8_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_LOCVOL:	a_attr->loc.vol	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_LOCBLK:	a_attr->loc.blk	= vbk$tlv_getu(l_vlen, l_val);			break;
			case	VBK$K_TAG_LOCOFF:	a_attr->loc.off	= (uint32_t) vbk$tlv_getu(l_vlen, l_val);	break;
			case	VBK$K_TAG_MTIME:	vbk$tlv_gettime(l_vlen, l_val, &a_attr->mtime);			break;
			case	VBK$K_TAG_ATIME:	vbk$tlv_gettime(l_vlen, l_val, &a_attr->atime);			break;
			case	VBK$K_TAG_CTIME:
				vbk$tlv_gettime(l_vlen, l_val, &a_attr->ctime);
				a_attr->hasctime = 1;
				break;

			case	VBK$K_TAG_BTIME:
				vbk$tlv_gettime(l_vlen, l_val, &a_attr->btime);
				a_attr->hasbtime = 1;
				break;

			case	VBK$K_TAG_FSFLAGS:
				a_attr->fsflags	 = (uint32_t) vbk$tlv_getu(l_vlen, l_val);
				a_attr->hasflags = 1;
				break;

			case	VBK$K_TAG_DEVINO:
				if ( l_vlen == 16 )
					{
					a_attr->dev	= vbk$get64(l_val);
					a_attr->ino	= vbk$get64(l_val + 8);
					a_attr->hasdevino = 1;
					}
				break;

			case	VBK$K_TAG_PATH:
				a_attr->path	= (const char *) l_val;
				a_attr->pathlen	= l_vlen;
				break;

			case	VBK$K_TAG_LINK:
				a_attr->link	= (const char *) l_val;
				a_attr->linklen	= l_vlen;
				break;

			case	VBK$K_TAG_UNAME:
				if ( l_vlen < sizeof(l_uname) )
					{
					memcpy(l_uname, l_val, l_vlen);
					l_uname [l_vlen] = '\0';
					a_attr->uname	= l_uname;
					}
				break;

			case	VBK$K_TAG_GNAME:
				if ( l_vlen < sizeof(l_gname) )
					{
					memcpy(l_gname, l_val, l_vlen);
					l_gname [l_vlen] = '\0';
					a_attr->gname	= l_gname;
					}
				break;
			}
		}

	if ( (l_status == STS$K_ERROR) || !a_attr->fileno || !a_attr->path || !a_attr->ftype )
		return	STS$K_ERROR;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Decide whom a restored file is to belong to, by /OWNER: ORIGINAL -
**	the owner by name when that name is known here, by number else;
**	a user given; DEFAULT - nobody is set, the file stays the creator's.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command
**	a_attr		The attributes of the file
**	a_uid, a_gid	Receive the owner
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- an owner is to be set;
**	STS$K_WARN	- none is.
**--
*/
int	vbk$atr_owner	(
	const	VBK$OPTS *	a_opts,
	const	VBK$ATTR *	a_attr,
		uid_t *		a_uid,
		gid_t *		a_gid
			)
{
struct passwd	l_pw, *l_pres = NULL;
struct group	l_gr, *l_gres = NULL;
char		l_buf [4096];

	switch ( a_opts->ownmode )
		{
		case	VBACKUP$K_OWN_DEFAULT:
			return	STS$K_WARN;

		case	VBACKUP$K_OWN_USER:
			*a_uid	= a_opts->ownuid;
			*a_gid	= a_opts->owngid;

			return	STS$K_SUCCESS;
		}

	*a_uid	= a_attr->uid;
	*a_gid	= a_attr->gid;

	if ( a_attr->uname && !getpwnam_r(a_attr->uname, &l_pw, l_buf, sizeof(l_buf), &l_pres) && l_pres )
		*a_uid	= l_pw.pw_uid;

	if ( a_attr->gname && !getgrnam_r(a_attr->gname, &l_gr, l_buf, sizeof(l_buf), &l_gres) && l_gres )
		*a_gid	= l_gr.gr_gid;

	return	STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	Put the owner, the mode and the extended attributes back on a
**	restored file.  Times and chattr flags are not touched here.
**
**  FORMAL PARAMETERS:
**
**	a_opts		The command: /OWNER, /[NO]XATTRS
**	a_path		The file
**	a_fd		An open descriptor of it, -1 - none
**	a_attr		The attributes
**
**  RETURN VALUE:
**	STS$K_SUCCESS	- all put back;
**	STS$K_WARN	- something was not, and has been reported.
**--
*/
int	vbk$atr_apply	(
	const	VBK$OPTS *	a_opts,
	const	char *		a_path,
		int		a_fd,
	const	VBK$ATTR *	a_attr
			)
{
uid_t		l_uid;
gid_t		l_gid;
uint32_t	l_pos = 0, l_vlen;
uint16_t	l_tag;
const uint8_t *	l_val;
int		l_rc, l_status = STS$K_SUCCESS, l_islnk = (a_attr->ftype == VBK$K_FT_SYMLINK);

	if ( 1 & vbk$atr_owner(a_opts, a_attr, &l_uid, &l_gid) )
		{
		l_rc	= (a_fd >= 0) ? fchown(a_fd, l_uid, l_gid) : lchown(a_path, l_uid, l_gid);

		if ( l_rc )
			l_status = $VBKMSG(VBACKUP$_ATTRERR, a_path, "owner", errno, strerror(errno));
		}

	if ( !l_islnk )
		{
		l_rc	= (a_fd >= 0) ? fchmod(a_fd, a_attr->mode) : fchmodat(AT_FDCWD, a_path, a_attr->mode, AT_SYMLINK_NOFOLLOW);

		if ( l_rc )
			l_status = $VBKMSG(VBACKUP$_ATTRERR, a_path, "protection", errno, strerror(errno));
		}

	if ( !a_opts->xattrs )
		return	(1 & l_status) ? STS$K_SUCCESS : STS$K_WARN;

	while ( 1 & vbk$tlv_next(a_attr->xattr, a_attr->xattrlen, &l_pos, &l_tag, &l_vlen, &l_val) )
		{
		char		l_name [256];
		uint32_t	l_nlen;

		if ( l_tag != VBK$K_TAG_XATTR )
			continue;

		/* A counted name; the system call wants it ended by a NUL, so it is copied out */
		if ( !l_vlen || !(l_nlen = l_val [0]) || ((1 + l_nlen) > l_vlen) || memchr(l_val + 1, '\0', l_nlen) )
			continue;

		memcpy(l_name, l_val + 1, l_nlen);
		l_name [l_nlen] = '\0';

		l_rc	= (a_fd >= 0) ? fsetxattr(a_fd, l_name, l_val + 1 + l_nlen, l_vlen - 1 - l_nlen, 0)
				      : lsetxattr(a_path, l_name, l_val + 1 + l_nlen, l_vlen - 1 - l_nlen, 0);

		if ( l_rc )
			l_status = $VBKMSG(VBACKUP$_ATTRERR, a_path, l_name, errno, strerror(errno));
		}

	return	(1 & l_status) ? STS$K_SUCCESS : STS$K_WARN;
}


/*
**  The times of a restored file, the link itself for a symlink
*/
int	vbk$atr_settimes	(
	const	char *		a_path,
	const	VBK$ATTR *	a_attr
			)
{
struct timespec	l_ts [2];

	l_ts [0].tv_sec	 = (time_t) a_attr->atime.sec;
	l_ts [0].tv_nsec = (long) a_attr->atime.nsec;
	l_ts [1].tv_sec	 = (time_t) a_attr->mtime.sec;
	l_ts [1].tv_nsec = (long) a_attr->mtime.nsec;

	if ( utimensat(AT_FDCWD, a_path, l_ts, AT_SYMLINK_NOFOLLOW) )
		return	$VBKMSG(VBACKUP$_ATTRERR, a_path, "times", errno, strerror(errno));

	return	STS$K_SUCCESS;
}


/*
**  The chattr flags of a restored file - the portable ones only, and only
**  when they differ from what the file has already
*/
int	vbk$atr_setflags	(
	const	char *		a_path,
		uint32_t	a_flags
			)
{
int	l_fd, l_cur = 0, l_new;

	if ( 0 > (l_fd = open(a_path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC)) )
		return	$VBKMSG(VBACKUP$_ATTRERR, a_path, "flags", errno, strerror(errno));

	if ( ioctl(l_fd, FS_IOC_GETFLAGS, &l_cur) )
		{
		/* The file system has no flags at all: there is nothing to put back */
		close(l_fd);

		return	((errno == ENOTTY) || (errno == EOPNOTSUPP)) && !(a_flags & VBK$M_FSFLAGS) ? STS$K_SUCCESS
			: $VBKMSG(VBACKUP$_ATTRERR, a_path, "flags", errno, strerror(errno));
		}

	l_new	= (int) (((uint32_t) l_cur & ~VBK$M_FSFLAGS) | (a_flags & VBK$M_FSFLAGS));

	if ( (l_new != l_cur) && ioctl(l_fd, FS_IOC_SETFLAGS, &l_new) )
		{
		close(l_fd);

		return	$VBKMSG(VBACKUP$_ATTRERR, a_path, "flags", errno, strerror(errno));
		}

	close(l_fd);

	return	STS$K_SUCCESS;
}


/*
**  The time /SINCE and /BEFORE look at; STS$K_WARN - the file has none
**  such (no creation time on this file system)
*/
int	vbk$atr_xtime	(
	const	VBK$ATTR *	a_attr,
		int		a_timsrc,
		fao_time_t *	a_time
			)
{
	switch ( a_timsrc )
		{
		case	VBACKUP$K_TIM_CREATED:
			if ( !a_attr->hasbtime )
				return	STS$K_WARN;

			*a_time	= (fao_time_t) a_attr->btime.sec;
			break;

		case	VBACKUP$K_TIM_CHANGED:
			*a_time	= (fao_time_t) a_attr->ctime.sec;
			break;

		default:
			*a_time	= (fao_time_t) a_attr->mtime.sec;
		}

	return	STS$K_SUCCESS;
}
