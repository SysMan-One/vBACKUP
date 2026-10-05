#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/smoke.sh
#
#	ABSTRACT:	Functional checks of the VBACKUP utility: save, restore
#			and compare of a tree with every kind of file and
#			attribute; /LIST in its formats; /VERIFY; volumes; the
#			repair of a damaged saveset and the report of a lost
#			block; wildcards and "..."; /SELECT, /EXCLUDE; /EXTRACT;
#			/REPLACE; the nodump flag; a saveset cut short; the
#			standard output; the completion codes.
#
#	DESCRIPTION:	VBACKUP names the image, SCRATCH a directory the script
#			may fill and remove - it must be on ext4 or xfs: user
#			xattrs and chattr flags exist there, not on tmpfs.
#			Run as root the owner and the immutable flag are
#			checked as well; as anybody else they are skipped.
#
#			A failing check prints %VBACKUP-E-SMOKE and the script
#			goes on; the completion code is the number of failures.
#			KEEP=1 leaves the scratch directory for a look.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 3-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 5-OCT-2026	RRL	X-13 : /LIST/FULL glued, a listing.
#
#		 5-OCT-2026	RRL	X-11 : Volumes through a pipe; a saveset copied block for
#					block (the same bytes, encrypted, no key; a bad
#					block copied and said); node::file through a
#					stand-in for ssh: save /VERIFY, restore, an error.
#
#		 5-OCT-2026	RRL	X-09 : /LIST /SELECT; /VERIFY and /DELETE with the standard
#					output refused.
#
#		 5-OCT-2026	RRL	X-08 : A saveset through a pipe: restore, listing,
#					/EXTRACT, encrypted and compressed, a bad block
#					repaired, a pipe cut short, /VOLUME_SIZE refused,
#					vbkx -.
#
#		 5-OCT-2026	RRL	X-06 : /ENCRYPT: save, list, restore, compare, /VERIFY,
#					volumes and compression; a wrong passphrase, no
#					key, a key file others may read; a forged block
#					(CRC right) rebuilt; vbkx -k.  The words of the
#					command: MAXPARM, glued qualifiers, wildcards,
#					.sav, a copy not into itself.
#
#		 4-OCT-2026	RRL	X-04 : /PHYSICAL: an image file, and loop devices as root
#					(the guards, the gaps zeroed, nothing beyond).
#					/IMAGE: an ext4 volume made again on a loop
#					device - the tree, the root, label and UUID.
#					/ORIGINAL (two bases), /DELETE (only after a
#					clean /VERIFY).  umount of a loop device tried
#					again: udev holds it a moment (TTR-GGW1).
#
#		 4-OCT-2026	RRL	X-04 : TAP=1 - the Test Anything Protocol (test/tap.sh).
#
#		 4-OCT-2026	RRL	X-04 : /DATA_FORMAT=COMPRESSED: smaller, the same tree,
#					the same listing, deterministic; vbkx reads it.
#
#		 3-OCT-2026	RRL	X-03 : Stage 3: the writer thread - a save without
#					it gives the same saveset contents, a volume
#					switch through the queue, a write error that
#					comes late is still reported.  The read-ahead
#					of files: the same saveset without it, a cold
#					file stays out of the page cache.  VBKX, the
#					stand-alone extractor (VBKX names it).  Two
#					saves of one tree list the same; counted XATTR
#					names; the journal in the order of its names.
#
#		 3-OCT-2026	RRL	X-02 : Stage 2: /RECORD, /SINCE=BACKUP, incremental
#					savesets restored /INCREMENTAL, the journal
#					listed and rebuilt, the copy disk to disk.
#					The base in a listing is absolute now; a file
#					given with a directory is copied there.
#
#		 3-OCT-2026	RRL	X-01 : Initial version.
#
#---

VB=${VBACKUP:?"VBACKUP must name the image"}
S=${SCRATCH:?"SCRATCH must name a scratch directory"}

#	ok, fail, check, bail, tap_end - plain output, or TAP with TAP=1
TAPNAME=SMOKE
. "$(dirname "$0")/tap.sh"

#	An immutable file cannot be removed: the flag goes first, whatever happened
wipe ()	{ [ -e "$1" ] && chattr -R -i "$1" 2>/dev/null; rm -rf "$1"; }
cleanup () { wipe "$S"; }
[ -n "$KEEP" ] || trap cleanup EXIT

cleanup
mkdir -p "$S" || exit 1
cd "$S" || exit 1

ROOT=0
[ "$(id -u)" = 0 ] && ROOT=1

#
#	The tree: every kind of file, every kind of attribute
#
T=src/tree
mkdir -p $T/sub/deep $T/empty-dir
echo hello > $T/a.txt
: > $T/empty
head -c 300000 /dev/urandom > $T/sub/rand.bin
head -c 3000000 /dev/urandom > $T/big.bin
truncate -s 50M $T/sparse
printf 'middle' | dd of=$T/sparse bs=1 seek=20000000 conv=notrunc 2>/dev/null
ln -s a.txt $T/link
ln -s /nonexistent/target $T/dangling
ln $T/a.txt $T/sub/hard
mkfifo $T/fifo
echo x > "$T/sub/deep/имя с пробелом"
LONG=$(printf 'n%.0s' $(seq 1 200))
echo long > "$T/sub/$LONG"
chmod 750 $T/sub
chmod 4755 $T/big.bin
python3 -c "import os; os.setxattr('$T/a.txt', 'user.test', b'value1'); os.setxattr('$T/sub', 'user.dir', b'd')" 2>/dev/null \
	|| echo "%VBACKUP-W-SMOKE, no user xattrs here: $(stat -f -c %T .)"
command -v setfacl >/dev/null && setfacl -m u:nobody:r $T/sub/rand.bin && setfacl -d -m u:nobody:rx $T/sub/deep
touch -d '2001-02-03 04:05:06.789' $T/a.txt $T/sub/deep
echo nodump > $T/nodump.txt
chattr +d $T/nodump.txt 2>/dev/null || echo "%VBACKUP-W-SMOKE, no chattr flags here"
[ $ROOT = 1 ] && chown 65534:65534 $T/empty && chattr +i $T/empty

#	The state of a tree, for comparing two: types, modes, sizes, times, links
state () {
	( cd "$1" && {
	find . -type f ! -name nodump.txt -printf 'f %m %s %T@ %U %P\n'
	find . -type d -printf 'd %m %T@ %P\n'
	find . -type l -printf 'l %l %T@ %P\n'
	find . -type p -printf 'p %m %T@ %P\n'
	} | sort )
}

same_tree () {
	[ "$(state "$1")" = "$(state "$2")" ] || return 1

	( cd "$1" && find . -type f ! -name nodump.txt ) | while read -r F; do
		cmp -s "$1/$F" "$2/$F" || { echo "  differs: $F"; return 1; }
	done
}

#
#	1. Save, list, restore, compare
#
$VB src/tree x.bck > save.log 2>&1
check '[ $? = 0 ]' "save: completion code, $(cat save.log)"
check '[ -s x.bck ]' "save: no saveset"

$VB x.bck /LIST > list.txt 2>&1
check '[ $? = 0 ]' "list: completion code"
check 'grep -q "^tree/a.txt " list.txt' "list: tree/a.txt not listed"
check 'grep -q "Total of" list.txt && grep -q "End of save set" list.txt' "list: no totals"
check '! grep -q nodump.txt list.txt' "list: a nodump file was saved"

$VB x.bck /LIST /FORMAT=LS > ls.txt
check 'grep -q "^lrwxrwxrwx .* tree/link -> a.txt$" ls.txt' "list /FORMAT=LS: symlink line"
check 'grep -q "^-rwsr-xr-x .* tree/big.bin$" ls.txt' "list /FORMAT=LS: setuid file line"
check 'grep -Eq "^-rw-r--r-- +2 .* [0-9]{2}-[0-9]{2}-[0-9]{4} [0-9:]{8} tree/sub/hard -> tree/a.txt$" ls.txt' "list /FORMAT=LS: hard link line"

