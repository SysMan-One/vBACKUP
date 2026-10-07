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
#		/LEVEL, format.md 6.7) alike, of the three codecs: 1 the LZ4
#		block format, 2 raw Deflate (RFC 1951; by Compress::Raw::Zlib
#		when the perl has it - the core since 5.10, not perl-base - and
#		VBKXPL_PURE is not 1, else by the inflate here), 3 raw LZMA1
#		(lc=3 lp=0 pb=2, the end marker; the decoder here, some
#		0.7 MB a second; the inflate here some 0.9).  A DATAZ block is decompressed under
#		the same checks as everything else - a length, an offset or a
#		distance out of bounds, a code set that is not one, octets
#		short or left over, an unknown codec, make it a bad record,
#		and its file is named incomplete.
#
#  SOLID:	a saveset of version 3 (vbackup /LEVEL or /DATA_FORMAT=COMPRESSED,
#		unless /NOSOLID; format.md 6.12) may hold SOLID records: the
#		FILE, DATA and FEND records of several small files compressed
#		together, by one of the three codecs above.  One is opened
#		and its records are taken as if they stood in the stream; in
#		version 1 and 2 a record of its type is unknown and skipped.
#		One that does not open (an unknown codec, a length out of
#		bounds, a stream that is not right) or holds a record that is
#		not FILE, DATA, FEND or does not end inside it is a gap, as
#		lost blocks are: its files are named from the catalog ("not
#		extracted"), or incomplete - never wrong octets.
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
#		rebuilt from the group's XOR block - any m of them in a saveset
#		of version 2 or 3 made /PARITY=m (format.md 4.1), from its XOR and
#		PARITY blocks by Reed-Solomon; after a loss the stream
#		is picked up at the next good block (in an encrypted saveset
#		the TAG judges a block as much as its CRC does).  A file that lost data
#		is kept as far as it got and named: "File: <name> - is
#		incomplete".  A block lost in a SOLID takes all the files of
#		that SOLID.  A file whose records were lost entirely is named
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
#	X01-22		 7-OCT-2026	RRL
#		The repair of a group: the good DATA blocks first made as the
#		writer left them past PAYLEN - zeros up to the TAG.  Nothing
#		authenticates those octets, the parity covers them: one byte
#		changed there in a good block, and the bad one of its group
#		could not be rebuilt.
#
#	X01-21		 7-OCT-2026	RRL
#		Version 3 (format.md 3, 6.12): its blocks taken as those of
#		version 2, its groups those of version 1 without PARITY 2 or
#		more in the SUMMARY; the SOLID record opened by any of the
#		three codecs, its FILE, DATA and FEND records returned as if
#		in the stream; one that does not open, or a record in it that
#		makes no sense, is a gap: its files named lost.
#
#	X01-19		 6-OCT-2026	RRL
#		DATAZ codecs 2 (raw Deflate: Compress::Raw::Zlib, else the
#		inflate here) and 3 (raw LZMA1, the decoder here), format.md
#		6.7.2 and 6.7.3; selftest of both, the inflate here too.
#
#	X01-16		 6-OCT-2026	RRL
#		The CRC by Compress::Raw::Zlib when the perl has it (the core
#		since 5.10), as Digest::SHA for SHA-256: a plain saveset is read
#		some 170 times faster; VBKXPL_PURE=1 keeps the code here.
#
#	X01-14		 5-OCT-2026	RRL
#		Version 2 (format.md 4.1): /PARITY=m, a group closed by its XOR
#		block and m - 1 PARITY blocks; any m bad blocks of a group
#		rebuilt by Reed-Solomon in GF(2^8), the header parity first,
#		surplus rows checked, a row whose CRC is right and whose bytes
#		are not passed over; selftest of it.
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
	BT_PARITY => 7,			# a parity row >= 1, version 2 and 3 only: format.md 4.1
	MAXPAR	=> 8,			# parity blocks of a group at most
	TAGSZ	=> 32,			# the TAG at the end of their payload area
	KDFMIN	=> 1000,		# the fewest PBKDF2 iterations taken
	PASSMAX	=> 1024,		# the longest passphrase
	RT_SUMMARY => 1, RT_FILE => 2, RT_DATA => 3, RT_FEND => 4, RT_CATALOG => 5, RT_END => 6,
	RT_DATAZ => 7,			# DATA, compressed: format.md 6.7
	RT_SOLID => 8,			# the records of several files compressed together, version 3: format.md 6.12
	SOLIDHDR => 12,			# codec, rawlen, count
	MAXSOLID => 1048576 + 65536,	# the most octets its records take, decompressed
	MAXDATA => 1048576,		# the most octets a DATA or DATAZ record holds
	CODEC_LZ4 => 1, CODEC_DEFLATE => 2, CODEC_LZMA => 3,
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

#  Compress::Raw::Zlib (in the perl core since 5.10) does the same in C, a
#  hundred times faster: taken when it is there and VBKXPL_PURE is not 1.
my $ZCRC;

