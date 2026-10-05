#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/vms.sh
#
#	ABSTRACT:	The savesets of OpenVMS BACKUP read by VBACKUP and vbkx:
#			the listing against BACKUP/LIST's own, every file
#			against what FTP of VMS made of it, the repair of a
#			bad block, the report of lost ones, a forged name.
#
#	DESCRIPTION:	VBACKUP and VBKX name the images, SCRATCH a directory
#			the script may fill and remove.  The savesets and the
#			references are in test/vms, made on OpenVMS Alpha V8.3
#			by test/vms/MKVBKS.COM (see test/vms/README.md):
#
#			    VBKS1.BCK	/BLOCK_SIZE=8192 /GROUP_SIZE=5
#			    VBKS2.BCK	/BLOCK_SIZE=8192 /NOCRC /GROUP_SIZE=0
#			    VBKS3.BCK	the defaults: 32256, a group of 10
#			    VBKS1.LIS	BACKUP/LIST/FULL VBKS1.BCK/SAVE_SET
#			    VBKS2.LIS	BACKUP/LIST VBKS2.BCK/SAVE_SET
#
#			TEXT.TXT is the text every text file was made of (so
#			FTP of VMS gives it back from each), LONG.TXT one with
#			records of 4 to 9 KB, BIN.DAT the binary of BIN.DAT and
#			UDF.DAT; FIX80.REF the fixed records with LF, IDX.REF
#			the indexed file as it is on the disk.
#
#			TAP=1 - the Test Anything Protocol (test/tap.sh).
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 5-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 5-OCT-2026	RRL	X-13 : Initial version.
#
#---

VB=${VBACKUP:?"VBACKUP must name the image"}
VX=${VBKX:?"VBKX must name vbkx"}
S=${SCRATCH:?"SCRATCH must name a scratch directory"}
R=$(cd "$(dirname "$0")/vms" && pwd)

TAPNAME=VMS
. "$(dirname "$0")/tap.sh"

cleanup () { rm -rf "$S"; }
[ -n "$KEEP" ] || trap cleanup EXIT

cleanup
mkdir -p "$S" || exit 1
cd "$S" || exit 1

#	A copy of a saveset with octets changed: name, offset, how many
zap () {
	cp "$R/$1" "$2"
	python3 - "$2" "$3" <<-'EOF'
	import sys
	p = sys.argv[1]; d = bytearray(open(p, 'rb').read())
	for o in sys.argv[2].split(','):
	    d[int(o)] ^= 0x5A
	open(p, 'wb').write(d)
	EOF
}

#	The files of a restore of VBKS: each against its reference
tree_ok () {
	T=$1/LAISHEV/VBKS
	for f in VAR.TXT STM.TXT STMCR.TXT STMLF.TXT VFC.TXT a.b.c.txt "Mixed Case.txt" SUB/V.TXT "SUB/V.TXT;1"; do
		cmp -s "$T/$f" "$R/TEXT.TXT" || { echo "$f differs from TEXT.TXT"; return 1; }
	done
	cmp -s "$T/LONG.TXT" "$R/LONG.TXT" || { echo "LONG.TXT differs"; return 1; }
	cmp -s "$T/FIX80.TXT" "$R/FIX80.REF" || { echo "FIX80.TXT differs"; return 1; }
	cmp -s "$T/BIN.DAT" "$R/BIN.DAT" || { echo "BIN.DAT differs"; return 1; }
	cmp -s "$T/UDF.DAT" "$R/BIN.DAT" || { echo "UDF.DAT differs"; return 1; }
	cmp -s "$T/IDX.DAT" "$R/IDX.REF" || { echo "IDX.DAT differs"; return 1; }
	[ -f "$T/EMPTY.DAT" ] && [ ! -s "$T/EMPTY.DAT" ] || { echo "EMPTY.DAT"; return 1; }
	[ -d "$T/SUB" ] || { echo "SUB is no directory"; return 1; }
	[ "$(find "$1" | wc -l)" = 19 ] || { echo "not 19 names: $(find "$1" | wc -l)"; return 1; }
	return 0
}

#
#	The listing: what BACKUP/LIST shows, octet for octet
#
$VB "$R/VBKS1.BCK" /LIST /FULL > l1.lis 2> l1.err; rc=$?
check 'cmp -s l1.lis "$R/VBKS1.LIS"' "list /FULL: not BACKUP/LIST/FULL's listing"
check '[ ! -s l1.err ]' "list /FULL: messages where none are due"

$VB "$R/VBKS1.BCK" /LIST/FULL > l1g.lis 2>&1; rc=$?
check 'cmp -s l1g.lis "$R/VBKS1.LIS"' "list: /LIST/FULL glued is not /LIST /FULL"
check '[ ! -d /LIST/LAISHEV ]' "list: /LIST/FULL restored into a directory /LIST"

