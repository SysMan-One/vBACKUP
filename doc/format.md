# vBACKUP saveset format, version 1

This document is the single source of truth for the bytes on the medium.
DESIGN.md only summarizes it.  A reader written from this document alone
must be able to list and restore any saveset.

Additions since VBACKUP X01-01, all within version 1 - a reader of X01-01
skips them by the rules of sections 5 and 6:

- SUMMARY tags KIND and FILTER (6.5): a full or an incremental saveset;
- CATALOG entries carry CTIME and DEVINO (6.4), so that a journal can be
  rebuilt from catalogs (section 9);
- the catalog STATUS value 3, PRESENT: a file the saveset covers but did
  not save (6.4, 6.6);
- the SUMMARY BASE items are absolute (realpath) names; X01-01 wrote
  them as they were given;
- the journal file (section 9) - a file of its own, not part of a saveset;
- /IMAGE (6.9): the files of a whole file system and its identity;
  the tags IMAGE, FSTYPE, FSLABEL, FSUUID, FSUSED, ROOTATTR, MOUNTOPTS,
  since X01-04;
- /PHYSICAL (6.8): one device as one sparse regular file; the tags
  PHYSICAL, DEVSIZE, SECTORSIZE, since X01-04;
- the DATAZ record (type 7) and the SUMMARY tag COMPRESS (6.7): data
  compressed in the LZ4 block format, since X01-04; a reader of an
  earlier version skips it and reports the files damaged;
- encryption (6.10): the block types EDATA and ETRAILER, the tags
  CIPHER, KDF, KDFITER, SALT, KEYCHECK, since X01-06; a reader of an
  earlier version sees every block of such a saveset as bad and
  restores nothing from it;
- the XATTR value (6.1) carries a counted name (u8 length, name) since
  X01-03; before it the name was ended by a NUL.  No saveset of the
  earlier form was ever given out: the change is made within version 1,
  and a reader takes the counted form only.

## 1. Conventions

- All integers are little-endian, unsigned unless noted: u8, u16, u32,
  u64; i64 is two's complement.  Fields are encoded and decoded byte by
  byte (put/get helpers); no C structure is ever written or read as is.
- TIME is 12 bytes: i64 seconds since 1970-01-01 00:00:00 UTC, then u32
  nanoseconds (0..999999999).
- Strings and paths are raw bytes, without a terminating NUL.  Their
  length is the length of the TLV item; where an item holds more than
  one string, each but the last is counted (ASCIC: a length, then the
  bytes).  No string anywhere in the format is ended by a NUL.  Linux
  file names are not guaranteed to be UTF-8, so no encoding is assumed.
- CRC is CRC-32/IEEE (the zlib/Ethernet one): reflected polynomial
  0xEDB88320, initial value 0xFFFFFFFF, final XOR 0xFFFFFFFF.  Check
  value: CRC("123456789") = 0xCBF43926.  This is what StarLet's
  `__util$crc32c(0, buf, len)` returns, despite its name.  It is **not**
  CRC-32C (Castagnoli).
- UUID is 16 random bytes, version 4 / variant 1 bits set (RFC 4122),
  taken from getrandom(2).

## 2. Volumes and blocks

A saveset is one or more volumes (files).  Volume 1 has the name given
by the user; volume k >= 2 has that name followed by "." and k written
in decimal, at least three digits: `x.bck`, `x.bck.002`, `x.bck.003`, ...

A volume is a sequence of blocks of one fixed size B (the block size).

- B is 8192 .. 1048576 and a multiple of 512; default 65536.
- Each block has a 64-byte header, then a payload area of P = B - 64
  bytes.  The unused tail of the payload area is zero.
- Blocks are numbered by `blkno` continuously through the whole saveset,
  starting with 0, all block types included.

Layout:

```
volume 1 : VHDR  G G G ... G
volume 2 : VHDR  G G ... G
...
volume K : VHDR  G G ... G  TRAILER

G (group) = DATA x n, then XOR        (n = gcount, 1 <= n <= N)
```

- N is the group size (`/GROUP_SIZE`).  N = 0 means no XOR blocks: the
  DATA blocks simply follow one another.
- A group never crosses a volume boundary.  The last group of a volume,
  and the last group of the saveset, may be shorter than N.