$VB x.bck /LIST /FULL > full.txt
check 'grep -q "Checksum:" full.txt' "list /FULL: no checksum"

$VB x.bck out > rest.log 2>&1
check '[ $? = 0 ]' "restore: completion code, $(cat rest.log)"
check 'same_tree src/tree out/tree' "restore: the tree differs"
check '[ "$(stat -c %i out/tree/a.txt)" = "$(stat -c %i out/tree/sub/hard)" ]' "restore: hard link lost"
check '[ "$(du -k out/tree/sparse | cut -f1)" -lt 1000 ]' "restore: the sparse file is not sparse"
check '[ "$(python3 -c "import os; print(os.getxattr(\"out/tree/a.txt\", \"user.test\").decode())" 2>/dev/null)" = value1 ]' \
	"restore: user xattr lost"
check '[ "$(python3 -c "import os; print(os.getxattr(\"out/tree/sub\", \"user.dir\").decode())" 2>/dev/null)" = d ]' \
	"restore: user xattr of a directory lost"

if command -v getfacl >/dev/null; then
	check '[ "$(getfacl -c src/tree/sub/rand.bin)" = "$(getfacl -c out/tree/sub/rand.bin)" ]' "restore: ACL lost"
	check '[ "$(getfacl -c src/tree/sub/deep)" = "$(getfacl -c out/tree/sub/deep)" ]' "restore: default ACL lost"
fi

if [ $ROOT = 1 ]; then
	check 'lsattr -d out/tree/empty | grep -q "^....i"' "restore: immutable flag lost"
	check '[ "$(stat -c %u:%g out/tree/empty)" = 65534:65534 ]' "restore: owner lost"
fi

$VB x.bck src /COMPARE > cmp.log 2>&1
check '[ $? = 0 ]' "compare: completion code, $(cat cmp.log)"

$VB x.bck /COMPARE > cmp2.log 2>&1
check '[ $? = 0 ]' "compare against the bases: completion code, $(cat cmp2.log)"

echo changed > src/tree/sub/rand.bin.tmp && cp src/tree/a.txt a.save && echo HELLO > src/tree/a.txt
$VB x.bck src /COMPARE > cmp3.log 2>&1
check '[ $? = 2 ] && grep -q COMPARERR cmp3.log' "compare: a difference not found"
cp a.save src/tree/a.txt && touch -d '2001-02-03 04:05:06.789' src/tree/a.txt && rm -f src/tree/sub/rand.bin.tmp

#
#	2. Restore over what is there: /REPLACE
#
$VB x.bck out /SELECT=tree/a.txt > repl.log 2>&1
check '[ $? = 1 ] && grep -q FILEEXISTS repl.log' "restore over a file: no FILEEXISTS"
$VB x.bck out /SELECT=tree/a.txt /REPLACE > repl2.log 2>&1
check '[ $? = 0 ]' "restore /REPLACE: completion code, $(cat repl2.log)"

#
#	3. /VERIFY, volumes, the repair of a block, a lost block
#
$VB src/tree v.bck /VERIFY /VOLUME_SIZE=300K /BLOCK_SIZE=16384 /GROUP_SIZE=4 > ver.log 2>&1
check '[ $? = 0 ]' "save /VERIFY /VOLUME_SIZE: completion code, $(cat ver.log)"
check '[ -s v.bck.003 ]' "save /VOLUME_SIZE: fewer than 3 volumes"

$VB v.bck vout > vrest.log 2>&1
check '[ $? = 0 ] && same_tree src/tree vout/tree' "restore of volumes: differs, $(cat vrest.log)"

#	Block 3 of volume 2 zeroed: one block of a group, rebuilt from the XOR block
dd if=/dev/zero of=v.bck.002 bs=16384 seek=3 count=1 conv=notrunc 2>/dev/null
wipe vout
$VB v.bck vout > vfix.log 2>&1
check '[ $? = 0 ] && grep -q BLKFIXED vfix.log && same_tree src/tree vout/tree' "repair of a block: $(cat vfix.log)"

#	Blocks 1 and 2 of volume 3 zeroed: two of one group, lost
dd if=/dev/zero of=v.bck.003 bs=16384 seek=1 count=2 conv=notrunc 2>/dev/null
wipe vout
$VB v.bck vout > vlost.log 2>&1
check '[ $? = 2 ] && grep -q BLKLOST vlost.log && grep -q FILDAMAGED vlost.log' "lost blocks: not reported, $(cat vlost.log)"
check '[ -s vout/tree/a.txt ]' "lost blocks: the files elsewhere were not restored"

#
#	4. Wildcards and "...", /EXCLUDE, /SELECT
#
$VB 'src/tree/.../*.txt' w.bck > w.log 2>&1
$VB w.bck /LIST /FORMAT=LS > w.txt
check 'grep -q " a.txt$" w.txt && ! grep -q "rand.bin" w.txt' "wildcards: wrong selection"
$VB w.bck /LIST > w2.txt
check 'grep -q "^Base: *$S/src/tree$" w2.txt' "wildcards: wrong base (an absolute one expected)"

$VB src/tree e.bck '/EXCLUDE=(*.bin,*/deep)' > e.log 2>&1
$VB e.bck /LIST /FORMAT=LS > e.txt
check '! grep -q "\.bin$" e.txt && ! grep -q "deep" e.txt && grep -q "tree/a.txt$" e.txt' "/EXCLUDE: wrong selection"

$VB x.bck sel '/SELECT=*.bin' > sel.log 2>&1
check '[ -f sel/tree/big.bin ] && [ ! -f sel/tree/a.txt ]' "restore /SELECT: wrong selection"

#
#	5. /EXTRACT
#
$VB x.bck /EXTRACT=tree/sub/rand.bin > ext.bin 2> ext.log
check 'cmp -s ext.bin src/tree/sub/rand.bin' "/EXTRACT to stdout: differs, $(cat ext.log)"
$VB x.bck sp.out /EXTRACT=tree/sparse > ext2.log 2>&1
check 'cmp -s sp.out src/tree/sparse' "/EXTRACT of a sparse file: differs"
$VB x.bck /EXTRACT=tree/nothing > /dev/null 2> ext3.log
check '[ $? = 2 ] && grep -q NOTFOUND ext3.log' "/EXTRACT of a missing name: no NOTFOUND"

#
#	6. The nodump flag, /IGNORE=NOBACKUP
#
$VB src/tree nd.bck /IGNORE=NOBACKUP > nd.log 2>&1
check '$VB nd.bck /LIST | grep -q nodump.txt' "/IGNORE=NOBACKUP: the nodump file is missing"

#
#	7. A saveset cut short, the standard output, a file that is no saveset
#
cp x.bck cut.bck && truncate -s 262144 cut.bck
$VB cut.bck cout > cut.log 2>&1
check 'grep -q NOTRAILER cut.log && [ -f cout/tree/a.txt ]' "cut saveset: $(cat cut.log)"

$VB src/tree - > stdout.bck 2> std.log
check '[ $? = 0 ] && $VB stdout.bck /LIST | grep -q "tree/a.txt"' "save to the standard output: $(cat std.log)"

$VB list.txt somewhere > nots.log 2>&1
check '[ $? = 0 ] && cmp -s list.txt somewhere/list.txt' "a file to a directory is a copy: $(cat nots.log)"
$VB list.txt /LIST > nots2.log 2>&1
check '[ $? = 2 ]' "a text file listed as a saveset"

