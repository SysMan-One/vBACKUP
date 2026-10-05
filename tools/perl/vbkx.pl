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
#		reaches), and the 32-bit words of the ciphers are summed
#		modulo 2**32 and shifted masked.  Digest::SHA (in the "perl"
#		package, not in perl-base) is used when it is there, for
#		speed only: without it the SHA-256 written here does the same
#		work, some hundred times slower (see ENCRYPTED); it is looked
#		for only when an encrypted saveset is opened, and
#		VBKXPL_PURE=1 has the code here used even when it is there.
#
#  USAGE:	perl vbkx.pl l saveset [-k file]           list the files
#		perl vbkx.pl x saveset [-C dir] [-k file]  extract them all into dir
#							   (default: the current one)
#		perl vbkx.pl t saveset [-k file]           read it all, check the checksums
#		perl vbkx.pl selftest                      check the primitives of
#							   encryption against the
#							   vectors of their standards
#
#		saveset is volume 1 (x.bck); volumes 2, 3, ... are looked
#		for beside it as x.bck.002, x.bck.003, ...
#
#		  $ perl vbkx.pl l /mnt/usb/home.bck
#		  $ perl vbkx.pl x /mnt/usb/home.bck -C /tmp/restore
#		  $ perl vbkx.pl t /mnt/usb/home.bck -k ~/.vbackup.key
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
#  DATA:	DATA records and DATAZ records (vbackup /DATA_FORMAT=COMPRESSED,
#		the LZ4 block format, format.md 6.7) alike; a DATAZ block is
#		decompressed under the same checks as everything else - a
#		length or an offset out of bounds makes it a bad record, and
#		its file is named incomplete.
#
#  ENCRYPTED:	a saveset of vbackup /ENCRYPT (format.md 6.10) is read with
#		its passphrase: the first line of the file of -k (without its
#		LF or CR LF; a file only its owner may read and write - else
#		"chmod 600 it"), else of the file VBACKUP_KEY_FILE names, else
#		asked for on the terminal without echo.  Its bytes are taken
#		as they are.  A wrong one is said and nothing is read or
#		written (completion 2).  Every block is checked by its TAG
#		(HMAC-SHA256) before it is decrypted (ChaCha20); a block whose
#		CRC is right and whose TAG is not was changed on purpose: it is
#		said ("... its authentication fails") and repaired from its
#		group like any bad block, or lost.  The keys come from PBKDF2
#		of KDFITER iterations (600000 by default): with Digest::SHA
#		that takes a second or two, without it some minutes.
#		selftest tries SHA-256, HMAC-SHA256, PBKDF2 and ChaCha20
#		against the vectors of FIPS 180-4, RFC 4231, RFC 7914 and
#		RFC 8439; the PBKDF2 vector of 80000 iterations only with
#		Digest::SHA (the SHA-256 here would take minutes).
#
#  VOLUMES:	a saveset of vbackup /PHYSICAL (format.md 6.8) holds one device:
#		it comes out as one sparse file, the image of that device
#		(sdb1); one of vbackup /IMAGE (6.9) comes out as the plain tree
#		of the files of the volume - making the file system again is
#		vbackup's business, not this one's.
#
#  DAMAGE:	every block is checked (CRC-32); one bad block in a group is
#		rebuilt from the group's XOR block; after a loss the stream
#		is picked up at the next good block (in an encrypted saveset
#		the TAG judges a block as much as its CRC does).  A file that lost data
#		is kept as far as it got and named: "File: <name> - is
#		incomplete".  A file whose records were lost entirely is named
#		from the catalog: "File: <name> - not extracted"; without a
#		readable catalog that is said once ("Saveset: <saveset> - files
#		missing from the output cannot all be named").  A
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
#		Messages go to the standard error, each beginning "vbkx-pl: ",
#		in the form of vbkx's: what it is about first, "Label: value"
#		("File: name, errno: n"), then " - " and the words.
#
#  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#  CREATION DATE:  4-OCT-2026
#
#  MODIFICATION HISTORY:
#
#	X01-08		 5-OCT-2026	RRL
#		The messages in the form of vbkx's and VBACKUP's: "Label: value"
#		first, then " - " and the words; a system error as "errno: N -
#		words (text)".
#
#	X01-06		 5-OCT-2026	RRL
#		Encrypted savesets (format.md 6.10): -k keyfile, VBACKUP_KEY_FILE
#		or the terminal; EDATA blocks checked by their TAG, repaired,
#		decrypted; the ETRAILER checked.  SHA-256, HMAC, PBKDF2 and
#		ChaCha20 in Perl (Digest::SHA when there); selftest.
#
#	X01-04		 4-OCT-2026	RRL
#		DATAZ: the data compressed in the LZ4 block format.
#		/PHYSICAL and /IMAGE savesets said in the manual above.
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
	BT_EDATA => 5, BT_ETRAILER => 6,	# DATA and TRAILER of an encrypted saveset: format.md 6.10
	TAGSZ	=> 32,			# the TAG at the end of their payload area
	KDFMIN	=> 1000,		# the fewest PBKDF2 iterations taken
	PASSMAX	=> 1024,		# the longest passphrase
	RT_SUMMARY => 1, RT_FILE => 2, RT_DATA => 3, RT_FEND => 4, RT_CATALOG => 5, RT_END => 6,
	RT_DATAZ => 7,			# DATA, compressed: format.md 6.7
	MAXDATA => 1048576,		# the most octets a DATA or DATAZ record holds
	CODEC_LZ4 => 1,
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
#  The primitives of an encrypted saveset (6.10): SHA-256 (FIPS 180-4),
#  HMAC-SHA256 (RFC 2104), PBKDF2-HMAC-SHA256 (RFC 8018), ChaCha20 (RFC
#  8439).  SHA-256 is taken from Digest::SHA when the perl has it (the
#  "perl" package does, "perl-base" alone does not); else it is the one
#  written here, the same function, only slower.  ChaCha20 is always the
#  one here.  Words are 32 bits: every sum is taken modulo 2**32 (Perl's
#  "%", right on a 32-bit perl too) and every shift is masked, so the
#  result is the same on every machine.
#
my $HAVE_DS;			# Digest::SHA is there: looked for the first time it is wanted, never for a plain saveset
my $PURE    = ($ENV{VBKXPL_PURE} || '') eq '1' ? 1 : 0;	# 1 - the code here even when Digest::SHA is there

