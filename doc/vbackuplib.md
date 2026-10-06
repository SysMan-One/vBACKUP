# VBACKUP -- save files into a saveset, restore them, compare them

VBACKUP keeps copies of your files in a saveset. A saveset is one
file, or several files called volumes. It holds the files, their
owners, their permissions, their times, their extended attributes and
ACLs. It also holds checksums, so damage is always found, and spare
blocks, so small damage is repaired.

VBACKUP works like the BACKUP command of OpenVMS. You give it what to
read and where to write:

```
VBACKUP input-spec[,...] output-spec [/qualifiers]
```

What it does depends on the two sides:

```
files    ->  saveset       save          vbackup /home/rrl rrl.bck
saveset  ->  directory     restore       vbackup rrl.bck /tmp/back
saveset  /LIST             list          vbackup rrl.bck /LIST
saveset  /COMPARE          compare       vbackup rrl.bck /COMPARE
saveset  /EXTRACT=name     one file out  vbackup rrl.bck /EXTRACT=rrl/a.txt
files    ->  directory     copy          vbackup /home/rrl /mnt/copy
saveset  /RECORD           to journal    vbackup rrl.bck /RECORD
/JOURNAL /LIST             journal       vbackup /JOURNAL /LIST
```

VBACKUP knows a saveset by what is inside it, not by its name. When
you save, it knows that the output is a saveset because the name ends
in .bck, because you give /SAVE_SET, or because the name is "-" (the
standard output).

Put wildcards in quotes. VBACKUP expands them itself:

```
$ vbackup '/home/rrl/.../*.c' sources.bck
```