#
#	8. A forged saveset: names that climb out, a link to go through
#
python3 - evil.bck <<'PYEOF'
import struct, sys, zlib
B = 8192; P = B - 64; UU = b'\x11' * 16
def tlv(t, v): return struct.pack('<HI', t, len(v)) + v
def rec(t, body): return struct.pack('<HHI', t, 0, len(body)) + body
def blk(typ, no, recoff, pay):
	h = b'VBKB' + struct.pack('<HHIBBH', 64, 1, B, typ, 0, 0) + UU + struct.pack('<QIIIIII', no, 1, recoff, len(pay), 0xFFFFFFFF, 0, 0)
	b = bytearray(h + pay + bytes(P - len(pay)))
	struct.pack_into('<I', b, 60, zlib.crc32(bytes(b)))
	return bytes(b)
def file(no, path, ftype, link=b''):
	t = tlv(1, struct.pack('<I', no)) + tlv(2, path) + tlv(3, bytes([ftype])) + tlv(4, struct.pack('<I', 0o644))
	t += tlv(5, struct.pack('<I', 0)) + tlv(6, struct.pack('<I', 0)) + tlv(9, struct.pack('<Q', 4)) + tlv(10, struct.pack('<qI', 0, 0))
	return rec(2, t + (tlv(15, link) if link else b''))
def data(no): return rec(3, struct.pack('<IIQ', no, 0, 0) + b'pwnd') + rec(4, tlv(1, struct.pack('<I', no)) + tlv(9, struct.pack('<Q', 4)) + tlv(32, struct.pack('<I', zlib.crc32(b'pwnd'))))
summ = rec(1, tlv(71, struct.pack('<I', 0)))
stream = summ + file(1, b'../evil', 1) + data(1) + file(2, b'/tmp/vbackup-evil', 1) + data(2)
stream += file(3, b'l', 3, sys.argv[1].encode() and b'../escape') + rec(4, tlv(1, struct.pack('<I', 3)))
stream += file(4, b'l/inside', 1) + data(4) + rec(6, b'')
open(sys.argv[1], 'wb').write(blk(3, 0, 0, summ) + blk(1, 1, 0, stream))
PYEOF
mkdir -p evil/out
$VB evil.bck evil/out /REPLACE > evil.log 2>&1
check '[ ! -e evil/evil ] && [ ! -e /tmp/vbackup-evil ] && [ ! -e evil/escape ]' "a forged saveset wrote outside the output directory"
check '[ "$(grep -c OPENOUT evil.log)" = 3 ]' "a forged saveset: the refusals not reported, $(cat evil.log)"

#
#	9. The journal and the incremental saves: /RECORD, /SINCE=BACKUP
#
J=$S/test.jnl
I=inc/tree
mkdir -p inc
mkdir -p $I/d1 $I/d2
echo keep > $I/keep.txt; echo modify > $I/modify.txt; echo gone > $I/gone.txt; echo old > $I/old-name.txt
echo chmod > $I/chmod.txt; echo inner > $I/d2/inner.txt; echo x > $I/d1/x.txt

$VB inc/tree f.bck /RECORD /JOURNAL=$J > f.log 2>&1
check '[ $? = 0 ] && grep -q RECORDED f.log && [ -s $J ]' "full /RECORD: $(cat f.log)"
check '! $VB f.bck /LIST | grep -q "unchanged file" && $VB f.bck /LIST | grep -q "^Kind: *full"' "a full save lists present entries, or is not full"

#	Nothing changed: an incremental saves no file, lists them all as present
$VB inc/tree i0.bck /SINCE=BACKUP /JOURNAL=$J > i0.log 2>&1
check '[ $? = 0 ] && [ -z "$($VB i0.bck /LIST /FORMAT=LS | grep -v "^d")" ]' "incremental of an unchanged tree saved files"
check '$VB i0.bck /LIST | grep -q "^Kind: *incremental" && $VB i0.bck /LIST | grep -q "unchanged files present"' "incremental: kind or present count not listed"

sleep 1
echo modified > $I/modify.txt
rm $I/gone.txt
mv $I/old-name.txt $I/new-name.txt
chmod 600 $I/chmod.txt
echo added > $I/added.txt
rm -rf $I/d2

$VB inc/tree i1.bck /SINCE=BACKUP /RECORD /JOURNAL=$J > i1.log 2>&1
check '[ $? = 0 ]' "incremental /RECORD: $(cat i1.log)"
$VB i1.bck /LIST /FORMAT=LS | grep -v '^d' | sed 's/.* //' | sort > i1.names
check '[ "$(tr "\n" " " < i1.names)" = "tree/added.txt tree/chmod.txt tree/modify.txt tree/new-name.txt " ]' \
	"incremental saved the wrong files: $(tr '\n' ' ' < i1.names)"

#	The chain restored: the full, then the incremental, /INCREMENTAL
$VB f.bck,i1.bck chain /INCREMENTAL > chain.log 2>&1
check '[ $? = 0 ] && same_tree inc/tree chain/tree' "chain /INCREMENTAL: the tree differs from the source, $(cat chain.log)"
check '[ ! -e chain/tree/gone.txt ] && [ ! -e chain/tree/old-name.txt ] && [ ! -e chain/tree/d2 ]' "chain /INCREMENTAL: removed files are still there"

#	The incremental alone over an empty directory: what it does not hold is MISSING
$VB i1.bck alone /INCREMENTAL > alone.log 2>&1
check 'grep -q "MISSING.*keep.txt" alone.log' "an incremental without its full: no MISSING, $(cat alone.log)"

#	/INCREMENTAL refuses a saveset of X01-01 make (no KIND): the cut saveset of section 7 has no catalog either
mkdir -p refuse/tree && echo mine > refuse/tree/mine.txt
$VB cut.bck refuse /INCREMENTAL > refuse.log 2>&1
check '[ $? = 2 ] && grep -q NOTINCR refuse.log && [ -f refuse/tree/mine.txt ]' "/INCREMENTAL without a catalog was not refused: $(cat refuse.log)"

#	A selection beside /INCREMENTAL is refused: the cleaning would reach beyond it
$VB f.bck,i1.bck sel2 /INCREMENTAL '/SELECT=*.txt' > sel2.log 2>&1
check '[ $? = 2 ] && grep -q CONFQUAL sel2.log && [ ! -e sel2 ]' "/INCREMENTAL with /SELECT not refused: $(cat sel2.log)"

#	The journal listed, and rebuilt from the catalogs
$VB /JOURNAL=$J /LIST > jl.txt 2>&1
check 'grep -q "Total of 2 savesets" jl.txt' "journal listing: $(cat jl.txt)"
$VB /JOURNAL=$J /LIST /FULL '/SELECT=*added*' > jl2.txt 2>&1
check 'grep -q "added.txt" jl2.txt && ! grep -q "keep.txt" jl2.txt' "journal /FULL /SELECT: $(cat jl2.txt)"

rm -f $J
$VB f.bck,i1.bck /RECORD /JOURNAL=$J > rb.log 2>&1
check '[ $? = 0 ] && grep -q RECORDED rb.log' "journal rebuild: $(cat rb.log)"
$VB inc/tree i2.bck /SINCE=BACKUP /JOURNAL=$J > i2.log 2>&1
check '[ -z "$($VB i2.bck /LIST /FORMAT=LS | grep -v "^d")" ]' "after the rebuild /SINCE=BACKUP still saved files"

#	A file touched after the last /RECORD is taken by the next /SINCE=BACKUP
$VB inc/tree j.bck /SINCE=BACKUP /RECORD /JOURNAL=$J > /dev/null 2>&1
touch inc/tree/keep.txt
$VB inc/tree i3.bck /SINCE=BACKUP /JOURNAL=$J > /dev/null 2>&1
check '$VB i3.bck /LIST /FORMAT=LS | grep -q "tree/keep.txt$"' "a touched file was not taken by /SINCE=BACKUP"

