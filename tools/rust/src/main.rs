/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	tools/rust/src/main.rs
**
**  ABSTRACT:	vbkx-rs - the extractor of last resort for VBACKUP savesets.
**		One file, the Rust standard library only, no crates: for
**		the day everything else is dead.  Simple and plain on
**		purpose - one block at a time, no threads, no mappings, no
**		tricks - so that a human can read it against doc/format.md
**		and fix it.  Speed is not a goal; getting the data out is.
**
**  BUILD:	rustc -O -C strip=symbols -o vbkx-rs src/main.rs   (Rust 1.63+)
**		or: cargo build --release, or: make (the Makefile here).
**
**  USAGE:	vbkx-rs l saveset              list the files
**		vbkx-rs x saveset [-C dir]     extract them all into dir
**						(default: the current directory)
**		vbkx-rs t saveset              read it all, check the checksums
**
**		saveset is volume 1 (x.bck); volumes 2, 3, ... are looked
**		for beside it as x.bck.002, x.bck.003, ...
**
**		  $ vbkx-rs l /mnt/usb/home.bck
**		  $ vbkx-rs x /mnt/usb/home.bck -C /tmp/restore
**		  $ vbkx-rs t /mnt/usb/home.bck
**
**  LISTING:	one line per file, the time in UTC, always (no time zone
**		files are needed):
**
**		  2026-10-03 20:40:12         1234 -0644 tree/a.txt
**		  2026-10-03 20:40:12            5 l0777 tree/link -> a.txt
**		  2026-10-03 20:40:12            6 h0644 tree/hard link to tree/a.txt
**
**		The type letters are those of ls: - d l h(ard link) c b p s.
**		The listing is taken from the FILE records of the stream, so
**		it needs no catalog.
**
**  RESTORED:	data (holes stay holes), mode (with setuid/setgid/sticky),
**		modification and access times, directories, symbolic links,
**		hard links, FIFOs.
**		NOT restored: owners, ACLs, extended attributes, chattr
**		flags, device files, sockets - use vbackup or vbkx for those.
**		A file that is there is never overwritten.  Names with "..",
**		a leading "/" or an empty component are refused, and so is a
**		way through a symbolic link.
**
**  DATA:	DATA records and DATAZ records (vbackup /DATA_FORMAT=COMPRESSED,
**		the LZ4 block format, format.md 6.7) alike; a DATAZ block is
**		decompressed under the same checks as everything else - a
**		length or an offset out of bounds makes it a bad record, and
**		its file is named incomplete.
**
**  VOLUMES:	a saveset of vbackup /PHYSICAL (format.md 6.8) holds one device:
**		it comes out as one sparse file, the image of that device
**		(sdb1); one of vbackup /IMAGE (6.9) comes out as the plain tree
**		of the files of the volume - making the file system again is
**		vbackup's business, not this one's.
**
**  DAMAGE:	every block is checked (CRC-32); one bad block in a group is
**		rebuilt from the group's XOR block; after a loss the stream
**		is picked up at the next good block.  A file that lost data
**		is kept as far as it got and named: "<name> is incomplete".
**		A file whose records were lost entirely is named from the
**		catalog: "<name> was not extracted"; without a readable
**		catalog that is said once ("... cannot all be named").  A
**		missing or cut volume is skipped, the next one is read.  A
**		volume 1 whose first block (VHDR) is bad is still read: the
**		block size is found by trying every legal size against the
**		CRC of the blocks after it.
**
**  GUARANTEED:	no panic and no hang on any input: every length and index
**		taken from the saveset is checked, nothing is unwrapped; the
**		only "unsafe" are the two system calls std of Rust 1.63 does
**		not offer (utimensat, mkfifo), on names already checked.
**		Nothing written outside the output directory; no silent
**		damage - a file that is not named in a message is the file
**		that was saved.
**
**  EXIT:	0 - all done; 1 - something was damaged or not done;
**		2 - the command or the saveset cannot be used.
**
**  AUTHOR:	StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:  4-OCT-2026
**
**  MODIFICATION HISTORY:
**
**	X01-04		 4-OCT-2026	RRL
**		DATAZ: the data compressed in the LZ4 block format.
**		/PHYSICAL and /IMAGE savesets said in the manual above.
**
**	X01-03		 4-OCT-2026	RRL
**		Initial version.
**
**--
*/

use std::collections::HashSet;
use std::ffi::CString;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom};
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::{FileExt, PermissionsExt};
use std::path::{Path, PathBuf};
use std::process::exit;

/* The numbers of doc/format.md */
const HDR: usize = 64; // block header, section 3
const NONE: u32 = 0xFFFF_FFFF; // "no record begins here"
const MAXREC: u64 = 16 << 20; // sanity ceiling of a record body
const MINBSZ: u32 = 8192;
const MAXBSZ: u32 = 1 << 20;
const MAXGRP: u32 = 100;
const MAXVOL: u32 = 9999;
const VOLGAP: u32 = 16; // missing volume names in a row that end the search

const BT_DATA: u8 = 1;
const BT_XOR: u8 = 2;
const BT_VHDR: u8 = 3;
const BT_TRAILER: u8 = 4;

const RT_SUMMARY: u16 = 1;
const RT_FILE: u16 = 2;
const RT_DATA: u16 = 3;
const RT_FEND: u16 = 4;
const RT_CATALOG: u16 = 5;
const RT_END: u16 = 6;
const RT_DATAZ: u16 = 7; // DATA, compressed: format.md 6.7

const MAXDATA: u32 = 1 << 20; // the most octets a DATA or DATAZ record holds
const CODEC_LZ4: u32 = 1;

