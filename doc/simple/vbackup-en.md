# VBACKUP — very simple

VBACKUP puts your files into one big "box".
You can put the box on a USB stick.
Later you can take everything out of the box again.

```
   your folder              box (saveset)               USB stick
  +-----------+            +--------------+            +-------+
  | /home/ivan|  ------->  |  ivan.bck    |  ------->  | /mnt/ |
  |  letters  |  vbackup   | [][][][][][] |            |  usb  |
  |  photos   |            +--------------+            +-------+
  +-----------+
```

---

## The three main commands

Put files into a box:

```
vbackup /home/ivan /mnt/usb/ivan.bck
```

See what is in the box:

```
vbackup /mnt/usb/ivan.bck /LIST
```

Take everything back out (into a new folder):

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

That is all you need. Below, step by step.

And more: make the box smaller — section 7; lock it with a password — section 8;
save a whole disk — sections 9 and 10.

---

## Before you start

1. Open the window for commands. It is called the "terminal".
2. Type the command exactly as it is written here.
3. Press the **Enter** key.

Important rules:

- The name of a box **always** ends with `.bck` or `.sav`. For example: `ivan.bck`.
  If you forget it, VBACKUP does not make a box. It just copies the folder.
  (Another name? Then add `/SAVE_SET`.)
- A word like `/LOG` or `/LIST` is a "qualifier". It **always** starts with `/`.
  If you type `.log` instead of `/LOG`, VBACKUP says `MAXPARM` and does nothing.
- A qualifier glued to the name works too: `box.sav/sav/log`. VBACKUP then says `GLUED`.
- The spaces between the parts of a command matter. Do not leave them out.
- Big and small letters matter: `/home/ivan` and `/Home/Ivan` are different.
- At the end VBACKUP writes `completed`. That is good. It means it worked.

In the examples:

- `/home/ivan` is your folder with files.
- `/mnt/usb` is the USB stick.
- `/mnt/disk2` is a second disk.

Put your own names instead.

---

## 1. Save my files

**Why:** so that you have a copy if the computer breaks.

**What to type:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /LOG /VERIFY
```

`/LOG` shows every file. `/VERIFY` checks the box right away.
You can leave them out; then VBACKUP shows only the start, the totals and the end.

**What you will see:**

```
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-STARTED, Operation: save, Input: /home/ivan, Output: /mnt/usb/ivan.bck - started
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-CREATED, Volume: /mnt/usb/ivan.bck - created
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan - saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan/letters - saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan/letters/anna.txt - saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan/photos - saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan/photos/cat.jpg - saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan/photos/dog.jpg - saved
04-10-2026 12:59:32.288 2278116 %VBACKUP-I-SAVESUMM, Files: 6, Bytes: 50010, Blocks: 4, Volumes: 1 - saved
04-10-2026 12:59:32.288 2278116 %VBACKUP-I-VERIFYING, Saveset: /mnt/usb/ivan.bck - verifying
...
04-10-2026 12:59:32.292 2278116 %VBACKUP-I-CMPSUMM, Files: 6, Differences: 0 - compared
04-10-2026 12:59:32.292 2278116 %VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.01 - completed
```

**What it means:**

- The start of each line is the date, the time and a number. You can ignore it.
- `started` and `completed` — the work began and ended. `completed` means all went well.
- `saved` — the file is in the box.
- `Files: 6 ... saved` — 6 things were saved in all.
- `Differences: 0` — the box was checked, everything matches. Well done!

**Small or old USB stick (FAT32)?** Cut the box into pieces of 4 GB:

```
vbackup /home/ivan /mnt/usb/ivan.bck /VOLUME_SIZE=4G
```

The pieces are called `ivan.bck`, `ivan.bck.002`, `ivan.bck.003`…
Always keep them together, in one folder.

---

## 2. See what is inside

**Why:** to know what is in the box without taking anything out.

**What to type:**

```
vbackup /mnt/usb/ivan.bck /LIST
```

**What you will see (the end of the list):**

```
ivan/
ivan/letters/
ivan/letters/anna.txt                                    10   4-OCT-2026 12:59
ivan/photos/
ivan/photos/cat.jpg                                   20000   4-OCT-2026 12:59
ivan/photos/dog.jpg                                   30000   4-OCT-2026 12:59

