#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/geeks.sh
#
#	ABSTRACT:	The extractors of last resort, vbkx-go, vbkx-rs and vbkx-pl,
#			against VBKX: the same listing (in UTC), the same tree,
#			the same checksums; and under damage - one bad block a
#			group repaired, more lost and named, anything at all
#			survived without a crash, a hang or silent damage; a
#			volume 1 without its first block; a forged saveset;
#			the same output twice for the same input.
#
#	DESCRIPTION:	VBACKUP and VBKX name the images, VBKXGO and VBKXRS the
#			extractors, VBKXPL the Perl one (vbkx.pl, run by perl;
#			any of them may be missing), SCRATCH a directory
#			the script may fill and remove.  ROUNDS (default 12)
#			damage rounds, SEED (default 1).  KEEP=1 keeps it.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 4-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 4-OCT-2026	RRL	X-04 : TAP=1 - the Test Anything Protocol (test/tap.sh).
#
#		 4-OCT-2026	RRL	X-03 : DATAZ: the tree saved /DATA_FORMAT=COMPRESSED read the
#					same as vbkx; a saveset of the reference LZ4
#					compressor (python3-lz4, all modes); the rounds
#					of damage on the compressed saveset too, with
#					garbage inside the DATAZ bodies, blocks resealed.
#
#		 4-OCT-2026	RRL	X-02 : VBKXPL, the extractor in Perl; a Perl warning
#					in the log counts as a crash.
#
#		 4-OCT-2026	RRL	X-01 : Initial version.
#
#---

VB=${VBACKUP:?"VBACKUP must name the image"}
VX=${VBKX:?"VBKX must name the stand-alone extractor"}
S=${SCRATCH:?"SCRATCH must name a scratch directory"}
ROUNDS=${ROUNDS:-12}
SEED=${SEED:-1}

BSZ=8192
GRP=4
export BSZ GRP

#	ok, fail, check, bail, tap_end - plain output, or TAP with TAP=1
TAPNAME=GEEKS
. "$(dirname "$0")/tap.sh"

cleanup () { rm -rf "$S"; }
[ -n "$KEEP" ] || trap cleanup EXIT

rm -rf "$S"
mkdir -p "$S" || exit 1
cd "$S" || exit 1

#
#	The damage and the judge, as in damage.sh, with the names of these two
#
cat > g.py <<'EOF'
import os, random, re, sys, glob

BSZ, GRP = int(os.environ["BSZ"]), int(os.environ["GRP"])

def vols(d):
	v = [d + "/x.bck"] + sorted(glob.glob(d + "/x.bck.[0-9][0-9][0-9]"))
	return [p for p in v if os.path.exists(p)]

def groupblocks(path, last):
	n = os.path.getsize(path) // BSZ
	return list(range(1, n - 1 if last else n))

def hit(path, pos, rnd):
	with open(path, "r+b") as f:
		k = rnd.randrange(3)
		if k == 0:
			for _ in range(rnd.randint(1, 8)):
				f.seek(pos * BSZ + rnd.randrange(BSZ)); f.write(bytes([rnd.randrange(256)]))
		elif k == 1:
			off = rnd.randrange(BSZ); f.seek(pos * BSZ + off); f.write(b"\0" * rnd.randint(1, BSZ - off))
		else:
			f.seek(pos * BSZ); f.write(rnd.randbytes(BSZ))

def damage(d, mode, seed):
	rnd = random.Random(seed)
	v = vols(d)
	if mode == "FIX":
		for i, p in enumerate(v):
			pos = groupblocks(p, i == len(v) - 1)
			for g in range(0, len(pos), GRP + 1):
				if rnd.random() < 0.5:
					hit(p, rnd.choice(pos[g:g + GRP + 1]), rnd)
	elif mode == "LOSE":
		for i, p in enumerate(v):
			pos = groupblocks(p, i == len(v) - 1)
			for g in range(0, len(pos), GRP + 1):
				grp = pos[g:g + GRP + 1]
				if len(grp) >= 2 and rnd.random() < 0.3:
					for b in rnd.sample(grp, rnd.randint(2, len(grp))):
						hit(p, b, rnd)
	elif mode == "VHDR":
		with open(v[0], "r+b") as f:
			f.write(b"\0" * BSZ)
	else:
		for _ in range(rnd.randint(1, 6)):
			p = rnd.choice(v)
			n = os.path.getsize(p) // BSZ
			k = rnd.randrange(6)
			if k == 0 and n:
				hit(p, rnd.randrange(n), rnd)
			elif k == 1:
				os.truncate(p, rnd.randrange(os.path.getsize(p) + 1))
			elif k == 2 and p != v[0]:
				os.unlink(p); v.remove(p)
			elif k == 3 and n >= 2:
				a, b = rnd.sample(range(n), 2)
				with open(p, "r+b") as f:
					f.seek(a * BSZ); x = f.read(BSZ); f.seek(b * BSZ); y = f.read(BSZ)
					f.seek(a * BSZ); f.write(y); f.seek(b * BSZ); f.write(x)
			elif k == 4 and n >= 2:
				a, b = rnd.sample(range(n), 2)
				with open(p, "r+b") as f:
					f.seek(a * BSZ); x = f.read(BSZ); f.seek(b * BSZ); f.write(x)
			elif k == 5:
				with open(p, "r+b") as f:
					f.seek(rnd.randrange(max(1, os.path.getsize(p)))); f.write(rnd.randbytes(rnd.randint(1, 3 * BSZ)))