const FT_REG: u8 = 1;
const FT_DIR: u8 = 2;
const FT_SYMLINK: u8 = 3;
const FT_HARDLINK: u8 = 4;
const FT_FIFO: u8 = 7;

const FS_CHANGED: u8 = 1;
const FS_READERR: u8 = 2;
const FS_PRESENT: u8 = 3;

macro_rules! msg {
    ($($a:tt)*) => { eprintln!("vbkx-rs: {}", format!($($a)*)) };
}

/* CRC-32/IEEE, section 1: reflected 0xEDB88320, init and final XOR 0xFFFFFFFF */
struct Crc {
    table: [u32; 256],
}

impl Crc {
    fn new() -> Crc {
        let mut table = [0u32; 256];
        for (i, t) in table.iter_mut().enumerate() {
            let mut c = i as u32;
            for _ in 0..8 {
                c = if c & 1 != 0 { 0xEDB8_8320 ^ (c >> 1) } else { c >> 1 };
            }
            *t = c;
        }
        Crc { table }
    }

    /* Chained as zlib's crc32(crc, buf): update(0, b) is the CRC of b */
    fn update(&self, crc: u32, buf: &[u8]) -> u32 {
        let mut c = !crc;
        for &b in buf {
            c = self.table[((c ^ b as u32) & 0xFF) as usize] ^ (c >> 8);
        }
        !c
    }
}

fn u16_at(b: &[u8], off: usize) -> u16 {
    match b.get(off..off.saturating_add(2)) {
        Some(s) => u16::from_le_bytes([s[0], s[1]]),
        None => 0,
    }
}

fn u32_at(b: &[u8], off: usize) -> u32 {
    match b.get(off..off.saturating_add(4)) {
        Some(s) => u32::from_le_bytes([s[0], s[1], s[2], s[3]]),
        None => 0,
    }
}

fn u64_at(b: &[u8], off: usize) -> u64 {
    match b.get(off..off.saturating_add(8)) {
        Some(s) => u64::from_le_bytes([s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]]),
        None => 0,
    }
}

/*
** lz4_decompress - the LZ4 block format (format.md 6.7): exactly rawlen
** octets out of exactly src, or None.  Every length is checked against
** what is left of the input and of the output, every offset against what
** has been written; nothing is indexed without a check.
 */
fn lz4_decompress(src: &[u8], rawlen: u32) -> Option<Vec<u8>> {
    let raw = rawlen as usize;
    let mut dst: Vec<u8> = Vec::with_capacity(raw);
    let mut ip = 0usize;

    /* A length beyond the nibble: bytes added until one is below 255, never past rawlen */
    fn getlen(src: &[u8], ip: &mut usize, mut v: usize, raw: usize) -> Option<usize> {
        loop {
            let b = *src.get(*ip)?;
            *ip += 1;
            v += b as usize;
            if v > raw {
                return None;
            }
            if b != 255 {
                return Some(v);
            }
        }
    }

    loop {
        let tok = *src.get(ip)?;
        ip += 1;
        let mut lit = (tok >> 4) as usize;
        if lit == 15 {
            lit = getlen(src, &mut ip, lit, raw)?;
        }
        if lit > src.len() - ip || lit > raw - dst.len() {
            return None;
        }
        dst.extend_from_slice(src.get(ip..ip + lit)?);
        ip += lit;
        if ip == src.len() {
            break; // the last sequence has its literals only
        }
        if src.len() - ip < 2 {
            return None;
        }
        let off = (*src.get(ip)? as usize) | ((*src.get(ip + 1)? as usize) << 8);
        ip += 2;
        if off == 0 || off > dst.len() {
            return None;
        }
        let mut ml = (tok & 15) as usize;
        if ml == 15 {
            ml = getlen(src, &mut ip, ml, raw)?;
        }
        ml += 4;
        if ml > raw - dst.len() {
            return None;
        }
        for _ in 0..ml {
            /* octet by octet: a match may overlap what it makes */
            let b = *dst.get(dst.len() - off)?;
            dst.push(b);
        }
    }
    if dst.len() != raw {
        return None;
    }
    Some(dst)
}

/*
** data_view - the file, the offset and the octets of a DATA or DATAZ
** record; None - a bad record
 */
fn data_view(typ: u16, body: &[u8]) -> Option<(u32, u64, std::borrow::Cow<'_, [u8]>)> {
    if typ == RT_DATA {
        let d = body.get(16..)?;
        return Some((u32_at(body, 0), u64_at(body, 8), std::borrow::Cow::Borrowed(d)));
    }
    let z = body.get(20..)?;
    let rawlen = u32_at(body, 16);
    if u32_at(body, 4) != CODEC_LZ4 || rawlen > MAXDATA {
        return None;
    }
    let d = lz4_decompress(z, rawlen)?;
    Some((u32_at(body, 0), u64_at(body, 8), std::borrow::Cow::Owned(d)))
}

/* The block header, format.md section 3 */
#[derive(Clone, Copy, Default)]
struct Bhdr {
    typ: u8,
    gindex: u16,
    uuid: [u8; 16],
    blkno: u64,
    volno: u32,
    recoff: u32,
    paylen: u32,
    prvrecoff: u32,
    prvpaylen: u32,
}

