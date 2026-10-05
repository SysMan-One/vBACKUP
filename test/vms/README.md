# test/vms - savesets of OpenVMS BACKUP

Made on OpenVMS Alpha V8.3 (BACKUP V8.3, an ODS-5 disk) on 5-OCT-2026
by `MKVBKS.COM`, from `TEXT.TXT`, `LONG.TXT`, `BIN.DAT` and the FDL
files here, all put there by FTP:

    $ @MKVBKS.COM          ! in SYS$SYSDEVICE:[LAISHEV], with the files above

| File | What |
|---|---|
| `VBKS1.BCK` | `BACKUP [LAISHEV.VBKS...]*.*;* VBKS1.BCK/SAVE_SET/BLOCK_SIZE=8192/GROUP_SIZE=5` |
| `VBKS2.BCK` | the same `/BLOCK_SIZE=8192/NOCRC/GROUP_SIZE=0` |
| `VBKS3.BCK` | the same with the defaults: 32256, a group of 10 |
| `VBKS1.LIS` | `BACKUP/LIST=VBKS1.LIS/FULL VBKS1.BCK/SAVE_SET` |
| `VBKS2.LIS` | `BACKUP/LIST=VBKS2.LIS VBKS2.BCK/SAVE_SET` |
| `TEXT.TXT` | the text all the text files were made of: VAR, VAR non-spanned (`NOSPAN.TXT`, 0xFFFF at the end of each block), STM, STMCR, STMLF, VFC (CONVERT/FDL), and their copies - FTP of VMS gives it back from each |
| `LONG.TXT` | a variable file of records of 4 to 9 KB, as FTP gives it |
| `BIN.DAT` | 3000 random octets: `BIN.DAT` (fixed 512) and `UDF.DAT` (undefined) |
| `FIX80.REF` | `FIX80.TXT` (fixed 80, CONVERT/PAD) as FTP gives it: each record of 80 octets and LF |
| `IDX.REF` | `IDX.DAT` (indexed) in binary: what is restored of it |

The listings and the references came back by FTP; the savesets in
binary mode.  `test/vms.sh` checks VBACKUP and vbkx against them;
`doc/vmsbackup.md` tells the format.
