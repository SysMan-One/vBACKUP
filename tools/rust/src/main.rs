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
**  USAGE:	vbkx-rs l saveset [-k file]    list the files
**		vbkx-rs x saveset [-C dir] [-k file]
**					       extract them all into dir
**						(default: the current directory)
**		vbkx-rs t saveset [-k file]    read it all, check the checksums
**		vbkx-rs selftest               check the primitives of the
**						encryption against the test
**						vectors of their standards, and
**						the codecs against streams of
**						zlib and liblzma
**
**		saveset is volume 1 (x.bck); volumes 2, 3, ... are looked
**		for beside it as x.bck.002, x.bck.003, ...
**
**		  $ vbkx-rs l /mnt/usb/home.bck
**		  $ vbkx-rs x /mnt/usb/home.bck -C /tmp/restore
**		  $ vbkx-rs t /mnt/usb/home.bck
**		  $ vbkx-rs x /mnt/usb/secret.bck -k ~/.vbackup.key
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
**  DATA:	DATA records and DATAZ records (vbackup /DATA_FORMAT=COMPRESSED
**		and /LEVEL, format.md 6.7: codec 1 the LZ4 block format, 2 raw
**		Deflate, 3 raw LZMA1) alike; a DATAZ block is
**		decompressed under the same checks as everything else - a
**		length or an offset out of bounds makes it a bad record, and
**		its file is named incomplete.
**		SOLID records (version 3, format.md 6.12: the FILE, DATA
**		and FEND records of small files compressed together, by the
**		same codecs) are opened and their records taken as if they
**		stood in the stream; one that does not open, or holds a
**		record that is not right, is a gap like lost blocks - its
**		files named from the catalog, never wrong octets.
**
**  VOLUMES:	a saveset of vbackup /PHYSICAL (format.md 6.8) holds one device:
**		it comes out as one sparse file, the image of that device
**		(sdb1); one of vbackup /IMAGE (6.9) comes out as the plain tree
**		of the files of the volume - making the file system again is
**		vbackup's business, not this one's.
**
**  ENCRYPTED:	a saveset of vbackup /ENCRYPT (format.md 6.10) is read with
**		its passphrase: the first line of the key file of -k (the
**		line end, LF or CR LF, left out; a file that its group or
**		others may read or write is refused - "chmod 600 it"), else
**		of the file VBACKUP_KEY_FILE names, else it is asked for on
**		the terminal without echo (stty; no terminal - give -k).
**		Its bytes are taken as they are.  The keys come from it by
**		PBKDF2-HMAC-SHA256 with the SALT and KDFITER of the VHDR; a
**		wrong passphrase is said ("the passphrase does not open")
**		and nothing is read or made.  Every block is checked by its
**		CRC, then by its TAG (HMAC-SHA256): a block whose CRC is
**		right and whose TAG is not was changed on purpose - it is
**		said, and it is a bad block like any, rebuilt from its group
**		(the rebuilt one must pass its TAG too) or lost.  Only then
**		are the good blocks decrypted (ChaCha20).  SHA-256, HMAC,
**		PBKDF2 and ChaCha20 are written out in this file from their
**		standards; "selftest" checks them.  A volume 1 without its
**		VHDR takes the keys from the VHDR of any other volume.
**
**  DAMAGE:	every block is checked (CRC-32); one bad block in a group is
**		rebuilt from the group's XOR block - with vbackup /PARITY=m
**		(format.md 4.1, version 2 or 3) any m bad blocks of a group, by
**		Reed-Solomon in GF(2^8), written out here too; after a loss the stream
**		is picked up at the next good block.  A file that lost data
**		is kept as far as it got and named: "File: <name> - is
**		incomplete".  A file whose records were lost entirely is
**		named from the catalog: "File: <name> - not extracted";
**		without a readable catalog that is said once ("Saveset:
**		<saveset> - files missing ... cannot all be named").  A
**		missing or cut volume is skipped, the next one is read.  A
**		volume 1 whose first block (VHDR) is bad is still read: the
**		block size is found by trying every legal size against the
**		CRC of the blocks after it.
**
**  GUARANTEED:	no panic and no hang on any input: every length and index
**		taken from the saveset is checked, nothing is unwrapped; the
**		only "unsafe" are the two system calls std of Rust 1.63 does
**		not offer (utimensat, mkfifo), on names already checked.
**		(The terminal is set by stty run as a program, for the
**		same reason: no termios in std.)
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
**	X01-22		 7-OCT-2026	RRL
**		The repair of a group: the good DATA blocks first made as the
**		writer left them past PAYLEN - zeros up to the TAG.  Nothing
**		authenticates those octets, the parity covers them: one byte
**		changed there in a good block, and the bad one of its group
**		could not be rebuilt.
**
**	X01-21		 7-OCT-2026	RRL
**		Savesets of version 3 (format.md 3, 6.12): the groups of version 1,
**		or of version 2 with PARITY 2 or more; the SOLID record opened, its
**		FILE, DATA, FEND records taken one by one as if in the stream; a
**		SOLID that does not open is a gap, its files named lost.  The block
**		size tried on a header before the whole block is read.
**
**	X01-19		 6-OCT-2026	RRL
**		DATAZ codecs 2 (raw Deflate, RFC 1951: stored, fixed, dynamic
**		blocks) and 3 (raw LZMA1, lc=3 lp=0 pb=2, the end marker), each
**		under the checks of the LZ4 one; the selftest has their streams.
**
**	X01-16		 6-OCT-2026	RRL
**		The products of the repair by the vector instructions: AVX2 or
**		SSSE3 (found at run time), NEON on aarch64 - the nibble tables of
**		a coefficient, checked against the portable code once; the CRC
**		eight octets a step (slicing-by-8).
**
**	X01-14		 5-OCT-2026	RRL
**		Savesets of version 2 (/PARITY=m, format.md 4.1): PARITY
**		blocks, groups of GRPSZ + m blocks, up to m bad blocks of a
**		group rebuilt with their headers (the header parity in the
**		RECOFF and PAYLEN of the parity blocks); the rows left over
**		check the result, one row whose CRC is right and whose bytes
**		are not is passed over.  Reed-Solomon in selftest.
**
**	X01-08		 5-OCT-2026	RRL
**		The messages in the form of vbkx and VBACKUP: what they are
**		about first, as "Label: value", then " - " and the words;
**		an error of the system as "errno: N - words (its text)".
**
**	X01-06		 5-OCT-2026	RRL
**		Encrypted savesets (format.md 6.10): EDATA and ETRAILER,
**		the passphrase from -k, VBACKUP_KEY_FILE or the terminal,
**		TAG checked before the repair and after it, ChaCha20;
**		SHA-256, HMAC, PBKDF2, ChaCha20 written out here, std only;
**		the command selftest.
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
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::{FileExt, MetadataExt, PermissionsExt};
use std::process::{Command, Stdio};
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
const BT_EDATA: u8 = 5; // DATA of an encrypted saveset: format.md 6.10
const BT_ETRAILER: u8 = 6; // its TRAILER
const BT_PARITY: u8 = 7; // a parity row >= 1 of version 2 and 3: format.md 4.1
const TAG_GROUPSIZE: u16 = 71;
const TAG_PARITY: u16 = 93; // m, the parity blocks of a group (version 2 and 3)
const MAXPAR: u32 = 8;

/* Encryption, format.md 6.10 */
const TAG_CIPHER: u16 = 88;
const TAG_KDF: u16 = 89;
const TAG_KDFITER: u16 = 90;
const TAG_SALT: u16 = 91;
const TAG_KEYCHECK: u16 = 92;
const CIPHER_CC20HS: u64 = 1; // ChaCha20 and HMAC-SHA256
const KDF_PBKDF2: u64 = 1; // PBKDF2-HMAC-SHA256
const KDFMIN: u32 = 1000; // the fewest iterations a reader accepts
const TAGSZ: usize = 32; // the TAG at the end of a payload area
const PASSMAX: usize = 1024; // the longest passphrase

const RT_SUMMARY: u16 = 1;
const RT_FILE: u16 = 2;
const RT_DATA: u16 = 3;
const RT_FEND: u16 = 4;
const RT_CATALOG: u16 = 5;
const RT_END: u16 = 6;
const RT_DATAZ: u16 = 7; // DATA, compressed: format.md 6.7
const RT_SOLID: u16 = 8; // FILE, DATA, FEND of small files, compressed: format.md 6.12

const MAXDATA: u32 = 1 << 20; // the most octets a DATA or DATAZ record holds
const MAXSOLID: u32 = (1 << 20) + 65536; // the records of a SOLID, at most
const SOLIDHDR: usize = 12; // codec, rawlen, count of a SOLID
const CODEC_LZ4: u32 = 1;
const CODEC_DEFLATE: u32 = 2; // raw Deflate, RFC 1951: format.md 6.7.2
const CODEC_LZMA: u32 = 3; // raw LZMA1, lc=3 lp=0 pb=2, end marker: format.md 6.7.3

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
    t8: Vec<[u32; 256]>, /* slicing-by-8: T[k][b] = the CRC of b followed by k zero octets */
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
        let mut t8 = vec![[0u32; 256]; 8];
        t8[0] = table;
        for k in 1..8 {
            for b in 0..256 {
                let p = t8[k - 1][b];
                t8[k][b] = (p >> 8) ^ table[(p & 0xFF) as usize];
            }
        }
        Crc { table, t8 }
    }

    /* Chained as zlib's crc32(crc, buf): update(0, b) is the CRC of b */
    fn update(&self, crc: u32, buf: &[u8]) -> u32 {
        let mut c = !crc;
        let t = &self.t8;
        let mut chunks = buf.chunks_exact(8);
        for w in &mut chunks {
            let lo = c ^ u32::from_le_bytes([w[0], w[1], w[2], w[3]]);
            let hi = u32::from_le_bytes([w[4], w[5], w[6], w[7]]);
            c = t[7][(lo & 0xFF) as usize]
                ^ t[6][((lo >> 8) & 0xFF) as usize]
                ^ t[5][((lo >> 16) & 0xFF) as usize]
                ^ t[4][(lo >> 24) as usize]
                ^ t[3][(hi & 0xFF) as usize]
                ^ t[2][((hi >> 8) & 0xFF) as usize]
                ^ t[1][((hi >> 16) & 0xFF) as usize]
                ^ t[0][(hi >> 24) as usize];
        }
        for &b in chunks.remainder() {
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
** inflate - codec 2, raw Deflate (RFC 1951, format.md 6.7.2): stored,
** fixed and dynamic blocks; every code set checked (none over-subscribed,
** an incomplete one only with a single code), every length and distance
** against what has been output and what is still wanted; exactly rawlen
** octets, the final block ending in the last octet of src (only its
** padding bits after the end-of-block code).  None - a bad stream.
 */
const DFL_LBASE: [u16; 29] = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258];
const DFL_LEXT: [u8; 29] = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
const DFL_DBASE: [u16; 30] =
    [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577];
const DFL_DEXT: [u8; 30] = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13];
const DFL_CLORDER: [usize; 19] = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];

/* The bits of the stream, LSB first */
struct BitIn<'a> {
    src: &'a [u8],
    ip: usize,
    bits: u64,
    n: u32,
}

impl<'a> BitIn<'a> {
    fn fill(&mut self) {
        while self.n <= 56 && self.ip < self.src.len() {
            self.bits |= (self.src[self.ip] as u64) << self.n;
            self.ip += 1;
            self.n += 8;
        }
    }
    fn get(&mut self, k: u32) -> Option<u32> {
        if self.n < k {
            self.fill();
            if self.n < k {
                return None;
            }
        }
        let v = (self.bits & ((1u64 << k) - 1)) as u32;
        self.bits >>= k;
        self.n -= k;
        Some(v)
    }
}

/* A Huffman code: the counts of each length and the symbols in canonical order (as puff of zlib keeps them) */
struct Huff {
    count: [u16; 16],
    symbol: Vec<u16>,
}

