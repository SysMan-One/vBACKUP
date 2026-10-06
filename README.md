# VBACKUP

Русская версия: [README_ru.md](README_ru.md)

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
- XOR blocks (`/GROUP_SIZE`): one bad block in a group is rebuilt -
  with `/PARITY=m` (Reed-Solomon) any m of them, side by side too - and
  after a loss the reader picks the stream up again at the next good
  block - only the files that lay in the lost blocks are hurt;
- volumes (`/VOLUME_SIZE=4G`), each of which identifies itself - on
  disk, through a pipe, or straight onto another node
  (`vbackup /home host::/backup/home.bck`, as DECnet wrote it);
- a catalog at the end: `/LIST` and `/EXTRACT` are instant;
- incremental saves: `/RECORD` keeps a journal, `/SINCE=BACKUP` saves
  what changed since, and a chain restored `/INCREMENTAL` comes back with
  deleted files gone;
- a copy disk to disk with everything a restore would give back;
- compression (`/DATA_FORMAT=COMPRESSED`, LZ4 on all cores), a whole
  device block by block (`/PHYSICAL`) or a whole file system (`/IMAGE`);
- encryption (`/ENCRYPT`, `/KEY_FILE`): ChaCha20 and HMAC-SHA256 per
  block, keys by PBKDF2 - the names of the files hidden too, damage
  still repaired without the passphrase, a block changed on purpose
  recognized; no crypto library, so every reader below reads it;
- `vbkx`, a stand-alone extractor linked statically: list, extract and
  test a saveset where VBACKUP is not installed (`vbackup /HELP VBKX`);
- the savesets of OpenVMS BACKUP read too: listed as `BACKUP/LIST`
  lists them, their files restored with Linux names and their texts made
  texts, bad blocks rebuilt from their XOR groups - by vbackup, vbkx,
  vbkx.exe and Midnight Commander; not by far2l, the WCX plugin or the
  extractors for the geeks (`vbackup /HELP OPENVMS`,
  [doc/vmsbackup.md](doc/vmsbackup.md));
- no dependency beyond libc, StarLet and HELP.

The format is described in [doc/format.md](doc/format.md); the
design in [doc/DESIGN.md](doc/DESIGN.md) (Russian).  The user manual
is the help library: `vbackup /HELP`, sources in
[doc/vbackuplib.md](doc/vbackuplib.md) and
[doc/vbackuplib_ru.md](doc/vbackuplib_ru.md).

New to all this?  The simple guides, task by task, with commands to
copy: [English](doc/simple/vbackup-en.md), [Русский](doc/simple/vbackup-ru.md),
[Español](doc/simple/vbackup-es.md).

## Why VBACKUP rather than tar, dar, borg or dd

Each of them is good at what it was made for.  VBACKUP was made for one
thing above speed: that what was saved comes back - from a damaged disk,
on a machine that has nothing installed, years later.

**Damage is repaired, not only found.**  Every block and every file has a
checksum, so damage is never silent.  The XOR block of each group
(`/GROUP_SIZE`) rebuilds one bad block of the group, without asking -
`/PARITY=m` adds Reed-Solomon blocks, and any m bad blocks of a group are
rebuilt, a burst of them too; with more lost, the reader picks the stream up again at the next good block, and
only the files that lay in the lost blocks are hurt - and named.  A
compressed tar is unreadable after its first bad byte; a plain tar checks
its headers only, so damaged contents come back wrong and unnoticed.  dar
repairs only through par2 files made beside it; borg and restic find
damage, and a bad chunk of a deduplicated repository hurts every backup
that shares it.

**Readable without VBACKUP.**  The format is a public, complete
specification ([doc/format.md](doc/format.md)), and there are four
independent readers of it besides VBACKUP: `vbkx` (static C, also
`vbkx.exe` for Windows) and `vbkx-go`, `vbkx-rs`, `vbkx-pl`, each one
source file with no dependencies beyond the language's standard library.
A rescue system with a C compiler, or Go, or Rust, or only Perl, reads a
saveset - encrypted ones too.

**Encryption that does not cost the repair.**  ChaCha20 and HMAC-SHA256 per
block, keys by PBKDF2: the checksums and the XOR groups lie over the
ciphertext, so a damaged encrypted saveset is repaired without the
passphrase, and a block changed on purpose is recognized and rebuilt.
No crypto library is used, so every reader above reads it.

**Instant listing and extraction.**  The catalog at the end of the saveset
lists hundreds of gigabytes at once, and one file is reached through its
place in the catalog - tar must read everything before it.  Through a pipe
or a damaged saveset without its catalog, the stream is read instead.