Total of 6 files, 50010 bytes
End of save set
```

**What it means:**

- Each line is one file. If it ends with `/`, it is a folder.
- The number is the size of the file. Then comes when the file was last changed.
- At the top there is also a "header": when and by whom the box was made.

---

## 3. Get my files back

**Why:** files are lost or broken — we take them out of the box.

**What to type:**

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored /LOG
```

**What you will see:**

```
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-STARTED, Operation: restore, Input: /mnt/usb/ivan.bck, Output: /home/ivan/restored - started
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan/letters - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan/letters/anna.txt - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan/photos - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan/photos/cat.jpg - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan/photos/dog.jpg - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTSUMM, Files: 6, Bytes: 50010 - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-COMPLETED, Operation: restore, Seconds: 0.00 - completed
```

**What it means:**

- `restored` — the file is back.
- The files are in a new folder, `/home/ivan/restored/ivan/…`.
  Look at them there, calmly. Nothing old was touched.

Tip: always take files out into a **new, empty** folder. It is safer.

---

## 4. Get one file back

**Why:** you need just one file, not all of them.

First look up the exact name of the file with the command from part 2.
For example: `ivan/letters/anna.txt`.

**What to type:**

```
vbackup /mnt/usb/ivan.bck /EXTRACT=ivan/letters/anna.txt /home/ivan/anna.txt
```

**What you will see:**

```
04-10-2026 12:59:45.611 2281302 %VBACKUP-I-STARTED, Operation: extract, Input: /mnt/usb/ivan.bck, Output: /home/ivan/anna.txt - started
04-10-2026 12:59:45.611 2281302 %VBACKUP-I-COMPLETED, Operation: extract, Seconds: 0.00 - completed
```

That is good. The file is now in `/home/ivan/anna.txt`.

You can also just show the file on the screen, without saving it:

```
vbackup /mnt/usb/ivan.bck /EXTRACT=ivan/letters/anna.txt
```

```
04-10-2026 12:59:47.204 2281449 %VBACKUP-I-STARTED, Operation: extract, Input: /mnt/usb/ivan.bck - started
Dear Anna
04-10-2026 12:59:47.204 2281449 %VBACKUP-I-COMPLETED, Operation: extract, Seconds: 0.00 - completed
```

If the name is wrong, you will see:

```
04-10-2026 12:59:49.082 2281596 %VBACKUP-E-NOTFOUND, File: ivan/letters/nope.txt - is not in the saveset
```

It means: there is no such file in the box. Check the name in the list (part 2).

---

## 5. Save only what is new (every day)

**Why:** so you do not copy everything again each day, only what changed.

**Step 1. Once — a big box with everything:**

```
vbackup /home/ivan /mnt/usb/full.bck /RECORD
```

```
04-10-2026 13:00:01.548 2284063 %VBACKUP-I-STARTED, Operation: save, Input: /home/ivan, Output: /mnt/usb/full.bck - started
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-SAVESUMM, Files: 4, Bytes: 1842, Blocks: 4, Volumes: 1 - saved
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-RECORDED, Files: 3, Journal: /var/lib/vbackup/vbackup.jnl - recorded
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.01 - completed
```

`/RECORD` means: remember what is saved. VBACKUP keeps a notebook for this — the "journal".

**Step 2. Every day — a small box with only the new things:**

```
vbackup /home/ivan /mnt/usb/mon.bck /SINCE=BACKUP /RECORD
```

```
04-10-2026 13:00:03.285 2284360 %VBACKUP-I-STARTED, Operation: save, Input: /home/ivan, Output: /mnt/usb/mon.bck - started
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-SAVESUMM, Files: 2, Bytes: 120, Blocks: 4, Volumes: 1 - saved
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-INCRSUMM, Files: 2 - unchanged, listed as present, not saved
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-RECORDED, Files: 1, Journal: /var/lib/vbackup/vbackup.jnl - recorded
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.01 - completed
```

The journal is a notebook where VBACKUP remembers what it has saved.
For root it is `/var/lib/vbackup/vbackup.jnl`; for any other user it is
`~/.vbackup/vbackup.jnl` (in your home folder). Leave it alone.

The next day use another name: `tue.bck`, then `wed.bck`, and so on.

**Get everything back from the chain of boxes:** list them with commas,
**from the oldest to the newest**, and add `/INCREMENTAL`:

```
vbackup /mnt/usb/full.bck,/mnt/usb/mon.bck,/mnt/usb/tue.bck /home/ivan/restored /INCREMENTAL /LOG
```

**What you will see (the end):**

```
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-DELETED, File: /home/ivan/restored/ivan/photos/dog.jpg - deleted: it is not in the incremental saveset
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-RESTSUMM, Files: 13, Bytes: 50021 - restored
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-COMPLETED, Operation: restore, Seconds: 0.02 - completed
```

**What it means:**

- The files come back as they were on the day of the last box.
- `deleted` — you deleted this file on Tuesday, so it is not there. That is right.

Remember: no spaces after the commas.

---

## 6. Copy a folder to another disk

**Why:** just to make a copy of a folder, without a box.

**What to type** (the name **without** `.bck`):

```
vbackup /home/ivan /mnt/disk2 /VERIFY
```

**What you will see:**

```
04-10-2026 13:00:12.410 2285890 %VBACKUP-I-STARTED, Operation: copy, Input: /home/ivan, Output: /mnt/disk2 - started
04-10-2026 13:00:12.418 2285890 %VBACKUP-I-CPYSUMM, Files: 6, Bytes: 20021 - copied
04-10-2026 13:00:12.418 2285890 %VBACKUP-I-COMPLETED, Operation: copy, Seconds: 0.01 - completed
```

That is good. The folder is now here too: `/mnt/disk2/ivan`.

With `/LOG`, VBACKUP shows every file:

```
...
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-COPIED, File: /mnt/disk2/ivan/photos/cat.jpg - copied
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-CPYSUMM, Files: 6, Bytes: 20021 - copied
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-COMPLETED, Operation: copy, Seconds: 0.01 - completed
```

`copied` — the file is copied.

---

## 7. Make the box smaller

**Why:** so that the box takes less room on the USB stick.

**What to type:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /DATA_FORMAT=COMPRESSED
```

**What you see:** the start, the totals and `completed`, as in section 1. The box is ready, and it is smaller.

Compare the same folder without and with compression:

```
without:  1048576 bytes
with:      655360 bytes
```

**What it means:** letters and documents shrink well.
Photos, videos and archives (`.jpg`, `.mp4`, `.zip`) are packed already —
VBACKUP keeps them as they are. Nothing breaks.

Getting a small box back is the same as always, nothing to add:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

**Careful:** an old VBACKUP (before X01-04) does not understand a small box.
It says the files are damaged:

```
%VBACKUP-E-CRCERR, /home/ivan/restored/ivan/letters/letter1.txt: checksum mismatch, the data differ from what was saved
%VBACKUP-E-FILDAMAGED, /home/ivan/restored/ivan/letters/letter1.txt is incomplete: its data was lost in bad blocks
```

The files in the box are fine. Just install the new VBACKUP.

---

## 8. Lock the box with a password

**Why:** so that nobody else can look into the box.
For example, if the USB stick is lost or stolen.

**What to type:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /ENCRYPT
```

VBACKUP asks for the password two times:

```
Passphrase for /mnt/usb/ivan.bck:
The same passphrase again:
```

Type the password and press Enter. Then type it once more, the same.
While you type, **nothing is shown** on the screen, not even stars. That is right.

**What it means:** the box is locked. Without the password nobody can look
inside or take files out. Not even the names of the files can be seen.

**Open a locked box:** exactly as before — see inside (section 2),
get files back (sections 3 and 4). Nothing to add:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

VBACKUP sees by itself that the box is locked and asks:

```
Passphrase for /mnt/usb/ivan.bck:
```

On a small computer opening takes a second or a few. That is on purpose:
it makes guessing the password very slow.

**The password is the key. Very important!**

- If the password is lost, **NOBODY** can open the box. Not you, not the person
  who set up your computer, not even the author of VBACKUP.
- Take a long password: five or more random words.
- Write it on paper. Keep the paper in a safe place.

**Saving without you, at a set time (cron)? A file manager?** Nobody is there to type the password.
Then put the password into a file. Only the first line counts.
And make the file private — only you may read it:

```
printf 'my long password words here\n' > /root/backup.key
chmod 600 /root/backup.key
```

Now give the file instead of typing:

```
vbackup /home/ivan /mnt/usb/ivan.bck /ENCRYPT /KEY_FILE=/root/backup.key
```