sub ds
{
	$HAVE_DS = (!$PURE && eval { require Digest::SHA; 1 }) ? 1 : 0 unless defined($HAVE_DS);
	return $HAVE_DS && !$PURE;
}

my @SHA_K = (
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2);
my @SHA_H0 = (0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19);

sub rotr { my ($x, $n) = @_; return (($x >> $n) | ($x << (32 - $n))) & 0xFFFFFFFF; }

# One 64-octet block into the state (8 words, changed in place)
sub sha_block
{
	my ($st, $blk) = @_;
	my @w = unpack('N16', $blk);
	for my $t (16 .. 63)
	{
		my ($x, $y) = ($w[$t - 15], $w[$t - 2]);
		my $s0 = rotr($x, 7) ^ rotr($x, 18) ^ ($x >> 3);
		my $s1 = rotr($y, 17) ^ rotr($y, 19) ^ ($y >> 10);
		$w[$t] = ($w[$t - 16] + $s0 + $w[$t - 7] + $s1) % 4294967296;
	}
	my ($a, $b, $c, $d, $e, $f, $g, $h) = @$st;
	for my $t (0 .. 63)
	{
		my $t1 = ($h + (rotr($e, 6) ^ rotr($e, 11) ^ rotr($e, 25)) + (($e & $f) ^ ((~$e & 0xFFFFFFFF) & $g))
			  + $SHA_K[$t] + $w[$t]) % 4294967296;
		my $t2 = ((rotr($a, 2) ^ rotr($a, 13) ^ rotr($a, 22)) + (($a & $b) ^ ($a & $c) ^ ($b & $c))) % 4294967296;
		($h, $g, $f, $e, $d, $c, $b, $a) = ($g, $f, $e, ($d + $t1) % 4294967296, $c, $b, $a, ($t1 + $t2) % 4294967296);
	}
	my @n = ($a, $b, $c, $d, $e, $f, $g, $h);
	$st->[$_] = ($st->[$_] + $n[$_]) % 4294967296 for 0 .. 7;
}

# The digest of DATA, the state ST (default: the initial one) having taken PRE octets before it
sub sha_finish
{
	my ($data, $st, $pre) = @_;
	my @s = $st ? @$st : @SHA_H0;
	my $bits = (($pre || 0) + length($data)) * 8;
	$data .= "\x80" . ("\0" x ((55 - length($data)) % 64)) . pack('N N', int($bits / 4294967296), $bits % 4294967296);
	sha_block(\@s, substr($data, $_ * 64, 64)) for 0 .. length($data) / 64 - 1;
	return pack('N8', @s);
}

sub sha256
{
	my ($data) = @_;
	return Digest::SHA::sha256($data) if ds();
	return sha_finish($data);
}

# An HMAC key prepared once: the key padded, the states after its inner and outer pads
sub hmac_key
{
	my ($key) = @_;
	$key = sha256($key) if length($key) > 64;
	$key .= "\0" x (64 - length($key));
	my @i = @SHA_H0;
	my @o = @SHA_H0;
	sha_block(\@i, $key ^ ("\x36" x 64));
	sha_block(\@o, $key ^ ("\x5c" x 64));
	return { key => $key, i => \@i, o => \@o };
}