/*
** Decode and check a block: magic, header length, version, size, saveset
** (uuid None - any), type, lengths, and the CRC of the header (its CRC
** field taken as 0) followed by the whole payload area.
*/
fn check(crc: &Crc, b: &[u8], bsize: u32, uuid: Option<&[u8; 16]>) -> Option<Bhdr> {
    if b.len() != bsize as usize || b.len() < HDR || &b[0..4] != b"VBKB" || u16_at(b, 4) != HDR as u16 || u16_at(b, 6) != 1 {
        return None;
    }
    let mut h = Bhdr {
        typ: b[12],
        gindex: u16_at(b, 14),
        blkno: u64_at(b, 32),
        volno: u32_at(b, 40),
        recoff: u32_at(b, 44),
        paylen: u32_at(b, 48),
        prvrecoff: u32_at(b, 52),
        prvpaylen: u32_at(b, 56),
        ..Default::default()
    };
    h.uuid.copy_from_slice(&b[16..32]);
    let psize = bsize - HDR as u32;
    if u32_at(b, 8) != bsize || h.typ < BT_DATA || h.typ > BT_TRAILER || h.paylen > psize || (h.recoff != NONE && h.recoff >= psize) {
        return None;
    }
    if let Some(u) = uuid {
        if &h.uuid != u {
            return None;
        }
    }
    let mut hdr = [0u8; HDR];
    hdr.copy_from_slice(&b[..HDR]);
    hdr[60..64].copy_from_slice(&[0, 0, 0, 0]);
    let c = crc.update(crc.update(0, &hdr), &b[HDR..]);
    if c == u32_at(b, 60) {
        Some(h)
    } else {
        None
    }
}

/* TLV items, section 6: u16 tag, u32 length, value; false - the body is cut */
fn tlv_each(body: &[u8], mut f: impl FnMut(u16, &[u8])) -> bool {
    let mut pos = 0usize;
    while pos < body.len() {
        if body.len() - pos < 6 {
            return false;
        }
        let tag = u16_at(body, pos);
        let vlen = u32_at(body, pos + 2) as usize;
        match body.get(pos + 6..(pos + 6).saturating_add(vlen)) {
            Some(v) => f(tag, v),
            None => return false,
        }
        pos += 6 + vlen;
    }
    true
}

fn getu(v: &[u8]) -> u64 {
    match v.len() {
        1 => v[0] as u64,
        2 => u16_at(v, 0) as u64,
        4 => u32_at(v, 0) as u64,
        8 => u64_at(v, 0),
        _ => 0,
    }
}

#[derive(Clone, Copy, Default)]
struct Ftime {
    sec: i64,
    nsec: i64,
}

fn gettime(v: &[u8]) -> Ftime {
    if v.len() != 12 {
        return Ftime::default();
    }
    let nsec = u32_at(v, 8) as i64;
    Ftime { sec: u64_at(v, 0) as i64, nsec: if nsec > 999_999_999 { 0 } else { nsec } }
}

/* One file as a FILE record or a catalog entry describes it, section 6.1 */
#[derive(Clone, Default)]
struct Entry {
    fileno: u32,
    path: Vec<u8>,
    link: Vec<u8>,
    ftype: u8,
    mode: u32,
    size: u64,
    mtime: Ftime,
    atime: Ftime,
    status: u8,
}

fn parse_entry(body: &[u8]) -> Option<Entry> {
    let mut e = Entry::default();
    let mut has_path = false;
    let ok = tlv_each(body, |tag, v| match tag {
        1 => e.fileno = getu(v) as u32,
        2 => {
            e.path = v.to_vec();
            has_path = true
        }
        3 => e.ftype = getu(v) as u8,
        4 => e.mode = getu(v) as u32,
        9 => e.size = getu(v),
        10 => e.mtime = gettime(v),
        11 => e.atime = gettime(v),
        15 => e.link = v.to_vec(),
        33 => e.status = getu(v) as u8,
        _ => {}
    });
    if e.atime.sec == 0 && e.atime.nsec == 0 {
        e.atime = e.mtime;
    }
    if ok && has_path && !e.path.is_empty() && e.ftype != 0 {
        Some(e)
    } else {
        None
    }
}

fn shown(b: &[u8]) -> String {
    String::from_utf8_lossy(b).into_owned()
}

/* A volume of the saveset; f None - it is missing */
struct Volume {
    f: Option<File>,
    firstblk: u64, // blkno of its first block
    nblk: u64,     // whole blocks in it
}

/* Read a block; what cannot be read is zeros - such a block fails its check */
fn read_block(f: Option<&File>, bsize: u32, pos: u64) -> Vec<u8> {
    let mut buf = vec![0u8; bsize as usize];
    if let Some(f) = f {
        let mut got = 0usize;
        while got < buf.len() {
            match f.read_at(&mut buf[got..], pos.saturating_mul(bsize as u64).saturating_add(got as u64)) {
                Ok(0) => break,
                Ok(n) => got += n,
                Err(ref e) if e.kind() == io::ErrorKind::Interrupted => continue,
                Err(_) => break,
            }
        }
        for b in buf.iter_mut().skip(got) {
            *b = 0;
        }
    }
    buf
}

fn blocks_in(f: &File, bsize: u32) -> u64 {
    match f.metadata() {
        Ok(m) => m.len() / bsize as u64,
        Err(_) => 0,
    }
}

struct Reader {
    crc: Crc,
    spec: String,
    bsize: u32,
    grpsz: u32,
    uuid: [u8; 16],
    vols: Vec<Volume>,
    trailer: bool,

    curvol: usize, // where the next group is read from
    curpos: u64,   // ... block position in that volume
    pays: Vec<Option<Vec<u8>>>,
    recoffs: Vec<u32>,
    blks: Vec<u64>,
    next: usize,