def zbody(d, seed):
	"""Garbage inside the compressed bytes of DATAZ records, the blocks resealed:
	the block CRC does not catch it, so the decoder and the file CRC must"""
	import struct, zlib
	rnd = random.Random(seed)
	blocks = []				# (volume, position, payload length) of the DATA blocks, in order
	for p in vols(d):
		with open(p, "rb") as f:
			raw = f.read()
		for pos in range(len(raw) // BSZ):
			h = raw[pos * BSZ:pos * BSZ + 64]
			if h[:4] == b"VBKB" and h[12] == 1:
				blocks.append((p, pos, struct.unpack_from("<I", h, 48)[0]))
	stream, where = bytearray(), []
	for p, pos, plen in blocks:
		with open(p, "rb") as f:
			f.seek(pos * BSZ + 64); stream += f.read(plen)
		where += [(p, pos, 64 + i) for i in range(plen)]
	targets, off = [], 0
	while off + 8 <= len(stream):			# the records: type, flags, length
		typ, _, ln = struct.unpack_from("<HHI", stream, off)
		if typ == 7 and ln > 20:
			targets += range(off + 8 + 20, min(off + 8 + ln, len(stream)))
		off += 8 + ln
	if not targets:
		return
	touched = set()
	for i in rnd.sample(targets, min(len(targets), rnd.randint(1, 6))):
		p, pos, at = where[i]
		with open(p, "r+b") as f:
			f.seek(pos * BSZ + at); f.write(bytes([rnd.randrange(256)]))
		touched.add((p, pos))
	for p, pos in touched:
		with open(p, "r+b") as f:
			f.seek(pos * BSZ); b = bytearray(f.read(BSZ))
			b[60:64] = b"\0\0\0\0"
			struct.pack_into("<I", b, 60, zlib.crc32(bytes(b)))
			f.seek(pos * BSZ); f.write(b)

def lz4ref(path, outdir):
	"""A saveset made by hand whose DATAZ records come from the reference LZ4
	compressor (python3-lz4), in all its modes: every decoder must read them"""
	import struct, zlib, lz4.block
	B = 8192; P = B - 64; UU = b"\x22" * 16; NONE = 0xFFFFFFFF
	def tlv(t, v): return struct.pack("<HI", t, len(v)) + v
	def rec(t, body): return struct.pack("<HHI", t, 0, len(body)) + body
	rnd = random.Random(99)
	files = [("tree/runs", b"A" * 300000 + b"B" * 70000),
		 ("tree/text", b"".join(b"line %d of a text that repeats itself\n" % (i % 50) for i in range(20000))),
		 ("tree/mixed", bytes(rnd.randrange(4) for _ in range(200000))),
		 ("tree/random", rnd.randbytes(5000)),
		 ("tree/short", b"abc"),
		 ("tree/overlap", b"ab" * 100000 + b"abc" * 70000)]
	modes = [dict(mode="default"), dict(mode="fast", acceleration=8), dict(mode="high_compression", compression=12)]
	recs = [rec(1, tlv(71, struct.pack("<I", 0)))]
	os.makedirs(outdir, exist_ok=True)
	for no, (name, data) in enumerate(files, 1):
		open(os.path.join(outdir, os.path.basename(name)), "wb").write(data)
		recs.append(rec(2, tlv(1, struct.pack("<I", no)) + tlv(2, name.encode()) + tlv(3, b"\x01") + tlv(4, struct.pack("<I", 0o644))
			+ tlv(5, struct.pack("<I", 0)) + tlv(6, struct.pack("<I", 0)) + tlv(9, struct.pack("<Q", len(data))) + tlv(10, struct.pack("<qI", 0, 0))))
		for k, at in enumerate(range(0, len(data), 1 << 20)):
			chunk = data[at:at + (1 << 20)]
			z = lz4.block.compress(chunk, store_size=False, **modes[(no + k) % 3])
			recs.append(rec(7, struct.pack("<IIQI", no, 1, at, len(chunk)) + z))
		recs.append(rec(4, tlv(1, struct.pack("<I", no)) + tlv(9, struct.pack("<Q", len(data))) + tlv(32, struct.pack("<I", zlib.crc32(data)))))
	recs.append(rec(6, b""))
	def blk(typ, no, recoff, pay):
		h = b"VBKB" + struct.pack("<HHIBBH", 64, 1, B, typ, 0, 0) + UU + struct.pack("<QIIIIII", no, 1, recoff, len(pay), NONE, 0, 0)
		b = bytearray(h + pay + bytes(P - len(pay)))
		struct.pack_into("<I", b, 60, zlib.crc32(bytes(b)))
		return bytes(b)
	out, cur, recoff, no = [blk(3, 0, 0, recs[0])], b"", NONE, 1
	for r in recs:					# a record header is never split over two blocks
		if P - len(cur) < 8:
			out.append(blk(1, no, recoff, cur)); no += 1; cur, recoff = b"", NONE
		if recoff == NONE:
			recoff = len(cur)
		while r:
			n = min(len(r), P - len(cur)); cur += r[:n]; r = r[n:]
			if len(cur) == P:
				out.append(blk(1, no, recoff, cur)); no += 1; cur, recoff = b"", NONE
	if cur:
		out.append(blk(1, no, recoff, cur))
	open(path, "wb").write(b"".join(out))

def judge(src, out, log):
	"""Silent damage is the one thing never allowed"""
	text = open(log, errors="replace").read()
	named = set(re.findall(r"vbkx-(?:go|rs|pl): (.+?) is incomplete", text))
	lost = set(re.findall(r"vbkx-(?:go|rs|pl): (.+?) was not extracted", text))
	unnamed = re.search(r"cannot all be named|is not a saveset", text)
	bad = []
	for root, ds, fs in os.walk(src):
		for f in fs:
			rel = os.path.relpath(os.path.join(root, f), os.path.dirname(src))
			o = os.path.join(out, rel)
			if rel in named:
				continue
			if not os.path.lexists(o):
				if rel not in lost and not unnamed:
					bad.append("missing, not named: " + rel)
				continue
			if os.path.islink(o) or not os.path.isfile(o):
				continue
			if open(os.path.join(root, f), "rb").read() != open(o, "rb").read():
				bad.append("differs, not named: " + rel)
	for b in bad[:10]:
		print("  " + b)
	return 1 if bad else 0

if sys.argv[1] == "damage" and sys.argv[3] == "ZBODY":
	zbody(sys.argv[2], int(sys.argv[4]))
elif sys.argv[1] == "damage":
	damage(sys.argv[2], sys.argv[3], int(sys.argv[4]))
elif sys.argv[1] == "lz4ref":
	lz4ref(sys.argv[2], sys.argv[3])
else:
	sys.exit(judge(sys.argv[2], sys.argv[3], sys.argv[4]))
EOF

#
#	The tree: every kind of file these extractors put back
#
T=src/tree
mkdir -p $T/sub/deep $T/empty-dir
echo hello > $T/a.txt
: > $T/empty
head -c 300000 /dev/urandom > $T/sub/rand.bin
head -c 100000 /dev/urandom > $T/big.bin
truncate -s 5M $T/sparse
printf 'middle' | dd of=$T/sparse bs=1 seek=2000000 conv=notrunc 2>/dev/null
ln -s a.txt $T/link
ln -s /nonexistent/target $T/dangling
ln $T/a.txt $T/sub/hard
mkfifo $T/fifo
echo x > "$T/sub/deep/имя с пробелом"
for i in $(seq 1 60); do echo "small $i" > $T/sub/s$i; done
yes "a line that compresses well, again and again" | head -c 400000 > $T/text.txt
head -c 250000 /dev/zero | tr '\0' 'z' > $T/sub/runs.bin
chmod 4750 $T/big.bin
chmod 0700 $T/sub/deep
touch -d '2001-02-03 04:05:06.789' $T/a.txt $T/sub/deep

mkdir base
$VB src/tree base/x.bck /BLOCK_SIZE=$BSZ /GROUP_SIZE=$GRP /VOLUME_SIZE=200000 > save.log 2>&1
[ $? = 0 ] && [ -e base/x.bck.003 ] || bail "the saveset could not be made: $(cat save.log)"

TZ=UTC $VX l base/x.bck > ref.lst 2>&1
$VX x base/x.bck -C ref > ref.log 2>&1

#	The same tree compressed (/DATA_FORMAT=COMPRESSED, DATAZ records)
mkdir zbase
$VB src/tree zbase/x.bck /BLOCK_SIZE=$BSZ /GROUP_SIZE=$GRP /VOLUME_SIZE=200000 /DATA_FORMAT=COMPRESSED > zsave.log 2>&1
[ $? = 0 ] && [ -e zbase/x.bck.002 ] || bail "the compressed saveset could not be made: $(cat zsave.log)"
[ "$(cat zbase/x.bck* | wc -c)" -lt "$(cat base/x.bck* | wc -c)" ] || bail "the compressed saveset is not smaller"
TZ=UTC $VX l zbase/x.bck > zref.lst 2>&1
$VX x zbase/x.bck -C zref > zref.log 2>&1

#	A saveset made by hand from the reference LZ4 compressor, if python3-lz4 is there
LZREF=0
python3 -c "import lz4.block" 2>/dev/null && python3 g.py lz4ref lzref.bck lzref.src && LZREF=1

#	The state of a tree: types, modes, sizes, times, link targets (the top itself left out)
state () { ( cd "$1" && find . -mindepth 1 -printf '%y %m %s %T@ %l %P\n' | sort ); }
same () {
	[ "$(state "$1")" = "$(state "$2")" ] || return 1
	( cd "$1" && find . -type f ) | while read -r F; do cmp -s "$1/$F" "$2/$F" || return 1; done
}

#	A completion code from a signal or a hang is a crash; so is a Go or Rust trace, or a Perl warning
crashed () { [ "$1" -ge 124 ] || grep -q "^panic:\|^goroutine \|panicked at\|internal error\| at .* line [0-9][0-9]*\.$" "$2"; }

#	"name command": the Perl one is run by perl
for NG in "vbkx-go ${VBKXGO:-}" "vbkx-rs ${VBKXRS:-}" "vbkx-pl ${VBKXPL:+perl $VBKXPL}"; do
	N=${NG%% *}
	G=${NG#* }
	[ -n "$G" ] && [ "$G" != "$N" ] || continue

	#	1. The same as VBKX
	$G l base/x.bck > $N.lst 2> $N.lerr
	check '[ $? = 0 ] && cmp -s $N.lst ref.lst' "$N l: the listing differs from vbkx l, $(diff ref.lst $N.lst | head -4)"
	$G x base/x.bck -C $N.out > $N.log 2>&1
	check '[ $? = 0 ] && same ref $N.out' "$N x: the tree differs from vbkx x, $(state ref > s1; state $N.out > s2; diff s1 s2 | head -4) $(head -3 $N.log)"
	check '[ "$(stat -c %i $N.out/tree/a.txt)" = "$(stat -c %i $N.out/tree/sub/hard)" ] && [ "$(du -k $N.out/tree/sparse | cut -f1)" -lt 1000 ]' \
		"$N x: hard link or holes lost"
	$G x base/x.bck -C $N.out > $N.log2 2>&1
	check '[ $? = 1 ] && grep -q "was not extracted: it exists" $N.log2' "$N x over the files that are there: $(head -2 $N.log2)"
	$G t base/x.bck > $N.t 2>&1
	check '[ $? = 0 ] && grep -q "all checksums match" $N.t' "$N t: $(cat $N.t)"
	$G q base/x.bck > /dev/null 2>&1
	check '[ $? = 2 ]' "$N: a command that is none, completion code"

	#	1z. The compressed saveset: the same as VBKX, the same tree as uncompressed
	$G l zbase/x.bck > $N.zlst 2> $N.zlerr
	check '[ $? = 0 ] && cmp -s $N.zlst zref.lst' "$N l of DATAZ: the listing differs from vbkx l, $(diff zref.lst $N.zlst | head -4)"
	$G x zbase/x.bck -C $N.zout > $N.zlog 2>&1
	check '[ $? = 0 ] && same zref $N.zout && same ref $N.zout' "$N x of DATAZ: the tree differs, $(head -3 $N.zlog)"
	$G t zbase/x.bck > $N.zt 2>&1
	check '[ $? = 0 ] && grep -q "all checksums match" $N.zt' "$N t of DATAZ: $(cat $N.zt)"

	#	1r. DATAZ records of the reference compressor, in its default, fast and high modes
	if [ $LZREF = 1 ]; then
		$G x lzref.bck -C $N.lz > $N.lzlog 2>&1
		RC=$?
		W=0
		for F in runs text mixed random short overlap; do cmp -s lzref.src/$F $N.lz/tree/$F || W=$((W + 1)); done
		check '[ $RC = 0 ] && [ $W = 0 ]' "$N: the blocks of the reference LZ4 not read right ($W files), $(head -3 $N.lzlog)"
	fi

	#	2. Volume 1 without its VHDR: the block size and the group size found by trying
	rm -rf d && cp -r base d && python3 g.py damage d VHDR 0
	$G x d/x.bck -C $N.vh > $N.vh.log 2>&1
	check '[ $? = 0 ] && same ref $N.vh && grep -q "found by trying" $N.vh.log' "$N: volume 1 without its VHDR, $(head -3 $N.vh.log)"

	#	3. The rounds of damage: on the plain saveset, then on the compressed one - there also
	#	   garbage inside the compressed bytes of DATAZ records with the blocks resealed (ZBODY),
	#	   which the block CRC cannot catch: the decoder and the file CRC must
	for BASE in base zbase; do
	r=1
	while [ $r -le "$ROUNDS" ]; do
		case $((r % 3)) in
			1) MODE=FIX ;;
			2) MODE=LOSE ;;
			0) MODE=CHAOS ;;
		esac
		[ $BASE = zbase ] && [ $((r % 4)) = 0 ] && MODE=ZBODY
		RS=$((SEED * 1000 + r))
		rm -rf d o o2
		cp -r $BASE d
		python3 g.py damage d $MODE $RS

		timeout 120 $G x d/x.bck -C o > x.log 2>&1
		RC=$?
		if crashed $RC x.log; then
			fail "$N $BASE round $r $MODE seed $RS: crashed or hung, completion code $RC: $(tail -3 x.log)"
		elif ! python3 g.py judge src/tree o x.log; then
			fail "$N $BASE round $r $MODE seed $RS: silent damage"
		elif [ $MODE = FIX ] && { [ $RC != 0 ] || ! same ref o; }; then
			fail "$N $BASE round $r FIX seed $RS: not all repaired, completion code $RC: $(grep -v repaired x.log | head -3)"
		else
			ok "$N $BASE round $r $MODE seed $RS: x"
		fi

		#	The same input, the same output: twice, byte for byte (into the same name: errors carry it)
		rm -rf o
		timeout 120 $G x d/x.bck -C o > x2.log 2>&1
		check 'cmp -s x.log x2.log' "$N $BASE round $r $MODE seed $RS: two runs of x say different things"
		for Q in l t; do
			timeout 120 $G $Q d/x.bck > q1.log 2>&1
			RC=$?
			timeout 120 $G $Q d/x.bck > q2.log 2>&1
			if crashed $RC q1.log; then
				fail "$N $BASE round $r $MODE seed $RS: $Q crashed or hung, completion code $RC"
			elif ! cmp -s q1.log q2.log; then
				fail "$N $BASE round $r $MODE seed $RS: two runs of $Q say different things"
			else
				ok "$N $BASE round $r $MODE seed $RS: $Q, twice the same"
			fi
		done
		r=$((r + 1))
	done
	done

	#	4. A forged saveset: names that climb out, a link to go through
	mkdir -p evil.$N/out
	python3 - evil.$N/x.bck <<'PYEOF'
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
stream = summ + file(1, b'../evil', 1) + data(1) + file(2, b'/tmp/vbackup-geek-evil', 1) + data(2)
stream += file(3, b'l', 3, b'../escape') + rec(4, tlv(1, struct.pack('<I', 3)))
stream += file(4, b'l/inside', 1) + data(4) + file(5, b'h', 4, b'../evil') + rec(4, tlv(1, struct.pack('<I', 5))) + rec(6, b'')
open(sys.argv[1], 'wb').write(blk(3, 0, 0, summ) + blk(1, 1, 0, stream))
PYEOF
	$G x evil.$N/x.bck -C evil.$N/out > evil.$N.log 2>&1
	check '[ $? = 1 ] && [ ! -e evil.$N/evil ] && [ ! -e /tmp/vbackup-geek-evil ] && [ ! -e evil.$N/escape ] && [ ! -e evil.$N/out/l/inside ]' \
		"$N: a forged saveset wrote outside, $(cat evil.$N.log)"
done

tap_end
