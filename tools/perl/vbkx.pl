#!/usr/bin/perl
#+++
#
#  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#  MODULE:	tools/perl/vbkx.pl
#
#  ABSTRACT:	vbkx-pl - the extractor of last resort for VBACKUP savesets,
#		in Perl.  One text file, nothing to build: for the day
#		everything else is dead and only the Perl every Debian and
#		Ubuntu system carries (the "perl-base" package, which cannot
#		even be removed) is left.  Simple and plain on purpose - one
#		block at a time, no tricks - so that a human can read it
#		against doc/format.md and fix it.  Speed is not a goal
#		(a few MB a second); getting the data out is.
#
#  NEEDS:	perl 5.10 or later; only what perl-base has: strict,
#		warnings, Fcntl, POSIX.  Nothing to build, nothing to install:
#		copy this file next to the saveset and run it with perl.
#		A 32-bit perl will do: 64-bit fields are read as two halves
#		(exact up to 2**53, which no file size or block number
#		reaches).
#
#  USAGE:	perl vbkx.pl l saveset              list the files
#		perl vbkx.pl x saveset [-C dir]     extract them all into dir
#						    (default: the current one)
#		perl vbkx.pl t saveset              read it all, check the checksums
#
#		saveset is volume 1 (x.bck); volumes 2, 3, ... are looked
#		for beside it as x.bck.002, x.bck.003, ...
#
#		  $ perl vbkx.pl l /mnt/usb/home.bck
#		  $ perl vbkx.pl x /mnt/usb/home.bck -C /tmp/restore
#		  $ perl vbkx.pl t /mnt/usb/home.bck
#
#  LISTING:	one line per file, the time in UTC, always (no time zone
#		files are needed):
#
#		  2026-10-03 20:40:12         1234 -0644 tree/a.txt
#		  2026-10-03 20:40:12            5 l0777 tree/link -> a.txt
#		  2026-10-03 20:40:12            6 h0644 tree/hard link to tree/a.txt
#
#		The type letters are those of ls: - d l h(ard link) c b p s.
#		The listing is taken from the FILE records of the stream, so
#		it needs no catalog.  It is the listing of vbkx-go and vbkx-rs,
#		byte for byte.
#
#  RESTORED:	data (holes stay holes), mode (with setuid/setgid/sticky),
#		modification and access times (to the nanosecond where the
#		system call is known for the machine - x86_64, aarch64, and
#		a few more; else to the second, and a symbolic link keeps
#		the time of its making), directories, symbolic links, hard
#		links, FIFOs.
#		NOT restored: owners, ACLs, extended attributes, chattr
#		flags, device files, sockets - use vbackup or vbkx for those.
#		A file that is there is never overwritten.  Names with "..",
#		a leading "/" or an empty component are refused, and so is a
#		way through a symbolic link.
#
#  DAMAGE:	every block is checked (CRC-32); one bad block in a group is
#		rebuilt from the group's XOR block; after a loss the stream
#		is picked up at the next good block.  A file that lost data
#		is kept as far as it got and named: "<name> is incomplete".
#		A file whose records were lost entirely is named from the
#		catalog: "<name> was not extracted"; without a readable
#		catalog that is said once ("... cannot all be named").  A
#		missing or cut volume is skipped, the next one is read.  A
#		volume 1 whose first block (VHDR) is bad is still read: the
#		block size is found by trying every legal size against the
#		CRC of the blocks after it.
#
#  GUARANTEED:	no crash and no hang on any input; nothing written outside
#		the output directory; no silent damage - a file that is not
#		named in a message is the file that was saved; the same
#		input gives the same output, byte for byte (no hash order,
#		no clock, no randomness anywhere).
#
#  EXIT:	0 - all done; 1 - something was damaged or not done;
#		2 - the command or the saveset cannot be used.
#
#		Messages go to the standard error, each beginning "vbkx-pl: ".
#
#  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#  CREATION DATE:  4-OCT-2026
#
#  MODIFICATION HISTORY:
#
#	X01-03		 4-OCT-2026	RRL
#		Initial version.
#
#---

use strict;
use warnings;
use Fcntl qw(O_RDONLY O_WRONLY O_CREAT O_EXCL O_NOFOLLOW SEEK_SET);
use POSIX ();

# The numbers of doc/format.md
use constant {
	HDRSZ	=> 64,			# block header, section 3
	NONE	=> 0xFFFFFFFF,		# "no record begins here"
	MAXREC	=> 16 * 1048576,	# sanity ceiling of a record body
	MINBSZ	=> 8192,
	MAXBSZ	=> 1048576,
	MAXGRP	=> 100,
	MAXVOL	=> 9999,
	VOLGAP	=> 16,			# missing volume names in a row that end the search

	BT_DATA	=> 1, BT_XOR => 2, BT_VHDR => 3, BT_TRAILER => 4,
	RT_SUMMARY => 1, RT_FILE => 2, RT_DATA => 3, RT_FEND => 4, RT_CATALOG => 5, RT_END => 6,
	FT_REG => 1, FT_DIR => 2, FT_SYMLINK => 3, FT_HARDLINK => 4, FT_FIFO => 7,
	FS_CHANGED => 1, FS_READERR => 2, FS_PRESENT => 3,
	TWO32	=> 4294967296,
};