#
#	10. The copy, disk to disk
#
$VB src/tree cpy /VERIFY > cpy.log 2>&1
check '[ $? = 0 ] && same_tree src/tree cpy/tree' "copy: the tree differs, $(cat cpy.log)"
check '[ "$(stat -c %i cpy/tree/a.txt)" = "$(stat -c %i cpy/tree/sub/hard)" ]' "copy: hard link lost"
check '[ "$(du -k cpy/tree/sparse | cut -f1)" -lt 1000 ]' "copy: the sparse file is not sparse"
check '[ "$(python3 -c "import os; print(os.getxattr(\"cpy/tree/a.txt\", \"user.test\").decode())" 2>/dev/null)" = value1 ]' "copy: xattr lost"

#	A copy into the tree it copies does not copy itself
mkdir -p src/tree/into
$VB src/tree src/tree/into > into.log 2>&1
check '[ ! -e src/tree/into/tree/into ]' "copy into itself recursed"
wipe src/tree/into

#
#	11. The writer thread
#
VBACKUP_PIPELINE=0 $VB src/tree nopipe.bck /VOLUME_SIZE=1000000 > /dev/null 2>&1
check '[ $? = 0 ]' "a save without the writer thread failed"
$VB src/tree pipe.bck /VOLUME_SIZE=1000000 > /dev/null 2>&1
check '[ $? = 0 ] && [ -e pipe.bck.003 ]' "a save over volumes through the writer thread failed"
check '[ "$($VB pipe.bck /LIST /FORMAT=LS | sort)" = "$($VB nopipe.bck /LIST /FORMAT=LS | sort)" ]' "with and without the thread the listings differ"
$VB pipe.bck rpipe > /dev/null 2>&1
check '[ $? = 0 ] && same_tree src/tree rpipe/tree' "the saveset of the writer thread does not restore the tree"
#	The read-ahead of files: the same saveset with and without it; under a time filter too
VBACKUP_PREFETCH=0 $VB src/tree nopre.bck > /dev/null 2>&1
VBACKUP_PREFETCH=4 $VB src/tree pre.bck > /dev/null 2>&1
check '[ "$($VB pre.bck /LIST /FORMAT=LS | sort)" = "$($VB nopre.bck /LIST /FORMAT=LS | sort)" ]' "with and without the read-ahead the listings differ"
VBACKUP_PREFETCH=4 $VB src/tree pres.bck /SINCE=TODAY > /dev/null 2>&1
check '[ $? = 0 ]' "the read-ahead under a time filter failed"
VBACKUP_PREFETCH=4 $VB src/tree cpre > /dev/null 2>&1
check '[ $? = 0 ] && same_tree src/tree cpre/tree' "a copy with the read-ahead differs"

#	A cold file read ahead is still dropped behind the save; a warm one stays
if command -v fincore > /dev/null 2>&1; then
	mkdir -p cold && for i in 1 2 3 4 5 6 7 8; do head -c 200000 /dev/urandom > cold/f$i; done
	sync
	python3 -c 'import os,sys
for f in sys.argv[1:]:
    fd=os.open(f,os.O_RDONLY); os.posix_fadvise(fd,0,0,os.POSIX_FADV_DONTNEED); os.close(fd)' cold/f*
	VBACKUP_PREFETCH=4 $VB cold cold.bck > /dev/null 2>&1
	check '[ "$(fincore -nb -o RES cold/f* | awk "{s+=\$1} END {print s+0}")" = 0 ]' "a cold file read ahead stayed in the page cache"
	cat cold/f* > /dev/null
	VBACKUP_PREFETCH=4 $VB cold cold.bck /REPLACE > /dev/null 2>&1
	check '[ "$(fincore -nb -o RES cold/f* | awk "{s+=\$1} END {print s+0}")" -ge 1600000 ]' "a warm file was dropped from the page cache"
fi

#	EXTRACT seeks to the catalog, then to the file in the same group: read once, a repair reported once
mkdir -p sk/t && echo one > sk/t/f1 && head -c 100000 /dev/urandom > sk/t/r.bin
$VB sk/t sk.bck > /dev/null 2>&1
python3 -c 'f=open("sk.bck","r+b"); f.seek(65536+200); f.write(b"\xff"*50)'
$VB sk.bck /EXTRACT=t/r.bin 2> sk.log | cmp -s - sk/t/r.bin
check '[ $? = 0 ] && [ "$(grep -c BLKFIXED sk.log)" = 1 ]' "EXTRACT through a repaired group: $(cat sk.log)"

if [ -w /dev/full ]; then
	$VB src/tree /dev/full /SAVE_SET /REPLACE > full.log 2>&1
	check '[ $? = 2 ] && grep -q "WRITERR.*errno: 28" full.log' "a write error of the thread was not reported: $(cat full.log)"
	VBACKUP_PIPELINE=0 $VB src/tree /dev/full /SAVE_SET /REPLACE > full0.log 2>&1
	check '[ $? = 2 ] && grep -q "WRITERR.*errno: 28" full0.log' "a write error without the thread was not reported"
fi

#	Determinism: the same tree saved twice lists the same, attributes and all, but for the date;
#	the same files recorded in another order make the same journal
python3 -c 'import os; os.setxattr("src/tree/a.txt", "user.zz", b"2"); os.setxattr("src/tree/a.txt", "user.aa", b"1")' 2>/dev/null
$VB src/tree det1.bck > /dev/null 2>&1
$VB src/tree det2.bck > /dev/null 2>&1
check '[ "$($VB det1.bck /LIST /FULL | grep -v "^Date:\|^Save set:\|^Command:")" = "$($VB det2.bck /LIST /FULL | grep -v "^Date:\|^Save set:\|^Command:")" ]' "two saves of one tree list differently"
$VB det1.bck det1 > /dev/null 2>&1
check '[ "$(python3 -c "import os; print(os.getxattr(\"det1/tree/a.txt\", \"user.aa\").decode() + os.getxattr(\"det1/tree/a.txt\", \"user.zz\").decode())" 2>/dev/null)" = 12 ] || [ -z "$(python3 -c "import os; print(os.listxattr(\"src/tree/a.txt\"))" 2>/dev/null | grep user.aa)" ]' "counted xattr names: not restored"
mkdir -p jdet && echo a > jdet/a && echo b > jdet/b
$VB jdet jd1.bck /RECORD /JOURNAL=jd1.jnl > /dev/null 2>&1
$VB jd1.bck /RECORD /JOURNAL=jd2.jnl > /dev/null 2>&1
#	(the bytes differ by the times of the commit; the files must not)
check '[ "$($VB /JOURNAL=jd1.jnl /LIST /FULL | grep jdet | sed "s/ [0-9-]*-20[0-9][0-9] [0-9:.]*//g")" = "$($VB /JOURNAL=jd2.jnl /LIST /FULL | grep jdet | sed "s/ [0-9-]*-20[0-9][0-9] [0-9:.]*//g")" ]' "the journal depends on the order its files came in"

