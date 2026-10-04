/*
**++
**
**  FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
**
**  MODULE:	tools/go/main.go
**
**  ABSTRACT:	vbkx-go - the extractor of last resort for VBACKUP savesets.
**		One file, the Go standard library only, nothing else: for
**		the day everything else is dead.  Simple and plain on
**		purpose - one block at a time, no threads, no mappings, no
**		tricks - so that a human can read it against doc/format.md
**		and fix it.  Speed is not a goal; getting the data out is.
**
**  BUILD:	go build -o vbkx-go main.go          (Go 1.19 or later)
**		or: make            (the Makefile beside this file)
**		A static binary needs nothing more: CGO is not used.
**
**  USAGE:	vbkx-go l saveset              list the files
**		vbkx-go x saveset [-C dir]     extract them all into dir
**						(default: the current directory)
**		vbkx-go t saveset              read it all, check the checksums
**
**		saveset is volume 1 (x.bck); volumes 2, 3, ... are looked
**		for beside it as x.bck.002, x.bck.003, ...
**
**		  $ vbkx-go l /mnt/usb/home.bck
**		  $ vbkx-go x /mnt/usb/home.bck -C /tmp/restore
**		  $ vbkx-go t /mnt/usb/home.bck
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
**  GUARANTEED:	no crash and no hang on any input; nothing written outside
**		the output directory; no silent damage - a file that is not
**		named in a message is the file that was saved.
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
**
**	X01-03		 4-OCT-2026	RRL
**		Initial version.
**
**--
 */
package main

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"time"
	"unsafe"
)

/* The numbers of doc/format.md */
const (
	hdrSize = 64         // block header, section 3
	none    = 0xFFFFFFFF // "no record begins here"
	maxRec  = 16 << 20   // sanity ceiling of a record body
	minBsz  = 8192
	maxBsz  = 1 << 20
	maxGrp  = 100
	maxVol  = 9999
	volGap  = 16 // missing volume names in a row that end the search

	btData    = 1
	btXor     = 2
	btVhdr    = 3
	btTrailer = 4

	rtSummary = 1
	rtFile    = 2
	rtData    = 3
	rtFend    = 4
	rtCatalog = 5
	rtEnd     = 6
	rtDataz   = 7 // DATA, compressed: format.md 6.7

	maxData  = 1 << 20 // the most octets a DATA or DATAZ record holds
	codecLZ4 = 1

	ftReg      = 1
	ftDir      = 2
	ftSymlink  = 3
	ftHardlink = 4
	ftFifo     = 7

	fsChanged = 1
	fsReaderr = 2
	fsPresent = 3
)

var (
	le  = binary.LittleEndian
	bad bool // something was damaged or not done: completion 1
)

func msg(format string, a ...interface{}) {
	fmt.Fprintf(os.Stderr, "vbkx-go: "+format+"\n", a...)
}

func u16(b []byte, off int) uint16 {
	if off < 0 || off+2 > len(b) {
		return 0
	}
	return le.Uint16(b[off:])
}

func u32(b []byte, off int) uint32 {
	if off < 0 || off+4 > len(b) {
		return 0
	}
	return le.Uint32(b[off:])
}

func u64(b []byte, off int) uint64 {
	if off < 0 || off+8 > len(b) {
		return 0
	}
	return le.Uint64(b[off:])
}

/* The block header, format.md section 3 */
type bhdr struct {
	bsize, recoff, paylen, prvrecoff, prvpaylen, volno, crc uint32
	typ                                                     uint8
	gindex                                                  uint16
	uuid                                                    [16]byte
	blkno                                                   uint64
}

/*
** Decode and check a block: magic, header length, version, size, saveset
** (uuid nil - any), type, lengths, and the CRC of the header (its CRC
** field taken as 0) followed by the whole payload area.
 */