binmode(STDOUT);
binmode(STDERR);

my $bad = 0;			# something was damaged or not done: completion 1

sub msg { my $s = sprintf(shift, @_); print STDERR "vbkx-pl: $s\n"; }

#
#  Little-endian fields, section 1.  Every one is checked against the length
#  of its string first: what is not there reads as 0.
#
sub u8  { my ($b, $o) = @_; return ($o >= 0 && $o + 1 <= length($$b)) ? unpack('C', substr($$b, $o, 1)) : 0; }
sub u16 { my ($b, $o) = @_; return ($o >= 0 && $o + 2 <= length($$b)) ? unpack('v', substr($$b, $o, 2)) : 0; }
sub u32 { my ($b, $o) = @_; return ($o >= 0 && $o + 4 <= length($$b)) ? unpack('V', substr($$b, $o, 4)) : 0; }

sub u64
{
	my ($b, $o) = @_;
	return 0 unless $o >= 0 && $o + 8 <= length($$b);
	my ($lo, $hi) = unpack('V V', substr($$b, $o, 8));
	return $hi * TWO32 + $lo;
}

sub i64
{
	my ($b, $o) = @_;
	return 0 unless $o >= 0 && $o + 8 <= length($$b);
	my ($lo, $hi) = unpack('V V', substr($$b, $o, 8));
	return ($hi >= 2147483648) ? (($hi - TWO32) * TWO32 + $lo) : ($hi * TWO32 + $lo);
}

#
#  CRC-32/IEEE, section 1: reflected 0xEDB88320, in and out XOR 0xFFFFFFFF.
#  crc(crc, data) chains, as zlib's crc32() does; crc(0, "123456789") is
#  0xCBF43926.
#
my @CRCTAB;
for my $n (0 .. 255)
{
	my $c = $n;
	for (1 .. 8) { $c = ($c & 1) ? (0xEDB88320 ^ ($c >> 1)) : ($c >> 1); }
	$CRCTAB[$n] = $c;
}

sub crc
{
	my ($c, $d) = @_;
	$c ^= 0xFFFFFFFF;
	$c = $CRCTAB[($c ^ $_) & 0xFF] ^ ($c >> 8) for unpack('C*', $d);
	return $c ^ 0xFFFFFFFF;
}

#
#  Decode and check a block (section 3): magic, header length, version,
#  size, saveset (uuid undef - any), type, lengths, and the CRC of the
#  header (its CRC field taken as 0) followed by the whole payload area.
#  Returns the header as a hash reference, or undef.
#
sub check
{
	my ($b, $bsize, $uuid) = @_;
	return undef unless length($$b) == $bsize && $bsize >= HDRSZ;
	return undef unless substr($$b, 0, 4) eq 'VBKB' && u16($b, 4) == HDRSZ && u16($b, 6) == 1;
	my %h = (
		bsize	  => u32($b, 8),
		typ	  => u8($b, 12),
		gindex	  => u16($b, 14),
		uuid	  => substr($$b, 16, 16),
		blkno	  => u64($b, 32),
		volno	  => u32($b, 40),
		recoff	  => u32($b, 44),
		paylen	  => u32($b, 48),
		prvrecoff => u32($b, 52),
		prvpaylen => u32($b, 56),
		crc	  => u32($b, 60),
	);
	my $psize = $bsize - HDRSZ;
	return undef if $h{bsize} != $bsize || (defined($uuid) && $h{uuid} ne $uuid);
	return undef if $h{typ} < BT_DATA || $h{typ} > BT_TRAILER || $h{paylen} > $psize;
	return undef if $h{recoff} != NONE && $h{recoff} >= $psize;
	my $c = crc(0, substr($$b, 0, 60) . "\0\0\0\0");
	$c = crc($c, substr($$b, HDRSZ));
	return ($c == $h{crc}) ? \%h : undef;
}

#
#  TLV items, section 6: u16 tag, u32 length, value.  Returns a list of
#  [tag, value] in their order, and whether the body was whole.
#
sub tlv
{
	my ($body) = @_;
	my ($pos, $len, @items) = (0, length($body));
	while ($pos < $len)
	{
		return (\@items, 0) if $len - $pos < 6;
		my ($tag, $vlen) = unpack('v V', substr($body, $pos, 6));
		return (\@items, 0) if $vlen > $len - $pos - 6;
		push @items, [ $tag, substr($body, $pos + 6, $vlen) ];
		$pos += 6 + $vlen;
	}
	return (\@items, 1);
}

