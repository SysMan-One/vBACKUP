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

---

## Before you start

1. Open the window for commands. It is called the "terminal".
2. Type the command exactly as it is written here.
3. Press the **Enter** key.

Important rules:

- The name of a box **always** ends with `.bck`. For example: `ivan.bck`.
  If you forget `.bck`, VBACKUP does not make a box. It just copies the folder.
- The spaces between the parts of a command matter. Do not leave them out.
- Big and small letters matter: `/home/ivan` and `/Home/Ivan` are different.
- If nothing is written back, that is good. It means it worked.

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
You can leave them out; then VBACKUP works quietly.

**What you will see:**

```
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-CREATED, /mnt/usb/ivan.bck created
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan/letters saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan/letters/anna.txt saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan/photos saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan/photos/cat.jpg saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan/photos/dog.jpg saved
04-10-2026 12:59:32.288 2278116 %VBACKUP-I-SAVESUMM, 6 files, 50010 bytes saved in 4 blocks and 1 volume
04-10-2026 12:59:32.288 2278116 %VBACKUP-I-VERIFYING, verifying /mnt/usb/ivan.bck
...
04-10-2026 12:59:32.292 2278116 %VBACKUP-I-CMPSUMM, 6 files compared, 0 differences
```

**What it means:**

- The start of each line is the date, the time and a number. You can ignore it.
- `saved` — the file is in the box.
- `6 files ... saved` — 6 things were saved in all.
- `0 differences` — the box was checked, everything matches. Well done!

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
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/letters restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/letters/anna.txt restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/photos restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/photos/cat.jpg restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/photos/dog.jpg restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTSUMM, 6 files, 50010 bytes restored
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

**What you will see:** nothing. That is good. The file is now in `/home/ivan/anna.txt`.

You can also just show the file on the screen, without saving it:

```
vbackup /mnt/usb/ivan.bck /EXTRACT=ivan/letters/anna.txt
```

```
Dear Anna
```

If the name is wrong, you will see:

```
04-10-2026 12:59:49.082 2281596 %VBACKUP-E-NOTFOUND, ivan/letters/nope.txt is not in the saveset
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
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-RECORDED, 3 files recorded in the journal /var/lib/vbackup/vbackup.jnl
```

`/RECORD` means: remember what is saved. VBACKUP keeps a notebook for this — the "journal".

**Step 2. Every day — a small box with only the new things:**

```
vbackup /home/ivan /mnt/usb/mon.bck /SINCE=BACKUP /RECORD
```

```
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-RECORDED, 1 file recorded in the journal /var/lib/vbackup/vbackup.jnl
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
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-DELETED, /home/ivan/restored/ivan/photos/dog.jpg deleted: it is not in the incremental saveset
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-RESTSUMM, 13 files, 50021 bytes restored
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

**What you will see:** nothing. That is good. The folder is now here too: `/mnt/disk2/ivan`.

With `/LOG`, VBACKUP shows every file:

```
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-COPIED, /mnt/disk2/ivan/photos/cat.jpg copied
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-CPYSUMM, 6 files, 20021 bytes copied
```

`copied` — the file is copied.

---

## 7. No VBACKUP here? Use vbkx

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

## 8. If something went wrong

A message looks like this: `%VBACKUP-E-NAME, text`.
The letter after `VBACKUP-` tells you how serious it is:

- `I` — just telling you. All is well.
- `W` — a warning. Look carefully.
- `E` or `F` — an error. Something was not done.

### FILEEXISTS

**What you see:**

```
%VBACKUP-W-FILEEXISTS, /home/ivan/restored/ivan/letters/anna.txt already exists, not restored
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

### OPENOUT … errno=17 (File exists)

**What you see:**

```
%VBACKUP-E-OPENOUT, error creating /mnt/usb/ivan.bck as output, errno=17 (File exists)
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
%VBACKUP-E-NOTSAVESET, /mnt/usb/fake.bck is not a saveset
```

**What happened:** this file is not a VBACKUP box.

**What to do:** check the name. See what is on the USB stick:

```
ls /mnt/usb
```

### NOPARAM

**What you see:**

```
%VBACKUP-E-NOPARAM, missing parameter: input specification - it does not exist
```

or

```
%VBACKUP-E-NOPARAM, missing parameter: output specification
```

**What happened:** in the first case, there is no such file (a typo in the name).
In the second, you asked for `/LIST`, but the file is not a box, so VBACKUP thought
you wanted to save something, and you did not say where to.

**What to do:** check the name with `ls /mnt/usb`.

### IVOP

**What you see:**

```
%VBACKUP-E-IVOP, cannot tell what to do: the input does not exist
```

**What happened:** the first thing you wrote is not there. Most likely a typo.

**What to do:** check the name:

```
ls /home/ivan
```

### BLKFIXED

**What you see:**

```
%VBACKUP-I-BLKFIXED, block 3 of volume 1 was bad and has been rebuilt from its group
```

**What happened:** a small piece of the box was broken, but VBACKUP fixed it by itself.
**All files are fine.**

**What to do:** the USB stick may be starting to fail.
Soon make a new box on another USB stick.

### BLKLOST and FILDAMAGED

**What you see:**

```
%VBACKUP-E-BLKLOST, block 3 of volume 1 is bad and cannot be rebuilt
%VBACKUP-E-BLKLOST, block 4 of volume 1 is bad and cannot be rebuilt
%VBACKUP-E-FILDAMAGED, /home/ivan/restored/ivan/photo1.jpg is incomplete: its data was lost in bad blocks
```

**What happened:** part of the box is broken and could not be fixed.
The files named by `FILDAMAGED` came back incomplete. **All the others are fine.**

**What to do:** take those files from another box, if you have one.
Next time make two boxes on two different USB sticks.

### FILLOST

**What you see:**

```
%VBACKUP-E-FILLOST, /home/ivan/restored/ivan/photo10.jpg was not restored: its records were lost in bad blocks
```

**What happened:** this file was lost with the broken piece. It is not there at all.

**What to do:** take it from another, older box:

```
vbackup /mnt/usb/old.bck /LIST
```

### UNNAMED

**What you see:**

```
%VBACKUP-W-UNNAMED, /mnt/usb/ivan.bck: blocks were lost and it has no catalog - files missing from the restore cannot all be named
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
%VBACKUP-E-MISSVOL, volume 2 of /mnt/usb/ivan.bck is missing
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
%VBACKUP-W-NOTRAILER, /mnt/usb/ivan.bck has no trailer: the save did not complete, or its last volume is missing
```

**What happened:** the save did not finish (the power went off, the disk was full),
or the last piece is missing.

**What to do:** the files up to the break can still be taken out — the usual way.
Then make the box again.

### NOTINCR

**What you see:**

```
%VBACKUP-E-NOTINCR, /mnt/usb/ivan.bck: it has no catalog, nothing restored with /INCREMENTAL
```

**What happened:** `/INCREMENTAL` needs a whole box with a "table of contents". This one has none.

**What to do:** take it out without `/INCREMENTAL`:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

---

## 9. Help, I do not understand anything

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
- **Saveset (box)** — one big file that holds all your files. Its name ends with `.bck`.
- **Volume (piece)** — one part of a box that was cut up: `ivan.bck.002`, `ivan.bck.003`.
- **USB stick** — a small disk you plug into the computer.
- **Journal** — VBACKUP's notebook, where it writes down what is already saved.
- **Block** — a small piece of the box. If one is broken, VBACKUP fixes it by itself.
- **Table of contents (catalog)** — the list of all the files at the end of the box.