func check(b []byte, bsize uint32, uuid *[16]byte) (bhdr, bool) {
	var h bhdr
	if len(b) != int(bsize) || bsize < hdrSize || string(b[0:4]) != "VBKB" || u16(b, 4) != hdrSize || u16(b, 6) != 1 {
		return h, false
	}
	h.bsize = u32(b, 8)
	h.typ = b[12]
	h.gindex = u16(b, 14)
	copy(h.uuid[:], b[16:32])
	h.blkno = u64(b, 32)
	h.volno = u32(b, 40)
	h.recoff = u32(b, 44)
	h.paylen = u32(b, 48)
	h.prvrecoff = u32(b, 52)
	h.prvpaylen = u32(b, 56)
	h.crc = u32(b, 60)
	psize := bsize - hdrSize
	if h.bsize != bsize || (uuid != nil && h.uuid != *uuid) || h.typ < btData || h.typ > btTrailer ||
		h.paylen > psize || (h.recoff != none && h.recoff >= psize) {
		return h, false
	}
	var hdr [hdrSize]byte
	copy(hdr[:], b[:hdrSize])
	le.PutUint32(hdr[60:], 0)
	crc := crc32.Update(0, crc32.IEEETable, hdr[:])
	crc = crc32.Update(crc, crc32.IEEETable, b[hdrSize:])
	return h, crc == h.crc
}

/* TLV items, section 6: u16 tag, u32 length, value; false - the body is cut */
func tlvEach(body []byte, fn func(tag uint16, val []byte)) bool {
	pos := 0
	for pos < len(body) {
		if len(body)-pos < 6 {
			return false
		}
		tag := u16(body, pos)
		vlen := int(u32(body, pos+2))
		if vlen < 0 || vlen > len(body)-pos-6 {
			return false
		}
		fn(tag, body[pos+6:pos+6+vlen])
		pos += 6 + vlen
	}
	return true
}

func getu(v []byte) uint64 {
	switch len(v) {
	case 1:
		return uint64(v[0])
	case 2:
		return uint64(u16(v, 0))
	case 4:
		return uint64(u32(v, 0))
	case 8:
		return u64(v, 0)
	}
	return 0
}

type ftime struct {
	sec  int64
	nsec int64
}

func gettime(v []byte) ftime {
	if len(v) != 12 {
		return ftime{}
	}
	t := ftime{int64(u64(v, 0)), int64(u32(v, 8))}
	if t.nsec > 999999999 {
		t.nsec = 0
	}
	return t
}

/* One file as a FILE record or a catalog entry describes it, section 6.1 */
type entry struct {
	fileno       uint32
	path, link   string
	ftype        uint8
	mode         uint32
	size         uint64
	mtime, atime ftime
	status       uint8
}

func parseEntry(body []byte) (entry, bool) {
	var e entry
	hasPath := false
	ok := tlvEach(body, func(tag uint16, v []byte) {
		switch tag {
		case 1:
			e.fileno = uint32(getu(v))
		case 2:
			e.path, hasPath = string(v), true
		case 3:
			e.ftype = uint8(getu(v))
		case 4:
			e.mode = uint32(getu(v))
		case 9:
			e.size = getu(v)
		case 10:
			e.mtime = gettime(v)
		case 11:
			e.atime = gettime(v)
		case 15:
			e.link = string(v)
		case 33:
			e.status = uint8(getu(v))
		}
	})
	if e.atime.sec == 0 && e.atime.nsec == 0 {
		e.atime = e.mtime
	}
	return e, ok && hasPath && e.path != "" && e.ftype != 0
}

/* A volume of the saveset; f nil - it is missing */
type volume struct {
	f        *os.File
	firstblk uint64 // blkno of its first block
	nblk     uint64 // whole blocks in it
}

type reader struct {
	spec    string
	bsize   uint32
	grpsz   uint32
	uuid    [16]byte
	vols    []volume
	trailer bool

	curvol  int    // where the next group is read from
	curpos  uint64 // ... block position in that volume
	pays    [][]byte
	recoffs []uint32
	blks    []uint64
	next    int

	gap    bool // a block has been lost since the last record
	pay    []byte
	payoff int
	payblk uint64
	payvol int
	resync bool // the record returned follows a loss
}

func volspec(spec string, n int) string {
	if n <= 1 {
		return spec
	}
	return fmt.Sprintf("%s.%03d", spec, n)
}