sub hmac
{
	my ($k, $data) = @_;
	return Digest::SHA::hmac_sha256($data, $k->{key}) if ds();
	return sha_finish(sha_finish($data, $k->{i}, 64), $k->{o}, 64);
}

sub pbkdf2
{
	my ($pass, $salt, $iter, $dklen) = @_;
	my $k = hmac_key($pass);
	my $dk = '';
	for (my $blk = 1; length($dk) < $dklen; $blk++)
	{
		my $u = hmac($k, $salt . pack('N', $blk));
		my $t = $u;
		for (2 .. $iter)
		{
			$u = hmac($k, $u);
			$t ^= $u;
		}
		$dk .= $t;
	}
	return substr($dk, 0, $dklen);
}

# Equal or not, all the octets compared wherever they differ
sub same_bytes { my ($x, $y) = @_; return (length($x) == length($y) && ($x ^ $y) =~ /\A\0*\z/) ? 1 : 0; }

#
#  ChaCha20, RFC 8439 2.3 and 2.4: DATA XOR the key stream of KEY (32
#  octets) and NONCE (12) from block COUNTER on
#
my @QR = ([0, 4, 8, 12], [1, 5, 9, 13], [2, 6, 10, 14], [3, 7, 11, 15],
	  [0, 5, 10, 15], [1, 6, 11, 12], [2, 7, 8, 13], [3, 4, 9, 14]);

