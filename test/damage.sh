#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/damage.sh
#
#	ABSTRACT:	The saveset under damage: random bytes, whole blocks,
#			cut and missing volumes, blocks moved about - more and
#			more at random than smoke.sh does by hand.
#
#	DESCRIPTION:	One tree is saved once, in small blocks and groups and
#			over several volumes.  Every round damages a copy of
#			the saveset in one of three ways and restores it:
#
#			FIX	at most one block of a group - inside the groups,
#				header or payload: every block is repaired, the
#				tree comes back whole, no BLKLOST;
#			LOSE	two or more blocks of some groups: blocks are lost,
#			CHAOS	anything - the VHDRs, the TRAILER, cut volumes, a
#				missing volume, blocks swapped or copied over:
#				for both, no crash and no hang, and above all no
#				silent damage - a file that is restored and not
#				named by FILDAMAGED is the file that was saved; a
#				file not restored at all is named by FILLOST, or
#				UNNAMED says the catalog cannot name them all.
#
#			/LIST and /COMPARE of the damaged saveset must not crash
#			either.  ROUNDS (default 30) rounds, SEED (default 1)
#			makes a run repeatable; a failing round prints its mode
#			and seed.  VBACKUP names the image, SCRATCH a directory
#			the script may fill and remove.  KEEP=1 keeps it.  A
#			sanitizer build of the image is the best use of it.
#			VBKX, when it names the stand-alone extractor, goes
#			through every round as well.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 3-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 5-OCT-2026	RRL	X-08 : The judge reads the messages in their new form,
#					"File: name - text".
#
#		 5-OCT-2026	RRL	X-06 : Every fourth round damages the tree saved
#					/ENCRYPT and seals the spoilt blocks again:
#					a right CRC, a wrong TAG.
#
#		 4-OCT-2026	RRL	X-04 : TAP=1 - the Test Anything Protocol (test/tap.sh).
#
#		 4-OCT-2026	RRL	X-04 : Every other round damages the same tree saved
#					/DATA_FORMAT=COMPRESSED; every third file of
#					the tree compresses.
#
#		 3-OCT-2026	RRL	X-01 : Initial version; VBKX too.
#
#---

VB=${VBACKUP:?"VBACKUP must name the image"}
VX=${VBKX:-}
S=${SCRATCH:?"SCRATCH must name a scratch directory"}
ROUNDS=${ROUNDS:-30}
SEED=${SEED:-1}

BSZ=16384
GRP=5
VOLSZ=1048576

#	ok, fail, check, bail, tap_end - plain output, or TAP with TAP=1
TAPNAME=DAMAGE
. "$(dirname "$0")/tap.sh"

cleanup () { rm -rf "$S"; }
[ -n "$KEEP" ] || trap cleanup EXIT

rm -rf "$S"
mkdir -p "$S" || exit 1
cd "$S" || exit 1

#
#	The damage itself, and the judgement of a restore: in Python, the
#	only tool the tests use besides the shell
#
cat > dmg.py <<'EOF'
import os, random, re, sys, glob, zlib

BSZ, GRP = int(os.environ["BSZ"]), int(os.environ["GRP"])

def vols(d):
	v = [d + "/x.bck"] + sorted(glob.glob(d + "/x.bck.[0-9][0-9][0-9]"))
	return [p for p in v if os.path.exists(p)]

def groupblocks(path, last):
	"""Positions of the blocks inside groups: not the VHDR, not the TRAILER"""
	n = os.path.getsize(path) // BSZ
	return list(range(1, n - 1 if last else n))

FORGE = os.environ.get("DMG_FORGE") == "1"

def reseal(f, pos):
	"""The CRC of a spoilt block made right again: only its TAG can tell (an encrypted saveset)"""
	f.seek(pos * BSZ); b = bytearray(f.read(BSZ))
	if len(b) == BSZ and b[0:4] == b"VBKB":
		b[60:64] = bytes(4)
		b[60:64] = (zlib.crc32(bytes(b)) & 0xffffffff).to_bytes(4, "little")
		f.seek(pos * BSZ); f.write(b)