sub getu
{
	my ($v) = @_;
	my $l = length($v);
	return unpack('C', $v) if $l == 1;
	return unpack('v', $v) if $l == 2;
	return unpack('V', $v) if $l == 4;
	return u64(\$v, 0)     if $l == 8;
	return 0;
}

sub gettime
{
	my ($v) = @_;
	return (0, 0) unless length($v) == 12;
	my $ns = u32(\$v, 8);
	return (i64(\$v, 0), ($ns > 999999999) ? 0 : $ns);
}

#
#  One file as a FILE record or a catalog entry describes it (6.1);
#  undef - it makes no sense
#
sub parse_entry
{
	my ($body) = @_;
	my ($items, $whole) = tlv($body);
	my %e = (fileno => 0, path => undef, link => '', ftype => 0, mode => 0, size => 0,
		 msec => 0, mnsec => 0, asec => 0, ansec => 0, status => 0);
	for my $it (@$items)
	{
		my ($tag, $v) = @$it;
		if    ($tag == 1)  { $e{fileno} = getu($v); }
		elsif ($tag == 2)  { $e{path}	= $v; }
		elsif ($tag == 3)  { $e{ftype}	= getu($v); }
		elsif ($tag == 4)  { $e{mode}	= getu($v); }
		elsif ($tag == 9)  { $e{size}	= getu($v); }
		elsif ($tag == 10) { ($e{msec}, $e{mnsec}) = gettime($v); }
		elsif ($tag == 11) { ($e{asec}, $e{ansec}) = gettime($v); }
		elsif ($tag == 15) { $e{link}	= $v; }
		elsif ($tag == 33) { $e{status} = getu($v); }
		# 16 XATTR, and every other tag, is skipped
	}
	($e{asec}, $e{ansec}) = ($e{msec}, $e{mnsec}) if $e{asec} == 0 && $e{ansec} == 0;
	return undef unless $whole && defined($e{path}) && length($e{path}) && $e{ftype};
	return \%e;
}

#
#  The saveset being read
#
my %R = (
	spec	=> '',
	bsize	=> 0,
	grpsz	=> 0,
	uuid	=> '',
	vols	=> [],		# [ { fh, firstblk, nblk } or undef - missing ], index volno - 1
	trailer	=> 0,

	curvol	=> 1,		# where the next group is read from
	curpos	=> 1,		# ... block position in that volume
	pays	=> [],		# the payloads of the DATA blocks of the group, undef - lost
	recoffs	=> [],
	blks	=> [],
	gvol	=> 1,
	next	=> 0,

	gap	=> 0,		# a block has been lost since the last record
	pay	=> undef,
	payoff	=> 0,
	payblk	=> 0,
	payvol	=> 0,
	resync	=> 0,		# the record returned follows a loss
);

sub volspec { my ($spec, $n) = @_; return ($n <= 1) ? $spec : sprintf('%s.%03d', $spec, $n); }

# Read a block; what cannot be read is zeros - such a block fails its check
sub read_block
{
	my ($fh, $bsize, $pos) = @_;
	my $buf = '';
	if (defined($fh) && sysseek($fh, $pos * $bsize, SEEK_SET))
	{
		while (length($buf) < $bsize)
		{
			my $n = sysread($fh, $buf, $bsize - length($buf), length($buf));
			last unless $n;
		}
	}
	$buf .= "\0" x ($bsize - length($buf)) if length($buf) < $bsize;
	return \$buf;
}

sub blocks_in
{
	my ($fh, $bsize) = @_;
	my @st = stat($fh);
	return 0 unless @st && $st[7] > 0;
	return int($st[7] / $bsize);
}

# The group size, out of the SUMMARY record a VHDR carries
sub summary_group
{
	my ($b, $h) = @_;
	my $pay = substr($$b, HDRSZ, $h->{paylen});
	return undef unless length($pay) >= 8 && u16(\$pay, 0) == RT_SUMMARY;
	my $blen = u32(\$pay, 4);
	return undef if $blen > length($pay) - 8;
	my ($items) = tlv(substr($pay, 8, $blen));
	my $grp = 0;
	for my $it (@$items) { $grp = getu($it->[1]) if $it->[0] == 71; }
	return ($grp <= MAXGRP) ? $grp : undef;
}