**One tool for files, volumes and devices.**  Files with everything Linux
gives them (owners, nanosecond times, xattrs, ACLs, capabilities, chattr
flags, hard links, sparse files); `/IMAGE`, a whole file system made again
with its label and UUID; `/PHYSICAL`, a device block by block, zero pieces
skipped, with the same checksums, repair, compression and encryption
that dd, partclone and Clonezilla images do not have.  Volumes of any size,
each identifying itself; incremental chains with a journal, a restore that
deletes what was deleted; a copy disk to disk; savesets through ssh.

**The commands of OpenVMS BACKUP.**  `/SAVE_SET`, `/SINCE=BACKUP`,
`/RECORD`, `/INCREMENTAL`, `/IMAGE`, `/PHYSICAL`, `/VERIFY`, `/LIST`,
`/SELECT`, `/EXCLUDE` - with HELP, messages of the VMS form and a reference
manual in the manner of DEC ([doc/ref](doc/ref/vbackup_ref.md)).

**A bridge from OpenVMS.**  A saveset written by OpenVMS BACKUP - off an
old disk, a tape copied into a file, an archive of a VAX or an Alpha - is
read on Linux: `vbackup USERS.BCK /LIST /FULL` prints what `BACKUP/LIST
/FULL` prints, octet for octet; a restore gives its files Linux names
(`[SMITH]LOGIN.COM;3` is `SMITH/LOGIN.COM`, older versions keep `;n`),
makes its texts texts and repairs its bad blocks from its XOR groups as
BACKUP would.  `vbkx` and `vbkx.exe` read it too.  None of the others
does.

**Scheduling and rotation are left to a batch queue.**  BATCH (the batch
job subsystem) runs a save every night the way OpenVMS does - a job that
submits itself again for the next day, keeps its log, and stays in the
queue when it fails (`vbackup /HELP SCHEDULING`).

**In the file managers.**  Midnight Commander, far2l and Far, Total and
Double Commander open a saveset like a folder (read only).

| | VBACKUP | tar (.gz) | dar | borg / restic | dd / partclone |
|---|---|---|---|---|---|
| Checksum of every block and file | yes | headers only | yes | yes | no |
| Repairs bad blocks by itself | yes: 1 a group, or m with `/PARITY=m` | no | with par2 | no | no |
| Damage spreads | lost blocks only | to the end (.gz) | slice | shared chunks | - |
| Readers without the tool | 4, no dependencies | many | no | no | many |
| Encryption, repair without the key | yes, no library | no | libgcrypt | yes, no | no |
| Listing of a large backup | catalog, instant | reads it all | catalog | index | - |
| Volumes | yes | multi-volume, awkward | slices | - | split |
| Whole device / file system | `/PHYSICAL`, `/IMAGE` | no | no | no | yes |
| Reads OpenVMS BACKUP savesets | yes, with repair | no | no | no | no |
| Deduplication | **no** | no | no | **yes** | no |
| Remote repository | pipe through ssh | pipe | pipe | **yes** | pipe |

**Where others are better.**  borg and restic deduplicate - a hundred
daily backups of a large tree cost little more than one - and keep a
remote repository; VBACKUP makes self-contained savesets, one per save,
for media you put on a shelf.  tar is everywhere and every tool reads it.
VBACKUP is young: its format is fixed and tested by its own damage tests,
but it has not had the decades of use the others have.

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
format, each with its own checked decoder) and encrypted ones (`-k
keyfile`, else `VBACKUP_KEY_FILE`, else the passphrase is asked for on
the terminal): every block checked by its TAG before it is decrypted, a
forged block rebuilt from its group like a bad one; what the language
does not give is written out in each file, and `selftest` checks the
primitives against the vectors of their standards.  The head of each
source is its manual.

    $ go build -o vbkx-go main.go                         # in tools/go
    $ rustc -O -C strip=symbols -o vbkx-rs src/main.rs    # in tools/rust

`make` in either directory does the same; `cargo build --release` works
for Rust too.  The CMake build makes them when Go or Rust is found.

And a third, with nothing to build at all: [tools/perl/vbkx.pl](tools/perl/vbkx.pl),
`vbkx-pl`.  It needs only the Perl that every Debian and Ubuntu system
carries (the `perl-base` package: strict, warnings, Fcntl, POSIX), does
the same as the two above with the same listing, and is slow (a few MB a
second) and sure.  For an encrypted saveset it uses Digest::SHA when it
is there and its own SHA-256 when it is not.  Copy it next to the saveset:

    $ perl vbkx.pl l /mnt/usb/home.bck
    $ perl vbkx.pl x /mnt/usb/home.bck -C /tmp/restore

A saveset made with `/PHYSICAL` comes out of every extractor as the image
file of the device; one made with `/IMAGE` as the plain tree of files of
the volume.

## vbkx on Windows

`vbkx.exe` reads savesets on Windows, with the same commands as `vbkx`.
It needs no StarLet and no CMake; with MinGW-w64, from the top of the
source tree:

    $ x86_64-w64-mingw32-gcc -O2 -Ilib -o vbkx.exe tools/vbkx.c \
          lib/vbkfmt.c lib/vbkrd.c lib/vbklz4.c lib/vbkcrp.c lib/vbkvms.c lib/vbkrs.c -static -lshell32
    $ make -f tools/Makefile.win                     # the same
    $ cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake

On Windows itself the gcc of MinGW-w64 or MSYS2 takes the same line.  It
puts back data, times, read-only files, directories and hard links;
symbolic links where Windows allows them (developer mode).  A name
Windows cannot hold (`a:b`, `x?`, `con.txt`, ...) is not extracted, and
said.  `test/win.sh` runs it under wine.

## vbackup on Windows

`vbackup.exe` is the whole utility on Windows - save, restore, list,
compare, the journal and incremental savesets, encryption, parity,
volumes, pipes - and its savesets go both ways between Linux and
Windows.  It is built on Linux with MinGW-w64 and the StarLet sources
(StarLet itself is not changed for it):

    $ cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
          -DVBACKUP_STARLET_SRC=/root/Works/starlet-1.6.8
    $ cmake --build build-win                        # vbackup.exe, vbkx.exe, units.exe

The Linux build makes it by itself when MinGW-w64 and the StarLet kit
(`../starlet-x.y.z`) are there, and `test/winutil.sh` runs it under
wine, both ways.  Names are Unicode, paths of any length; data and
sparse files, the four times, the NTFS attributes, the security
descriptor (owner, group, DACL, SACL), the alternate data streams (as
`user.` attributes), directories, hard links, symbolic links and
junctions are saved.  Run as an administrator it is a backup operator:
it reads every file and puts the owners and the ACL back.  `node::file`
goes through `ssh.exe`; `/PHYSICAL` and `/IMAGE` are Linux only.  A
name Windows cannot hold is not restored, and said;
two names that differ in case only - one file there - are never
written over each other.  `vbackup /HELP WINDOWS` tells the rest.

## File managers

A saveset opens like a folder in the file managers below: list, view,
copy out, test.  Read only - nothing is ever written into a saveset.
The listing comes from the catalog, so a saveset of hundreds of
gigabytes opens at once, and a file is copied out through its place in
the catalog.

- **Midnight Commander** - [plugins/mc/uvbk](plugins/mc/uvbk), an extfs
  script over `vbackup`.  `cmake --install` puts it into the extfs of MC
  and its section into `mc.ext.ini`, when MC is there (and takes them out
  again on uninstall); then Enter on a `.bck` or `.sav` file - or on a
  saveset of any name: the installation adds the magic of VBACKUP to
  `/etc/magic`, and MC knows it by its contents.
- **far2l and Far3** - [plugins/far/vbackup.ini](plugins/far/vbackup.ini),
  a format description for MultiArc over `vbkx` (`vbkx.exe` on Windows).
  The installation appends it to the `custom.ini` of far2l's MultiArc.
- **Total Commander and Double Commander** - [plugins/wcx/vbkwcx.c](plugins/wcx/vbkwcx.c),
  a WCX packer plugin over the reading core: `vbackup.wcx64` and
  `vbackup.wcx` for Total Commander 64 and 32 bit, `vbackup.wcx` (a
  shared object) for Double Commander on Linux.  It checks the CRC of
  every file, repairs what the XOR blocks allow, names what they do not,
  asks for a volume that is not beside volume 1, and keeps a forged
  saveset inside the target.  Install in TC: open a zip of the plugin
  with [pluginst.inf](plugins/wcx/pluginst.inf) (`make -f plugins/wcx/Makefile zip`),
  or Configuration -> Options -> Packer -> Configure packer extension
  WCXs, extension `bck`.  In DC: Options -> Plugins -> Packer plugins
  (WCX) -> Add, extension `bck`.  The head of the source is its manual.

      $ make -f plugins/wcx/Makefile linux win64 win32   # out/linux, out/win64, out/win32

  The CMake build makes the Linux one always (installed under
  `share/vbackup/plugins/wcx`) and the Windows ones when MinGW-w64 is
  found; `test/wcx.sh` checks them against `vbkx` - natively and under
  wine.

## Build

Requires StarLet 1.6 or later; HELP 1.0 (optional) for `/HELP` and the
help libraries.

    $ cmake -S . -B build
    $ cmake --build build
    $ (cd build && ctest)
    $ cmake --install build

`make kit` in the build tree makes `vbackup-<ident>.tar.gz` - in a git
tree by git archive of HEAD, so only what is committed goes into it.

The tests speak TAP (the Test Anything Protocol) when `TAP=1` is set,
for prove, Jenkins, GitLab and their kin; without it they print only
the failures:

    $ TAP=1 build/test/units /tmp/u.d
    $ prove -e "env TAP=1 VBACKUP=build/vbackup VBKX=build/vbkx SCRATCH=/var/tmp/s sh" test/smoke.sh

## Author

StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan).