- The volume size (`/VOLUME_SIZE`) is rounded down to a multiple of B.
  Every volume except the last one is exactly that size.  The minimum is
  (N + 3) * B.
- A volume holding only VHDR and TRAILER is legal (the TRAILER did not
  fit into the previous volume).

## 3. Block header (64 bytes)

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | magic | bytes `V` `B` `K` `B` |
| 4 | 2 | hdrlen | 64 |
| 6 | 2 | version | 1 |
| 8 | 4 | bsize | B |
| 12 | 1 | type | 1 DATA, 2 XOR, 3 VHDR, 4 TRAILER; 5 EDATA, 6 ETRAILER - those two of an encrypted saveset (6.10) |
| 13 | 1 | flags | bit 0 LASTINVOL: last block of this volume; bit 1 LASTINSET: last block of the saveset |
| 14 | 2 | gindex | DATA: position in its group, 0-based; XOR: n, the number of DATA blocks it covers; else 0 |
| 16 | 16 | ssuuid | UUID of the saveset |
| 32 | 8 | blkno | block number |
| 40 | 4 | volno | volume number, from 1 |
| 44 | 4 | recoff | DATA: offset in the payload of the first record header that begins in this block, 0xFFFFFFFF if none begins here; VHDR, TRAILER: 0; XOR: 0xFFFFFFFF |
| 48 | 4 | paylen | DATA: bytes of the payload used; VHDR, TRAILER: length of their TLV body; XOR: P |
| 52 | 4 | prvrecoff | `recoff` of the previous DATA block of the same group; 0xFFFFFFFF for the first block of a group and for VHDR, TRAILER |
| 56 | 4 | prvpaylen | `paylen` of the previous DATA block of the same group; 0 when there is none |
| 60 | 4 | crc | CRC of the 64-byte header with this field set to 0, followed by the whole payload area (P bytes) |

A block is valid when magic, hdrlen, version, bsize, ssuuid match and
the CRC is right.

## 4. XOR blocks and the repair of one lost block

The payload of an XOR block is the byte-wise XOR of the payload areas
(P bytes each, zero tails included) of the n DATA blocks of its group.

When exactly one DATA block k of a group is bad:

- payload(k) = XOR payload  ^  payload of every other DATA block of the group;
- header(k) is rebuilt: magic, hdrlen, version, bsize, ssuuid, volno
  from its neighbours; type DATA; flags 0; blkno = blkno of the XOR block
  - n + gindex; gindex = its position;
- recoff(k) and paylen(k) are `prvrecoff` and `prvpaylen` of the block
  that follows k: the next DATA block of the group, or the XOR block
  when k is the last DATA block.

When the XOR block itself is bad and all DATA blocks are good, nothing
needs repair.  Two or more bad blocks in a group cannot be repaired; the
reader skips them (section 8).

## 5. The record stream

The payloads of all DATA blocks, `paylen` bytes each, taken in `blkno`
order through all volumes, form one stream of records.  A record may
cross block and volume boundaries; a record header (8 bytes) never does:
when fewer than 8 bytes remain in a payload, the writer ends that block
early (paylen < P).

Record header, 8 bytes:

| Off | Size | Field |
|---|---|---|
| 0 | 2 | type |
| 2 | 2 | flags, 0 in version 1 |
| 4 | 4 | length of the body |

| type | Record | Body |
|---|---|---|
| 1 | SUMMARY | TLV items, section 6.1 |
| 2 | FILE | TLV items, section 6.2 |
| 3 | DATA | u32 fileno, u32 reserved (0), u64 offset in the file, then the bytes (at most 1048576) |
| 4 | FEND | TLV items, section 6.3 |
| 5 | CATALOG | a sequence of entries: u32 entry length, then TLV items, section 6.4 |
| 6 | END | TLV items, section 6.5 |
| 7 | DATAZ | u32 fileno, u32 codec (1 = LZ4 block), u64 offset in the file, u32 rawlen (at most 1048576), then the compressed bytes - section 6.7 |

Order in the stream:

```
SUMMARY
  ( FILE [DATA|DATAZ ...] FEND ) ... one group per file, files in walk order
  CATALOG ...                        one or more records, at most 1 MiB body each
END
```