    gap: bool, // a block has been lost since the last record
    pay: Option<Vec<u8>>,
    payoff: usize,
    payblk: u64,
    payvol: usize,
    resync: bool, // the record returned follows a loss
    bad: bool,    // something was damaged or not done: completion 1
}

fn volspec(spec: &str, n: u32) -> String {
    if n <= 1 {
        spec.to_string()
    } else {
        format!("{}.{:03}", spec, n)
    }
}

/* The group size, out of the SUMMARY record a VHDR carries */
fn summary_group(blk: &[u8], h: &Bhdr) -> Option<u32> {
    let pay = blk.get(HDR..HDR + h.paylen as usize)?;
    if pay.len() < 8 || u16_at(pay, 0) != RT_SUMMARY {
        return None;
    }
    let body = pay.get(8..8usize.saturating_add(u32_at(pay, 4) as usize))?;
    let mut grp = 0u32;
    tlv_each(body, |tag, v| {
        if tag == 71 {
            grp = getu(v) as u32
        }
    });
    if grp <= MAXGRP {
        Some(grp)
    } else {
        None
    }
}

impl Reader {
    fn open(spec: &str) -> Result<Reader, String> {
        let mut f = File::open(spec).map_err(|e| format!("{}: {}", spec, e))?;
        let mut r = Reader {
            crc: Crc::new(),
            spec: spec.to_string(),
            bsize: 0,
            grpsz: 0,
            uuid: [0; 16],
            vols: Vec::new(),
            trailer: false,
            curvol: 1,
            curpos: 1,
            pays: Vec::new(),
            recoffs: Vec::new(),
            blks: Vec::new(),
            next: 0,
            gap: false,
            pay: None,
            payoff: 0,
            payblk: 0,
            payvol: 0,
            resync: false,
            bad: false,
        };
        /* The block size comes from the first header; it is believed only when the block checks */
        let mut head = [0u8; HDR];
        let _ = f.seek(SeekFrom::Start(0)).and_then(|_| f.read(&mut head));
        let mut found = false;
        let bs = u32_at(&head, 8);
        if &head[0..4] == b"VBKB" && (MINBSZ..=MAXBSZ).contains(&bs) && bs % 512 == 0 {
            let blk = read_block(Some(&f), bs, 0);
            if let Some(h) = check(&r.crc, &blk, bs, None) {
                if h.typ == BT_VHDR && h.volno == 1 {
                    if let Some(g) = summary_group(&blk, &h) {
                        r.bsize = bs;
                        r.uuid = h.uuid;
                        r.grpsz = g;
                        found = true;
                    }
                }
            }
        }
        if !found && !r.guess(&f) {
            return Err(format!("{} is not a saveset", spec));
        }
        let n1 = blocks_in(&f, r.bsize);
        r.vols.push(Volume { f: Some(f), firstblk: 0, nblk: n1 });

        /* The further volumes; a missing name does not end the search at once */
        let mut miss = 0;
        let mut n = 2;
        while n <= MAXVOL && miss < VOLGAP {
            let vf = match File::open(volspec(spec, n)) {
                Ok(vf) => vf,
                Err(_) => {
                    miss += 1;
                    n += 1;
                    continue;
                }
            };
            /* Its VHDR, or else its next block, tells where it begins */
            let first;
            match check(&r.crc, &read_block(Some(&vf), r.bsize, 0), r.bsize, Some(&r.uuid)) {
                Some(h) if h.typ == BT_VHDR && h.volno == n => first = Some(h.blkno),
                _ => match check(&r.crc, &read_block(Some(&vf), r.bsize, 1), r.bsize, Some(&r.uuid)) {
                    Some(h) if h.volno == n && h.blkno > 0 => {
                        msg!("volume {}: its first block is bad, it is read all the same", n);
                        first = Some(h.blkno - 1)
                    }
                    _ => first = None,
                },
            }
            match first {
                Some(first) => {
                    while r.vols.len() < (n - 1) as usize {
                        r.vols.push(Volume { f: None, firstblk: 0, nblk: 0 });
                    }
                    let nb = blocks_in(&vf, r.bsize);
                    r.vols.push(Volume { f: Some(vf), firstblk: first, nblk: nb });
                    miss = 0;
                }
                None => {
                    msg!("volume {} belongs to another saveset, or is none", n);
                    miss += 1;
                }
            }
            n += 1;
        }

        /* The TRAILER, last block of the last volume: it is not part of the groups */
        if let Some(lv) = r.vols.last() {
            if lv.nblk > 1 {
                let b = read_block(lv.f.as_ref(), r.bsize, lv.nblk - 1);
                if let Some(h) = check(&r.crc, &b, r.bsize, Some(&r.uuid)) {
                    r.trailer = h.typ == BT_TRAILER;
                }
            }
        }
        Ok(r)
    }

    /*
    ** Volume 1 without a good VHDR: try every legal block size against the
    ** blocks after the first one; the first that checks gives the size and
    ** the saveset.  The group size is then where the first XOR block stands.
    */
    fn guess(&mut self, f: &File) -> bool {
        let mut bs = MINBSZ;
        while bs <= MAXBSZ {
            for pos in 1..=8u64 {
                let h = match check(&self.crc, &read_block(Some(f), bs, pos), bs, None) {
                    Some(h) if h.volno == 1 && h.blkno == pos => h,
                    _ => continue,
                };
                self.bsize = bs;
                self.uuid = h.uuid;
                self.grpsz = 0;
                for p in 1..=(MAXGRP as u64 + 1) {
                    if let Some(x) = check(&self.crc, &read_block(Some(f), bs, p), bs, Some(&self.uuid)) {
                        if x.typ == BT_XOR && x.gindex as u64 == p - 1 {
                            self.grpsz = x.gindex as u32;
                            break;
                        }
                    }
                }
                msg!("{}: the first block is bad; block size {} and group size {} found by trying", self.spec, bs, self.grpsz);
                return true;
            }
            bs += 512;
        }
        false
    }

