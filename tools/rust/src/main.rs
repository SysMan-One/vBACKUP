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
**						vectors of their standards
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
**		rebuilt from the group's XOR block; after a loss the stream
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
    if u32_at(b, 8) != bsize || h.typ < BT_DATA || h.typ > BT_ETRAILER || h.paylen > psize || (h.recoff != NONE && h.recoff >= psize) {
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

/* The group size and the encryption tags (CIPHER makes it encrypted), out of the SUMMARY record a VHDR carries */
fn summary_group(blk: &[u8], h: &Bhdr) -> Option<(u32, Option<Vcrypt>)> {
    let pay = blk.get(HDR..HDR + h.paylen as usize)?;
    if pay.len() < 8 || u16_at(pay, 0) != RT_SUMMARY {
        return None;
    }
    let body = pay.get(8..8usize.saturating_add(u32_at(pay, 4) as usize))?;
    let mut grp = 0u32;
    let mut has_cipher = false;
    let mut vc = Vcrypt::default();
    tlv_each(body, |tag, v| match tag {
        71 => grp = getu(v) as u32,
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
        Some((grp, if has_cipher { Some(vc) } else { None }))
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
                    if let Some((g, vc)) = summary_group(&blk, &h) {
                        r.bsize = bs;
                        r.uuid = h.uuid;
                        r.grpsz = g;
                        r.vcrypt = vc;
                        found = true;
                    }
                }
            }
        }
        if !found && !r.guess(&f) {
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
                        if let Some((_, vc)) = summary_group(&read_block(Some(&vf), r.bsize, 0), &h) {
                            r.vcrypt = vc;
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
                let h = match check(&self.crc, &read_block(Some(f), bs, pos), bs, None) {
                    Some(h) if h.volno == 1 && h.blkno == pos => h,
                    _ => continue,
                };
                self.bsize = bs;
                self.uuid = h.uuid;
                self.grpsz = 0;
                self.enc_seen = h.typ == BT_EDATA || h.typ == BT_ETRAILER;
                for p in 1..=(MAXGRP as u64 + 1) {
                    if let Some(x) = check(&self.crc, &read_block(Some(f), bs, p), bs, Some(&self.uuid)) {
                        self.enc_seen |= x.typ == BT_EDATA || x.typ == BT_ETRAILER;
                        if x.typ == BT_XOR && x.gindex as u64 == p - 1 {
                            self.grpsz = x.gindex as u32;
                            break;
                        }
                    }
                }
                msg!("Saveset: {} - its first block is bad: block size {} and group size {} found by trying", self.spec, bs, self.grpsz);
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
                msg!("Block: {}, Volume: {} - an invalid record, skipped", self.payblk, self.payvol);
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
                msg!("Saveset: {} - ends inside a record", self.spec);
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
        "vbkx-rs X01-08 - the extractor of last resort for VBACKUP savesets\n\n  \
         vbkx-rs l saveset [-k file]           list the files (times in UTC)\n  \
         vbkx-rs x saveset [-C dir] [-k file]  extract them all\n  \
         vbkx-rs t saveset [-k file]           read it all, check the checksums\n  \
         vbkx-rs selftest                      check SHA-256, HMAC, PBKDF2, ChaCha20 against their standards\n\n  \
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
