#	vbkx-rs - the extractor of last resort for VBACKUP savesets.
#	make			build vbkx-rs with rustc alone (no cargo needed)
#	make check		build vbkx-rs-check: debug assertions, overflow checks
#	make clean
RUSTC	?= rustc

all: vbkx-rs

vbkx-rs: src/main.rs
	$(RUSTC) -O -C overflow-checks=on -C strip=symbols -o $@ src/main.rs

check: vbkx-rs-check

vbkx-rs-check: src/main.rs
	$(RUSTC) -C debug-assertions=on -C overflow-checks=on -o $@ src/main.rs

clean:
	rm -f vbkx-rs vbkx-rs-check

.PHONY: all check clean