    fn volend(&self, n: usize) -> u64 {
        let v = &self.vols[n - 1];
        if self.trailer && n == self.vols.len() && v.nblk > 0 {
            v.nblk - 1
        } else {
            v.nblk
        }
    }

    /*
    ** Read the next group (section 4), check its blocks, rebuild one bad
    ** DATA block from the XOR block; false - the end of the saveset.
    */
    fn load_group(&mut self) -> bool {
        let mut end;
        loop {
            if self.curvol > self.vols.len() {
                return false;
            }
            if self.vols[self.curvol - 1].f.is_none() {
                msg!("volume {} is missing", self.curvol);
                self.bad = true;
                self.gap = true;
                self.curvol += 1;
                self.curpos = 1;
                continue;
            }
            end = self.volend(self.curvol);
            if self.curpos >= end {
                self.curvol += 1;
                self.curpos = 1;
                continue;
            }
            break;
        }
        let firstblk = self.vols[self.curvol - 1].firstblk;
        let mut n: u64 = if self.grpsz > 0 { self.grpsz as u64 + 1 } else { 1 };
        if end - self.curpos < n {
            n = end - self.curpos;
        }
        let n = n as usize;
        let mut blks: Vec<Vec<u8>> = Vec::with_capacity(n);
        let mut hdrs: Vec<Bhdr> = Vec::with_capacity(n);
        let mut ok: Vec<bool> = Vec::with_capacity(n);
        for i in 0..n {
            let pos = self.curpos + i as u64;
            let b = read_block(self.vols[self.curvol - 1].f.as_ref(), self.bsize, pos);
            match check(&self.crc, &b, self.bsize, Some(&self.uuid)) {
                Some(h) => {
                    ok.push(h.blkno == firstblk.wrapping_add(pos) && h.volno as usize == self.curvol);
                    hdrs.push(h);
                }
                None => {
                    ok.push(false);
                    hdrs.push(Bhdr::default());
                }
            }
            blks.push(b);
        }

        /* Where the XOR block is: a full group ends with it; a short one, cut short, may not have one */
        let mut has_xor = 0usize;
        let xi = n - 1;
        if self.grpsz > 0 {
            if n == self.grpsz as usize + 1 {
                has_xor = 1;
            } else if ok[xi] {
                if hdrs[xi].typ == BT_XOR {
                    has_xor = 1;
                }
            } else if n >= 2 {
                has_xor = 1;
            }
            if has_xor == 1 && ok[xi] && hdrs[xi].typ != BT_XOR {
                ok[xi] = false;
            }
        }
        let gdata = n - has_xor;
        let (mut nbad, mut badi) = (0, 0);
        for i in 0..gdata {
            if ok[i] && hdrs[i].typ != BT_DATA {
                ok[i] = false;
            }
            if !ok[i] {
                nbad += 1;
                badi = i;
            }
        }

        /* One bad DATA block: its payload is the XOR of all the others, two header fields kept by the next block */
        if nbad == 1 && has_xor == 1 && ok[xi] && hdrs[xi].gindex as usize == gdata && badi + 1 < n {
            let mut d = blks[xi][HDR..].to_vec();
            for (i, b) in blks.iter().enumerate().take(gdata) {
                if i == badi {
                    continue;
                }
                for (x, y) in d.iter_mut().zip(b[HDR..].iter()) {
                    *x ^= *y;
                }
            }
            let s = hdrs[badi + 1];
            let h = Bhdr { typ: BT_DATA, recoff: s.prvrecoff, paylen: s.prvpaylen, blkno: firstblk.wrapping_add(self.curpos + badi as u64), ..Default::default() };
            if h.paylen <= self.bsize - HDR as u32 && (h.recoff == NONE || h.recoff < h.paylen) {
                blks[badi][HDR..].copy_from_slice(&d);
                hdrs[badi] = h;
                ok[badi] = true;
                msg!("block {} of volume {} was bad and has been repaired", h.blkno, self.curvol);
            }
        }

        self.pays.clear();
        self.recoffs.clear();
        self.blks.clear();
        for i in 0..gdata {
            let b = firstblk.wrapping_add(self.curpos + i as u64);
            if !ok[i] {
                msg!("block {} of volume {} is bad and cannot be repaired", b, self.curvol);
                self.bad = true;
                self.pays.push(None);
            } else {
                self.pays.push(blks[i].get(HDR..HDR + hdrs[i].paylen as usize).map(|p| p.to_vec()));
            }
            self.recoffs.push(hdrs[i].recoff);
            self.blks.push(b);
        }
        self.next = 0;
        self.curpos += n as u64;
        true
    }

    /*
    ** Make the payload of the next good DATA block the current one.
    ** 0 - the stream goes on in it; 1 - blocks were lost before it, it is
    ** taken from its first record header (RECOFF); 2 - the end.
    */
    fn next_pay(&mut self) -> u8 {
        loop {
            if self.next >= self.pays.len() {
                if !self.load_group() {
                    return 2;
                }
                continue;
            }
            let i = self.next;
            self.next += 1;
            let p = match self.pays[i].take() {
                Some(p) => p,
                None => {
                    self.gap = true;
                    continue;
                }
            };
            let plen = p.len();
            self.pay = Some(p);
            self.payoff = 0;
            self.payblk = self.blks[i];
            self.payvol = self.curvol;
            if !self.gap {
                return 0;
            }
            /* After a loss: a block in which no record begins is of no use */
            if self.recoffs[i] == NONE || self.recoffs[i] as usize >= plen {
                continue;
            }
            self.payoff = self.recoffs[i] as usize;
            self.gap = false;
            return 1;
        }
    }