#
#  Volume 1 without a good VHDR: try every legal block size against the
#  blocks after the first one; the first that checks gives the size and the
#  saveset.  The group size is then where the first XOR block stands.
#
sub guess
{
	my ($fh) = @_;
	for (my $bs = MINBSZ; $bs <= MAXBSZ; $bs += 512)
	{
		for my $pos (1 .. 8)
		{
			my $h = check(read_block($fh, $bs, $pos), $bs, undef);
			next unless $h && $h->{volno} == 1 && $h->{blkno} == $pos;
			$R{bsize} = $bs;
			$R{uuid}  = $h->{uuid};
			$R{grpsz} = 0;
			for my $p (1 .. MAXGRP + 1)
			{
				my $x = check(read_block($fh, $bs, $p), $bs, $R{uuid});
				if ($x && $x->{typ} == BT_XOR && $x->{gindex} == $p - 1)
				{
					$R{grpsz} = $x->{gindex};
					last;
				}
			}
			msg('%s: the first block is bad; block size %d and group size %d found by trying', $R{spec}, $bs, $R{grpsz});
			return 1;
		}
	}
	return 0;
}

# Open the saveset; an error message - it cannot be used
sub open_saveset
{
	my ($spec) = @_;
	$R{spec} = $spec;
	my $fh;
	sysopen($fh, $spec, O_RDONLY) or return "$spec: $!";

	# The block size comes from the first header; it is believed only when the block checks
	my $head = ${ read_block($fh, HDRSZ, 0) };
	my $found = 0;
	my $bs = u32(\$head, 8);
	if (substr($head, 0, 4) eq 'VBKB' && $bs >= MINBSZ && $bs <= MAXBSZ && $bs % 512 == 0)
	{
		my $blk = read_block($fh, $bs, 0);
		my $h = check($blk, $bs, undef);
		if ($h && $h->{typ} == BT_VHDR && $h->{volno} == 1)
		{
			my $grp = summary_group($blk, $h);
			if (defined($grp))
			{
				@R{qw(bsize uuid grpsz)} = ($bs, $h->{uuid}, $grp);
				$found = 1;
			}
		}
	}
	if (!$found && !guess($fh))
	{
		close($fh);
		return "$spec is not a saveset";
	}
	push @{ $R{vols} }, { fh => $fh, firstblk => 0, nblk => blocks_in($fh, $R{bsize}) };

	# The further volumes; a missing name does not end the search at once
	my $miss = 0;
	for (my $n = 2; $n <= MAXVOL && $miss < VOLGAP; $n++)
	{
		my $vf;
		if (!sysopen($vf, volspec($spec, $n), O_RDONLY))
		{
			$miss++;
			next;
		}
		# Its VHDR, or else its next block, tells where it begins
		my $first;
		my $h = check(read_block($vf, $R{bsize}, 0), $R{bsize}, $R{uuid});
		if ($h && $h->{typ} == BT_VHDR && $h->{volno} == $n)
		{
			$first = $h->{blkno};
		}
		elsif (($h = check(read_block($vf, $R{bsize}, 1), $R{bsize}, $R{uuid})) && $h->{volno} == $n && $h->{blkno} > 0)
		{
			$first = $h->{blkno} - 1;
			msg('volume %d: its first block is bad, it is read all the same', $n);
		}
		else
		{
			msg('volume %d belongs to another saveset, or is none', $n);
			close($vf);
			$miss++;
			next;
		}
		push @{ $R{vols} }, undef while @{ $R{vols} } < $n - 1;
		push @{ $R{vols} }, { fh => $vf, firstblk => $first, nblk => blocks_in($vf, $R{bsize}) };
		$miss = 0;
	}

	# The TRAILER, last block of the last volume: it is not part of the groups
	my $lv = $R{vols}[-1];
	if ($lv->{nblk} > 1)
	{
		my $th = check(read_block($lv->{fh}, $R{bsize}, $lv->{nblk} - 1), $R{bsize}, $R{uuid});
		$R{trailer} = 1 if $th && $th->{typ} == BT_TRAILER;
	}
	return undef;
}

sub volend
{
	my ($n) = @_;
	my $v = $R{vols}[$n - 1];
	return ($R{trailer} && $n == @{ $R{vols} } && $v->{nblk} > 0) ? $v->{nblk} - 1 : $v->{nblk};
}