impl Huff {
    fn build(lens: &[u8]) -> Option<Huff> {
        let mut h = Huff { count: [0; 16], symbol: vec![0; lens.len()] };
        for &l in lens {
            if l > 15 {
                return None;
            }
            h.count[l as usize] += 1;
        }
        let (mut left, mut codes) = (1i32, 0i32);
        for b in 1..16 {
            left = (left << 1) - h.count[b] as i32;
            codes += h.count[b] as i32;
            if left < 0 {
                return None; // over-subscribed
            }
        }
        if left != 0 && codes > 1 {
            return None; // incomplete: only a single code may be
        }
        let mut offs = [0u16; 16];
        for b in 1..15 {
            offs[b + 1] = offs[b] + h.count[b];
        }
        for (i, &l) in lens.iter().enumerate() {
            if l != 0 {
                h.symbol[offs[l as usize] as usize] = i as u16;
                offs[l as usize] += 1;
            }
        }
        Some(h)
    }
    /* One symbol, a bit at a time */
    fn decode(&self, inp: &mut BitIn) -> Option<usize> {
        let (mut code, mut first, mut index) = (0i32, 0i32, 0i32);
        for b in 1..16 {
            code |= inp.get(1)? as i32;
            let c = self.count[b] as i32;
            if code - c < first {
                return self.symbol.get((index + (code - first)) as usize).map(|&s| s as usize);
            }
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        None
    }
}

fn inflate_codes(inp: &mut BitIn, lit: &Huff, dist: &Huff, dst: &mut Vec<u8>, raw: usize) -> Option<()> {
    loop {
        let sym = lit.decode(inp)?;
        if sym < 256 {
            if dst.len() >= raw {
                return None;
            }
            dst.push(sym as u8);
            continue;
        }
        if sym == 256 {
            return Some(());
        }
        let s = sym - 257;
        if s >= 29 {
            return None;
        }
        let len = DFL_LBASE[s] as usize + inp.get(DFL_LEXT[s] as u32)? as usize;
        let ds = dist.decode(inp)?;
        if ds >= 30 {
            return None;
        }
        let d = DFL_DBASE[ds] as usize + inp.get(DFL_DEXT[ds] as u32)? as usize;
        if d > dst.len() || len > raw - dst.len() {
            return None;
        }
        for _ in 0..len {
            /* octet by octet: a match may overlap what it makes */
            let b = dst[dst.len() - d];
            dst.push(b);
        }
    }
}

fn inflate(src: &[u8], rawlen: u32) -> Option<Vec<u8>> {
    let raw = rawlen as usize;
    let mut dst: Vec<u8> = Vec::with_capacity(raw);
    let mut inp = BitIn { src, ip: 0, bits: 0, n: 0 };
    loop {
        let last = inp.get(1)?;
        match inp.get(2)? {
            0 => {
                /* Stored: to the octet, LEN and its complement, the octets */
                let pad = inp.n & 7;
                inp.bits >>= pad;
                inp.n -= pad;
                let n = inp.get(16)? as usize;
                let nn = inp.get(16)? as usize;
                if n != (!nn & 0xFFFF) || n > raw - dst.len() {
                    return None;
                }
                let mut k = n;
                while k > 0 && inp.n >= 8 {
                    dst.push(inp.get(8)? as u8);
                    k -= 1;
                }
                let s = inp.src.get(inp.ip..inp.ip.checked_add(k)?)?;
                dst.extend_from_slice(s);
                inp.ip += k;
            }
            1 => {
                let mut fl = [0u8; 288];
                for (i, l) in fl.iter_mut().enumerate() {
                    *l = if i < 144 { 8 } else if i < 256 { 9 } else if i < 280 { 7 } else { 8 };
                }
                let lit = Huff::build(&fl)?;
                let dist = Huff::build(&[5u8; 32])?;
                inflate_codes(&mut inp, &lit, &dist, &mut dst, raw)?;
            }
            2 => {
                let hlit = inp.get(5)? as usize + 257;
                let hdist = inp.get(5)? as usize + 1;
                let hclen = inp.get(4)? as usize + 4;
                if hlit > 286 || hdist > 30 {
                    return None;
                }
                let mut cll = [0u8; 19];
                for &o in DFL_CLORDER.iter().take(hclen) {
                    cll[o] = inp.get(3)? as u8;
                }
                let cl = Huff::build(&cll)?;
                let mut lens = vec![0u8; hlit + hdist];
                let mut i = 0;
                while i < hlit + hdist {
                    let sym = cl.decode(&mut inp)?;
                    if sym < 16 {
                        lens[i] = sym as u8;
                        i += 1;
                        continue;
                    }
                    let (val, rep) = match sym {
                        16 => {
                            if i == 0 {
                                return None;
                            }
                            (lens[i - 1], 3 + inp.get(2)? as usize)
                        }
                        17 => (0, 3 + inp.get(3)? as usize),
                        _ => (0, 11 + inp.get(7)? as usize),
                    };
                    if i + rep > hlit + hdist {
                        return None;
                    }
                    for _ in 0..rep {
                        lens[i] = val;
                        i += 1;
                    }
                }
                if lens[256] == 0 {
                    return None; // no end-of-block code: no block can end
                }
                let lit = Huff::build(&lens[..hlit])?;
                let dist = Huff::build(&lens[hlit..])?;
                inflate_codes(&mut inp, &lit, &dist, &mut dst, raw)?;
            }
            _ => return None,
        }
        if last == 1 {
            break;
        }
    }
    /* All of it: the octets wanted, the input to its last octet - only the padding bits of that one left */
    if dst.len() != raw || inp.ip != src.len() || inp.n >= 8 {
        return None;
    }
    Some(dst)
}

/*
** lzma_decompress - codec 3, raw LZMA1 (format.md 6.7.3): lc=3 lp=0 pb=2,
** the range decoder of the LZMA specification; every distance against
** what has been output, every length against what is still wanted;
** exactly rawlen octets, then the end marker with the code at 0 and the
** input used up.  None - a bad stream.
 */
const LZM_END: u32 = 0xFFFF_FFFF;

struct RcIn<'a> {
    src: &'a [u8],
    ip: usize,
    range: u32,
    code: u32,
    bad: bool,
}

impl<'a> RcIn<'a> {
    fn norm(&mut self) {
        if self.range < (1 << 24) {
            self.range <<= 8;
            match self.src.get(self.ip) {
                Some(&b) => {
                    self.code = (self.code << 8) | b as u32;
                    self.ip += 1;
                }
                None => {
                    self.bad = true;
                    self.code <<= 8;
                }
            }
        }
    }
    fn bit(&mut self, p: &mut u16) -> u32 {
        let bound = (self.range >> 11).wrapping_mul(*p as u32);
        let b = if self.code < bound {
            self.range = bound;
            *p += (2048 - *p) >> 5;
            0
        } else {
            self.range -= bound;
            self.code -= bound;
            *p -= *p >> 5;
            1
        };
        self.norm();
        b
    }
    fn direct(&mut self, n: u32) -> u32 {
        let mut res = 0u32;
        for _ in 0..n {
            self.range >>= 1;
            self.code = self.code.wrapping_sub(self.range);
            let t = 0u32.wrapping_sub(self.code >> 31);
            self.code = self.code.wrapping_add(self.range & t);
            if self.code == self.range {
                self.bad = true;
            }
            self.norm();
            res = (res << 1).wrapping_add(t.wrapping_add(1));
        }
        res
    }
    fn tree(&mut self, p: &mut [u16], bits: u32) -> u32 {
        let mut m = 1usize;
        for _ in 0..bits {
            m = (m << 1) | self.bit(&mut p[m]) as usize;
        }
        (m - (1 << bits)) as u32
    }
    fn revtree(&mut self, p: &mut [u16], bits: u32) -> u32 {
        let (mut m, mut sym) = (1usize, 0u32);
        for i in 0..bits {
            let b = self.bit(&mut p[m]);
            m = (m << 1) | b as usize;
            sym |= b << i;
        }
        sym
    }
}

struct LzmLen {
    choice: u16,
    choice2: u16,
    low: [[u16; 8]; 4],
    mid: [[u16; 8]; 4],
    high: [u16; 256],
}

impl LzmLen {
    fn new() -> LzmLen {
        LzmLen { choice: 1024, choice2: 1024, low: [[1024; 8]; 4], mid: [[1024; 8]; 4], high: [1024; 256] }
    }
    fn decode(&mut self, rc: &mut RcIn, ps: usize) -> u32 {
        if rc.bit(&mut self.choice) == 0 {
            return 2 + rc.tree(&mut self.low[ps], 3);
        }
        if rc.bit(&mut self.choice2) == 0 {
            return 2 + 8 + rc.tree(&mut self.mid[ps], 3);
        }
        2 + 16 + rc.tree(&mut self.high, 8)
    }
}

fn lzma_decompress(src: &[u8], rawlen: u32) -> Option<Vec<u8>> {
    let raw = rawlen as usize;
    /* The range coder: a zero, then the code in four octets */
    if src.len() < 5 || src[0] != 0 {
        return None;
    }
    let mut rc = RcIn { src, ip: 5, range: 0xFFFF_FFFF, code: u32::from_be_bytes([src[1], src[2], src[3], src[4]]), bad: false };
    if rc.code == rc.range {
        return None;
    }
    let mut literal = vec![1024u16; 0x300 << 3];
    let mut ismatch = [[1024u16; 4]; 12];
    let mut isrep0long = [[1024u16; 4]; 12];
    let (mut isrep, mut isrepg0, mut isrepg1, mut isrepg2) = ([1024u16; 12], [1024u16; 12], [1024u16; 12], [1024u16; 12]);
    let mut posslot = [[1024u16; 64]; 4];
    let mut posspec = [1024u16; 115];
    let mut align = [1024u16; 16];
    let (mut lenc, mut replenc) = (LzmLen::new(), LzmLen::new());
    let mut reps = [0u32; 4];
    let mut state = 0usize;
    let mut dst: Vec<u8> = Vec::with_capacity(raw);

    while !rc.bad {
        let ps = dst.len() & 3;
        if rc.bit(&mut ismatch[state][ps]) == 0 {
            /* A literal: after a match, the octet at rep0 guides its bits while they agree */
            if dst.len() >= raw {
                return None;
            }
            let base = 0x300 * (dst.last().map_or(0, |&b| b as usize) >> 5);
            let lit = &mut literal[base..base + 0x300];
            let mut sym = 1usize;
            if state >= 7 {
                let mut mb = *dst.get(dst.len().checked_sub(reps[0] as usize + 1)?)? as usize;
                while sym < 0x100 {
                    let mbit = (mb >> 7) & 1;
                    mb <<= 1;
                    let b = rc.bit(&mut lit[0x100 + (mbit << 8) + sym]) as usize;
                    sym = (sym << 1) | b;
                    if mbit != b {
                        break;
                    }
                }
            }
            while sym < 0x100 {
                sym = (sym << 1) | rc.bit(&mut lit[sym]) as usize;
            }
            dst.push(sym as u8);
            state = if state < 4 { 0 } else if state < 10 { state - 3 } else { state - 6 };
            continue;
        }
        let len;
        if rc.bit(&mut isrep[state]) == 1 {
            if dst.is_empty() {
                return None;
            }
            if rc.bit(&mut isrepg0[state]) == 0 {
                if rc.bit(&mut isrep0long[state][ps]) == 0 {
                    /* A short rep: one octet at rep0 */
                    if dst.len() >= raw {
                        return None;
                    }
                    state = if state < 7 { 9 } else { 11 };
                    let b = *dst.get(dst.len().checked_sub(reps[0] as usize + 1)?)?;
                    dst.push(b);
                    continue;
                }
            } else {
                let d;
                if rc.bit(&mut isrepg1[state]) == 0 {
                    d = reps[1];
                } else {
                    if rc.bit(&mut isrepg2[state]) == 0 {
                        d = reps[2];
                    } else {
                        d = reps[3];
                        reps[3] = reps[2];
                    }
                    reps[2] = reps[1];
                }
                reps[1] = reps[0];
                reps[0] = d;
            }
            len = replenc.decode(&mut rc, ps);
            state = if state < 7 { 8 } else { 11 };
        } else {
            reps[3] = reps[2];
            reps[2] = reps[1];
            reps[1] = reps[0];
            len = lenc.decode(&mut rc, ps);
            state = if state < 7 { 7 } else { 10 };
            /* The distance (less one) */
            let ls = ((len - 2) as usize).min(3);
            let slot = rc.tree(&mut posslot[ls], 6);
            reps[0] = if slot < 4 {
                slot
            } else {
                let foot = (slot >> 1) - 1;
                let base = (2 | (slot & 1)) << foot;
                if slot < 14 {
                    base + rc.revtree(&mut posspec[(base - slot) as usize..], foot)
                } else {
                    let d = base.wrapping_add(rc.direct(foot - 4) << 4);
                    d.wrapping_add(rc.revtree(&mut align, 4))
                }
            };
            if reps[0] == LZM_END {
                /* The end: all the octets out, the code at 0, the input used up */
                if !rc.bad && dst.len() == raw && rc.code == 0 && rc.ip == src.len() {
                    return Some(dst);
                }
                return None;
            }
        }
        let d = reps[0] as usize;
        if d >= dst.len() || len as usize > raw - dst.len() {
            return None;
        }
        for _ in 0..len {
            let b = dst[dst.len() - d - 1];
            dst.push(b);
        }
    }
    None
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
    if rawlen > MAXDATA {
        return None;
    }
    /* An unknown codec: a bad record, its file named incomplete - never wrong octets */
    let d = match u32_at(body, 4) {
        CODEC_LZ4 => lz4_decompress(z, rawlen)?,
        CODEC_DEFLATE => inflate(z, rawlen)?,
        CODEC_LZMA => lzma_decompress(z, rawlen)?,
        _ => return None,
    };
    Some((u32_at(body, 0), u64_at(body, 8), std::borrow::Cow::Owned(d)))
}