sub chacha20
{
	my ($key, $nonce, $ctr, $data) = @_;
	my @in = (0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, unpack('V8', $key), 0, unpack('V3', $nonce));
	my $ks = '';
	while (length($ks) < length($data))
	{
		$in[12] = $ctr % 4294967296;
		my @x = @in;
		for (1 .. 10)			# 20 rounds: a column round and a diagonal round, ten times
		{
			for my $q (@QR)
			{
				my ($a, $b, $c, $d) = @$q;
				$x[$a] = ($x[$a] + $x[$b]) % 4294967296; $x[$d] ^= $x[$a]; $x[$d] = (($x[$d] << 16) & 0xFFFFFFFF) | ($x[$d] >> 16);
				$x[$c] = ($x[$c] + $x[$d]) % 4294967296; $x[$b] ^= $x[$c]; $x[$b] = (($x[$b] << 12) & 0xFFFFFFFF) | ($x[$b] >> 20);
				$x[$a] = ($x[$a] + $x[$b]) % 4294967296; $x[$d] ^= $x[$a]; $x[$d] = (($x[$d] <<  8) & 0xFFFFFFFF) | ($x[$d] >> 24);
				$x[$c] = ($x[$c] + $x[$d]) % 4294967296; $x[$b] ^= $x[$c]; $x[$b] = (($x[$b] <<  7) & 0xFFFFFFFF) | ($x[$b] >> 25);
			}
		}
		$ks .= pack('V16', map { ($x[$_] + $in[$_]) % 4294967296 } 0 .. 15);
		$ctr++;
	}
	return $data ^ substr($ks, 0, length($data));
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
	return undef if $h{typ} < BT_DATA || $h{typ} > BT_ETRAILER || $h{paylen} > $psize;
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

	crypt	=> 0,		# encrypted (6.10): the VHDR has CIPHER
	cipher	=> 0,		# CIPHER, KDF, KDFITER, SALT, KEYCHECK of the VHDR
	kdf	=> 0,
	kdfiter	=> 0,
	salt	=> undef,
	keycheck => undef,
	dtype	=> BT_DATA,	# the type of a DATA block: EDATA when encrypted
	ttype	=> BT_TRAILER,	# ... of the TRAILER: ETRAILER
	cap	=> 0,		# the most a payload holds: P, or P - 32 for the TAG
	kenc	=> undef,	# the keys, once the passphrase is right
	kmac	=> undef,
	trlraw	=> undef,	# the ETRAILER block as it lies, until the keys check its TAG
	guessenc => 0,		# volume 1 guessed: its blocks are encrypted

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

#
#  The TAG of a block (6.10): HMAC-SHA256 under KMAC of the header fields
#  ssuuid, bsize, blkno, volno, type, gindex, recoff, paylen, then the
#  ciphertext.  Right only when PAYLEN leaves room for it and all its 32
#  octets match the last 32 of the payload area.
#
sub tag_ok
{
	my ($h, $b) = @_;
	my $psize = $R{bsize} - HDRSZ;
	return 0 if $psize < TAGSZ || $h->{paylen} > $psize - TAGSZ;
	my $m = $h->{uuid} . pack('V V V V C v V V', $h->{bsize}, $h->{blkno} % TWO32, int($h->{blkno} / TWO32),
		$h->{volno}, $h->{typ}, $h->{gindex}, $h->{recoff}, $h->{paylen});
	return same_bytes(hmac($R{kmac}, $m . substr($$b, HDRSZ, $h->{paylen})), substr($$b, HDRSZ + $psize - TAGSZ, TAGSZ));
}

# The plaintext of a block that passed its TAG; the nonce is u32 0, u64 blkno
sub decrypt
{
	my ($h, $b) = @_;
	my $nonce = pack('V V V', 0, $h->{blkno} % TWO32, int($h->{blkno} / TWO32));
	return chacha20($R{kenc}, $nonce, 0, substr($$b, HDRSZ, $h->{paylen}));
}

#
#  The passphrase: the keys derived from it and the SALT, judged by the
#  KEYCHECK of the VHDR (0 - it does not open the saveset); then the TAG
#  of the ETRAILER, when there is one
#
sub set_key
{
	my ($pass) = @_;
	my $mk = pbkdf2($pass, $R{salt}, $R{kdfiter}, 32);
	my $k = hmac_key($mk);
	return 0 unless same_bytes(hmac($k, 'VBACKUP CHECK'), $R{keycheck});
	$R{kenc} = hmac($k, 'VBACKUP ENC');
	$R{kmac} = hmac_key(hmac($k, 'VBACKUP MAC'));
	if (defined($R{trlraw}))
	{
		my $th = check($R{trlraw}, $R{bsize}, $R{uuid});
		if (!$th || !tag_ok($th, $R{trlraw}))
		{
			msg('Saveset: %s - its trailer fails its authentication: read as a saveset without a catalog', $R{spec});
			$bad = 1;
		}
	}
	return 1;
}

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

#
#  The group size, out of the SUMMARY record a VHDR carries; and, when it
#  has CIPHER, the encryption (6.10), the first time it is met
#
sub summary_group
{
	my ($b, $h) = @_;
	my $pay = substr($$b, HDRSZ, $h->{paylen});
	return undef unless length($pay) >= 8 && u16(\$pay, 0) == RT_SUMMARY;
	my $blen = u32(\$pay, 4);
	return undef if $blen > length($pay) - 8;
	my ($items) = tlv(substr($pay, 8, $blen));
	my ($grp, %c) = (0);
	for my $it (@$items)
	{
		my ($tag, $v) = @$it;
		if    ($tag == 71) { $grp = getu($v); }
		elsif ($tag == 88) { $c{cipher}	  = getu($v); }
		elsif ($tag == 89) { $c{kdf}	  = getu($v); }
		elsif ($tag == 90) { $c{kdfiter}  = getu($v); }
		elsif ($tag == 91) { $c{salt}	  = $v; }
		elsif ($tag == 92) { $c{keycheck} = $v; }
	}
	if (defined($c{cipher}) && !$R{crypt})
	{
		$R{crypt} = 1;
		$R{$_} = $c{$_} for grep { defined($c{$_}) } qw(cipher kdf kdfiter salt keycheck);
	}
	return ($grp <= MAXGRP) ? $grp : undef;
}

#
#  The encryption of the VHDR is one this extractor knows, with sizes that
#  make sense; else nothing of the saveset can be read
#
sub crypt_known
{
	return $R{cipher} == 1 && $R{kdf} == 1 && $R{kdfiter} >= KDFMIN && defined($R{salt}) && length($R{salt}) == 32
		&& defined($R{keycheck}) && length($R{keycheck}) == 32;
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
			$R{guessenc} = 1 if $h->{typ} == BT_EDATA || $h->{typ} == BT_ETRAILER;
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
			msg('Saveset: %s - its first block is bad: block size %d and group size %d found by trying', $R{spec}, $bs, $R{grpsz});
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
	sysopen($fh, $spec, O_RDONLY) or return sprintf("File: %s, errno: %d - cannot be opened (%s)", $spec, $! + 0, "$!");

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
		return "File: $spec - is not a saveset";
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
			summary_group(read_block($vf, $R{bsize}, 0), $h) unless $R{crypt};	# every VHDR has the keys
		}
		elsif (($h = check(read_block($vf, $R{bsize}, 1), $R{bsize}, $R{uuid})) && $h->{volno} == $n && $h->{blkno} > 0)
		{
			$first = $h->{blkno} - 1;
			msg('Volume: %d - its first block is bad, it is read all the same', $n);
		}
		else
		{
			msg('Volume: %d - belongs to another saveset, or is none', $n);
			close($vf);
			$miss++;
			next;
		}
		push @{ $R{vols} }, undef while @{ $R{vols} } < $n - 1;
		push @{ $R{vols} }, { fh => $vf, firstblk => $first, nblk => blocks_in($vf, $R{bsize}) };
		$miss = 0;
	}

	# Encrypted (6.10): DATA blocks are EDATA, the TRAILER an ETRAILER, a payload leaves room for the TAG
	return "Saveset: $spec - an encryption this extractor does not know: it cannot be read" if $R{crypt} && !crypt_known();
	return "Saveset: $spec - is encrypted, and no volume has a readable VHDR to give its keys" if $R{guessenc} && !$R{crypt};
	$R{dtype} = $R{crypt} ? BT_EDATA : BT_DATA;
	$R{ttype} = $R{crypt} ? BT_ETRAILER : BT_TRAILER;
	$R{cap}	  = $R{bsize} - HDRSZ - ($R{crypt} ? TAGSZ : 0);

	# The TRAILER, last block of the last volume: it is not part of the groups
	my $lv = $R{vols}[-1];
	if ($lv->{nblk} > 1)
	{
		my $tb = read_block($lv->{fh}, $R{bsize}, $lv->{nblk} - 1);
		my $th = check($tb, $R{bsize}, $R{uuid});
		if ($th && $th->{typ} == $R{ttype})
		{
			$R{trailer} = 1;
			$R{trlraw}  = $tb if $R{crypt};
		}
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
			msg('Volume: %d - is missing', $R{curvol});
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
		$ok[$i] = 0 if $ok[$i] && $hdrs[$i]{typ} != $R{dtype};

		# A good CRC and a wrong TAG: changed on purpose - a bad block all the same, repairable as any
		if ($ok[$i] && $R{crypt} && !tag_ok($hdrs[$i], $blks[$i]))
		{
			$ok[$i] = 0;
			msg('Block: %d, Volume: %d - is not what was written: its CRC is right, its authentication fails',
				$hdrs[$i]{blkno}, $R{curvol});
		}
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
		my %h = (typ => $R{dtype}, recoff => $s->{prvrecoff}, paylen => $s->{prvpaylen}, gindex => $badi,
			 blkno => $v->{firstblk} + $R{curpos} + $badi, volno => $R{curvol}, bsize => $R{bsize}, uuid => $R{uuid});
		my $blk = substr(${ $blks[$badi] }, 0, HDRSZ) . $d;
		# A repaired block of an encrypted saveset must pass its TAG too
		if ($h{paylen} <= $R{cap} && ($h{recoff} == NONE || $h{recoff} < $h{paylen}) && (!$R{crypt} || tag_ok(\%h, \$blk)))
		{
			$blks[$badi] = \$blk;
			$hdrs[$badi] = \%h;
			$ok[$badi] = 1;
			msg('Block: %d, Volume: %d - was bad, rebuilt from its group', $h{blkno}, $R{curvol});
		}
	}

	my (@pays, @recoffs, @bnos);
	for my $i (0 .. $gdata - 1)
	{
		my $b = $v->{firstblk} + $R{curpos} + $i;
		if (!$ok[$i])
		{
			msg('Block: %d, Volume: %d - is bad and cannot be rebuilt', $b, $R{curvol});
			$bad = 1;
			push @pays, undef;
			push @recoffs, NONE;
		}
		else
		{
			# Every check done, the repair too: only now is a block decrypted
			push @pays, $R{crypt} ? decrypt($hdrs[$i], $blks[$i]) : substr(${ $blks[$i] }, HDRSZ, $hdrs[$i]{paylen});
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
			msg('Block: %d, Volume: %d - an invalid record, skipped', $R{payblk}, $R{payvol});
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
			msg('Saveset: %s - ends inside a record', $R{spec});
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
			return [POSIX::ENOTDIR(), POSIX::strerror(POSIX::ENOTDIR())] unless -d _ && !-l _;
			next;
		}
		return [$! + 0, "$!"] unless $create;
		mkdir($p, 0700) or return [$! + 0, "$!"];
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
		msg('Record: FILE - makes no sense, skipped');
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
		msg('File: %s - its name leads out of the output directory, not extracted', $name);
		$bad = 1;
		return;
	}
	my $err = parents($X{out}, $name, 1);
	if (defined($err))
	{
		msg('File: %s, errno: %d - a directory on the way cannot be made, or is a link, not extracted (%s)', $name, @$err);
		$bad = 1;
		return;
	}
	my $path = "$X{out}/$name";
	if ($e->{ftype} == FT_DIR)
	{
		my @why;
		if (!mkdir($path, 0700) && (@why = ($! + 0, "$!")) && !(lstat($path) && -d _ && !-l _))
		{
			msg('File: %s, errno: %d - cannot be made, not extracted (%s)', $name, @why);
			$bad = 1;
			return;
		}
		push @{ $X{dirs} }, $e;
		return;
	}
	if (lstat($path))
	{
		msg('File: %s - already exists, not extracted (never overwritten)', $name);
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
		else { $err = [$! + 0, "$!"]; }
	}
	elsif ($e->{ftype} == FT_SYMLINK)
	{
		if (symlink($e->{link}, $path)) { set_times($path, $e, 1); }
		else				{ $err = [$! + 0, "$!"]; }
	}
	elsif ($e->{ftype} == FT_HARDLINK)
	{
		if (!name_ok($e->{link})) { $err = 'a link that leads out of the output directory, not extracted'; }
		elsif (!defined($err = parents($X{out}, $e->{link}, 0)))
		{
			$err = [$! + 0, "$!"] unless link("$X{out}/$e->{link}", $path);
		}
	}
	elsif ($e->{ftype} == FT_FIFO)
	{
		if (POSIX::mkfifo($path, 0600))
		{
			chmod($e->{mode} & 07777, $path);
			set_times($path, $e, 0);
		}
		else { $err = [$! + 0, "$!"]; }
	}
	else
	{
		return;		# devices and sockets are not made
	}
	if (defined($err))
	{
		if (ref($err)) { msg('File: %s, errno: %d - cannot be made, not extracted (%s)', $name, @$err); }
		else	       { msg('File: %s - %s', $name, $err); }
		$bad = 1;
	}
}