/* Read a block; what cannot be read is zeros - such a block fails its check */
func readBlock(f *os.File, bsize uint32, pos uint64) []byte {
	buf := make([]byte, bsize)
	if f == nil {
		return buf
	}
	n, _ := f.ReadAt(buf, int64(pos)*int64(bsize))
	for i := n; i >= 0 && i < len(buf); i++ {
		buf[i] = 0
	}
	return buf
}

func blocksIn(f *os.File, bsize uint32) uint64 {
	st, err := f.Stat()
	if err != nil || st.Size() < 0 {
		return 0
	}
	return uint64(st.Size()) / uint64(bsize)
}

/* The group size, out of the SUMMARY record a VHDR carries */
func summaryGroup(blk []byte, h bhdr) (uint32, bool) {
	pay := blk[hdrSize : hdrSize+int(h.paylen)]
	if len(pay) < 8 || u16(pay, 0) != rtSummary {
		return 0, false
	}
	blen := int(u32(pay, 4))
	if blen < 0 || blen > len(pay)-8 {
		return 0, false
	}
	var grp uint32
	tlvEach(pay[8:8+blen], func(tag uint16, v []byte) {
		if tag == 71 {
			grp = uint32(getu(v))
		}
	})
	return grp, grp <= maxGrp
}

/*
** Volume 1 without a good VHDR: try every legal block size against the
** blocks after the first one; the first that checks gives the size and the
** saveset.  The group size is then where the first XOR block stands.
 */
func guess(r *reader, f *os.File) bool {
	for bs := uint32(minBsz); bs <= maxBsz; bs += 512 {
		for pos := uint64(1); pos <= 8; pos++ {
			h, ok := check(readBlock(f, bs, pos), bs, nil)
			if !ok || h.volno != 1 || h.blkno != pos {
				continue
			}
			r.bsize, r.uuid = bs, h.uuid
			r.grpsz = 0
			for p := uint64(1); p <= maxGrp+1; p++ {
				if x, ok := check(readBlock(f, bs, p), bs, &r.uuid); ok && x.typ == btXor && uint64(x.gindex) == p-1 {
					r.grpsz = uint32(x.gindex)
					break
				}
			}
			msg("%s: the first block is bad; block size %d and group size %d found by trying", r.spec, bs, r.grpsz)
			return true
		}
	}
	return false
}

func open(spec string) (*reader, error) {
	r := &reader{spec: spec, curvol: 1, curpos: 1}
	f, err := os.Open(spec)
	if err != nil {
		return nil, err
	}
	/* The block size comes from the first header; it is believed only when the block checks */
	head := make([]byte, hdrSize)
	f.ReadAt(head, 0)
	found := false
	if bs := u32(head, 8); string(head[0:4]) == "VBKB" && bs >= minBsz && bs <= maxBsz && bs%512 == 0 {
		blk := readBlock(f, bs, 0)
		if h, ok := check(blk, bs, nil); ok && h.typ == btVhdr && h.volno == 1 {
			if grp, ok := summaryGroup(blk, h); ok {
				r.bsize, r.uuid, r.grpsz, found = bs, h.uuid, grp, true
			}
		}
	}
	if !found && !guess(r, f) {
		f.Close()
		return nil, fmt.Errorf("%s is not a saveset", spec)
	}
	r.vols = append(r.vols, volume{f, 0, blocksIn(f, r.bsize)})

	/* The further volumes; a missing name does not end the search at once */
	miss := 0
	for n := 2; n <= maxVol && miss < volGap; n++ {
		vf, err := os.Open(volspec(spec, n))
		if err != nil {
			miss++
			continue
		}
		/* Its VHDR, or else its next block, tells where it begins */
		var first uint64
		h, ok := check(readBlock(vf, r.bsize, 0), r.bsize, &r.uuid)
		if ok && h.typ == btVhdr && h.volno == uint32(n) {
			first = h.blkno
		} else if h, ok = check(readBlock(vf, r.bsize, 1), r.bsize, &r.uuid); ok && h.volno == uint32(n) && h.blkno > 0 {
			first = h.blkno - 1
			msg("volume %d: its first block is bad, it is read all the same", n)
		} else {
			msg("volume %d belongs to another saveset, or is none", n)
			vf.Close()
			miss++
			continue
		}
		for len(r.vols) < n-1 {
			r.vols = append(r.vols, volume{})
		}
		r.vols = append(r.vols, volume{vf, first, blocksIn(vf, r.bsize)})
		miss = 0
	}

	/* The TRAILER, last block of the last volume: it is not part of the groups */
	if lv := r.vols[len(r.vols)-1]; lv.nblk > 1 {
		if th, ok := check(readBlock(lv.f, r.bsize, lv.nblk-1), r.bsize, &r.uuid); ok && th.typ == btTrailer {
			r.trailer = true
		}
	}
	return r, nil
}

