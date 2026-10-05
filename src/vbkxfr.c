#define	__MODULE__	"VBKXFR"
#define	__IDENT__	"X01-11"
#define	__REV__		"1.11.0"

/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	vbkxfr.c
**
**  ABSTRACT:	A saveset to a saveset, block for block: from a pipe into
**		its volume files, from its volume files into a pipe, from
**		one place to another.
**
**  DESCRIPTION: vbackup x.bck -		the volumes, back to back, to stdout
**		vbackup - y.bck			a stream into y.bck, y.bck.002, ...
**		vbackup x.bck y.bck		a copy of the saveset
**
**		The blocks are copied as they are, never the records: an
**		encrypted saveset goes without its passphrase, and what comes
**		out is byte for byte what went in.  A stream is split into
**		volume files at each VHDR (format.md, section 2).  Every block
**		is checked on the way: a bad one is said (BLKCOPIED) and
**		copied as it is - the copy keeps what it was given, a restore
**		repairs it from its group.  A saveset that ends without its
**		TRAILER is said (NOTRAILER).  So the receiving end of a save
**		over the network checks what arrived.
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
#include	<unistd.h>
#include	<sys/stat.h>

#include	"vbkdef.h"

typedef struct vbk_xfr_t
{
	VBK$OPTS *	opts;
	const char *	in;			/* The input saveset, "-" - the standard input	*/
	const char *	out;			/* The output, "-" - the standard output	*/
	uint32_t	bsize;
	uint8_t		ssuuid [VBK$K_UUIDSZ];
	uint8_t *	blk;

	int		infd;			/* The volume of the input being read		*/
	uint32_t	invol;

	int		outfd;			/* The volume of the output being written	*/
	uint32_t	outvol;
	char		outspec [VBACKUP$K_SZ_PATH];

	uint64_t	nblocks, nbad;
	uint32_t	nvols;
	int		trailer;		/* The last block copied was the TRAILER	*/
	int		failed;
} VBK$XFR;


/*
**  Read one whole block; 0 - the end of the input (a part of a block at
**  the end is no block), -1 - an error, reported
*/
static	int	s_vbk$xread	(
		VBK$XFR *	a_x
			)
{
size_t	l_got = 0;
ssize_t	l_rc;

	while ( l_got < a_x->bsize )
		{
		if ( 0 > (l_rc = read(a_x->infd, a_x->blk + l_got, a_x->bsize - l_got)) )
			{
			if ( errno == EINTR )
				continue;

			$VBKMSG(VBACKUP$_READERR, a_x->in, errno, strerror(errno));

			return	-1;
			}

		if ( !l_rc )
			break;

		l_got	+= (size_t) l_rc;
		}

	return	(l_got == a_x->bsize) ? 1 : 0;
}


/*
**  Close the output volume in hand: on the disk for good (fsync)
*/
static	int	s_vbk$xclose	(
		VBK$XFR *	a_x
			)
{
int	l_status = STS$K_SUCCESS;

	if ( (a_x->outfd < 0) || (a_x->outfd == STDOUT_FILENO) )
		return	STS$K_SUCCESS;

	if ( fsync(a_x->outfd) || close(a_x->outfd) )
		l_status = $VBKMSG(VBACKUP$_WRITERR, a_x->outspec, errno, strerror(errno));

	a_x->outfd	= -1;

	return	l_status;
}