sub crc
{
	my ($c, $d) = @_;

	unless ( defined $ZCRC )
	{
		$ZCRC = (($ENV{VBKXPL_PURE} || '') ne '1') && eval { require Compress::Raw::Zlib; 1 } ? 1 : 0;
	}

	return Compress::Raw::Zlib::crc32($d, $c) if $ZCRC;

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
	return undef unless substr($$b, 0, 4) eq 'VBKB' && u16($b, 4) == HDRSZ && u16($b, 6) >= 1 && u16($b, 6) <= 3;
	my %h = (
		version	  => u16($b, 6),
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
	return undef if $h{typ} < BT_DATA || $h{typ} > BT_PARITY || ($h{typ} == BT_PARITY && $h{version} < 2);
	# The parity blocks of version 2 and 3 carry the header parity in RECOFF and PAYLEN (4.1)
	if ($h{version} < 2 || ($h{typ} != BT_XOR && $h{typ} != BT_PARITY))
	{
		return undef if $h{paylen} > $psize;
		return undef if $h{recoff} != NONE && $h{recoff} >= $psize;
	}
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
	parity	=> 1,		# parity blocks of a group: 1 (version 1; 3 without PARITY 2 or more), 2 .. 8 (version 2 and 3, 4.1)
	version	=> 1,		# of every block: that of the VHDR of volume 1
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

	sol	=> undef,	# the records of the SOLID open (6.12), decompressed
	solpos	=> 0,		# ... the next one in it
	solblk	=> 0,		# ... where its header is
	solvol	=> 0,
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
		elsif ($tag == 93) { $c{parity}	  = getu($v); }
		elsif ($tag == 88) { $c{cipher}	  = getu($v); }
		elsif ($tag == 89) { $c{kdf}	  = getu($v); }
		elsif ($tag == 90) { $c{kdfiter}  = getu($v); }
		elsif ($tag == 91) { $c{salt}	  = $v; }
		elsif ($tag == 92) { $c{keycheck} = $v; }
	}
	$R{parity} = $c{parity} if defined($c{parity}) && !$R{gotpar}++;
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
			$R{version} = $h->{version};
			$R{grpsz} = 0;
			for my $p (1 .. MAXGRP + 1)
			{
				my $x = check(read_block($fh, $bs, $p), $bs, $R{uuid});
				if ($x && $x->{typ} == BT_XOR && ($x->{gindex} & 0xFF) == $p - 1 && ($x->{gindex} >> 8) == 0)
				{
					$R{grpsz} = $x->{gindex} & 0xFF;
					# Version 2 and 3: the parity blocks are the XOR block and the PARITY blocks after it
					if ($R{version} >= 2 && !$R{gotpar})
					{
						my $m = 1;
						while ($m < MAXPAR)
						{
							my $y = check(read_block($fh, $bs, $p + $m), $bs, $R{uuid});
							last unless $y && $y->{typ} == BT_PARITY && $y->{gindex} == ($R{grpsz} | ($m << 8));
							$m++;
						}
						$R{parity} = $m;
					}
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
				@R{qw(bsize uuid grpsz version)} = ($bs, $h->{uuid}, $grp, $h->{version});
				$found = 1;
			}
		}
	}
	if (!$found && !guess($fh))
	{
		close($fh);
		return "File: $spec - is not a saveset";
	}
	# Version 2: as many parity blocks a group as the SUMMARY says (4.1); version 1: the XOR block;
	# version 3: either - the XOR block alone without PARITY 2 or more (section 3)
	if ($R{version} == 1 || ($R{version} == 3 && $R{parity} < 2))
	{
		$R{parity} = 1;
	}
	elsif ($R{parity} < 2 || $R{parity} > MAXPAR || !$R{grpsz})
	{
		close($fh);
		return "File: $spec - is not a saveset: version $R{version} without a parity count it can use";
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
#  The good DATA blocks of a group as the writer made them past PAYLEN:
#  zeros up to the TAG.  What lies there carries nothing, no TAG covers
#  it; a byte changed there would make the parity disagree, and nothing
#  of the group be rebuilt.
#
sub canon
{
	my ($blks, $hdrs, $ok, $n) = @_;
	for my $i (0 .. $n - 1)
	{
		my $h = $hdrs->[$i];
		next unless $ok->[$i] && $h->{typ} == $R{dtype} && $h->{paylen} < $R{cap};
		substr(${ $blks->[$i] }, HDRSZ + $h->{paylen}, $R{cap} - $h->{paylen}) = "\0" x ($R{cap} - $h->{paylen});
	}
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
	my $n = ($R{grpsz} > 0) ? $R{grpsz} + $R{parity} : 1;
	$n = $end - $R{curpos} if $end - $R{curpos} < $n;

	my (@blks, @hdrs, @ok);
	for my $i (0 .. $n - 1)
	{
		$blks[$i] = read_block($v->{fh}, $R{bsize}, $R{curpos} + $i);
		$hdrs[$i] = check($blks[$i], $R{bsize}, $R{uuid});
		$ok[$i] = ($hdrs[$i] && $hdrs[$i]{blkno} == $v->{firstblk} + $R{curpos} + $i && $hdrs[$i]{volno} == $R{curvol}
			&& $hdrs[$i]{version} == $R{version}) ? 1 : 0;
	}

	# Version 2: groups of several parity blocks, repaired by Reed-Solomon
	return load_group2($v, $n, \@blks, \@hdrs, \@ok) if $R{grpsz} > 0 && $R{parity} > 1;

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

	canon(\@blks, \@hdrs, \@ok, $gdata) if $nbad;

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
#  Reed-Solomon of version 2 (format.md 4.1): GF(2^8), the polynomial
#  0x11D; a(j, i) = y_i / (j + y_i), y_i = 128 + i - row 0 all ones, the
#  XOR block.  A multiply-accumulate over a payload goes through a tr///
#  made once a coefficient: a lookup an octet, then the string XOR.
#
my (@GEXP, @GLOG, %GTR);
{
	my $x = 1;
	for my $i (0 .. 254)
	{
		$GEXP[$i] = $GEXP[$i + 255] = $x;
		$GLOG[$x] = $i;
		$x <<= 1;
		$x ^= 0x11D if $x & 0x100;
	}
}

sub gmul { my ($a, $b) = @_; return ($a && $b) ? $GEXP[$GLOG[$a] + $GLOG[$b]] : 0; }
sub ginv { return $GEXP[255 - $GLOG[$_[0]]]; }

sub rs_coef
{
	my ($j, $i) = @_;
	return 1 unless $j;
	my $y = 128 + $i;
	return gmul($y, ginv($j ^ $y));
}

# dst .= dst XOR c * src
sub rs_muladd
{
	my ($dst, $src, $c) = @_;
	return unless $c;
	if ($c == 1)
	{
		$$dst ^= $src;
		return;
	}
	$GTR{$c} ||= eval 'sub { (my $t = $_[0]) =~ tr/\x00-\xff/' . join('', map { sprintf('\\x%02x', gmul($c, $_)) } 0 .. 255) . '/; return $t; }'
		or die "vbkx-pl: tr: $@";
	$$dst ^= $GTR{$c}->($src);
}

#
#  The bad DATA vectors of a group rebuilt from its good parity rows:
#  'ok', 'err' (more bad than good rows, nothing touched) or 'warn' (a
#  row left over disagrees - not to be trusted).  $d, $p - arrays of
#  strings of one length; $dok, $pok - good or bad.
#
sub rs_repair
{
	my ($n, $m, $d, $dok, $p, $pok) = @_;
	my @lost = grep { !$dok->[$_] } 0 .. $n - 1;
	my @good = grep { $pok->[$_] } 0 .. $m - 1;
	my $e = @lost;
	return 'err' if $e > @good;
	my @rows = @good[0 .. $e - 1];
	my @extra = @good[$e .. $#good];
	return 'ok' unless $e || @extra;
	my $len = length($p->[$good[0]]);
	if ($e)
	{
		my (@syn, @mat);
		for my $k (0 .. $e - 1)
		{
			my $s = $p->[$rows[$k]];
			rs_muladd(\$s, $d->[$_], rs_coef($rows[$k], $_)) for grep { $dok->[$_] } 0 .. $n - 1;
			$syn[$k] = $s;
			$mat[$k] = [ map { rs_coef($rows[$k], $_) } @lost ];
		}
		# Gauss-Jordan of the e x e coefficients
		my @inv = map { my $r = $_; [ map { ($_ == $r) ? 1 : 0 } 0 .. $e - 1 ] } 0 .. $e - 1;
		for my $c (0 .. $e - 1)
		{
			my $pv = $c;
			$pv++ while $pv < $e && !$mat[$pv][$c];
			return 'err' if $pv == $e;
			@mat[$c, $pv] = @mat[$pv, $c];
			@inv[$c, $pv] = @inv[$pv, $c];
			my $f = ginv($mat[$c][$c]);
			$mat[$c] = [ map { gmul($_, $f) } @{ $mat[$c] } ];
			$inv[$c] = [ map { gmul($_, $f) } @{ $inv[$c] } ];
			for my $r (0 .. $e - 1)
			{
				next if $r == $c;
				my $g = $mat[$r][$c];
				next unless $g;
				$mat[$r][$_] ^= gmul($g, $mat[$c][$_]) for 0 .. $e - 1;
				$inv[$r][$_] ^= gmul($g, $inv[$c][$_]) for 0 .. $e - 1;
			}
		}
		for my $t (0 .. $e - 1)
		{
			my $v = "\0" x $len;
			rs_muladd(\$v, $syn[$_], $inv[$t][$_]) for 0 .. $e - 1;
			$d->[$lost[$t]] = $v;
		}
	}
	# The rows left over, each made again from the whole group
	for my $x (@extra)
	{
		my $v = "\0" x $len;
		rs_muladd(\$v, $d->[$_], rs_coef($x, $_)) for 0 .. $n - 1;
		return 'warn' if $v ne $p->[$x];
	}
	return 'ok';
}

#
#  The rest of load_group for version 2 (4.1): d DATA blocks and m parity
#  blocks; the repair of as many bad DATA blocks as there are good rows,
#  their header parity first, then their payloads; tried again without
#  each good row in turn when the result does not hold.
#
sub load_group2
{
	my ($v, $n, $blks, $hdrs, $ok) = @_;
	my $m = $R{parity};
	my $first = $v->{firstblk} + $R{curpos};
	my $d;

	# How many DATA blocks: any good parity block says it, standing at n + its row
	for my $i (0 .. $n - 1)
	{
		my $h = $hdrs->[$i];
		next unless $ok->[$i];
		my ($row, $cnt) = ($h->{gindex} >> 8, $h->{gindex} & 0xFF);
		if ((($h->{typ} == BT_XOR && !$row) || ($h->{typ} == BT_PARITY && $row && $row < $m)) && $cnt && $cnt <= $R{grpsz} && $cnt + $row == $i)
		{
			$d = $cnt;
			last;
		}
	}
	if (!defined($d))
	{
		if    ($n == $R{grpsz} + $m)				{ $d = $R{grpsz}; }
		elsif ($ok->[$n - 1] && $hdrs->[$n - 1]{typ} == $R{dtype}) { $d = $n; }
		else							{ $d = ($n > $m) ? $n - $m : $n; }
	}
	$d = $n if $d > $n;

	my (@pay, @hv, @pok, @ppay, @phv);
	for my $j (0 .. $m - 1)
	{
		my $i = $d + $j;
		my $h = ($i < $n) ? $hdrs->[$i] : undef;
		$pok[$j] = ($i < $n && $ok->[$i] && $h->{typ} == ($j ? BT_PARITY : BT_XOR) && $h->{gindex} == ($d | ($j << 8))) ? 1 : 0;
		$ppay[$j] = $pok[$j] ? substr(${ $blks->[$i] }, HDRSZ) : '';
		$phv[$j]  = $pok[$j] ? pack('V V', $h->{recoff}, $h->{paylen}) : "\0" x 8;
	}
	my $npok = grep { $_ } @pok;

	my @dok;
	my $nbad = 0;
	for my $i (0 .. $d - 1)
	{
		$ok->[$i] = 0 if $ok->[$i] && ($hdrs->[$i]{typ} != $R{dtype} || $hdrs->[$i]{gindex} != $i);
		# A good CRC and a wrong TAG: a bad block all the same
		if ($ok->[$i] && $R{crypt} && !tag_ok($hdrs->[$i], $blks->[$i]))
		{
			$ok->[$i] = 0;
			msg('Block: %d, Volume: %d - is not what was written: its CRC is right, its authentication fails',
				$hdrs->[$i]{blkno}, $R{curvol});
		}
		$dok[$i] = $ok->[$i];
		$hv[$i]  = $dok[$i] ? pack('V V', $hdrs->[$i]{recoff}, $hdrs->[$i]{paylen}) : "\0" x 8;
		$nbad++ unless $dok[$i];
	}
	canon($blks, $hdrs, $ok, $d) if $nbad;
	$pay[$_] = $dok[$_] ? substr(${ $blks->[$_] }, HDRSZ) : '' for 0 .. $d - 1;

	my ($done, $forged) = (0, 0);
	for (my $skip = -1; $nbad && $nbad <= $npok && !$done && $skip < $m; $skip++)
	{
		next if $skip >= 0 && !$pok[$skip];
		my @try = @pok;
		$try[$skip] = 0 if $skip >= 0;
		next if (grep { $_ } @try) < $nbad;
		my @thv = @hv;
		my @tpay = @pay;
		my $rc = rs_repair($d, $m, \@thv, \@dok, \@phv, \@try);
		$rc = rs_repair($d, $m, \@tpay, \@dok, \@ppay, \@try) if $rc eq 'ok';
		$forged = 1 if $rc eq 'warn';
		next unless $rc eq 'ok';

		my (@nh, @nb);
		my $allok = 1;
		for my $i (0 .. $d - 1)
		{
			if ($dok[$i])
			{
				$nh[$i] = $hdrs->[$i];
				next;
			}
			my ($ro, $pl) = unpack('V V', $thv[$i]);
			my %h = (version => $R{version}, typ => $R{dtype}, recoff => $ro, paylen => $pl, gindex => $i, blkno => $first + $i,
				 volno => $R{curvol}, bsize => $R{bsize}, uuid => $R{uuid},
				 prvrecoff => $i ? $nh[$i - 1]{recoff} : NONE, prvpaylen => $i ? $nh[$i - 1]{paylen} : 0);
			my $blk = substr(${ $blks->[$i] }, 0, HDRSZ) . $tpay[$i];
			if ($pl > $R{cap} || ($ro != NONE && $ro >= $pl) || ($R{crypt} && !tag_ok(\%h, \$blk)))
			{
				$allok = 0;
				last;
			}
			$nh[$i] = \%h;
			$nb[$i] = \$blk;
		}
		next unless $allok;
		$done = 1;
		for my $i (0 .. $d - 1)
		{
			next if $dok[$i];
			($blks->[$i], $hdrs->[$i], $ok->[$i]) = ($nb[$i], $nh[$i], 1);
			msg('Block: %d, Volume: %d - was bad, rebuilt from its group', $nh[$i]{blkno}, $R{curvol});
		}
	}
	if ($nbad && !$done && $forged)
	{
		msg('Block: %d, Volume: %d - the group beginning here does not agree with its parity: nothing of it is rebuilt', $first, $R{curvol});
		$bad = 1;
	}

	my (@pays, @recoffs, @bnos);
	for my $i (0 .. $d - 1)
	{
		my $b = $first + $i;
		if (!$ok->[$i])
		{
			msg('Block: %d, Volume: %d - is bad and cannot be rebuilt', $b, $R{curvol});
			$bad = 1;
			push @pays, undef;
			push @recoffs, NONE;
		}
		else
		{
			push @pays, $R{crypt} ? decrypt($hdrs->[$i], $blks->[$i]) : substr(${ $blks->[$i] }, HDRSZ, $hdrs->[$i]{paylen});
			push @recoffs, $hdrs->[$i]{recoff};
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
#  A SOLID record (6.12) opened: codec, rawlen, count, the compressed
#  octets; its records, decompressed, kept to be returned one by one.
#  0 - it does not open: an unknown codec, rawlen out of bounds, a stream
#  that is not right.
#
sub sol_open
{
	my ($body) = @_;
	$R{sol} = undef;
	return 0 if length($body) < SOLIDHDR;
	my ($codec, $raw) = (u32(\$body, 0), u32(\$body, 4));
	return 0 if $raw < 8 || $raw > MAXSOLID;
	my $d = unpack_data($codec, substr($body, SOLIDHDR), $raw);
	return 0 unless defined($d);
	@R{qw(sol solpos)} = ($d, 0);
	return 1;
}

#
#  The next record of the open SOLID: FILE, DATA or FEND only, wholly in
#  it; () - anything else, and the rest of it is dropped.  The last ends
#  exactly at its end: then the SOLID is closed.
#
sub sol_next
{
	my $left = length($R{sol}) - $R{solpos};
	my ($typ, $len) = (u16(\$R{sol}, $R{solpos}), u32(\$R{sol}, $R{solpos} + 4));
	if ($left < 8 || ($typ != RT_FILE && $typ != RT_DATA && $typ != RT_FEND) || $len > $left - 8)
	{
		$R{sol} = undef;
		return ();
	}
	my $body = substr($R{sol}, $R{solpos} + 8, $len);
	$R{solpos} += 8 + $len;
	$R{sol} = undef if $R{solpos} >= length($R{sol});
	return ($typ, $body);
}

#
#  The next record of the stream (section 5): (type, body), or () - the
#  end.  $R{resync}: blocks were lost before it.  Version 3: the records
#  of a SOLID are returned as if they stood in the stream; a SOLID that
#  does not open, or a record in it that makes no sense, is a gap - the
#  blocks are all there, only what it held is lost (6.12).
#
sub next_record
{
	my $resync = 0;
	for (;;)
	{
		if (defined($R{sol}))
		{
			my ($typ, $body) = sol_next();
			if (defined($typ))
			{
				$R{resync} = $resync;
				return ($typ, $body);
			}
			msg('Block: %d, Volume: %d - a SOLID record that makes no sense: the rest of it is lost', $R{solblk}, $R{solvol});
			$bad = 1;
			$resync = 1;
		}

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
		my ($hblk, $hvol) = @R{qw(payblk payvol)};
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
		if ($typ == RT_SOLID && $R{version} == 3)
		{
			@R{qw(solblk solvol)} = ($hblk, $hvol);
			next if sol_open($body);
			# It does not open: its files are named lost from the catalog
			msg('Block: %d, Volume: %d - a SOLID record that does not open: the files in it are lost', $hblk, $hvol);
			$bad = 1;
			$resync = 1;
			next;
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

#
#  Codec 2: raw Deflate (RFC 1951, format.md 6.7.2).  By Compress::Raw::Zlib
#  when it is there and VBKXPL_PURE is not 1 - window bits -15, the output
#  limited, so a stream that would give more than $rawlen octets is cut
#  off, not followed; else by the inflate below.  Either way: exactly
#  $rawlen octets, the final block ending in the last octet, or undef.
#
my $ZINF;

sub inflate
{
	my ($src, $rawlen) = @_;
	unless (defined $ZINF)
	{
		$ZINF = (($ENV{VBKXPL_PURE} || '') ne '1') && eval { require Compress::Raw::Zlib; 1 } ? 1 : 0;
	}
	return inflate_pure($src, $rawlen) unless $ZINF;
	my ($z, $st) = eval { Compress::Raw::Zlib::Inflate->new(-WindowBits => -15, -LimitOutput => 1, -Bufsize => 65536,
		-AppendOutput => 1, -ConsumeInput => 1) };
	return inflate_pure($src, $rawlen) unless $z && $st == Compress::Raw::Zlib::Z_OK();
	my ($in, $out) = ($src, '');
	while (1)
	{
		my ($li, $lo) = (length($in), length($out));
		$st = $z->inflate($in, $out);
		return undef if length($out) > $rawlen;
		last if $st == Compress::Raw::Zlib::Z_STREAM_END();
		return undef unless $st == Compress::Raw::Zlib::Z_OK() || $st == Compress::Raw::Zlib::Z_BUF_ERROR();
		# No progress: the input ended before the stream did
		return undef if length($in) == $li && length($out) == $lo;
	}
	return (length($out) == $rawlen && length($in) == 0) ? $out : undef;
}

my @DFL_LBASE = (3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258);
my @DFL_LEXT  = (0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0);
my @DFL_DBASE = (1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
		 4097, 6145, 8193, 12289, 16385, 24577);
my @DFL_DEXT  = (0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13);
my @DFL_CLORD = (16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15);

# A Huffman code from its lengths, as puff of zlib keeps it: the count of
# each length, the symbols in canonical order; undef - over-subscribed, or
# incomplete with more than one code
sub dfl_build
{
	my ($lens) = @_;
	my @count = (0) x 16;
	$count[$_]++ for @$lens;
	$count[0] = 0;
	my ($left, $codes) = (1, 0);
	for my $b (1 .. 15)
	{
		$left = ($left << 1) - $count[$b];
		$codes += $count[$b];
		return undef if $left < 0;
	}
	return undef if $left && $codes > 1;
	my @offs = (0, 0);
	$offs[$_ + 1] = $offs[$_] + $count[$_] for 1 .. 14;
	my @sym;
	for my $i (0 .. $#$lens)
	{
		$sym[$offs[$lens->[$i]]++] = $i if $lens->[$i];
	}
	return [\@count, \@sym];
}

# inflate_pure: the inflate here, a bit at a time - slow, plain, checked
sub inflate_pure
{
	my ($src, $rawlen) = @_;
	my @in = unpack('C*', $src);
	my ($n, $ip, $buf, $cnt) = (scalar(@in), 0, 0, 0);
	my @out;
	my $bits = sub
	{
		my ($need) = @_;
		while ($cnt < $need)
		{
			return undef if $ip >= $n;
			$buf |= $in[$ip++] << $cnt;
			$cnt += 8;
		}
		my $v = $buf & ((1 << $need) - 1);
		$buf >>= $need;
		$cnt -= $need;
		return $v;
	};
	my $decode = sub
	{
		my ($h) = @_;
		my ($count, $sym) = @$h;
		my ($code, $first, $index) = (0, 0, 0);
		for my $len (1 .. 15)
		{
			my $b = $bits->(1);
			return undef unless defined($b);
			$code |= $b;
			my $c = $count->[$len];
			return $sym->[$index + $code - $first] if $code - $c < $first;
			$index += $c;
			$first = ($first + $c) << 1;
			$code <<= 1;
		}
		return undef;
	};
	my $codes = sub
	{
		my ($lit, $dist) = @_;
		while (1)
		{
			my $s = $decode->($lit);
			return 0 unless defined($s);
			if ($s < 256)
			{
				return 0 if @out >= $rawlen;
				push(@out, $s);
				next;
			}
			return 1 if $s == 256;
			$s -= 257;
			return 0 if $s >= 29;
			my $e = $bits->($DFL_LEXT[$s]);
			return 0 unless defined($e);
			my $len = $DFL_LBASE[$s] + $e;
			my $d = $decode->($dist);
			return 0 unless defined($d) && $d < 30;
			$e = $bits->($DFL_DEXT[$d]);
			return 0 unless defined($e);
			my $dist_ = $DFL_DBASE[$d] + $e;
			return 0 if $dist_ > @out || $len > $rawlen - @out;
			my $from = @out - $dist_;
			push(@out, $out[$from++]) for 1 .. $len;
		}
	};
	my $last = 0;
	while (!$last)
	{
		$last = $bits->(1);
		my $type = $bits->(2);
		return undef unless defined($last) && defined($type);
		if ($type == 0)
		{
			# Stored: to the octet, LEN, NLEN, the octets
			$buf >>= ($cnt & 7);
			$cnt -= ($cnt & 7);
			my $len = $bits->(16);
			my $nlen = $bits->(16);
			return undef unless defined($nlen) && $len == (~$nlen & 0xFFFF);
			return undef if $len > $rawlen - @out;
			while ($len && $cnt >= 8)
			{
				push(@out, $bits->(8));
				$len--;
			}
			return undef if $len > $n - $ip;
			push(@out, @in[$ip .. $ip + $len - 1]) if $len;
			$ip += $len;
		}
		elsif ($type == 1)
		{
			my @l = ((8) x 144, (9) x 112, (7) x 24, (8) x 8);
			return undef unless $codes->(dfl_build(\@l), dfl_build([(5) x 32]));
		}
		elsif ($type == 2)
		{
			my $hlit = $bits->(5);
			my $hdist = $bits->(5);
			my $hclen = $bits->(4);
			return undef unless defined($hclen);
			($hlit, $hdist, $hclen) = ($hlit + 257, $hdist + 1, $hclen + 4);
			return undef if $hlit > 286 || $hdist > 30;
			my @cl = (0) x 19;
			for my $i (0 .. $hclen - 1)
			{
				my $v = $bits->(3);
				return undef unless defined($v);
				$cl[$DFL_CLORD[$i]] = $v;
			}
			my $clh = dfl_build(\@cl);
			return undef unless $clh;
			my @lens;
			while (@lens < $hlit + $hdist)
			{
				my $s = $decode->($clh);
				return undef unless defined($s);
				if ($s < 16)
				{
					push(@lens, $s);
					next;
				}
				my ($val, $rep) = (0, undef);
				if ($s == 16)
				{
					return undef unless @lens;
					$val = $lens[-1];
					$rep = $bits->(2);
					$rep += 3 if defined($rep);
				}
				elsif ($s == 17)
				{
					$rep = $bits->(3);
					$rep += 3 if defined($rep);
				}
				else
				{
					$rep = $bits->(7);
					$rep += 11 if defined($rep);
				}
				return undef unless defined($rep) && @lens + $rep <= $hlit + $hdist;
				push(@lens, ($val) x $rep);
			}
			# No end-of-block code: no block of it can end
			return undef unless $lens[256];
			my $lh = dfl_build([@lens[0 .. $hlit - 1]]);
			my $dh = dfl_build([@lens[$hlit .. $hlit + $hdist - 1]]);
			return undef unless $lh && $dh && $codes->($lh, $dh);
		}
		else
		{
			return undef;
		}
	}
	return undef unless @out == $rawlen && $ip == $n && $cnt < 8;
	return pack('C*', @out);
}

#
#  Codec 3: raw LZMA1, lc=3 lp=0 pb=2, ended by the end marker (format.md
#  6.7.3) - the decoder of the LZMA specification, in Perl.  The range
#  coder works in 32 bits, every shift masked.  Exactly $rawlen octets,
#  then the end marker with the code at 0 and the input used up; every
#  distance against what has been output, every length against what is
#  still wanted; or undef.
#
use constant {
	LZ_LIT => 0, LZ_ISMATCH => 6144, LZ_REP0L => 6192, LZ_ISREP => 6240, LZ_REPG0 => 6252, LZ_REPG1 => 6264,
	LZ_REPG2 => 6276, LZ_SLOT => 6288, LZ_SPEC => 6544, LZ_ALIGN => 6659, LZ_LEN => 6675, LZ_RLEN => 6997,
	LZ_NPROB => 7319,
};

sub lzma_decompress
{
	my ($src, $rawlen) = @_;
	my $n = length($src);
	return undef if $n < 5 || ord($src) != 0;
	my @in = unpack('C*', $src);
	my $ip = 5;
	my $code = ($in[1] << 24) | ($in[2] << 16) | ($in[3] << 8) | $in[4];
	my $range = 0xFFFFFFFF;
	return undef if $code == $range;
	my $bad = 0;
	my @p = (1024) x LZ_NPROB;
	my @out;
	my $bit = sub
	{
		my $i = $_[0];
		my $bound = ($range >> 11) * $p[$i];
		my $b;
		if ($code < $bound)
		{
			$range = $bound;
			$p[$i] += (2048 - $p[$i]) >> 5;
			$b = 0;
		}
		else
		{
			$range -= $bound;
			$code -= $bound;
			$p[$i] -= $p[$i] >> 5;
			$b = 1;
		}
		if ($range < 16777216)
		{
			$range = ($range << 8) & 0xFFFFFFFF;
			if ($ip < $n) { $code = (($code << 8) | $in[$ip++]) & 0xFFFFFFFF; }
			else	      { $code = ($code << 8) & 0xFFFFFFFF; $bad = 1; }
		}
		return $b;
	};
	my $tree = sub
	{
		my ($base, $nb) = @_;
		my $m = 1;
		$m = ($m << 1) | $bit->($base + $m) for 1 .. $nb;
		return $m - (1 << $nb);
	};
	my $revtree = sub
	{
		my ($base, $nb) = @_;
		my ($m, $sym) = (1, 0);
		for my $i (0 .. $nb - 1)
		{
			my $b = $bit->($base + $m);
			$m = ($m << 1) | $b;
			$sym |= $b << $i;
		}
		return $sym;
	};
	my $len = sub
	{
		my ($base, $ps) = @_;
		return 2 + $tree->($base + 2 + $ps * 8, 3) unless $bit->($base);
		return 10 + $tree->($base + 34 + $ps * 8, 3) unless $bit->($base + 1);
		return 18 + $tree->($base + 66, 8);
	};
	my $dist = sub
	{
		my ($l) = @_;
		my $slot = $tree->(LZ_SLOT + 64 * ($l - 2 < 3 ? $l - 2 : 3), 6);
		return $slot if $slot < 4;
		my $foot = ($slot >> 1) - 1;
		my $d = (2 | ($slot & 1)) << $foot;
		return $d + $revtree->(LZ_SPEC + $d - $slot, $foot) if $slot < 14;
		my $r = 0;
		for (1 .. $foot - 4)
		{
			# A direct bit: half the range
			$range >>= 1;
			if ($code >= $range) { $code -= $range; $r = ($r << 1) | 1; }
			else		     { $r <<= 1; }
			$bad = 1 if $code == $range;
			if ($range < 16777216)
			{
				$range = ($range << 8) & 0xFFFFFFFF;
				if ($ip < $n) { $code = (($code << 8) | $in[$ip++]) & 0xFFFFFFFF; }
				else	      { $code = ($code << 8) & 0xFFFFFFFF; $bad = 1; }
			}
		}
		return ($d + ($r << 4) + $revtree->(LZ_ALIGN, 4)) & 0xFFFFFFFF;
	};
	my ($state, @rep) = (0, 0, 0, 0, 0);
	while (!$bad)
	{
		my $op = scalar(@out);
		my $ps = $op & 3;
		if (!$bit->(LZ_ISMATCH + $state * 4 + $ps))
		{
			return undef if $op >= $rawlen;
			my $lit = LZ_LIT + 0x300 * (($op ? $out[$op - 1] : 0) >> 5);
			my $sym = 1;
			if ($state >= 7)
			{
				my $mb = $out[$op - $rep[0] - 1];
				while ($sym < 0x100)
				{
					my $mbit = ($mb >> 7) & 1;
					$mb <<= 1;
					my $b = $bit->($lit + 0x100 + ($mbit << 8) + $sym);
					$sym = ($sym << 1) | $b;
					last if $mbit != $b;
				}
			}
			$sym = ($sym << 1) | $bit->($lit + $sym) while $sym < 0x100;
			push(@out, $sym & 0xFF);
			$state = $state < 4 ? 0 : $state < 10 ? $state - 3 : $state - 6;
			next;
		}
		my $l;
		if ($bit->(LZ_ISREP + $state))
		{
			return undef unless $op;
			if (!$bit->(LZ_REPG0 + $state))
			{
				if (!$bit->(LZ_REP0L + $state * 4 + $ps))
				{
					# A short rep: one octet at rep0
					return undef if $op >= $rawlen;
					$state = $state < 7 ? 9 : 11;
					push(@out, $out[$op - $rep[0] - 1]);
					next;
				}
			}
			else
			{
				my $d;
				if (!$bit->(LZ_REPG1 + $state)) { $d = $rep[1]; }
				else
				{
					if (!$bit->(LZ_REPG2 + $state)) { $d = $rep[2]; }
					else				 { $d = $rep[3]; $rep[3] = $rep[2]; }
					$rep[2] = $rep[1];
				}
				$rep[1] = $rep[0];
				$rep[0] = $d;
			}
			$l = $len->(LZ_RLEN, $ps);
			$state = $state < 7 ? 8 : 11;
		}
		else
		{
			@rep[1 .. 3] = @rep[0 .. 2];
			$l = $len->(LZ_LEN, $ps);
			$state = $state < 7 ? 7 : 10;
			$rep[0] = $dist->($l);
			if ($rep[0] == 0xFFFFFFFF)
			{
				# The end marker: all the octets, the code at 0, the input used up
				return undef if $bad || $op != $rawlen || $code != 0 || $ip != $n;
				return pack('C*', @out);
			}
		}
		return undef if $rep[0] >= $op || $l > $rawlen - $op;
		my $from = $op - $rep[0] - 1;
		push(@out, $out[$from++]) for 1 .. $l;
	}
	return undef;
}

# unpack_data: exactly $raw octets out of $src by the codec of a DATAZ or SOLID record, or undef
sub unpack_data
{
	my ($codec, $src, $raw) = @_;
	return ($codec == CODEC_LZ4) ? lz4_decompress($src, $raw)
	     : ($codec == CODEC_DEFLATE) ? inflate($src, $raw)
	     : ($codec == CODEC_LZMA) ? lzma_decompress($src, $raw) : undef;
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
	return () if length($body) < 20 || u32(\$body, 16) > MAXDATA;
	my ($codec, $raw) = (u32(\$body, 4), u32(\$body, 16));
	# An unknown codec: a bad record, its file named incomplete - never wrong octets
	my $d = unpack_data($codec, substr($body, 20), $raw);
	return () unless defined($d);
	return (u32(\$body, 0), u64(\$body, 8), $d);
}

# A DATA record (u32 fileno, u32 0, u64 offset, the bytes) or a DATAZ one
# (u32 fileno, u32 codec, u64 offset, u32 rawlen, the compressed octets); a DATAZ that
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

	# Reed-Solomon of version 2 (4.1): every pattern of at most m = 3 lost of 6 + 3 blocks rebuilt
	{
		my @o = map { my $i = $_; join('', map { chr(($i * 131 + $_ * 17 + $i * $_) & 0xFF) } 0 .. 36) } 0 .. 5;
		my @p = map { my $j = $_; my $v = "\0" x 37; rs_muladd(\$v, $o[$_], rs_coef($j, $_)) for 0 .. 5; $v } 0 .. 2;
		my $nbad = 0;
		for my $set (0 .. (1 << 9) - 1)
		{
			my @bits = grep { $set & (1 << $_) } 0 .. 8;
			next if @bits > 3;
			my @dok = map { ($set & (1 << $_)) ? 0 : 1 } 0 .. 5;
			my @pok = map { ($set & (1 << (6 + $_))) ? 0 : 1 } 0 .. 2;
			my @d = map { $dok[$_] ? $o[$_] : 'x' x 37 } 0 .. 5;
			$nbad++ unless rs_repair(6, 3, \@d, \@dok, \@p, \@pok) eq 'ok' && join('', @d) eq join('', @o);
		}
		$try->('Reed-Solomon, every erasure of 6 + 3', $nbad ? 'FAILED' : 'ok', unpack('H*', 'ok'));
	}
	# The codecs of DATAZ (6.7.2, 6.7.3): streams of zlib and of liblzma, read back; bad ones refused
	{
		my $text = join('', map { "line $_: " . (substr('abcxyz', 0, $_ % 7) x ($_ % 5)) . '|' } 0 .. 299);
		my $mixed = ("vbkx-pl: the extractor of last resort; " x 12) . join('', map { chr } 0 .. 255) . ("\0" x 300);
		my $dyn = pack('H*',
		  '85d75b6edc300c05d0ad6409ba572f32bb498b7e1408fadd165d7c93d8239292ec0192811e638b363887d2fbcf5f3f5e'
		. 'd2ebcbbff7cf065e5fde8e163f5adfdebe1d9dfcd9f97efc1d23e56be4b7ff3f26eae356edf8c69fbf47b73fc6e563fc'
		. 'ed5c448f456c21582438ae3f7b3ceff6f5718ee5c70afef39c2be33e23207c4574b6fbe389cebec4e73947d5af3aafcf'
		. '112b315a9f713ede607e3cddd92fee2d8e9539e2e3e38d9ddd1e1fef1c95f175fd7a91e7627904938f17777668cf758e'
		. 'e4e599ce89b27b9d7eed3c42cd6de449ee215164c994ac57a95246cc0531590ac74c76e952ca9c2f6584549acf97d2d7'
		. '7c29729d2f451ff7a923a80acb97ca982f35eff2a596bb7ca923d6da46ab5bbe5489f95275972f6dc4d710f2a571972f'
		. '2d8faf179f2fcd7ea7cde54beb73be34b9c897a6cff2a58f50bbb9d2032c7d95a55fd2d247cc7dc665e8d23d2f7df145'
		. '4648127c918d2f72e38b0c5f640425ce17997c91ad2f72eb8b8e5875f8a2ce179d7cd1ad2f3ae2d3e88b6e7dd1e18b06'
		. '5f904ce6e485415a8841ba3206e929324806763266908233482b344897d2c0d795c91a8036e7b501166ee0aa090238c0'
		. '461ce0861c6098032b25a053079cd801b7ee80b7f0c06a0cd8ace9ec01277cc0ad3eb03a831cfd41de02849ced8a4010'
		. 'ac9e207b8490178590af18427eea10acd0a0984428812294d522944b8c60a50765e208a5db9c070965ddf158c1418d7b'
		. '9ebadbf4d4bb5d4f75db1e0baffa8d4f9d773e75bff5a9b736c1ca101aace978429b7c42db0285e676689128b4ad5168'
		. '625744a5ace4a007a5faaa54bf54aa3f57ca6a11ba53aa47a5fa46a97ead945527c8ac9498521294925529ab4990a894'
		. 'ec94923ba5c494b28204f54ae9ac94ee95d27ba5ac52414d29f54ae9ac946e95a2152ba6a814d35629a66c5704a568f5'
		. '88c92bc5b428c574a514d353a508779e70e73004a5885529e252295ac12226a5886e735e2962518aeeb0c3a014b9518a'
		. 'bc518a2c762f0b8f4e2972528adc2a45de9fd0ac5a31db192d8743da7c4acbfb639a152be6e9a096f727b56c47b51c94'
		. 'a2d52316af14cba214cb95522c4f95a2d52816538a2528c5b22ac572a914ad60b14e4ab1d2e6bc52ac8b52b40ac51a94'
		. '62dd28c57aa314abda29dac26b4e29b64929b6ad526cb74ab1b9f37ab3a6538a6d528a6daf94152bf649a9be57aa9b52'
		. '3d2a65f5883d28d557a5faa552fdb95256a3284e29894ac94629b956ca0a1665564a4c29094ac9aa9455286a544a774a'
		. 'e99d526a4a5965a27aa574564af74ae99552ff01');
		my $fix = pack('H*',
		  '2b4bcaaed02dc8b15228c9485548ad28294a4c2ec92f52c84f53c8492c2e51284a2dce2f2ab156281b55367494313032'
		. '31b3b0b2b173707271f3f0f2f10b080a098b888a894b484a49cbc8cac92b282a29aba8aaa96b686a69ebe8eae91b181a'
		. '199b989a995b585a59dbd8dad93b383a39bbb8bab97b787a79fbf8faf9070406058784868547444645c7c4c6c5272426'
		. '25a7a4a6a567646665e7e4e6e5171416159794969557545655d7d4d6d537343635b7b4b6b577747675f7f4f6f54f9838'
		. '69f294a9d3a6cf98396bf69cb9f3e62f58b868f192a5cb96af58b96af59ab5ebd66fd8b869f396addbb6efd8b96bf79e'
		. 'bdfbf61f3878e8f091a3c78e9f3879eaf499b3e7ce5fb878e9f295abd7aedfb879ebf69dbbf7ee3f78f8e8f193a7cf9e'
		. 'bf78f9eaf59bb7efde7ff8f8e9f397afdfbefff8f9ebf79fbffffe338c02a20100');
		my $sto = pack('H*', '010d00f2ff73746f72656420626c6f636b21');
		my $lzm = pack('H*',
		  '00361a4a1f08a026564e15942ead67c84ffb53d9155706a325813602854e1ed5d4d83a11c8f1cb513dccb69908fbaefc'
		. '509445ec785e0daf8748b1e9c6d49958831e37073fb7626c4d7dba71dd17343d3733e7bfa4152d8e42145f09aeac020a'
		. 'c766c67c6fcca0721b5ec7f93cca537f9e335994f181cc399da11edd6d64d05337f84af4c309798b9a703f45f4fe2aa1'
		. '7d994c4d19aad1e037c4b7fc74d09a4a23781d4ee991349a02a59e259eb8e529ccf54eb5efbbceff607111342d776b72'
		. 'a597bcace3300d0479a4eca14ae1ea7aee747f7b1df37603194e950f25e7f8b7cb7914e09d22ccfe8c4f387dd737b068'
		. 'ee66872e2bb4cce15f99791f85179b3540d1b2004c1b44d46a546b5396601254c64daa2a826ea969a765ea66d17657fc'
		. '170484d7ce756dbb775603e362c187f5eb8751023372545b9741f03efb3ad0ba1497bbb763577868b1ccee14a1603a0f'
		. '4148cf7bfdf4bdedabffdff69568cbc322eadc0c04bae9785cc25f8dd5073a129a81c0322fcd93f5eefb073ddb95e786'
		. '90a570135ac8895918ec9f498d8bdf586f5c84abbd9af43de3bc43923181c47c34e409b27f1d8cb3e9e7cd3b73a03c41'
		. '33850d08500f683ed41c3f639568e8aa7fa4c9dc356d4c460b2b7238d562d402070f7c7b07ef25b4a0cc903a5e748796'
		. '669e7cfa5492cd4fceb0d4cf026f829a794c1b4bb37ba62a2f70a7fc38d68afbad9a82e202af3a7bc193e9e10fc7d7a9'
		. '63eec36695460c6ea67c7c686d2d3eae2e2d8d28788ba04143f4d20fe91fe2bfcb6df917b82e911f90ac4a288526711d'
		. '29bb6fdfb1644b9293d74f9b1f620d68574a828a9e3fd6b6b26f90bd85fa85d7591f3977124d1fe6a63d0694ea33441e'
		. 'e9653c78b78ef51bc1ba36b5507c599e503bc419304922ebe985638a7e17946e908aea61e5e81d7e2a4270925ea7078c'
		. 'c7cf1f3469652cb0460ad161759434c95d29e0b635e0baad86998c84c1fb99cf78dfcec91de33be9b67a2b42b2e687af'
		. '9f3309a74ef321adc4e133d0d5aa52cdefdd6726df835abbcde6891e8842f62d7752cf2b8e1a240934341f2867251fff'
		. '5c59f900');
		my $zinf = $ZINF;
		my @inf = ([\&inflate_pure, ' (Perl)']);
		push(@inf, [\&inflate, ' (Compress::Raw::Zlib)']) if !$PURE && eval { require Compress::Raw::Zlib; 1 };
		$ZINF = 1 if @inf > 1;
		for my $w (@inf)
		{
			my ($f, $sfx) = @$w;
			my $ok = sub { my ($got, $want) = @_; return (defined($got) && $got eq $want) ? 'ok' : 'bad'; };
			$try->("Deflate, dynamic codes$sfx", $ok->($f->($dyn, length($text)), $text), unpack('H*', 'ok'));
			$try->("Deflate, fixed codes$sfx", $ok->($f->($fix, length($mixed)), $mixed), unpack('H*', 'ok'));
			$try->("Deflate, stored$sfx", $ok->($f->($sto, 13), 'stored block!'), unpack('H*', 'ok'));
			my $refused = (!defined($f->(substr($dyn, 0, -5), length($text))) && !defined($f->($dyn . "\0", length($text)))
				&& !defined($f->($dyn, length($text) + 1)) && !defined($f->($dyn, length($text) - 1))) ? 'ok' : 'bad';
			$try->("Deflate, short, long, cut streams refused$sfx", $refused, unpack('H*', 'ok'));
		}
		$ZINF = $zinf;
		$try->('LZMA1', (lzma_decompress($lzm, length($text)) // '') eq $text ? 'ok' : 'bad', unpack('H*', 'ok'));
		$try->('LZMA1, nothing', (lzma_decompress(pack('H*', '0083fffbffffc0000000'), 0) // 'x') eq '' ? 'ok' : 'bad', unpack('H*', 'ok'));
		my $refused = (!defined(lzma_decompress(substr($lzm, 0, -3), length($text))) && !defined(lzma_decompress($lzm . "\0", length($text)))
			&& !defined(lzma_decompress($lzm, length($text) + 1)) && !defined(lzma_decompress($lzm, length($text) - 1))) ? 'ok' : 'bad';
		$try->('LZMA1, short, long, cut streams refused', $refused, unpack('H*', 'ok'));
	}
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
	print STDERR "vbkx-pl X01-22 - the extractor of last resort for VBACKUP savesets\n\n",
		"  perl vbkx.pl l saveset [-k file]           list the files (times in UTC)\n",
		"  perl vbkx.pl x saveset [-C dir] [-k file]  extract them all\n",
		"  perl vbkx.pl t saveset [-k file]           read it all, check the checksums\n",
		"  perl vbkx.pl selftest                      check the primitives: encryption, parity, codecs\n\n",
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
