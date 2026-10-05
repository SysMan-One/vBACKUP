#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/win.sh
#
#	ABSTRACT:	VBKX.EXE, the stand-alone extractor built for Windows,
#			run under wine against savesets VBACKUP made on Linux:
#			the same listing as VBKX, the same data, the names
#			Windows cannot hold refused and said, damage repaired
#			or named, a forged saveset kept inside the output.
#
#	DESCRIPTION:	VBACKUP and VBKX name the Linux images, VBKXEXE the
#			Windows one, WINE the wine to run it (default: wine),
#			SCRATCH a directory the script may fill and remove.
#			KEEP=1 keeps it.  Every run of wine costs a second or
#			two; the whole takes about a minute.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 4-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 5-OCT-2026	RRL	X-03 : The messages of vbkx in their new form, "File: name - text".
#
#		 4-OCT-2026	RRL	X-02 : TAP=1 - the Test Anything Protocol (test/tap.sh).
#
#		 4-OCT-2026	RRL	X-01 : Initial version.
#
#---

VB=${VBACKUP:?"VBACKUP must name the image"}
VX=${VBKX:?"VBKX must name the stand-alone extractor"}
VW=${VBKXEXE:?"VBKXEXE must name vbkx.exe"}
WINE=${WINE:-wine}
S=${SCRATCH:?"SCRATCH must name a scratch directory"}

export WINEDEBUG=-all TZ=UTC

#	ok, fail, check, bail, tap_end - plain output, or TAP with TAP=1
TAPNAME=WIN
. "$(dirname "$0")/tap.sh"

#	vbkx.exe under wine, with a ceiling: a hang is a failure, not a wait
vw ()	{ timeout 300 $WINE "$VW" "$@"; }

cleanup () { rm -rf "$S"; }
[ -n "$KEEP" ] || trap cleanup EXIT

rm -rf "$S"
mkdir -p "$S" || exit 1
cd "$S" || exit 1

#
#	The judge: every file of the tree whose name Windows can hold is in
#	the output and the same, or is named by a message; the others are not
#	there and are named "not a valid name on Windows"
#
cat > judge.py <<'EOF'
import os, re, sys

def winok(rel):
	for c in rel.split("/"):
		if any(ord(ch) < 32 or ch in '<>:"\\|?*' for ch in c) or c.endswith((".", " ")):
			return False
		b = c.split(".")[0].upper()
		if b in ("CON", "PRN", "AUX", "NUL") or (len(b) == 4 and b[:3] in ("COM", "LPT") and b[3] in "123456789"):
			return False
	return True

src, out, log = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(log, errors="replace").read()
bad = []
for root, ds, fs in os.walk(src):
	for f in fs:
		p = os.path.join(root, f)
		rel = os.path.relpath(p, os.path.dirname(src))
		o = os.path.join(out, rel)
		# Links and FIFOs have checks of their own; the judge looks at regular files
		if os.path.islink(p) or not os.path.isfile(p):
			continue
		if not winok(rel):
			if os.path.exists(o):
				bad.append("made, though Windows cannot hold the name: " + rel)
			elif ("File: " + rel + " - not a valid name on Windows") not in text and ("File: " + rel + " - not extracted") not in text \
					and not re.search(r"cannot all be named|names cannot be looked up", text):
				bad.append("refused without a word: " + rel)
			continue
		if ("File: " + rel + " - is incomplete") in text or ("File: " + rel + " - not extracted") in text:
			continue
		if not os.path.exists(o):
			if not re.search(r"cannot all be named|names cannot be looked up", text):
				bad.append("missing, not named: " + rel)
			continue
		if open(p, "rb").read() != open(o, "rb").read():
			bad.append("differs, not named: " + rel)
for b in bad[:10]:
	print("  " + b)
sys.exit(1 if bad else 0)
EOF

#
#	The tree: what Windows takes, and what it does not
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
echo 1 > "$T/a:b"
echo 2 > "$T/con.txt"
echo 3 > "$T/x?"
echo 4 > "$T/trail."
mkfifo $T/fifo
touch -d '2001-02-03 04:05:06' $T/a.txt

$VB src/tree plain.bck > /dev/null 2>&1
$VB src/tree packed.bck /DATA_FORMAT=COMPRESSED > /dev/null 2>&1
$VB src/tree vols.bck /BLOCK_SIZE=16384 /GROUP_SIZE=5 /VOLUME_SIZE=1048576 > /dev/null 2>&1
check '[ -e plain.bck ] && [ -e packed.bck ] && [ -e vols.bck.003 ]' "the savesets could not be made"