# lz4_decompress: the LZ4 block format (format.md 6.7) - exactly $rawlen
# octets out of exactly $src, or undef.  Every length is checked against
# what is left of the input and of the output, every offset against what
# has been written; nothing is taken on trust.
sub lz4_decompress
{
	my ($src, $rawlen) = @_;
	my ($n, $ip, $dst) = (length($src), 0, '');

	# A length beyond the nibble: octets added until one is below 255, never past $rawlen
	my $getlen = sub
	{
		my ($v) = @_;
		while (1)
		{
			return undef if $ip >= $n;
			my $b = ord(substr($src, $ip++, 1));
			$v += $b;
			return undef if $v > $rawlen;
			return $v if $b != 255;
		}
	};

	while (1)
	{
		return undef if $ip >= $n;
		my $tok = ord(substr($src, $ip++, 1));
		my $lit = $tok >> 4;
		if ($lit == 15)
		{
			$lit = $getlen->($lit);
			return undef unless defined($lit);
		}
		return undef if $lit > $n - $ip || $lit > $rawlen - length($dst);
		$dst .= substr($src, $ip, $lit);
		$ip += $lit;
		last if $ip == $n;			# the last sequence has its literals only
		return undef if $n - $ip < 2;
		my $off = ord(substr($src, $ip, 1)) | (ord(substr($src, $ip + 1, 1)) << 8);
		$ip += 2;
		return undef if $off == 0 || $off > length($dst);
		my $ml = $tok & 15;
		if ($ml == 15)
		{
			$ml = $getlen->($ml);
			return undef unless defined($ml);
		}
		$ml += 4;
		return undef if $ml > $rawlen - length($dst);
		# A match may overlap what it makes (a run): copied in pieces no longer than the offset
		while ($ml > 0)
		{
			my $k = ($ml < $off) ? $ml : $off;
			$dst .= substr($dst, length($dst) - $off, $k);
			$ml -= $k;
		}
	}
	return (length($dst) == $rawlen) ? $dst : undef;
}