#
#  Read the next group (section 4), check its blocks, rebuild one bad DATA
#  block from the XOR block; 0 - the end of the saveset.
#
sub load_group
{
	my ($v, $end);
	for (;;)
	{
		return 0 if $R{curvol} > @{ $R{vols} };
		$v = $R{vols}[$R{curvol} - 1];
		if (!defined($v))
		{
			msg('volume %d is missing', $R{curvol});
			$bad = 1;
			$R{gap} = 1;
			$R{curvol}++;
			$R{curpos} = 1;
			next;
		}
		$end = volend($R{curvol});
		if ($R{curpos} >= $end)
		{
			$R{curvol}++;
			$R{curpos} = 1;
			next;
		}
		last;
	}
	my $n = ($R{grpsz} > 0) ? $R{grpsz} + 1 : 1;
	$n = $end - $R{curpos} if $end - $R{curpos} < $n;

	my (@blks, @hdrs, @ok);
	for my $i (0 .. $n - 1)
	{
		$blks[$i] = read_block($v->{fh}, $R{bsize}, $R{curpos} + $i);
		$hdrs[$i] = check($blks[$i], $R{bsize}, $R{uuid});
		$ok[$i] = ($hdrs[$i] && $hdrs[$i]{blkno} == $v->{firstblk} + $R{curpos} + $i && $hdrs[$i]{volno} == $R{curvol}) ? 1 : 0;
	}

	# Where the XOR block is: a full group ends with it; a short one, cut short, may not have one
	my $hasxor = 0;
	my $xi = $n - 1;
	if ($R{grpsz} > 0)
	{
		if    ($n == $R{grpsz} + 1) { $hasxor = 1; }
		elsif ($ok[$xi])	   { $hasxor = ($hdrs[$xi]{typ} == BT_XOR) ? 1 : 0; }
		elsif ($n >= 2)		   { $hasxor = 1; }
		$ok[$xi] = 0 if $hasxor && $ok[$xi] && $hdrs[$xi]{typ} != BT_XOR;
	}
	my $gdata = $n - $hasxor;
	my ($nbad, $badi) = (0, 0);
	for my $i (0 .. $gdata - 1)
	{
		$ok[$i] = 0 if $ok[$i] && $hdrs[$i]{typ} != BT_DATA;
		if (!$ok[$i])
		{
			$nbad++;
			$badi = $i;
		}
	}

	# One bad DATA block: its payload is the XOR of all the others, two header fields kept by the next block
	if ($nbad == 1 && $hasxor && $ok[$xi] && $hdrs[$xi]{gindex} == $gdata && $badi + 1 < $n && $hdrs[$badi + 1])
	{
		my $d = substr(${ $blks[$xi] }, HDRSZ);
		for my $i (0 .. $gdata - 1)
		{
			next if $i == $badi;
			$d ^= substr(${ $blks[$i] }, HDRSZ);
		}
		my $s = $hdrs[$badi + 1];
		my %h = (typ => BT_DATA, recoff => $s->{prvrecoff}, paylen => $s->{prvpaylen},
			 blkno => $v->{firstblk} + $R{curpos} + $badi);
		if ($h{paylen} <= $R{bsize} - HDRSZ && ($h{recoff} == NONE || $h{recoff} < $h{paylen}))
		{
			my $blk = substr(${ $blks[$badi] }, 0, HDRSZ) . $d;
			$blks[$badi] = \$blk;
			$hdrs[$badi] = \%h;
			$ok[$badi] = 1;
			msg('block %d of volume %d was bad and has been repaired', $h{blkno}, $R{curvol});
		}
	}

	my (@pays, @recoffs, @bnos);
	for my $i (0 .. $gdata - 1)
	{
		my $b = $v->{firstblk} + $R{curpos} + $i;
		if (!$ok[$i])
		{
			msg('block %d of volume %d is bad and cannot be repaired', $b, $R{curvol});
			$bad = 1;
			push @pays, undef;
			push @recoffs, NONE;
		}
		else
		{
			push @pays, substr(${ $blks[$i] }, HDRSZ, $hdrs[$i]{paylen});
			push @recoffs, $hdrs[$i]{recoff};
		}
		push @bnos, $b;
	}
	@R{qw(pays recoffs blks next gvol)} = (\@pays, \@recoffs, \@bnos, 0, $R{curvol});
	$R{curpos} += $n;
	return 1;
}

#
#  Make the payload of the next good DATA block the current one.
#  0 - the stream goes on in it; 1 - blocks were lost before it, it is
#  taken from its first record header (RECOFF); 2 - the end.
#
sub next_pay
{
	for (;;)
	{
		if ($R{next} >= @{ $R{pays} })
		{
			return 2 unless load_group();
			next;
		}
		my $i = $R{next}++;
		if (!defined($R{pays}[$i]))
		{
			$R{gap} = 1;
			next;
		}
		@R{qw(pay payoff payblk payvol)} = ($R{pays}[$i], 0, $R{blks}[$i], $R{gvol});
		return 0 unless $R{gap};

		# After a loss: a block in which no record begins is of no use
		my $ro = $R{recoffs}[$i];
		next if $ro == NONE || $ro >= length($R{pays}[$i]);
		$R{payoff} = $ro;
		$R{gap} = 0;
		return 1;
	}
}

