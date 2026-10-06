#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/winutil.sh
#
#	ABSTRACT:	VBACKUP.EXE, the utility built for Windows, run under
#			wine, both ways: savesets made on Linux listed,
#			restored, compared by it; savesets made by it listed
#			and restored on Linux.  The names Windows cannot hold
#			refused and said; encryption, parity, volumes, the
#			journal and the incremental chain, a pipe.
#
#	DESCRIPTION:	VBACKUP names the Linux image, VBACKUPEXE the Windows
#			one, WINE the wine to run it (default: wine), OBJDIR
#			the objects of vbackup.exe and NM the nm of MinGW-w64
#			(the imports are looked at: no ANSI call of the C
#			library), UNITSEXE test/units.c built for Windows,
#			SCRATCH a directory the script may fill and remove.
#			KEEP=1 keeps it.
#
#			Not here, wine cannot show them: the links of NTFS
#			(wine follows the links of Linux and makes none), the
#			ACL, a console without echo for the passphrase.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 6-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 6-OCT-2026	RRL	X-01 : Initial version.
#
#---

VB=${VBACKUP:?"VBACKUP must name the image"}
VW=${VBACKUPEXE:?"VBACKUPEXE must name vbackup.exe"}
WINE=${WINE:-wine}
S=${SCRATCH:?"SCRATCH must name a scratch directory"}

export WINEDEBUG=-all TZ=UTC

#	ok, fail, check, bail, tap_end - plain output, or TAP with TAP=1
TAPNAME=WINUTIL
. "$(dirname "$0")/tap.sh"

#	vbackup.exe under wine, with a ceiling: a hang is a failure, not a wait
vw ()	{ timeout 300 $WINE "$VW" "$@"; }

cleanup () { rm -rf "$S"; }
[ -n "$KEEP" ] || trap cleanup EXIT

rm -rf "$S"
mkdir -p "$S" || exit 1
cd "$S" || exit 1

#	Two trees the same: names, sizes, contents, modification times to the second
state () {
	( cd "$1" && {
	find . -type f -printf 'f %s %TY%Tm%Td%TH%TM%TS %P\n' | sed 's/\.[0-9]* / /'
	find . -type d -printf 'd %P\n'
	} | sort )
}

same_tree () {
	[ "$(state "$1")" = "$(state "$2")" ] || { diff "$(state "$1" > s1.tmp; echo s1.tmp)" "$(state "$2" > s2.tmp; echo s2.tmp)" | head -5; return 1; }

	( cd "$1" && find . -type f ) | while read -r F; do
		cmp -s "$1/$F" "$2/$F" || { echo "  differs: $F"; return 1; }
	done
}

#
#	0. The image: none of the ANSI calls of the C library (a name in the
#	code page, a descriptor in text mode) in the objects of vbackup.exe
#
if [ -n "$OBJDIR" ] && [ -n "$NM" ]; then
	BAD=$(find "$OBJDIR" -name '*.obj' -exec "$NM" -u {} \; 2>/dev/null | awk '{print $2}' \
		| grep -xE '_?(open|stat|stat64|stati64|fopen|tmpfile|remove|rename|mkdir|rmdir|unlink|access|chmod|chdir|getcwd|fullpath|utime|utime64|findfirst|findfirst64|getenv|opendir)' | sort -u | tr '\n' ' ')
	check '[ -z "$BAD" ]' "ANSI calls of the C library in vbackup.exe: $BAD"
fi

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
ln $T/a.txt $T/sub/hard
echo x > "$T/sub/deep/имя с пробелом"
echo ro > $T/readonly.txt && chmod 0444 $T/readonly.txt
echo 1 > "$T/a:b"
echo 2 > "$T/con.txt"
echo 3 > "$T/x?"
echo 4 > "$T/trail."
mkfifo $T/fifo
touch -d '2001-02-03 04:05:06' $T/a.txt
head -c 64 /dev/urandom > key && chmod 600 key

