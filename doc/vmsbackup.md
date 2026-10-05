# OpenVMS BACKUP savesets, as VBACKUP reads them

VBACKUP X01-13 reads the savesets of OpenVMS BACKUP: it lists them as
`BACKUP/LIST` does, restores their files into a Linux directory and
extracts one file; `vbkx` (and `vbkx.exe`) does the same.  This document
says what is read and how, and what each file becomes.  The code is
`lib/vbkvms.c` (the reading, portable, no stdio) and `src/vbkvms.c` (the
utility side).

Everything below was checked against savesets written by BACKUP V8.3 on
OpenVMS Alpha V8.3, an ODS-5 disk - the ones in `test/vms`, made by
`test/vms/MKVBKS.COM`: the listing VBACKUP prints equals the one of
`BACKUP/LIST[/FULL]` octet for octet, and every file it restores equals
what FTP of VMS gives for it in ASCII mode (a text) or in binary mode
(the rest).  What a field means where these savesets did not show it is
said as such.

Not read: savesets encrypted by BACKUP (`/ENCRYPT`, DES) and
`/IMAGE` or `/PHYSICAL` savesets as volumes - the files of an
`/IMAGE` saveset are read like the files of any other, their LBN data is
not.  Tape savesets with their labels are not read as tapes; a saveset
copied off a tape into a file (`dd`) is a disk saveset as far as this
goes, provided the copy keeps the block size.

## 1. Conventions

All integers are little-endian.  A *block* is one block of the saveset
(`/BLOCK_SIZE`, 2048 to 65535 octets; 32256 by default); a *VBN* is a
block of 512 octets of a file on the VMS disk, numbered from 1.  VMS
time is a u64 of 100 ns units since 17-NOV-1858 00:00, in the time of
the system that wrote it - VMS keeps no time zone.

## 2. Blocks

A saveset is a row of blocks of one size, nothing before the first and
nothing after the last.  Each begins with a block header (BBH) of 256
octets:

| Offset | Size | Field | Use |
|---:|---:|---|---|
| 0 | u16 | size | 256: the size of this header |
| 2 | u16 | opsys | 0x0800 OpenVMS Alpha (0x0400 VAX, 0x1000 I64 - not seen) |
| 4 | u16 | subsys | 0 |
| 6 | u16 | applic | 1 a data block, 2 an XOR block |
| 8 | u32 | number | the block's number in the saveset, from 1; XOR blocks are counted |
| 12 | 20 | - | 0 |
| 32 | u16 | struclev | 0x0101: structure level 1, version 1 |
| 34 | u16 | volnum | 1 |
| 36 | u32 | crc | the CRC of the block (2.1), 0 under `/NOCRC` |
| 40 | u32 | blocksize | the block size |
| 44 | u32 | flags | bit 0: `/NOCRC` - no CRC |
| 48 | 32 | ssname | the saveset's name, counted (u8 length, octets) |
| 80 | 6 | fid | the file in progress at the start of the block (not used) |
| 86 | 6 | did | ... its directory (not used) |
| 92 | 128 | filename | ... its name, counted (not used) |
| 220 | 34 | - | the record attributes of that file, and 0 (not used) |
| 254 | u16 | checksum | a checksum of the header: its algorithm is not known here, and nothing depends on it |

The first block of a saveset is known by its header: size 256,
structure level 0x0101, applic 1, number 1, a block size from 2048 to
65535.  There is no magic string.  VBACKUP looks for a saveset of BACKUP
before it looks for one of its own; one of its own begins with "VBKB",
which a BBH never does (its first u16 is 256).

### 2.1 CRC

CRC-32/IEEE - the CRC of VBACKUP's own blocks (format.md, section 1),
AUTODIN-II in the words of VMS - over the whole block, the crc field
(36..39) and the checksum field (254..255) taken as zeroes.  A block of
a `/NOCRC` saveset (flag bit 0) is not checked: VBACKUP says once
(VMSNOCRC) that damage in it cannot be seen.

### 2.2 Groups and XOR blocks

With `/GROUP_SIZE=n` (10 by default, 0 - none) every n data blocks are
followed by one XOR block (applic 2), the XOR of all the n, headers
included; the last group of a saveset may be short, and still ends with
its XOR block.  The group size is in the SUMMARY (XORSIZE, section 3).
Since the header of an XOR block is the XOR of the headers of its
group, its structure level and block size are those only when the group
has an odd number of blocks; an XOR block is judged by its size field,
its applic and its CRC alone.

