# VBACKUP

An OpenVMS BACKUP-style saveset utility for Linux: save files into a
saveset, restore them, list and compare them -- one command, the
qualifiers of BACKUP.

    $ vbackup /home/rrl /backup/rrl.bck /VERIFY
    $ vbackup /backup/rrl.bck /LIST
    $ vbackup /backup/rrl.bck /tmp/restore

What a saveset gives you:

- files with owners, permissions, nanosecond times, extended
  attributes, POSIX ACLs, capabilities, chattr flags; hard links,
  symbolic links, sparse files, FIFOs, devices;
- a checksum of every block and of every file: damage is always found;
- XOR blocks (`/GROUP_SIZE`): one bad block in a group is rebuilt, and
  after a loss the reader picks the stream up again at the next good
  block - only the files that lay in the lost blocks are hurt;
- volumes (`/VOLUME_SIZE=4G`), each of which identifies itself;
- a catalog at the end: `/LIST` and `/EXTRACT` are instant;
- incremental saves: `/RECORD` keeps a journal, `/SINCE=BACKUP` saves
  what changed since, and a chain restored `/INCREMENTAL` comes back with
  deleted files gone;
- a copy disk to disk with everything a restore would give back;
- `vbkx`, a stand-alone extractor linked statically: list, extract and
  test a saveset where VBACKUP is not installed (`vbackup /HELP VBKX`);
- no dependency beyond libc, StarLet and HELP.

The format is described in [doc/format.md](doc/format.md); the
design in [doc/DESIGN.md](doc/DESIGN.md) (Russian).  The user manual
is the help library: `vbackup /HELP`, sources in
[doc/vbackuplib.md](doc/vbackuplib.md) and
[doc/vbackuplib_ru.md](doc/vbackuplib_ru.md).

New to all this?  The simple guides, task by task, with commands to
copy: [English](doc/simple/vbackup-en.md), [Русский](doc/simple/vbackup-ru.md),
[Español](doc/simple/vbackup-es.md).

## For geeks: vbkx-go, vbkx-rs, vbkx-pl

Two more extractors of last resort, for the day everything else is
dead: [tools/go/main.go](tools/go/main.go) and
[tools/rust/src/main.rs](tools/rust/src/main.rs).  One file each, the
standard library only, plain on purpose so that they can be read and
fixed against [doc/format.md](doc/format.md).  They list (`l`, times in
UTC), extract (`x`) and test (`t`) a saveset in one pass, repair one bad
block per group, pick the stream up after a loss, find the block size by
trying when volume 1 lost its first block, and name every damaged or
missing file.  They read compressed savesets too (DATAZ, the LZ4 block
format, each with its own checked decoder).  The head of each source is
its manual.

    $ go build -o vbkx-go main.go                         # in tools/go
    $ rustc -O -C strip=symbols -o vbkx-rs src/main.rs    # in tools/rust

`make` in either directory does the same; `cargo build --release` works
for Rust too.  The CMake build makes them when Go or Rust is found.

And a third, with nothing to build at all: [tools/perl/vbkx.pl](tools/perl/vbkx.pl),
`vbkx-pl`.  It needs only the Perl that every Debian and Ubuntu system
carries (the `perl-base` package: strict, warnings, Fcntl, POSIX), does
the same as the two above with the same listing, and is slow (a few MB a
second) and sure.  Copy it next to the saveset:

    $ perl vbkx.pl l /mnt/usb/home.bck
    $ perl vbkx.pl x /mnt/usb/home.bck -C /tmp/restore

## vbkx on Windows

`vbkx.exe` reads savesets on Windows, with the same commands as `vbkx`.
It needs no StarLet and no CMake; with MinGW-w64, from the top of the
source tree:

    $ x86_64-w64-mingw32-gcc -O2 -Ilib -o vbkx.exe tools/vbkx.c \
          lib/vbkfmt.c lib/vbkrd.c lib/vbklz4.c -static -lshell32
    $ make -f tools/Makefile.win                     # the same
    $ cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake

On Windows itself the gcc of MinGW-w64 or MSYS2 takes the same line.  It
puts back data, times, read-only files, directories and hard links;
symbolic links where Windows allows them (developer mode).  A name
Windows cannot hold (`a:b`, `x?`, `con.txt`, ...) is not extracted, and
said.  `test/win.sh` runs it under wine.

## Build

Requires StarLet 1.6 or later; HELP 1.0 (optional) for `/HELP` and the
help libraries.

    $ cmake -S . -B build
    $ cmake --build build
    $ (cd build && ctest)
    $ cmake --install build

`make kit` in the build tree makes `vbackup-<ident>.tar.gz`.

The tests speak TAP (the Test Anything Protocol) when `TAP=1` is set,
for prove, Jenkins, GitLab and their kin; without it they print only
the failures:

    $ TAP=1 build/test/units /tmp/u.d
    $ prove -e "env TAP=1 VBACKUP=build/vbackup VBKX=build/vbkx SCRATCH=/var/tmp/s sh" test/smoke.sh

## Author

StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan).