#	The tree as Windows can have it: the reference of what comes back
mkdir -p ref && cp -a $T ref/ && rm -f "ref/tree/a:b" ref/tree/con.txt "ref/tree/x?" ref/tree/trail. ref/tree/fifo

$VB src/tree plain.bck > /dev/null 2>&1
$VB src/tree packed.bck /DATA_FORMAT=COMPRESSED > /dev/null 2>&1
$VB src/tree vols.bck /BLOCK_SIZE=16384 /GROUP_SIZE=5 /VOLUME_SIZE=1048576 /PARITY=2 > /dev/null 2>&1
$VB src/tree crypt.bck /ENCRYPT /KEY_FILE=key > /dev/null 2>&1
check '[ -e plain.bck ] && [ -e packed.bck ] && [ -e vols.bck.003 ] && [ -e crypt.bck ]' "the Linux savesets could not be made"

#
#	1. Linux -> Windows: the same listing, the tree back, the bad names said
#
for B in plain packed vols crypt; do
	K=""
	[ $B = crypt ] && K="/KEY_FILE=key"

	$VB $B.bck /LIST /FORMAT=LS $K > $B.ll 2>&1
	vw $B.bck /LIST /FORMAT=LS $K > $B.lw 2>&1
	check '[ $? = 0 ] && cmp -s $B.ll $B.lw' "$B: the listing differs from Linux: $(diff $B.ll $B.lw | head -3)"

	vw $B.bck $B.out $K > $B.rw 2>&1
	RC=$?
	check '[ $RC = 2 ]' "$B: the restore, completion code $RC (the bad names are errors): $(grep -E -- '-[EF]-' $B.rw | head -3)"
	check 'same_tree ref/tree $B.out/tree' "$B: the tree restored by vbackup.exe differs"

	for N in "a:b" con.txt "x?" trail.; do
		check 'grep -qF "File: tree/$N, errno: 22 - cannot be created as output (not a valid name on Windows)" $B.rw' \
			"$B: $N not refused as a name Windows cannot hold: $(grep -F "$N" $B.rw)"
	done

	check 'grep -q "fifo - a special file not restored" $B.rw' "$B: the FIFO not said: $(grep fifo $B.rw)"
done

check '[ "$(stat -c %i plain.out/tree/a.txt)" = "$(stat -c %i plain.out/tree/sub/hard)" ]' "the hard link not restored as one"
check '[ ! -w plain.out/tree/readonly.txt ] || [ "$(id -u)" = 0 -a "$(stat -c %a plain.out/tree/readonly.txt)" = 444 ]' "the read-only file restored writable"

#
#	2. Windows -> Linux: made by vbackup.exe, restored by the Linux image
#
vw ref/tree w.bck /VERIFY > w.log 2>&1
check '[ $? = 0 ] && grep -q "Differences: 0" w.log' "save by vbackup.exe /VERIFY: $(grep -E -- '-[EFW]-' w.log | head -3)"
check 'grep -q "Operating system:  Windows" "$($VB w.bck /LIST > w.lis 2>&1; echo w.lis)"' "the saveset of vbackup.exe does not say Windows"

vw ref/tree wv.bck /BLOCK_SIZE=16384 /VOLUME_SIZE=1048576 /PARITY=3 /DATA_FORMAT=COMPRESSED > wv.log 2>&1
check '[ $? = 0 ] && [ -e wv.bck.002 ]' "save over volumes by vbackup.exe: $(grep -E -- '-[EFW]-' wv.log | head -3)"
vw ref/tree wc.bck /ENCRYPT /KEY_FILE=key > wc.log 2>&1
check '[ $? = 0 ]' "an encrypted save by vbackup.exe: $(grep -E -- '-[EFW]-' wc.log | head -3)"