func (r *reader) volend(n int) uint64 {
	v := r.vols[n-1]
	if r.trailer && n == len(r.vols) && v.nblk > 0 {
		return v.nblk - 1
	}
	return v.nblk
}

/*
** Read the next group (section 4), check its blocks, rebuild one bad DATA
** block from the XOR block; false - the end of the saveset.
 */
func (r *reader) loadGroup() bool {
	var v volume
	var end uint64
	for {
		if r.curvol > len(r.vols) {
			return false
		}
		v = r.vols[r.curvol-1]
		if v.f == nil {
			msg("volume %d is missing", r.curvol)
			bad = true
			r.gap = true
			r.curvol++
			r.curpos = 1
			continue
		}
		if end = r.volend(r.curvol); r.curpos >= end {
			r.curvol++
			r.curpos = 1
			continue
		}
		break
	}
	n := uint64(1)
	if r.grpsz > 0 {
		n = uint64(r.grpsz) + 1
	}
	if end-r.curpos < n {
		n = end - r.curpos
	}
	blks := make([][]byte, n)
	hdrs := make([]bhdr, n)
	ok := make([]bool, n)
	for i := range blks {
		blks[i] = readBlock(v.f, r.bsize, r.curpos+uint64(i))
		hdrs[i], ok[i] = check(blks[i], r.bsize, &r.uuid)
		ok[i] = ok[i] && hdrs[i].blkno == v.firstblk+r.curpos+uint64(i) && hdrs[i].volno == uint32(r.curvol)
	}

	/* Where the XOR block is: a full group ends with it; a short one, cut short, may not have one */
	hasXor := 0
	xi := int(n) - 1
	if r.grpsz > 0 {
		switch {
		case n == uint64(r.grpsz)+1:
			hasXor = 1
		case ok[xi]:
			if hdrs[xi].typ == btXor {
				hasXor = 1
			}
		case n >= 2:
			hasXor = 1
		}
		if hasXor == 1 && ok[xi] && hdrs[xi].typ != btXor {
			ok[xi] = false
		}
	}
	gdata := int(n) - hasXor
	nbad, badi := 0, 0
	for i := 0; i < gdata; i++ {
		if ok[i] && hdrs[i].typ != btData {
			ok[i] = false
		}
		if !ok[i] {
			nbad++
			badi = i
		}
	}

	/* One bad DATA block: its payload is the XOR of all the others, two header fields kept by the next block */
	if nbad == 1 && hasXor == 1 && ok[xi] && int(hdrs[xi].gindex) == gdata && badi+1 < len(hdrs) {
		d := blks[badi][hdrSize:]
		copy(d, blks[xi][hdrSize:])
		for i := 0; i < gdata; i++ {
			if i == badi {
				continue
			}
			p := blks[i][hdrSize:]
			for j := range d {
				if j < len(p) {
					d[j] ^= p[j]
				}
			}
		}
		s := hdrs[badi+1]
		h := bhdr{typ: btData, recoff: s.prvrecoff, paylen: s.prvpaylen, blkno: v.firstblk + r.curpos + uint64(badi)}
		if h.paylen <= r.bsize-hdrSize && (h.recoff == none || h.recoff < h.paylen) {
			hdrs[badi], ok[badi] = h, true
			msg("block %d of volume %d was bad and has been repaired", h.blkno, r.curvol)
		}
	}

	r.pays, r.recoffs, r.blks = r.pays[:0], r.recoffs[:0], r.blks[:0]
	for i := 0; i < gdata; i++ {
		b := v.firstblk + r.curpos + uint64(i)
		if !ok[i] {
			msg("block %d of volume %d is bad and cannot be repaired", b, r.curvol)
			bad = true
			r.pays = append(r.pays, nil)
		} else {
			r.pays = append(r.pays, blks[i][hdrSize:hdrSize+int(hdrs[i].paylen)])
		}
		r.recoffs = append(r.recoffs, hdrs[i].recoff)
		r.blks = append(r.blks, b)
	}
	r.next = 0
	r.curpos += n
	return true
}