A qualifier may be shortened while it stays clear: /VER for /VERIFY.
It may also be written the Unix way: --VERIFY. A word that starts with
a slash is a qualifier only when it names one, so /etc and /home are
file names. A lone \`--\` ends the qualifiers.

The exit code is 0 when all went well, 1 when there were warnings
(for example a file changed while it was saved), and 2 when something
was not done (for example a file could not be read).

## Parameters

### input-spec

When you save: the files and directories to save, separated by
commas. A directory is saved with everything in it. See the topic
WILDCARDS.

When you restore, list, compare or extract: the saveset. Give the
name of its first volume; VBACKUP finds the other volumes by itself.
A saveset written by OpenVMS BACKUP is read too: see the topic
OPENVMS.

A saveset may come through a pipe: "-" is the standard input.

```
ssh host 'vbackup /home -' | vbackup - /restore
vbackup /home - | ssh host 'vbackup - /backup/home.bck'
```

Restore, /LIST, /COMPARE, /EXTRACT and vbkx read it as it comes, once,
forward only; the catalog at its end is of no use there, so a file of
/EXTRACT is found by reading. Bad blocks are repaired as from a file.
A saveset written to "-" carries its volumes (/VOLUME_SIZE) back to
back; a pipe that stops before the saveset ends gives NOTRAILER.

A saveset on another node, as DECnet wrote it - node::file:

```
vbackup /home host::/backup/home.bck /VOLUME_SIZE=4G /VERIFY
vbackup host::/backup/home.bck /restore
vbackup host::/backup/home.bck /LIST
```

VBACKUP runs there too, through ssh (VBACKUP_RSH names another
command), and the saveset goes through a pipe: a save is split into its
volume files there, each block checked as it arrives; /VERIFY reads it
back from there and compares it with the disk here. The messages of the
other side come here; its completion code becomes this one's
(REMOTEERR). The other node needs VBACKUP X01-11 or later on the PATH
of ssh, and must let this one in by its keys. /DELETE and /LIST are not
taken with a saveset made on another node.

When you restore, several savesets may be given, separated by commas:
they are restored one after the other. With /INCREMENTAL give the full
saveset first, then the incremental ones in the order they were made.

When you copy: the files and directories to copy, as for a save.

### output-spec

When you save: the saveset to create. If it exists already, VBACKUP
stops; give /REPLACE to overwrite it.

When you restore: the directory to restore into. It is created if
needed.

When you compare: the directory to compare with. Without it, the
files are compared with the places they were saved from.

When you extract: the file to write. Without it, or with "-", the
file goes to the standard output.

When you copy: the directory to copy into. An output that is not a
saveset (no .bck or .sav, no /SAVE_SET) means a copy, as in BACKUP.
The files keep everything a restore would give them back.

Only these two parameters are taken. A third word is an error
(%VBACKUP-E-MAXPARM): most often it is a qualifier typed without its
slash - ".log" for "/LOG".

Qualifiers may be glued to a parameter, as in DCL: "x.sav/sav/log" is
"x.sav /SAVE_SET /LOG". VBACKUP says so (%VBACKUP-I-GLUED), so that a
mistyped name of a file is not taken for a qualifier unseen. A word
that is the name of an existing file is never cut; a qualifier whose
value holds a slash (/JOURNAL=/var/...) must be given apart.

## /SAVE_SET -- the output is a saveset

Says that the output-spec is a saveset, whatever its name. You do not
need it when the name ends in .bck or .sav (the names savesets have on
OpenVMS).

## /BLOCK_SIZE -- the size of a block

```
/BLOCK_SIZE=n
```

A saveset is made of blocks of n bytes. n is 8192 to 1048576, a
multiple of 512. The default is 65536. Bigger blocks are a bit faster;
smaller blocks lose less when one goes bad.

## /GROUP_SIZE -- spare blocks for repair

```
/GROUP_SIZE=n
```

After every n blocks of data VBACKUP writes one XOR block. If one
block of a group goes bad, it is rebuilt from the others when the
saveset is read. You see %VBACKUP-I-BLKFIXED and nothing is lost.

The default is 10: the saveset is 10% bigger. n may be 0 to 100;
/GROUP_SIZE=0 writes no XOR blocks and nothing can be repaired.

## /PARITY -- more than one bad block of a group repaired

```
/PARITY=m
```

Every group gets m parity blocks instead of the one XOR block: any m
bad blocks of a group - one next to another too, data or parity - are
rebuilt when the saveset is read (Reed-Solomon). /PARITY=1 is the
default, the XOR block alone. m may be 1 to 8; it needs groups, not
/GROUP_SIZE=0.

```
$ vbackup /home /mnt/usb/home.bck /PARITY=2
$ vbackup /home /mnt/tape/home.bck /GROUP_SIZE=20 /PARITY=4
```

The saveset grows by m/n: /GROUP_SIZE=10 /PARITY=2 makes it 20%
bigger, /GROUP_SIZE=20 /PARITY=4 too, and the second survives four bad
blocks in a row instead of two. Take it for media that fail in bursts:
old disks, USB sticks, optical discs, tapes.

A saveset with /PARITY=2 or more is of format version 2. VBACKUP and
vbkx before X01-14 do not read it: they say it is not a saveset.
VBACKUP X01-14 reads both. Encrypted savesets repair without the
passphrase, as always.

The parity is computed by the vector instructions of the processor
(AVX2 or SSSE3, NEON on ARM) where it has them, some ten times faster
than without; VBACKUP_NOSIMD=1 turns them off for trouble-shooting.

## /VOLUME_SIZE -- cut the saveset into volumes

```
/VOLUME_SIZE=size
```

Cuts the saveset into files of this size. Use it for a disk that
takes files up to 4 GB (FAT32), for upload, or for media of a fixed
size. The size may end with K, M, G or T:

```
$ vbackup /home home.bck /VOLUME_SIZE=4G
```

The volumes are named home.bck, home.bck.002, home.bck.003 and so on.
Keep them together in one directory. If a volume is lost, the files
in the other volumes can still be restored.

## /COMMENT -- a note in the saveset

```
/COMMENT="text"
```

Keeps the text in the saveset. /LIST shows it.

## /ORIGINAL -- restore the files where they came from

```
vbackup /mnt/usb/home.bck /ORIGINAL
vbackup /mnt/usb/home.bck /ORIGINAL /REPLACE     over the files that are there
```

No output is given: every file goes back to the directory it was saved
from (the saveset keeps it, as an absolute name, since X01-02). VBACKUP
says where the files go before it writes them. Files that are there
are kept unless you give /REPLACE.

A saveset made before X01-02 does not know where its files came from:
give an output directory (ORIGNOBASE). A saveset of several inputs puts
each file back under its own; /INCREMENTAL works with a saveset of one
input only.

Be careful with savesets you did not make yourself: /ORIGINAL writes
wherever the saveset says.

## /DELETE -- delete the files after saving them

```
vbackup /home/ivan/old /mnt/usb/old.bck /VERIFY /DELETE
```

Saves the files, reads the saveset back and compares it with the disk
(/VERIFY, which is required), and only then deletes the files from the
disk. A file is deleted only if the comparison found no differences at
all and the file has not changed since it was saved (same inode, size,
modification and change time); otherwise it is kept and SRCKEPT says
why. Directories are kept. /CONFIRM asks for each file; /LOG names each
file deleted.

/DELETE is refused with /PHYSICAL, /IMAGE, /SINCE and /BEFORE, and does
nothing when the saveset goes to the standard output (it cannot be
verified).

## /PHYSICAL -- a whole device, block by block

```
vbackup /dev/sdb1 /mnt/usb/sdb1.bck /PHYSICAL          save the device
vbackup /mnt/usb/sdb1.bck /dev/sdc1 /PHYSICAL /REPLACE restore onto a device
vbackup /mnt/usb/sdb1.bck /tmp/sdb1.img /PHYSICAL      restore into an image file
vbackup /mnt/usb/sdb1.bck /tmp/dir                     the same: dir/sdb1 is the image
```

Copies every block of a device - a partition or a whole disk - or of an
image file: any file system, encrypted volumes, boot areas. Blocks of
zeros take no room in the saveset. /DATA_FORMAT=COMPRESSED works with
it. Run it as root.

What can go wrong, and what VBACKUP does about it:

- A device that is written to while it is copied gives a broken copy,
  which looks fine until you need it. VBACKUP refuses a device that is
  mounted read-write, or whose partition is. Unmount it, mount it
  read-only, or copy a snapshot (LVM, btrfs).
- A device in use - a physical volume of LVM, a RAID member, a dm-crypt
  container, swap - is refused too.
- A restore overwrites the whole output device. VBACKUP does it only
  with /REPLACE, never onto a mounted or busy device, never onto one
  smaller than the device saved. At a terminal you must type YES. A
  script gives /REPLACE and is not asked.
- Onto a larger device the copy takes the first part; the file system
  keeps its old size (grow it with resize2fs, xfs_growfs, ...).
- The copy has the same labels and UUIDs as the original. Never mount
  both at the same time: xfs refuses it, btrfs can damage both.
- A lost block of the saveset is a lost part of the device: the restore
  says FILDAMAGED. Keep /GROUP_SIZE small and two copies of what counts.

/PHYSICAL takes one input and refuses /SELECT, /EXCLUDE, /SINCE,
/BEFORE, /BY_OWNER, /RECORD and /INCREMENTAL: a device is not a tree of
files.

## /IMAGE -- a whole file system, made again on restore

```
vbackup /mnt/data /mnt/usb/data.bck /IMAGE             save the volume
vbackup /dev/sdb1 /mnt/usb/data.bck /IMAGE             the same, by its device
vbackup /mnt/usb/data.bck /dev/sdc1 /IMAGE /REPLACE    make it again on a device
vbackup /mnt/usb/data.bck /tmp/dir                     just the files, into a directory
```

Saves every file of one file system - all of it, nothing of other file
systems mounted below it - and what makes it that volume: its type,
label, UUID and the owner and mode of its root. The input is the mount
point, or the device when it is mounted (read-only is enough). Run it
as root.

A restore with /IMAGE makes a new file system of the same type, label
and UUID on the output device (it runs mkfs.ext4, mkfs.xfs, mkfs.btrfs
or mkfs.vfat - that program must be installed), restores the files
into it and unmounts it. The device may be smaller or larger than the
one saved, as long as the files fit.

The guards are those of /PHYSICAL: /REPLACE, never onto a mounted or
busy device, YES at a terminal; the copy has the UUID of the original -
never mount both at once.

What /IMAGE is not: a copy of the boot sectors or the partition table
(save the whole disk with /PHYSICAL for that), nor of the inode
numbers. A volume written to while it is saved is consistent file by
file, not across files: save a snapshot when that matters. Other file
system types (minix, ntfs, ...): use /PHYSICAL.

/IMAGE works with /RECORD, /SINCE, /VERIFY and /DATA_FORMAT; it refuses
/SELECT, /EXCLUDE, /BY_OWNER, /INCREMENTAL, /PHYSICAL and, on restore,
/CONFIRM.

## /DATA_FORMAT -- compress the data

```
/DATA_FORMAT=COMPRESSED
/DATA_FORMAT=UNCOMPRESSED     (the default)
```

COMPRESSED makes the saveset smaller: text, programs and documents
often shrink to half, or less. /LEVEL says how hard (below); without
it COMPRESSED is /LEVEL=1, fast. Files that are compressed already
(photos, video, .gz, .zip) stay as they are - VBACKUP tries the first
64 KB of each piece and stores it plain when it does not shrink, which
costs almost no time. The checksums and the repair of bad blocks work
as always.

The compression runs on all the cores of the machine (at most 8); the
saveset is the same as with one. VBACKUP_ZTHREADS=n in the environment
sets the number of threads, 1 - none.

A restore, /LIST, /COMPARE, /EXTRACT and vbkx need nothing: they see
compressed data by themselves.

Note: VBACKUP before X01-04, and vbkx before X01-04, cannot read
compressed data. They do not write wrong files: they report the
compressed files as damaged (CRCERR, FILDAMAGED). Update them.

Example: vbackup /home /mnt/usb/home.bck /DATA_FORMAT=COMPRESSED

## /LEVEL -- how hard to compress

```
/LEVEL=n      n from 1 to 9; it compresses by itself
```

The higher, the smaller the saveset and the slower the save. A restore
is fast at every level.

```
/LEVEL=1        LZ4: fast, about half (the default of /DATA_FORMAT=COMPRESSED)
/LEVEL=2 .. 5   Deflate (the method of zip and gzip): smaller, slower
/LEVEL=6 .. 9   LZMA (the method of xz and 7-Zip): the smallest, slowest
```

Measured on one core (a text, 1 MB): LZ4 2.1 times smaller at 130 MB/s;
Deflate 3.2 to 3.7 times at 33 to 11 MB/s; LZMA at 6, 7 4.0 to 4.2 times
at 12 to 8 MB/s; at 8, 9 - the optimal parse of xz - 4.8 times at 2 MB/s,
as xz -6. All cores work (VBACKUP_ZTHREADS). Many small files compress
less than one tar of them: each file is compressed on its own.

Every piece VBACKUP compresses is decompressed again at once and
compared with the data; only a piece that comes back the same is
written compressed (else it is stored plain, and ZCHECK says so). Each
piece is compressed on its own: a bad block costs that piece alone,
and the repair of bad blocks works as always. vbkx and vbkx-go, -rs,
-pl read every level. VBACKUP and vbkx before X01-19 do not read levels
2 to 9: they say the files are damaged, they never write them wrong.

Example: vbackup /home /mnt/usb/home.bck /LEVEL=6

## /TRANSFER -- a saveset to a saveset, block for block

```
vbackup x.bck y.bck             a copy, volume for volume
vbackup x.bck -                 the volumes, back to back, to the output
vbackup - y.bck                 a stream into y.bck, y.bck.002, ...
```

When the input is a saveset and the output is one, the blocks are
copied as they are - never the records: an encrypted saveset needs no
passphrase, and the copy is byte for byte the original. Every block is
checked on the way; a bad one is copied as it is and said (BLKCOPIED) -
a restore repairs it from its group. /TRANSFER asks for this whatever
the names would mean (node::file uses it, so that an older VBACKUP on
the other node refuses rather than restores).

## /ENCRYPT -- make an encrypted saveset

```
vbackup /home/me me.bck /ENCRYPT
vbackup /home/me me.bck /ENCRYPT /KEY_FILE=/root/backup.key
```

The data, the names of the files and everything else in the saveset is
encrypted with a passphrase; without it nothing can be read. VBACKUP
asks for the passphrase twice on the terminal (nothing is shown while
you type), or reads it from a key file (/KEY_FILE). The passphrase is
never given on the command line: the command line is stored in the
saveset and can be seen by other users with ps.

Restore, /LIST, /COMPARE, /EXTRACT and vbkx see by themselves that a
saveset is encrypted and ask for the passphrase (or take /KEY_FILE).
/ENCRYPT is given to the save only.

A damaged encrypted saveset is repaired as any other: checking and
repairing do not need the passphrase. A block that somebody changed on
purpose (its checksum made right again) is recognized by its
authentication tag, reported (%VBACKUP-W-BLKFORGED) and repaired from
its group like a bad block.

The passphrase IS the key. Keep it somewhere safe: nobody, VBACKUP's
author included, can open a saveset whose passphrase is lost. Use a
long one - five or more random words. The journal (/RECORD) is not
encrypted: it is a file of this system.

The encryption runs on several cores: the key stream of a block in
stripes, the blocks of a group checked and decrypted side by side when
reading. VBACKUP_CTHREADS=n sets the threads, 1 - none.

How: ChaCha20 and HMAC-SHA256 per block, the keys from the passphrase
by PBKDF2-HMAC-SHA256 with 600000 iterations and a random salt per
saveset (format.md, 6.10). No library is used: vbkx and the extractors
for the geeks read it with nothing else.

Note: VBACKUP and vbkx before X01-06 cannot read an encrypted saveset;
they report all blocks lost and write nothing.

## /KEY_FILE -- the passphrase from a file

```
/KEY_FILE=file
```

The first line of the file is the passphrase (without the end of the
line). The file must be readable by its owner only - chmod 600 file -
or VBACKUP refuses it (%VBACKUP-E-KEYFILE). For a save run by cron
or a timer, and for the file managers.

Without /KEY_FILE the environment variable VBACKUP_KEY_FILE is taken.
Without both, VBACKUP asks on the terminal; when there is none (cron,
a pipe), it stops with %VBACKUP-E-NOKEY - it never waits.
VBACKUP_NOPROMPT=1 forbids the question even with a terminal (the
plugins of the file managers set it).

## /SELECT -- take only some files

```
/SELECT=(pattern[,...])
```

Takes only the files whose stored name matches a pattern. Works when
you save, restore, compare and list. In a pattern, * matches any
characters (also /), % and ? match one character:

```
$ vbackup home.bck /tmp/r '/SELECT=(*.c,*.h)'
```

A stored name is the name relative to the base of the saveset, for
example rrl/src/a.c. /LIST shows the stored names.

## /EXCLUDE -- leave files out

```
/EXCLUDE=(pattern[,...])
```

Leaves out the files whose stored name matches a pattern. A directory
that matches is left out with everything in it:

```
$ vbackup /home/rrl rrl.bck '/EXCLUDE=(*.o,*/.cache)'
```

## /SINCE -- only files changed since a time

```
/SINCE=time
/SINCE=BACKUP
```

Saves only the files not older than the time. Directories are always
saved. See the topic TIME.

/SINCE=BACKUP saves only the files that changed since they were last
saved with /RECORD - the journal knows. A file the journal does not know
is saved. Without a journal everything is saved, as the first time.

Any /SINCE or /BEFORE makes the saveset incremental: the files it does
not save are still listed in its catalog, as present. See the topic
INCREMENTAL.

## /BEFORE -- only files older than a time

```
/BEFORE=time
```

Saves only the files older than the time. See the topic TIME.

## /MODIFIED -- /SINCE and /BEFORE look at the modification time

This is the default.

## /CREATED -- /SINCE and /BEFORE look at the creation time

Linux keeps the creation time on ext4, xfs and btrfs. A file system
without it has no creation time, and its files are not selected.

## /CHANGED -- /SINCE and /BEFORE look at the change time

The change time (ctime) moves also when permissions, owner or name
change.

## /BY_OWNER -- only the files of one user

```
/BY_OWNER=user
```

Saves only the files that belong to the user, given by name or number.

## /CROSS_DEVICE -- go into other file systems

```
/CROSS_DEVICE
/NOCROSS_DEVICE   (default)
```

By default a mount point is saved as a directory, but what is mounted
on it is not. So saving / does not save /proc, /sys or a mounted USB
disk. Give /CROSS_DEVICE to save them too.

## /IGNORE -- save what is normally left out

```
/IGNORE=NOBACKUP
```

Files and directories with the nodump flag (chattr +d) are not saved.
This is the NOBACKUP flag of OpenVMS. /IGNORE=NOBACKUP saves them too.

## /XATTRS -- extended attributes and ACLs

```
/XATTRS     (default)
/NOXATTRS
```

Saves and restores the extended attributes, the POSIX ACLs, the file
capabilities and the SELinux label. Some of them can only be restored
by root.

## /VERIFY -- check the saveset after saving

After the saveset is written, VBACKUP reads it again and compares
every file with the disk. A difference is reported with
%VBACKUP-E-COMPARERR.

## /LOG -- report every file

Reports every file saved, restored or compared.

Without /LOG too, VBACKUP says when it begins (%VBACKUP-I-STARTED),
the totals, and how it ended and how long it took
(%VBACKUP-I-COMPLETED). A listing is its own answer and says neither.

## /CONFIRM -- ask before every file

Asks at the terminal before every file: answer YES, NO, QUIT or ALL.
An empty answer is NO.

## /REPLACE -- overwrite what is there

When you save: an existing saveset is overwritten.

When you restore: an existing file is removed and restored again.
Without /REPLACE it is kept, and you see %VBACKUP-W-FILEEXISTS.

When you extract: an existing output file is overwritten.

## /OWNER -- who owns the restored files

```
/OWNER=ORIGINAL
/OWNER=DEFAULT
/OWNER=user
```

ORIGINAL gives the files back to their owners: by name when the name
is known on this machine, by number otherwise. This is the default
for root.

DEFAULT leaves the files to you. This is the default for everybody
else, who cannot give files away anyway.

user gives all files to that user.

## /LIST -- list the saveset

```
/LIST[=file]
```

Shows what the saveset holds: first the summary (who wrote it, when,
with what command), then the files. With a file name the listing goes
into the file.

The list is read from the catalog at the end of the saveset, so even
a very big saveset is listed at once.

/LIST may also be given when you save: the new saveset is listed.

## /BRIEF -- a short listing

Name, size and date of every file. This is the default.

## /FULL -- a long listing

Adds the type, the owner, the permissions, the checksum and the
status of every file.

## /FORMAT -- the form of the listing

```
/FORMAT=VMS   (default)
/FORMAT=LS
```

LS prints one line per file like "ls -l", with the full stored name.
It is meant for programs to read.

## /EXTRACT -- get one file out

```
/EXTRACT=stored-name
```

Writes the contents of one file to the output-spec, or to the
standard output. The name is exact, without wildcards; /LIST shows
it. The file is found through the catalog, so this is fast even in a
big saveset.

```
$ vbackup home.bck /EXTRACT=rrl/notes.txt | less
```

## /COMPARE -- compare the saveset with the disk

Reads the saveset and compares every file with the disk: the type,
the size, the contents, the target of a symbolic link. A difference
is reported with %VBACKUP-E-COMPARERR, and the exit code is 2.

## /RECORD -- remember what has been saved

When you save: after the saveset is written (and, with /VERIFY,
checked), the journal records the saveset and, for every file saved
without trouble, its size, times and inode. The next /SINCE=BACKUP
saves only what changed since. A file that changed while it was saved,
or could not be read, is not recorded, so it is saved again next time.

With savesets and no output (vbackup a.bck,b.bck /RECORD): the journal
is rebuilt from their catalogs. Use it when the journal is lost.

## /JOURNAL -- which journal

```
/JOURNAL[=file]
```

The journal is /var/lib/vbackup/vbackup.jnl for root and
~/.vbackup/vbackup.jnl for everybody else. /JOURNAL=file uses another
one - for example one journal for each backup plan.

With /LIST and no parameter, the journal is listed: the savesets it
knows. /FULL adds the files and where their last copy is; /SELECT
picks files by their absolute names:

```
$ vbackup /JOURNAL /LIST /FULL '/SELECT=*/notes.txt'
```

## /INCREMENTAL -- restore a chain of savesets

```
$ vbackup full.bck,mon.bck,tue.bck /home /INCREMENTAL
```

Restores the full saveset, then every incremental one, oldest first,
and makes every directory look as it did at the last save: a file the
later saveset does not list any more is deleted. Files that changed are
replaced - /INCREMENTAL implies /REPLACE.

Only directories that are in the saveset are cleaned, never the output
directory itself. A saveset without a catalog, or made by VBACKUP
before X01-02, is refused (%VBACKUP-E-NOTINCR): nothing is deleted on
the word of an incomplete list. /CONFIRM asks before every deletion,
/LOG reports it. /SELECT and /EXCLUDE are not taken with /INCREMENTAL:
the cleaning would reach directories you did not mean to touch.

Note: OpenVMS BACKUP restores an incremental chain newest first;
VBACKUP takes them oldest first.

## /HELP -- show this description

```
vbackup /HELP [topic ...]
```

Shows a topic of this description, for example vbackup /HELP /VERIFY.

## Wildcards -- how a file specification is expanded

```
*      any characters in one name
% ?    one character
...    any number of directory levels, also none
```

The base of a specification is the part before the first wildcard.
The stored names are relative to it:

```
/home/rrl                base /home        names rrl, rrl/...
/home/rrl/src/.../*.c    base /home/rrl/src  names a.c, lib/b.c
/etc/*.conf              base /etc         names host.conf, ...
```

A directory that matches is taken with everything in it. Hidden
files (names that begin with a dot) are matched too.

## Incremental -- save only what changed

Make a full saveset once, with /RECORD, then incremental ones:

```
$ vbackup /home full.bck /RECORD
$ vbackup /home mon.bck /SINCE=BACKUP /RECORD
$ vbackup /home tue.bck /SINCE=BACKUP /RECORD
```

Each incremental saveset holds only the files changed since the last
/RECORD, but its catalog lists every file there was - so a restore
knows what was deleted. Restore them all, in order:

```
$ vbackup full.bck,mon.bck,tue.bck /restore /INCREMENTAL
```

What decides what is covered and what is saved: /SELECT, /EXCLUDE, the
nodump flag, /NOCROSS_DEVICE and /BY_OWNER say which files the saveset
is about; /SINCE, /BEFORE and /SINCE=BACKUP say which of them are saved.

## Scheduling -- regular saves and rotation, by BATCH

VBACKUP does not schedule itself nor delete old savesets: that is the
business of a batch queue, and BATCH (the batch job subsystem, `batch
submit`) does it the way OpenVMS does - a job that runs the save and
submits itself again for the next day.

```sh
#!/bin/sh
# /opt/jobs/vbackup-daily.sh - a full save on Sunday, an incremental on the other days
T=/backup/home-$(date +%Y%m%d)
if [ "$(date +%u)" = 7 ]; then
	vbackup /home $T.bck /RECORD /VERIFY
else
	vbackup /home $T.bck /SINCE=BACKUP /RECORD /VERIFY
fi
# Rotation: five weeks of savesets are kept
find /backup -name 'home-*.bck*' -mtime +35 -delete
# Again tomorrow at 02:00 - the job submits itself
batch submit "$0" /NAME=vbackup-daily "/AFTER=$(date -d tomorrow +%d-%b-%Y:02:00 | tr a-z A-Z)"
```

```
$ batch submit /opt/jobs/vbackup-daily.sh /NAME=vbackup-daily /AFTER=TOMORROW
```

A job that fails stays in the queue with its log (`batch show`), the
messages of VBACKUP and its completion code in it; nothing is lost
silently, as with cron.

## OpenVMS -- savesets written by OpenVMS BACKUP

A saveset that OpenVMS BACKUP wrote is read too: copy it here in binary
mode (FTP "binary", or a raw copy of the tape file) and give it as the
input. VBACKUP knows it by its first block and says VMSSAVESET.

```
$ vbackup USERS.BCK /LIST                what BACKUP/LIST shows
$ vbackup USERS.BCK /LIST /FULL          what BACKUP/LIST/FULL shows
$ vbackup USERS.BCK /restore/vms         the files under /restore/vms
$ vbackup USERS.BCK /restore/vms /SELECT=*.COM
$ vbackup USERS.BCK "/EXTRACT=[SMITH]LOGIN.COM;3" login.com
$ vbkx x USERS.BCK -C /restore/vms       the same with vbkx (vbkx.exe too)
```

The listing is BACKUP's own, line for line. On restore the names
become Linux names: [SMITH.WORK]NOTES.TXT;5 is SMITH/WORK/NOTES.TXT
under the output directory; the highest version gets the plain name,
older ones keep ";n" (NOTES.TXT;4). The escapes of ODS-5 are undone
(^_ a blank, ^. a dot). /SELECT, /EXCLUDE and /EXTRACT take the Linux
names; /EXTRACT also the name with ";n", or the name of VMS as the
listing shows it.

Text files - variable, VFC, fixed records with a carriage control, the
stream formats - become texts with LF, as FTP of VMS makes them.
Executables, object files and other files without a carriage control
are copied as they are on the VMS disk. Indexed and relative files are
copied as the image of the RMS file (VMSRAW): their records need RMS.
The mode comes from the protection (R - r, W - w; E only on
directories), the time from the revision date; the owner is the user
who restores.

Bad blocks are rebuilt from the XOR blocks of /GROUP_SIZE as BACKUP
would; a file that lost data is said FILDAMAGED. Not read: savesets
BACKUP encrypted (/ENCRYPT), and the LBN data of /IMAGE and /PHYSICAL
savesets. /COMPARE, /INCREMENTAL, /ORIGINAL, /IMAGE and /TRANSFER are
not for such a saveset. Midnight Commander opens it like one of
VBACKUP's (through vbackup); MultiArc of far2l (it knows a saveset by
"VBKB" at its start, which BACKUP does not write), the WCX plugin of
Total and Double Commander and vbkx-go, vbkx-rs, vbkx-pl do not. doc/vmsbackup.md tells the format.

## Windows -- vbackup.exe, the utility on Windows

vbackup.exe is VBACKUP built for Windows (MinGW-w64): the same command,
the same savesets. A saveset made on Linux is listed, restored and
compared there; one made there is read on Linux and by vbkx. It is
built from this tree on Linux, when MinGW-w64 and the sources of StarLet
are at hand:

```
$ cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
        -DVBACKUP_STARLET_SRC=/root/Works/starlet-1.6.8
$ cmake --build build-win                vbackup.exe, vbkx.exe, units.exe
```

```
C:\> vbackup C:\Users\ivan\Documents D:\docs.bck /LOG
C:\> vbackup D:\docs.bck /LIST
C:\> vbackup D:\docs.bck C:\restore
C:\> vbackup C:\data D:\data.bck /ENCRYPT /KEY_FILE=C:\Users\ivan\backup.key
```

Names are Unicode (UTF-8 in the saveset), paths of any length; "\" and
"/" both separate - a "\" anywhere on the command line is a separator,
in /SELECT and /COMMENT too. What is saved: data, times (modification,
access, change and creation), the attributes (read-only, hidden, system,
archive, temporary, not indexed), the security descriptor (owner, group,
DACL; the SACL too for a backup operator), the alternate data streams
(as the extended attributes "user.<stream>", up to 64 KB each),
directories, hard links, symbolic links and junctions (as links), sparse
files with their holes. /LIST /FULL shows the attributes ("Windows:
HIDDEN ARCHIVE") and the owner as DOMAIN\user.

Run vbackup.exe as an administrator ("Run as administrator") to save
every file and put everything back: it then holds the privileges of a
backup operator - it reads files it has no right to, and /OWNER=ORIGINAL
is its default, so the owners and the ACL come back as they were. A
user who is no administrator restores the files as theirs: they take
the ACL of the directory they go into (give /OWNER=ORIGINAL to have the
saved one); /OWNER=user and /BY_OWNER are refused there. On Linux a
saveset of Windows restores as any other; the
streams become "user." attributes, the attributes and the ACL are left.

On restore a name Windows cannot hold - a ":" in it, "<>"|?*\", a
trailing dot or blank, CON, NUL, COM1 and the like - is not made, and
said (OPENOUT ... not a valid name on Windows). Two names that differ
in case only are one file on Windows: the second is not restored
(FILEEXISTS; with /REPLACE too: OPENOUT ... differs in case only). A
FIFO or a device of a Linux saveset is said (UNSUPP). Of the extended
attributes of a Linux saveset only "user." ones become streams; the
others are skipped. A symbolic link needs the right to make links (an
administrator, or the developer mode of Windows 10/11).

A saveset on another node (node::file) goes through ssh.exe, the OpenSSH
client of Windows 10/11 (VBACKUP_RSH names another one). Not on
Windows: /PHYSICAL and /IMAGE - refused at once; the read-ahead of files;
the shadow copies (VSS) - a file in use by another program is saved as
it can be read, or reported. The mode of the key file is not checked:
keep it in your profile, where only you can read it. The journal is
%USERPROFILE%\.vbackup\vbackup.jnl.

## Time -- how a time value is written

```
dd-MMM-yyyy[ hh:mm[:ss]]   3-OCT-2026 14:00
hh:mm[:ss]                 today at that time
-[dd ]hh:mm[:ss]           so much time ago: "-1 0:0" is one day ago
TODAY  YESTERDAY  TOMORROW  NOW
BACKUP                     /SINCE only: since the last /RECORD of each file
```

## Plugins -- savesets in file managers

Savesets can be browsed like folders in file managers. All plugins are
read only: you can look inside and copy files out; a saveset is never
changed. The list comes from the catalog, so even a very large saveset
opens at once.

Midnight Commander: press Enter on a .bck or .sav file - or on a saveset of any name: the installation adds the magic of VBACKUP to /etc/magic, and MC knows it by its contents. The installation puts
the script uvbk into the extfs of MC and a [vbackup] section into
mc.ext.ini; by hand: copy share/vbackup/plugins/mc/uvbk into
~/.local/share/mc/extfs.d and the section of mc.ext.ini.vbackup into
your mc.ext.ini, before [Default]. A saveset of several volumes opens
from a local disk only.

far2l (Linux) and Far Manager 3 (Windows): MultiArc, with the format in
share/vbackup/plugins/far/vbackup.ini (the installation adds it for
far2l). It runs vbkx; on Windows put vbkx.exe on the PATH. Press Enter
or Ctrl+PgDn on a saveset (known by its signature, whatever its name); F5 copies files out.

Total Commander (Windows) and Double Commander (Linux): the packer
plugin vbackup.wcx64 (64-bit TC) / vbackup.wcx (32-bit TC; on Linux, for DC) - install it in the
plugin settings and associate it with the extension bck.

An encrypted saveset: the file managers cannot ask for a passphrase.
Put it into a key file (chmod 600) and name the file in the environment
variable VBACKUP_KEY_FILE before you start the file manager (on
Windows: in the system settings of environment variables). Without it
the saveset does not open - nothing waits for an answer on the screen.

## Vbkx -- the stand-alone extractor

vbkx reads savesets on a machine where VBACKUP is not installed. It
is one statically linked program: copy it there and run it.

```
vbkx l saveset                          list the files
vbkx x saveset [-C dir] [-f] [name...]  extract all, or the names given
vbkx p saveset name                     write one file to the output
vbkx t saveset                          read it all, check the checksums
  ... -k keyfile                        an encrypted saveset: the passphrase
  ... -n                                never ask for it (for programs)
```

A name is a stored name as the listing shows it; a directory name
takes what is below it. -C names the output directory (it is made if
missing); -f overwrites files that are there.

vbkx puts back the data, holes, mode, times, symbolic and hard links
and FIFOs; as root also the owner (by number) and device files. It
does not put back ACLs, extended attributes or chattr flags: restore
with VBACKUP when you need them.

Damaged savesets are repaired as far as the XOR blocks allow. vbkx
names every file that is incomplete ("is incomplete") and every file it
could not reach ("not extracted"). Completion code: 0 -- done;
1 -- something was damaged or not done; 2 -- the command or the
saveset cannot be used.

A saveset written by OpenVMS BACKUP is taken the same way, by the
Linux names of its files (see the topic OPENVMS).

An encrypted saveset: vbkx takes the passphrase from -k keyfile, else
from VBACKUP_KEY_FILE, else asks on the terminal (the console on
Windows).

There is vbkx.exe for Windows too, with the same commands. It puts back
the data, times, read-only files, directories and hard links; symbolic
links only where Windows allows them. A name Windows cannot hold (with
: ? * and the like, or CON, NUL ...) is not extracted, and said.

## Examples

Save your home directory, check it, list it:

```
$ vbackup /home/rrl /backup/rrl.bck /VERIFY
$ vbackup /backup/rrl.bck /LIST
```

Restore it somewhere else:

```
$ vbackup /backup/rrl.bck /tmp/restore
```

Save only what changed today, in volumes of 4 GB:

```
$ vbackup /home /mnt/usb/home.bck /SINCE=TODAY /VOLUME_SIZE=4G
```

Restore only the C files and overwrite what is there:

```
$ vbackup /backup/rrl.bck /home '/SELECT=*.c' /REPLACE
```

Copy a tree to another disk, and check the copy:

```
$ vbackup /home/rrl /mnt/disk2 /VERIFY
```

Send a saveset to another machine:

```
$ vbackup /etc - | ssh backup-host 'cat > etc.bck'
```

## Troubleshooting -- what to do when

**The saveset is not created: %VBACKUP-E-OPENOUT ... errno: 17 (File
exists).** A saveset of that name is there. Give another name, or
/REPLACE to overwrite it.

**%VBACKUP-E-IVOP, cannot tell what to do.** The input is not a
saveset, and the output does not look like one. When you save, give
/SAVE_SET or end the name with .bck or .sav.

**%VBACKUP-E-MAXPARM, too many parameters: .log.** A qualifier was
typed without its slash. Write /LOG, not .log.

**On Windows: %VBACKUP-E-OPENOUT ... not a valid name on Windows.**
The saveset holds a name Windows cannot have: a ":" or "?" in it, a dot
or blank at its end, CON, NUL, COM1... The file is not restored; the
others are. Restore it on Linux, or extract it under another name:
/EXTRACT="a:b" ab.txt.

**On Windows: %VBACKUP-E-QUALUSE, Qualifier: /PHYSICAL - not on
Windows.** /PHYSICAL and /IMAGE work on Linux only. Save the files
instead, or do it on Linux.

**%VBACKUP-W-ZCHECK.** A piece of data VBACKUP compressed did not come
back the same when it was checked. It was stored uncompressed: the
saveset is right. It is a fault of VBACKUP: please report it with the
file and the /LEVEL.

**%VBACKUP-W-XATTRSKIP.** An extended attribute of the file was not
saved: it cannot be read, or it is longer than 64 KB. On Windows it is
an alternate data stream longer than 64 KB. The file itself was saved.
Copy the stream into a file of its own if it matters, or give /NOXATTRS.

**On Windows: %VBACKUP-W-ATTRERR ... security not restored.** The owner
of the file could not be set: only an administrator may give a file to
another account. Run vbackup.exe as an administrator, or restore without
/OWNER=ORIGINAL - the files are then yours.

**On Windows: a symbolic link is not restored (OPENOUT ... errno: 1).**
Making links needs a right: run as an administrator, or turn on the
developer mode of Windows (Settings, For developers).

**%VBACKUP-E-WRONGKEY.** The passphrase does not open the saveset.
Check the key file: only its first line counts, and capitals matter.
Nothing was written. A saveset whose passphrase is lost cannot be
opened by anybody.

**%VBACKUP-E-NOKEY.** The saveset is encrypted and there is no terminal
to ask on (cron, a pipe, a file manager). Give /KEY_FILE=file or set
VBACKUP_KEY_FILE.

**%VBACKUP-E-KEYFILE ... chmod 600 it.** Others may read the key file.
Make it yours only: chmod 600 file.

**%VBACKUP-W-BLKFORGED.** A block of an encrypted saveset is not what
was written, though its checksum is right: somebody changed it on
purpose, or the medium does very odd things. When BLKFIXED follows, it
was repaired and the files are right; find out who could write to the
saveset.

**Opening an encrypted saveset takes a few seconds.** That is the
deliberate cost of PBKDF2 (600000 iterations) - every guess of an
attacker pays it too. About 0.5 s on a PC, 1.6 s on a small ARM board
with SHA instructions, 6 s on one without. VBACKUP_KDFITER is for the
tests; do not lower it for real savesets.

**%VBACKUP-I-BLKFIXED when you restore.** A block of the saveset was
bad and was repaired. All files are fine. The medium may be failing:
copy the saveset to another one.

**%VBACKUP-E-BLKLOST and %VBACKUP-E-FILDAMAGED.** Blocks were lost and
could not be repaired. The files named by FILDAMAGED are incomplete;
the files named by FILLOST were not restored at all; all other files
are fine. Next time give /PARITY=2 or more (several bad blocks of a
group repaired), a smaller /GROUP_SIZE, or keep two copies of important
savesets.

**%VBACKUP-W-PARITYERR.** A saveset made with /PARITY: blocks of a group
were bad, and its parity blocks did not agree with what was rebuilt -
some block of the group with a right checksum holds other bytes (written
wrong, or changed on purpose). Nothing of that group is restored from
the parity; its files are named by FILDAMAGED. Keep the saveset: the
other groups are fine.

**%VBACKUP-E-NOTSAVESET for a saveset made with /PARITY.** It is of
format version 2: VBACKUP or vbkx before X01-14 cannot read it. Use
X01-14 or later.

**%VBACKUP-W-UNNAMED.** Blocks were lost, and the saveset has no
catalog, or its catalog was damaged too. Some files may be missing
from the output, and VBACKUP cannot name them all. Compare the output
with the source, or with /LIST of an older saveset.

**%VBACKUP-E-MISSVOL.** A volume is missing. Put all volumes into one
directory, with their names unchanged.

**%VBACKUP-W-NOTRAILER.** The save did not finish (the disk was full,
the program was stopped), or the last volume is missing. The files up
to the break can be restored. /LIST is slower: it reads the whole
saveset.

**%VBACKUP-W-FILCHANGED when you save.** The file was written to
while it was saved; the copy may be a mix of old and new. Save it
again when nobody writes to it, or save from a snapshot.

**%VBACKUP-W-ATTRERR when you restore.** An owner, permission, time,
attribute or flag could not be set. Most often you are not root:
restore as root, or give /OWNER=DEFAULT.

**%VBACKUP-E-OPENOUT ... a name that leads out of the output
directory.** The saveset holds a name with ".." or a leading "/", or
a name that goes through a symbolic link. Such a file is never
written. The saveset was not made by VBACKUP, or was made to do harm.

**/SINCE=BACKUP saves everything.** The journal does not know the files:
the saves were made without /RECORD, with another /JOURNAL, or as
another user (each user has a journal of their own). See vbackup
/JOURNAL /LIST.