for B in w wv wc; do
	K=""
	[ $B = wc ] && K="/KEY_FILE=key"

	$VB $B.bck $B.lout $K > $B.lr 2>&1
	check '[ $? = 0 ] && same_tree ref/tree $B.lout/tree' "$B: the saveset of vbackup.exe not restored the same on Linux, $(grep -E -- '-[EFW]-' $B.lr | head -3)"
done

check '[ "$(stat -c %i w.lout/tree/a.txt)" = "$(stat -c %i w.lout/tree/sub/hard)" ]' "the hard link saved by vbackup.exe not found as one"

#	/COMPARE of vbackup.exe: none differs, then one
cp -a ref cmpdir
vw w.bck cmpdir /COMPARE > cmp.log 2>&1
check 'grep -q "Differences: 0 - compared" cmp.log' "compare: differences found where none are, $(grep -E -- '-[EW]-' cmp.log | head -3)"
echo more >> cmpdir/tree/empty
vw w.bck cmpdir /COMPARE > cmp2.log 2>&1
check 'grep -q "File: cmpdir/tree/empty - the size differs" cmp2.log' "compare: a changed file not found, $(head -3 cmp2.log)"

#	Damage: three blocks of a group of the /PARITY=3 saveset, repaired by vbackup.exe
python3 - wv.bck <<'PYEOF'
import sys
f = open(sys.argv[1], 'r+b')
for b in (21, 22, 23):
	f.seek(b * 16384 + 300)
	f.write(b'DAMAGED!')
PYEOF
vw wv.bck wv.wout > wvd.log 2>&1
check '[ $? = 0 ] && same_tree ref/tree wv.wout/tree' "vbackup.exe did not repair three bad blocks by /PARITY=3: $(grep -E -- '-[EFW]-' wvd.log | head -3)"

#
#	3. The journal and the incremental chain, by vbackup.exe
#
mkdir -p inc && echo 1 > inc/a && echo 2 > inc/b
vw inc f.bck /RECORD /JOURNAL=j.jnl > inc1.log 2>&1
check '[ $? = 0 ]' "full /RECORD: $(grep -E -- '-[EFW]-' inc1.log | head -3)"
sleep 1
echo 3 > inc/c && echo 22 > inc/b && rm inc/a
vw inc i.bck /SINCE=BACKUP /RECORD /JOURNAL=j.jnl > inc2.log 2>&1
check '[ $? = 0 ]' "incremental /SINCE=BACKUP: $(grep -E -- '-[EFW]-' inc2.log | head -3)"
vw /JOURNAL=j.jnl /LIST > jl.log 2>&1
check 'grep -q "Total of 2 savesets" jl.log' "the journal does not list both savesets: $(cat jl.log | tail -3)"
vw f.bck,i.bck incr /INCREMENTAL > inc3.log 2>&1
check '[ $? = 0 ] && [ ! -e incr/inc/a ] && [ "$(cat incr/inc/b)" = 22 ] && [ "$(cat incr/inc/c)" = 3 ]' "the incremental chain not restored right: $(ls incr/inc 2>&1)"

#	/ORIGINAL: the base of a saveset made on Windows is "Z:/..." - absolute there
mkdir -p orig/d && echo o > orig/d/f
vw orig/d orig.bck > /dev/null 2>&1
rm -f orig/d/f
vw orig.bck /ORIGINAL > orig.log 2>&1
check '[ $? = 0 ] && [ "$(cat orig/d/f 2>/dev/null)" = o ]' "/ORIGINAL by vbackup.exe: $(grep -E -- '-[EFW]-' orig.log | head -3)"

#	A path in the form of Windows: "\" separates, "log" is a directory, not /LOG
mkdir -p "bs/log" && echo b > bs/log/f
WP=$(echo "Z:$S/bs" | tr / '\\')
vw "$WP\\log" "$WP\\bs.bck" > bs.log 2>&1
check '[ $? = 0 ] && $VB bs/bs.bck /LIST | grep -q "^log/f "' "a path with backslashes: $(grep -E -- '-[EFW]-' bs.log | head -3)"