Repair: when exactly one data block of a group is bad and its XOR
block is good, the bad one is the XOR of all the other blocks of the
group, the XOR block included; its applic and number are set again
(1, its position).  VBACKUP says BLKFIXED.  Two or more bad blocks in
one group are lost: BLKLOST for each, and the files with data in them
are said FILDAMAGED.  A bad XOR block costs nothing.

When the first block is bad, the SUMMARY with the group size is in it:
the group size is then taken from the first good XOR block ahead (its
number is n + 1), and the first block is rebuilt like any other.

## 3. Records

After the header a block holds records, each with a record header (BRH)
of 16 octets:

| Offset | Size | Field | Use |
|---:|---:|---|---|
| 0 | u16 | rsize | the size of the record after this header |
| 2 | u16 | rtype | 0 NULL, 1 SUMMARY, 2 VOLUME, 3 FILE, 4 VBN, 5 PHYSVOL, 6 LBN, 7 FID |
| 4 | u32 | flags | not used |
| 8 | u32 | address | VBN: the first VBN of the file it carries |
| 12 | u32 | - | 0 |

A record never crosses a block.  A NULL record fills the rest of a
block.  A record that would leave its block is bad: the rest of the
block is given up (BADREC).

The SUMMARY and the FILE record hold, after a u16 structure level
(0x0101), attributes: u16 length, u16 type, the value; a length and a
type of 0 end them.

SUMMARY attributes (what BACKUP/LIST shows at the head):

| Type | Value |
|---:|---|
| 1 | the saveset's name |
| 2 | the command |
| 4 | the user, 12 octets with blanks ("Written by") |
| 5 | the UIC, u32: member in the low word, group in the high |
| 6 | the date, VMS time |
| 7 | opsys, u16 (as in the BBH) |
| 8 | the system version, "V8.3" |
| 10 | the CPU ID register, u32 |
| 11 | the device it was written on, "_ALPHA1$DQB0:" |
| 12 | the BACKUP version |
| 13 | the block size, u32 |
| 14 | the group size (XORSIZE), u16 |
| 15 | the buffer count, u16 |

FILE attributes - those VBACKUP uses:

| Type | Value |
|---:|---|
| 42 | the file specification: `[DIR.SUB]NAME.TYPE;VERSION`, with the escapes of ODS-5 |
| 44 | the file ID: three u16 - number, sequence, RVN in the low octet and the high octet of the number in the high one |
| 45 | the ID of its directory, the same way |
| 47 | the owner's UIC, as in the SUMMARY |
| 48 | the protection, u16: four bits a class from the low end - SYSTEM, OWNER, GROUP, WORLD - R 1, W 2, E 4, D 8; a bit set denies |
| 51 | the file characteristics, u32: 0x20 contiguous-best-try, 0x40 locked, 0x80 contiguous, 0x2000 a directory |
| 52 | the record attributes, the FAT of 32 octets (below) |
| 53 | the revision count, u16 |
| 54, 55, 56, 57 | the creation, revision, expiration and backup dates, VMS time; 0 - none |
| 93, 94 | the access and attribute revision dates (ODS-5) |

Seen and not used: 43 (the structure level of the file, 0x0501), 46 (its
size), 49, 50, 71, 72, 74, 75, 79, 80, 87, 95, 96.

The FAT (attribute 52):

| Offset | Size | Field |
|---:|---:|---|
| 0 | u8 | the record format in the low nibble (0 undefined, 1 fixed, 2 variable, 3 VFC, 4 stream, 5 stream_LF, 6 stream_CR); the organization in the high one (0 sequential, 1 relative, 2 indexed) |
| 1 | u8 | the record attributes: 1 Fortran carriage control, 2 carriage return, 4 print file, 8 records do not cross a block |
| 2 | u16 | the record size (fixed), the longest record (variable) |
| 4 | u32 | the highest VBN allocated - two u16, the high one first |
| 8 | u32 | the end of file VBN - two u16, the high one first |
| 12 | u16 | the first free octet in the end of file VBN |
| 14 | u8 | the bucket size |
| 15 | u8 | the size of the fixed control area of VFC |
| 16 | u16 | the maximum record size |
| 18 | u16 | the default extend quantity |
| 20 | u16 | the global buffer count |

The file has (efblk - 1) * 512 + ffbyte octets; the listing's "used"
blocks are efblk, or efblk - 1 when ffbyte is 0.