Or say it once, and VBACKUP takes the file by itself every time:

```
export VBACKUP_KEY_FILE=/root/backup.key
```

To lock a new box, still write `/ENCRYPT`.

File managers (MC, far2l, Total Commander, Double Commander) cannot ask
for a password. They open a locked box only this way, with `VBACKUP_KEY_FILE`.

**vbkx** (section 11) opens a locked box too. Give it the file with `-k`, or it asks:

```
vbkx x /mnt/usb/ivan.bck -k /root/backup.key
```

**Good to know:**

- A broken locked box is repaired as before (`BLKFIXED`). For that no password is needed.
- If somebody changed the box on purpose, VBACKUP notices it (`BLKFORGED`, section 12).

**Careful:** an old VBACKUP (before X01-06) cannot open a locked box.
It just says blocks are lost and writes nothing. Install the new VBACKUP.

---

## 9. Save a whole disk or partition, just as it is

**Why:** to make an exact copy of the whole disk — every little piece of it.
This is how you copy the disk the computer starts from, or an encrypted disk.

Only the chief user of the computer can do this — **root**.

**First find the name of the disk.** This is very important:

```
lsblk
```

You see a list of disks, for example `sdb`, and on it a partition `sdb1`.
The full name of the partition is `/dev/sdb1`.

**Before you save:** nobody may write to this disk.
Unplug it from the system (`umount`) or connect it "read-only".
Otherwise the copy is broken, and you will not notice.

**What to type:**

```
vbackup /dev/sdb1 /mnt/usb/sdb1.bck /PHYSICAL
```

**What you see:**

```
%VBACKUP-I-STARTED, Operation: save, Input: /dev/sdb1, Output: /mnt/usb/sdb1.bck - started
%VBACKUP-I-PHYSSUMM, Device: /dev/sdb1, Bytes: 67108864, Data: 1507328 - the rest zeros
%VBACKUP-I-SAVESUMM, Files: 1, Bytes: 1507328, Blocks: 29, Volumes: 1 - saved
%VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.35 - completed
```

**What it means:** a disk of 64 MB is saved. It held 1.5 MB of real data.
Empty places (zeros) take no room in the box.

**Put it back on a disk.** This **erases everything** on `/dev/sdc1`!
Check the name with `lsblk` twice.

```
vbackup /mnt/usb/sdb1.bck /dev/sdc1 /PHYSICAL /REPLACE
```

VBACKUP asks:

```
%VBACKUP-I-STARTED, Operation: restore, Input: /mnt/usb/sdb1.bck, Output: /dev/sdc1 - started
Everything on /dev/sdc1 (134217728 bytes) is to be overwritten with the device saved in /mnt/usb/sdb1.bck.
Type YES to go on:
```

Type `YES` in capital letters and press Enter. Any other answer means no.

**What you see:**

```
%VBACKUP-I-PHYSLARGER, Device: /dev/sdc1, Bytes: 134217728, Saved: 67108864 - larger: the rest stays as it is, the file system keeps its old size
%VBACKUP-I-PHYSUUID, Device: /dev/sdc1 - now carries the labels and UUIDs of the device saved: never mount it beside the original
%VBACKUP-I-PHYSSUMM, Device: /dev/sdc1, Bytes: 67108864, Data: 1507328 - the rest zeros
%VBACKUP-I-COMPLETED, Operation: restore, Seconds: 0.41 - completed
```

**What it means:**

- the new disk is bigger — the copy sits at its start, the rest is untouched;
- the copy is a twin of the old disk. **Never connect** the old and the new
  disk at the same time: they get mixed up;
- a disk smaller than the one saved does not fit — VBACKUP refuses.

**Put it into an image file** (erases nothing):

```
vbackup /mnt/usb/sdb1.bck /home/ivan/sdb1.img /PHYSICAL
```

---

## 10. Save a whole file system and make it again on another disk

**How it differs from section 9:** section 9 copies every piece of the disk;
here VBACKUP copies all the **files** of the disk and remembers what disk it was.
The new disk may have another size.

Only **root** can do this too.

**What to type** (`/mnt/photos` is where the disk is connected):

```
vbackup /mnt/photos /mnt/usb/photos.bck /IMAGE
```

**What you see:**