def hit(path, pos, rnd):
	"""Spoil one block: a few bytes, a run of zeros, or garbage all over; DMG_FORGE=1 - and seal it again"""
	with open(path, "r+b") as f:
		spoil(f, pos, rnd)
		if FORGE:
			reseal(f, pos)

def spoil(f, pos, rnd):
	if True:
		k = rnd.randrange(3)
		if k == 0:
			for _ in range(rnd.randint(1, 8)):
				f.seek(pos * BSZ + rnd.randrange(BSZ)); f.write(bytes([rnd.randrange(256)]))
		elif k == 1:
			off = rnd.randrange(BSZ); f.seek(pos * BSZ + off); f.write(b"\0" * rnd.randint(1, BSZ - off))
		else:
			f.seek(pos * BSZ); f.write(os.urandom(BSZ))

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
					o = rnd.randrange(max(1, os.path.getsize(p))); f.seek(o); f.write(os.urandom(rnd.randint(1, 3 * BSZ)))
					if FORGE:
						for b in range(o // BSZ, o // BSZ + 4):
							reseal(f, b)

def judge(src, out, log):
	"""Silent damage is the one thing never allowed"""
	text = open(log, errors="replace").read()
	# A damaged file is named as it lies in the output directory
	# A damaged file is named as it lies in the output directory (VBACKUP) or by its stored name (VBKX)
	relo = lambda n: os.path.relpath(n, out) if n.startswith(out + "/") else n
	named = set(relo(n) for n in re.findall(r"FILDAMAGED, File: (.+?) - is incomplete", text))
	named |= set(relo(n) for n in re.findall(r"vbkx: (.+?) is incomplete", text))
	lost = set(relo(n) for n in re.findall(r"FILLOST, File: (.+?) - not restored", text))
	lost |= set(relo(n) for n in re.findall(r"vbkx: (.+?) was not extracted", text))
	bad = []
	for root, ds, fs in os.walk(src):
		for f in fs:
			rel = os.path.relpath(os.path.join(root, f), os.path.dirname(src))
			o = os.path.join(out, rel)
			if rel in named:
				continue
			if not os.path.exists(o):
				# Not restored: named by FILLOST - or the catalog could not name it, and that was said
				if rel not in lost and not re.search(r"UNNAMED|NOTSAVESET|-E-OPENIN|cannot all be named|is not a saveset", text):
					bad.append("missing, not named: " + rel)
				continue
			if open(os.path.join(root, f), "rb").read() != open(o, "rb").read():
				bad.append("differs, not named: " + rel)
	for b in bad[:10]:
		print("  " + b)
	return 1 if bad else 0

def pick(src, seed):
	"""A few files of the tree, by their stored names"""
	allf = sorted(os.path.relpath(os.path.join(r, f), os.path.dirname(src)) for r, ds, fs in os.walk(src) for f in fs)
	print(" ".join(random.Random(seed).sample(allf, 4)))

def judgen(src, out, log, names):
	"""Extraction by name: the names asked for, judged as a whole pass is; nothing else made"""
	bad = []
	text = open(log, errors="replace").read()
	for root, ds, fs in os.walk(out):
		for f in fs:
			rel = os.path.relpath(os.path.join(root, f), out)
			if rel not in names:
				bad.append("made, not asked for: " + rel)
	for n in names:
		o = os.path.join(out, n)
		if "vbkx: " + n + " is incomplete" in text:
			continue
		if not os.path.exists(o):
			if ("vbkx: " + n + " was not extracted") not in text and not re.search(r"cannot all be named|names cannot be looked up|is not a saveset", text):
				bad.append("missing, not named: " + n)
			continue
		if open(os.path.join(os.path.dirname(src), n), "rb").read() != open(o, "rb").read():
			bad.append("differs, not named: " + n)
	for b in bad[:10]:
		print("  " + b)
	return 1 if bad else 0

if sys.argv[1] == "pick":
	pick(sys.argv[2], int(sys.argv[3]))
elif sys.argv[1] == "judgen":
	sys.exit(judgen(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:]))
elif sys.argv[1] == "damage":
	damage(sys.argv[2], sys.argv[3], int(sys.argv[4]))
