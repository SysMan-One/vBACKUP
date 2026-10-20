#!/bin/sh
#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		tools/mkrelease.sh
#
#	ABSTRACT:	The files of a release, made from a tag: the sources
#			(vbackup-<tag>.tar.gz, git archive of the tag), the images
#			for Windows with their licenses (vbackup-<tag>-win64.zip),
#			SHA256SUMS of both, and NOTES.md - the text of the release.
#			.github/workflows/release.yml publishes them; here the same
#			thing is made by hand and looked at before.
#
#	DESCRIPTION:	sh tools/mkrelease.sh tag starlet-src out
#
#			tag		X01-24 and so on, a tag of this repository
#			starlet-src	the source kit of StarLet compiled into
#					vbackup.exe (VBACKUP_STARLET_SRC)
#			out		where the files go (made); the build is in
#					out/build and stays there
#
#			An installed StarLet for the Linux side of the build is
#			found by CMake as usual (CMAKE_PREFIX_PATH); MinGW-w64,
#			x86_64 and i686 (the 32-bit plugin), and zip are needed.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	20-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		20-OCT-2026	RRL	X-01 : Initial version.
#
#---

set -eu

if [ $# -ne 3 ]
then
	echo "Usage: sh tools/mkrelease.sh tag starlet-src out" >&2
	exit 2
fi

TAG=$1
SLSRC=$(cd "$2" && pwd)
mkdir -p "$3"
OUT=$(cd "$3" && pwd)
TOP=$(cd "$(dirname "$0")/.." && pwd)
SRC=vbackup-$TAG
WIN=vbackup-$TAG-win64

git -C "$TOP" rev-parse -q --verify "refs/tags/$TAG" > /dev/null || { echo "Tag: $TAG - no such tag" >&2; exit 1; }

#	The sources: the tag as it is, nothing of the working tree
rm -rf "$OUT/$SRC" "$OUT/$WIN" "$OUT/build" "$OUT/inst"
git -C "$TOP" archive --format=tar.gz --prefix="$SRC/" -o "$OUT/$SRC.tar.gz" "$TAG"
tar xzf "$OUT/$SRC.tar.gz" -C "$OUT"

#	The images for Windows: the build of the tag with StarLet compiled in
cmake -S "$OUT/$SRC" -B "$OUT/build" -DVBACKUP_STARLET_SRC="$SLSRC" \
	'-DVBACKUP$M_GEEKS=OFF' '-DVBACKUP$M_PLUGINS=OFF' '-DVBACKUP$M_HELPLIB=OFF' > "$OUT/cmake.log" 2>&1 || { tail -20 "$OUT/cmake.log" >&2; exit 1; }
cmake --build "$OUT/build" -j "$(nproc)" > "$OUT/build.log" 2>&1 || { grep -E 'error|Error' "$OUT/build.log" | head -20 >&2; exit 1; }

[ -f "$OUT/build/winexe/vbackup.exe" ] || { echo "File: vbackup.exe - not built: no MinGW-w64, or no StarLet sources" >&2; exit 1; }
[ -f "$OUT/build/win32/vbackup.wcx" ] || { echo "File: vbackup.wcx - not built: no i686-w64-mingw32-gcc" >&2; exit 1; }

DESTDIR="$OUT/inst" cmake --install "$OUT/build/winexe" --prefix / > /dev/null

#	winpthreads is in vbackup.exe: its notice goes with it (NOTICE, COPYING.MinGW-w64)
mkdir "$OUT/$WIN"
cp "$OUT/inst/bin/vbackup.exe" "$OUT/inst/bin/vbkx.exe" "$OUT/inst/bin/vbackup.wcx64" "$OUT/build/win32/vbackup.wcx" \
	"$OUT/$SRC/plugins/wcx/pluginst.inf" "$OUT/$SRC/README.md" "$OUT/$WIN/"
cp "$OUT/inst/share/doc/vbackup/LICENSE" "$OUT/inst/share/doc/vbackup/NOTICE" "$OUT/inst/share/doc/vbackup/COPYING.MinGW-w64" "$OUT/$WIN/"

rm -f "$OUT/$WIN.zip"
(cd "$OUT" && zip -qr -X "$WIN.zip" "$WIN")
(cd "$OUT" && sha256sum "$SRC.tar.gz" "$WIN.zip" > SHA256SUMS)

#	The text: the message of the tag, what the files are, their sums
L_IDENT=$(grep -m1 '^#define[[:space:]]*__IDENT__' "$OUT/$SRC/src/vbackup.c" | sed 's/.*"\(.*\)".*/\1/')
L_SLVER=$(grep -m1 'project(StarLet' "$SLSRC/CMakeLists.txt" | sed 's/.*VERSION \([0-9.]*\).*/\1/')
L_CC=$(x86_64-w64-mingw32-gcc -dumpversion)

{
	git -C "$TOP" tag -l --format='%(contents)' "$TAG" | sed '/^-----BEGIN PGP/,$d'
	echo
	echo "## Files"
	echo
	echo "- \`$SRC.tar.gz\` - the sources of the tag; built with StarLet (https://github.com/SysMan-One/utility_routines)."
	echo "- \`$WIN.zip\` - vbackup.exe ($L_IDENT, StarLet $L_SLVER compiled in), vbkx.exe (the stand-alone extractor),"
	echo "  the Total Commander plugin (vbackup.wcx64, vbackup.wcx, pluginst.inf), LICENSE, NOTICE, COPYING.MinGW-w64,"
	echo "  README.md. Built by MinGW-w64 (GCC $L_CC), statically."
	echo
	echo "SHA-256:"
	echo
	echo '```'
	cat "$OUT/SHA256SUMS"
	echo '```'
} > "$OUT/NOTES.md"

echo "Release: $TAG - $OUT/$SRC.tar.gz, $OUT/$WIN.zip, $OUT/SHA256SUMS, $OUT/NOTES.md"