**%VBACKUP-E-NOTINCR.** /INCREMENTAL needs a saveset with a catalog,
made by VBACKUP X01-02 or later. Restore that one without /INCREMENTAL.

**%VBACKUP-W-MISSING.** A file should be there from an earlier saveset
of the chain, and it is not. Most often a saveset of the chain was left
out: give them all, oldest first.

**%VBACKUP-E-JNLERR.** The journal cannot be read or written. Look at
the reason in the message. A damaged journal can be rebuilt: delete it
and give the savesets with /RECORD.

**Nothing is saved from a directory.** Look for the nodump flag:
lsattr -d dir. Give /IGNORE=NOBACKUP to save it anyway.

**A save is slow, or you want to know if the writer thread is to
blame.** The saveset is written by a thread of its own while the files
are read. Set VBACKUP_PIPELINE=0 in the environment to write it without
that thread: VBACKUP_PIPELINE=0 vbackup /home h.bck. Only the thread
is off; the hints to the page cache stay. The saveset is the same.
If the problem goes away, report it.

Eight more threads read the next files ahead while a file is saved or
copied, so that the save finds them in the cache. VBACKUP_PREFETCH=n
sets their number; 0 turns the read-ahead off. More threads may help on
NFS or a slow network disk; fewer on a single slow hard disk.

