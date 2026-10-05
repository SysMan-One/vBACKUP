#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/plugins.sh
#
#	ABSTRACT:	The plugins of the file managers: the extfs of Midnight
#			Commander (plugins/mc/uvbk) and the MultiArc format of
#			far2l (plugins/far/vbackup.ini).
#
#	DESCRIPTION:	Two layers.  The commands themselves - uvbk list and
#			copyout, the List, Extract, ExtractWithoutPath and Test
#			lines of the MultiArc format, the listing cut by its
#			Format0 columns as MultiArc cuts it - against vbackup and
#			vbkx; these need nothing but the shell and python3.  Then
#			the real programs, when tmux, mc and far2l are there:
#			each started in a tmux session of its own, the saveset
#			entered, its contents looked for on the screen, a file
#			copied out with F5.  Without them that layer is skipped.
#
#			mc runs with a HOME of its own (the extfs is put there),
#			far2l with a profile of its own (-u) - nothing of the
#			user's settings is touched; far2l, which outlives its
#			terminal, is stopped by its name and our profile together.
#
#			VBACKUP, VBKX name the images, SCRATCH a directory the
#			script may fill and remove; KEEP=1 keeps it; TAP=1 - TAP.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 4-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 5-OCT-2026	RRL	X-04 : mc: Enter on x.sav, Works.save and a saveset with no
#					extension at all opens it (.sav, and file(1) with
#					our magic).
#
#		 5-OCT-2026	RRL	X-03 : far2l's question to revive an instance is known by
#					its prompt too: with many instances left over the
#					first line of it is scrolled off the screen.
#
#		 5-OCT-2026	RRL	X-02 : An encrypted saveset through VBACKUP_KEY_FILE; without
#					it the commands fail at once.
#
#		 4-OCT-2026	RRL	X-01 : Initial version.
#
#---

VB=${VBACKUP:?"VBACKUP must name the image"}
VX=${VBKX:?"VBKX must name the stand-alone extractor"}
S=${SCRATCH:?"SCRATCH must name a scratch directory"}
SRC=$(cd "$(dirname "$0")/.." && pwd)

#	ok, fail, check, bail, tap_end - plain output, or TAP with TAP=1
TAPNAME=PLUGINS
. "$(dirname "$0")/tap.sh"

TMUXSESS=
#	far2l outlives its terminal (it detaches and waits to be revived): it is found by the name
#	of its command and by our profile - by fields, so that the search never finds itself
farstop () {
	[ -n "${FP:-}" ] || return 0
	for P in $(ps -eo pid=,args= | awk -v p="$FP" '$2 == "far2l" && index($0, p) { print $1 }'); do
		kill "$P" 2>/dev/null
	done
}

cleanup () {
	[ -n "$TMUXSESS" ] && tmux kill-session -t "$TMUXSESS" 2>/dev/null
	farstop
	[ -n "$KEEP" ] || rm -rf "$S"
}
trap cleanup EXIT

rm -rf "$S"
mkdir -p "$S/bin" "$S/out" || bail "the scratch directory cannot be made"
cd "$S" || bail "the scratch directory cannot be entered"

#	The images first on the PATH: the plugins call vbackup and vbkx by name
ln -s "$VB" bin/vbackup && ln -s "$VX" bin/vbkx
PATH=$S/bin:$PATH
export PATH

#	A saveset with the names a plugin trips over: spaces, Cyrillic, a link, a hard link, a subdirectory
mkdir -p tree/sub "tree/с пробелом"
echo hello > "tree/a b.txt"
echo "имя" > "tree/с пробелом/файл.txt"
head -c 200000 /dev/urandom > tree/sub/r.bin
ln tree/sub/r.bin tree/sub/hard
ln -s "a b.txt" tree/link
$VB tree x.bck > save.log 2>&1 || bail "the saveset cannot be made: $(cat save.log)"
SUM0=$(sha256sum x.bck | cut -c1-64)

UVBK=$SRC/plugins/mc/uvbk
INI=$SRC/plugins/far/vbackup.ini