A reader skips a record of an unknown type by its length.

## 6. TLV items

A TLV item is u16 tag, u32 length, then the value.  A reader skips an
item of an unknown tag by its length.  One tag space serves all records,
so a tag means the same thing wherever it appears.

Value types: u8 .. u64, TIME, STR (raw bytes), UUID.

### 6.1 Per-file tags (FILE, CATALOG)

| Tag | Name | Type | Meaning |
|---|---|---|---|
| 1 | FILENO | u32 | number of the file in the saveset, from 1 |
| 2 | PATH | STR | stored name, relative to its base, `/` separated |
| 3 | FTYPE | u8 | 1 REG, 2 DIR, 3 SYMLINK, 4 HARDLINK, 5 CHR, 6 BLK, 7 FIFO, 8 SOCK |
| 4 | MODE | u32 | st_mode permission and special bits (07777) |
| 5 | UID | u32 | |
| 6 | GID | u32 | |
| 7 | UNAME | STR | owner name, when it could be resolved |
| 8 | GNAME | STR | group name, when it could be resolved |
| 9 | SIZE | u64 | size when the file was opened |
| 10 | MTIME | TIME | |
| 11 | ATIME | TIME | |
| 12 | CTIME | TIME | |
| 13 | BTIME | TIME | creation time (statx), when the file system has it |
| 14 | RDEV | u64 | major << 32 \| minor, for CHR and BLK |
| 15 | LINK | STR | SYMLINK: the target as read by readlink; HARDLINK: stored name of the first occurrence |
| 16 | XATTR | STR | u8 name length n (1..255), n bytes of name, then the value (the rest of the item); one item per attribute, in the byte order of the names; POSIX ACLs are `system.posix_acl_access` and `system.posix_acl_default`, kernel binary form |
| 17 | FSFLAGS | u32 | FS_IOC_GETFLAGS flags |
| 18 | DEVINO | u64 + u64 | st_dev, st_ino at save time |
| 19 | BASEIDX | u16 | which BASE of the SUMMARY the PATH is relative to, from 0 |
| 20 | NLINK | u32 | st_nlink |

FILE always has FILENO, PATH, FTYPE, MODE, UID, GID, SIZE, MTIME.

### 6.2 FILE record

Per-file tags of section 6.1.  It is followed by the DATA records of the
file (REG only), in increasing offset order, then by FEND.

Sparse files have no map: DATA records exist only for the regions that
hold data.  A restore truncates the file to SIZE (or to the FEND size)
and writes each DATA record at its offset; the gaps stay holes.

### 6.3 FEND record

| Tag | Name | Type | Meaning |
|---|---|---|---|
| 1 | FILENO | u32 | |
| 9 | SIZE | u64 | logical size of the file as saved |
| 32 | CRC | u32 | CRC of the DATA bytes of the file, in offset order, chained |
| 33 | STATUS | u8 | 0 OK; 1 CHANGED (the file changed while it was read); 2 READERR (a read failed, the rest of the file is missing) |

### 6.4 CATALOG entry

Per-file tags FILENO, PATH, FTYPE, MODE, UID, GID, UNAME, GNAME, SIZE,
MTIME, CTIME, LINK, NLINK, DEVINO, BASEIDX as in the FILE record, plus:

| Tag | Name | Type | Meaning |
|---|---|---|---|
| 32 | CRC | u32 | as in FEND |
| 33 | STATUS | u8 | as in FEND, or 3 PRESENT: covered, not saved in this saveset (6.6) |
| 34 | LOCVOL | u32 | volume of the block in which the FILE record begins |
| 35 | LOCBLK | u64 | blkno of that block |
| 36 | LOCOFF | u32 | offset of the FILE record header in that block's payload |

STATUS 3 is a catalog value only; FEND never carries it.  A PRESENT
entry has no FILE record in the stream and therefore no LOCVOL, LOCBLK,
LOCOFF and no CRC; its FILENO is a number of its own all the same.
CTIME and DEVINO are written since X01-02.

A listing shows the saved entries only (STATUS 0..2); the PRESENT ones
are counted.

### 6.5 SUMMARY, END, TRAILER and VHDR

SUMMARY tags:

| Tag | Name | Type | Meaning |
|---|---|---|---|
| 64 | PRODUCT | STR | `VBACKUP X01-01` |
| 65 | HOST | STR | node name |
| 66 | USER | STR | user name |
| 67 | CMDLINE | STR | the command, at most 4096 bytes |
| 68 | CREATED | TIME | start of the save |
| 69 | BASE | STR | one item per input specification, in order; BASEIDX counts them; absolute (realpath) since X01-02 |
| 70 | BLOCKSIZE | u32 | B |
| 71 | GROUPSIZE | u32 | N |
| 72 | VOLSIZE | u64 | volume size, 0 = one volume |
| 73 | COMMENT | STR | `/COMMENT` |
| 74 | SYSTEM | STR | uname -s -r -m |
| 75 | KIND | u8 | 0 FULL: every covered file is saved; 1 INCREMENTAL: a time filter chose what is saved, the catalog lists the rest as PRESENT |
| 76 | FILTER | STR | the time filter of an INCREMENTAL saveset as it was given, e.g. `/SINCE=BACKUP` |
| 77 | COMPRESS | u8 | the codec of the DATAZ records, 1 = LZ4 block; information only - a reader goes by the record types |
| 78 | PHYSICAL | u8 | 1: the saveset holds one device, block by block (6.8); also in its FILE record and catalog entry |
| 79 | DEVSIZE | u64 | the size of that device in bytes |
| 80 | SECTORSIZE | u32 | its logical sector size in bytes |
| 81 | IMAGE | u8 | 1: the saveset holds the files of one whole file system (6.9) |
| 82 | FSTYPE | STR | its type, as the kernel names it (`ext4`, `xfs`, `vfat`, ...) |
| 83 | FSLABEL | STR | its label, when it has one |
| 84 | FSUUID | STR | its UUID as text (`xxxxxxxx-xxxx-...`; for FAT the serial `XXXX-XXXX`) |
| 85 | FSUSED | u64 | bytes in use when it was saved |
| 86 | ROOTATTR | STR | the per-file tags (6.1) of its root directory: PATH `.`, owner, mode, times, XATTR items |
| 87 | MOUNTOPTS | STR | the options it was mounted with (for the operator only) |

END and TRAILER tags:

| Tag | Name | Type | Meaning |
|---|---|---|---|
| 96 | NFILES | u64 | files saved |
| 97 | NBYTES | u64 | data bytes saved |
| 98 | NERRORS | u32 | files with a status other than OK, plus files that could not be read at all |
| 99 | NBLOCKS | u64 | blocks written, all types |
| 100 | CATVOL | u32 | volume of the block where the first CATALOG record begins |
| 101 | CATBLK | u64 | its blkno |
| 102 | CATOFF | u32 | offset of the record header in its payload |
| 103 | NVOLS | u32 | number of volumes |
| 104 | NENTRIES | u64 | catalog entries |

END carries 96..98; 99 is in the TRAILER only - the number of blocks
is known only once the stream has ended.  The TRAILER block payload is
one TLV body with 96..104.

The VHDR payload is one complete SUMMARY record (header and body) - so
each volume identifies itself and its saveset.

A saveset without KIND (written by X01-01) is FULL by its contents, but
it is not to be trusted with deletions: a restore /INCREMENTAL refuses it.

### 6.6 Covered and saved

Two sets describe what a saveset holds:

- the **covered** set: every entry the input specifications select after
  the name filters - /SELECT, /EXCLUDE, the nodump flag, /NOCROSS_DEVICE
  - and /BY_OWNER;
- the **saved** set: the part of the covered set that has FILE records.
  In a FULL saveset it is the covered set itself; in an INCREMENTAL one
  the time filters - /SINCE, /BEFORE, /SINCE=BACKUP - pick it.

The catalog of an INCREMENTAL saveset lists the whole covered set: saved
entries with their status, the others as PRESENT.  So it describes the
tree as it was at the save, and a restore /INCREMENTAL can remove from a
directory what was not in it any more.  A plain FULL save writes no
PRESENT entries.

### 6.7 DATAZ and the LZ4 block format