/*
** The primitives of an encrypted saveset (format.md 6.10), written out
** here from their standards, std only: SHA-256 (FIPS 180-4), HMAC-SHA256
** (RFC 2104), PBKDF2-HMAC-SHA256 (RFC 8018) and ChaCha20 (RFC 8439 2.4).
** All arithmetic modulo 2^32 is wrapping_*: overflow checks stay on.
*/
const SHA_K: [u32; 64] = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

#[derive(Clone)]
struct Sha256 {
    h: [u32; 8],
    buf: [u8; 64],
    nbuf: usize,
    total: u64, // octets fed so far
}

impl Sha256 {
    fn new() -> Sha256 {
        Sha256 {
            h: [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19],
            buf: [0; 64],
            nbuf: 0,
            total: 0,
        }
    }

    fn block(&mut self, b: &[u8]) {
        let mut w = [0u32; 64];
        for (i, wi) in w.iter_mut().enumerate().take(16) {
            *wi = u32::from_be_bytes([b[4 * i], b[4 * i + 1], b[4 * i + 2], b[4 * i + 3]]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16].wrapping_add(s0).wrapping_add(w[i - 7]).wrapping_add(s1);
        }
        let mut v = self.h;
        for i in 0..64 {
            let s1 = v[4].rotate_right(6) ^ v[4].rotate_right(11) ^ v[4].rotate_right(25);
            let ch = (v[4] & v[5]) ^ (!v[4] & v[6]);
            let t1 = v[7].wrapping_add(s1).wrapping_add(ch).wrapping_add(SHA_K[i]).wrapping_add(w[i]);
            let s0 = v[0].rotate_right(2) ^ v[0].rotate_right(13) ^ v[0].rotate_right(22);
            let maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            let t2 = s0.wrapping_add(maj);
            v[7] = v[6];
            v[6] = v[5];
            v[5] = v[4];
            v[4] = v[3].wrapping_add(t1);
            v[3] = v[2];
            v[2] = v[1];
            v[1] = v[0];
            v[0] = t1.wrapping_add(t2);
        }
        for (h, x) in self.h.iter_mut().zip(v.iter()) {
            *h = h.wrapping_add(*x);
        }
    }

    fn update(&mut self, mut data: &[u8]) {
        self.total = self.total.wrapping_add(data.len() as u64);
        while !data.is_empty() {
            let n = (64 - self.nbuf).min(data.len());
            self.buf[self.nbuf..self.nbuf + n].copy_from_slice(&data[..n]);
            self.nbuf += n;
            data = &data[n..];
            if self.nbuf == 64 {
                let b = self.buf;
                self.block(&b);
                self.nbuf = 0;
            }
        }
    }

    fn finish(mut self) -> [u8; 32] {
        let bits = self.total.wrapping_mul(8);
        self.update(&[0x80]);
        while self.nbuf != 56 {
            self.update(&[0]);
        }
        self.update(&bits.to_be_bytes());
        let mut d = [0u8; 32];
        for (i, h) in self.h.iter().enumerate() {
            d[4 * i..4 * i + 4].copy_from_slice(&h.to_be_bytes());
        }
        d
    }
}

/* HMAC-SHA256: the two hash states after the padded key, kept for any number of messages */
#[derive(Clone)]
struct Hmac {
    inner: Sha256,
    outer: Sha256,
}

impl Hmac {
    fn new(key: &[u8]) -> Hmac {
        let mut k = [0u8; 64];
        if key.len() > 64 {
            let mut s = Sha256::new();
            s.update(key);
            k[..32].copy_from_slice(&s.finish());
        } else {
            k[..key.len()].copy_from_slice(key);
        }
        let (mut inner, mut outer) = (Sha256::new(), Sha256::new());
        let mut pad = [0u8; 64];
        for (p, x) in pad.iter_mut().zip(k.iter()) {
            *p = x ^ 0x36;
        }
        inner.update(&pad);
        for (p, x) in pad.iter_mut().zip(k.iter()) {
            *p = x ^ 0x5c;
        }
        outer.update(&pad);
        Hmac { inner, outer }
    }

    /* The MAC of the concatenation of the pieces */
    fn mac(&self, parts: &[&[u8]]) -> [u8; 32] {
        let mut s = self.inner.clone();
        for p in parts {
            s.update(p);
        }
        let d = s.finish();
        let mut o = self.outer.clone();
        o.update(&d);
        o.finish()
    }
}

fn pbkdf2(pass: &[u8], salt: &[u8], iter: u32, out: &mut [u8]) {
    let h = Hmac::new(pass);
    for (i, chunk) in out.chunks_mut(32).enumerate() {
        let idx = (i as u32).wrapping_add(1).to_be_bytes();
        let mut u = h.mac(&[salt, &idx]);
        let mut t = u;
        for _ in 1..iter {
            u = h.mac(&[&u]);
            for (x, y) in t.iter_mut().zip(u.iter()) {
                *x ^= *y;
            }
        }
        let n = chunk.len();
        chunk.copy_from_slice(&t[..n]);
    }
}

/* ChaCha20 of RFC 8439: buf XOR the key stream from block COUNTER on */
fn chacha20(key: &[u8; 32], nonce: &[u8; 12], counter: u32, buf: &mut [u8]) {
    fn qr(s: &mut [u32; 16], a: usize, b: usize, c: usize, d: usize) {
        s[a] = s[a].wrapping_add(s[b]);
        s[d] = (s[d] ^ s[a]).rotate_left(16);
        s[c] = s[c].wrapping_add(s[d]);
        s[b] = (s[b] ^ s[c]).rotate_left(12);
        s[a] = s[a].wrapping_add(s[b]);
        s[d] = (s[d] ^ s[a]).rotate_left(8);
        s[c] = s[c].wrapping_add(s[d]);
        s[b] = (s[b] ^ s[c]).rotate_left(7);
    }
    let mut init = [0u32; 16];
    init[0..4].copy_from_slice(&[0x6170_7865, 0x3320_646e, 0x7962_2d32, 0x6b20_6574]);
    for i in 0..8 {
        init[4 + i] = u32_at(key, 4 * i);
    }
    for i in 0..3 {
        init[13 + i] = u32_at(nonce, 4 * i);
    }
    let mut ctr = counter;
    for chunk in buf.chunks_mut(64) {
        init[12] = ctr;
        let mut s = init;
        for _ in 0..10 {
            qr(&mut s, 0, 4, 8, 12);
            qr(&mut s, 1, 5, 9, 13);
            qr(&mut s, 2, 6, 10, 14);
            qr(&mut s, 3, 7, 11, 15);
            qr(&mut s, 0, 5, 10, 15);
            qr(&mut s, 1, 6, 11, 12);
            qr(&mut s, 2, 7, 8, 13);
            qr(&mut s, 3, 4, 9, 14);
        }
        for (j, x) in chunk.iter_mut().enumerate() {
            let w = s[j / 4].wrapping_add(init[j / 4]);
            *x ^= (w >> (8 * (j % 4))) as u8;
        }
        ctr = ctr.wrapping_add(1);
    }
}

/* Equal or not, in a time that does not depend on where they differ */
fn equal(a: &[u8], b: &[u8]) -> bool {
    a.len() == b.len() && a.iter().zip(b.iter()).fold(0u8, |d, (x, y)| d | (x ^ y)) == 0
}

/* The keys of a saveset, out of its passphrase: format.md 6.10 */
struct Keys {
    enc: [u8; 32],
    mac: Hmac,
    check: [u8; 32],
}

fn derive(pass: &[u8], salt: &[u8], iter: u32) -> Keys {
    let mut mk = [0u8; 32];
    pbkdf2(pass, salt, iter, &mut mk);
    let h = Hmac::new(&mk);
    Keys { enc: h.mac(&[b"VBACKUP ENC"]), mac: Hmac::new(&h.mac(&[b"VBACKUP MAC"])), check: h.mac(&[b"VBACKUP CHECK"]) }
}

/* The TAG of a block: the header fields of 6.10, little-endian, then the ciphertext */
fn tag(k: &Keys, bsize: u32, h: &Bhdr, ct: &[u8]) -> [u8; 32] {
    let mut m = Vec::with_capacity(39);
    m.extend_from_slice(&h.uuid);
    m.extend_from_slice(&bsize.to_le_bytes());
    m.extend_from_slice(&h.blkno.to_le_bytes());
    m.extend_from_slice(&h.volno.to_le_bytes());
    m.push(h.typ);
    m.extend_from_slice(&h.gindex.to_le_bytes());
    m.extend_from_slice(&h.recoff.to_le_bytes());
    m.extend_from_slice(&h.paylen.to_le_bytes());
    k.mac.mac(&[&m, ct])
}

/* The TAG of a payload area is right, and PAYLEN leaves room for it */
fn tag_ok(k: &Keys, bsize: u32, h: &Bhdr, pay: &[u8]) -> bool {
    if pay.len() < TAGSZ || h.paylen as usize > pay.len() - TAGSZ {
        return false;
    }
    equal(&tag(k, bsize, h, &pay[..h.paylen as usize]), &pay[pay.len() - TAGSZ..])
}

/* Decrypt PAYLEN octets of a payload in place: the nonce is u32 0, u64 blkno */
fn decrypt(k: &Keys, h: &Bhdr, pay: &mut [u8]) {
    let mut nonce = [0u8; 12];
    nonce[4..].copy_from_slice(&h.blkno.to_le_bytes());
    let n = (h.paylen as usize).min(pay.len());
    chacha20(&k.enc, &nonce, 0, &mut pay[..n]);
}

/*
** Reed-Solomon of a group, format.md 4.1: GF(2^8) with the polynomial
** 0x11D; the coefficient of DATA block i in row j is y_i / (j + y_i),
** y_i = 128 + i - a Cauchy matrix scaled so that row 0 is all ones, the
** XOR block.  Any n of the n + m blocks give the group back.
*/
struct Gf {
    exp: [u8; 512],
    log: [u8; 256],
    simd: u8, /* 0 the portable code, 1 SSSE3, 2 AVX2, 3 NEON - checked against the portable code */
}

