#	vbkx-go - the extractor of last resort for VBACKUP savesets.
#	make			build vbkx-go (static, no CGO)
#	make clean
GO	?= go

all: vbkx-go

vbkx-go: main.go
	CGO_ENABLED=0 $(GO) build -o $@ main.go

clean:
	rm -f vbkx-go

.PHONY: all clean
