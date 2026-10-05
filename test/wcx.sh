#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/wcx.sh
#
#	ABSTRACT:	The WCX plugin (plugins/wcx/vbkwcx.c) against VBKX:
#			the same listing, the same data - plain, compressed,
#			several volumes, a volume asked for, no catalog; a
#			sparse file, a hard link, a Cyrillic name with a space;
#			one bad block a group repaired, two named - never
#			silent damage; a forged saveset kept inside the output.
#
#	DESCRIPTION:	sh wcx.sh <harness> <plugin> [<harness> <plugin>...]
#
#			VBACKUP and VBKX name the Linux images, SCRATCH a
#			directory the script may fill and remove (KEEP=1 keeps
#			it).  The harness is test/wcx_harness.c built for the
#			plugin: the Linux .so natively; with WINE set, the
#			Windows ones (.wcx64, .wcx) under wine - a pair wine
#			cannot run (a 32-bit one without the i386 wine) is
#			said and passed over.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 4-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 5-OCT-2026	RRL	X-06 : An encrypted saveset: through VBACKUP_KEY_FILE, not
#					without it, not with a wrong passphrase.
#
#		 4-OCT-2026	RRL	X-01 : Initial version.
#
#---

VB=${VBACKUP:?"VBACKUP must name the image"}
VX=${VBKX:?"VBKX must name the stand-alone extractor"}
S=${SCRATCH:?"SCRATCH must name a scratch directory"}
WINE=${WINE:-}

export WINEDEBUG=-all TZ=UTC

TAPNAME=WCX
. "$(dirname "$0")/tap.sh"