```
%VBACKUP-I-STARTED, Operation: save, Input: /mnt/photos, Output: /mnt/usb/photos.bck - started
%VBACKUP-I-SAVESUMM, Files: 11, Bytes: 760000, Blocks: 16, Volumes: 1 - saved
%VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.09 - completed
```

**Make the disk again on `/dev/sdc1`.** This **erases everything** on `/dev/sdc1`!

```
vbackup /mnt/usb/photos.bck /dev/sdc1 /IMAGE /REPLACE
```

**What you see:**

```
%VBACKUP-I-STARTED, Operation: restore, Input: /mnt/usb/photos.bck, Output: /dev/sdc1 - started
%VBACKUP-I-PHYSUUID, Device: /dev/sdc1 - now carries the labels and UUIDs of the device saved: never mount it beside the original
%VBACKUP-I-IMGSUMM, Device: /dev/sdc1, Type: ext4, Files: 11, Bytes: 760000 - file system made, files restored
%VBACKUP-I-COMPLETED, Operation: restore, Seconds: 1.27 - completed
```

**What it means:** a new disk of the same kind (ext4) with the same name
was made on `/dev/sdc1`, and all the files were put on it.

Remember:

- it needs the program that makes disks of that kind
  (`mkfs.ext4`, `mkfs.vfat` …). The person who set up your computer installs it;
- this way you **cannot** move the disk the computer starts from.
  For that, save the whole disk (`/dev/sdb`, not `/dev/sdb1`) as in section 9;
- do not connect the old and the new disk at the same time — they are twins.

---

## 11. No VBACKUP here? Use vbkx

**Why:** you are on another computer, and VBACKUP is not installed there.
But there is `vbkx` — one small program. Bring it on the same USB stick.

**See what is in the box:**

```
vbkx l /mnt/usb/ivan.bck
```

```
2026-10-04 12:59:23         4096 d0755 ivan
2026-10-04 12:59:23         4096 d0755 ivan/letters
2026-10-04 12:59:23           10 -0644 ivan/letters/anna.txt
2026-10-04 12:59:23         4096 d0755 ivan/photos
2026-10-04 12:59:23        20000 -0644 ivan/photos/cat.jpg
2026-10-04 12:59:23        30000 -0644 ivan/photos/dog.jpg
```

The letter `d` is a folder, `-` is an ordinary file.

**Take everything out into a folder:**

```
vbkx x /mnt/usb/ivan.bck -C /home/ivan/restored
```

Nothing is written — it worked.

**Take out only one folder:**

```
vbkx x /mnt/usb/ivan.bck -C /home/ivan/restored ivan/photos
```

**Show one file on the screen:**

```
vbkx p /mnt/usb/ivan.bck ivan/letters/anna.txt
```

```
Dear Anna
```

**Check that the box is whole:**

```
vbkx t /mnt/usb/ivan.bck
```

```
/mnt/usb/ivan.bck: all files read, all checksums match
```

It means: the box is whole.

If a file is already there, `vbkx` does not touch it and says:

```
vbkx: ivan/letters/anna.txt exists, not extracted (-f to overwrite)
```

To replace it, add `-f`.

---

## 12. If something went wrong

A message looks like this: `%VBACKUP-E-NAME, File: name - text`.
First comes what it is about (a file, a box, a disk), then what happened.
The letter after `VBACKUP-` tells you how serious it is:

- `I` — just telling you. All is well.
- `W` — a warning. Look carefully.
- `E` or `F` — an error. Something was not done.

The last line, `COMPLETED`, says how it all went: `completed` — all is well;
`completed with warnings` — look carefully; `completed with errors` — something was not done.

### FILEEXISTS

**What you see:**

```
%VBACKUP-W-FILEEXISTS, File: /home/ivan/restored/ivan/letters/anna.txt - already exists, not restored
```

**What happened:** such a file is already there. VBACKUP keeps it safe and does not touch it.

**What to do:** take files out into a new, empty folder:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored2
```

or replace the old files (careful!):

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored /REPLACE
```

### OPENOUT … errno: 17 (File exists)

**What you see:**

```
%VBACKUP-E-OPENOUT, File: /mnt/usb/ivan.bck, errno: 17 - cannot be created as output (File exists)
```

**What happened:** a box with this name is already there.

**What to do:** give another name:

```
vbackup /home/ivan /mnt/usb/ivan2.bck
```

or write over the old box (the old one will be gone!):