A FILE record is followed by the VBN records of the file, in order: the
address of each is the VBN its data begins at, its size a multiple of
512.  Its octets up to the end of file are the file as it is on the VMS
disk.  A VBN record whose address goes back belongs to another file -
the FILE record of that one was lost with a bad block: the file in hand
ends there.  An address that jumps ahead, or a record after lost blocks,
makes the file FILDAMAGED.

## 4. Names

`[LAISHEV.VBKS.SUB]V.TXT;2` becomes `LAISHEV/VBKS/SUB/V.TXT`, relative
to the output directory:

- the directories, each a component; `000000` (the top) is none;
- the escapes of ODS-5 undone: `^_` a blank, `^.` a dot, `^xx` the octet
  xx, `^Uxxxx` the UCS-2 character, `^c` the character c;
- 8-bit names (ISO Latin-1 on ODS-5) made UTF-8; a NUL or a "/" in a
  name becomes "_";
- a type that is empty loses its dot: `README.;1` is `README`;
- the version is dropped for the first - the highest - version of a
  name, BACKUP writes them highest first; an older one keeps ";n":
  `V.TXT;1`;
- a directory file (characteristic 0x2000) is the directory, without
  `.DIR;1`, its data not restored;
- the case is kept as stored (ODS-2 names are upper case).  A
  /LOWERCASE may come one day; it does not exist now.

A name that would lead out of the output directory - `[^.^.]` is ".." -
is refused (OPENOUT).

`/SELECT`, `/EXCLUDE` and `/EXTRACT` take the Linux names; `/EXTRACT`
also the name with its version (`V.TXT;1`), or the specification of VMS
as the listing shows it, the case not minded.

## 5. What a file becomes

Sequential files with a carriage control (carriage return, Fortran,
print file) are texts, and become texts of Linux - what FTP of VMS makes
of them in ASCII mode:

| Record format | Becomes |
|---|---|
| Variable | the u16 count of each record dropped, the record, LF; an odd record is followed by a pad octet, dropped; a count of 0xFFFF ends the data of its VBN |
| VFC | as variable, the fixed control area (vfcsize octets, 2 by default) dropped too; the print control in it is not interpreted - every record is one line |
| Fixed | each record of rsize octets, LF; a pad octet after an odd one dropped; with "records do not cross a block" a record that does not fit begins in the next VBN |
| Stream | CR LF becomes LF; a CR alone stays |
| Stream_CR | CR becomes LF |
| Stream_LF | as it is |

Fortran carriage control: the first octet of a record is the control -
"0" a line more, "1" a form feed, the others nothing - and is dropped.

Checked against `test/vms`: variable (also with records of 4 to 9 KB,
and non-spanned - each block of it ends with 0xFFFF), VFC with print
control, fixed of 80 with carriage return, Stream, Stream_CR, Stream_LF,
fixed of 512 and undefined without a carriage control, an indexed file.
Taken from the description of RMS, not shown by a saveset here: Fortran
carriage control, fixed non-spanned records, variable and VFC records
without a carriage control, relative files.

Everything else is copied as it is on the disk, up to the end of file,
a block lost a hole of zeroes at its place: undefined and fixed records
without a carriage control (an image, an object library), variable and
VFC records without one (an object file - their counts kept), and every
relative or indexed file, whose records cannot be had without RMS: such
a file is said VMSRAW, its data is the image of the RMS file.

## 6. Attributes

- the mode from the protection, the OWNER, GROUP and WORLD classes: R -
  r, W - w; E is x for a directory only (an image of VMS does not run
  here, and RWED would make every text executable).  SYSTEM and D have
  no place.  A directory keeps rwx for its owner while files are made in
  it;
- the modification time the revision date, the access time the access
  date (else the revision date); the VMS time is taken as the local
  time of this system;
- the owner is the user who restores: a UIC is not a Linux user;
- ACLs, the expiration and backup dates, the version limit, the file
  ID: listed by `/LIST/FULL` where BACKUP lists them, not put back.

## 7. The listing

`/LIST /FORMAT=LS` prints one line per file as `ls -l`, by the Linux
names - for the extfs of Midnight Commander.  `/LIST` prints what
`BACKUP/LIST` prints, `/LIST/FULL` what `BACKUP/LIST/FULL` prints: the heading from the SUMMARY, one line - or
one entry - per file, the totals of files and blocks.  The `/FULL`
entries show the attributes of section 3 in the layout of BACKUP V8.3;
a field of the FAT not shown by the savesets of `test/vms` (a VFC file
with a maximum record size, a relative file) is shown the way the others
are, and may differ from BACKUP's words there.