/*
**  The output volume <a_volno>: a file of its own, or the standard output
*/
static	int	s_vbk$xopen	(
		VBK$XFR *	a_x,
		uint32_t	a_volno
			)
{
int	l_flags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC;

	if ( !strcmp(a_x->out, "-") )
		{
		a_x->outfd	= STDOUT_FILENO;
		a_x->outvol	= a_volno;

		return	STS$K_SUCCESS;
		}

	if ( !(1 & s_vbk$xclose(a_x)) )
		return	STS$K_ERROR;

	if ( !(1 & vbk$volspec(a_x->out, a_volno, a_x->outspec, sizeof(a_x->outspec))) )
		return	$VBKMSG(VBACKUP$_OPENOUT, a_x->out, ENAMETOOLONG, strerror(ENAMETOOLONG));

	if ( !a_x->opts->replace )
		l_flags	|= O_EXCL;

	if ( 0 > (a_x->outfd = open(a_x->outspec, l_flags, 0644)) )
		return	$VBKMSG(VBACKUP$_OPENOUT, a_x->outspec, errno, strerror(errno));

	a_x->outvol	= a_volno;
	a_x->nvols++;

	if ( a_x->opts->log || (a_volno > 1) )
		$VBKMSG(VBACKUP$_CREATED, a_x->outspec);

	return	STS$K_SUCCESS;
}


/*
**  Copy the block in hand: checked, split at a VHDR, written
*/
static	int	s_vbk$xblock	(
		VBK$XFR *	a_x
			)
{
VBK$BHDR	l_h;
int		l_ok = 1 & vbk$blk_check(a_x->blk, a_x->bsize, a_x->ssuuid, &l_h);
size_t		l_put = 0;
ssize_t		l_rc;

	/* A good VHDR begins an output volume; a bad block is copied as it is, and said */
	if ( l_ok && (l_h.type == VBK$K_BT_VHDR) && (l_h.volno != a_x->outvol) )
		{
		if ( !(1 & s_vbk$xopen(a_x, l_h.volno)) )
			return	STS$K_ERROR;
		}
	else if ( !l_ok )
		{
		a_x->nbad++;
		$VBKMSG(VBACKUP$_BLKCOPIED, a_x->nblocks, a_x->outvol);
		}

	a_x->trailer = l_ok && ((l_h.type == VBK$K_BT_TRAILER) || (l_h.type == VBK$K_BT_ETRAILER));

	while ( l_put < a_x->bsize )
		{
		if ( 0 > (l_rc = write(a_x->outfd, a_x->blk + l_put, a_x->bsize - l_put)) )
			{
			if ( errno == EINTR )
				continue;

			return	$VBKMSG(VBACKUP$_WRITERR, strcmp(a_x->out, "-") ? a_x->outspec : "(standard output)", errno, strerror(errno));
			}

		l_put	+= (size_t) l_rc;
		}

	a_x->nblocks++;

	return	STS$K_SUCCESS;
}