#
#	1. MC: the extfs script itself
#
sh "$UVBK" list x.bck > mc.lst 2> mc.err
check '[ $? = 0 ] && [ "$(cat mc.lst)" = "$($VB x.bck /LIST /FORMAT=LS)" ]' "uvbk list is not the listing of the catalog: $(cat mc.err)"
check 'grep -q " tree/a b.txt$" mc.lst && grep -q "tree/link -> a b.txt$" mc.lst && grep -q " tree/с пробелом/файл.txt$" mc.lst' "uvbk list: a name with a space, Cyrillic or a link is wrong"
sh "$UVBK" copyout x.bck "tree/с пробелом/файл.txt" out/f.txt 2>> mc.err
check '[ $? = 0 ] && cmp -s out/f.txt "tree/с пробелом/файл.txt"' "uvbk copyout of a Cyrillic name with a space"
sh "$UVBK" copyout x.bck tree/sub/hard out/h.bin 2>> mc.err
check '[ $? = 0 ] && cmp -s out/h.bin tree/sub/r.bin' "uvbk copyout of a further name of a file"
sh "$UVBK" copyout x.bck tree/none out/none 2>> mc.err
check '[ $? != 0 ] && [ ! -s out/none ]' "uvbk copyout of a name not there succeeded"
for OP in copyin rm mkdir rmdir; do
	sh "$UVBK" $OP x.bck tree/a.txt > /dev/null 2>&1
	check '[ $? = 1 ]' "uvbk $OP was not refused"
done

#
#	2. far2l / Far3: the MultiArc format - its commands, and its listing cut as MultiArc cuts it
#
ini () { sed -n "s/^$1=//p" "$INI" | head -1; }
check '[ "$(ini ID)" = "56 42 4B 42" ] && [ "$(head -c 4 x.bck)" = "VBKB" ] && [ "$(ini Extension)" = bck ]' "MultiArc: the ID or the extension do not describe a saveset"

LISTCMD=$(ini List | sed 's/%%AQ/x.bck/')
sh -c "$LISTCMD" > far.lst 2> far.err
check '[ $? = 0 ] && [ -s far.lst ] && ! grep -q -- " -> " far.lst' "MultiArc List: $(cat far.err)"

#	Format0: y t d h m s z a n at their columns; every other character is skipped
python3 - "$(ini Format0)" far.lst tree > far.parse 2>&1 << 'PYEOF'
import os, sys
fmt, lst, tree = sys.argv[1], sys.argv[2], sys.argv[3]
bad = 0
for line in open(lst, encoding="utf-8"):
	line = line.rstrip("\n")
	f = {}
	for i, c in enumerate(fmt):
		if c in "ytdhmszan" and i < len(line):
			f[c] = f.get(c, "") + line[i]
	name, size = f.get("n", "").rstrip(), f.get("z", "").strip()
	path = os.path.join(os.path.dirname(tree) or ".", name)
	if not os.path.lexists(path):
		print("no such entry:", repr(name)); bad += 1; continue
	if os.path.isfile(path) and not os.path.islink(path) and str(os.path.getsize(path)) != size:
		print("size:", repr(name), size); bad += 1
	if f.get("y", "") and not (f["y"].isdigit() and f["t"].isdigit() and f["d"].isdigit() and f["h"].isdigit()):
		print("time fields:", repr(line)); bad += 1
	if f.get("a", "")[:1] == "d" and not os.path.isdir(path):
		print("not a directory:", repr(name)); bad += 1
print("bad", bad)
PYEOF
check 'grep -q "^bad 0$" far.parse && [ "$(wc -l < far.lst)" = "$(find tree | wc -l)" ]' "MultiArc Format0 does not cut the listing right: $(head -3 far.parse)"

