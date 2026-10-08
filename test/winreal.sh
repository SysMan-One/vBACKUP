#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/winreal.sh
#
#	ABSTRACT:	VBACKUP.EXE on a real Windows, in three phases on two
#			machines: "make" on Linux makes the savesets and their
#			references, "run" on Windows (the bash of MSYS2) reads
#			them and saves a tree of NTFS - streams, holes, hard
#			links, symbolic links and junctions, attributes, the
#			owner, the DACL and the SACL - and puts it back, "check"
#			on Linux reads what Windows made.  What wine cannot
#			show (test/winutil.sh), checked where it is.
#
#	DESCRIPTION:	sh winreal.sh make	VBACKUP the Linux image, SCRATCH
#						a directory, OUT where the savesets
#						go (made)
#			sh winreal.sh run	VBACKUPEXE, VBKXEXE, UNITSEXE,
#						FAKESSHEXE the images for Windows,
#						IN the OUT of "make", OUT where the
#						savesets of Windows go, SCRATCH
#			sh winreal.sh check	VBACKUP, IN the OUT of "run",
#						LIN the OUT of "make", SCRATCH
#
#			"run" on Windows itself when uname says MINGW or MSYS;
#			elsewhere through WINE (default: wine), and then what
#			only NTFS has is SKIP - so the script itself is tried
#			on Linux before it goes to a Windows.  TAP=1 - TAP;
#			KEEP=1 keeps SCRATCH.
#
#			Not here: VSS, a console without echo for the
#			passphrase.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 8-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 8-OCT-2026	RRL	X-01 : Initial version.
#
#---

PHASE=${1:?"the phase: make, run or check"}
SKIPS=0
S=${SCRATCH:?"SCRATCH must name a scratch directory"}

#	The bash of MSYS2 makes "/LIST" a path for a program of Windows: none of that
export MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 WINEDEBUG=-all TZ=UTC

TAPNAME=WINREAL
. "$(dirname "$0")/tap.sh"

#	In GitHub Actions a failure is an annotation too: seen without the log
if [ -n "${GITHUB_ACTIONS:-}" ] && [ -z "${TAP:-}" ]; then
	fail () {
		CHECKS=$((CHECKS + 1))
		FAILS=$((FAILS + 1))
		echo "%VBACKUP-E-$TAPNAME, $*"
		echo "::error title=winreal.sh $PHASE::$(printf '%s' "$*" | tr '\n' ' ')"
	}
fi

cleanup () { rm -rf "$S"; }
[ -n "$KEEP" ] || trap cleanup EXIT

rm -rf "$S"
mkdir -p "$S" && S=$(cd "$S" && pwd) || exit 1

#	A directory named by the caller, made absolute before the cd
absdir () { mkdir -p "$1" && ( cd "$1" && pwd ); }

#	An image named by the caller, absolute: the run cds into its directories
absfile () { [ -z "$1" ] || echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }

#	A tree as a list: files with size, time to the second and SHA-256, directories
state () {
	( cd "$1" && {
	find . -type f -printf '%P\n' | while read -r F; do
		printf 'f %s %s %s %s\n' "$(stat -c %s "$F")" "$(date -u -r "$F" +%Y%m%d%H%M%S)" \
			"$(sha256sum < "$F" | cut -c1-64)" "$F"
	done
	find . -mindepth 1 -type d -printf 'd %P\n'
	} | LC_ALL=C sort )
}

same_state () {
	state "$2" > "$S/st.tmp"
	cmp -s "$1" "$S/st.tmp" || { diff "$1" "$S/st.tmp" | head -5; return 1; }
}

#	The errors and warnings of a log, for a message
errs () { grep -E -- '-[EFW]-' "$@" | head -3; }

case $PHASE in