/*
**  All the blocks of one input volume (or of the stream)
*/
static	int	s_vbk$xvolume	(
		VBK$XFR *	a_x
			)
{
int	l_rc;

	while ( 0 < (l_rc = s_vbk$xread(a_x)) )
		if ( !(1 & s_vbk$xblock(a_x)) )
			return	STS$K_ERROR;

	return	l_rc ? STS$K_ERROR : STS$K_SUCCESS;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**	A saveset to a saveset, block for block (see the head of the module).
**
**  FORMAL PARAMETERS:
**
**	a_opts		The input (one saveset or "-"), the output, /REPLACE, /LOG
**
**  RETURN VALUE:
**	STS$K_SUCCESS, or the condition reported.
**--
*/
int	vbk$transfer	(
		VBK$OPTS *	a_opts
			)
{
VBK$XFR		l_x = { .opts = a_opts, .in = a_opts->input [0], .out = a_opts->output, .infd = -1, .outfd = -1 };
uint8_t		l_hdr [VBK$K_HDRSZ];
VBK$BHDR	l_h;
size_t		l_got = 0;
ssize_t		l_rc;
int		l_status = STS$K_SUCCESS, l_stream = !strcmp(l_x.in, "-");

	l_x.infd = l_stream ? STDIN_FILENO : open(l_x.in, O_RDONLY | O_CLOEXEC);

	if ( l_x.infd < 0 )
		return	$VBKMSG(VBACKUP$_OPENIN, l_x.in, errno, strerror(errno));

	/* The block size and the saveset are those of the first VHDR; nothing else is trusted before its checksum */
	while ( l_got < sizeof(l_hdr) )
		{
		if ( 0 >= (l_rc = read(l_x.infd, l_hdr + l_got, sizeof(l_hdr) - l_got)) )
			{
			if ( (l_rc < 0) && (errno == EINTR) )
				continue;

			break;
			}

		l_got	+= (size_t) l_rc;
		}

	if ( (l_got < sizeof(l_hdr)) || !(1 & vbk$bhdr_peek(l_hdr, &l_h)) || (l_h.bsize < VBK$K_MINBSZ) || (l_h.bsize > VBK$K_MAXBSZ)
		|| (l_h.bsize % VBK$K_BSZALIGN) || !(l_x.blk = malloc(l_h.bsize)) )
		{
		if ( !l_stream )
			close(l_x.infd);

		return	$VBKMSG(VBACKUP$_NOTSAVESET, l_x.in);
		}

	l_x.bsize = l_h.bsize;
	memcpy(l_x.blk, l_hdr, sizeof(l_hdr));

	/* The rest of the first block, then it is judged: a VHDR of volume 1, of a saveset */
	for ( l_got = sizeof(l_hdr); l_got < l_x.bsize; )
		{
		if ( 0 >= (l_rc = read(l_x.infd, l_x.blk + l_got, l_x.bsize - l_got)) )
			{
			if ( (l_rc < 0) && (errno == EINTR) )
				continue;

			break;
			}

		l_got	+= (size_t) l_rc;
		}

	if ( (l_got < l_x.bsize) || !(1 & vbk$blk_check(l_x.blk, l_x.bsize, NULL, &l_h)) || (l_h.type != VBK$K_BT_VHDR) || (l_h.volno != 1) )
		{
		if ( !l_stream )
			close(l_x.infd);

		free(l_x.blk);

		return	$VBKMSG(VBACKUP$_NOTSAVESET, l_x.in);
		}

	memcpy(l_x.ssuuid, l_h.ssuuid, VBK$K_UUIDSZ);

	if ( !(1 & s_vbk$xblock(&l_x)) )
		l_status = STS$K_ERROR;
	else if ( l_stream )
		l_status = s_vbk$xvolume(&l_x);
	else	{
		/* The volume files of the input, in order: one missing is said, the others still go */
		uint32_t	l_miss = 0;

		l_status = s_vbk$xvolume(&l_x);
		close(l_x.infd);

		for ( uint32_t l_v = 2; (1 & l_status) && (l_v <= VBK$K_MAXVOL) && (l_miss < VBK$K_VOLGAP); l_v++ )
			{
			char	l_spec [VBACKUP$K_SZ_PATH];

			if ( !(1 & vbk$volspec(l_x.in, l_v, l_spec, sizeof(l_spec))) || (0 > (l_x.infd = open(l_spec, O_RDONLY | O_CLOEXEC))) )
				{
				l_miss++;
				continue;
				}

			for ( uint32_t l_m = l_v - l_miss; l_miss && (l_m < l_v); l_m++ )
				$VBKMSG(VBACKUP$_MISSVOL, l_m, l_x.in);

			l_miss	 = 0;
			l_status = s_vbk$xvolume(&l_x);
			close(l_x.infd);
			}
		}

	if ( !(1 & s_vbk$xclose(&l_x)) )
		l_status = STS$K_ERROR;

	if ( (1 & l_status) && !l_x.trailer )
		$VBKMSG(VBACKUP$_NOTRAILER, l_stream ? "(standard input)" : l_x.in);

	$VBKMSG(VBACKUP$_XFRSUMM, l_x.nblocks, strcmp(l_x.out, "-") ? l_x.nvols : l_x.outvol, l_x.nbad);

	free(l_x.blk);

	return	l_status;
}