elif sys.argv[1] == "judge":
	sys.exit(judge(sys.argv[2], sys.argv[3], sys.argv[4]))
elif sys.argv[1] == "tree":
	rnd = random.Random(int(sys.argv[3]))
	for i in range(int(sys.argv[4])):
		d = os.path.join(sys.argv[2], "d%02d" % (i % 13), "s%d" % (i % 3))
		os.makedirs(d, exist_ok=True)
		size = rnd.choice([0, 1, 100, 5000, BSZ - 1, BSZ, 3 * BSZ + 7, 70000, 250000, 900000])
		# Every third file compresses (text), the rest does not: a compressed saveset holds both DATAZ and DATA
		if i % 3 == 0:
			line = ("line %d of file %d, some words to repeat\n" % (rnd.randrange(1000), i)).encode()
			data = (line * (size // len(line) + 1))[:size]
		else:
			data = rnd.randbytes(size)
		open(os.path.join(d, "f%04d" % i), "wb").write(data)
EOF

export BSZ GRP
python3 dmg.py tree src/tree "$SEED" 300 || bail "the tree could not be made"

mkdir -p base basez basee
$VB src/tree base/x.bck /BLOCK_SIZE=$BSZ /GROUP_SIZE=$GRP /VOLUME_SIZE=$VOLSZ > save.log 2>&1
[ $? = 0 ] && [ -e base/x.bck.003 ] || bail "the saveset could not be made: $(cat save.log)"

#	The same tree compressed: every other round damages this one - garbage inside a DATAZ body is the new case
$VB src/tree basez/x.bck /BLOCK_SIZE=$BSZ /GROUP_SIZE=$GRP /VOLUME_SIZE=$VOLSZ /DATA_FORMAT=COMPRESSED > savez.log 2>&1
[ $? = 0 ] && [ -e basez/x.bck.002 ] || bail "the compressed saveset could not be made: $(cat savez.log)"

#	The same tree encrypted and compressed (format.md 6.10): every fourth round damages this one and
#	seals every spoilt block again - a right CRC over a wrong block, only the TAG tells
printf 'damage passphrase\n' > key && chmod 600 key
VBACKUP_KEY_FILE=$S/key VBACKUP_KDFITER=1000
export VBACKUP_KEY_FILE VBACKUP_KDFITER
$VB src/tree basee/x.bck /BLOCK_SIZE=$BSZ /GROUP_SIZE=$GRP /VOLUME_SIZE=$VOLSZ /DATA_FORMAT=COMPRESSED /ENCRYPT > savee.log 2>&1
[ $? = 0 ] && [ -e basee/x.bck.002 ] || bail "the encrypted saveset could not be made: $(cat savee.log)"
$VB basee/x.bck re0 > re0.log 2>&1
python3 dmg.py judge src/tree re0 re0.log > /dev/null && [ "$(grep -c BLK re0.log)" = 0 ]
[ $? = 0 ] && ok "the undamaged encrypted saveset restores" || fail "the undamaged encrypted saveset does not restore: $(head -3 re0.log)"

#	The undamaged saveset first: the judge itself must agree with it
$VB base/x.bck r0 > r0.log 2>&1
python3 dmg.py judge src/tree r0 r0.log > /dev/null && [ "$(grep -c BLK r0.log)" = 0 ]
[ $? = 0 ] && ok "the undamaged saveset restores" || fail "the undamaged saveset does not restore"

#	A completion code from a signal (>= 128) or a hang (124) is a crash; so is a report of a sanitizer in the log
crashed () { [ "$1" -ge 124 ] || grep -q "Sanitizer\|runtime error:" "$2"; }

r=1
while [ $r -le "$ROUNDS" ]; do
	case $((r % 3)) in
		1) MODE=FIX ;;
		2) MODE=LOSE ;;
		0) MODE=CHAOS ;;
	esac
	RS=$((SEED * 1000 + r))

	rm -rf d out
	BASE=base
	[ $((r % 2)) = 0 ] && BASE=basez
	[ $((r % 4)) = 3 ] && BASE=basee
	cp -r $BASE d
	[ $BASE = basee ] && DMG_FORGE=1 || DMG_FORGE=0
	DMG_FORGE=$DMG_FORGE python3 dmg.py damage d $MODE $RS

	#	A forged block repaired is a warning still: the saveset was changed by somebody
	FIXRC=0
	[ $BASE = basee ] && grep -q BLKFORGED rst.log 2>/dev/null && FIXRC=1

	timeout 120 $VB d/x.bck out > rst.log 2>&1
	RC=$?
	[ $BASE = basee ] && grep -q BLKFORGED rst.log && FIXRC=1

	if crashed $RC rst.log; then
		fail "round $r $MODE seed $RS: the restore crashed or hung, completion code $RC"
	elif ! python3 dmg.py judge src/tree out rst.log; then
		fail "round $r $MODE seed $RS: silent damage"
	elif [ $MODE = FIX ] && { grep -q BLKLOST rst.log || [ $RC != $FIXRC ]; }; then
		fail "round $r FIX seed $RS: not all repaired, completion code $RC: $(grep -v BLKFIXED rst.log | head -3)"
	else
		ok "round $r $MODE seed $RS: restore${DMG_FORGE:+ ($BASE)}"
	fi

	#	The same damage through the stand-alone extractor
	if [ -n "$VX" ]; then
		rm -rf vout
		timeout 120 $VX x d/x.bck -C vout > vx.log 2>&1
		RC=$?

		if crashed $RC vx.log; then
			fail "round $r $MODE seed $RS: vbkx crashed or hung, completion code $RC"
		elif ! python3 dmg.py judge src/tree vout vx.log; then
			fail "round $r $MODE seed $RS: vbkx, silent damage"
		elif [ $MODE = FIX ] && [ $RC != 0 ]; then
			fail "round $r FIX seed $RS: vbkx did not repair all, completion code $RC: $(grep -v repaired vx.log | head -3)"
		else
			ok "round $r $MODE seed $RS: vbkx x"
		fi

		#	By name, through the catalog: the right files or none, never another one
		NAMES=$(python3 dmg.py pick src/tree $RS)
		rm -rf vn
		timeout 120 $VX x d/x.bck -C vn $NAMES > vn.log 2>&1
		RC=$?

		if crashed $RC vn.log; then
			fail "round $r $MODE seed $RS: vbkx x by name crashed or hung, completion code $RC"
		elif ! python3 dmg.py judgen src/tree vn vn.log $NAMES; then
			fail "round $r $MODE seed $RS: vbkx x by name, silent damage"
		else
			ok "round $r $MODE seed $RS: vbkx x by name"
		fi

		ONE=${NAMES%% *}
		timeout 120 $VX p d/x.bck $ONE > vp.out 2> vp.log
		RC=$?

		if crashed $RC vp.log; then
			fail "round $r $MODE seed $RS: vbkx p crashed or hung, completion code $RC"
		elif [ -s vp.out ] && ! cmp -s vp.out src/$ONE && ! grep -q "$ONE is incomplete" vp.log; then
			fail "round $r $MODE seed $RS: vbkx p wrote other data, unnamed: $(head -2 vp.log)"
		elif [ $RC = 0 ] && ! cmp -s vp.out src/$ONE; then
			fail "round $r $MODE seed $RS: vbkx p completed with the wrong data"
		else
			ok "round $r $MODE seed $RS: vbkx p"
		fi

		timeout 120 $VX l d/x.bck > q.log 2>&1
		RC=$?
		crashed $RC q.log && fail "round $r $MODE seed $RS: vbkx l crashed or hung, completion code $RC" || ok "round $r $MODE seed $RS: vbkx l"
	fi

	for Q in "/LIST" "/LIST /FULL" "/COMPARE"; do
		if [ "$Q" = "/COMPARE" ]; then
			timeout 120 $VB d/x.bck src $Q > q.log 2>&1
		else
			timeout 120 $VB d/x.bck $Q > q.log 2>&1
		fi
		RC=$?
		crashed $RC q.log && fail "round $r $MODE seed $RS: $Q crashed or hung, completion code $RC" || ok "round $r $MODE seed $RS: $Q"
	done

	r=$((r + 1))
done

tap_end