#	/DATA_FORMAT=COMPRESSED: smaller, the same tree back, the same listing; deterministic
mkdir -p ztree && for i in $(seq 1 40); do yes "line $i of a text that compresses well" | head -c 150000 > ztree/t$i.txt; done
head -c 500000 /dev/urandom > ztree/rand.bin
$VB ztree zp.bck > /dev/null 2>&1
$VB ztree z1.bck /DATA_FORMAT=COMPRESSED /VERIFY > z1.log 2>&1
check '[ $? = 0 ]' "a compressed save failed: $(cat z1.log)"
$VB ztree z2.bck /DATA_FORMAT=COMPRESSED > /dev/null 2>&1
check '[ $(stat -c %s z1.bck) -lt $(( $(stat -c %s zp.bck) / 4 )) ]' "compressed $(stat -c %s z1.bck), plain $(stat -c %s zp.bck): it did not shrink"
check '[ $(stat -c %s z1.bck) = $(stat -c %s z2.bck) ]' "two compressed saves of one tree differ in size"
$VB z1.bck zout > /dev/null 2>&1
check '[ $? = 0 ] && diff -r ztree zout/ztree > /dev/null' "a compressed saveset does not restore the tree"
check '[ "$($VB z1.bck /LIST /FORMAT=LS)" = "$($VB zp.bck /LIST /FORMAT=LS)" ]' "compressed and plain list differently"
check '$VB z1.bck /EXTRACT=ztree/t7.txt | cmp -s - ztree/t7.txt && $VB z1.bck . /COMPARE > /dev/null 2>&1' "compressed: /EXTRACT or /COMPARE"
$VB ztree zbad.bck /DATA_FORMAT=SQUEEZED > zbad.log 2>&1
check '[ $? = 2 ] && [ ! -e zbad.bck ]' "/DATA_FORMAT=SQUEEZED accepted: $(cat zbad.log)"

#
#	11a. /PHYSICAL: an image file (anybody), loop devices (root, losetup, mkfs.ext4)
#
truncate -s 32M phys.img
head -c 3000000 /dev/urandom | dd of=phys.img bs=1M seek=7 conv=notrunc 2>/dev/null
printf 'a header at the start' | dd of=phys.img conv=notrunc 2>/dev/null
$VB phys.img phys.bck /PHYSICAL /VERIFY > phys.log 2>&1
check '[ $? = 0 ] && grep -q PHYSSUMM phys.log' "/PHYSICAL save of an image: $(cat phys.log)"
check '[ $(stat -c %s phys.bck) -lt 8000000 ]' "/PHYSICAL: the zeros took room ($(stat -c %s phys.bck) bytes)"
check '$VB phys.bck /LIST /FULL | grep -q "^Physical:"' "/PHYSICAL: the listing does not say so"
$VB phys.bck phys.out /PHYSICAL > physr.log 2>&1
check '[ $? = 0 ] && cmp -s phys.img phys.out' "/PHYSICAL restore into an image file: $(cat physr.log)"
$VB phys.bck physdir > /dev/null 2>&1
check 'cmp -s phys.img physdir/phys.img && [ $(du -k physdir/phys.img | cut -f1) -lt 8000 ]' "a /PHYSICAL saveset restored plainly is not the sparse image"
$VB phys.bck phys.out /PHYSICAL > physx.log 2>&1
check '[ $? = 2 ] && grep -q "OPENOUT.*errno: 17" physx.log' "/PHYSICAL restore over a file without /REPLACE: $(cat physx.log)"
$VB x.bck phys2.out /PHYSICAL > physn.log 2>&1
check '[ $? = 2 ] && grep -q PHYSNOTPHYS physn.log' "/PHYSICAL restore of a plain saveset: $(cat physn.log)"
$VB src phys3.bck /PHYSICAL > physd.log 2>&1
check '[ $? = 2 ] && grep -q PHYSNOTDEV physd.log' "/PHYSICAL of a directory: $(cat physd.log)"
$VB phys.img phys4.bck /PHYSICAL /SELECT=x > physq.log 2>&1
check '[ $? = 2 ] && grep -q CONFQUAL physq.log' "/PHYSICAL /SELECT accepted"
$VB phys.img physz.bck /PHYSICAL /DATA_FORMAT=COMPRESSED > /dev/null 2>&1 && $VB physz.bck physz.out /PHYSICAL > /dev/null 2>&1
check 'cmp -s phys.img physz.out' "/PHYSICAL compressed: not the same image"
[ -n "${VBKX:-}" ] && { $VBKX x phys.bck -C physvx > /dev/null 2>&1; check 'cmp -s phys.img physvx/phys.img' "vbkx: a /PHYSICAL saveset is not the image"; }

if [ $ROOT = 1 ] && command -v losetup > /dev/null 2>&1 && PATH=$PATH:/sbin:/usr/sbin command -v mkfs.ext4 > /dev/null 2>&1; then
	L1= L2= L3=
	#	udev looks at a device after every change of it, and holds it a moment: an umount then may fail - tried again
	settle () { command -v udevadm > /dev/null 2>&1 && udevadm settle 2>/dev/null; true; }
	umnt () { for i in 1 2 3 4 5; do settle; umount "$1" 2>/dev/null && return 0; mountpoint -q "$1" || return 0; sleep 1; done; umount -l "$1"; }
	freeloops () { for M in physmnt imgsrc imgdst; do mountpoint -q $M 2>/dev/null && umnt $M; done; for L in $L1 $L2 $L3; do losetup -d $L 2>/dev/null; done; }
	truncate -s 48M ploop.img && PATH=$PATH:/sbin:/usr/sbin mkfs.ext4 -q -F ploop.img
	head -c 64M /dev/urandom > pbig.img && cp pbig.img pbig.orig
	truncate -s 16M psmall.img
	L1=$(losetup -f --show ploop.img) && L2=$(losetup -f --show pbig.img) && L3=$(losetup -f --show psmall.img)
	settle
	if [ -n "$L3" ]; then
		mkdir -p physmnt
		mount $L1 physmnt && { $VB $L1 pl0.bck /PHYSICAL > pm.log 2>&1; check '[ $? = 2 ] && grep -q PHYSMOUNTED pm.log' "a device mounted read-write was saved: $(cat pm.log)"; umnt physmnt; }
		$VB $L1 pl.bck /PHYSICAL /VERIFY > pl.log 2>&1
		check '[ $? = 0 ]' "/PHYSICAL save of a loop device: $(cat pl.log)"
		$VB pl.bck pl.ref /PHYSICAL > /dev/null 2>&1
		$VB pl.bck $L2 /PHYSICAL > pr.log 2>&1
		check '[ $? = 2 ] && grep -q PHYSREPLACE pr.log' "a device overwritten without /REPLACE: $(cat pr.log)"
		$VB pl.bck $L3 /PHYSICAL /REPLACE > ps.log 2>&1
		check '[ $? = 2 ] && grep -q PHYSSMALL ps.log' "a smaller device written: $(cat ps.log)"
		$VB pl.bck $L2 /PHYSICAL /REPLACE < /dev/null > pw.log 2>&1
		check '[ $? = 0 ] && grep -q PHYSLARGER pw.log && cmp -s -n 50331648 pl.ref $L2' "/PHYSICAL restore onto a device over garbage: the gaps are not zeros, $(cat pw.log)"
		check 'cmp -s -i 50331648:50331648 -n 16777216 pbig.orig $L2' "/PHYSICAL restore wrote beyond the size of the device saved"
		mount -o ro $L2 physmnt && { $VB pl.bck $L2 /PHYSICAL /REPLACE > pt.log 2>&1; check '[ $? = 2 ] && grep -q PHYSMOUNTED pt.log' "a mounted device was overwritten"; umnt physmnt; }

		#	/IMAGE: the ext4 volume of L1 saved from its mount point, made again on L2, the same tree, label and UUID
		mkdir -p imgsrc imgdst
		if mount $L1 imgsrc; then
			mkdir -p imgsrc/d/e && echo one > imgsrc/d/a && ln imgsrc/d/a imgsrc/d/b && head -c 200000 /dev/urandom > imgsrc/d/e/r
			truncate -s 5M imgsrc/sparse && ln -s d/a imgsrc/l && chmod 0750 imgsrc && chown 1:1 imgsrc
			$VB imgsrc img.bck /IMAGE > img.log 2>&1
			check '[ $? = 0 ] && $VB img.bck /LIST /FULL | grep -q "^Kind"' "/IMAGE save of a mount point: $(cat img.log)"
			umnt imgsrc
			$VB img.bck $L2 /IMAGE /REPLACE < /dev/null > imgr.log 2>&1
			check '[ $? = 0 ] && grep -q IMGSUMM imgr.log' "/IMAGE restore onto a device: $(cat imgr.log)"
			mount -o ro $L1 imgsrc && mount -o ro $L2 imgdst && {
				check 'diff -r --no-dereference imgsrc imgdst > /dev/null && [ "$(stat -c %A%U imgsrc)" = "$(stat -c %A%U imgdst)" ]' "/IMAGE: the volume made again differs"
				check '[ "$(stat -c %i imgdst/d/a)" = "$(stat -c %i imgdst/d/b)" ]' "/IMAGE: a hard link lost"
				umnt imgdst; }
			umnt imgsrc 2>/dev/null
			check '[ "$(python3 -c "import sys; f=open(sys.argv[1],\"rb\"); f.seek(1024); s=f.read(256); print(s[0x68:0x88].hex())" $L1)" = "$(python3 -c "import sys; f=open(sys.argv[1],\"rb\"); f.seek(1024); s=f.read(256); print(s[0x68:0x88].hex())" $L2)" ]' "/IMAGE: the UUID and label are not those of the volume saved"
			$VB src imgn.bck /IMAGE > imgn.log 2>&1
			check '[ $? = 2 ] && grep -q IMGNOTVOL imgn.log' "/IMAGE of a directory that is not a mount point: $(cat imgn.log)"
			$VB pl.bck nothing.img /IMAGE > imgp.log 2>&1
			check '[ $? = 2 ] && grep -q IMGNOTIMG imgp.log' "/IMAGE restore of a saveset not made so: $(cat imgp.log)"
		fi
	else
		echo "%VBACKUP-W-SMOKE, no loop devices to be had: /PHYSICAL on devices not checked"
	fi
	freeloops