#	A name not in ASCII on the command line: the arguments are taken in UTF-16, made UTF-8
mkdir -p "имя" && echo z > "имя/f"
vw "имя" cyr.bck > cyr.log 2>&1
check '[ $? = 0 ] && $VB cyr.bck /LIST | grep -q "^имя/f "' "a Cyrillic name on the command line: $(grep -E -- '-[EFW]-' cyr.log | head -3)"

#	The journal by default: %USERPROFILE%\.vbackup\vbackup.jnl (no HOME); not when one is there already
WPFX=${WINEPREFIX:-$HOME/.wine}
JD="$WPFX/drive_c/users/$(id -un)/.vbackup"
if [ -d "$WPFX/drive_c/users/$(id -un)" ] && [ ! -e "$JD" ]; then
	env -u HOME WINEPREFIX="$WPFX" timeout 300 $WINE "$VW" "имя" jdef.bck /RECORD > jdef.log 2>&1
	check '[ $? = 0 ] && [ -s "$JD/vbackup.jnl" ]' "the journal by default not in the profile: $(ls "$JD" 2>&1; grep -E -- '-[EFW]-' jdef.log | head -3)"
	rm -rf "$JD"
fi

#
#	4. A pipe: binary both ways
#
vw ref/tree - 2> pipe.log > p.bck
check '[ -s p.bck ] && $VB p.bck /LIST > /dev/null 2>&1' "a save to the standard output not a saveset: $(head -3 pipe.log)"
cat plain.bck | vw - pout > pin.log 2>&1
check 'same_tree ref/tree pout/tree' "a restore from the standard input differs: $(grep -E -- '-[EF]-' pin.log | grep -v 'Windows' | head -3)"

#
#	5. The command line: what vbackup.exe refuses
#
vw ref/tree x.bck /PHYSICAL > q1.log 2>&1
check '[ $? = 2 ] && grep -q "Qualifier: /PHYSICAL - not on Windows" q1.log' "/PHYSICAL not refused: $(cat q1.log)"
vw ref/tree x.bck /IMAGE > q2.log 2>&1
check '[ $? = 2 ] && grep -q "Qualifier: /IMAGE - not on Windows" q2.log' "/IMAGE not refused: $(cat q2.log)"
vw ref/tree node::x.bck > q3.log 2>&1
check '[ $? = 2 ] && grep -q "REMOTE, Node: node" q3.log' "a saveset on another node not refused: $(cat q3.log)"

#
#	6. Two names that differ in case only - one file on Windows: the second
#	is not restored, with /REPLACE neither (it would replace the first)
#
mkdir -p case/t && echo upper > case/t/README && echo lower > case/t/readme
$VB case/t case.bck > /dev/null 2>&1
vw case.bck case1 > case1.log 2>&1
check 'grep -q "readme - already exists, not restored" case1.log && [ "$(cat case1/t/*)" = upper ]' "case: the second name not refused, $(cat case1.log | head -3)"
vw case.bck case2 /REPLACE > case2.log 2>&1
check 'grep -q "differs in case only is there - one file on Windows" case2.log && [ "$(cat case2/t/*)" = upper ]' \
	"case: /REPLACE replaced the file of the other name, $(grep -E -- '-[EW]-' case2.log | head -3)"

#
#	7. test/units.c on Windows: the core, the parity by the vector code of
#	this CPU, the cipher and its pool of threads
#
if [ -n "$UNITSEXE" ] && [ -e "$UNITSEXE" ]; then
	mkdir -p units.d
	timeout 900 $WINE "$UNITSEXE" units.d > units.log 2>&1
	check '[ $? = 0 ] && grep -q " 0 failures" units.log' "test/units.c under wine: $(grep -iE 'fail|error' units.log | head -3)"
fi

tap_end
