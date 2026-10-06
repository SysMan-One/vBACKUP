#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		plugins/wcx/Makefile
#
#	ABSTRACT:	The WCX plugin without CMake:
#
#			    make -f plugins/wcx/Makefile linux	vbackup.wcx, Double Commander (Linux)
#			    make -f plugins/wcx/Makefile win64	vbackup.wcx64, Total Commander 64
#			    make -f plugins/wcx/Makefile win32	vbackup.wcx, Total Commander 32
#			    make -f plugins/wcx/Makefile zip	vbackup_wcx.zip: both Windows
#								ones and pluginst.inf - TC installs
#								it when the archive is opened
#			    make -f plugins/wcx/Makefile all	linux win64 win32
#
#			From the root of the sources, or from here (make linux
#			...).  Into out/linux, out/win64, out/win32 (O=...).
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 4-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 6-OCT-2026	RRL	X-02 : lib/vbkdfl.c, lib/vbklzm.c - codecs 2 and 3.
#
#		 4-OCT-2026	RRL	X-01 : Initial version.
#
#---

#	The root of the sources: two levels up from this file
TOP	:= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
O	?= out

CC	?= cc
CC64	?= x86_64-w64-mingw32-gcc
CC32	?= i686-w64-mingw32-gcc

CFLAGS	?= -O2
WFLAGS	:= -Wall -Wextra -Wno-unused-parameter
SRC	:= $(TOP)/plugins/wcx/vbkwcx.c $(TOP)/lib/vbkfmt.c $(TOP)/lib/vbkrd.c $(TOP)/lib/vbklz4.c $(TOP)/lib/vbkdfl.c $(TOP)/lib/vbklzm.c $(TOP)/lib/vbkcrp.c $(TOP)/lib/vbkrs.c
DEP	:= $(SRC) $(TOP)/lib/vbkos.h $(TOP)/lib/vbkfmt.h $(TOP)/lib/vbkrd.h $(TOP)/lib/vbklz4.h $(TOP)/lib/vbkcrp.h

.PHONY:	all linux win64 win32 zip clean

all:	linux win64 win32

linux:	$(O)/linux/vbackup.wcx
win64:	$(O)/win64/vbackup.wcx64
win32:	$(O)/win32/vbackup.wcx

$(O)/linux/vbackup.wcx: $(DEP)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) -shared -fPIC -fvisibility=hidden -DVBK_NOSTARLET -D_GNU_SOURCE -I$(TOP)/lib -o $@ $(SRC)

$(O)/win64/vbackup.wcx64: $(DEP)
	mkdir -p $(dir $@)
	$(CC64) $(CFLAGS) $(WFLAGS) -shared -I$(TOP)/lib -o $@ $(SRC) -static -Wl,--kill-at

$(O)/win32/vbackup.wcx: $(DEP)
	mkdir -p $(dir $@)
	$(CC32) $(CFLAGS) $(WFLAGS) -shared -I$(TOP)/lib -o $@ $(SRC) -static -Wl,--kill-at

zip:	win64 win32
	rm -f $(O)/vbackup_wcx.zip
	cd $(O) && cp win64/vbackup.wcx64 win32/vbackup.wcx $(TOP)/plugins/wcx/pluginst.inf . \
		&& zip -q vbackup_wcx.zip vbackup.wcx64 vbackup.wcx pluginst.inf && rm -f vbackup.wcx64 vbackup.wcx pluginst.inf

clean:
	rm -rf $(O)