fi

#
#	11b. /ORIGINAL - back where the files came from; /DELETE - after /VERIFY, nothing changed
#
mkdir -p orig/one/sub orig/two && echo 1 > orig/one/a && echo 2 > orig/one/sub/b && echo 3 > orig/two/c
$VB orig/one,orig/two og.bck > /dev/null 2>&1
rm -rf orig/one orig/two
$VB og.bck /ORIGINAL > og.log 2>&1
check '[ $? = 0 ] && [ "$(cat orig/one/sub/b)" = 2 ] && [ "$(cat orig/two/c)" = 3 ] && [ "$(grep -c ORIGTARGET og.log)" = 2 ]' "/ORIGINAL, two bases: $(cat og.log)"
$VB og.bck ogout /ORIGINAL > ogo.log 2>&1
check '[ $? = 2 ] && grep -q QUALUSE ogo.log' "/ORIGINAL with an output accepted"
$VB orig/one dl0.bck /DELETE > dl0.log 2>&1
check '[ $? = 2 ] && grep -q QUALUSE dl0.log && [ -e orig/one/a ]' "/DELETE without /VERIFY: $(cat dl0.log)"
$VB orig/one dl.bck /VERIFY /DELETE > dl.log 2>&1
check '[ $? = 0 ] && [ ! -e orig/one/a ] && [ ! -e orig/one/sub/b ] && [ -d orig/one/sub ]' "/DELETE: the files saved and verified are not gone, $(cat dl.log)"
$VB dl.bck /ORIGINAL > /dev/null 2>&1
check '[ "$(cat orig/one/a)" = 1 ]' "after /DELETE the files do not come back with /ORIGINAL"
$VB orig/two - /VERIFY /DELETE > /dev/null 2> dlt.log; true
check '[ -e orig/two/c ] && grep -q "QUALUSE.*VERIFY" dlt.log' "/DELETE with the standard output (nothing to verify against) not refused: $(cat dlt.log)"
check '[ "$($VB x.bck /LIST /FORMAT=LS /SELECT=tree/a.txt 2> /dev/null | wc -l)" = 1 ]' "/LIST /SELECT listed other than the file chosen"

#
#	12. VBKX, the stand-alone extractor
#
VX=${VBKX:-}
if [ -n "$VX" ]; then
	$VX l x.bck > vx.lst 2>&1
	check '[ $? = 0 ] && grep -q " tree/a.txt$" vx.lst && grep -q " tree/link -> a.txt$" vx.lst' "vbkx l: $(head -3 vx.lst)"
	#	Against what VBACKUP restores from it: the tree has been changed since x.bck was made
	$VB x.bck vref > /dev/null 2>&1
	$VX x x.bck -C vx > vx.log 2>&1
	check '[ $? = 0 ] && same_tree vref/tree vx/tree' "vbkx x: the tree differs, $(cat vx.log)"
	check '[ "$(stat -c %i vx/tree/a.txt)" = "$(stat -c %i vx/tree/sub/hard)" ] && [ "$(du -k vx/tree/sparse | cut -f1)" -lt 1000 ]' "vbkx x: hard link or holes lost"
	$VX x x.bck -C vx > vx2.log 2>&1
	check '[ $? = 1 ] && grep -q "exists, not extracted" vx2.log' "vbkx x over files that are there: $(head -2 vx2.log)"
	check '$VX p x.bck tree/sub/rand.bin | cmp -s - vref/tree/sub/rand.bin && $VX p x.bck tree/sparse | cmp -s - vref/tree/sparse' "vbkx p: the data differs"
	check '$VX p x.bck tree/sub/hard | cmp -s - vref/tree/a.txt' "vbkx p of a further name of a file: not its data"
	$VX x x.bck -C vn tree/sub/hard > vn.log 2>&1
	check '[ $? = 0 ] && cmp -s vn/tree/sub/hard vref/tree/a.txt && [ ! -e vn/tree/a.txt ]' "vbkx x of a further name alone: $(cat vn.log)"
	$VX x z1.bck -C vz > /dev/null 2>&1
	check '[ $? = 0 ] && diff -r ztree vz/ztree > /dev/null && $VX t z1.bck > /dev/null 2>&1' "vbkx on a compressed saveset"
	$VX t x.bck > vt.log 2>&1
	check '[ $? = 0 ] && grep -q "all checksums match" vt.log' "vbkx t: $(cat vt.log)"
	mkdir -p evil/vx
	$VX x evil.bck -C evil/vx > vevil.log 2>&1
	check '[ $? = 1 ] && [ ! -e evil/evil ] && [ ! -e /tmp/vbackup-evil ] && [ ! -e evil/escape ]' "vbkx: a forged saveset wrote outside, $(cat vevil.log)"
	$VX p x.bck tree/none > /dev/null 2>&1
	check '[ $? = 2 ]' "vbkx p of a name not there: completion code"

	#	Every block lost in turn: by name, the file asked for, or nothing - never the next one
	#	(the 13 bytes more put a FILE record first in a block: as plain "file N" the layout never does)
	mkdir -p tiny/t && for i in $(seq 100 199); do echo "file $i xxxxxxxxxxxx" > tiny/t/f$i; done
	$VB tiny/t tiny.bck /BLOCK_SIZE=8192 /GROUP_SIZE=0 > /dev/null 2>&1
	N=$(( $(stat -c %s tiny.bck) / 8192 ))
	B=1
	WRONG=0
	while [ $B -lt $((N - 1)) ]; do
		cp tiny.bck tinyd.bck
		dd if=/dev/zero of=tinyd.bck bs=8192 seek=$B count=1 conv=notrunc 2>/dev/null
		for I in $(seq 100 199); do
			$VX p tinyd.bck t/f$I > tp.out 2> tp.log
			if [ -s tp.out ] && ! cmp -s tp.out tiny/t/f$I && ! grep -q "f$I - is incomplete" tp.log; then
				WRONG=$((WRONG + 1))
			fi
		done
		B=$((B + 1))
	done
	check '[ $WRONG = 0 ]' "vbkx p after a lost block: the data of another file, $WRONG times"

	#	A saveset cut down to its VHDR: nothing to read is not "all files read"
	head -c 65536 x.bck > vhdronly.bck
	$VX t vhdronly.bck > vh.log 2>&1
	check '[ $? = 1 ] && grep -q "ends before its catalog" vh.log' "vbkx t of a saveset cut down to its VHDR: $(tail -1 vh.log)"

