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
saveset (no .bck, no /SAVE_SET) means a copy, as in BACKUP. The files
keep everything a restore would give them back.

## /SAVE_SET -- the output is a saveset

Says that the output-spec is a saveset, whatever its name. You do not
need it when the name ends in .bck.

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

Reports every file saved, restored or compared, and the totals at the
end.

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

## Time -- how a time value is written

```
dd-MMM-yyyy[ hh:mm[:ss]]   3-OCT-2026 14:00
hh:mm[:ss]                 today at that time
-[dd ]hh:mm[:ss]           so much time ago: "-1 0:0" is one day ago
TODAY  YESTERDAY  TOMORROW  NOW
BACKUP                     /SINCE only: since the last /RECORD of each file
```

## Vbkx -- the stand-alone extractor

vbkx reads savesets on a machine where VBACKUP is not installed. It
is one statically linked program: copy it there and run it.

```
vbkx l saveset                          list the files
vbkx x saveset [-C dir] [-f] [name...]  extract all, or the names given
vbkx p saveset name                     write one file to the output
vbkx t saveset                          read it all, check the checksums
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
could not reach ("was not extracted"). Completion code: 0 -- done;
1 -- something was damaged or not done; 2 -- the command or the
saveset cannot be used.

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

**The saveset is not created: %VBACKUP-E-OPENOUT ... errno=17 (File
exists).** A saveset of that name is there. Give another name, or
/REPLACE to overwrite it.

**%VBACKUP-E-IVOP, cannot tell what to do.** The input is not a
saveset, and the output does not look like one. When you save, give
/SAVE_SET or end the name with .bck.

**%VBACKUP-I-BLKFIXED when you restore.** A block of the saveset was
bad and was repaired. All files are fine. The medium may be failing:
copy the saveset to another one.

**%VBACKUP-E-BLKLOST and %VBACKUP-E-FILDAMAGED.** Blocks were lost and
could not be repaired. The files named by FILDAMAGED are incomplete;
the files named by FILLOST were not restored at all; all other files
are fine. Next time give a smaller /GROUP_SIZE (more XOR blocks), or
keep two copies of important savesets.

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
```

A message goes to the standard error. It begins with the date, the
time and the process number:

```
03-10-2026 15:22:19.109 3715297 %VBACKUP-I-RESTORED, out/tree restored
```

## Author

StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan).