#
#	Linux: the savesets Windows reads, the lists of what is in them
#
make)
	VB=$(absfile "${VBACKUP:?"VBACKUP must name the image"}")
	O=$(absdir "${OUT:?"OUT must name the output directory"}") || exit 1
	cd "$S" || exit 1

	#	The tree: what Windows takes, and what it does not (test/winutil.sh)
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
	head -c 48 /dev/urandom | base64 -w0 > $O/key && chmod 600 $O/key

	#	The tree as Windows can have it: the reference of what comes back
	mkdir -p ref && cp -a $T ref/ && rm -f "ref/tree/a:b" ref/tree/con.txt "ref/tree/x?" ref/tree/trail. ref/tree/fifo
	state ref/tree > $O/ref.state

	$VB src/tree $O/plain.bck > mk.log 2>&1
	$VB src/tree $O/packed.bck /DATA_FORMAT=COMPRESSED >> mk.log 2>&1
	$VB src/tree $O/vols.bck /BLOCK_SIZE=16384 /GROUP_SIZE=5 /VOLUME_SIZE=1048576 /PARITY=2 >> mk.log 2>&1
	$VB src/tree $O/crypt.bck /ENCRYPT /KEY_FILE=$O/key >> mk.log 2>&1
	$VB src/tree $O/z9.bck /LEVEL=9 >> mk.log 2>&1
	check '[ -e $O/plain.bck ] && [ -e $O/packed.bck ] && [ -e $O/vols.bck.003 ] && [ -e $O/crypt.bck ] && [ -e $O/z9.bck ]' \
		"the Linux savesets could not be made: $(errs mk.log)"

	for B in plain packed vols crypt z9; do
		K=""
		[ $B = crypt ] && K="/KEY_FILE=$O/key"
		( cd $O && $VB $B.bck /LIST /FORMAT=LS $K ) > $O/$B.ll 2>&1
	done

	#	Links: relative to a file and to a directory - the symbolic links of NTFS there
	mkdir -p links/l/dir && echo t > links/l/target.txt && echo d > links/l/dir/f
	ln -s target.txt links/l/rel
	ln -s dir links/l/dl
	$VB links/l $O/links.bck > /dev/null 2>&1

	#	A stream of NTFS from Linux: the attribute user.s1
	mkdir -p xa/x && echo base > xa/x/f
	if python3 -c 'import os,sys; os.setxattr(sys.argv[1], "user.s1", b"stream from linux")' xa/x/f 2> /dev/null; then
		$VB xa/x $O/xa.bck > /dev/null 2>&1
	fi

	#	A file with holes: 64 MB, one MB of data in the middle
	mkdir -p sp/s && truncate -s 64M sp/s/holes.bin
	head -c 1048576 /dev/urandom | dd of=sp/s/holes.bin bs=1M seek=32 conv=notrunc 2> /dev/null
	sha256sum < sp/s/holes.bin | cut -c1-64 > $O/holes.sha
	$VB sp/s $O/sp.bck > /dev/null 2>&1

	#	Two names that differ in case only
	mkdir -p case/t && echo upper > case/t/README && echo lower > case/t/readme
	$VB case/t $O/case.bck > /dev/null 2>&1

	check '[ -e $O/links.bck ] && [ -e $O/sp.bck ] && [ -e $O/case.bck ]' "the savesets of links, holes, case not made"
	;;