#	Extract, ExtractWithoutPath, Test - as MultiArc runs them: in the folder to extract into
NAMES="'tree/a b.txt' 'tree/с пробелом/файл.txt' tree/sub/r.bin"
mkdir -p far.x far.j
(cd far.x && sh -c "$(ini Extract | sed "s#%%AQ#$S/x.bck#; s#%%FSQ#$NAMES#")") > far.xlog 2>&1
check '[ $? = 0 ] && cmp -s "far.x/tree/a b.txt" "tree/a b.txt" && cmp -s "far.x/tree/с пробелом/файл.txt" "tree/с пробелом/файл.txt" && cmp -s far.x/tree/sub/r.bin tree/sub/r.bin' "MultiArc Extract: $(cat far.xlog)"
(cd far.j && sh -c "$(ini ExtractWithoutPath | sed "s#%%AQ#$S/x.bck#; s#%%FSQ#$NAMES#")") > far.jlog 2>&1
check '[ $? = 0 ] && cmp -s "far.j/a b.txt" "tree/a b.txt" && cmp -s far.j/файл.txt "tree/с пробелом/файл.txt" && [ ! -d far.j/tree ]' "MultiArc ExtractWithoutPath: $(cat far.jlog)"
sh -c "$(ini Test | sed "s#%%AQ#x.bck#")" > far.tlog 2>&1
check '[ $? = 0 ]' "MultiArc Test: $(cat far.tlog)"
check '[ -z "$(ini Delete)" ] && [ -z "$(ini Add)" ]' "MultiArc: the format offers to change a saveset"

#	An encrypted saveset: through VBACKUP_KEY_FILE; without it the commands fail at once, never ask on the screen
printf 'plugin passphrase\n' > key && chmod 600 key
VBACKUP_KDFITER=1000 VBACKUP_KEY_FILE=$S/key $VB tree e.bck /ENCRYPT > /dev/null 2>&1
VBACKUP_KEY_FILE=$S/key sh "$UVBK" list e.bck > mce.lst 2> mce.err
check '[ $? = 0 ] && [ "$(cat mce.lst)" = "$(cat mc.lst)" ]' "uvbk list of an encrypted saveset: $(head -2 mce.err)"
VBACKUP_KEY_FILE=$S/key sh "$UVBK" copyout e.bck "tree/a b.txt" out/e.txt 2>> mce.err
check '[ $? = 0 ] && cmp -s out/e.txt "tree/a b.txt"' "uvbk copyout of an encrypted saveset"
env -u VBACKUP_KEY_FILE timeout 20 sh "$UVBK" list e.bck > mcn.lst 2>&1 < /dev/null
RC=$?
check '[ $RC = 1 ] || [ $RC = 2 ]' "uvbk list of an encrypted saveset without a key: hung or succeeded"
VBACKUP_KEY_FILE=$S/key sh -c "$(ini List | sed 's/%%AQ/e.bck/')" > fare.lst 2>&1
check '[ $? = 0 ] && [ "$(cat fare.lst)" = "$(cat far.lst)" ]' "MultiArc List of an encrypted saveset: $(head -2 fare.lst)"
env -u VBACKUP_KEY_FILE timeout 20 sh -c "$(ini List | sed 's/%%AQ/e.bck/')" > farn.lst 2>&1
check '[ $? = 2 ] && grep -q "give -k" farn.lst' "MultiArc List without a key: $(head -1 farn.lst)"

#
#	3. The real programs, in tmux
#
#	Wait up to N seconds for a text on the screen of the session
waitfor () {
	i=0
	while [ $i -lt "${3:-15}" ]; do
		tmux capture-pane -t "$1" -p 2>/dev/null | grep -q -- "$2" && return 0
		sleep 1
		i=$((i + 1))
	done
	return 1
}

if command -v tmux > /dev/null 2>&1 && command -v mc > /dev/null 2>&1; then
	MH=$S/mchome
	mkdir -p "$MH/.local/share/mc/extfs.d" mcout
	cp "$UVBK" "$MH/.local/share/mc/extfs.d/uvbk"
	TMUXSESS=vbkplug-mc-$$
	tmux new-session -d -s $TMUXSESS -x 140 -y 32 "cd '$S' && HOME='$MH' PATH='$PATH' TERM=xterm exec mc -u -b '$S' '$S/mcout'"
	waitfor $TMUXSESS "x.bck" 15
	tmux send-keys -t $TMUXSESS "cd x.bck/uvbk://tree/sub" Enter
	check 'waitfor $TMUXSESS "uvbk://tree/sub" 15 && waitfor $TMUXSESS "r.bin" 10' "mc: the saveset is not entered: $(tmux capture-pane -t $TMUXSESS -p | head -6)"
	#	The entries are sorted: .., hard, r.bin - the cursor down onto hard, F5, Enter
	tmux send-keys -t $TMUXSESS Down
	sleep 1
	tmux send-keys -t $TMUXSESS F5
	sleep 1
	tmux send-keys -t $TMUXSESS Enter
	i=0; while [ $i -lt 15 ] && [ ! -s mcout/hard ]; do sleep 1; i=$((i + 1)); done
	check 'cmp -s mcout/hard tree/sub/r.bin' "mc: F5 did not copy the file out of the saveset"
	tmux send-keys -t $TMUXSESS F10
	sleep 1
	tmux kill-session -t $TMUXSESS 2>/dev/null
	TMUXSESS=

	#	Enter on a saveset: known by .sav as by .bck, and by its contents whatever its name (file(1) and our magic)
	if [ -r /etc/mc/mc.ext.ini ] && command -v file > /dev/null 2>&1; then
		mkdir -p "$MH/.config/mc"
		cp /etc/mc/mc.ext.ini "$MH/.config/mc/mc.ext.ini"
		cmake -DMODE=add -DTARGET="$MH/.config/mc/mc.ext.ini" -DSNIPPET="$SRC/plugins/mc/mc.ext.ini.vbackup" "-DBEFORE=[Default]" \
			-P "$SRC/cmake/plugcfg.cmake" > /dev/null
		L_MAGIC="$SRC/plugins/magic/vbackup.magic:$(file --version | sed -n 's/^magic file from //p')"
		for N in w.sav Works.save noname; do
			rm -rf "assoc.$N" && mkdir "assoc.$N" && cp x.bck "assoc.$N/$N"
			TMUXSESS=vbkplug-mca-$$
			tmux new-session -d -s $TMUXSESS -x 140 -y 32 "cd '$S/assoc.$N' && HOME='$MH' PATH='$PATH' MAGIC='$L_MAGIC' TERM=xterm exec mc -u '$S/assoc.$N' '$S/assoc.$N'"
			waitfor $TMUXSESS "$N" 15
			tmux send-keys -t $TMUXSESS Down
			sleep 1
			tmux send-keys -t $TMUXSESS Enter
			check 'waitfor $TMUXSESS "uvbk:" 15' "mc: Enter on $N does not open the saveset: $(tmux capture-pane -t $TMUXSESS -p | head -3)"
			tmux send-keys -t $TMUXSESS F10
			sleep 1
			tmux kill-session -t $TMUXSESS 2>/dev/null
			TMUXSESS=
		done
	fi
else
	echo "%VBACKUP-W-PLUGINS, no tmux or no mc: the real Midnight Commander not tried"
fi

if command -v tmux > /dev/null 2>&1 && command -v far2l > /dev/null 2>&1; then
	FP=$S/farprof
	mkdir -p "$FP/.config/plugins/multiarc" farout
	cp "$INI" "$FP/.config/plugins/multiarc/custom.ini"
	TMUXSESS=vbkplug-far-$$
	tmux new-session -d -s $TMUXSESS -x 140 -y 32 "cd '$S' && PATH='$PATH' TERM=xterm exec far2l --tty -u '$FP' '$S' '$S/farout'"
	sleep 3
	#	What a fresh far2l asks before the panels: revive an instance left over (no - a new one),
	#	OSC52 for the clipboard (no), the Getting Started help (closed)
	i=0
	while [ $i -lt 8 ]; do
		SCR=$(tmux capture-pane -t $TMUXSESS -p 2>/dev/null)
		case "$SCR" in
			*"lost in space"*|*"instance index to revive"*)	tmux send-keys -t $TMUXSESS Enter ;;
			*OSC52*|*"Getting Started"*)	tmux send-keys -t $TMUXSESS Escape ;;
			*x.bck*)			break ;;
		esac
		sleep 1
		i=$((i + 1))
	done
	#	The left panel: .., bin, far.j, ... - x.bck sorted after the directories; End puts the cursor on the last entry
	tmux send-keys -t $TMUXSESS End
	sleep 1
	if tmux capture-pane -t $TMUXSESS -p | tail -6 | grep -q "x.bck"; then
		tmux send-keys -t $TMUXSESS C-PageDown
		check 'waitfor $TMUXSESS "VBACKUP:x.bck" 15' "far2l: MultiArc does not open the saveset: $(tmux capture-pane -t $TMUXSESS -p | head -4)"
		tmux send-keys -t $TMUXSESS Down
		sleep 1
		tmux send-keys -t $TMUXSESS Enter
		check 'waitfor $TMUXSESS "a b.txt" 10' "far2l: the folder of the saveset is not shown"
		#	.., sub, с пробелом, a b.txt, link - files after folders: End is link, one up is "a b.txt"
		tmux send-keys -t $TMUXSESS End
		sleep 1
		tmux send-keys -t $TMUXSESS Up
		sleep 1
		tmux send-keys -t $TMUXSESS F5
		sleep 2
		tmux send-keys -t $TMUXSESS Enter
		i=0; while [ $i -lt 15 ] && [ ! -s "farout/a b.txt" ]; do sleep 1; i=$((i + 1)); done
		check 'cmp -s "farout/a b.txt" "tree/a b.txt"' "far2l: F5 did not extract the file: $(ls farout)"
	else
		fail "far2l: the cursor could not be put on x.bck: $(tmux capture-pane -t $TMUXSESS -p | tail -6)"
	fi
	tmux send-keys -t $TMUXSESS F10
	sleep 1
	tmux kill-session -t $TMUXSESS 2>/dev/null
	TMUXSESS=
	farstop
else
	echo "%VBACKUP-W-PLUGINS, no tmux or no far2l: the real far2l not tried"
fi

#	Read only, whatever was done to it
check '[ "$(sha256sum x.bck | cut -c1-64)" = "$SUM0" ]' "a plugin changed the saveset"

tap_end
