#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		test/tap.sh
#
#	ABSTRACT:	The check helpers of the functional tests, read in by
#			smoke.sh, damage.sh and geeks.sh: plain output, or TAP
#			(the Test Anything Protocol) when TAP is set.
#
#	DESCRIPTION:	The script sets TAPNAME (SMOKE, DAMAGE, GEEKS) and reads
#			this file in:  . "$(dirname "$0")/tap.sh"
#
#			    ok "what"			a check passed
#			    fail "what"			a check failed (the first line
#							is its name, the rest notes)
#			    check 'condition' "what"	either, by the condition
#			    bail "why"			the test cannot go on
#			    tap_end			the totals, the plan, exit
#
#			Plain (TAP unset): only the failures are printed, as
#			%VBACKUP-E-<TAPNAME>, then "N checks, M failures"; the
#			completion code is the number of failures.
#
#			TAP=1: "TAP version 13", one "ok N - what" or
#			"not ok N - what" per check, the plan "1..N" at the end
#			(any TAP consumer takes a trailing plan: prove, Jenkins,
#			GitLab).  Whatever else the script and the programs it
#			runs print goes to a scratch file and is passed on as
#			"# ..." notes before the next check, so the stream stays
#			valid TAP.  A check is named by its text, which says what
#			goes wrong - read "ok 5 - save: no saveset" as "the check
#			against 'save: no saveset' passed".  A "#" in a name is
#			escaped ("\#"): in TAP it begins a directive.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 4-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 4-OCT-2026	RRL	X-01 : Initial version.
#
#---

FAILS=0
CHECKS=0
TAPNAME=${TAPNAME:-TEST}

if [ -n "${TAP:-}" ]; then
	TAPDIAG=$(mktemp "${TMPDIR:-/tmp}/vbktap.XXXXXX") || exit 1
	exec 3>&1 1>>"$TAPDIAG"
	echo "TAP version 13" >&3
fi

#	What was printed since the last check, as notes
tap_diag () {
	[ -n "${TAP:-}" ] || return 0
	[ -s "$TAPDIAG" ] && sed 's/^/# /' "$TAPDIAG" >&3
	: > "$TAPDIAG"
}

ok () {
	CHECKS=$((CHECKS + 1))

	if [ -n "${TAP:-}" ]; then
		tap_diag
		printf 'ok %d - %s\n' "$CHECKS" "$(printf '%s\n' "${1:-check $CHECKS}" | head -n 1 | sed 's/#/\\#/g')" >&3
	fi
}

fail () {
	CHECKS=$((CHECKS + 1))
	FAILS=$((FAILS + 1))

	if [ -n "${TAP:-}" ]; then
		tap_diag
		printf 'not ok %d - %s\n' "$CHECKS" "$(printf '%s\n' "$*" | head -n 1 | sed 's/#/\\#/g')" >&3
		printf '%s\n' "$*" | tail -n +2 | sed 's/^/# /' >&3
	else
		echo "%VBACKUP-E-$TAPNAME, $*"
	fi
}

check () {
	if eval "$1"; then ok "$2"; else fail "$2"; fi
}

bail () {
	if [ -n "${TAP:-}" ]; then
		tap_diag
		echo "Bail out! $*" >&3
		rm -f "$TAPDIAG"
	else
		echo "%VBACKUP-F-$TAPNAME, $*"
	fi

	exit 1
}

tap_end () {
	echo "$CHECKS checks, $FAILS failures"

	if [ -n "${TAP:-}" ]; then
		tap_diag
		echo "1..$CHECKS" >&3
		rm -f "$TAPDIAG"
	fi

	exit $FAILS
}