/*
** Make the payload of the next good DATA block the current one.
** 0 - the stream goes on in it; 1 - blocks were lost before it, it is
** taken from its first record header (RECOFF); 2 - the end.
 */
func (r *reader) nextPay() int {
	for {
		if r.next >= len(r.pays) {
			if !r.loadGroup() {
				return 2
			}
			continue
		}
		i := r.next
		r.next++
		if r.pays[i] == nil {
			r.gap = true
			continue
		}
		r.pay, r.payoff, r.payblk, r.payvol = r.pays[i], 0, r.blks[i], r.curvol
		if !r.gap {
			return 0
		}
		/* After a loss: a block in which no record begins is of no use */
		if r.recoffs[i] == none || int(r.recoffs[i]) >= len(r.pays[i]) {
			continue
		}
		r.payoff = int(r.recoffs[i])
		r.gap = false
		return 1
	}
}

/* The next record of the stream (section 5); ok false - the end.  RESYNC: blocks were lost before it. */
func (r *reader) nextRecord() (uint16, []byte, bool) {
	resync := false
	for {
		for r.pay == nil || r.payoff >= len(r.pay) {
			switch r.nextPay() {
			case 1:
				resync = true
			case 2:
				r.resync = resync
				return 0, nil, false
			}
		}
		/* A record header is never split over two blocks */
		var typ uint16
		length := uint64(maxRec + 1)
		if len(r.pay)-r.payoff >= 8 {
			typ = u16(r.pay, r.payoff)
			length = uint64(u32(r.pay, r.payoff+4))
		}
		if typ == 0 || length > maxRec {
			msg("a bad record in block %d of volume %d", r.payblk, r.payvol)
			bad = true
			r.gap, r.pay = true, nil
			continue
		}
		r.payoff += 8
		body := make([]byte, 0, length)
		st := 0
		for uint64(len(body)) < length {
			if r.payoff >= len(r.pay) {
				if st = r.nextPay(); st != 0 {
					break
				}
			}
			n := len(r.pay) - r.payoff
			if want := int(length) - len(body); n > want {
				n = want
			}
			body = append(body, r.pay[r.payoff:r.payoff+n]...)
			r.payoff += n
		}
		if st == 1 {
			/* Lost in the middle of the body: dropped, a new header is at hand */
			resync = true
			continue
		}
		if st == 2 {
			msg("the saveset ends inside a record")
			bad = true
			r.resync = true
			return 0, nil, false
		}
		r.resync = resync
		return typ, body, true
	}
}

/* A stored name that is safe to use: relative, no "", "." or ".." component, no NUL */
func nameOK(name string) bool {
	if name == "" || name[0] == '/' || strings.IndexByte(name, 0) >= 0 {
		return false
	}
	for _, c := range strings.Split(name, "/") {
		if c == "" || c == "." || c == ".." {
			return false
		}
	}
	return true
}

/* The directories a name lies in, below OUT, made if CREATE; a symbolic link on the way is refused */
func parents(out, name string, create bool) error {
	comps := strings.Split(name, "/")
	p := out
	for _, c := range comps[:len(comps)-1] {
		p = filepath.Join(p, c)
		st, err := os.Lstat(p)
		if err == nil {
			if !st.IsDir() {
				return fmt.Errorf("a directory on the way is a link, or no directory")
			}
			continue
		}
		if !create {
			return err
		}
		if err := os.Mkdir(p, 0700); err != nil {
			return err
		}
	}
	return nil
}