/*
**  dst ^= c * src by vector instructions: LO[x] = c * x, HI[x] = c * (x << 4),
**  an octet b is LO[b & 15] ^ HI[b >> 4]; whole vectors only, the octets
**  done are returned
*/
#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "ssse3")]
unsafe fn vec_ssse3(dst: &mut [u8], src: &[u8], lo: &[u8; 16], hi: &[u8; 16]) -> usize {
    use std::arch::x86_64::*;
    let n = dst.len().min(src.len()) & !15;
    let l = _mm_loadu_si128(lo.as_ptr() as *const __m128i);
    let h = _mm_loadu_si128(hi.as_ptr() as *const __m128i);
    let m = _mm_set1_epi8(0x0F);
    let mut i = 0;
    while i < n {
        let s = _mm_loadu_si128(src.as_ptr().add(i) as *const __m128i);
        let p = _mm_xor_si128(
            _mm_shuffle_epi8(l, _mm_and_si128(s, m)),
            _mm_shuffle_epi8(h, _mm_and_si128(_mm_srli_epi64(s, 4), m)),
        );
        let d = dst.as_mut_ptr().add(i) as *mut __m128i;
        _mm_storeu_si128(d, _mm_xor_si128(_mm_loadu_si128(d), p));
        i += 16;
    }
    n
}

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn vec_avx2(dst: &mut [u8], src: &[u8], lo: &[u8; 16], hi: &[u8; 16]) -> usize {
    use std::arch::x86_64::*;
    let n = dst.len().min(src.len()) & !31;
    let l = _mm256_broadcastsi128_si256(_mm_loadu_si128(lo.as_ptr() as *const __m128i));
    let h = _mm256_broadcastsi128_si256(_mm_loadu_si128(hi.as_ptr() as *const __m128i));
    let m = _mm256_set1_epi8(0x0F);
    let mut i = 0;
    while i < n {
        let s = _mm256_loadu_si256(src.as_ptr().add(i) as *const __m256i);
        let p = _mm256_xor_si256(
            _mm256_shuffle_epi8(l, _mm256_and_si256(s, m)),
            _mm256_shuffle_epi8(h, _mm256_and_si256(_mm256_srli_epi64(s, 4), m)),
        );
        let d = dst.as_mut_ptr().add(i) as *mut __m256i;
        _mm256_storeu_si256(d, _mm256_xor_si256(_mm256_loadu_si256(d), p));
        i += 32;
    }
    n
}

#[cfg(target_arch = "aarch64")]
unsafe fn vec_neon(dst: &mut [u8], src: &[u8], lo: &[u8; 16], hi: &[u8; 16]) -> usize {
    use std::arch::aarch64::*;
    let n = dst.len().min(src.len()) & !15;
    let l = vld1q_u8(lo.as_ptr());
    let h = vld1q_u8(hi.as_ptr());
    let m = vdupq_n_u8(0x0F);
    let mut i = 0;
    while i < n {
        let s = vld1q_u8(src.as_ptr().add(i));
        let p = veorq_u8(vqtbl1q_u8(l, vandq_u8(s, m)), vqtbl1q_u8(h, vshrq_n_u8(s, 4)));
        let d = dst.as_mut_ptr().add(i);
        vst1q_u8(d, veorq_u8(vld1q_u8(d), p));
        i += 16;
    }
    n
}

impl Gf {
    fn new() -> Gf {
        let mut g = Gf { exp: [0; 512], log: [0; 256], simd: 0 };
        let mut x: u32 = 1;
        for i in 0..255usize {
            g.exp[i] = x as u8;
            g.exp[i + 255] = x as u8;
            g.log[x as usize] = i as u8;
            x <<= 1;
            if x & 0x100 != 0 {
                x ^= 0x11D;
            }
        }
        g.exp[510] = g.exp[0];
        g.exp[511] = g.exp[0];
        g.simd = g.pick();
        g
    }
    /* The vector instructions there are, when they give the bytes of the portable code */
    fn pick(&self) -> u8 {
        if std::env::var("VBACKUP_NOSIMD").map(|v| v == "1").unwrap_or(false) {
            return 0;
        }
        #[allow(unused_mut)]
        let mut k: u8 = 0;
        #[cfg(target_arch = "x86_64")]
        {
            if is_x86_feature_detected!("avx2") {
                k = 2;
            } else if is_x86_feature_detected!("ssse3") {
                k = 1;
            }
        }
        #[cfg(target_arch = "aarch64")]
        {
            k = 3;
        }
        if k == 0 {
            return 0;
        }
        let src: Vec<u8> = (0..203u32).map(|i| (i * 37 + 11) as u8).collect();
        let mut probe = Gf { exp: self.exp, log: self.log, simd: 0 };
        for c in (2..256u32).step_by(7) {
            let mut a = vec![0x5Au8; 203];
            let mut b = vec![0x5Au8; 203];
            probe.simd = 0;
            probe.muladd(&mut a[1..], &src[1..], c as u8);
            probe.simd = k;
            probe.muladd(&mut b[1..], &src[1..], c as u8);
            if a != b {
                return 0;
            }
        }
        k
    }
    /* The vector instructions chosen: the octets they did */
    #[allow(unused_variables)]
    fn vec(&self, dst: &mut [u8], src: &[u8], lo: &[u8; 16], hi: &[u8; 16]) -> usize {
        #[cfg(target_arch = "x86_64")]
        unsafe {
            return match self.simd {
                2 => vec_avx2(dst, src, lo, hi),
                1 => vec_ssse3(dst, src, lo, hi),
                _ => 0,
            };
        }
        #[cfg(target_arch = "aarch64")]
        unsafe {
            return vec_neon(dst, src, lo, hi);
        }
        #[allow(unreachable_code)]
        0
    }
    /* What makes the products */
    fn simd_name(&self) -> &'static str {
        match self.simd {
            1 => "SSSE3",
            2 => "AVX2",
            3 => "NEON",
            _ => "portable",
        }
    }
    fn mul(&self, a: u8, b: u8) -> u8 {
        if a == 0 || b == 0 {
            0
        } else {
            self.exp[self.log[a as usize] as usize + self.log[b as usize] as usize]
        }
    }
    fn inv(&self, a: u8) -> u8 {
        self.exp[255 - self.log[a as usize] as usize]
    }
    fn coef(&self, row: usize, col: usize) -> u8 {
        if row == 0 {
            return 1;
        }
        let y = (128 + col) as u8;
        self.mul(y, self.inv(row as u8 ^ y))
    }
    /* dst += c * src */
    fn muladd(&self, dst: &mut [u8], src: &[u8], c: u8) {
        if c == 0 {
            return;
        }
        if c == 1 {
            for (d, s) in dst.iter_mut().zip(src.iter()) {
                *d ^= *s;
            }
            return;
        }
        let n = dst.len().min(src.len());
        if self.simd != 0 && n >= 16 {
            let mut lo = [0u8; 16];
            let mut hi = [0u8; 16];
            for x in 0..16usize {
                lo[x] = self.mul(c, x as u8);
                hi[x] = self.mul(c, (x << 4) as u8);
            }
            let done = self.vec(&mut dst[..n], &src[..n], &lo, &hi);
            for i in done..n {
                dst[i] ^= lo[(src[i] & 15) as usize] ^ hi[(src[i] >> 4) as usize];
            }
            return;
        }
        let mut t = [0u8; 256];
        for (x, v) in t.iter_mut().enumerate().skip(1) {
            *v = self.exp[self.log[c as usize] as usize + self.log[x] as usize];
        }
        for (d, s) in dst.iter_mut().zip(src.iter()) {
            *d ^= t[*s as usize];
        }
    }
    /* The inverse of an e x e matrix; None - singular */
    fn invert(&self, mut m: Vec<Vec<u8>>) -> Option<Vec<Vec<u8>>> {
        let e = m.len();
        let mut v: Vec<Vec<u8>> = (0..e).map(|r| (0..e).map(|c| (r == c) as u8).collect()).collect();
        for c in 0..e {
            let p = (c..e).find(|&r| m[r][c] != 0)?;
            m.swap(c, p);
            v.swap(c, p);
            let f = self.inv(m[c][c]);
            for k in 0..e {
                m[c][k] = self.mul(m[c][k], f);
                v[c][k] = self.mul(v[c][k], f);
            }
            for r in 0..e {
                let f = m[r][c];
                if r == c || f == 0 {
                    continue;
                }
                for k in 0..e {
                    let (a, b) = (self.mul(f, m[c][k]), self.mul(f, v[c][k]));
                    m[r][k] ^= a;
                    v[r][k] ^= b;
                }
            }
        }
        Some(v)
    }
    /*
    ** Rebuild the bad DATA vectors (dok false) from the good ones and the
    ** good parity rows.  0 - rebuilt (or none bad); 1 - more bad than good
    ** rows, nothing touched; 2 - rebuilt, but a row left over disagrees.
    */
    fn repair(&self, data: &mut [Vec<u8>], dok: &[bool], par: &[Vec<u8>], pok: &[bool]) -> u8 {
        let lost: Vec<usize> = (0..data.len()).filter(|&i| !dok[i]).collect();
        let good: Vec<usize> = (0..par.len()).filter(|&j| pok[j]).collect();
        if good.len() < lost.len() {
            return 1;
        }
        let (rows, extra) = good.split_at(lost.len());
        let len = data.first().map_or(0, |d| d.len());
        if !lost.is_empty() {
            let mut syn: Vec<Vec<u8>> = Vec::with_capacity(lost.len());
            let mut mat: Vec<Vec<u8>> = Vec::with_capacity(lost.len());
            for &r in rows {
                let mut sv = par[r].clone();
                sv.resize(len, 0);
                for (i, d) in data.iter().enumerate() {
                    if dok[i] {
                        self.muladd(&mut sv, d, self.coef(r, i));
                    }
                }
                syn.push(sv);
                mat.push(lost.iter().map(|&l| self.coef(r, l)).collect());
            }
            let inv = match self.invert(mat) {
                Some(v) => v,
                None => return 1,
            };
            for (t, &l) in lost.iter().enumerate() {
                let mut out = vec![0u8; len];
                for (k, sv) in syn.iter().enumerate() {
                    self.muladd(&mut out, sv, inv[t][k]);
                }
                data[l] = out;
            }
        }
        for &x in extra {
            let mut sv = vec![0u8; len];
            for (i, d) in data.iter().enumerate() {
                self.muladd(&mut sv, d, self.coef(x, i));
            }
            if sv[..] != par[x][..len.min(par[x].len())] {
                return 2;
            }
        }
        0
    }
}