A DATAZ record stands where a DATA record would and means the same: the
`rawlen` bytes of the file at `offset`.  The writer chooses per record;
a saveset may hold both.  The FEND and CATALOG CRC is the CRC of the raw
bytes, as for DATA; the block CRC covers the compressed bytes as they
lie in the stream.  A reader that does not know type 7 skips it: the
file comes out short or with holes, and its CRC and size disagree with
the FEND - it is reported damaged, never silently wrong.

Codec 1 is the LZ4 block format (no frame, no checksum of its own).
The compressed bytes are a series of sequences:

```
token     1 byte: high 4 bits = literal length L, low 4 bits = match length M - 4
[L ext]   if L = 15: more bytes, each added to L, until a byte < 255
literals  L bytes, copied to the output
offset    2 bytes, little-endian, 1..65535: the match begins that far back
[M ext]   if the low 4 bits are 15: more bytes, each added, until a byte < 255
match     M + 4 bytes copied from (output - offset), one byte at a time
          (a match may overlap the bytes it produces: a run)
```

The last sequence has its literals only: the block ends right after
them, with no offset.  A reader must check every length against what is
left of the input and of `rawlen`, every offset against what has been
output, and want exactly `rawlen` bytes from exactly the given input;
anything else is a bad record (its file is damaged).  A writer keeps
the last 5 bytes as literals and begins no match in the last 12 bytes
(the rules of the reference implementation, so that any LZ4 decoder
reads it); VBACKUP's own writer is a fixed greedy one, so the same data
compresses to the same bytes.

A record is written as DATAZ only when it is smaller than the DATA
record would be; incompressible data stays DATA.

### 6.8 A device, block by block (/PHYSICAL)

A saveset made with `/PHYSICAL` holds exactly one regular file: the
device (or the image file) given as input.  Its FILE record has FTYPE 1
(REG), SIZE = DEVSIZE, the name of the device node as PATH (`sdb1`), the
owner, mode and times of the node, and the tag PHYSICAL; so has its
catalog entry.  The SUMMARY carries PHYSICAL, DEVSIZE and SECTORSIZE,
and BASE is the directory of the node (`/dev`).

The data is cut in pieces of 65536 bytes (the last one shorter); a
piece that is all zeros is not written - the convention of sparse files
(6.2): a gap between DATA/DATAZ records reads as zeros.  The CRC of the
FEND is that of the bytes written, in offset order, as for any file.

So a reader that knows nothing of PHYSICAL restores such a saveset as a
sparse image file - which is what it is.  A restore onto a device must
write zeros into the gaps (a device, unlike a new file, keeps its old
bytes where nothing is written) up to DEVSIZE, and nothing beyond.

### 6.9 A whole file system (/IMAGE)

A saveset made with `/IMAGE` holds every file of one mounted file system,
saved from its mount point with names relative to its root (`etc/hosts`,
not `root/etc/hosts`), nothing of other file systems mounted below it,
nodump flags ignored.  It is an ordinary saveset of files: any reader
restores it as a tree.  The SUMMARY adds what makes the volume that
volume: IMAGE, FSTYPE, FSLABEL, FSUUID, FSUSED, ROOTATTR, MOUNTOPTS, and
BASE is the mount point.

A restore of the volume makes a new file system of FSTYPE with that
label and UUID (the format does not say how; VBACKUP runs mkfs), restores
the files into it, then applies ROOTATTR to its root directory.  Inode
numbers, the layout on the disk and anything outside the file system
(boot sectors, the partition table) are not part of it - that is 6.8.

### 6.10 An encrypted saveset (/ENCRYPT)

An encrypted saveset is an ordinary saveset whose DATA and TRAILER
blocks carry their payload encrypted and authenticated.  Everything
below the payload stays as it is: block headers, CRCs and XOR blocks
are computed over the bytes as they lie on the medium (the ciphertext),
so a reader checks, repairs (section 4) and resynchronizes a saveset
without the passphrase; it needs the passphrase only to read records.

**Primitives.**  Two, both public and fixed: ChaCha20 (RFC 8439, section
2.4: 256-bit key, 96-bit nonce, 32-bit block counter) and SHA-256
(FIPS 180-4), with HMAC-SHA256 (RFC 2104) and PBKDF2-HMAC-SHA256
(RFC 8018) built from it.  No other algorithm is involved.