fi

#
#	13. The completion codes of the command line
#
$VB > /dev/null 2>&1
check '[ $? = 0 ]' "no parameters: the summary, completion code 0"
$VB src/tree x2.bck /BLOCK_SIZE=1000 > bs.log 2>&1
check '[ $? = 2 ] && grep -q IVQUAL bs.log' "/BLOCK_SIZE=1000 accepted"

#	The words of the command: a third parameter, qualifiers glued to one, wildcards, .sav
$VB src/tree w1.sav .log > w1.log 2>&1
check '[ $? = 2 ] && grep -q MAXPARM w1.log && [ ! -e w1.sav ]' "a third parameter (.log for /LOG) was not refused"
$VB 'src/tree/.../*.txt' w2.sav/sav/log > w2.log 2>&1
check '[ $? = 0 ] && grep -q GLUED w2.log && grep -q SAVESUMM w2.log && [ -s w2.sav ]' "w2.sav/sav/log: the glued qualifiers not taken: $(head -2 w2.log)"
$VB w2.sav /LIST /FORMAT=LS > w2.lst 2> /dev/null
check 'grep -q "\.txt$" w2.lst && [ -z "$(grep "^-" w2.lst | grep -v "\.txt$")" ]' "a pattern saved more than its files: $(grep "^-" w2.lst | grep -v "\.txt$" | head -2)"
#	Qualifiers glued to one another and to nothing: /LIST/FULL is a listing (X01-12 restored into /LIST)
$VB w2.sav /LIST/FULL > w2f.lst 2> w2f.log
check '[ $? = 0 ] && grep -q "^Listing of save set" w2f.lst && ! grep -q "STARTED" w2f.log && [ ! -d /LIST/src ]' "/LIST/FULL glued: not a listing: $(head -2 w2f.log)"
mkdir -p wsav/sav && echo x > wsav/sav/f
$VB wsav/sav w3.bck > w3.log 2>&1
check '[ $? = 0 ] && ! grep -q GLUED w3.log' "a directory named sav taken for /SAVE_SET"
rm -rf wcp; $VB 'src/tree/.../*.txt' wcp > wcp.log 2>&1 && $VB 'src/tree/.../*.txt' src/tree/wcp2 > wcp2.log 2>&1
check '[ -z "$(find src/tree/wcp2 -path "*wcp2/*wcp2*" 2>/dev/null)" ] && [ -n "$(find wcp -name "*.txt")" ]' "a copy with a pattern copied into itself"
rm -rf src/tree/wcp2

#
#	14. /ENCRYPT (format.md 6.10): the passphrase from a key file
#
VBACKUP_KDFITER=1000
export VBACKUP_KDFITER
rm -rf etree; mkdir -p etree/sub/deeper
echo "секрет-имя" > "etree/sub/plaintext-marker-name.txt"; seq 1 20000 > etree/sub/deeper/numbers.txt
head -c 1500000 /dev/urandom > etree/random.bin; ln -s sub/deeper/numbers.txt etree/link
printf 'correct horse battery staple\n' > key && chmod 600 key
printf 'Correct horse battery staple\r\n' > bad && chmod 600 bad
$VB etree e1.bck /ENCRYPT /KEY_FILE=key /VERIFY /LOG > e1.log 2>&1
check '[ $? = 0 ] && grep -q ENCRYPTED e1.log && grep -q VERIFYING e1.log' "an encrypted save /VERIFY: $(grep -- -E- e1.log | head -2)"
check '! grep -q "plaintext-marker-name" e1.bck && ! grep -q "numbers.txt" e1.bck && ! grep -q "correct horse" e1.bck && ! grep -q "$(hostname)" e1.bck' "names, the host or the passphrase are in the clear in an encrypted saveset"
$VB e1.bck /LIST /KEY_FILE=key > e1.lst 2>&1
check '[ $? = 0 ] && grep -q "^Encryption: .*1000 iterations" e1.lst' "the listing of an encrypted saveset: $(head -2 e1.lst)"
check '[ "$($VB e1.bck /LIST /FORMAT=LS /KEY_FILE=key 2>/dev/null)" = "$($VB etree eplain.bck /REPLACE > /dev/null 2>&1; $VB eplain.bck /LIST /FORMAT=LS 2>/dev/null)" ]' "an encrypted saveset lists other than a plain one of the same tree"
rm -rf eo; $VB e1.bck eo /KEY_FILE=key > eo.log 2>&1
check '[ $? = 0 ] && diff -r --no-dereference etree eo/etree > /dev/null' "an encrypted saveset restored: $(head -2 eo.log)"
VBACKUP_KEY_FILE=$S/key $VB e1.bck /COMPARE > ec.log 2>&1
check '[ $? = 0 ]' "an encrypted saveset compared through VBACKUP_KEY_FILE: $(head -2 ec.log)"
rm -rf ew; $VB e1.bck ew /KEY_FILE=bad > ew.log 2>&1
check '[ $? = 2 ] && grep -q WRONGKEY ew.log && [ -z "$(ls -A ew 2>/dev/null)" ]' "a wrong passphrase: $(head -2 ew.log)"
$VB e1.bck /LIST < /dev/null > en.log 2>&1
check '[ $? = 2 ] && grep -q NOKEY en.log' "no key and no terminal: $(head -1 en.log)"
chmod 644 bad; $VB e1.bck /LIST /KEY_FILE=bad > em.log 2>&1
check '[ $? = 2 ] && grep -q "KEYFILE.*chmod 600" em.log' "a key file others may read was taken"
$VB e1.bck /LIST /ENCRYPT /KEY_FILE=key > eq.log 2>&1
check '[ $? = 2 ] && grep -q QUALUSE eq.log' "/ENCRYPT on a listing accepted"
$VB etree e2.bck /ENCRYPT /KEY_FILE=key /DATA_FORMAT=COMPRESSED /BLOCK_SIZE=16384 /VOLUME_SIZE=600000 > e2.log 2>&1
check '[ $? = 0 ] && [ -e e2.bck.002 ]' "an encrypted compressed save on volumes: $(head -2 e2.log)"
rm -rf e2o; VBACKUP_KEY_FILE=$S/key $VB e2.bck e2o > e2o.log 2>&1
check '[ $? = 0 ] && diff -r --no-dereference etree e2o/etree > /dev/null' "encrypted volumes restored: $(head -2 e2o.log)"

#	Damage: one block zapped and one forged with a right CRC, in other groups - both rebuilt
python3 - e2.bck 16384 << 'PYEOF'
import sys, zlib
f, B = sys.argv[1], int(sys.argv[2])
d = bytearray(open(f, "rb").read())
d[3 * B:4 * B] = bytes(B)
b = 14 * B
d[b + 64 + 200] ^= 0x20
h = bytearray(d[b:b + 64]); h[60:64] = bytes(4)
c = zlib.crc32(bytes(h) + bytes(d[b + 64:b + B])) & 0xffffffff
d[b + 60:b + 64] = c.to_bytes(4, "little")
open(f, "wb").write(d)
PYEOF
rm -rf e2d; VBACKUP_KEY_FILE=$S/key $VB e2.bck e2d > e2d.log 2>&1
#	A warning: the saveset was changed by somebody, though nothing of it is lost
check '[ $? = 1 ] && grep -q BLKFORGED e2d.log && [ "$(grep -c BLKFIXED e2d.log)" = 2 ] && diff -r --no-dereference etree e2d/etree > /dev/null' "a forged and a zapped block: $(grep -- '-[EW]-' e2d.log | head -3)"