/* utimensat(2): the times of a name; NOFOLLOW - of a symbolic link itself */
func setTimes(path string, e *entry, nofollow bool) {
	ts := [2]syscall.Timespec{{Sec: e.atime.sec, Nsec: e.atime.nsec}, {Sec: e.mtime.sec, Nsec: e.mtime.nsec}}
	p, err := syscall.BytePtrFromString(path)
	if err != nil {
		return
	}
	flags := 0
	if nofollow {
		flags = 0x100 // AT_SYMLINK_NOFOLLOW
	}
	atFdcwd := -100
	syscall.Syscall6(syscall.SYS_UTIMENSAT, uintptr(atFdcwd), uintptr(unsafe.Pointer(p)), uintptr(unsafe.Pointer(&ts[0])),
		uintptr(flags), 0, 0)
}

/* The files of the stream being put back (x), or only checked (t) */
type extractor struct {
	out     string
	make    bool
	e       entry
	f       *os.File
	path    string
	active  bool
	crc     uint32
	damaged bool
	seen    map[uint32]bool
	dirs    []entry
	catSeen bool
	catHole bool
}

func (x *extractor) begin(body []byte) {
	x.active, x.f, x.crc, x.damaged = false, nil, 0, false
	e, ok := parseEntry(body)
	if !ok {
		msg("a FILE record that makes no sense is skipped")
		bad = true
		return
	}
	x.e = e
	x.seen[e.fileno] = true
	if !x.make {
		x.active = e.ftype == ftReg
		return
	}
	if !nameOK(e.path) {
		msg("%s was not extracted: a name that leads out of the output directory", e.path)
		bad = true
		return
	}
	if err := parents(x.out, e.path, true); err != nil {
		msg("%s was not extracted: %v", e.path, err)
		bad = true
		return
	}
	path := filepath.Join(x.out, e.path)
	if e.ftype == ftDir {
		if err := os.Mkdir(path, 0700); err != nil && !os.IsExist(err) {
			msg("%s was not extracted: %v", e.path, err)
			bad = true
			return
		}
		x.dirs = append(x.dirs, e)
		return
	}
	if _, err := os.Lstat(path); err == nil {
		msg("%s was not extracted: it exists, and is never overwritten", e.path)
		bad = true
		return
	}
	var err error
	switch e.ftype {
	case ftReg:
		if x.f, err = os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0600); err == nil {
			x.active, x.path = true, path
		}
	case ftSymlink:
		if err = os.Symlink(e.link, path); err == nil {
			setTimes(path, &e, true)
		}
	case ftHardlink:
		if !nameOK(e.link) {
			err = fmt.Errorf("a link that leads out of the output directory")
		} else if err = parents(x.out, e.link, false); err == nil {
			err = os.Link(filepath.Join(x.out, e.link), path)
		}
	case ftFifo:
		if err = syscall.Mkfifo(path, 0600); err == nil {
			syscall.Chmod(path, e.mode&07777)
			setTimes(path, &e, false)
		}
	default:
		return // devices and sockets are not made
	}
	if err != nil {
		msg("%s was not extracted: %v", e.path, err)
		bad = true
	}
}

/*
** lz4Decompress - the LZ4 block format (format.md 6.7): exactly rawlen
** octets out of exactly src, or nil.  Every length is checked against
** what is left of the input and of the output, every offset against
** what has been written; nothing else is trusted.
 */
func lz4Decompress(src []byte, rawlen uint32) []byte {
	dst := make([]byte, 0, rawlen)
	ip, n := 0, len(src)
	getlen := func(v uint32) (uint32, bool) { // a length beyond the nibble
		for {
			if ip >= n {
				return 0, false
			}
			b := src[ip]
			ip++
			v += uint32(b)
			if v > rawlen {
				return 0, false
			}
			if b != 255 {
				return v, true
			}
		}
	}
	for {
		if ip >= n {
			return nil
		}
		tok := src[ip]
		ip++
		lit := uint32(tok >> 4)
		ok := true
		if lit == 15 {
			if lit, ok = getlen(lit); !ok {
				return nil
			}
		}
		if uint64(lit) > uint64(n-ip) || lit > rawlen-uint32(len(dst)) {
			return nil
		}
		dst = append(dst, src[ip:ip+int(lit)]...)
		ip += int(lit)
		if ip == n { // the last sequence has its literals only
			break
		}
		if n-ip < 2 {
			return nil
		}
		off := uint32(src[ip]) | uint32(src[ip+1])<<8
		ip += 2
		if off == 0 || off > uint32(len(dst)) {
			return nil
		}
		ml := uint32(tok & 15)
		if ml == 15 {
			if ml, ok = getlen(ml); !ok {
				return nil
			}
		}
		ml += 4
		if ml > rawlen-uint32(len(dst)) {
			return nil
		}
		for i := uint32(0); i < ml; i++ { // octet by octet: a match may overlap what it makes
			dst = append(dst, dst[len(dst)-int(off)])
		}
	}
	if uint32(len(dst)) != rawlen {
		return nil
	}
	return dst
}