**Block types.**  5 EDATA - a DATA block of an encrypted saveset; 6
ETRAILER - its TRAILER.  The header fields mean what they mean for
types 1 and 4 (`paylen` is the length of the plaintext, which is also
the length of the ciphertext).  XOR (2) and VHDR (3) blocks are not
encrypted.  A reader that does not know types 5 and 6 sees every block
of the stream as a bad block and no TRAILER: it reports the blocks lost
and restores nothing - it never reads ciphertext as records.  In the
repair of section 4, the rebuilt header of a DATA block of an encrypted
saveset has type EDATA.

**Payload area of EDATA and ETRAILER** (P bytes):

| Offset | Length | Contents |
|---|---|---|
| 0 | `paylen` | the ciphertext |
| `paylen` | P - 32 - `paylen` | zeros |
| P - 32 | 32 | TAG |

So `paylen` is at most P - 32: writer rule 2 applies with P - 32 in
place of P.

**Keys.**  The passphrase is a string of bytes, taken as it is (no
encoding, no normalization; from a key file, its first line without
the line end - LF or CR LF).

```
MK    = PBKDF2-HMAC-SHA256(passphrase, SALT, KDFITER, 32 bytes)
KENC  = HMAC-SHA256(MK, "VBACKUP ENC")
KMAC  = HMAC-SHA256(MK, "VBACKUP MAC")
CHECK = HMAC-SHA256(MK, "VBACKUP CHECK")
```

The labels are the ASCII bytes shown, without quotes and without a NUL.
SALT is 32 random bytes (getrandom(2)) taken anew for every saveset, so
no two savesets share keys even under one passphrase.

**Encryption.**  ciphertext = plaintext XOR ChaCha20(KENC, nonce, counter
0 ...), where the nonce is 12 bytes: u32 0, then u64 `blkno` (both
little-endian).  `blkno` is unique within a saveset and a payload is at
most 1048512 bytes = 16383 ChaCha20 blocks, so no nonce and counter are
ever used twice under one key.

**TAG** = HMAC-SHA256(KMAC, M), all 32 bytes, where M is

```
ssuuid (16)  bsize (u32)  blkno (u64)  volno (u32)  type (u8)
gindex (u16)  recoff (u32)  paylen (u32)  ciphertext (paylen bytes)
```

- the fields of the block header, little-endian, in this order.  The
`flags`, `prvrecoff`, `prvpaylen` and `crc` fields are not part of it:
the repair of section 4 rebuilds `flags` as 0.  A block whose CRC is good
but whose TAG is wrong (compare all 32 bytes) is a bad block, exactly as
if its CRC were wrong: it is repaired from its group when it can be,
else it is lost.  A repaired block must pass its TAG too.  A reader
checks the TAG before it decrypts, and decrypts only blocks that passed.

**VHDR.**  The VHDR of an encrypted saveset carries a short SUMMARY in
the clear, the same in every volume - so that any one volume can be
opened by itself - and nothing that names the system or the files:
PRODUCT, BLOCKSIZE, GROUPSIZE, VOLSIZE and

| Tag | Name | Type | Meaning |
|---|---|---|---|
| 88 | CIPHER | u8 | 1: ChaCha20 and HMAC-SHA256 as above |
| 89 | KDF | u8 | 1: PBKDF2-HMAC-SHA256 |
| 90 | KDFITER | u32 | its iteration count, at least 1000 |
| 91 | SALT | STR | 32 bytes |
| 92 | KEYCHECK | STR | CHECK, 32 bytes |

A reader that finds CIPHER derives the keys and compares CHECK with
KEYCHECK before anything else: a mismatch means a wrong passphrase, not
a damaged saveset.  A CIPHER or KDF value it does not know: it does not
read the saveset.  The complete SUMMARY is the first record of the
(encrypted) stream, as in any saveset, and repeats these tags.

**What stays visible** without the passphrase: the block size, group
size, volume size and number of volumes; the length of the stream and
where records begin in each block (`paylen`, `recoff`); when the save was
made only by the file times of the volumes.  Not visible: names, sizes
and attributes of the files, their contents, the host, the user, the
command, the counts of the TRAILER.