```
vbackup /home/ivan /mnt/usb/ivan.bck /REPLACE
```

### NOTSAVESET

**What you see:**

```
%VBACKUP-E-NOTSAVESET, File: /mnt/usb/fake.bck - is not a saveset
```

**What happened:** this file is not a VBACKUP box.

**What to do:** check the name. See what is on the USB stick:

```
ls /mnt/usb
```

### OPENIN … errno: 2 (No such file or directory)

**What you see:**

```
%VBACKUP-E-OPENIN, File: /mnt/usb/nosuch.bck, errno: 2 - cannot be opened as input (No such file or directory)
```

**What happened:** there is no such file (a typo in the name).

**What to do:** check the name with `ls /mnt/usb`.

### NOPARAM

**What you see:**

```
%VBACKUP-E-NOPARAM, Parameter: output specification - is missing
```

**What happened:** you asked for `/LIST`, but the file is not a box, so VBACKUP thought
you wanted to save something, and you did not say where to.

**What to do:** check the name with `ls /mnt/usb`.

### IVOP

**What you see:**

```
%VBACKUP-E-IVOP, cannot tell what to do: the input does not exist - and for a save the output must be named .bck or .sav, or /SAVE_SET given
```

**What happened:** the first thing you wrote is not there. Most likely a typo.

**What to do:** check the name:

```
ls /home/ivan
```

Is the first thing a box? Then its name should end with `.bck` or `.sav`.
Or add `/SAVE_SET`.

### MAXPARM

**What you see:**

```
%VBACKUP-E-MAXPARM, Parameter: .log - one too many: only an input and an output are taken; a qualifier begins with /
```

**What happened:** too many words. You probably typed `.log` instead of `/LOG`.

**What to do:** a qualifier always starts with `/`:

```
vbackup /home/ivan /mnt/usb/ivan.bck /LOG
```

### GLUED

**What you see:**

```
%VBACKUP-I-GLUED, Parameter: box.sav/sav - the qualifiers glued to it are taken as qualifiers
```

**What happened:** just telling you. `box.sav/sav` was understood as `box.sav /SAVE_SET`.

**What to do:** nothing. All is well.

### BLKFIXED

**What you see:**

```
%VBACKUP-I-BLKFIXED, Block: 3, Volume: 1 - was bad, rebuilt from its group
```

**What happened:** a small piece of the box was broken, but VBACKUP fixed it by itself.
**All files are fine.**

**What to do:** the USB stick may be starting to fail.
Soon make a new box on another USB stick.

### BLKFORGED

**What you see:**

```
%VBACKUP-W-BLKFORGED, Block: 3, Volume: 1 - is not what was written: its CRC is right, its authentication fails
```

**What happened:** a piece of a locked box was changed **on purpose**.
Its checksum looks right, but the lock says: this is not what VBACKUP wrote.

If `BLKFIXED` comes right after it, the piece was repaired. **Your files are fine.**

**What to do:** find out who could write to the box.
Keep your boxes where nobody else can change them.

### BLKLOST and FILDAMAGED

**What you see:**

```
%VBACKUP-E-BLKLOST, Block: 3, Volume: 1 - is bad and cannot be rebuilt
%VBACKUP-E-BLKLOST, Block: 4, Volume: 1 - is bad and cannot be rebuilt
%VBACKUP-E-FILDAMAGED, File: /home/ivan/restored/ivan/photo1.jpg - is incomplete: its data was lost in bad blocks
```

**What happened:** part of the box is broken and could not be fixed.
The files named by `FILDAMAGED` came back incomplete. **All the others are fine.**

**What to do:** take those files from another box, if you have one.
Next time make two boxes on two different USB sticks.

### FILLOST

**What you see:**

```
%VBACKUP-E-FILLOST, File: /home/ivan/restored/ivan/photo10.jpg - not restored: its records were lost in bad blocks
```

**What happened:** this file was lost with the broken piece. It is not there at all.

**What to do:** take it from another, older box:

```
vbackup /mnt/usb/old.bck /LIST
```

### UNNAMED

**What you see:**

```
%VBACKUP-W-UNNAMED, Saveset: /mnt/usb/ivan.bck - blocks were lost and it has no catalog: files missing from the restore cannot all be named
```

**What happened:** the box is broken, and its "table of contents" is gone too.
Some files may be lost, but VBACKUP cannot name them all.