#
#	1. Each saveset: list, extract, test, print
#
for B in plain packed vols; do
	$VX l $B.bck > $B.lx 2>&1
	vw l $B.bck > $B.lw 2> $B.lwe
	check '[ $? = 0 ] && cmp -s $B.lx $B.lw' "$B: the listing differs from VBKX: $(diff $B.lx $B.lw | head -3)"

	vw x $B.bck -C $B.out > $B.xw 2>&1
	RC=$?
	check '[ $RC = 1 ]' "$B: x completed $RC, not 1 (names refused, a FIFO not made)"
	check 'python3 judge.py src/tree $B.out $B.xw' "$B: the extracted tree"
	check '[ "$(stat -c %i $B.out/tree/a.txt)" = "$(stat -c %i $B.out/tree/sub/hard)" ]' "$B: the hard link is a copy"
	check '[ "$(stat -c %Y $B.out/tree/a.txt)" = "$(stat -c %Y src/tree/a.txt)" ]' "$B: the time of a file"
	check '[ "$(stat -c %Y $B.out/tree/sub)" = "$(stat -c %Y src/tree/sub)" ]' "$B: the time of a directory"
	check '[ ! -w $B.out/tree/readonly.txt ] || [ "$(id -u)" = 0 -a "$(stat -c %a $B.out/tree/readonly.txt)" = 444 ]' "$B: read-only not set"
	check 'grep -q "fifo - a FIFO, not made on Windows" $B.xw' "$B: the FIFO not said"

	vw t $B.bck > $B.tw 2>&1
	check '[ $? = 0 ] && grep -q "all checksums match" $B.tw' "$B: t, $(cat $B.tw)"

	vw p $B.bck tree/big.bin > $B.pw 2>/dev/null
	check 'cmp -s $B.pw src/tree/big.bin' "$B: p of big.bin"
	vw p $B.bck tree/sparse > $B.ps 2>/dev/null
	check 'cmp -s $B.ps src/tree/sparse' "$B: p of the sparse file"
done

#	By name, through the catalog; a name with a space and Cyrillic on the command line
vw x plain.bck -C named "tree/sub/deep/имя с пробелом" tree/text.txt > named.log 2>&1
check '[ $? = 0 ] && cmp -s "named/tree/sub/deep/имя с пробелом" "src/tree/sub/deep/имя с пробелом" && cmp -s named/tree/text.txt src/tree/text.txt && [ ! -e named/tree/a.txt ]' \
	"x by name: $(cat named.log)"

#	Over what is there: kept, 1; with -f: replaced
vw x plain.bck -C named tree/text.txt > again.log 2>&1
check '[ $? = 1 ] && grep -q "already exists, not extracted" again.log' "x over a file that is there: $(cat again.log)"
echo changed > named/tree/text.txt
vw x plain.bck -C named -f tree/text.txt > force.log 2>&1
check '[ $? = 0 ] && cmp -s named/tree/text.txt src/tree/text.txt' "x -f: $(cat force.log)"

#
#	2. Damage: one bad block a group is repaired, two are named
#
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
				f.seek(b * B + r.randrange(B - 64))	# inside the one block
				f.write(os.urandom(64))
EOF
for MODE in fix lose; do
	for SEED in 1 2 3; do
		rm -rf d && mkdir d && cp vols.bck* d/ && (cd d && python3 ../dmg.py $MODE $SEED)
		vw x d/vols.bck -C d.out > d.log 2>&1
		RC=$?
		check '[ $RC -le 2 ]' "$MODE $SEED: x crashed or hung, completion code $RC"
		check 'python3 judge.py src/tree d.out d.log' "$MODE $SEED: silent damage"
		[ $MODE = fix ] && check '! grep -q "cannot be rebuilt" d.log && grep -q "rebuilt from its group" d.log' "fix $SEED: not all repaired: $(grep -v rebuilt d.log | head -3)"
		rm -rf d.out
	done
done

#
#	3. A forged saveset: names that climb out, the Windows way too
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
stream += file(3, b'..\\evil2', 1) + data(3) + file(4, b'c:/evil3', 1) + data(4) + file(5, b'd/..\\..\\evil4', 1) + data(5)
stream += file(6, b'l', 3, b'..') + rec(4, tlv(1, struct.pack('<I', 6)))
stream += file(7, b'l/inside', 1) + data(7) + rec(6, b'')
open(sys.argv[1], 'wb').write(blk(3, 0, 0, summ) + blk(1, 1, 0, stream))
PYEOF
mkdir -p evil
vw x evil.bck -C evil/out > evil.log 2>&1
RC=$?
check '[ $RC = 1 ]' "forged: completion code $RC, $(cat evil.log)"
check '[ ! -e evil/evil ] && [ ! -e evil/evil2 ] && [ ! -e evil/evil3 ] && [ ! -e evil/evil4 ] && [ ! -e evil/inside ] && [ ! -e /tmp/vbackup-evil ]' \
	"forged: written outside the output directory"
check 'grep -q "evil2 - not a valid name on Windows" evil.log && grep -q "evil3 - not a valid name on Windows" evil.log' "forged: the Windows names not refused, $(cat evil.log)"

#
#	4. The command line
#
vw > /dev/null 2>&1
check '[ $? = 2 ]' "no parameters: completion code"
vw l nosuch.bck > /dev/null 2>&1
check '[ $? = 2 ]' "a saveset not there: completion code"

tap_end