**Strength.**  The passphrase is the key: PBKDF2 slows down every guess
by KDFITER (default 600000) HMAC computations, it does not make a weak
passphrase strong.  A passphrase of five or more random words is
advised.  The format does not encrypt the journal (section 9), which is
a file of the saving system.

## 7. Writer rules

1. Volume 1, block 0: VHDR.  Then the record stream.
2. A DATA block is closed when full, when fewer than 8 bytes are left for
   a record header, at the end of the stream, or when the volume is full.
3. After n DATA blocks (n = N, or fewer at the end of a volume or of the
   stream) an XOR block follows, if N > 0.
4. A volume is closed when the next block of the current group and its
   XOR block would not fit.  The next volume starts with VHDR.
5. After the END record: the last group is closed, then the TRAILER
   block, with LASTINVOL and LASTINSET set.  The last block of every other
   volume has LASTINVOL set.

## 8. Reader contract

Two access modes share one record parser.

**Catalog mode** (`/LIST`, `/SELECT` on restore, `/EXTRACT`): read the
last block of the last volume - it must be the TRAILER.  Go to CATVOL,
CATBLK, CATOFF and read the CATALOG records.  A single file is reached
through its LOCVOL, LOCBLK, LOCOFF without reading anything else.

**Sequential mode** (full restore, `/COMPARE`, damaged or truncated
savesets): read the blocks in order.  For each group, check every CRC:

- all good: deliver the payloads;
- one bad DATA block: repair it (section 4);
- two or more bad: report the lost blocks, mark the file being restored
  as damaged, and resume at the `recoff` of the next good DATA block
  (records that only continue are skipped).  DATA records carry their
  fileno, so a resumed reader always knows which file a DATA record
  belongs to;
- volume missing: report it; files that lie wholly in the other volumes
  are still restored.

A saveset without a TRAILER (the save was interrupted) is still readable
in sequential mode up to the last good block.

Reading a saveset from standard input is not part of stage 1.


## 9. The journal

The journal is a file of its own, not part of any saveset.  It records
which savesets were made with /RECORD and, for every file saved by them,
the state the file had when it was saved.  /SINCE=BACKUP compares a file
on the disk with that state.

Location: /JOURNAL=file; otherwise `/var/lib/vbackup/vbackup.jnl` for
root and `$HOME/.vbackup/vbackup.jnl` for everybody else.

Layout: a 16-byte header - the bytes `V` `B` `K` `J`, u16 version (1),
u16 reserved, u64 count of records - then records of the saveset record
format (section 5: u16 type, u16 flags, u32 length, a TLV body), all
little-endian.  The whole file ends with a u32 CRC of everything before
it.

| type | Record | Tags |
|---|---|---|
| 16 | SSET | SSUUID (128, UUID), SPEC (129, STR: the saveset, absolute), CREATED (68), KIND (75), FILTER (76), BASE (69, one per base), NFILES (96), NBYTES (97) |
| 17 | FSTATE | PATH (2, STR: the absolute name of the file), DEVINO (18), CTIME (12), MTIME (10), SIZE (9), FTYPE (3), SSUUID (128: the saveset that saved it), RECORDED (130, TIME: when) |

Rules:

1. The journal is rewritten whole: written to `<journal>.tmp`, synced,
   renamed over the journal.  Writers take an exclusive flock on
   `<journal>.lock` for the whole read-modify-write.
2. The key of an FSTATE is PATH: the absolute name of the file, made of
   the realpath of its base and its stored name.  One FSTATE per PATH;
   a later record replaces an earlier one.
3. Only files saved with STATUS OK are recorded.  A file saved CHANGED
   or READERR is not, so the next /SINCE=BACKUP saves it again.
4. /SINCE=BACKUP saves a file when the journal has no FSTATE for it, or
   when its st_ino, ctime, mtime or size differ from the FSTATE.  st_dev
   is recorded but not compared: btrfs subvolumes and network file
   systems get another device number at every mount, and a replaced
   file shows in the inode and the ctime anyway.
   ctime catches a change of the permissions, the owner, the attributes,
   or a rename into the place.  Directories are always saved.
5. The journal can be rebuilt from catalogs: every saved entry of a
   catalog with STATUS OK, CTIME and DEVINO (X01-02 and later) gives an
   FSTATE, the newer saveset winning.  A journal is a convenience, never
   the only copy of anything.