# data_view: the file, the offset and the octets of a DATA or DATAZ record; () - a bad record
sub data_view
{
	my ($typ, $body) = @_;
	if ($typ == RT_DATA)
	{
		return () if length($body) < 16;
		return (u32(\$body, 0), u64(\$body, 8), substr($body, 16));
	}
	return () if length($body) < 20 || u32(\$body, 4) != CODEC_LZ4 || u32(\$body, 16) > MAXDATA;
	my $d = lz4_decompress(substr($body, 20), u32(\$body, 16));
	return () unless defined($d);
	return (u32(\$body, 0), u64(\$body, 8), $d);
}

# A DATA record (u32 fileno, u32 0, u64 offset, the bytes) or a DATAZ one
# (u32 fileno, u32 codec, u64 offset, u32 rawlen, LZ4 block); a DATAZ that
# does not decompress leaves the file incomplete
sub x_data
{
	my ($typ, $body) = @_;
	return unless $X{active};
	my ($fileno, $off, $d) = data_view($typ, $body);
	if (!defined($fileno))
	{
		msg('File: %s - a data record that makes no sense', $X{e}{path});
		$X{damaged} = 1;
		return;
	}
	return unless $fileno == $X{e}{fileno};
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
		msg('File: %s, errno: %d - cannot be written (%s)', $X{e}{path}, $! + 0, "$!");
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
		msg('File: %s - checksum mismatch: the data differ from what was saved', $e->{path});
		$X{damaged} = 1;
	}
	if ($X{damaged})
	{
		msg('File: %s - is incomplete: its data was lost in bad blocks', $e->{path});
		$bad = 1;
	}
	elsif ($status == FS_CHANGED) { msg('File: %s - changed while it was saved: the copy may be a mix', $e->{path}); }
	elsif ($status == FS_READERR) { msg('File: %s - could not be read whole when it was saved', $e->{path}); }

	return unless defined($X{fh});
	truncate($X{fh}, $size) if $size <= 2**53;
	if (!close($X{fh}))
	{
		msg('File: %s, errno: %d - cannot be written (%s)', $e->{path}, $! + 0, "$!");
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
			msg('File: %s - not extracted: its records were lost in bad blocks', $e->{path});
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
		elsif ($typ == RT_DATA || $typ == RT_DATAZ) { x_data($typ, $body); }
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
	msg('Saveset: %s - files missing from the output cannot all be named', $R{spec}) if $bad && (!$X{catseen} || $X{cathole} || !$ended);
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

#
#  The passphrase of an encrypted saveset: the first line of the key file
#  (-k, else VBACKUP_KEY_FILE) - one only its owner may read or write -
#  without its LF or CR LF; else asked for on the terminal without echo.
#  Its bytes as they are; undef - none to be had (said).
#
sub get_pass
{
	my ($keyfile, $spec) = @_;
	my $kf = defined($keyfile) ? $keyfile : $ENV{VBACKUP_KEY_FILE};
	my $line;
	if (defined($kf) && length($kf))
	{
		my $fh;
		if (!open($fh, '<:raw', $kf))
		{
			msg('Key file: %s, errno: %d - cannot be read (%s)', $kf, $! + 0, "$!");
			return undef;
		}
		my @st = stat($fh);
		if (!@st || !-f _ || ($st[2] & 077))
		{
			close($fh);
			msg('Key file: %s - not a regular file, or others may read it: chmod 600 it', $kf);
			return undef;
		}
		local $/ = "\n";
		$line = <$fh>;
		close($fh);
	}
	else
	{
		my $tty;
		my $t = POSIX::Termios->new();
		if (!open($tty, '+<:raw', '/dev/tty') || !defined($t->getattr(fileno($tty))))
		{
			msg('Saveset: %s - is encrypted, and there is no terminal to ask the passphrase on: give -k file', $spec);
			return undef;
		}
		my $save = $t->getlflag();
		my $restore = sub { $t->setlflag($save); $t->setattr(fileno($tty), POSIX::TCSAFLUSH()); };
		local $SIG{INT}  = sub { $restore->(); print $tty "\n"; exit(2); };
		local $SIG{TERM} = $SIG{INT};
		local $SIG{HUP}	 = $SIG{INT};
		my $old = select($tty); $| = 1; select($old);
		print $tty "Passphrase for $spec: ";
		$t->setlflag($save & ~(POSIX::ECHO() | POSIX::ECHOE() | POSIX::ECHOK() | POSIX::ECHONL()));
		$t->setattr(fileno($tty), POSIX::TCSAFLUSH());
		local $/ = "\n";
		$line = eval { scalar(<$tty>) };
		$restore->();
		print $tty "\n";
		close($tty);
	}
	$line = '' unless defined($line);
	$line =~ s/\n\z//;
	my $n = length($line);
	$n = PASSMAX + 2 if $n > PASSMAX + 2;			# what the reference vbkx keeps of a line
	$line = substr($line, 0, $n);
	$line =~ s/\r\z//;
	if (!length($line) || length($line) >= PASSMAX + 2)
	{
		msg('Passphrase: none - empty, or longer than %d bytes', PASSMAX + 1);
		return undef;
	}
	return $line;
}

#
#  The primitives against the vectors of their standards (those of
#  test/units.c): selftest.  The code of this file is tried always; the
#  SHA-256 of Digest::SHA too, when the perl has it.
#
sub selftest
{
	my $failed = 0;
	my $try = sub
	{
		my ($name, $got, $want) = @_;
		my $ok = (unpack('H*', $got) eq $want) ? 1 : 0;
		print 'selftest: ', $name, ': ', ($ok ? 'ok' : 'FAILED'), "\n";
		$failed++ unless $ok;
	};
	my $force = $PURE;
	$PURE = 0;
	my @ways = (!$force && ds()) ? ([1, ' (Perl)'], [0, ' (Digest::SHA)']) : ([1, '']);
	for my $w (@ways)
	{
		my $sfx;
		($PURE, $sfx) = @$w;
		$try->("SHA-256 \"\"$sfx", sha256(''), 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
		$try->("SHA-256 \"abc\"$sfx", sha256('abc'), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
		$try->("SHA-256 448 bits$sfx", sha256('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'),
			'248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1');
		$try->("SHA-256 a million 'a'$sfx", sha256('a' x 1000000), 'cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0');
		$try->("HMAC-SHA256 RFC 4231 case 1$sfx", hmac(hmac_key("\x0b" x 20), 'Hi There'),
			'b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7');
		$try->("HMAC-SHA256 RFC 4231 case 2$sfx", hmac(hmac_key('Jefe'), 'what do ya want for nothing?'),
			'5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843');
		$try->("HMAC-SHA256 RFC 4231 case 6$sfx", hmac(hmac_key("\xaa" x 131), 'Test Using Larger Than Block-Size Key - Hash Key First'),
			'60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54');
		$try->("PBKDF2-HMAC-SHA256 passwd/salt/1$sfx", pbkdf2('passwd', 'salt', 1, 64),
			'55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc'
			. '49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783');
		$try->("PBKDF2-HMAC-SHA256 password/salt/4096$sfx", pbkdf2('password', 'salt', 4096, 32),
			'c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a');
		# 80000 iterations: some seconds with Digest::SHA, minutes with the code here - so with Digest::SHA only
		$try->("PBKDF2-HMAC-SHA256 Password/NaCl/80000$sfx", pbkdf2('Password', 'NaCl', 80000, 64),
			'4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56'
			. 'a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d') unless $PURE;
	}
	$PURE = $force;
	my $key = pack('C*', 0 .. 31);
	my $nonce = pack('C*', 0, 0, 0, 0, 0, 0, 0, 0x4a, 0, 0, 0, 0);
	my $pt = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
	my $ct = chacha20($key, $nonce, 1, $pt);
	$try->('ChaCha20 RFC 8439 2.4.2', $ct, '6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b'
		. 'f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc'
		. '514d16ccf806818ce91ab77937365af90bbf74a35be6b40b8eedf2785e42874d');
	$try->('ChaCha20 decrypted back', chacha20($key, $nonce, 1, $ct), unpack('H*', $pt));
	if ($failed)
	{
		print "selftest: $failed failed\n";
		return 1;
	}
	print "selftest: all primitives right\n";
	return 0;
}

sub usage
{
	print STDERR "vbkx-pl X01-08 - the extractor of last resort for VBACKUP savesets\n\n",
		"  perl vbkx.pl l saveset [-k file]           list the files (times in UTC)\n",
		"  perl vbkx.pl x saveset [-C dir] [-k file]  extract them all\n",
		"  perl vbkx.pl t saveset [-k file]           read it all, check the checksums\n",
		"  perl vbkx.pl selftest                      check the primitives of encryption\n\n",
		"  -k file  the passphrase of an encrypted saveset: the first line of file\n",
		"           (else VBACKUP_KEY_FILE, else it is asked for on the terminal)\n\n",
		"Completion: 0 - done; 1 - something damaged or not done; 2 - not usable.\n";
	return 2;
}

sub main
{
	my @args = @_;
	return selftest() if @args == 1 && $args[0] eq 'selftest';
	return usage() if @args < 2 || $args[0] !~ /\A[lxt]\z/;
	my ($op, $spec, $out, $keyfile) = ($args[0], $args[1], '.', undef);
	for (my $i = 2; $i < @args; $i++)
	{
		if    ($args[$i] eq '-C' && $i + 1 < @args && $op eq 'x') { $out = $args[++$i]; }
		elsif ($args[$i] eq '-k' && $i + 1 < @args)		  { $keyfile = $args[++$i]; }
		else							  { return usage(); }
	}
	my $err = open_saveset($spec);
	if (defined($err))
	{
		msg('%s', $err);
		return 2;
	}

	# Encrypted: nothing of it is read, nothing is written, before the passphrase is right
	if ($R{crypt})
	{
		my $pass = get_pass($keyfile, $spec);
		return 2 unless defined($pass);
		if (!set_key($pass))
		{
			msg('Saveset: %s - the passphrase does not open it', $spec);
			return 2;
		}
	}
	if ($op eq 'l')
	{
		list();
	}
	elsif ($op eq 'x')
	{
		if (!mkdir($out, 0755) && !-d $out)
		{
			msg('Directory: %s, errno: %d - cannot be made or entered (%s)', $out, $! + 0, "$!");
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
	msg('Error: %s - internal error', $why);
	$rc = 2;
}
exit($rc);