#
#	15. A saveset through a pipe: "-" as the input, read once, forward only
#
$VB etree - > pipe.bck 2> pipe.log
check '[ $? = 0 ] && [ -s pipe.bck ]' "a save to the standard output: $(head -2 pipe.log)"
rm -rf po; cat pipe.bck | $VB - po > po.log 2>&1
check '[ $? = 0 ] && diff -r --no-dereference etree po/etree > /dev/null && ! grep -q -- "-W-" po.log' "a restore from a pipe: $(grep -- '-[EW]-' po.log | head -2)"
check '[ "$(cat pipe.bck | $VB - /LIST /FORMAT=LS 2>/dev/null)" = "$($VB pipe.bck /LIST /FORMAT=LS 2>/dev/null)" ]' "a listing from a pipe differs from the listing of the file"
cat pipe.bck | $VB - pe.out /EXTRACT=etree/sub/deeper/numbers.txt > pe.log 2>&1
check '[ $? = 0 ] && cmp -s pe.out etree/sub/deeper/numbers.txt' "/EXTRACT from a pipe: $(head -2 pe.log)"
$VB etree - /ENCRYPT /KEY_FILE=key /DATA_FORMAT=COMPRESSED 2> /dev/null > pipee.bck
rm -rf poe; cat pipee.bck | $VB - poe /KEY_FILE=key > poe.log 2>&1
check '[ $? = 0 ] && diff -r --no-dereference etree poe/etree > /dev/null' "an encrypted compressed saveset through a pipe: $(head -2 poe.log)"
check 'cat pipee.bck | $VB - /LIST /FULL /KEY_FILE=key 2> /dev/null | grep -q "^Node name:"' "an encrypted pipe: the full SUMMARY not taken from the stream"
python3 -c "import sys; d=bytearray(open('pipe.bck','rb').read()); B=65536; d[2*B:3*B]=bytes(B); open('pipez.bck','wb').write(d)"
rm -rf pz; cat pipez.bck | $VB - pz > pz.log 2>&1
check '[ $? = 0 ] && grep -q BLKFIXED pz.log && diff -r --no-dereference etree pz/etree > /dev/null' "a bad block in a pipe not repaired: $(grep -- '-[EW]-' pz.log | head -2)"
rm -rf pc; head -c 300000 pipe.bck | $VB - pc > pc.log 2>&1
check '[ $? = 2 ] && grep -q NOTRAILER pc.log' "a pipe cut short not reported: $(tail -2 pc.log)"
#	Volumes through a pipe, back to back; a saveset copied block for block - the same bytes, no key needed
rm -rf pmv; $VB etree - /VOLUME_SIZE=600000 /BLOCK_SIZE=16384 2> /dev/null | $VB - pmv > pmv.log 2>&1
check '[ $? = 0 ] && diff -r --no-dereference etree pmv/etree > /dev/null' "volumes through a pipe: $(grep -- '-[EW]-' pmv.log | head -2)"
rm -f xs.bck* xd.bck*
$VB etree xs.bck /VOLUME_SIZE=600000 /BLOCK_SIZE=16384 /DATA_FORMAT=COMPRESSED /ENCRYPT /KEY_FILE=key > /dev/null 2>&1
$VB xs.bck - 2> /dev/null | $VB - xd.bck > xd.log 2>&1
check '[ $? = 0 ] && [ -e xs.bck.002 ] && cmp -s xs.bck xd.bck && cmp -s xs.bck.002 xd.bck.002 && grep -q "XFRSUMM.*Bad: 0" xd.log' "a saveset through a pipe into its volume files, not the same bytes: $(tail -2 xd.log)"
python3 -c "import sys; d=bytearray(open('xs.bck','rb').read()); d[3*16384+99]^=1; open('xb.bck','wb').write(d)"; cp xs.bck.002 xb.bck.002; cp xs.bck.003 xb.bck.003 2>/dev/null; cp xs.bck.004 xb.bck.004 2>/dev/null; cp xs.bck.005 xb.bck.005 2>/dev/null
rm -f xbd.bck*; $VB xb.bck xbd.bck > xbd.log 2>&1
check '[ $? = 1 ] && grep -q BLKCOPIED xbd.log && cmp -s xb.bck xbd.bck' "a bad block not copied as it is, or not said: $(tail -2 xbd.log)"
$VB nosuch-input nosuch.bck > nsi.log 2>&1
check '[ $? = 2 ] && grep -q OPENIN nsi.log && [ ! -e nosuch.bck ]' "a save of what is not there made a saveset"

#	node::file, through a stand-in for ssh that runs the command here with this vbackup
mkdir -p rshbin && ln -sf "$VB" rshbin/vbackup
printf '#!/bin/sh\nshift\nPATH=%s:$PATH exec sh -c "$1"\n' "$S/rshbin" > fakessh && chmod +x fakessh
rm -rf remote && mkdir remote
VBACKUP_RSH=$S/fakessh $VB etree "node::$S/remote/r.bck" /VOLUME_SIZE=600000 /BLOCK_SIZE=16384 /VERIFY > rsh.log 2>&1
check '[ $? = 0 ] && [ -e remote/r.bck.002 ] && grep -q "CMPSUMM.*Differences: 0" rsh.log' "a save to node::file /VERIFY: $(grep -- '-[EW]-' rsh.log | head -2)"
rm -rf rsho; VBACKUP_RSH=$S/fakessh $VB "node::$S/remote/r.bck" rsho > rsho.log 2>&1
check '[ $? = 0 ] && diff -r --no-dereference etree rsho/etree > /dev/null' "a restore from node::file: $(grep -- '-[EW]-' rsho.log | head -2)"
VBACKUP_RSH=$S/fakessh $VB "node::$S/remote/nosuch.bck" /LIST > rshn.log 2>&1
check '[ $? = 2 ] && grep -q REMOTEERR rshn.log' "a saveset not there on the node: not reported"
if [ -n "$VBKX" ]; then
	rm -rf pvx; cat pipe.bck | $VBKX x - -C pvx > pvx.log 2>&1
	check '[ $? = 0 ] && diff -r --no-dereference etree pvx/etree > /dev/null' "vbkx x - from a pipe: $(head -2 pvx.log)"
	cat pipe.bck | $VBKX l - > pvl.log 2>&1
	check '[ $? = 0 ]' "vbkx l - from a pipe: $(tail -1 pvl.log)"
fi

if [ -n "$VBKX" ]; then
	rm -rf ex; $VBKX x e1.bck -C ex -k key > ex.log 2>&1
	check '[ $? = 0 ] && diff -r --no-dereference etree ex/etree > /dev/null' "vbkx -k: $(head -2 ex.log)"
	$VBKX t e2.bck -k bad > exb.log 2>&1 || true
	chmod 600 bad; $VBKX t e2.bck -k bad > exb.log 2>&1
	check '[ $? = 2 ] && grep -q "does not open" exb.log' "vbkx with a wrong passphrase: $(head -1 exb.log)"
	VBACKUP_KEY_FILE=$S/key $VBKX t e2.bck > ext.log 2>&1
	check '[ $? = 0 ] && grep -q "Block: .* - is not what was written" ext.log' "vbkx t of the forged saveset: $(head -2 ext.log)"
fi


tap_end