$VB "$R/VBKS2.BCK" /LIST > l2.lis 2> l2.err; rc=$?
check 'cmp -s l2.lis "$R/VBKS2.LIS"' "list brief: not BACKUP/LIST's listing"
check 'grep -q "VMSNOCRC, Saveset:" l2.err' "list /NOCRC: VMSNOCRC not said"

$VB "$R/VBKS3.BCK" /LIST > l3.lis 2>&1; rc=$?
check 'grep -q "^Total of 16 files, 233 blocks$" l3.lis && grep -q "^Block size:        32256$" l3.lis' "list default sizes: totals or block size"

$VB "$R/VBKS1.BCK" /LIST /SELECT="*/SUB/*" > ls.lis 2>&1; rc=$?
check 'grep -q "^Total of 2 files, 30 blocks$" ls.lis' "list /SELECT: not the two files of SUB"

#
#	The restore: each file as FTP of VMS gives it
#
$VB "$R/VBKS1.BCK" r1 > r1.log 2>&1; rc=$?
check '[ $rc = 0 ] && tree_ok r1 > r1.why' "restore VBKS1: $(cat r1.why 2>/dev/null)"
check 'grep -q "VMSSAVESET, Saveset: .*Block size: 8192, Group size: 5" r1.log' "restore: VMSSAVESET not said"
check 'grep -q "VMSRAW, File: r1/LAISHEV/VBKS/IDX.DAT, Organization: Indexed" r1.log' "restore: the indexed file not said VMSRAW"
check 'grep -q "RESTSUMM, Files: 16," r1.log' "restore: RESTSUMM not 16 files"
check '[ "$(stat -c %a r1/LAISHEV/VBKS/VAR.TXT)" = 640 ] && [ "$(stat -c %a r1/LAISHEV/VBKS/SUB)" = 751 ]' "restore: modes from the protection (640, a directory 751)"
check '[ "$(date -r r1/LAISHEV/VBKS/VAR.TXT "+%Y-%m-%d %H:%M:%S")" = "2026-10-05 18:44:12" ]' "restore: the revision date as the time of the file"

$VB "$R/VBKS2.BCK" r2 > r2.log 2>&1; rc=$?
check 'tree_ok r2 > r2.why' "restore VBKS2 (/NOCRC, no groups): $(cat r2.why 2>/dev/null)"
$VB "$R/VBKS3.BCK" r3 > r3.log 2>&1; rc=$?
check 'tree_ok r3 > r3.why' "restore VBKS3 (32256, group 10): $(cat r3.why 2>/dev/null)"

$VB "$R/VBKS1.BCK" r1 > rx.log 2>&1; rc=$?
check '[ $rc != 0 ] && grep -q "FILEEXISTS, File: r1/LAISHEV/VBKS/VAR.TXT" rx.log' "restore again: FILEEXISTS not said"
$VB "$R/VBKS1.BCK" r1 /REPLACE > rr.log 2>&1; rc=$?
check '[ $rc = 0 ] && tree_ok r1 > /dev/null' "restore /REPLACE: not restored"

$VB "$R/VBKS1.BCK" rs /SELECT="*.TXT" /EXCLUDE="*/SUB/*" > rs.log 2>&1; rc=$?
check '[ -f rs/LAISHEV/VBKS/VAR.TXT ] && [ ! -e rs/LAISHEV/VBKS/BIN.DAT ] && [ ! -e rs/LAISHEV/VBKS/SUB/V.TXT ]' "restore /SELECT /EXCLUDE"

#
#	/EXTRACT: by the Linux name, by the name of VMS, to the standard output
#
$VB "$R/VBKS1.BCK" /EXTRACT=LAISHEV/VBKS/VFC.TXT e1.txt > e1.log 2>&1; rc=$?
check '[ $rc = 0 ] && cmp -s e1.txt "$R/TEXT.TXT"' "extract VFC.TXT by its Linux name"
$VB "$R/VBKS1.BCK" "/EXTRACT=[LAISHEV.VBKS]BIN.DAT;1" - > e2.bin 2> e2.log; rc=$?
check 'cmp -s e2.bin "$R/BIN.DAT"' "extract BIN.DAT by its VMS name to the standard output"
$VB "$R/VBKS1.BCK" "/EXTRACT=LAISHEV/VBKS/SUB/V.TXT;1" e3.txt > e3.log 2>&1; rc=$?
check 'cmp -s e3.txt "$R/TEXT.TXT"' "extract an older version by name;version"
$VB "$R/VBKS1.BCK" /EXTRACT=no/such/file e4.txt > e4.log 2>&1; rc=$?
check '[ $rc != 0 ] && grep -q "NOTFOUND, File: no/such/file" e4.log' "extract: NOTFOUND not said"