/*
** dataView - the file, the offset and the octets of a DATA or DATAZ
** record; ok false - a bad record
 */
func dataView(typ uint16, body []byte) (fileno uint32, off uint64, d []byte, ok bool) {
	if typ == rtData {
		if len(body) < 16 {
			return 0, 0, nil, false
		}
		return u32(body, 0), u64(body, 8), body[16:], true
	}
	if len(body) < 20 || u32(body, 4) != codecLZ4 || u32(body, 16) > maxData {
		return 0, 0, nil, false
	}
	if d = lz4Decompress(body[20:], u32(body, 16)); d == nil {
		return 0, 0, nil, false
	}
	return u32(body, 0), u64(body, 8), d, true
}

/*
** A DATA record (u32 fileno, u32 0, u64 offset, the bytes) or a DATAZ one
** (u32 fileno, u32 codec, u64 offset, u32 rawlen, LZ4 block); a DATAZ that
** does not decompress leaves the file incomplete
 */
func (x *extractor) data(typ uint16, body []byte) {
	if !x.active {
		return
	}
	fileno, off, d, ok := dataView(typ, body)
	if !ok {
		msg("%s: a data record that makes no sense", x.e.path)
		x.damaged = true
		return
	}
	if fileno != x.e.fileno {
		return
	}
	x.crc = crc32.Update(x.crc, crc32.IEEETable, d)
	if x.f == nil {
		return
	}
	if off > 1<<62 {
		x.damaged = true
		return
	}
	if _, err := x.f.WriteAt(d, int64(off)); err != nil {
		msg("%s: %v", x.e.path, err)
		x.damaged = true
	}
}

/* The end of a regular file: size, checksum, attributes; body nil - it ends without its FEND */
func (x *extractor) end(body []byte) {
	if !x.active {
		return
	}
	x.active = false
	var fileno, crc uint32
	var status uint8
	size := x.e.size
	hasCRC := false
	if body != nil {
		tlvEach(body, func(tag uint16, v []byte) {
			switch tag {
			case 1:
				fileno = uint32(getu(v))
			case 9:
				size = getu(v)
			case 32:
				crc, hasCRC = uint32(getu(v)), true
			case 33:
				status = uint8(getu(v))
			}
		})
	}
	if body == nil || fileno != x.e.fileno {
		x.damaged = true
	} else if hasCRC && crc != x.crc {
		msg("%s: the checksum does not match", x.e.path)
		x.damaged = true
	}
	switch {
	case x.damaged:
		msg("%s is incomplete: its data was lost in bad blocks", x.e.path)
		bad = true
	case status == fsChanged:
		msg("%s changed while it was saved: the copy may be a mix", x.e.path)
	case status == fsReaderr:
		msg("%s could not be read whole when it was saved", x.e.path)
	}
	if x.f == nil {
		return
	}
	if size <= 1<<62 {
		x.f.Truncate(int64(size))
	}
	if err := x.f.Close(); err != nil {
		msg("%s: %v", x.e.path, err)
		bad = true
	}
	x.f = nil
	syscall.Chmod(x.path, x.e.mode&07777)
	setTimes(x.path, &x.e, false)
}

/* A CATALOG record (section 6.4): the files never met in the stream are named */
func (x *extractor) catalog(body []byte) {
	x.catSeen = true
	off := 0
	for off+4 <= len(body) {
		elen := int(u32(body, off))
		if elen < 0 || elen > len(body)-off-4 {
			x.catHole = true
			break
		}
		if e, ok := parseEntry(body[off+4 : off+4+elen]); ok && e.status != fsPresent && !x.seen[e.fileno] {
			msg("%s was not extracted: its records were lost in bad blocks", e.path)
			bad = true
		}
		off += 4 + elen
	}
}