/* The test vectors of the standards (those of test/units.c): vbkx-rs selftest */
fn selftest() -> i32 {
    fn hex(b: &[u8]) -> String {
        b.iter().map(|x| format!("{:02x}", x)).collect()
    }
    fn sha(d: &[u8]) -> String {
        let mut s = Sha256::new();
        s.update(d);
        hex(&s.finish())
    }
    let mut failed = 0;
    let mut said = |name: &str, good: bool| {
        println!("selftest: {}: {}", name, if good { "ok" } else { "FAILED" });
        if !good {
            failed += 1;
        }
    };
    said("SHA-256 \"\"", sha(b"") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    said("SHA-256 \"abc\"", sha(b"abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    said(
        "SHA-256 448 bits",
        sha(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
    );
    {
        /* A million 'a', fed in pieces of every size from 1 up */
        let m = vec![b'a'; 1_000_000];
        let mut s = Sha256::new();
        let (mut o, mut n) = (0usize, 1usize);
        while o < m.len() {
            let k = n.min(m.len() - o);
            s.update(&m[o..o + k]);
            o += k;
            n = (n % 131) + 1;
        }
        said("SHA-256 a million 'a'", hex(&s.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }
    said(
        "HMAC-SHA256 RFC 4231 case 1",
        hex(&Hmac::new(&[0x0b; 20]).mac(&[b"Hi There"])) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
    );
    said(
        "HMAC-SHA256 RFC 4231 case 2",
        hex(&Hmac::new(b"Jefe").mac(&[b"what do ya want for nothing?"])) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
    );
    said(
        "HMAC-SHA256 RFC 4231 case 6",
        hex(&Hmac::new(&[0xaa; 131]).mac(&[b"Test Using Larger Than Block-Size Key - Hash Key First"]))
            == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
    );
    let mut d = [0u8; 64];
    pbkdf2(b"passwd", b"salt", 1, &mut d);
    said(
        "PBKDF2-HMAC-SHA256 passwd/salt/1",
        hex(&d) == "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783",
    );
    pbkdf2(b"Password", b"NaCl", 80000, &mut d);
    said(
        "PBKDF2-HMAC-SHA256 Password/NaCl/80000",
        hex(&d) == "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d",
    );
    let mut d32 = [0u8; 32];
    pbkdf2(b"password", b"salt", 4096, &mut d32);
    said("PBKDF2-HMAC-SHA256 password/salt/4096", hex(&d32) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    {
        let pt: &[u8] = b"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
        let mut key = [0u8; 32];
        for (i, k) in key.iter_mut().enumerate() {
            *k = i as u8;
        }
        let nonce = [0, 0, 0, 0, 0, 0, 0, 0x4a, 0, 0, 0, 0];
        let mut buf = pt.to_vec();
        chacha20(&key, &nonce, 1, &mut buf);
        said(
            "ChaCha20 RFC 8439 2.4.2",
            hex(&buf)
                == "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0bf91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab77937365af90bbf74a35be6b40b8eedf2785e42874d",
        );
        chacha20(&key, &nonce, 1, &mut buf);
        said("ChaCha20 decrypted back", buf == pt);
    }
    {
        /* Reed-Solomon: every erasure pattern of 6 DATA vectors and 3 rows; a forged row left over found */
        let g = Gf::new();
        let orig: Vec<Vec<u8>> = (0..6).map(|i| (0..29).map(|k| (i * 131 + k * 17 + i * k) as u8 ^ 0x5a).collect()).collect();
        let mut par: Vec<Vec<u8>> = vec![vec![0u8; 29]; 3];
        for (j, p) in par.iter_mut().enumerate() {
            for (i, d) in orig.iter().enumerate() {
                g.muladd(p, d, g.coef(j, i));
            }
        }
        let mut good = true;
        for set in 0u32..(1 << 9) {
            if set.count_ones() > 3 {
                continue;
            }
            let dok: Vec<bool> = (0..6).map(|i| set & (1 << i) == 0).collect();
            let pok: Vec<bool> = (0..3).map(|j| set & (1 << (6 + j)) == 0).collect();
            let mut d: Vec<Vec<u8>> = orig.iter().enumerate().map(|(i, v)| if dok[i] { v.clone() } else { vec![0xEE; 29] }).collect();
            good &= g.repair(&mut d, &dok, &par, &pok) == 0 && d == orig;
        }
        let dok = [true, false, true, true, true, true];
        let mut d = orig.clone();
        par[2][5] ^= 1;
        good &= g.repair(&mut d, &dok, &par, &[true, true, true]) == 2;
        good &= g.coef(0, 7) == 1;
        said("Reed-Solomon GF(2^8) 0x11D, scaled Cauchy, every pattern of 6 + 3", good);
    }
    {
        /* The products of the vector instructions against those of the portable code, octet by octet */
        let g = Gf::new();
        let src: Vec<u8> = (0..1100u32).map(|k| (k * 131 + (k >> 3)) as u8).collect();
        let mut same = true;
        for c in (0..256u32).step_by(5) {
            for off in 0..4usize {
                for len in (0..1090usize).step_by(97) {
                    let mut a = vec![0xA5u8; 1100];
                    let mut b = vec![0xA5u8; 1100];
                    let s0 = 3 - (off & 1);
                    g.muladd(&mut a[off..off + len], &src[s0..s0 + len], c as u8);
                    for k in 0..len {
                        g.muladd(&mut b[off + k..off + k + 1], &src[s0 + k..s0 + k + 1], c as u8);
                    }
                    same &= a == b;
                }
            }
        }
        said(&format!("Reed-Solomon products by {} = the portable ones", g.simd_name()), same);
        let crc = Crc::new();
        let data: Vec<u8> = (0..1000u32).map(|k| (k * 7 + 1) as u8).collect();
        let mut slow = !0u32;
        for &b in &data {
            slow = crc.table[((slow ^ b as u32) & 0xFF) as usize] ^ (slow >> 8);
        }
        said("CRC-32/IEEE eight octets a step = one at a time", crc.update(0, &data) == !slow && crc.update(0, b"123456789") == 0xCBF4_3926);
    }
    {
        /*
        ** The codecs 2 and 3 of DATAZ: streams of zlib (raw Deflate, level 1,
        ** level 9, the fixed codes) and of liblzma (raw LZMA1, lc=3 lp=0 pb=2,
        ** made by Python), a stored block made here; each must give the
        ** octets exactly, and none of them cut short, one octet more, a
        ** length one off or a bit turned may give anything but None
        ** silently wrong - or panic
        */
        let mut data: Vec<u8> = Vec::new();
        for k in 0..120 {
            data.extend_from_slice(format!("line {} of the vbkx-rs selftest\n", k).as_bytes());
        }
        data.extend((0..1000u32).map(|k| (k * k) as u8));
        fn unhex(h: &str) -> Vec<u8> {
            (0..h.len() / 2).map(|i| u8::from_str_radix(&h[2 * i..2 * i + 2], 16).unwrap_or(0)).collect()
        }
        fn unpack(codec: u32, z: &[u8], raw: u32) -> Option<Vec<u8>> {
            if codec == CODEC_DEFLATE {
                inflate(z, raw)
            } else {
                lzma_decompress(z, raw)
            }
        }
        let mut streams: Vec<(String, u32, Vec<u8>)> = [
        ("Deflate level 1", 2, "edd7af4b43511887f12bac68ba46db15560cc279cfefd3aeb062341a956d381c8a6e884661c57617178515a3d128ac188dc6c18ad1b826fe03e749b6e5271cf8f09ef01d0eae7a95aaaefbd5f8a257dd9d5fde1fde8eaa516fd81ff746e39de15f967cd6f96cf2d9e6b3cb679fcf219f633ea77c1662033701380139013a013b013c013d013e013f0d7e1afc34f869f0d3e0a7c14f839f063f0d7e1afc0cf819f033e067c0cf809f013f037e06fc0cf819f0b3e067c1cf829f053f0b7e16fc2cf859f0b3e067c1cf819f033f077e0efc1cf839f073e0e7c0cf819f033f0f7e1efc3cf879f0f3e0e7c1cf839f073f0f7e1efc02f805f00be017c02f805f00bf007e01fc02f805f08be017c12f825f04bf087e11fc22f845f08be017c12f815f02bf047e09fc12f825f04be097c02f815f023f5100280a044501a12830140588a2405114308a02475100298a24798a90248e115c233847708fe020c145829324b3498aadd676b9d796faa4fbd0bc2c96c57e67d0bcad76eb9bf957793c792f8e261fe5e9fca79eae52f3dd792ece1607cdbafb59bfb667e553ebb1786c3d95b3f66bfdd95d37078bb3e2b9f3dda4d5b4fe999f961f93a3e27d725c7ecd6feaddd55b33e8ec17cbc54bf3d03da9a5bd576eb7b636ef6ffc37f7f7ffffef17"),
        ("Deflate level 9", 2, "edd7ad4f42611cc5f1cb46d1748db6eb4621b83def2fedb2518c44a30e984ca6539893e846b15d22d18d623412dd2846a3918d4224d29cffc0f34d36f209bfedb3fb72ce7070d72b4471df2fc637bde2e9faf6f9fc71548c7ac3feb8371a1f0fff62998e553ad6e9d8a4639b8e5d3af6e938a4e3082cc4066e12e024c849a0936027014f829e043e097e0afc143d77e0a7c04f819f023f057e0afc14f829f0d3e0a7c14fd38b0b7e1afc34f869f0d3e0a7c14f839f013f037e06fc0c7df9c0cf809f013f037e06fc0cf859f0b3e067c1cf829fa55f07f859f0b3e067c1cf829f033f077e0efc1cf839f073f4ef053f077e0efc1cf879f0f3e0e7c1cf839f073f0f7e9eca0bf879f0f3e017c02f805f00bf007e01fc02f805f00bd4fec02f805f04bf087e11fc22f845f08be017c12f825fa4fa8cfd990ab4a0062da8420bead0824ab4a0162da8460bead1828ab420499e2224896304d708ce11dc2338487091e024496c92ac563fca4f1bb2ec7427d5fb6a9d9db507d57273523e2c7ef28be967d69a7ee5978b5d39dbc46adb7ecbae56cd6adffd2e3f1af3fcb5fe92bdd45ff379e3a3fceeeeabe6ea2a7b6b6fabb89995bbc565fe356d659fd38bfc67f1509e6c96d5a07d96ad57efd5a4db2965e3343faad70ef70ff70ff7fffffe2f"),
        ("Deflate fixed", 2, "cbc9cc4b553050c84f5328c94855284bcaaed02d2a56284ecd492b492d2ee1ca01491be29736c22f6d8c5fda04bfb4297e6933fcd2e6f8a52df04b5b12081642c14620dc0c09049c218190332410748604c2ce9040e01912083d4302c1674820fc8c08849f11a1744720fc8c08849f1181f03322107e4604c2cf8840f81911083f2302e1674c20fc8c09849f31a18c4b20fc8c09849f3181f03326107ec604c2cf9840f81913083f1302e1674220fc4c08849f09a1928f40f89910083f1302e1674220fc4c08849f0981f03325107ea604c2cf9440f89912083f5342550781f03325107ea604c2cf9440f89912083f3302e1674620fccc08849f1981f03323107e6684ea5e02e1674620fccc08849f1981f03327107ee604c2cf9c40f89913083f7302e1674e20fccc09355e08849f3981f03327107e1604c2cf8240f85910083f0b02e1674120fc2c08849f0581f0b320d4fa23107e1604c2cf9240f85912083f4b02e1674920fc2c09849f2581f0b324107e9604c2cf9250f39960fb995003da80500bda805013da80501bda805023da80502bda805033da80503bda805043da80504812ee8a100a49829d1182bd1182dd1182fd11821d12823d12825d123c7d120646164e0149154387c094ca092b8f3c645074c99cb0f389a043e1929b029e2d07191c5b4e0a442ef9e830f189e584972e0b19128f684ef89972d161a3ca4c814e96468646964e81992a1b1d2ea6fc9ca079249161a1cbcb09964f263a7c5c122970b2c591e1608ba7c0cd25850e824f764ec8745164787864e584ca944007431549014e16c651fb47ed1fb59ff6f60300"),
        ("LZMA", 3, "00361a4a1f08a026034d069df8b2a5acb4809fd1e9afa97f6e1892f157c7ae473bc72bb7bca5c85519748c54bc5ef6835beae3f19f2e87d92577b4328350d2139ffae4915ed0de58f644282f5484dff3f7ca1732dd0d96bc16bb6f87df0f2587cc95e62f09992e5f9e6157fddf4b70004f9ce4e9e4fab9f0831f8094359065b8214aa96322d8649b9b9a30647757726a1d44a12099fb0441199726c8c6a9f3a2f262fdeaa794549cf31e30ad9890d8a2e62d73477c73d181d47d310bbdeaca0e592763283495a99e3b8a966200d54180d1463f71bf4296431004345fcc201d8c9c7c56141b4157f748af7d67abebabed4c2614ac4d39ff1ef4919dde081f776ddd76d65e540d12f0895c889b5b8394d7c1d738acca6b2aed20011a3249ee5fd9d341f20577f233cdcb4af059cb387ca7844542377b6ca0eb7a04c37bf7e1be33c1904f88805850823fffffd7822840"),
        ("LZMA extreme", 3, "00361a4a1f08a026034d069df8b2a5acb4809fd1e9afa97f6e1892f157c7ae473bc72bb7bca5c85519748c54bc5ef6835beae3f19f2e87d92577b4328350d2139ffae4915ed0de58f644282f5484dff3f7ca1732dd0d96bc16bb6f87df0f2587cc95e62f09992e5f9e6157fddf4b70004f9ce4e9e4fab9f0831f8094359065b8214aa96322d8649b9b9a30647757726a1d44a12099fb0441199726c8c6a9f3a2f262fdeb235eaa1de0a9f78e30d1c6e9c1569f3fab6f88e3ceed11d7c793c653a053969197bbad4039fd36be273c4e0846e73f2ad418880d3e9de740553f3a32baceced290f9b462ae5fdcdaa639079e35a464dec0c2993f9b6e83659d0cf5ecff40bd1f8ea7c7a7da4fb646070ce5dc32f27dd769d0d3ad40372f05f8f088581d1641df9730fbd3a67b981604b935143acad45eade0e208282c7e132bdd2fd6f81d3d81539431f6f3ffffbea669a0"),
        ]
        .iter()
        .map(|&(n, c, h)| (n.to_string(), c, unhex(h)))
        .collect();
        {
            /* Stored: BFINAL, BTYPE 0, to the octet, LEN, NLEN, the octets */
            let n = data.len() as u16;
            let mut z = vec![1u8];
            z.extend_from_slice(&n.to_le_bytes());
            z.extend_from_slice(&(!n).to_le_bytes());
            z.extend_from_slice(&data);
            streams.push(("Deflate stored".to_string(), CODEC_DEFLATE, z));
        }
        let raw = data.len() as u32;
        for (name, codec, z) in &streams {
            said(&format!("{}: the octets", name), unpack(*codec, z, raw).as_deref() == Some(&data[..]));
            let mut longer = z.clone();
            longer.push(0);
            let refused = unpack(*codec, &z[..z.len() - 1], raw).is_none()
                && unpack(*codec, &longer, raw).is_none()
                && unpack(*codec, z, raw + 1).is_none()
                && unpack(*codec, z, raw - 1).is_none();
            said(&format!("{}: cut, longer, a length off refused", name), refused);
            let (mut seed, mut silent) = (12345u32, 0);
            for _ in 0..300 {
                seed = seed.wrapping_mul(1103515245).wrapping_add(12345);
                let mut t = z.clone();
                let pos = (seed >> 8) as usize % t.len();
                t[pos] ^= 1 << ((seed >> 4) & 7);
                if let Some(d) = unpack(*codec, &t, raw) {
                    if d != data && *codec == CODEC_LZMA {
                        silent += 1;
                    }
                }
            }
            /* Deflate has no check of its own (the CRC of the block and of FEND are): no panic is the test; LZMA ends checked */
            said(&format!("{}: bits turned, no panic{}", name, if *codec == CODEC_LZMA { ", none silently wrong" } else { "" }), silent == 0);
        }
    }
    if failed == 0 {
        println!("selftest: all primitives right");
        0
    } else {
        println!("selftest: {} failed", failed);
        1
    }
}

/* The block header, format.md section 3 */
#[derive(Clone, Copy, Default)]
struct Bhdr {
    version: u16,
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
    if b.len() != bsize as usize || b.len() < HDR || &b[0..4] != b"VBKB" || u16_at(b, 4) != HDR as u16 || !(1..=3).contains(&u16_at(b, 6)) {
        return None;
    }
    let mut h = Bhdr {
        version: u16_at(b, 6),
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
    /* The parity blocks of version 2 (and 3, PARITY >= 2) carry the header parity in RECOFF and PAYLEN (format.md 4.1) */
    let hpar = h.version >= 2 && (h.typ == BT_XOR || h.typ == BT_PARITY);
    if u32_at(b, 8) != bsize
        || h.typ < BT_DATA
        || h.typ > BT_PARITY
        || (h.typ == BT_PARITY && h.version < 2)
        || (!hpar && (h.paylen > psize || (h.recoff != NONE && h.recoff >= psize)))
    {
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

/* The header alone could be that of a block of this size: the whole block is not read for nothing */
fn head_is(f: &File, bsize: u32, pos: u64) -> bool {
    let mut h = [0u8; 16];
    match f.read_at(&mut h, pos.saturating_mul(bsize as u64)) {
        Ok(16) => &h[0..4] == b"VBKB" && u16_at(&h, 4) == HDR as u16 && (1..=3).contains(&u16_at(&h, 6)) && u32_at(&h, 8) == bsize,
        _ => false,
    }
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
    parity: u32,  // parity blocks of a group: 1, or 2 .. 8 in version 2 (and 3)
    version: u16, // of every block: that of the VHDR
    gf: Gf,
    uuid: [u8; 16],
    vols: Vec<Volume>,
    trailer: bool,

    /* Encryption, format.md 6.10: what the VHDR says, the keys once the passphrase is right */
    vcrypt: Option<Vcrypt>,  // a VHDR with CIPHER; None - a plain saveset
    keys: Option<Keys>,
    dtype: u8,               // what a DATA position holds: DATA, or EDATA when encrypted
    ttype: u8,               // TRAILER, or ETRAILER
    cap: u32,                // the most PAYLEN of a DATA block: P, or P - 32 when encrypted
    trlraw: Option<Vec<u8>>, // the ETRAILER, kept until its TAG can be checked
    enc_seen: bool,          // an EDATA block met while the block size was guessed

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

    /* The SOLID being read (version 3, format.md 6.12): its records, decompressed */
    sol: Vec<u8>,
    solpos: usize, // the next record in it
    solblk: u64,   // where it begins: the messages
    solvol: usize,
}

fn volspec(spec: &str, n: u32) -> String {
    if n <= 1 {
        spec.to_string()
    } else {
        format!("{}.{:03}", spec, n)
    }
}

/* The encryption tags of a VHDR SUMMARY, format.md 6.10 */
#[derive(Clone, Default)]
struct Vcrypt {
    cipher: u64,
    kdf: u64,
    kdfiter: u64,
    salt: Vec<u8>,
    check: Vec<u8>,
}

/* The group size, the parity count and the encryption tags (CIPHER makes it encrypted), out of the SUMMARY record a VHDR carries */
fn summary_group(blk: &[u8], h: &Bhdr) -> Option<(u32, u32, Option<Vcrypt>)> {
    let pay = blk.get(HDR..HDR + h.paylen as usize)?;
    if pay.len() < 8 || u16_at(pay, 0) != RT_SUMMARY {
        return None;
    }
    let body = pay.get(8..8usize.saturating_add(u32_at(pay, 4) as usize))?;
    let mut grp = 0u32;
    let mut par = 0u32;
    let mut has_cipher = false;
    let mut vc = Vcrypt::default();
    tlv_each(body, |tag, v| match tag {
        TAG_GROUPSIZE => grp = getu(v) as u32,
        TAG_PARITY => par = getu(v).min(255) as u32,
        TAG_CIPHER => {
            has_cipher = true;
            vc.cipher = getu(v)
        }
        TAG_KDF => vc.kdf = getu(v),
        TAG_KDFITER => vc.kdfiter = getu(v),
        TAG_SALT => vc.salt = v.to_vec(),
        TAG_KEYCHECK => vc.check = v.to_vec(),
        _ => {}
    });
    if grp <= MAXGRP {
        Some((grp, par, if has_cipher { Some(vc) } else { None }))
    } else {
        None
    }
}

impl Reader {
    fn open(spec: &str) -> Result<Reader, String> {
        let mut f = File::open(spec).map_err(|e| oserr(&format!("File: {}", spec), &e, "cannot be opened"))?;
        let mut r = Reader {
            crc: Crc::new(),
            spec: spec.to_string(),
            bsize: 0,
            grpsz: 0,
            parity: 1,
            version: 1,
            gf: Gf::new(),
            uuid: [0; 16],
            vols: Vec::new(),
            trailer: false,
            vcrypt: None,
            keys: None,
            dtype: BT_DATA,
            ttype: BT_TRAILER,
            cap: 0,
            trlraw: None,
            enc_seen: false,
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
            sol: Vec::new(),
            solpos: 0,
            solblk: 0,
            solvol: 0,
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
                    if let Some((g, m, vc)) = summary_group(&blk, &h) {
                        r.bsize = bs;
                        r.uuid = h.uuid;
                        r.grpsz = g;
                        r.version = h.version;
                        /* Version 3: the groups of version 1, or of version 2 with PARITY >= 2 */
                        r.parity = if h.version == 2 || (h.version == 3 && m >= 2) { m } else { 1 };
                        r.vcrypt = vc;
                        found = true;
                    }
                }
            }
        }
        if !found && !r.guess(&f) {
            return Err(format!("File: {} - is not a saveset", spec));
        }
        /* Version 2 (3 with PARITY): as many parity blocks as the SUMMARY says, and groups (format.md 4.1) */
        if (r.version == 2 || r.parity > 1) && (r.parity < 2 || r.parity > MAXPAR || r.grpsz == 0) {
            return Err(format!("File: {} - is not a saveset", spec));
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
                Some(h) if h.typ == BT_VHDR && h.volno == n => {
                    /* Volume 1 had no good VHDR: the keys are in any other one (the same in all) */
                    if !found && r.vcrypt.is_none() {
                        if let Some((_, m, vc)) = summary_group(&read_block(Some(&vf), r.bsize, 0), &h) {
                            r.vcrypt = vc;
                            if h.version >= 2 && h.version == r.version && m >= 2 {
                                r.parity = m;
                            }
                        }
                    }
                    first = Some(h.blkno)
                }
                _ => match check(&r.crc, &read_block(Some(&vf), r.bsize, 1), r.bsize, Some(&r.uuid)) {
                    Some(h) if h.volno == n && h.blkno > 0 => {
                        msg!("Volume: {} - its first block is bad, it is read all the same", n);
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
                    msg!("Volume: {} - belongs to another saveset, or is none", n);
                    miss += 1;
                }
            }
            n += 1;
        }

        /* Encrypted: an algorithm not known here, and nothing of it can be read - said at once */
        r.cap = r.bsize - HDR as u32;
        if let Some(vc) = &r.vcrypt {
            if vc.cipher != CIPHER_CC20HS
                || vc.kdf != KDF_PBKDF2
                || vc.kdfiter < KDFMIN as u64
                || vc.kdfiter > u32::MAX as u64
                || vc.salt.len() != 32
                || vc.check.len() != 32
            {
                return Err(format!("Saveset: {} - an encryption this extractor does not know: it cannot be read", spec));
            }
            r.dtype = BT_EDATA;
            r.ttype = BT_ETRAILER;
            r.cap -= TAGSZ as u32;
        } else if r.enc_seen {
            return Err(format!("Saveset: {} - is encrypted, and no volume has a readable VHDR to give its keys", spec));
        }

        /* The TRAILER, last block of the last volume: it is not part of the groups */
        if let Some(lv) = r.vols.last() {
            if lv.nblk > 1 {
                let b = read_block(lv.f.as_ref(), r.bsize, lv.nblk - 1);
                if let Some(h) = check(&r.crc, &b, r.bsize, Some(&r.uuid)) {
                    r.trailer = h.typ == r.ttype;
                    if r.trailer && r.vcrypt.is_some() {
                        r.trlraw = Some(b);
                    }
                }
            }
        }
        Ok(r)
    }

    /*
    ** The passphrase of an encrypted saveset: the keys derived from it and
    ** the SALT, judged by the KEYCHECK; false - it is not the right one.
    ** The ETRAILER is then checked by its TAG: when it fails, that is said
    ** and the saveset is read without a catalog - which is how this
    ** extractor reads every saveset anyway.
    */
    fn setkey(&mut self, pass: &[u8]) -> bool {
        let vc = match &self.vcrypt {
            Some(vc) => vc,
            None => return true,
        };
        let k = derive(pass, &vc.salt, vc.kdfiter as u32);
        if !equal(&k.check, &vc.check) {
            return false;
        }
        if let Some(b) = self.trlraw.take() {
            let good = match check(&self.crc, &b, self.bsize, Some(&self.uuid)) {
                Some(h) => tag_ok(&k, self.bsize, &h, &b[HDR..]),
                None => false,
            };
            if !good {
                msg!("Saveset: {} - its trailer fails its authentication: read as a saveset without a catalog", self.spec);
                self.bad = true;
            }
        }
        self.keys = Some(k);
        true
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
                if !head_is(f, bs, pos) {
                    continue;
                }
                let h = match check(&self.crc, &read_block(Some(f), bs, pos), bs, None) {
                    Some(h) if h.volno == 1 && h.blkno == pos => h,
                    _ => continue,
                };
                self.bsize = bs;
                self.uuid = h.uuid;
                self.grpsz = 0;
                self.version = h.version;
                self.parity = 1;
                self.enc_seen = h.typ == BT_EDATA || h.typ == BT_ETRAILER;
                for p in 1..=(MAXGRP as u64 + 1) {
                    if let Some(x) = check(&self.crc, &read_block(Some(f), bs, p), bs, Some(&self.uuid)) {
                        self.enc_seen |= x.typ == BT_EDATA || x.typ == BT_ETRAILER;
                        if x.typ == BT_XOR && (x.gindex & 0xFF) as u64 == p - 1 && x.version == self.version {
                            self.grpsz = (x.gindex & 0xFF) as u32;
                            /* Version 2 and 3: the PARITY blocks after it, row by row, are the parity count */
                            if self.version >= 2 {
                                self.parity = 1;
                                while self.parity < MAXPAR {
                                    match check(&self.crc, &read_block(Some(f), bs, p + self.parity as u64), bs, Some(&self.uuid)) {
                                        Some(y) if y.typ == BT_PARITY && y.gindex == (self.grpsz | (self.parity << 8)) as u16 => self.parity += 1,
                                        _ => break,
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
                msg!("Saveset: {} - its first block is bad: block size {} and group size {} found by trying", self.spec, bs, self.grpsz);
                if self.version == 2 && self.parity < 2 {
                    return false;
                }
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
    ** The good DATA blocks of a group as the writer made them past PAYLEN:
    ** zeros up to the TAG.  What lies there carries nothing, no TAG covers
    ** it; a byte changed there would make the parity disagree, and nothing
    ** of the group be rebuilt.
    */
    fn canon(&self, blks: &mut [Vec<u8>], hdrs: &[Bhdr], ok: &[bool], n: usize) {
        let end = HDR + self.cap as usize;
        for i in 0..n {
            if ok[i] && hdrs[i].typ == self.dtype && hdrs[i].paylen < self.cap {
                for x in blks[i][HDR + hdrs[i].paylen as usize..end].iter_mut() {
                    *x = 0;
                }
            }
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
                msg!("Volume: {} - is missing", self.curvol);
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
        let mut n: u64 = if self.grpsz > 0 { self.grpsz as u64 + self.parity as u64 } else { 1 };
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
                    ok.push(h.blkno == firstblk.wrapping_add(pos) && h.volno as usize == self.curvol && h.version == self.version);
                    hdrs.push(h);
                }
                None => {
                    ok.push(false);
                    hdrs.push(Bhdr::default());
                }
            }
            blks.push(b);
        }

        /* Version 2: several parity blocks, Reed-Solomon */
        if self.parity > 1 {
            return self.group2(firstblk, blks, hdrs, ok);
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
            if ok[i] && hdrs[i].typ != self.dtype {
                ok[i] = false;
            }
            /* A good CRC and a wrong TAG: changed on purpose - a bad block all the same, repairable as any */
            if ok[i] {
                if let Some(k) = &self.keys {
                    if !tag_ok(k, self.bsize, &hdrs[i], &blks[i][HDR..]) {
                        ok[i] = false;
                        msg!(
                            "Block: {}, Volume: {} - is not what was written: its CRC is right, its authentication fails",
                            hdrs[i].blkno,
                            self.curvol
                        );
                    }
                }
            }
            if !ok[i] {
                nbad += 1;
                badi = i;
            }
        }

        if nbad > 0 {
            self.canon(&mut blks, &hdrs, &ok, gdata);
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
            let h = Bhdr {
                version: self.version,
                typ: self.dtype,
                gindex: badi as u16,
                uuid: self.uuid,
                blkno: firstblk.wrapping_add(self.curpos + badi as u64),
                volno: self.curvol as u32,
                recoff: s.prvrecoff,
                paylen: s.prvpaylen,
                prvrecoff: NONE,
                prvpaylen: 0,
            };
            /* Encrypted: the block rebuilt must pass its TAG too */
            if h.paylen <= self.cap
                && (h.recoff == NONE || h.recoff < h.paylen)
                && self.keys.as_ref().map_or(true, |k| tag_ok(k, self.bsize, &h, &d))
            {
                blks[badi][HDR..].copy_from_slice(&d);
                hdrs[badi] = h;
                ok[badi] = true;
                msg!("Block: {}, Volume: {} - was bad, rebuilt from its group", h.blkno, self.curvol);
            }
        }

        /* Every check done, the repair too: now the good blocks are decrypted where they lie */
        if let Some(k) = &self.keys {
            for i in 0..gdata {
                if ok[i] {
                    decrypt(k, &hdrs[i], &mut blks[i][HDR..]);
                }
            }
        }

        self.pays.clear();
        self.recoffs.clear();
        self.blks.clear();
        for i in 0..gdata {
            let b = firstblk.wrapping_add(self.curpos + i as u64);
            if !ok[i] {
                msg!("Block: {}, Volume: {} - is bad and cannot be rebuilt", b, self.curvol);
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
    ** The rest of a group of version 2 (format.md 4.1): d DATA blocks and m
    ** parity blocks; up to as many bad DATA blocks as there are good rows
    ** rebuilt, payloads and the header parity alike; the rebuilt headers
    ** checked as good ones are; when that fails, once more without each
    ** good row in turn.
    */
    fn group2(&mut self, firstblk: u64, mut blks: Vec<Vec<u8>>, mut hdrs: Vec<Bhdr>, mut ok: Vec<bool>) -> bool {
        let n = blks.len();
        let m = self.parity as usize;
        let grpsz = self.grpsz as usize;

        /* How many DATA blocks: a good parity block says it (n in GINDEX, its row in the high octet) */
        let mut d: Option<usize> = None;
        for i in 0..n {
            let (row, cnt) = ((hdrs[i].gindex >> 8) as usize, (hdrs[i].gindex & 0xFF) as usize);
            if ok[i]
                && ((hdrs[i].typ == BT_XOR && row == 0) || (hdrs[i].typ == BT_PARITY && row >= 1 && row < m))
                && cnt >= 1
                && cnt <= grpsz
                && cnt + row == i
            {
                d = Some(cnt);
                break;
            }
        }
        let d = match d {
            Some(d) => d,
            None if n == grpsz + m => grpsz,
            None if ok[n - 1] && hdrs[n - 1].typ == self.dtype => n,
            None if n > m => n - m,
            None => n,
        }
        .min(n);

        /* The parity rows there are, each where its row says */
        let mut pok = vec![false; m];
        let mut par: Vec<Vec<u8>> = Vec::with_capacity(m);
        let mut hpar: Vec<Vec<u8>> = Vec::with_capacity(m);
        for (j, pk) in pok.iter_mut().enumerate() {
            let i = d + j;
            let mut hv = vec![0u8; 8];
            if i < n {
                let h = &hdrs[i];
                *pk = ok[i] && h.typ == if j == 0 { BT_XOR } else { BT_PARITY } && h.gindex as usize == (d | (j << 8));
                if *pk {
                    hv[0..4].copy_from_slice(&h.recoff.to_le_bytes());
                    hv[4..8].copy_from_slice(&h.paylen.to_le_bytes());
                }
                par.push(blks[i][HDR..].to_vec());
            } else {
                par.push(Vec::new());
            }
            hpar.push(hv);
        }

        for i in 0..d {
            if ok[i] && (hdrs[i].typ != self.dtype || hdrs[i].gindex as usize != i) {
                ok[i] = false;
            }
            /* A good CRC and a wrong TAG: a bad block all the same */
            if ok[i] {
                if let Some(k) = &self.keys {
                    if !tag_ok(k, self.bsize, &hdrs[i], &blks[i][HDR..]) {
                        ok[i] = false;
                        msg!(
                            "Block: {}, Volume: {} - is not what was written: its CRC is right, its authentication fails",
                            hdrs[i].blkno,
                            self.curvol
                        );
                    }
                }
            }
        }

        let dok: Vec<bool> = ok[..d].to_vec();
        let nbad = dok.iter().filter(|&&x| !x).count();
        let npok = pok.iter().filter(|&&x| x).count();
        if nbad > 0 {
            self.canon(&mut blks, &hdrs, &ok, d);
        }
        if nbad > 0 && nbad <= npok {
            let mut forged = false;
            let mut done = false;
            let mut skip: isize = -1;
            while !done && skip < m as isize {
                let mut tryok = pok.clone();
                if skip >= 0 {
                    let s = skip as usize;
                    if !pok[s] {
                        skip += 1;
                        continue;
                    }
                    tryok[s] = false;
                }
                skip += 1;
                if tryok.iter().filter(|&&x| x).count() < nbad {
                    continue;
                }
                /* The header parity first, then the payloads */
                let mut hv: Vec<Vec<u8>> = (0..d)
                    .map(|i| {
                        let mut v = vec![0u8; 8];
                        if dok[i] {
                            v[0..4].copy_from_slice(&hdrs[i].recoff.to_le_bytes());
                            v[4..8].copy_from_slice(&hdrs[i].paylen.to_le_bytes());
                        }
                        v
                    })
                    .collect();
                let mut rc = self.gf.repair(&mut hv, &dok, &hpar, &tryok);
                let mut pv: Vec<Vec<u8>> = Vec::new();
                if rc == 0 {
                    pv = (0..d).map(|i| blks[i][HDR..].to_vec()).collect();
                    rc = self.gf.repair(&mut pv, &dok, &par, &tryok);
                }
                forged |= rc == 2;
                if rc != 0 {
                    continue;
                }
                let mut newh: Vec<Bhdr> = hdrs[..d].to_vec();
                let mut allok = true;
                for i in 0..d {
                    if dok[i] {
                        continue;
                    }
                    let (prvrecoff, prvpaylen) = if i > 0 { (newh[i - 1].recoff, newh[i - 1].paylen) } else { (NONE, 0) };
                    let h = Bhdr {
                        version: self.version,
                        typ: self.dtype,
                        gindex: i as u16,
                        uuid: self.uuid,
                        blkno: firstblk.wrapping_add(self.curpos + i as u64),
                        volno: self.curvol as u32,
                        recoff: u32_at(&hv[i], 0),
                        paylen: u32_at(&hv[i], 4),
                        prvrecoff,
                        prvpaylen,
                    };
                    if h.paylen > self.cap
                        || (h.recoff != NONE && h.recoff >= h.paylen)
                        || !self.keys.as_ref().map_or(true, |k| tag_ok(k, self.bsize, &h, &pv[i]))
                    {
                        allok = false;
                        break;
                    }
                    newh[i] = h;
                }
                if allok {
                    done = true;
                    for i in 0..d {
                        if dok[i] {
                            continue;
                        }
                        blks[i][HDR..].copy_from_slice(&pv[i]);
                        hdrs[i] = newh[i];
                        ok[i] = true;
                        msg!("Block: {}, Volume: {} - was bad, rebuilt from its group", hdrs[i].blkno, self.curvol);
                    }
                }
            }
            if !done && forged {
                msg!(
                    "Block: {}, Volume: {} - the group beginning here does not agree with its parity: nothing of it is rebuilt",
                    firstblk.wrapping_add(self.curpos),
                    self.curvol
                );
                self.bad = true;
            }
        }

        /* Every check done, the repair too: the good blocks decrypted where they lie */
        if let Some(k) = &self.keys {
            for i in 0..d {
                if ok[i] {
                    decrypt(k, &hdrs[i], &mut blks[i][HDR..]);
                }
            }
        }

        self.pays.clear();
        self.recoffs.clear();
        self.blks.clear();
        for i in 0..d {
            let b = firstblk.wrapping_add(self.curpos + i as u64);
            if !ok[i] {
                msg!("Block: {}, Volume: {} - is bad and cannot be rebuilt", b, self.curvol);
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

    /*
    ** A SOLID record (format.md 6.12) opened: its records decompressed into
    ** SOL, to be taken one by one; false - it does not open: an unknown
    ** codec, a length out of bounds, a stream that is not right.
    */
    fn solopen(&mut self, body: &[u8]) -> bool {
        self.sol = Vec::new();
        self.solpos = 0;
        if body.len() < SOLIDHDR {
            return false;
        }
        let rawlen = u32_at(body, 4);
        if rawlen > MAXSOLID || rawlen < 8 {
            return false;
        }
        let z = &body[SOLIDHDR..];
        let d = match u32_at(body, 0) {
            CODEC_LZ4 => lz4_decompress(z, rawlen),
            CODEC_DEFLATE => inflate(z, rawlen),
            CODEC_LZMA => lzma_decompress(z, rawlen),
            _ => None,
        };
        match d {
            Some(d) if d.len() == rawlen as usize => {
                self.sol = d;
                true
            }
            _ => false,
        }
    }

    /* The next record of the open SOLID: FILE, DATA or FEND only, whole in it; None - anything else, the rest dropped */
    fn solnext(&mut self) -> Option<(u16, Vec<u8>)> {
        let left = self.sol.len() - self.solpos;
        let (typ, len) = (u16_at(&self.sol, self.solpos), u32_at(&self.sol, self.solpos + 4) as usize);
        if left < 8 || !matches!(typ, RT_FILE | RT_DATA | RT_FEND) || len > left - 8 {
            self.sol = Vec::new();
            self.solpos = 0;
            return None;
        }
        let at = self.solpos + 8;
        self.solpos = at + len;
        Some((typ, self.sol[at..at + len].to_vec()))
    }

    /* The next record of the stream (section 5); None - the end.  RESYNC: blocks were lost before it. */
    fn next_record(&mut self) -> Option<(u16, Vec<u8>)> {
        let mut resync = false;
        loop {
            /* The records of an open SOLID come first, as if they were in the stream */
            if self.solpos < self.sol.len() {
                if let Some(rec) = self.solnext() {
                    self.resync = resync;
                    return Some(rec);
                }
                /* A record in it that makes no sense: the rest is a gap, its files are named lost */
                msg!("Block: {}, Volume: {} - a SOLID record that is not right, the rest of it is lost", self.solblk, self.solvol);
                self.bad = true;
                resync = true;
            }
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
                msg!("Block: {}, Volume: {} - an invalid record, skipped", self.payblk, self.payvol);
                self.bad = true;
                self.gap = true;
                self.pay = None;
                continue;
            }
            self.payoff += 8;
            let (hblk, hvol) = (self.payblk, self.payvol);
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
                msg!("Saveset: {} - ends inside a record", self.spec);
                self.bad = true;
                self.resync = true;
                return None;
            }
            /* Version 3: a SOLID is opened, and its records taken from the top of the loop */
            if typ == RT_SOLID && self.version == 3 {
                self.solblk = hblk;
                self.solvol = hvol;
                if !self.solopen(&body) {
                    /* It does not open: what it held is a gap, its files are named lost */
                    msg!("Block: {}, Volume: {} - a SOLID record that does not open, its files are lost", hblk, hvol);
                    self.bad = true;
                    resync = true;
                }
                continue;
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
fn parents(out: &Path, name: &[u8], create: bool) -> io::Result<()> {
    let comps: Vec<&[u8]> = name.split(|&c| c == b'/').collect();
    let mut p = out.to_path_buf();
    for c in comps.iter().take(comps.len().saturating_sub(1)) {
        p.push(std::ffi::OsStr::from_bytes(c));
        match fs::symlink_metadata(&p) {
            Ok(m) => {
                if !m.is_dir() {
                    return Err(io::Error::from_raw_os_error(20)); // ENOTDIR: a link, or no directory
                }
            }
            Err(e) => {
                if !create {
                    return Err(e);
                }
                fs::create_dir(&p)?;
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
                msg!("Record: FILE - makes no sense, skipped");
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
            msg!("File: {} - its name leads out of the output directory, not extracted", name);
            r.bad = true;
            return;
        }
        if let Err(err) = parents(&self.out, &e.path, true) {
            msg!("{}", oserr(&format!("File: {}", name), &err, "a directory on the way cannot be made, or is a link, not extracted"));
            r.bad = true;
            return;
        }
        let path = join(&self.out, &e.path);
        if e.ftype == FT_DIR {
            if let Err(err) = fs::create_dir(&path) {
                if err.kind() != io::ErrorKind::AlreadyExists {
                    msg!("{}", oserr(&format!("File: {}", name), &err, "cannot be made, not extracted"));
                    r.bad = true;
                    return;
                }
            }
            self.dirs.push(e);
            return;
        }
        if fs::symlink_metadata(&path).is_ok() {
            msg!("File: {} - already exists, not extracted (never overwritten)", name);
            r.bad = true;
            return;
        }
        /* Err: the message, whole */
        let what = format!("File: {}", name);
        let made = |err: io::Error| oserr(&what, &err, "cannot be made, not extracted");
        let res: Result<(), String> = match e.ftype {
            FT_REG => match OpenOptions::new().write(true).create_new(true).open(&path) {
                Ok(f) => {
                    self.f = Some(f);
                    self.active = true;
                    self.path = path.clone();
                    Ok(())
                }
                Err(err) => Err(made(err)),
            },
            FT_SYMLINK => match std::os::unix::fs::symlink(std::ffi::OsStr::from_bytes(&e.link), &path) {
                Ok(()) => {
                    set_times(&path, &e, true);
                    Ok(())
                }
                Err(err) => Err(made(err)),
            },
            FT_HARDLINK => {
                if !name_ok(&e.link) {
                    Err(format!("{} - a link that leads out of the output directory, not extracted", what))
                } else {
                    parents(&self.out, &e.link, false).and_then(|_| fs::hard_link(join(&self.out, &e.link), &path)).map_err(made)
                }
            }
            FT_FIFO => match CString::new(path.as_os_str().as_bytes()) {
                Ok(c) => {
                    if sys::make_fifo(&c) {
                        set_mode(&path, e.mode);
                        set_times(&path, &e, false);
                        Ok(())
                    } else {
                        Err(made(io::Error::last_os_error()))
                    }
                }
                Err(_) => Err(format!("{} - a FIFO, cannot be made, not extracted", what)),
            },
            _ => Ok(()), // devices and sockets are not made
        };
        if let Err(err) = res {
            msg!("{}", err);
            r.bad = true;
        }
    }

    /*
    ** A DATA record (u32 fileno, u32 0, u64 offset, the bytes) or a DATAZ one
    ** (u32 fileno, u32 codec, u64 offset, u32 rawlen, LZ4, Deflate or LZMA); a DATAZ
    ** that does not decompress leaves the file incomplete
     */
    fn data(&mut self, r: &Reader, typ: u16, body: &[u8]) {
        if !self.active {
            return;
        }
        let (fileno, off, view) = match data_view(typ, body) {
            Some(v) => v,
            None => {
                msg!("File: {} - a data record that makes no sense", shown(&self.e.path));
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
                msg!("{}", oserr(&format!("File: {}", shown(&self.e.path)), &err, "cannot be written"));
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
            msg!("File: {} - checksum mismatch: the data differ from what was saved", name);
            self.damaged = true;
        }
        if self.damaged {
            msg!("File: {} - is incomplete: its data was lost in bad blocks", name);
            r.bad = true;
        } else if status == FS_CHANGED {
            msg!("File: {} - changed while it was saved: the copy may be a mix", name);
        } else if status == FS_READERR {
            msg!("File: {} - could not be read whole when it was saved", name);
        }
        if let Some(f) = self.f.take() {
            if size <= 1 << 62 {
                let _ = f.set_len(size);
            }
            if let Err(err) = f.sync_all() {
                msg!("{}", oserr(&format!("File: {}", name), &err, "cannot be written"));
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
                    msg!("File: {} - not extracted: its records were lost in bad blocks", shown(&e.path));
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
        msg!("Saveset: {} - files missing from the output cannot all be named", r.spec);
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

/* An error of the system as strerror says it, without Rust's " (os error N)" */
fn syserr(e: &io::Error) -> String {
    let t = e.to_string();
    match t.find(" (os error ") {
        Some(i) => t[..i].to_string(),
        None => t,
    }
}

/* "Label: value, errno: N - words (text)"; an error without a number: "Label: value - words (text)" */
fn oserr(label: &str, e: &io::Error, words: &str) -> String {
    match e.raw_os_error() {
        Some(n) => format!("{}, errno: {} - {} ({})", label, n, words, syserr(e)),
        None => format!("{} - {} ({})", label, words, syserr(e)),
    }
}

/*
** The passphrase of an encrypted saveset: the first line of the key file
** (-k, else VBACKUP_KEY_FILE) - one only its owner may read or write -
** else asked for on the terminal without echo (stty, std has no termios).
** Its bytes as they are, the line end (LF or CR LF) left out.  None -
** none to be had (said).
*/
fn passphrase(keyfile: Option<&str>, spec: &str) -> Option<Vec<u8>> {
    let env = std::env::var_os("VBACKUP_KEY_FILE");
    let kf: Option<PathBuf> = match keyfile {
        Some(k) => Some(PathBuf::from(k)),
        None => env.filter(|e| !e.is_empty()).map(PathBuf::from),
    };
    let mut line: Vec<u8> = Vec::new();
    if let Some(kf) = kf.filter(|k| !k.as_os_str().is_empty()) {
        let name = kf.display().to_string();
        let mut f = match File::open(&kf) {
            Ok(f) => f,
            Err(e) => {
                msg!("{}", oserr(&format!("Key file: {}", name), &e, "cannot be read"));
                return None;
            }
        };
        match f.metadata() {
            Ok(m) if m.is_file() && m.mode() & 0o077 == 0 => {}
            _ => {
                msg!("Key file: {} - not a regular file, or others may read it: chmod 600 it", name);
                return None;
            }
        }
        let mut b = [0u8; 1];
        while line.len() <= PASSMAX + 1 {
            match f.read(&mut b) {
                Ok(1) if b[0] != b'\n' => line.push(b[0]),
                Err(ref e) if e.kind() == io::ErrorKind::Interrupted => continue,
                _ => break,
            }
        }
    } else {
        let tty = OpenOptions::new().read(true).write(true).open("/dev/tty");
        let stty = |arg: &str, t: &File| -> Option<Vec<u8>> {
            let o = Command::new("stty").arg(arg).stdin(Stdio::from(t.try_clone().ok()?)).stderr(Stdio::null()).output().ok()?;
            if o.status.success() {
                Some(o.stdout)
            } else {
                None
            }
        };
        let mut t = match tty {
            Ok(t) => t,
            Err(_) => {
                msg!("Saveset: {} - is encrypted, and there is no terminal to ask the passphrase on: give -k file", spec);
                return None;
            }
        };
        let saved = match stty("-g", &t) {
            Some(g) => String::from_utf8_lossy(&g).trim().to_string(),
            None => {
                msg!("Saveset: {} - is encrypted, and there is no terminal to ask the passphrase on: give -k file", spec);
                return None;
            }
        };
        let _ = write!(t, "Passphrase for {}: ", spec);
        let _ = t.flush();
        stty("-echo", &t);
        let mut b = [0u8; 1];
        while line.len() <= PASSMAX + 1 {
            match t.read(&mut b) {
                Ok(1) if b[0] != b'\n' => line.push(b[0]),
                Err(ref e) if e.kind() == io::ErrorKind::Interrupted => continue,
                _ => break,
            }
        }
        /* The terminal as it was, whatever was typed */
        if stty(&saved, &t).is_none() {
            stty("echo", &t);
        }
        let _ = writeln!(t);
    }
    if line.last() == Some(&b'\r') {
        line.pop();
    }
    if line.is_empty() || line.len() > PASSMAX {
        msg!("Passphrase: none - empty, or longer than {} bytes", PASSMAX);
        return None;
    }
    Some(line)
}

fn usage() -> i32 {
    eprintln!(
        "vbkx-rs X01-22 - the extractor of last resort for VBACKUP savesets\n\n  \
         vbkx-rs l saveset [-k file]           list the files (times in UTC)\n  \
         vbkx-rs x saveset [-C dir] [-k file]  extract them all\n  \
         vbkx-rs t saveset [-k file]           read it all, check the checksums\n  \
         vbkx-rs selftest                      check SHA-256, HMAC, PBKDF2, ChaCha20, the codecs against their standards\n\n  \
         -k file  the passphrase of an encrypted saveset: the first line of file\n           \
         (else VBACKUP_KEY_FILE, else it is asked for on the terminal)\n\n\
         Completion: 0 - done; 1 - something damaged or not done; 2 - not usable."
    );
    2
}

fn main1() -> i32 {
    let args: Vec<String> = std::env::args().collect();
    if args.len() == 2 && args[1] == "selftest" {
        return selftest();
    }
    if args.len() < 3 || !matches!(args[1].as_str(), "l" | "x" | "t") {
        return usage();
    }
    let op = args[1].clone();
    let spec = args[2].clone();
    let mut out = ".".to_string();
    let mut keyfile: Option<String> = None;
    let mut i = 3;
    while i < args.len() {
        if args[i] == "-C" && i + 1 < args.len() && op == "x" {
            out = args[i + 1].clone();
            i += 2;
        } else if args[i] == "-k" && i + 1 < args.len() {
            keyfile = Some(args[i + 1].clone());
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
    /* Encrypted: nothing of it is read, nothing made, before the passphrase is right */
    if r.vcrypt.is_some() {
        let pass = match passphrase(keyfile.as_deref(), &spec) {
            Some(p) => p,
            None => return 2,
        };
        if !r.setkey(&pass) {
            msg!("Saveset: {} - the passphrase does not open it", spec);
            return 2;
        }
    }
    match op.as_str() {
        "l" => list(&mut r),
        "x" => {
            if let Err(e) = fs::create_dir(&out) {
                if e.kind() != io::ErrorKind::AlreadyExists {
                    msg!("{}", oserr(&format!("Directory: {}", out), &e, "cannot be made or entered"));
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