**A saveset from OpenVMS is "not a saveset", or a copy is made of it.**
It was copied in text mode, or with another record format: FTP must be
in binary mode ("binary", "type image"). A saveset taken off a tape must
keep its block size. The size of a good copy is a multiple of the block
size BACKUP/LIST shows.

**A text file from OpenVMS has a byte count before each line.** It has
no carriage control on VMS (SET FILE/ATTRIBUTE=RAT:CR it there, or
CONVERT it), so VBACKUP copied it as it is, the record counts with it.

**%VBACKUP-I-VMSRAW for an indexed file.** Its records need RMS. Restore
it, copy it back to VMS in binary mode and SET FILE/ATTRIBUTE it, or
CONVERT it to a sequential file on VMS before the save.

**Files are not in the page cache after a save.** This is on purpose.
VBACKUP drops the pages of the saveset it writes or reads, and the
pages of a file that was not in the cache before it was read. Files
that were in the cache stay there.

## Messages -- the conditions the utility signals

```
%VBACKUP-I-CREATED      a saveset volume has been created
%VBACKUP-I-SAVED        /LOG: a file has been saved
%VBACKUP-I-RESTORED     /LOG: a file has been restored
%VBACKUP-I-COMPARED     /LOG: a file has been compared
%VBACKUP-I-SKIPPED      a file has been left out, and why
%VBACKUP-W-FILEEXISTS   a file is there and was not restored: /REPLACE
%VBACKUP-W-FILCHANGED   a file changed while it was saved
%VBACKUP-E-OPENIN       a file cannot be opened
%VBACKUP-E-OPENOUT      a file cannot be created
%VBACKUP-E-READERR      a file cannot be read
%VBACKUP-E-WRITERR      a file cannot be written
%VBACKUP-E-OPENDIR      a directory cannot be read
%VBACKUP-E-NOTSAVESET   the file is not a saveset
%VBACKUP-W-WRONGVOL     a volume belongs to another saveset, ignored
%VBACKUP-E-MISSVOL      a volume is missing
%VBACKUP-W-NOTRAILER    the save did not finish, or the last volume is missing
%VBACKUP-I-BLKFIXED     a bad block has been repaired
%VBACKUP-E-BLKLOST      a bad block cannot be repaired
%VBACKUP-W-BADREC       a damaged record has been skipped
%VBACKUP-E-FILDAMAGED   a file is incomplete: data was lost
%VBACKUP-E-FILLOST      a file was not restored: its records were lost
%VBACKUP-W-UNNAMED      files were lost and cannot all be named
%VBACKUP-E-PHYSMOUNTED  /PHYSICAL: the device is mounted
%VBACKUP-E-PHYSHELD     /PHYSICAL: the device is in use (LVM, RAID, swap)
%VBACKUP-E-PHYSNOTDEV   /PHYSICAL: neither a block device nor a file
%VBACKUP-E-PHYSNOTPHYS  the saveset was not made with /PHYSICAL
%VBACKUP-E-PHYSSMALL    the output device is smaller than the one saved
%VBACKUP-I-PHYSLARGER   the output device is larger: the rest stays
%VBACKUP-E-PHYSREPLACE  a device is overwritten with /REPLACE only
%VBACKUP-E-PHYSABORT    the answer was not YES: nothing written
%VBACKUP-I-PHYSUUID     the copy has the UUIDs of the original
%VBACKUP-E-PHYSSIZE     the device changed its size while it was read
%VBACKUP-I-PHYSSUMM     /PHYSICAL: the totals
%VBACKUP-E-IMGNOTVOL    /IMAGE: neither a mount point nor a device
%VBACKUP-E-IMGNOTMNT    /IMAGE: the device is not mounted
%VBACKUP-E-IMGNOTIMG    the saveset was not made with /IMAGE
%VBACKUP-E-IMGUNSUPP    no mkfs for this file system type: /PHYSICAL
%VBACKUP-E-IMGMKFS      mkfs failed, or is not installed
%VBACKUP-E-IMGMOUNT     the new file system cannot be mounted
%VBACKUP-E-IMGSMALL     the device cannot hold the files
%VBACKUP-W-IMGNOID      the label or UUID is not known
%VBACKUP-I-IMGCMD       /LOG: the mkfs command
%VBACKUP-I-IMGSUMM      /IMAGE: the totals
%VBACKUP-E-ORIGNOBASE   /ORIGINAL: the saveset does not say where from
%VBACKUP-I-ORIGTARGET   /ORIGINAL: where the files go back to
%VBACKUP-I-SRCDELETED   /DELETE /LOG: a file saved, verified, deleted
%VBACKUP-W-SRCKEPT      /DELETE: a file kept, and why
%VBACKUP-I-DELSUMM      /DELETE: the totals
%VBACKUP-E-QUALUSE      a qualifier where it cannot be used, and why
%VBACKUP-E-CRCERR       the data restored differ from the data saved
%VBACKUP-E-COMPARERR    a difference between the saveset and the disk
%VBACKUP-W-ATTRERR      an attribute could not be restored
%VBACKUP-W-UNSUPP       a file of this type could not be restored
%VBACKUP-E-NOTFOUND     /EXTRACT: no such file in the saveset
%VBACKUP-W-NOFILES      a specification selected no file
%VBACKUP-W-TOODEEP      directories are nested too deep
%VBACKUP-F-NOMEM        memory cannot be allocated
%VBACKUP-E-IVQUAL       the value of a qualifier is illegal
%VBACKUP-E-IVTIME       a time value is illformed
%VBACKUP-E-CONFQUAL     two qualifiers exclude each other
%VBACKUP-E-NOPARAM      a parameter is missing
%VBACKUP-E-IVOP         it cannot be told what to do
%VBACKUP-E-NOTOPIC      /HELP names a topic the library has not
%VBACKUP-I-SAVESUMM     /LOG: the totals of a save
%VBACKUP-I-RESTSUMM     /LOG: the totals of a restore
%VBACKUP-I-CMPSUMM      the totals of a compare
%VBACKUP-F-FATALSAVE    the saveset could not be finished
%VBACKUP-I-VERIFYING    /VERIFY begins
%VBACKUP-W-NOCATALOG    the saveset is listed by reading it whole
%VBACKUP-W-TOOMANY      a list is longer than the utility takes
%VBACKUP-I-RECORDED     the journal has been updated
%VBACKUP-E-JNLERR       the journal cannot be read or written
%VBACKUP-I-NOJOURNAL    no journal yet: /SINCE=BACKUP saves everything
%VBACKUP-I-DELETED      /INCREMENTAL deleted a file
%VBACKUP-W-MISSING      a file of an earlier saveset is not there
%VBACKUP-E-NOTINCR      /INCREMENTAL refused a saveset
%VBACKUP-I-COPIED       /LOG: a file has been copied
%VBACKUP-I-CPYSUMM      /LOG: the totals of a copy
%VBACKUP-I-INCRSUMM     /LOG: unchanged files listed as present
%VBACKUP-W-NOINODE      a catalog too old to rebuild the journal from
%VBACKUP-E-MAXPARM      more than an input and an output: a qualifier without its /
%VBACKUP-I-GLUED        qualifiers glued to a parameter, taken as qualifiers
%VBACKUP-I-ENCRYPTED    /LOG: the saveset is made encrypted
%VBACKUP-E-NOKEY        encrypted, and no passphrase to be had: /KEY_FILE
%VBACKUP-E-WRONGKEY     the passphrase does not open the saveset
%VBACKUP-E-KEYFILE      the key file cannot be used (chmod 600, first line)
%VBACKUP-E-KEYMATCH     the two passphrases of a save differ: nothing saved
%VBACKUP-W-BLKFORGED    a block changed on purpose: its checksum right, its tag not
%VBACKUP-W-BLKCOPIED    /TRANSFER: a bad block copied as it is
%VBACKUP-I-XFRSUMM      /TRANSFER: the totals
%VBACKUP-E-REMOTE       node::file: the pipe to VBACKUP there cannot be made
%VBACKUP-E-REMOTEERR    node::file: VBACKUP there did not complete
%VBACKUP-I-STARTED      the work begins: what, from where, to where
%VBACKUP-I-COMPLETED    the work is done: completed, with warnings or with errors; how long
%VBACKUP-I-VMSSAVESET   the input is a saveset of OpenVMS BACKUP: its block and group size
%VBACKUP-I-VMSNOCRC     ... written /NOCRC: damage in its blocks cannot be seen
%VBACKUP-I-VMSRAW       ... an indexed or relative file restored as its RMS image
%VBACKUP-W-PARITYERR    /PARITY: the parity of a group disagrees, nothing of it rebuilt
%VBACKUP-W-XATTRSKIP    an extended attribute (on Windows a stream) not saved: unreadable, or over 64 KB
%VBACKUP-W-ZCHECK       a compressed piece did not come back the same: stored plain (report it)
```

A message goes to the standard error. It begins with the date, the
time and the process number:

```
03-10-2026 15:22:19.109 3715297 %VBACKUP-I-RESTORED, out/tree restored
```

## Author

StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan).