#
#  The next record of the stream (section 5): (type, body), or () - the
#  end.  $R{resync}: blocks were lost before it.
#
sub next_record
{
	my $resync = 0;
	for (;;)
	{
		while (!defined($R{pay}) || $R{payoff} >= length($R{pay}))
		{
			my $st = next_pay();
			if ($st == 1) { $resync = 1; }
			elsif ($st == 2)
			{
				$R{resync} = $resync;
				return ();
			}
		}

		# A record header is never split over two blocks
		my ($typ, $len) = (0, MAXREC + 1);
		if (length($R{pay}) - $R{payoff} >= 8)
		{
			$typ = u16(\$R{pay}, $R{payoff});
			$len = u32(\$R{pay}, $R{payoff} + 4);
		}
		if ($typ == 0 || $len > MAXREC)
		{
			msg('a bad record in block %d of volume %d', $R{payblk}, $R{payvol});
			$bad = 1;
			$R{gap} = 1;
			$R{pay} = undef;
			next;
		}
		$R{payoff} += 8;

		my ($body, $st) = ('', 0);
		while (length($body) < $len)
		{
			if ($R{payoff} >= length($R{pay}))
			{
				$st = next_pay();
				last if $st != 0;
			}
			my $want = $len - length($body);
			my $n = length($R{pay}) - $R{payoff};
			$n = $want if $n > $want;
			$body .= substr($R{pay}, $R{payoff}, $n);
			$R{payoff} += $n;
		}
		if ($st == 1)
		{
			# Lost in the middle of the body: dropped, a new header is at hand
			$resync = 1;
			next;
		}
		if ($st == 2)
		{
			msg('the saveset ends inside a record');
			$bad = 1;
			$R{resync} = 1;
			return ();
		}
		$R{resync} = $resync;
		return ($typ, $body);
	}
}

# A stored name that is safe to use: relative, no "", "." or ".." component, no NUL
sub name_ok
{
	my ($name) = @_;
	return 0 if !length($name) || substr($name, 0, 1) eq '/' || index($name, "\0") >= 0;
	for my $c (split(m{/}, $name, -1))
	{
		return 0 if $c eq '' || $c eq '.' || $c eq '..';
	}
	return 1;
}

# The directories a name lies in, below OUT, made if CREATE; a symbolic link on the way is refused
sub parents
{
	my ($out, $name, $create) = @_;
	my @comps = split(m{/}, $name, -1);
	pop @comps;
	my $p = $out;
	for my $c (@comps)
	{
		$p .= "/$c";
		if (lstat($p))
		{
			return 'a directory on the way is a link, or no directory' unless -d _ && !-l _;
			next;
		}
		return "$!" unless $create;
		mkdir($p, 0700) or return "$!";
	}
	return undef;
}

#
#  utimensat(2), for times to the nanosecond and for a symbolic link
#  itself; its number for the machines known.  Elsewhere utime(), to the
#  second, and a symbolic link keeps its own times.
#
my %UTIMENSAT = (x86_64 => 280, aarch64 => 88, riscv64 => 88, loongarch64 => 88, ppc64 => 304, ppc64le => 304,
		 s390x => 315, i386 => 320, i486 => 320, i586 => 320, i686 => 320, armv7l => 348, armv6l => 348);
my $SYS_UTIMENSAT = $UTIMENSAT{ (POSIX::uname())[4] };

sub set_times
{
	my ($path, $e, $nofollow) = @_;
	if (defined($SYS_UTIMENSAT))
	{
		my $ts = pack('l! l! l! l!', $e->{asec}, $e->{ansec}, $e->{msec}, $e->{mnsec});
		my $rc = syscall($SYS_UTIMENSAT, -100, $path, $ts, $nofollow ? 0x100 : 0);	# AT_FDCWD, AT_SYMLINK_NOFOLLOW
		return if $rc == 0;
	}
	utime($e->{asec}, $e->{msec}, $path) unless $nofollow;
}

#
#  The files of the stream being put back (x), or only checked (t)
#
my %X = (
	out	=> '.',
	make	=> 0,
	e	=> undef,
	fh	=> undef,
	path	=> '',
	active	=> 0,
	crc	=> 0,
	damaged	=> 0,
	seen	=> {},		# FILENOs met: looked up, never walked
	dirs	=> [],		# in the order they came
	catseen	=> 0,
	cathole	=> 0,
);