[ $# -ge 2 ] || bail "usage: wcx.sh <harness> <plugin> [<harness> <plugin>...]"

cleanup () { rm -rf "$S"; }
[ -n "$KEEP" ] || trap cleanup EXIT

rm -rf "$S"
mkdir -p "$S" || exit 1
S=$(cd "$S" && pwd)
cd "$S" || exit 1

#	A file name as the harness takes it: itself, or Z:\... under wine
wp ()	{ if [ -n "$WINE" ]; then printf 'Z:%s' "$1" | tr / '\\'; else printf '%s' "$1"; fi; }

#	The harness on the plugin of this round, with a ceiling: a hang is a failure
H=; P=
wh ()	{ timeout 300 $WINE "$H" "$(wp "$P")" "$@"; }

#
#	The judge: every regular file VBKX extracted (<ref>) is in <out> and
#	the same, or is named by an ERR line of <log>; nothing is in <out>
#	that is not in <ref>.  A listing ended by an error (END) says that
#	files were lost: a missing one is then named.  "strict" - no ERR, no
#	END at all.  Links: made with the
#	same target on Linux, not made on Windows.
#
cat > judge.py <<'EOF'
import os, sys
ref, out, log, mode = sys.argv[1:5]
win = len(sys.argv) > 5 and sys.argv[5] == "win"
errs = set(l.split(" ", 2)[2].rstrip("\n") for l in open(log, errors="replace") if l.startswith("ERR "))
#	The listing ended by an error: records were lost, a file may not have been listed
lost = any(l.startswith("END ") for l in open(log, errors="replace"))
bad = []
if mode == "strict" and (errs or lost):
	bad.append("errors: " + ", ".join(sorted(errs)[:5]) + (" (the listing cut short)" if lost else ""))
for root, ds, fs in os.walk(ref):
	for f in fs:
		p = os.path.join(root, f)
		rel = os.path.relpath(p, ref)
		o = os.path.join(out, rel)
		if os.path.islink(p):
			if win:
				if os.path.lexists(o):
					bad.append("a link made on Windows: " + rel)
			elif rel not in errs and (not os.path.islink(o) or os.readlink(o) != os.readlink(p)):
				bad.append("link not the same: " + rel)
			continue
		if not os.path.isfile(p) or rel in errs:
			continue
		if not os.path.isfile(o):
			if not lost:
				bad.append("missing, not named: " + rel)
		elif open(p, "rb").read() != open(o, "rb").read():
			bad.append("differs, not named: " + rel)
for root, ds, fs in os.walk(out):
	for f in fs:
		rel = os.path.relpath(os.path.join(root, f), out)
		if not os.path.lexists(os.path.join(ref, rel)):
			bad.append("not in the saveset: " + rel)
for b in bad[:10]:
	print("  " + b)
sys.exit(1 if bad else 0)
EOF

#	"<size> <name>" of a listing: VBKX's (l -m) and the harness's
lvx ()	{ $VX l "$1" -m 2> /dev/null | sed -E 's/^.{19} +([0-9]+) .{5} /\1 /' | sort; }
lwh ()	{ grep -E '^[0-9a-f]{2} ' | cut -d' ' -f2,4- | sort; }

#
#	The tree: nothing Windows cannot hold - those names are vbkx.exe's
#	business (test/win.sh); the plugin does not list them there
#
T=src/tree
mkdir -p $T/sub/deep $T/empty-dir
echo hello > $T/a.txt
: > $T/empty
head -c 300000 /dev/urandom > $T/sub/rand.bin
head -c 3000000 /dev/urandom > $T/big.bin
seq 1 200000 > $T/text.txt
truncate -s 20M $T/sparse
printf 'middle' | dd of=$T/sparse bs=1 seek=9000000 conv=notrunc 2>/dev/null
ln $T/a.txt $T/sub/hard
ln -s a.txt $T/link
echo x > "$T/sub/deep/имя с пробелом"
echo ro > $T/readonly.txt && chmod 0444 $T/readonly.txt
echo dot > $T/.hidden
mkfifo $T/fifo
touch -d '2001-02-03 04:05:06' $T/a.txt

$VB src/tree plain.bck > /dev/null 2>&1
$VB src/tree packed.bck /DATA_FORMAT=COMPRESSED > /dev/null 2>&1
$VB src/tree vols.bck /BLOCK_SIZE=16384 /GROUP_SIZE=5 /VOLUME_SIZE=1048576 > /dev/null 2>&1
[ -e plain.bck ] && [ -e packed.bck ] && [ -e vols.bck.003 ] || bail "the savesets could not be made"

#	Encrypted (format.md 6.10): the plugin takes the passphrase from VBACKUP_KEY_FILE only
printf 'wcx passphrase\n' > key && chmod 600 key && printf 'not it\n' > badkey && chmod 600 badkey
VBACKUP_KDFITER=1000 VBACKUP_KEY_FILE=$S/key $VB src/tree enc.bck /ENCRYPT /DATA_FORMAT=COMPRESSED > /dev/null 2>&1
[ -e enc.bck ] || bail "the encrypted saveset could not be made"

#	No catalog: the TRAILER, the last block, cut off - the stream is read
cp plain.bck nocat.bck
truncate -s -"$(python3 -c 'import struct,sys; print(struct.unpack_from("<I", open(sys.argv[1], "rb").read(12), 8)[0])' plain.bck)" nocat.bck

#	What VBKX makes of them: the reference
for B in plain packed vols nocat; do
	$VX x $B.bck -C ref.$B > /dev/null 2>&1
	lvx $B.bck > $B.lx
done

#	Damage: one bad block a group (repaired), or two in some (named)
cat > dmg.py <<'EOF'
import os, sys, random
B, G = 16384, 5
r = random.Random(int(sys.argv[2]))
v = ["vols.bck"] + ["vols.bck.%03d" % i for i in range(2, 100) if os.path.exists("vols.bck.%03d" % i)]
for i, p in enumerate(v):
	n = os.path.getsize(p) // B
	last = n - 1 if i == len(v) - 1 else n
	pos = list(range(1, last))
	for g in range(0, len(pos), G + 1):
		grp = pos[g:g + G + 1]
		if not grp:
			continue
		hit = [r.choice(grp)] if sys.argv[1] == "fix" else (r.sample(grp, 2) if len(grp) >= 2 and r.random() < 0.4 else [])
		with open(p, "r+b") as f:
			for b in hit:
				f.seek(b * B + r.randrange(B - 64))
				f.write(os.urandom(64))
EOF
for MODE in fix lose; do
	for SEED in 1 2; do
		mkdir -p d.$MODE$SEED && cp vols.bck* d.$MODE$SEED/ && (cd d.$MODE$SEED && python3 ../dmg.py $MODE $SEED)
	done
done

#	A forged saveset: names that climb out, the Windows way too, a link to go through
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
stream += file(3, b'..\\evil2', 1) + data(3) + file(4, b'c:/evil3', 1) + data(4) + file(5, b'd/..\\..\\evil4', 1) + data(5)
stream += file(6, b'l', 3, b'..') + rec(4, tlv(1, struct.pack('<I', 6)))
stream += file(7, b'l/inside', 1) + data(7)
stream += file(8, b'm', 3, b'../escape') + rec(4, tlv(1, struct.pack('<I', 8)))
stream += file(9, b'm/inside', 1) + data(9) + rec(6, b'')
open(sys.argv[1], 'wb').write(blk(3, 0, 0, summ) + blk(1, 1, 0, stream))
PYEOF
echo "not a saveset" > text.txt

#
#	Each plugin
#
ROUND=0
while [ $# -ge 2 ]; do
	H=$1 P=$2
	shift 2
	ROUND=$((ROUND + 1))
	R=r$ROUND
	N=$(basename "$P")
	WIN=; [ -n "$WINE" ] && WIN=win

	#	Not to be run here (wine without i386): said, passed over
	timeout 300 $WINE "$H" > /dev/null 2>&1
	if [ $? != 2 ]; then
		echo "$N: $H does not run here - not tested"
		continue
	fi

	mkdir -p $R

	#	1. Known by its contents; the abilities
	check '[ "$(wh c "$(wp $S/plain.bck)")" = "CAPS 68 CAN 1" ]' "$N: CanYouHandleThisFile/GetPackerCaps on a saveset: $(wh c "$(wp $S/plain.bck)")"
	check '[ "$(wh c "$(wp $S/text.txt)")" = "CAPS 68 CAN 0" ]' "$N: CanYouHandleThisFile on a text file"
	wh l "$(wp $S/text.txt)" > $R/notbck.log 2>&1
	check '[ $? = 2 ] && grep -q "^OPEN 14" $R/notbck.log' "$N: a text file opened: $(cat $R/notbck.log)"

	#	2. Each saveset: the listing, everything extracted, tested
	for B in plain packed vols nocat; do
		wh l "$(wp $S/$B.bck)" > $R/$B.l 2> /dev/null
		RC=$?
		lwh < $R/$B.l > $R/$B.lw
		check '[ $RC = 0 ] && cmp -s $B.lx $R/$B.lw' "$N $B: the listing differs from VBKX: $(diff $B.lx $R/$B.lw | head -4)"

		wh x "$(wp $S/$B.bck)" "$(wp $S/$R/$B.out)" > $R/$B.x 2> $R/$B.xe
		RC=$?
		check '[ $RC = 0 ] && python3 judge.py ref.$B $R/$B.out $R/$B.x strict $WIN' "$N $B: extracted ($RC): $(cat $R/$B.x | head -3)"
		check 'grep -q "PROGRESS [1-9]" $R/$B.xe' "$N $B: no progress reported"

		wh t "$(wp $S/$B.bck)" > $R/$B.t 2>&1
		check '[ $? = 0 ]' "$N $B: test: $(head -3 $R/$B.t)"
	done

	#	Attributes, time, a sparse file, a hard link, read-only
	check 'grep -q "^10 [0-9]* 00000000 tree/sub$" $R/plain.l' "$N: a directory not 0x10"
	check 'grep -q "^21 3 " $R/plain.l && grep -q "^2[13] [0-9]* [0-9a-f]* tree/readonly.txt$" $R/plain.l' "$N: read-only not 0x01"
	check 'grep -q "^22 4 [0-9a-f]* tree/.hidden$" $R/plain.l' "$N: a dot file not hidden"
	check 'grep -q "^20 20971520 [0-9a-f]* tree/sparse$" $R/plain.l' "$N: the size of the sparse file"
	check '[ "$(stat -c %Y $R/plain.out/tree/a.txt)" = "$(stat -c %Y src/tree/a.txt)" ]' "$N: the time of a file not set"
	check '[ ! -w $R/plain.out/tree/readonly.txt ] || [ "$(id -u)" = 0 -a "$(stat -c %a $R/plain.out/tree/readonly.txt)" = 444 ] || [ -n "$WIN" ]' \
		"$N: read-only not set"
	check '[ ! -e $R/plain.out/tree/fifo ]' "$N: a FIFO made"

	#	3. By name: a further name of a file alone gets the data; Cyrillic and a space
	for B in plain nocat; do
		wh x "$(wp $S/$B.bck)" "$(wp $S/$R/h.$B)" tree/sub/hard > $R/h.$B.log 2>&1
		check '[ $? = 0 ] && cmp -s $R/h.$B/tree/sub/hard src/tree/a.txt && [ ! -e $R/h.$B/tree/a.txt ]' "$N $B: a further name alone: $(cat $R/h.$B.log)"
	done
	wh x "$(wp $S/packed.bck)" "$(wp $S/$R/named)" "tree/sub/deep/имя с пробелом" tree/text.txt > $R/named.log 2>&1
	check '[ $? = 0 ] && cmp -s "$R/named/tree/sub/deep/имя с пробелом" "src/tree/sub/deep/имя с пробелом" && cmp -s $R/named/tree/text.txt src/tree/text.txt && [ ! -e $R/named/tree/a.txt ]' \
		"$N: by name, Cyrillic and a space: $(cat $R/named.log)"

	#	The old functions: the same listing (UTF-8 is the "ANSI" of Linux)
	if [ -z "$WINE" ]; then
		wh -a l "$S/plain.bck" 2> /dev/null | lwh > $R/plain.la
		check 'cmp -s plain.lx $R/plain.la' "$N: the listing of ReadHeaderEx differs"
		wh -a x "$S/plain.bck" "$S/$R/ansi.out" > $R/ansi.x 2>&1
		check '[ $? = 0 ] && python3 judge.py ref.plain $R/ansi.out $R/ansi.x strict' "$N: ProcessFile: $(head -3 $R/ansi.x)"
	fi

	#	4. A volume not beside volume 1: asked for, given - all there; refused - named
	rm -rf $R/mv && mkdir -p $R/mv/away && cp vols.bck* $R/mv/ && mv $R/mv/vols.bck.002 $R/mv/away/
	wh -v "$(wp $S/$R/mv/away)" x "$(wp $S/$R/mv/vols.bck)" "$(wp $S/$R/mv.out)" > $R/mv.log 2>&1
	RC=$?
	check '[ $RC = 0 ] && grep -q "^ASK vols.bck.002$" $R/mv.log && python3 judge.py ref.vols $R/mv.out $R/mv.log strict $WIN' "$N: a volume asked for ($RC): $(head -3 $R/mv.log)"
	wh x "$(wp $S/$R/mv/vols.bck)" "$(wp $S/$R/mv2.out)" > $R/mv2.log 2>&1
	RC=$?
	check '[ $RC = 1 ] && grep -q "^ASK vols.bck.002$" $R/mv2.log && grep -q "^ERR 12 " $R/mv2.log && python3 judge.py ref.vols $R/mv2.out $R/mv2.log named $WIN' \
		"$N: a volume refused ($RC): $(head -3 $R/mv2.log)"

	#	The last volume elsewhere (another disk): no TRAILER beside volume 1, asked for
	LASTV=$(ls vols.bck.* | sort | tail -1)
	rm -rf $R/lv && mkdir -p $R/lv/away && cp vols.bck* $R/lv/ && mv $R/lv/$LASTV $R/lv/away/
	wh -v "$(wp $S/$R/lv/away)" x "$(wp $S/$R/lv/vols.bck)" "$(wp $S/$R/lv.out)" > $R/lv.log 2>&1
	RC=$?
	check '[ $RC = 0 ] && grep -q "^ASK $LASTV$" $R/lv.log && python3 judge.py ref.vols $R/lv.out $R/lv.log strict $WIN' "$N: the last volume asked for ($RC): $(head -3 $R/lv.log)"

	#	5. Damage: repaired - all there; beyond repair - named, never silent
	for D in fix1 fix2 lose1 lose2; do
		wh x "$(wp $S/d.$D/vols.bck)" "$(wp $S/$R/$D.out)" > $R/$D.log 2>&1
		RC=$?
		case $D in
		fix*)	check '[ $RC = 0 ] && python3 judge.py ref.vols $R/$D.out $R/$D.log strict $WIN' "$N $D: one bad block a group not repaired ($RC): $(head -3 $R/$D.log)" ;;
		*)	check '[ $RC -le 1 ] && python3 judge.py ref.vols $R/$D.out $R/$D.log named $WIN' "$N $D: silent damage or a crash ($RC)"
			check 'grep -q "^ERR 12 " $R/$D.log' "$N $D: two bad blocks and no file named" ;;
		esac
		wh t "$(wp $S/d.$D/vols.bck)" > $R/$D.t 2>&1
		RC=$?
		case $D in
		fix*)	check '[ $RC = 0 ]' "$N $D: test ($RC): $(head -3 $R/$D.t)" ;;
		*)	check '[ $RC = 1 ]' "$N $D: test of a damaged saveset ($RC)" ;;
		esac
	done

	#	6. Forged: nothing outside the output directory
	rm -rf evil && mkdir -p evil
	wh x "$(wp $S/evil.bck)" "$(wp $S/evil/out)" > $R/evil.log 2>&1
	RC=$?
	check '[ $RC -le 1 ] && [ ! -e evil/evil ] && [ ! -e evil/evil2 ] && [ ! -e evil/evil3 ] && [ ! -e evil/evil4 ] && [ ! -e evil/inside ] && [ ! -e evil/escape ] && [ ! -e /tmp/vbackup-evil ]' \
		"$N: forged ($RC): written outside the output directory: $(ls -R evil | head -5)"
	#	A link made by one run is not gone through by the next (Linux; Windows makes none)
	wh x "$(wp $S/evil.bck)" "$(wp $S/evil/out2)" l > $R/evil2.log 2>&1
	wh x "$(wp $S/evil.bck)" "$(wp $S/evil/out2)" l/inside > $R/evil3.log 2>&1
	RC=$?
	check '[ ! -e evil/inside ] && { [ -n "$WIN" ] || { [ $RC = 1 ] && [ -L evil/out2/l ] && grep -q "^ERR 16 l/inside$" $R/evil3.log; }; }' \
		"$N: forged, two runs ($RC): written through a link of the first: $(cat $R/evil3.log)"

	#	7. Encrypted: with the key file - as the plain one; without - not opened; a wrong passphrase - a bad archive
	VBACKUP_KEY_FILE="$(wp $S/key)" wh x "$(wp $S/enc.bck)" "$(wp $S/$R/enc.out)" > $R/enc.log 2>&1
	RC=$?
	check '[ $RC = 0 ] && python3 judge.py ref.plain $R/enc.out $R/enc.log strict $WIN' "$N: an encrypted saveset ($RC): $(head -3 $R/enc.log)"
	env -u VBACKUP_KEY_FILE timeout 300 $WINE "$H" "$(wp "$P")" l "$(wp $S/enc.bck)" > $R/enc0.log 2>&1
	check 'grep -q "^OPEN 15$" $R/enc0.log' "$N: an encrypted saveset opened without a key: $(head -2 $R/enc0.log)"
	VBACKUP_KEY_FILE="$(wp $S/badkey)" wh l "$(wp $S/enc.bck)" > $R/encb.log 2>&1
	check 'grep -q "^OPEN 13$" $R/encb.log' "$N: an encrypted saveset opened with a wrong passphrase: $(head -2 $R/encb.log)"

	wh l "$(wp $S/evil.bck)" > $R/evil.l 2>&1
	check '! grep -qE "\.\./evil|vbackup-evil|evil2|evil4" $R/evil.l && { [ -z "$WIN" ] || ! grep -q evil3 $R/evil.l; }' \
		"$N: forged: a name that climbs out listed: $(grep evil $R/evil.l)"
done

tap_end