    fn paylen(&self) -> usize {
        self.pay.as_ref().map_or(0, |p| p.len())
    }

    /* The next record of the stream (section 5); None - the end.  RESYNC: blocks were lost before it. */
    fn next_record(&mut self) -> Option<(u16, Vec<u8>)> {
        let mut resync = false;
        loop {
            while self.pay.is_none() || self.payoff >= self.paylen() {
                match self.next_pay() {
                    1 => resync = true,
                    2 => {
                        self.resync = resync;
                        return None;
                    }
                    _ => {}
                }
            }
            /* A record header is never split over two blocks */
            let (mut typ, mut length) = (0u16, MAXREC + 1);
            if let Some(p) = &self.pay {
                if p.len() - self.payoff >= 8 {
                    typ = u16_at(p, self.payoff);
                    length = u32_at(p, self.payoff + 4) as u64;
                }
            }
            if typ == 0 || length > MAXREC {
                msg!("a bad record in block {} of volume {}", self.payblk, self.payvol);
                self.bad = true;
                self.gap = true;
                self.pay = None;
                continue;
            }
            self.payoff += 8;
            let length = length as usize;
            let mut body: Vec<u8> = Vec::with_capacity(length);
            let mut st = 0u8;
            while body.len() < length {
                if self.payoff >= self.paylen() {
                    st = self.next_pay();
                    if st != 0 {
                        break;
                    }
                }
                if let Some(p) = &self.pay {
                    let n = (p.len() - self.payoff).min(length - body.len());
                    body.extend_from_slice(&p[self.payoff..self.payoff + n]);
                    self.payoff += n;
                }
            }
            if st == 1 {
                /* Lost in the middle of the body: dropped, a new header is at hand */
                resync = true;
                continue;
            }
            if st == 2 {
                msg!("the saveset ends inside a record");
                self.bad = true;
                self.resync = true;
                return None;
            }
            self.resync = resync;
            return Some((typ, body));
        }
    }
}

/* A stored name that is safe to use: relative, no "", "." or ".." component, no NUL */
fn name_ok(name: &[u8]) -> bool {
    if name.is_empty() || name[0] == b'/' || name.contains(&0) {
        return false;
    }
    name.split(|&c| c == b'/').all(|c| !c.is_empty() && c != b"." && c != b"..")
}

fn join(out: &Path, name: &[u8]) -> PathBuf {
    out.join(std::ffi::OsStr::from_bytes(name))
}

/* The directories a name lies in, below OUT, made if CREATE; a symbolic link on the way is refused */
fn parents(out: &Path, name: &[u8], create: bool) -> Result<(), String> {
    let comps: Vec<&[u8]> = name.split(|&c| c == b'/').collect();
    let mut p = out.to_path_buf();
    for c in comps.iter().take(comps.len().saturating_sub(1)) {
        p.push(std::ffi::OsStr::from_bytes(c));
        match fs::symlink_metadata(&p) {
            Ok(m) => {
                if !m.is_dir() {
                    return Err("a directory on the way is a link, or no directory".to_string());
                }
            }
            Err(e) => {
                if !create {
                    return Err(e.to_string());
                }
                fs::create_dir(&p).map_err(|e| e.to_string())?;
                let _ = fs::set_permissions(&p, fs::Permissions::from_mode(0o700));
            }
        }
    }
    Ok(())
}

/* The two system calls std of Rust 1.63 does not offer, on names already checked */
mod sys {
    #[repr(C)]
    pub struct Timespec {
        pub sec: i64,
        pub nsec: i64,
    }
    extern "C" {
        fn utimensat(dirfd: i32, path: *const std::os::raw::c_char, times: *const Timespec, flags: i32) -> i32;
        fn mkfifo(path: *const std::os::raw::c_char, mode: u32) -> i32;
    }
    pub fn set_times(path: &std::ffi::CStr, atime: (i64, i64), mtime: (i64, i64), nofollow: bool) -> bool {
        let ts = [Timespec { sec: atime.0, nsec: atime.1 }, Timespec { sec: mtime.0, nsec: mtime.1 }];
        /* AT_FDCWD -100, AT_SYMLINK_NOFOLLOW 0x100: the same on every Linux */
        unsafe { utimensat(-100, path.as_ptr(), ts.as_ptr(), if nofollow { 0x100 } else { 0 }) == 0 }
    }
    pub fn make_fifo(path: &std::ffi::CStr) -> bool {
        unsafe { mkfifo(path.as_ptr(), 0o600) == 0 }
    }
}

fn set_times(path: &Path, e: &Entry, nofollow: bool) {
    if let Ok(c) = CString::new(path.as_os_str().as_bytes()) {
        sys::set_times(&c, (e.atime.sec, e.atime.nsec), (e.mtime.sec, e.mtime.nsec), nofollow);
    }
}

fn set_mode(path: &Path, mode: u32) {
    let _ = fs::set_permissions(path, fs::Permissions::from_mode(mode & 0o7777));
}

/* The files of the stream being put back (x), or only checked (t) */
struct Extractor {
    out: PathBuf,
    make: bool,
    e: Entry,
    f: Option<File>,
    path: PathBuf,
    active: bool,
    crc: u32,
    damaged: bool,
    seen: HashSet<u32>,
    dirs: Vec<Entry>,
    cat_seen: bool,
    cat_hole: bool,
}