sub x_begin
{
	my ($body) = @_;
	@X{qw(active fh crc damaged)} = (0, undef, 0, 0);
	my $e = parse_entry($body);
	if (!$e)
	{
		msg('a FILE record that makes no sense is skipped');
		$bad = 1;
		return;
	}
	$X{e} = $e;
	$X{seen}{ $e->{fileno} } = 1;
	if (!$X{make})
	{
		$X{active} = ($e->{ftype} == FT_REG) ? 1 : 0;
		return;
	}
	my $name = $e->{path};
	if (!name_ok($name))
	{
		msg('%s was not extracted: a name that leads out of the output directory', $name);
		$bad = 1;
		return;
	}
	my $err = parents($X{out}, $name, 1);
	if (defined($err))
	{
		msg('%s was not extracted: %s', $name, $err);
		$bad = 1;
		return;
	}
	my $path = "$X{out}/$name";
	if ($e->{ftype} == FT_DIR)
	{
		if (!mkdir($path, 0700) && !(lstat($path) && -d _ && !-l _))
		{
			msg('%s was not extracted: %s', $name, "$!");
			$bad = 1;
			return;
		}
		push @{ $X{dirs} }, $e;
		return;
	}
	if (lstat($path))
	{
		msg('%s was not extracted: it exists, and is never overwritten', $name);
		$bad = 1;
		return;
	}
	if ($e->{ftype} == FT_REG)
	{
		my $fh;
		if (sysopen($fh, $path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600))
		{
			binmode($fh);
			@X{qw(fh active path)} = ($fh, 1, $path);
		}
		else { $err = "$!"; }
	}
	elsif ($e->{ftype} == FT_SYMLINK)
	{
		if (symlink($e->{link}, $path)) { set_times($path, $e, 1); }
		else				{ $err = "$!"; }
	}
	elsif ($e->{ftype} == FT_HARDLINK)
	{
		if (!name_ok($e->{link})) { $err = 'a link that leads out of the output directory'; }
		elsif (!defined($err = parents($X{out}, $e->{link}, 0)))
		{
			$err = "$!" unless link("$X{out}/$e->{link}", $path);
		}
	}
	elsif ($e->{ftype} == FT_FIFO)
	{
		if (POSIX::mkfifo($path, 0600))
		{
			chmod($e->{mode} & 07777, $path);
			set_times($path, $e, 0);
		}
		else { $err = "$!"; }
	}
	else
	{
		return;		# devices and sockets are not made
	}
	if (defined($err))
	{
		msg('%s was not extracted: %s', $name, $err);
		$bad = 1;
	}
}

# A DATA record: u32 fileno, u32 0, u64 offset, the bytes
sub x_data
{
	my ($body) = @_;
	return unless $X{active} && length($body) >= 16 && u32(\$body, 0) == $X{e}{fileno};
	my $off = u64(\$body, 8);
	my $d = substr($body, 16);
	$X{crc} = crc($X{crc}, $d);
	return unless defined($X{fh});
	if ($off > 2**53)
	{
		$X{damaged} = 1;
		return;
	}
	my $ok = sysseek($X{fh}, $off, SEEK_SET);
	my $done = 0;
	while ($ok && $done < length($d))
	{
		my $n = syswrite($X{fh}, $d, length($d) - $done, $done);
		$ok = 0 unless $n;
		$done += $n if $n;
	}
	if (!$ok)
	{
		msg('%s: %s', $X{e}{path}, "$!");
		$X{damaged} = 1;
	}
}

# The end of a regular file: size, checksum, attributes; body undef - it ends without its FEND
sub x_end
{
	my ($body) = @_;
	return unless $X{active};
	$X{active} = 0;
	my $e = $X{e};
	my ($fileno, $crc, $status, $size, $hascrc) = (0, 0, 0, $e->{size}, 0);
	if (defined($body))
	{
		my ($items) = tlv($body);
		for my $it (@$items)
		{
			my ($tag, $v) = @$it;
			if    ($tag == 1)  { $fileno = getu($v); }
			elsif ($tag == 9)  { $size   = getu($v); }
			elsif ($tag == 32) { $crc    = getu($v); $hascrc = 1; }
			elsif ($tag == 33) { $status = getu($v); }
		}
	}
	if (!defined($body) || $fileno != $e->{fileno})
	{
		$X{damaged} = 1;
	}
	elsif ($hascrc && $crc != $X{crc})
	{
		msg('%s: the checksum does not match', $e->{path});
		$X{damaged} = 1;
	}
	if ($X{damaged})
	{
		msg('%s is incomplete: its data was lost in bad blocks', $e->{path});
		$bad = 1;
	}
	elsif ($status == FS_CHANGED) { msg('%s changed while it was saved: the copy may be a mix', $e->{path}); }
	elsif ($status == FS_READERR) { msg('%s could not be read whole when it was saved', $e->{path}); }

	return unless defined($X{fh});
	truncate($X{fh}, $size) if $size <= 2**53;
	if (!close($X{fh}))
	{
		msg('%s: %s', $e->{path}, "$!");
		$bad = 1;
	}
	$X{fh} = undef;
	chmod($e->{mode} & 07777, $X{path});
	set_times($X{path}, $e, 0);
}