#
#	What is not done with a saveset of VMS: said, nothing done
#
$VB "$R/VBKS1.BCK" r1 /COMPARE > c1.log 2>&1; rc=$?
check '[ $rc != 0 ] && grep -q "QUALUSE, Qualifier: /COMPARE" c1.log' "compare: not refused"
$VB "$R/VBKS1.BCK" ri /IMAGE > c2.log 2>&1; rc=$?
check '[ $rc != 0 ] && grep -q "QUALUSE, Qualifier: /IMAGE" c2.log && [ ! -e ri ]' "restore /IMAGE: not refused"

#
#	Damage: one bad block of a group rebuilt; the first block, with
#	the SUMMARY and the group size, too; two of a group lost, said
#
zap VBKS1.BCK d1.bck 19384
$VB d1.bck d1 > d1.log 2>&1; rc=$?
check 'grep -q "BLKFIXED, Block: 3, Volume: 1" d1.log && tree_ok d1 > /dev/null' "damage: one bad block not rebuilt"

zap VBKS1.BCK d0.bck 100
$VB d0.bck d0 > d0.log 2>&1; rc=$?
check 'grep -q "BLKFIXED, Block: 1, Volume: 1" d0.log && tree_ok d0 > /dev/null' "damage: the first block not rebuilt"

zap VBKS1.BCK d2.bck 57844,65536
$VB d2.bck d2 > d2.log 2>&1; rc=$?
check '[ $rc != 0 ] && grep -q "BLKLOST, Block: 8, Volume: 1" d2.log && grep -q "BLKLOST, Block: 9, Volume: 1" d2.log' "damage: two lost blocks not said"
check 'grep -q "FILDAMAGED, File: d2/LAISHEV/VBKS/LONG.TXT" d2.log' "damage: the file that lost data not said"
check 'cmp -s d2/LAISHEV/VBKS/VAR.TXT "$R/TEXT.TXT" && cmp -s d2/LAISHEV/VBKS/SUB/V.TXT "$R/TEXT.TXT"' "damage: the files behind the lost blocks spoiled"

zap VBKS1.BCK dx.bck 40970
$VB dx.bck dx > dx.log 2>&1; rc=$?
check '! grep -q "BLK" dx.log && tree_ok dx > /dev/null' "damage: a bad XOR block costs something"

#	A forged name - [^.^..LAISHEV] is ../LAISHEV - refused (VBKS2 has no CRC to forge)
cp "$R/VBKS2.BCK" f.bck
python3 - f.bck <<-'EOF'
import sys
p = sys.argv[1]; d = open(p, 'rb').read()
d = d.replace(b'[LAISHEV.VBKS]BIN.DAT;1', b'[^.^..LAISHEV]BIN.DAT;1', 1)
open(p, 'wb').write(d)
EOF
mkdir -p fo/in
$VB f.bck fo/in > f.log 2>&1; rc=$?
check 'grep -q "OPENOUT, File: ../LAISHEV/BIN.DAT" f.log && [ ! -e fo/LAISHEV ]' "forged ../ name: not refused"

#
#	vbkx: the same files, the same judgement
#
$VX l "$R/VBKS1.BCK" > x.lis 2>&1; rc=$?
check '[ "$(wc -l < x.lis)" = 16 ] && grep -q " d0751 LAISHEV/VBKS/SUB$" x.lis && grep -q "LAISHEV/VBKS/SUB/V.TXT;1$" x.lis' "vbkx l: not the 16 files"
$VX t "$R/VBKS1.BCK" > xt.log 2>&1; rc=$?
check '[ $rc = 0 ] && grep -q "all block CRCs match" xt.log' "vbkx t: not clean"
$VX x "$R/VBKS3.BCK" -C xo > xo.log 2>&1; rc=$?
check '[ $rc = 0 ] && tree_ok xo > xo.why' "vbkx x: $(cat xo.why 2>/dev/null)"
$VX p "$R/VBKS1.BCK" LAISHEV/VBKS/LONG.TXT > xp.txt 2> xp.log; rc=$?
check 'cmp -s xp.txt "$R/LONG.TXT"' "vbkx p: LONG.TXT"
$VX t d2.bck > xd.log 2>&1; rc=$?
check '[ $rc = 1 ] && grep -q "Block: 8, Volume: 1 - is bad" xd.log && grep -q "File: LAISHEV/VBKS/LONG.TXT - incomplete" xd.log' "vbkx t damaged: not said, or not 1"
$VX t d1.bck > xd1.log 2>&1; rc=$?
check '[ $rc = 0 ] && grep -q "Block: 3, Volume: 1 - was bad, rebuilt" xd1.log' "vbkx t: a rebuilt block"

tap_end