impl Extractor {
    fn new(out: &str, make: bool) -> Extractor {
        Extractor {
            out: PathBuf::from(out),
            make,
            e: Entry::default(),
            f: None,
            path: PathBuf::new(),
            active: false,
            crc: 0,
            damaged: false,
            seen: HashSet::new(),
            dirs: Vec::new(),
            cat_seen: false,
            cat_hole: false,
        }
    }

    fn begin(&mut self, r: &mut Reader, body: &[u8]) {
        self.active = false;
        self.f = None;
        self.crc = 0;
        self.damaged = false;
        let e = match parse_entry(body) {
            Some(e) => e,
            None => {
                msg!("a FILE record that makes no sense is skipped");
                r.bad = true;
                return;
            }
        };
        self.seen.insert(e.fileno);
        self.e = e.clone();
        let name = shown(&e.path);
        if !self.make {
            self.active = e.ftype == FT_REG;
            return;
        }
        if !name_ok(&e.path) {
            msg!("{} was not extracted: a name that leads out of the output directory", name);
            r.bad = true;
            return;
        }
        if let Err(err) = parents(&self.out, &e.path, true) {
            msg!("{} was not extracted: {}", name, err);
            r.bad = true;
            return;
        }
        let path = join(&self.out, &e.path);
        if e.ftype == FT_DIR {
            if let Err(err) = fs::create_dir(&path) {
                if err.kind() != io::ErrorKind::AlreadyExists {
                    msg!("{} was not extracted: {}", name, err);
                    r.bad = true;
                    return;
                }
            }
            self.dirs.push(e);
            return;
        }
        if fs::symlink_metadata(&path).is_ok() {
            msg!("{} was not extracted: it exists, and is never overwritten", name);
            r.bad = true;
            return;
        }
        let res: Result<(), String> = match e.ftype {
            FT_REG => match OpenOptions::new().write(true).create_new(true).open(&path) {
                Ok(f) => {
                    self.f = Some(f);
                    self.active = true;
                    self.path = path.clone();
                    Ok(())
                }
                Err(err) => Err(err.to_string()),
            },
            FT_SYMLINK => match std::os::unix::fs::symlink(std::ffi::OsStr::from_bytes(&e.link), &path) {
                Ok(()) => {
                    set_times(&path, &e, true);
                    Ok(())
                }
                Err(err) => Err(err.to_string()),
            },
            FT_HARDLINK => {
                if !name_ok(&e.link) {
                    Err("a link that leads out of the output directory".to_string())
                } else {
                    parents(&self.out, &e.link, false).and_then(|_| fs::hard_link(join(&self.out, &e.link), &path).map_err(|err| err.to_string()))
                }
            }
            FT_FIFO => match CString::new(path.as_os_str().as_bytes()) {
                Ok(c) if sys::make_fifo(&c) => {
                    set_mode(&path, e.mode);
                    set_times(&path, &e, false);
                    Ok(())
                }
                _ => Err("cannot be made".to_string()),
            },
            _ => Ok(()), // devices and sockets are not made
        };
        if let Err(err) = res {
            msg!("{} was not extracted: {}", name, err);
            r.bad = true;
        }
    }

    /*
    ** A DATA record (u32 fileno, u32 0, u64 offset, the bytes) or a DATAZ one
    ** (u32 fileno, u32 codec, u64 offset, u32 rawlen, LZ4 block); a DATAZ
    ** that does not decompress leaves the file incomplete
     */
    fn data(&mut self, r: &Reader, typ: u16, body: &[u8]) {
        if !self.active {
            return;
        }
        let (fileno, off, view) = match data_view(typ, body) {
            Some(v) => v,
            None => {
                msg!("{}: a data record that makes no sense", shown(&self.e.path));
                self.damaged = true;
                return;
            }
        };
        if fileno != self.e.fileno {
            return;
        }
        let d: &[u8] = &view;
        self.crc = r.crc.update(self.crc, d);
        if let Some(f) = &self.f {
            if off > 1 << 62 {
                self.damaged = true;
            } else if let Err(err) = f.write_all_at(d, off) {
                msg!("{}: {}", shown(&self.e.path), err);
                self.damaged = true;
            }
        }
    }

    /* The end of a regular file: size, checksum, attributes; body None - it ends without its FEND */
    fn end(&mut self, r: &mut Reader, body: Option<&[u8]>) {
        if !self.active {
            return;
        }
        self.active = false;
        let (mut fileno, mut crc, mut status, mut size, mut has_crc) = (0u32, 0u32, 0u8, self.e.size, false);
        if let Some(b) = body {
            tlv_each(b, |tag, v| match tag {
                1 => fileno = getu(v) as u32,
                9 => size = getu(v),
                32 => {
                    crc = getu(v) as u32;
                    has_crc = true
                }
                33 => status = getu(v) as u8,
                _ => {}
            });
        }
        let name = shown(&self.e.path);
        if body.is_none() || fileno != self.e.fileno {
            self.damaged = true;
        } else if has_crc && crc != self.crc {
            msg!("{}: the checksum does not match", name);
            self.damaged = true;
        }
        if self.damaged {
            msg!("{} is incomplete: its data was lost in bad blocks", name);
            r.bad = true;
        } else if status == FS_CHANGED {
            msg!("{} changed while it was saved: the copy may be a mix", name);
        } else if status == FS_READERR {
            msg!("{} could not be read whole when it was saved", name);
        }
        if let Some(f) = self.f.take() {
            if size <= 1 << 62 {
                let _ = f.set_len(size);
            }
            if let Err(err) = f.sync_all() {
                msg!("{}: {}", name, err);
                r.bad = true;
            }
            drop(f);
            set_mode(&self.path, self.e.mode);
            let e = self.e.clone();
            set_times(&self.path, &e, false);
        }
    }