# A CATALOG record (section 6.4): the files never met in the stream are named
sub x_catalog
{
	my ($body) = @_;
	$X{catseen} = 1;
	my ($off, $len) = (0, length($body));
	while ($off + 4 <= $len)
	{
		my $elen = u32(\$body, $off);
		if ($elen > $len - $off - 4)
		{
			$X{cathole} = 1;
			last;
		}
		my $e = parse_entry(substr($body, $off + 4, $elen));
		if ($e && $e->{status} != FS_PRESENT && !$X{seen}{ $e->{fileno} })
		{
			msg('%s was not extracted: its records were lost in bad blocks', $e->{path});
			$bad = 1;
		}
		$off += 4 + $elen;
	}
}

# The modes and times of the directories, deepest first: a file made in one changes its times
sub finish_dirs
{
	for my $e (reverse @{ $X{dirs} })
	{
		next if defined(parents($X{out}, $e->{path}, 0));
		my $path = "$X{out}/$e->{path}";
		next unless lstat($path) && -d _ && !-l _;
		chmod($e->{mode} & 07777, $path);
		set_times($path, $e, 0);
	}
}

sub run
{
	my $ended = 0;
	while (!$ended)
	{
		my ($typ, $body) = next_record();
		if ($R{resync})
		{
			$X{damaged} = 1 if $X{active};
			$X{cathole} = 1 if $X{catseen};
		}
		last unless defined($typ);
		if ($typ == RT_FILE)
		{
			x_end(undef) if $X{active};
			x_begin($body);
		}
		elsif ($typ == RT_DATA) { x_data($body); }
		elsif ($typ == RT_FEND) { x_end($body); }
		elsif ($typ == RT_CATALOG)
		{
			x_end(undef) if $X{active};
			x_catalog($body);
		}
		elsif ($typ == RT_END) { $ended = 1; }
	}
	x_end(undef) if $X{active};

	# Blocks were lost, and the catalog could not tell every name
	msg('%s: files missing from the output cannot all be named', $R{spec}) if $bad && (!$X{catseen} || $X{cathole} || !$ended);
}

sub list
{
	for (;;)
	{
		my ($typ, $body) = next_record();
		return if !defined($typ) || $typ == RT_CATALOG || $typ == RT_END;
		next unless $typ == RT_FILE;
		my $e = parse_entry($body) or next;
		# A time out of the years 1..9999 is shown as the start of 1970: gmtime has no year for it
		my $sec = ($e->{msec} >= -62135596800 && $e->{msec} <= 253402300799) ? $e->{msec} : 0;
		my @t = gmtime($sec);
		my $tc = ($e->{ftype} <= 8) ? substr('?-dlhcbps', $e->{ftype}, 1) : '?';
		my $line = sprintf('%04d-%02d-%02d %02d:%02d:%02d %12.0f %s%04o %s', $t[5] + 1900, $t[4] + 1, $t[3], $t[2], $t[1], $t[0],
			$e->{size}, $tc, $e->{mode} & 07777, $e->{path});
		if    ($e->{ftype} == FT_SYMLINK)  { $line .= " -> $e->{link}"; }
		elsif ($e->{ftype} == FT_HARDLINK) { $line .= " link to $e->{link}"; }
		print "$line\n";
	}
}

sub usage
{
	print STDERR "vbkx-pl X01-03 - the extractor of last resort for VBACKUP savesets\n\n",
		"  perl vbkx.pl l saveset              list the files (times in UTC)\n",
		"  perl vbkx.pl x saveset [-C dir]     extract them all\n",
		"  perl vbkx.pl t saveset              read it all, check the checksums\n\n",
		"Completion: 0 - done; 1 - something damaged or not done; 2 - not usable.\n";
	return 2;
}

sub main
{
	my @args = @_;
	return usage() if @args < 2 || $args[0] !~ /\A[lxt]\z/;
	my ($op, $spec, $out) = ($args[0], $args[1], '.');
	for (my $i = 2; $i < @args; $i++)
	{
		if ($args[$i] eq '-C' && $i + 1 < @args && $op eq 'x') { $out = $args[++$i]; }
		else							{ return usage(); }
	}
	my $err = open_saveset($spec);
	if (defined($err))
	{
		msg('%s', $err);
		return 2;
	}
	if ($op eq 'l')
	{
		list();
	}
	elsif ($op eq 'x')
	{
		if (!mkdir($out, 0755) && !-d $out)
		{
			msg('%s: %s', $out, "$!");
			return 2;
		}
		umask(0);
		@X{qw(out make)} = ($out, 1);
		run();
		finish_dirs();
	}
	else
	{
		run();
		print "$spec: all files read, all checksums match\n" unless $bad;
	}
	return $bad ? 1 : 0;
}

# The last line of defence: whatever slipped through is a message, not a trace
my $rc = eval { main(@ARGV) };
if (!defined($rc))
{
	my $why = $@ || 'unknown';
	$why =~ s/\s+\z//;
	msg('internal error: %s', $why);
	$rc = 2;
}
exit($rc);