/* The modes and times of the directories, deepest first: a file made in one changes its times */
func (x *extractor) finishDirs() {
	for i := len(x.dirs) - 1; i >= 0; i-- {
		e := &x.dirs[i]
		if parents(x.out, e.path, false) != nil {
			continue
		}
		path := filepath.Join(x.out, e.path)
		if st, err := os.Lstat(path); err != nil || !st.IsDir() {
			continue
		}
		syscall.Chmod(path, e.mode&07777)
		setTimes(path, e, false)
	}
}

func run(r *reader, x *extractor) {
	ended := false
	for !ended {
		typ, body, ok := r.nextRecord()
		if r.resync {
			if x.active {
				x.damaged = true
			}
			if x.catSeen {
				x.catHole = true
			}
		}
		if !ok {
			break
		}
		switch typ {
		case rtFile:
			if x.active {
				x.end(nil)
			}
			x.begin(body)
		case rtData, rtDataz:
			x.data(typ, body)
		case rtFend:
			x.end(body)
		case rtCatalog:
			if x.active {
				x.end(nil)
			}
			x.catalog(body)
		case rtEnd:
			ended = true
		}
	}
	if x.active {
		x.end(nil)
	}
	/* Blocks were lost, and the catalog could not tell every name */
	if bad && (!x.catSeen || x.catHole || !ended) {
		msg("%s: files missing from the output cannot all be named", r.spec)
	}
}

func list(r *reader) {
	for {
		typ, body, ok := r.nextRecord()
		if !ok || typ == rtCatalog || typ == rtEnd {
			return
		}
		if typ != rtFile {
			continue
		}
		e, ok := parseEntry(body)
		if !ok {
			continue
		}
		t := "?-dlhcbps"[0]
		if e.ftype <= 8 {
			t = "?-dlhcbps"[e.ftype]
		}
		line := fmt.Sprintf("%s %12d %c%04o %s", time.Unix(e.mtime.sec, 0).UTC().Format("2006-01-02 15:04:05"), e.size,
			t, e.mode&07777, e.path)
		if e.ftype == ftSymlink {
			line += " -> " + e.link
		} else if e.ftype == ftHardlink {
			line += " link to " + e.link
		}
		fmt.Println(line)
	}
}

func usage() int {
	fmt.Fprintf(os.Stderr, "vbkx-go X01-04 - the extractor of last resort for VBACKUP savesets\n\n"+
		"  vbkx-go l saveset              list the files (times in UTC)\n"+
		"  vbkx-go x saveset [-C dir]     extract them all\n"+
		"  vbkx-go t saveset              read it all, check the checksums\n\n"+
		"Completion: 0 - done; 1 - something damaged or not done; 2 - not usable.\n")
	return 2
}

func main1() int {
	args := os.Args
	if len(args) < 3 || len(args[1]) != 1 || !strings.Contains("lxt", args[1]) {
		return usage()
	}
	op, spec, out := args[1][0], args[2], "."
	for i := 3; i < len(args); i++ {
		if args[i] == "-C" && i+1 < len(args) && op == 'x' {
			out = args[i+1]
			i++
		} else {
			return usage()
		}
	}
	r, err := open(spec)
	if err != nil {
		msg("%v", err)
		return 2
	}
	switch op {
	case 'l':
		list(r)
	case 'x':
		if err := os.Mkdir(out, 0755); err != nil && !os.IsExist(err) {
			msg("%s: %v", out, err)
			return 2
		}
		syscall.Umask(0)
		x := &extractor{out: out, make: true, seen: map[uint32]bool{}}
		run(r, x)
		x.finishDirs()
	default:
		run(r, &extractor{seen: map[uint32]bool{}})
		if !bad {
			fmt.Printf("%s: all files read, all checksums match\n", spec)
		}
	}
	if bad {
		return 1
	}
	return 0
}

func main() {
	/* The last line of defence: whatever slipped through is a message, not a trace */
	defer func() {
		if p := recover(); p != nil {
			msg("internal error: %v", p)
			os.Exit(2)
		}
	}()
	os.Exit(main1())
}