#
#	Windows: what Linux made read, a tree of NTFS saved and put back
#
run)
	VW=$(absfile "${VBACKUPEXE:?"VBACKUPEXE must name vbackup.exe"}")
	VBKXEXE=$(absfile "$VBKXEXE") UNITSEXE=$(absfile "$UNITSEXE") FAKESSHEXE=$(absfile "$FAKESSHEXE")
	I=$(absdir "${IN:?"IN must name the savesets of make"}") || exit 1
	O=$(absdir "${OUT:?"OUT must name the output directory"}") || exit 1

	case $(uname -s) in
	MINGW*|MSYS*)	REAL=1 WINE="" ;;
	*)		REAL="" WINE=${WINE:-wine} ;;
	esac

	#	A path for cmd and the tools of Windows; for wine - its drive Z:
	if [ -n "$REAL" ]; then
		wp () { cygpath -w "$1"; }
	else
		wp () { case $1 in /*) echo "Z:$1" | tr / '\\' ;; *) echo "$1" | tr / '\\' ;; esac; }
	fi

	vw ()	{ timeout 600 $WINE "$VW" "$@"; }
	win ()	{ $WINE "$@"; }
	ps1 ()	{ powershell -NoProfile -NonInteractive -Command "$1" | tr -d '\r'; }

	#	The letters of attrib (R, H, S, A), the path cut off; the KB a sparse file holds on the disk
	attrs () { attrib "$(wp "$1")" | tr -d '\r' | sed 's/[A-Za-z]:\\.*//' | tr -d ' '; }
	alloc () { fsutil sparse queryrange "$(wp "$1")" | tr -d '\r' | gawk '/Length/ { s += strtonum($NF) } END { print int(s / 1024) }'; }

	#	Only on a real Windows: what wine has not
	ntfs ()	{ [ -n "$REAL" ] || { echo "SKIP: $1 (not a real Windows)"; SKIPS=$((SKIPS + 1)); return 1; }; }

	#	All relative: a path of MSYS2 (/d/a/...) means nothing to vbackup.exe
	cd "$S" || exit 1
	mkdir -p lin out && cp -a $I/. lin/ && cp lin/key key

	#
	#	1. test/units.c built for Windows: the core, the parity by the
	#	vector code of this CPU, the cipher and its pool of threads
	#
	if [ -n "$UNITSEXE" ] && [ -e "$UNITSEXE" ]; then
		mkdir -p units.d
		timeout 900 $WINE "$UNITSEXE" units.d > units.log 2>&1
		check '[ $? = 0 ] && grep -q " 0 failures" units.log' "units.exe: $(grep -iE 'fail|error' units.log | head -3)"
	fi

	#
	#	2. Linux -> Windows: the same listing, the tree back, the bad names said
	#
	for B in plain packed vols crypt z9; do
		K=""
		[ $B = crypt ] && K="/KEY_FILE=key"

		( cd lin && timeout 600 $WINE "$VW" $B.bck /LIST /FORMAT=LS $K ) 2>&1 | tr -d '\r' > $B.lw
		check 'cmp -s lin/$B.ll $B.lw' "$B: the listing differs from Linux: $(diff lin/$B.ll $B.lw | head -3)"

		vw lin/$B.bck $B.out $K > $B.rw 2>&1
		RC=$?
		check '[ $RC = 2 ]' "$B: the restore, completion code $RC (the bad names are errors): $(grep -E -- '-[EF]-' $B.rw | head -3)"
		check 'same_state lin/ref.state $B.out/tree' "$B: the tree restored by vbackup.exe differs"

		for N in "a:b" con.txt "x?" trail.; do
			check 'grep -qF "File: tree/$N, errno: 22 - cannot be created as output (not a valid name on Windows)" $B.rw' \
				"$B: $N not refused as a name Windows cannot hold: $(grep -F "$N" $B.rw)"
		done
	done

	if ntfs "the hard link from Linux"; then
		check '[ "$(fsutil hardlink list "$(wp plain.out/tree/a.txt)" | tr -d "\r" | grep -c .)" = 2 ]' \
			"the hard link from Linux not restored as one: $(fsutil hardlink list "$(wp plain.out/tree/a.txt)")"
		check 'case $(attrs plain.out/tree/readonly.txt) in *R*) ;; *) false ;; esac' "the read-only file restored writable: $(attrs plain.out/tree/readonly.txt)"
	fi

	#	vbkx.exe: the same tree
	if [ -n "$VBKXEXE" ] && [ -e "$VBKXEXE" ]; then
		timeout 600 $WINE "$VBKXEXE" x lin/plain.bck -C vbkx.out > vbkx.log 2>&1
		check 'same_state lin/ref.state vbkx.out/tree' "vbkx.exe: the tree differs, $(grep -v '^tree/' vbkx.log | head -3)"
	fi

	#	Case: the second of two names a file of NTFS would make one is not restored
	vw lin/case.bck case1 > case1.log 2>&1
	check 'grep -q "readme - already exists, not restored" case1.log && [ "$(cat case1/t/*)" = upper ]' "case: the second name not refused, $(head -3 case1.log)"

	#	Links from Linux: symbolic links of NTFS, to a file and to a directory
	if ntfs "symbolic links from Linux"; then
		vw lin/links.bck links.out > links.log 2>&1
		check '[ $? = 0 ]' "the links from Linux: $(errs links.log)"
		check '[ "$(ps1 "(Get-Item -LiteralPath links.out/l/rel).LinkType")" = SymbolicLink ] && [ "$(cat links.out/l/rel)" = t ]' \
			"the link to a file from Linux: $(ps1 "Get-Item -LiteralPath links.out/l/rel | Format-List LinkType,Target")"
		check '[ "$(ps1 "(Get-Item -LiteralPath links.out/l/dl).LinkType")" = SymbolicLink ] && [ "$(cat links.out/l/dl/f)" = d ]' \
			"the link to a directory from Linux: $(ps1 "Get-Item -LiteralPath links.out/l/dl | Format-List LinkType,Target")"
	fi

	#	The attribute user.s1 of Linux: a stream
	if [ -e lin/xa.bck ] && ntfs "a stream from Linux"; then
		vw lin/xa.bck xa.out > xa.log 2>&1
		check '[ $? = 0 ] && [ "$(ps1 "Get-Content -LiteralPath xa.out/x/f -Stream s1")" = "stream from linux" ]' \
			"user.s1 from Linux not a stream: $(errs xa.log) $(ps1 "Get-Item -LiteralPath xa.out/x/f -Stream *")"
	fi

	#	Holes from Linux: a sparse file of NTFS, the zeros not on the disk
	vw lin/sp.bck sp.out > sp.log 2>&1
	check '[ $? = 0 ] && [ "$(sha256sum < sp.out/s/holes.bin | cut -c1-64)" = "$(cat lin/holes.sha)" ]' "the file with holes from Linux: $(errs sp.log)"

	if ntfs "the holes from Linux"; then
		check 'fsutil sparse queryflag "$(wp sp.out/s/holes.bin)" | grep -qi "is set as sparse"' "the file with holes not sparse: $(fsutil sparse queryflag "$(wp sp.out/s/holes.bin)")"
		AL=$(ps1 "(Get-Item -LiteralPath sp.out/s/holes.bin).Length")
		DU=$(alloc sp.out/s/holes.bin)
		check '[ "$AL" = 67108864 ] && [ "$DU" -lt 16384 ]' "the holes from Linux on the disk: length $AL, $DU KB allocated"
	fi

	#
	#	3. Windows -> Linux: the reference tree saved by vbackup.exe
	#
	cp -a plain.out wref
	vw wref/tree out/w.bck /VERIFY > w.log 2>&1
	check '[ $? = 0 ] && grep -q "Differences: 0" w.log' "save by vbackup.exe /VERIFY: $(errs w.log)"
	vw wref/tree out/wv.bck /BLOCK_SIZE=16384 /VOLUME_SIZE=1048576 /PARITY=3 /DATA_FORMAT=COMPRESSED > wv.log 2>&1
	check '[ $? = 0 ] && [ -e out/wv.bck.002 ]' "save over volumes by vbackup.exe: $(errs wv.log)"
	vw wref/tree out/wc.bck /ENCRYPT /KEY_FILE=key /LEVEL=9 > wc.log 2>&1
	check '[ $? = 0 ]' "an encrypted /LEVEL=9 save by vbackup.exe: $(errs wc.log)"

	cp -a wref cmpdir
	vw out/w.bck cmpdir /COMPARE > cmp.log 2>&1
	check 'grep -q "Differences: 0 - compared" cmp.log' "compare: differences found where none are, $(errs cmp.log)"

	#
	#	4. A tree of NTFS: what only Windows has, saved, put back, compared
	#
	if ntfs "the tree of NTFS"; then
		N=nt/t
		mkdir -p $N/sub
		echo plain > $N/a.txt
		echo sub > $N/sub/f
		ps1 "Set-Content -LiteralPath $N/a.txt -Stream s1 -Value 'stream one' -NoNewline"
		head -c 70000 /dev/zero | tr '\0' 'z' > big.str
		ps1 "Set-Content -LiteralPath $N/a.txt -Stream big -Value (Get-Content -Raw big.str) -NoNewline"
		echo h > $N/hid.txt && attrib +H +S "$(wp $N/hid.txt)"
		echo r > $N/ro.txt && attrib +R "$(wp $N/ro.txt)"
		cmd /c "mklink /H $(wp $N/hard) $(wp $N/a.txt)" > /dev/null
		cmd /c "mklink $(wp $N/sl) a.txt" > /dev/null
		cmd /c "mklink /D $(wp $N/sd) sub" > /dev/null
		cmd /c "mklink /J $(wp $N/jn) $(wp $S/$N/sub)" > /dev/null

		#	64 MB, sparse, one MB of data at 32 MB
		fsutil file createnew "$(wp $N/holes.bin)" 67108864 > /dev/null
		fsutil sparse setflag "$(wp $N/holes.bin)" > /dev/null
		fsutil sparse setrange "$(wp $N/holes.bin)" 0 67108864 > /dev/null
		head -c 1048576 /dev/urandom | dd of=$N/holes.bin bs=1M seek=32 conv=notrunc 2> /dev/null

		#	The owner SYSTEM, a DACL of its own (not inherited, a deny), a SACL
		echo acl > $N/acl.txt
		icacls "$(wp $N/acl.txt)" /inheritance:r /grant:r "*S-1-5-32-544:(F)" "*S-1-5-18:(F)" "*S-1-5-32-545:(R)" > /dev/null
		icacls "$(wp $N/acl.txt)" /deny "*S-1-5-32-546:(W)" > /dev/null
		icacls "$(wp $N/acl.txt)" /setowner "*S-1-5-18" > /dev/null
		ps1 "\$a = Get-Acl -LiteralPath $N/acl.txt -Audit; \$a.AddAuditRule((New-Object System.Security.AccessControl.FileSystemAuditRule('Everyone','Write','Success'))); Set-Acl -LiteralPath $N/acl.txt -AclObject \$a"

		check '[ "$(ps1 "(Get-Acl -LiteralPath $N/acl.txt).Owner")" = "NT AUTHORITY\\SYSTEM" ] && [ "$(ps1 "(Get-Acl -LiteralPath $N/acl.txt -Audit).Audit.Count")" = 1 ]' \
			"the tree of NTFS could not be made: $(ps1 "Get-Acl -LiteralPath $N/acl.txt -Audit | Format-List Owner,Sddl")"

		vw nt/t out/nt.bck /VERIFY > nt.log 2>&1
		RC=$?
		check '[ $RC = 1 ] && grep -q "Differences: 0" nt.log' "save of the tree of NTFS /VERIFY, completion code $RC: $(errs nt.log)"
		check 'grep -q "XATTRSKIP, File: .*a.txt - an extended attribute not saved" nt.log' "the stream longer than 64 KB not said: $(errs nt.log)"

		#	An administrator: /OWNER=ORIGINAL by default, the descriptor back whole
		vw out/nt.bck nt.out > nto.log 2>&1
		check '[ $? = 0 ]' "the tree of NTFS restored: $(errs nto.log)"
		M=nt.out/t

		check '[ "$(ps1 "Get-Content -LiteralPath $M/a.txt -Stream s1")" = "stream one" ]' "the stream not back: $(ps1 "Get-Item -LiteralPath $M/a.txt -Stream * | Format-Table Stream,Length")"
		check '[ -z "$(ps1 "Get-Item -LiteralPath $M/a.txt -Stream big -ErrorAction SilentlyContinue")" ]' "the stream not saved is there"
		check 'case $(attrs $M/hid.txt) in *S*H*|*H*S*) ;; *) false ;; esac' "HIDDEN SYSTEM not back: $(attrs $M/hid.txt)"
		check 'case $(attrs $M/ro.txt) in *R*) ;; *) false ;; esac' "READONLY not back: $(attrs $M/ro.txt)"
		check 'case $(attrs $M/a.txt) in *H*|*S*|*R*) false ;; esac' "a plain file restored with attributes: $(attrs $M/a.txt)"
		check '[ "$(fsutil hardlink list "$(wp $M/a.txt)" | tr -d "\r" | grep -c .)" = 2 ]' "the hard link not back: $(fsutil hardlink list "$(wp $M/a.txt)")"

		check '[ "$(ps1 "(Get-Item -LiteralPath $M/sl).LinkType")" = SymbolicLink ] && [ "$(ps1 "(Get-Item -LiteralPath $M/sl).Target")" = a.txt ]' \
			"the symbolic link to a file: $(ps1 "Get-Item -LiteralPath $M/sl | Format-List LinkType,Target")"
		check '[ "$(ps1 "(Get-Item -LiteralPath $M/sd).LinkType")" = SymbolicLink ] && [ "$(cat $M/sd/f)" = sub ]' \
			"the symbolic link to a directory: $(ps1 "Get-Item -LiteralPath $M/sd | Format-List LinkType,Target")"

		#	A junction comes back a symbolic link to the same directory (src/vbkosw.c, vbk$w_symlink)
		check '[ "$(ps1 "(Get-Item -LiteralPath $M/jn).Target")" = "$(wp $S/$N/sub)" ] && [ "$(cat $M/jn/f)" = sub ]' \
			"the junction: $(ps1 "Get-Item -LiteralPath $M/jn | Format-List LinkType,Target")"

		check 'fsutil sparse queryflag "$(wp $M/holes.bin)" | grep -qi "is set as sparse"' "the sparse file not sparse: $(fsutil sparse queryflag "$(wp $M/holes.bin)")"
		check 'cmp -s $N/holes.bin $M/holes.bin && [ "$(alloc $M/holes.bin)" -lt 16384 ]' "the sparse file: $(alloc $N/holes.bin) KB, restored $(alloc $M/holes.bin) KB"

		SD1=$(ps1 "(Get-Acl -LiteralPath $N/acl.txt -Audit).Sddl")
		SD2=$(ps1 "(Get-Acl -LiteralPath $M/acl.txt -Audit).Sddl")
		check '[ -n "$SD1" ] && [ "$SD1" = "$SD2" ]' "the descriptor differs: $SD1 / $SD2"

		#	Not an administrator's restore: /OWNER=DEFAULT - the ACL of the directory, the owner the user
		vw out/nt.bck ntd.out /OWNER=DEFAULT > ntd.log 2>&1
		check '[ $? = 0 ] && [ "$(ps1 "(Get-Acl -LiteralPath ntd.out/t/acl.txt).Owner")" != "NT AUTHORITY\\SYSTEM" ]' \
			"/OWNER=DEFAULT kept the owner of the saveset: $(ps1 "(Get-Acl -LiteralPath ntd.out/t/acl.txt).Owner") $(errs ntd.log)"

		vw out/nt.bck nt /COMPARE > ntc.log 2>&1
		check 'grep -q "Differences: 0 - compared" ntc.log' "compare of the tree of NTFS: $(errs ntc.log)"

		sha256sum < $N/holes.bin | cut -c1-64 > out/ntholes.sha
	fi

	#	node::file through ssh.exe - the stand-in, test/fakessh.c
	if [ -n "$FAKESSHEXE" ] && [ -e "$FAKESSHEXE" ]; then
		export VBACKUP_RSH="$(wp "$FAKESSHEXE")" FAKESSH_VBACKUP="$(wp "$VW")"
		vw wref/tree "node::$(wp $S/rsh.bck)" > rsh1.log 2>&1
		check '[ $? = 0 ] && [ -s rsh.bck ]' "a save to node::file through ssh.exe: $(errs rsh1.log)"
		vw "node::$(wp $S/rsh.bck)" rsh.out > rsh2.log 2>&1
		check '[ $? = 0 ] && same_state lin/ref.state rsh.out/tree' "a restore from node::file through ssh.exe: $(errs rsh2.log)"
		unset VBACKUP_RSH FAKESSH_VBACKUP
	fi

	cp -a out/. $O/
	;;

#
#	Linux: what Windows made
#
check)
	VB=$(absfile "${VBACKUP:?"VBACKUP must name the image"}")
	I=$(absdir "${IN:?"IN must name the savesets of run"}") || exit 1
	L=$(absdir "${LIN:?"LIN must name the savesets of make"}") || exit 1
	cd "$S" || exit 1

	for B in w wv wc; do
		K=""
		[ $B = wc ] && K="/KEY_FILE=$L/key"

		$VB $I/$B.bck $B.out $K > $B.lr 2>&1
		check '[ $? = 0 ] && same_state $L/ref.state $B.out/tree' "$B: the saveset of vbackup.exe not restored the same on Linux, $(errs $B.lr)"
	done

	check '[ "$(stat -c %i w.out/tree/a.txt)" = "$(stat -c %i w.out/tree/sub/hard)" ]' "the hard link saved by vbackup.exe not found as one"
	check '$VB $I/w.bck /LIST | grep -q "Operating system:  Windows"' "the saveset of vbackup.exe does not say Windows"

	if [ -e $I/nt.bck ]; then
		$VB $I/nt.bck /LIST /FULL > nt.lis 2>&1
		check 'sed -n "/^t\/hid.txt$/,+3p" nt.lis | grep -q "Windows: .*HIDDEN"' "HIDDEN not in the listing: $(sed -n '/^t\/hid.txt$/,+3p' nt.lis)"
		check 'grep -q "Owner: NT AUTHORITY\\\\SYSTEM" nt.lis' "the owner SYSTEM not in the listing: $(grep -m3 Owner: nt.lis)"

		$VB $I/nt.bck nt.out > nt.lr 2>&1
		RC=$?
		check '[ $RC -le 1 ]' "the tree of NTFS restored on Linux, completion code $RC: $(errs nt.lr)"
		check '[ "$(python3 -c "import os,sys; print(os.getxattr(sys.argv[1], \"user.s1\").decode())" nt.out/t/a.txt 2>&1)" = "stream one" ]' \
			"the stream not the attribute user.s1 on Linux"
		check '[ "$(stat -c %i nt.out/t/a.txt)" = "$(stat -c %i nt.out/t/hard)" ]' "the hard link of NTFS not one on Linux"
		check '[ "$(readlink nt.out/t/sl)" = a.txt ] && [ "$(readlink nt.out/t/sd)" = sub ]' "the symbolic links of NTFS on Linux: $(ls -l nt.out/t)"
		check '[ "$(sha256sum < nt.out/t/holes.bin | cut -c1-64)" = "$(cat $I/ntholes.sha)" ] && [ "$(du -k nt.out/t/holes.bin | cut -f1)" -lt 16384 ]' \
			"the sparse file of NTFS on Linux: $(du -k nt.out/t/holes.bin)"
	fi
	;;

*)
	bail "the phase: make, run or check, not $PHASE"
	;;
esac

#	What ran, seen without the log
[ -z "${GITHUB_ACTIONS:-}" ] || echo "::notice title=winreal.sh $PHASE::$CHECKS checks, $FAILS failures, $SKIPS parts skipped"

tap_end