**What to do:** compare what came back with the list from another box:

```
vbackup /mnt/usb/old.bck /LIST
```

### MISSVOL

**What you see:**

```
%VBACKUP-E-MISSVOL, Volume: 2, Saveset: /mnt/usb/ivan.bck - is missing
```

**What happened:** the box was cut into pieces, and one piece is missing
(for example `ivan.bck.002`).

**What to do:** put all the pieces into one folder, do not change their names, and try again:

```
ls /mnt/usb
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

### NOTRAILER

**What you see:**

```
%VBACKUP-W-NOTRAILER, Saveset: /mnt/usb/ivan.bck - has no trailer: the save did not complete, or its last volume is missing
```

**What happened:** the save did not finish (the power went off, the disk was full),
or the last piece is missing.

**What to do:** the files up to the break can still be taken out — the usual way.
Then make the box again.

### NOTINCR

**What you see:**

```
%VBACKUP-E-NOTINCR, Saveset: /mnt/usb/ivan.bck - it has no catalog: nothing restored with /INCREMENTAL
```

**What happened:** `/INCREMENTAL` needs a whole box with a "table of contents". This one has none.

**What to do:** take it out without `/INCREMENTAL`:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

### WRONGKEY

**What you see:**

```
%VBACKUP-E-WRONGKEY, Saveset: /mnt/usb/ivan.bck - the passphrase does not open it
```

**What happened:** the password is not the right one. Nothing was written.

**What to do:** try again, slowly. Check big and small letters (the **Caps Lock** key!).
Password from a key file? Only its first line counts. Look at it:

```
head -1 /root/backup.key
```

### NOKEY

**What you see:**

```
%VBACKUP-E-NOKEY, Saveset: /mnt/usb/ivan.bck - needs a passphrase, and there is no terminal to ask it on: give /KEY_FILE=file or VBACKUP_KEY_FILE
```

**What happened:** the box is locked, and there is no place to ask for the password
(cron, a file manager).

**What to do:** give the key file (section 8):

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored /KEY_FILE=/root/backup.key
```

or say once:

```
export VBACKUP_KEY_FILE=/root/backup.key
```

### KEYFILE

**What you see:**

```
%VBACKUP-E-KEYFILE, Key file: /root/backup.key - others may read or change it - chmod 600 it
```

**What happened:** other people can read the key file. VBACKUP does not trust it.

**What to do:** make it private:

```
chmod 600 /root/backup.key
```

The same message comes when the file is empty, or its first line is too long.
Then write the password into it again (section 8).

### KEYMATCH

**What you see:**

```
%VBACKUP-E-KEYMATCH, the two passphrases differ: nothing saved
```

**What happened:** the two passwords you typed are not the same. Nothing was saved.

**What to do:** try again, slowly. Nothing is shown while you type, so type carefully.

### PHYSMOUNTED

**What you see:**

```
%VBACKUP-E-PHYSMOUNTED, Device: /dev/sdb1 - is mounted read-write on /mnt/photos: unmount it, mount it read-only, or save a snapshot
```

**What happened:** the disk is connected and can be written to. The copy would be broken.

**What to do:** disconnect the disk and try again:

```
umount /mnt/photos
vbackup /dev/sdb1 /mnt/usb/sdb1.bck /PHYSICAL
```

When you put a box back, the same message means: VBACKUP writes nothing to
a connected disk. Disconnect it.

### PHYSHELD

**What you see:**

```
%VBACKUP-E-PHYSHELD, Device: /dev/sdb2 - is in use (swap): free it first, or save what uses it
```

**What happened:** the system itself uses this disk (swap, LVM, RAID, encryption).

**What to do:** ask the person who set up your computer. Do not touch this disk yourself.

### PHYSREPLACE

**What you see:**

```
%VBACKUP-E-PHYSREPLACE, Device: /dev/sdc1 - everything on it would be overwritten: give /REPLACE to do so
```

**What happened:** VBACKUP protects the disk: without `/REPLACE` it does not erase it.

**What to do:** check the name of the disk (`lsblk`). If it is surely the right one, add `/REPLACE`.

### PHYSSMALL

**What you see:**

```
%VBACKUP-E-PHYSSMALL, Device: /dev/sdc1, Bytes: 33554432, Saved: 67108864 - too small, nothing written
```

**What happened:** the new disk is smaller than the one saved. It does not all fit.

