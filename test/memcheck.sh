#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/memcheck.sh
#
#	ABSTRACT:	VBACKUP and test/units under valgrind memcheck: no
#			read or write out of bounds, no use of what is not
#			set, no leak that is certain - the codecs at every
#			level, the cipher, the parity, a restore of each.
#
#	DESCRIPTION:	VBACKUP names the image, UNITS test/units, VALGRIND
#			the valgrind, SCRATCH a directory the script may fill
#			and remove.  KEEP=1 keeps it.  Slow - a minute or two
#			on a PC, much more on a small ARM board: not among
#			the tests of an installation, only with
#			-DVBACKUP$M_MEMCHECK=ON.  The threads of the pools
#			are left at the end, their stacks "possibly lost":
#			those are not counted.
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
UT=${UNITS:?"UNITS must name test/units"}
VG=${VALGRIND:-valgrind}
S=${SCRATCH:?"SCRATCH must name a scratch directory"}

TAPNAME=MEMCHECK
. "$(dirname "$0")/tap.sh"

cleanup () { rm -rf "$S"; }
[ -n "$KEEP" ] || trap cleanup EXIT

rm -rf "$S"
mkdir -p "$S" || exit 1
cd "$S" || exit 1

#	valgrind with its errors as the completion code 99; one log each
vg ()	{ VGL=$1; shift; $VG -q --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite --log-file=$VGL.vg "$@" > $VGL.out 2>&1; }

mkdir -p src/tree/sub
seq 1 60000 | sed 's/$/ a line that compresses/' > src/tree/text.txt
head -c 200000 /dev/urandom > src/tree/sub/rand.bin
: > src/tree/empty
truncate -s 3M src/tree/sparse
head -c 32 /dev/urandom > key && chmod 600 key

mkdir -p u.d
vg units $UT u.d
check '[ $? = 0 ]' "test/units under valgrind: $(grep -v '^==[0-9]*== *$' units.vg | head -5) $(tail -2 units.out)"

for L in 1 2 5 6 8 9; do
	vg s$L $VB src/tree l$L.bck /LEVEL=$L
	check '[ $? = 0 ]' "save /LEVEL=$L under valgrind: $(head -5 s$L.vg)"
	vg r$L $VB l$L.bck r$L
	check '[ $? = 0 ] && cmp -s src/tree/text.txt r$L/tree/text.txt' "restore /LEVEL=$L under valgrind: $(head -5 r$L.vg)"
done

VBACKUP_KDFITER=1000 vg se $VB src/tree e.bck /ENCRYPT /KEY_FILE=key /PARITY=3 /LEVEL=4 /BLOCK_SIZE=16384
check '[ $? = 0 ]' "save /ENCRYPT /PARITY=3 under valgrind: $(head -5 se.vg)"
VBACKUP_KDFITER=1000 vg re $VB e.bck re /KEY_FILE=key
check '[ $? = 0 ] && cmp -s src/tree/sub/rand.bin re/tree/sub/rand.bin' "restore /ENCRYPT under valgrind: $(head -5 re.vg)"

tap_end