    /* A CATALOG record (section 6.4): the files never met in the stream are named */
    fn catalog(&mut self, r: &mut Reader, body: &[u8]) {
        self.cat_seen = true;
        let mut off = 0usize;
        while off + 4 <= body.len() {
            let elen = u32_at(body, off) as usize;
            let ent = match body.get(off + 4..(off + 4).saturating_add(elen)) {
                Some(s) => s,
                None => {
                    self.cat_hole = true;
                    break;
                }
            };
            if let Some(e) = parse_entry(ent) {
                if e.status != FS_PRESENT && !self.seen.contains(&e.fileno) {
                    msg!("{} was not extracted: its records were lost in bad blocks", shown(&e.path));
                    r.bad = true;
                }
            }
            off += 4 + elen;
        }
    }

    /* The modes and times of the directories, deepest first: a file made in one changes its times */
    fn finish_dirs(&self) {
        for e in self.dirs.iter().rev() {
            if parents(&self.out, &e.path, false).is_err() {
                continue;
            }
            let path = join(&self.out, &e.path);
            match fs::symlink_metadata(&path) {
                Ok(m) if m.is_dir() => {
                    set_mode(&path, e.mode);
                    set_times(&path, e, false);
                }
                _ => {}
            }
        }
    }
}

fn run(r: &mut Reader, x: &mut Extractor) {
    let mut ended = false;
    while !ended {
        let rec = r.next_record();
        if r.resync {
            if x.active {
                x.damaged = true;
            }
            if x.cat_seen {
                x.cat_hole = true;
            }
        }
        let (typ, body) = match rec {
            Some(rec) => rec,
            None => break,
        };
        match typ {
            RT_FILE => {
                if x.active {
                    x.end(r, None);
                }
                x.begin(r, &body);
            }
            RT_DATA | RT_DATAZ => x.data(r, typ, &body),
            RT_FEND => x.end(r, Some(&body)),
            RT_CATALOG => {
                if x.active {
                    x.end(r, None);
                }
                x.catalog(r, &body);
            }
            RT_END => ended = true,
            _ => {}
        }
    }
    if x.active {
        x.end(r, None);
    }
    /* Blocks were lost, and the catalog could not tell every name */
    if r.bad && (!x.cat_seen || x.cat_hole || !ended) {
        msg!("{}: files missing from the output cannot all be named", r.spec);
    }
}

/* Seconds since 1970 to a UTC calendar date (the civil-from-days algorithm), no time zone needed */
fn utc(sec: i64) -> String {
    let days = sec.div_euclid(86400);
    let s = sec.rem_euclid(86400);
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + if m <= 2 { 1 } else { 0 };
    format!("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", y, m, d, s / 3600, s / 60 % 60, s % 60)
}

fn list(r: &mut Reader) {
    while let Some((typ, body)) = r.next_record() {
        if typ == RT_CATALOG || typ == RT_END {
            return;
        }
        if typ != RT_FILE {
            continue;
        }
        let e = match parse_entry(&body) {
            Some(e) => e,
            None => continue,
        };
        let t = b"?-dlhcbps".get(e.ftype as usize).copied().unwrap_or(b'?') as char;
        let mut line = format!("{} {:12} {}{:04o} {}", utc(e.mtime.sec), e.size, t, e.mode & 0o7777, shown(&e.path));
        if e.ftype == FT_SYMLINK {
            line.push_str(&format!(" -> {}", shown(&e.link)));
        } else if e.ftype == FT_HARDLINK {
            line.push_str(&format!(" link to {}", shown(&e.link)));
        }
        println!("{}", line);
    }
}

fn usage() -> i32 {
    eprintln!(
        "vbkx-rs X01-04 - the extractor of last resort for VBACKUP savesets\n\n  \
         vbkx-rs l saveset              list the files (times in UTC)\n  \
         vbkx-rs x saveset [-C dir]     extract them all\n  \
         vbkx-rs t saveset              read it all, check the checksums\n\n\
         Completion: 0 - done; 1 - something damaged or not done; 2 - not usable."
    );
    2
}

fn main1() -> i32 {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 || !matches!(args[1].as_str(), "l" | "x" | "t") {
        return usage();
    }
    let op = args[1].clone();
    let spec = args[2].clone();
    let mut out = ".".to_string();
    let mut i = 3;
    while i < args.len() {
        if args[i] == "-C" && i + 1 < args.len() && op == "x" {
            out = args[i + 1].clone();
            i += 2;
        } else {
            return usage();
        }
    }
    let mut r = match Reader::open(&spec) {
        Ok(r) => r,
        Err(e) => {
            msg!("{}", e);
            return 2;
        }
    };
    match op.as_str() {
        "l" => list(&mut r),
        "x" => {
            if let Err(e) = fs::create_dir(&out) {
                if e.kind() != io::ErrorKind::AlreadyExists {
                    msg!("{}: {}", out, e);
                    return 2;
                }
            }
            let mut x = Extractor::new(&out, true);
            run(&mut r, &mut x);
            x.finish_dirs();
        }
        _ => {
            let mut x = Extractor::new(".", false);
            run(&mut r, &mut x);
            if !r.bad {
                println!("{}: all files read, all checksums match", spec);
            }
        }
    }
    if r.bad {
        1
    } else {
        0
    }
}

fn main() {
    exit(main1());
}