**What to do:** take a bigger disk. Or put the box into an image file (section 9).

### PHYSABORT

**What you see:**

```
%VBACKUP-E-PHYSABORT, Device: /dev/sdc1 - not overwritten: the answer was not YES
```

**What happened:** you did not answer `YES`. Nothing is erased.

**What to do:** if you really want it, try again and type `YES` in capital letters.

### IMGNOTVOL

**What you see:**

```
%VBACKUP-E-IMGNOTVOL, File: /home/ivan - is neither the mount point of a file system nor a device: /IMAGE saves a whole volume
```

**What happened:** `/IMAGE` saves a whole disk, and you gave an ordinary folder.

**What to do:** give the place where the disk is connected (for example `/mnt/photos`),
or save the folder the usual way (section 1).

### IMGNOTMNT

**What you see:**

```
%VBACKUP-E-IMGNOTMNT, Device: /dev/sdb1 - is not mounted: mount it (read-only is enough) and give the mount point or the device
```

**What happened:** the disk is not connected — VBACKUP cannot read its files.

**What to do:** connect it read-only and try again:

```
mount -o ro /dev/sdb1 /mnt/photos
vbackup /mnt/photos /mnt/usb/photos.bck /IMAGE
```

### IMGUNSUPP

**What you see:**

```
%VBACKUP-E-IMGUNSUPP, Saveset: /mnt/usb/old.bck, Type: minix - VBACKUP does not make such a file system: use /PHYSICAL for it
```

**What happened:** VBACKUP cannot make a disk of this kind.

**What to do:** save such a disk as in section 9 (`/PHYSICAL`).
Or take just the files out of the box (section 3).

### IMGMKFS

**What you see** (for example):

```
%VBACKUP-E-IMGMKFS, Command: mkfs.xfs -f -q -L PHOTOS /dev/sdc1 - failed: the program is not installed
```

**What happened:** the new disk could not be made. Most often the program is missing.

**What to do:** ask the person who set up your computer to install it (here `mkfs.xfs`).

### IMGSMALL

**What you see:**

```
%VBACKUP-E-IMGSMALL, Device: /dev/sdc1, Bytes: 8388608, Needed: 17596518 - too small, nothing written
```

**What happened:** the files do not fit on this disk.

**What to do:** take a bigger disk.

---

## 13. Help, I do not understand anything

That is all right. Ask the program itself for help.

A short hint:

```
vbackup
```

The full help (next page — **Enter**, quit — **Q**):

```
vbackup /HELP
```

What to do about errors:

```
vbackup /HELP TROUBLESHOOTING
```

The manual (quit — the **q** key):

```
man vbackup
man vbkx
```

And most of all: ask the person who set up your computer. Show them the message.

---

## Little dictionary

- **Folder** — a place where files live. Like a drawer in a cupboard.
- **File** — one letter, one photo, one song.
- **Command** — a line you type in the terminal and send with the Enter key.
- **Terminal** — the window where you type commands.
- **Saveset (box)** — one big file that holds all your files. Its name ends with `.bck` or `.sav`.
- **Volume (piece)** — one part of a box that was cut up: `ivan.bck.002`, `ivan.bck.003`.
- **USB stick** — a small disk you plug into the computer.
- **Journal** — VBACKUP's notebook, where it writes down what is already saved.
- **Block** — a small piece of the box. If one is broken, VBACKUP fixes it by itself.
- **Table of contents (catalog)** — the list of all the files at the end of the box.
- **Partition** — a part of a disk. One disk can be cut into several partitions: `sdb1`, `sdb2`.
- **File system** — the order in which files lie on a disk. It has a kind: ext4, vfat …
- **Mount point** — the folder through which you see a connected disk. For example `/mnt/photos`.
- **Image** — one file that holds a whole disk, piece by piece.
- **UUID** — the long number of a disk, like a passport. A copy has the same one as the original.
- **Qualifier** — a word with `/` in front, like `/LOG`. It tells VBACKUP how to work.
- **Password (passphrase)** — the secret words that lock and open a box. Like the key of a door:
  lose it, and the door stays shut forever.
- **Key file** — a small file with the password on its first line. Only you may read it (`chmod 600`).
- **Locked (encrypted) box** — a box made with `/ENCRYPT`. Without the password nobody can look inside,
  not even at the names of the files.
