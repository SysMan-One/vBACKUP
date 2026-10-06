#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		CMakeLists.txt
#
#	ABSTRACT:	Build of the VBACKUP facility: the saveset core (lib/,
#			a static library), the utility, its tests, the help
#			libraries and the manual page.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 3-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 6-OCT-2026	RRL	X-18 : Stage 14: vbackup.exe with advapi32; fakessh.exe for
#						node::file in test/winutil.sh.
#
#		 6-OCT-2026	RRL	X-17 : Stage 13: vbackup.exe, the utility on Windows (MinGW-w64,
#						StarLet from its source kit), test/winutil.sh.
#
#		 6-OCT-2026	RRL	X-16 : Stage 12: the extractors for the geeks faster.
#
#		 5-OCT-2026	RRL	X-15 : Stage 11: VBKRS by AVX2, SSSE3 or NEON.
#
#		 5-OCT-2026	RRL	X-14 : Stage 10: VBKRS, the Reed-Solomon parity of a group
#					(/PARITY), into VBKCORE, vbkx, vbkx.exe and WCX.
#
#		 5-OCT-2026	RRL	X-13 : Stage 9: VBKVMS - the savesets of OpenVMS BACKUP
#					(lib/vbkvms.c into VBKCORE, vbkx and vbkx.exe;
#					src/vbkvms.c); doc/vmsbackup.md installed.
#
#		 5-OCT-2026	RRL	X-09 : The kit by git archive of HEAD (cmake/kit.cmake):
#					what is not committed never gets into it.
#					The magic of file(1) into /etc/magic: MC knows a
#					saveset by its contents; .sav beside .bck.
#
#		 4-OCT-2026	RRL	X-05 : Stage 5: the WCX plugin of Total Commander and
#					Double Commander - vbackup.wcx (Linux, the target
#					VBKWCX), and with MinGW-w64 vbackup.wcx64 and the
#					32-bit vbackup.wcx; plugins/ into the kit.
#
#		 4-OCT-2026	RRL	X-04 : Stage 5: the plugins of MC and far2l installed where
#					those are found (cmake/plugcfg.cmake); VBKPHY, VBKIMG,
#					VBKZPL.
#
#		 4-OCT-2026	RRL	X-04 : VBKLZ4; Windows: with cmake/mingw-w64.cmake (or a
#					MinGW of Windows) VBKX alone is built - the reading
#					core and the extractor, no StarLet, no tests; on
#					Linux, with MinGW-w64 and wine at hand, vbkx.exe is
#					built too and test/win.sh runs it under wine.
#
#		 3-OCT-2026	RRL	X-03 : Stage 3: Threads for the writer thread of VBKCORE;
#					VBKPRE, the read-ahead of files; VBKX, the
#					stand-alone extractor; vbkx-go, vbkx-rs, vbkx-pl.
#
#		 3-OCT-2026	RRL	X-02 : Stage 2: VBKHSH, VBKJNL, VBKCPY.
#
#		 3-OCT-2026	RRL	X-01 : Initial version, after SEARCH X01-31.
#
#---

cmake_minimum_required(VERSION 3.16)

project(VBACKUP VERSION 1.0.0 LANGUAGES C DESCRIPTION "OpenVMS BACKUP-style saveset utility")

#	No build type named - Release: without NDEBUG the diagnostics of StarLet
#	carry the module, routine and line of the call, as HELP says it
if ( NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES )
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
	message(STATUS "No build type named, assuming ${CMAKE_BUILD_TYPE}")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/vbackup")

#
#	The identification of the product is stated once, by __IDENT__ of the
#	head module, and is read out of it here for everything else that
#	carries it: the help libraries, the manual page, the SUMMARY of every
#	saveset, the name of the kit.
#
set(VBACKUP$K_HEADMOD "${CMAKE_CURRENT_SOURCE_DIR}/src/vbackup.c")

file(STRINGS "${VBACKUP$K_HEADMOD}" VBACKUP$K_IDENTLINE REGEX "^#define[ \t]+__IDENT__")

if ( NOT VBACKUP$K_IDENTLINE MATCHES "\"([^\"]+)\"" )
	message(FATAL_ERROR "No __IDENT__ in ${VBACKUP$K_HEADMOD}")
endif()

set(VBACKUP$K_IDENT "${CMAKE_MATCH_1}")

#
#	__IDENT__ and __REV__ of every module are two spellings of one number:
#	Xnn-mm[ECOkk] against nn.mm.kk.  A module revised without its __REV__
#	stops the configuration.
#
function(s_vbackup_check_ident)
	foreach ( F ${ARGN} )
		file(STRINGS "${F}" L_IDENT REGEX "^#define[ \t]+__IDENT__")
		file(STRINGS "${F}" L_REV   REGEX "^#define[ \t]+__REV__")

		if ( NOT L_IDENT MATCHES "\"([^\"]+)\"" )
			message(FATAL_ERROR "No __IDENT__ in ${F}")
		endif()

		set(L_I "${CMAKE_MATCH_1}")

		if ( NOT L_REV MATCHES "\"([^\"]+)\"" )
			message(FATAL_ERROR "No __REV__ in ${F}")
		endif()

		set(L_R "${CMAKE_MATCH_1}")

		if ( NOT L_I MATCHES "^X0*([0-9]+)-0*([0-9]+)(ECO0*([0-9]+))?$" )
			message(FATAL_ERROR "${F}: __IDENT__ \"${L_I}\" is not of the form Xnn-nn[ECOnn]")
		endif()

		set(L_MAJ "${CMAKE_MATCH_1}")
		set(L_MIN "${CMAKE_MATCH_2}")
		set(L_ECO "${CMAKE_MATCH_4}")

		if ( L_ECO STREQUAL "" )
			set(L_ECO "0")
		endif()

		if ( NOT L_R STREQUAL "${L_MAJ}.${L_MIN}.${L_ECO}" )
			message(FATAL_ERROR "${F}: __REV__ \"${L_R}\" does not follow __IDENT__ \"${L_I}\" - it should be \"${L_MAJ}.${L_MIN}.${L_ECO}\"")
		endif()
	endforeach()
endfunction()

file(GLOB VBACKUP$K_MODULES
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
	${CMAKE_CURRENT_SOURCE_DIR}/tools/*.c
	${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c
	${CMAKE_CURRENT_SOURCE_DIR}/lib/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/test/*.c
	${CMAKE_CURRENT_SOURCE_DIR}/win/*.h)

s_vbackup_check_ident(${VBACKUP$K_MODULES})

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${VBACKUP$K_MODULES})

#	CONFIGURE_FILE() does not replace a name carrying '$', hence the alias
set(VBACKUP_IDENT "${VBACKUP$K_IDENT}")

message(STATUS "VBACKUP: ${VBACKUP$K_IDENT}, from src/vbackup.c")

option(VBACKUP$M_STRICT "Treat the compilation diagnostics as errors" OFF)
option(BUILD_TESTING    "Build the regression tests"                 ON)
option(VBACKUP$M_TESTS  "Build the tests of this product"            ON)

add_library(vbackup_options INTERFACE)

target_compile_options(vbackup_options INTERFACE -Wall -Wextra -Wwrite-strings -Wno-unused-parameter)
target_compile_definitions(vbackup_options INTERFACE _GNU_SOURCE)

if ( VBACKUP$M_STRICT )
	target_compile_options(vbackup_options INTERFACE -Werror)
endif()

#
#	vbackup.exe, the utility on Windows (stage 13): the modules of the
#	Linux image less /PHYSICAL, /IMAGE and the read-ahead (vbkphy, vbkimg,
#	vbkpre), with the layer of src/vbkosw.c under them - win/vbkwin.h,
#	put in front of every source, gives the POSIX names to its routines.
#	StarLet is compiled from its source kit, VBACKUP_STARLET_SRC (by
#	default the newest ../starlet-<version> beside this tree): an installed StarLet
#	is a library of Linux.
#
set(VBACKUP_WINLIB lib/vbkfmt.c lib/vbkwrt.c lib/vbkrd.c lib/vbklz4.c lib/vbkcrp.c lib/vbkvms.c lib/vbkrs.c lib/vbkpar.c)
set(VBACKUP_WINSRC src/vbackup.c src/vbkmsg.c src/vbkatr.c src/vbkwlk.c src/vbksav.c src/vbkrst.c src/vbklst.c src/vbkcmp.c
	src/vbkhlp.c src/vbkhsh.c src/vbkjnl.c src/vbkcpy.c src/vbkzpl.c src/vbkkey.c src/vbkxfr.c src/vbkrsh.c src/vbkvms.c src/vbkosw.c)

#
#	Windows (cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake, or the
#	MinGW of Windows itself): VBKX - the reading core and the extractor,
#	statically, no StarLet (lib/vbkos.h has its own CRC) - the WCX
#	plugin, and vbackup.exe when the StarLet sources are found.  No tests:
#	the Linux build runs them under wine (test/winutil.sh).
#
if ( WIN32 )
	add_executable(vbkx tools/vbkx.c lib/vbkfmt.c lib/vbkrd.c lib/vbklz4.c lib/vbkcrp.c lib/vbkvms.c lib/vbkrs.c)
	target_include_directories(vbkx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
	target_link_libraries(vbkx PRIVATE vbackup_options shell32)
	target_link_options(vbkx PRIVATE -static)

	install(TARGETS vbkx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

	#	The WCX plugin of Total Commander: a DLL, the __stdcall names undecorated
	add_library(vbkwcx MODULE plugins/wcx/vbkwcx.c lib/vbkfmt.c lib/vbkrd.c lib/vbklz4.c lib/vbkcrp.c lib/vbkrs.c)
	target_include_directories(vbkwcx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
	target_link_libraries(vbkwcx PRIVATE vbackup_options)
	target_link_options(vbkwcx PRIVATE -static -Wl,--kill-at)

	if ( CMAKE_SIZEOF_VOID_P EQUAL 8 )
		set_target_properties(vbkwcx PROPERTIES PREFIX "" OUTPUT_NAME vbackup SUFFIX .wcx64)
	else()
		set_target_properties(vbkwcx PROPERTIES PREFIX "" OUTPUT_NAME vbackup SUFFIX .wcx)
	endif()

	install(TARGETS vbkwcx LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR})

	#	The newest StarLet kit beside this tree (or beside its parent), unless one is named
	file(GLOB L_SLDIRS ${CMAKE_CURRENT_SOURCE_DIR}/../starlet-* ${CMAKE_CURRENT_SOURCE_DIR}/../../starlet-*)
	list(SORT L_SLDIRS COMPARE NATURAL ORDER DESCENDING)
	find_path(VBACKUP_STARLET_SRC NAMES utility_routines.c cli_routines.c PATHS ${L_SLDIRS} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

	if ( NOT VBACKUP_STARLET_SRC )
		message(STATUS "VBACKUP: Windows - vbkx.exe and the WCX plugin alone: no StarLet sources (VBACKUP_STARLET_SRC)")

		return()
	endif()

	#	StarLet as it is: its few gaps on Windows are filled by win/vbkwcrt.h, put in front
	add_library(vbkstarletw STATIC ${VBACKUP_STARLET_SRC}/utility_routines.c ${VBACKUP_STARLET_SRC}/cli_routines.c ${VBACKUP_STARLET_SRC}/starlet.c)
	target_include_directories(vbkstarletw PUBLIC ${VBACKUP_STARLET_SRC} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/win/inc)
	target_compile_definitions(vbkstarletw PRIVATE _FILE_OFFSET_BITS=64 NDEBUG)
	target_compile_options(vbkstarletw PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/win/vbkwcrt.h -w)

	add_executable(vbackup ${VBACKUP_WINSRC} ${VBACKUP_WINLIB})
	target_include_directories(vbackup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/win/inc ${CMAKE_CURRENT_SOURCE_DIR}/win
		${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/lib)
	target_compile_definitions(vbackup PRIVATE _FILE_OFFSET_BITS=64 __USE_MINGW_ANSI_STDIO=1 NDEBUG VBK_STARLET
		VBACKUP_K_IDENT="${VBACKUP$K_IDENT}")
	target_compile_options(vbackup PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/win/vbkwin.h)
	target_link_libraries(vbackup PRIVATE vbkstarletw vbackup_options bcrypt shell32 ws2_32 advapi32)
	target_link_options(vbackup PRIVATE -static -pthread -Wl,--stack,8388608)	# 8 MB as on Linux: paths of UTF-16 live on the stack

	#	The vector code: GCC keeps the stack of Windows at 16 octets, a spilled __m256i must not be loaded aligned
	include(CheckCCompilerFlag)
	check_c_compiler_flag(-Wa,-muse-unaligned-vector-move VBACKUP_HAVE_UNALIGNED)

	if ( VBACKUP_HAVE_UNALIGNED )
		target_compile_options(vbackup PRIVATE -Wa,-muse-unaligned-vector-move)
	endif()

	install(TARGETS vbackup RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

	#	test/units.c on Windows: the core, the parity by the vector code, the cipher - run under wine by test/winutil.sh
	add_executable(units test/units.c src/vbkosw.c src/vbkmsg.c ${VBACKUP_WINLIB})
	target_include_directories(units PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/win/inc ${CMAKE_CURRENT_SOURCE_DIR}/win
		${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/lib)
	target_compile_definitions(units PRIVATE _FILE_OFFSET_BITS=64 __USE_MINGW_ANSI_STDIO=1 NDEBUG VBK_STARLET)
	target_compile_options(units PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/win/vbkwin.h)
	target_link_libraries(units PRIVATE vbkstarletw vbackup_options bcrypt shell32 ws2_32)
	target_link_options(units PRIVATE -static -pthread)

	if ( VBACKUP_HAVE_UNALIGNED )
		target_compile_options(units PRIVATE -Wa,-muse-unaligned-vector-move)
	endif()

	#	A stand-in for ssh.exe, for node::file in test/winutil.sh
	add_executable(fakessh test/fakessh.c)
	target_link_libraries(fakessh PRIVATE vbackup_options)
	target_link_options(fakessh PRIVATE -static)

	message(STATUS "VBACKUP: Windows - vbackup.exe, vbkx.exe and the WCX plugin; StarLet from ${VBACKUP_STARLET_SRC}")

	return()
endif()

#
#	HELP before StarLet: the HELP package asks for StarLet itself, and the
#	copy it was built against is then the one this product links.
#
option(VBACKUP$M_HELPAPI "Answer /HELP out of the help library when HELP is found" ON)

if ( VBACKUP$M_HELPAPI )
	find_package(HELP 1.0 QUIET)
endif()

if ( HELP_FOUND )
	message(STATUS "VBACKUP: /HELP answered out of the library, HELP ${HELP_VERSION} from ${HELP_DIR}")
else()
	message(STATUS "VBACKUP: /HELP answered by the summary built into the image")
endif()

#	StarLet - required, installed: CLI$, $PUTMSG/FAO, CRC-32, pattern match
find_package(StarLet 1.6 REQUIRED CONFIG)

#	The writer thread of the saveset core - libc itself on a current glibc
find_package(Threads REQUIRED)

message(STATUS "VBACKUP: StarLet ${StarLet_VERSION} from ${StarLet_DIR}")

#
#	The saveset core: the format, the writer, the reader.  A library of
#	its own, so that the reader can later serve other front ends too.
#
add_library(vbkcore STATIC
	lib/vbkfmt.c
	lib/vbkwrt.c
	lib/vbkrd.c
	lib/vbklz4.c
	lib/vbkcrp.c
	lib/vbkvms.c
	lib/vbkrs.c
	lib/vbkpar.c)

target_include_directories(vbkcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(vbkcore PUBLIC StarLet::starlet Threads::Threads PRIVATE vbackup_options)

add_executable(vbackup
	src/vbackup.c
	src/vbkmsg.c
	src/vbkatr.c
	src/vbkwlk.c
	src/vbksav.c
	src/vbkrst.c
	src/vbklst.c
	src/vbkcmp.c
	src/vbkhlp.c
	src/vbkhsh.c
	src/vbkjnl.c
	src/vbkcpy.c
	src/vbkphy.c
	src/vbkimg.c
	src/vbkzpl.c
	src/vbkkey.c
	src/vbkxfr.c
	src/vbkrsh.c
	src/vbkvms.c
	src/vbkpre.c)

#
#	VBKX, the stand-alone extractor: the reading core and libc, nothing
#	else; linked statically where the system has a static libc, so that
#	it runs on a machine VBACKUP is not installed on
#
add_executable(vbkx tools/vbkx.c)
target_link_libraries(vbkx PRIVATE vbkcore StarLet::starlet vbackup_options)

option(VBACKUP$M_VBKXSTATIC "Link VBKX statically when the system allows it" ON)

if ( VBACKUP$M_VBKXSTATIC )
	include(CheckCSourceCompiles)
	set(CMAKE_REQUIRED_LINK_OPTIONS -static)
	check_c_source_compiles("int main (void) { return 0; }" VBACKUP_HAVE_STATIC)
	unset(CMAKE_REQUIRED_LINK_OPTIONS)

	if ( VBACKUP_HAVE_STATIC )
		target_link_options(vbkx PRIVATE -static)
	endif()
endif()

#
#	For the geeks: the extractors of last resort in Go, in Rust and in
#	Perl, tools/go, tools/rust and tools/perl.  They need nothing of this
#	build - each is one command line (see the head of their source) - and
#	are built here only when the toolchain is there; the Perl one is not
#	built at all, only tested when there is a perl.
#
option(VBACKUP$M_GEEKS "Build vbkx-go and vbkx-rs when Go and Rust are found" ON)

set(VBACKUP_GEEKS "")

if ( VBACKUP$M_GEEKS )
	find_program(VBACKUP_GO go)
	find_program(VBACKUP_RUSTC rustc)

	if ( VBACKUP_GO )
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vbkx-go
			COMMAND ${CMAKE_COMMAND} -E env CGO_ENABLED=0 GOCACHE=${CMAKE_CURRENT_BINARY_DIR}/gocache
				${VBACKUP_GO} build -o ${CMAKE_CURRENT_BINARY_DIR}/vbkx-go main.go
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools/go
			DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/go/main.go
			COMMENT "Building vbkx-go")
		add_custom_target(vbkx-go ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/vbkx-go)
		list(APPEND VBACKUP_GEEKS go)
	endif()

	if ( VBACKUP_RUSTC )
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vbkx-rs
			COMMAND ${VBACKUP_RUSTC} -O -C overflow-checks=on -C strip=symbols -o ${CMAKE_CURRENT_BINARY_DIR}/vbkx-rs
				${CMAKE_CURRENT_SOURCE_DIR}/tools/rust/src/main.rs
			DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/rust/src/main.rs
			COMMENT "Building vbkx-rs")
		add_custom_target(vbkx-rs ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/vbkx-rs)
		list(APPEND VBACKUP_GEEKS rs)
	endif()

	find_program(VBACKUP_PERL perl)

	if ( VBACKUP_PERL )
		list(APPEND VBACKUP_GEEKS pl)
	endif()

	message(STATUS "VBACKUP: extractors for the geeks: ${VBACKUP_GEEKS}")
endif()

#
#	vbkx.exe from here, when MinGW-w64 is at hand - the same command line
#	as tools/Makefile.win - and test/win.sh runs it under wine, when there
#	is a wine
#
option(VBACKUP$M_WINVBKX "Build vbkx.exe with MinGW-w64 and test it under wine, when they are found" ON)

set(VBACKUP_WINVBKX "")

if ( VBACKUP$M_WINVBKX )
	find_program(VBACKUP_MINGW x86_64-w64-mingw32-gcc)
	find_program(VBACKUP_WINE wine)

	if ( VBACKUP_MINGW )
		set(L_SRC tools/vbkx.c lib/vbkfmt.c lib/vbkrd.c lib/vbklz4.c lib/vbkcrp.c lib/vbkvms.c lib/vbkrs.c)

		#	In a directory of its own: as a file next to them, CMake took vbkx.exe for the target vbkx
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/win/vbkx.exe
			COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/win
			COMMAND ${VBACKUP_MINGW} -O2 -Wall -Wextra -Wno-unused-parameter -Ilib -o ${CMAKE_CURRENT_BINARY_DIR}/win/vbkx.exe
				${L_SRC} -static -lshell32
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			DEPENDS ${L_SRC} lib/vbkos.h lib/vbkfmt.h lib/vbkrd.h lib/vbklz4.h lib/vbkcrp.h
			COMMENT "Building vbkx.exe (MinGW-w64)")
		add_custom_target(vbkx-exe ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/win/vbkx.exe)

		set(VBACKUP_WINVBKX ${CMAKE_CURRENT_BINARY_DIR}/win/vbkx.exe)
		message(STATUS "VBACKUP: vbkx.exe by ${VBACKUP_MINGW}")
	endif()
endif()

#
#	vbackup.exe, stage 13: this very tree built for Windows in a tree of
#	its own - the WIN32 branch above, by cmake/mingw-w64.cmake - when
#	MinGW-w64 and the sources of StarLet (the kit of the version found)
#	are at hand; test/winutil.sh runs it under wine
#
set(VBACKUP_WINEXE "")

if ( VBACKUP$M_WINVBKX AND VBACKUP_MINGW )
	file(GLOB L_SLDIRS ${CMAKE_CURRENT_SOURCE_DIR}/../starlet-${StarLet_VERSION} ${CMAKE_CURRENT_SOURCE_DIR}/../../starlet-${StarLet_VERSION})
	find_path(VBACKUP_STARLET_SRC NAMES utility_routines.c cli_routines.c PATHS ${L_SLDIRS} NO_DEFAULT_PATH)

	if ( VBACKUP_STARLET_SRC )
		include(ExternalProject)

		ExternalProject_Add(vbackup-exe
			SOURCE_DIR	${CMAKE_CURRENT_SOURCE_DIR}
			BINARY_DIR	${CMAKE_CURRENT_BINARY_DIR}/winexe
			CMAKE_ARGS	-DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
					-DVBACKUP_STARLET_SRC=${VBACKUP_STARLET_SRC}
			INSTALL_COMMAND	""
			BUILD_ALWAYS	ON
			BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/winexe/vbackup.exe)

		set(VBACKUP_WINEXE ${CMAKE_CURRENT_BINARY_DIR}/winexe/vbackup.exe)
		message(STATUS "VBACKUP: vbackup.exe by ${VBACKUP_MINGW}, StarLet from ${VBACKUP_STARLET_SRC}")
	else()
		message(STATUS "VBACKUP: no vbackup.exe - no StarLet ${StarLet_VERSION} sources beside this tree (VBACKUP_STARLET_SRC)")
	endif()
endif()

#
#	The WCX plugin (plugins/wcx/vbkwcx.c), stage 5: vbackup.wcx for Double
#	Commander on Linux - a shared object without StarLet (VBK_NOSTARLET:
#	StarLet is a static library, not position-independent); with
#	MinGW-w64, vbackup.wcx64 and the 32-bit vbackup.wcx for Total
#	Commander, the same command lines as plugins/wcx/Makefile
#
add_library(vbkwcx MODULE plugins/wcx/vbkwcx.c lib/vbkfmt.c lib/vbkrd.c lib/vbklz4.c lib/vbkcrp.c lib/vbkrs.c)
target_include_directories(vbkwcx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_compile_definitions(vbkwcx PRIVATE VBK_NOSTARLET)
target_link_libraries(vbkwcx PRIVATE vbackup_options)
set_target_properties(vbkwcx PROPERTIES PREFIX "" OUTPUT_NAME vbackup SUFFIX .wcx
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/wcx C_VISIBILITY_PRESET hidden)

install(TARGETS vbkwcx LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/vbackup/plugins/wcx)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugins/wcx/pluginst.inf DESTINATION ${CMAKE_INSTALL_DATADIR}/vbackup/plugins/wcx)

set(VBACKUP_WCX64 "")
set(VBACKUP_WCX32 "")

if ( VBACKUP$M_WINVBKX )
	find_program(VBACKUP_MINGW32 i686-w64-mingw32-gcc)

	set(L_SRC plugins/wcx/vbkwcx.c lib/vbkfmt.c lib/vbkrd.c lib/vbklz4.c lib/vbkcrp.c lib/vbkrs.c)
	set(L_DEP ${L_SRC} lib/vbkos.h lib/vbkfmt.h lib/vbkrd.h lib/vbklz4.h lib/vbkcrp.h)

	if ( VBACKUP_MINGW )
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/win/vbackup.wcx64
			COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/win
			COMMAND ${VBACKUP_MINGW} -O2 -Wall -Wextra -Wno-unused-parameter -shared -Ilib -o ${CMAKE_CURRENT_BINARY_DIR}/win/vbackup.wcx64
				${L_SRC} -static -Wl,--kill-at
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			DEPENDS ${L_DEP}
			COMMENT "Building vbackup.wcx64 (MinGW-w64)")
		add_custom_target(vbkwcx-win64 ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/win/vbackup.wcx64)
		set(VBACKUP_WCX64 ${CMAKE_CURRENT_BINARY_DIR}/win/vbackup.wcx64)
		install(FILES ${VBACKUP_WCX64} DESTINATION ${CMAKE_INSTALL_DATADIR}/vbackup/plugins/wcx/win)
	endif()

	if ( VBACKUP_MINGW32 )
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/win32/vbackup.wcx
			COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/win32
			COMMAND ${VBACKUP_MINGW32} -O2 -Wall -Wextra -Wno-unused-parameter -shared -Ilib -o ${CMAKE_CURRENT_BINARY_DIR}/win32/vbackup.wcx
				${L_SRC} -static -Wl,--kill-at
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			DEPENDS ${L_DEP}
			COMMENT "Building vbackup.wcx (MinGW-w64, 32 bits)")
		add_custom_target(vbkwcx-win32 ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/win32/vbackup.wcx)
		set(VBACKUP_WCX32 ${CMAKE_CURRENT_BINARY_DIR}/win32/vbackup.wcx)
		install(FILES ${VBACKUP_WCX32} DESTINATION ${CMAKE_INSTALL_DATADIR}/vbackup/plugins/wcx/win)
	endif()

	if ( VBACKUP_WCX64 OR VBACKUP_WCX32 )
		install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugins/wcx/pluginst.inf DESTINATION ${CMAKE_INSTALL_DATADIR}/vbackup/plugins/wcx/win)
		message(STATUS "VBACKUP: WCX plugin for Windows: ${VBACKUP_WCX64} ${VBACKUP_WCX32}")
	endif()
endif()

target_include_directories(vbackup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vbackup PRIVATE vbkcore StarLet::starlet vbackup_options)

#	A name carrying '$' is expanded away by MAKE, so the definitions are plain
target_compile_definitions(vbackup PRIVATE VBACKUP_K_IDENT="${VBACKUP$K_IDENT}")

set(VBACKUP$K_HLBDIR "${CMAKE_INSTALL_DATAROOTDIR}/help" CACHE STRING "Directory the help libraries are installed into")

if ( HELP_FOUND )
	target_link_libraries(vbackup PRIVATE HELP::help)

	target_compile_definitions(vbackup PRIVATE
		VBACKUP_WITH_HLB=1
		VBACKUP_K_HLBSPEC="${CMAKE_INSTALL_PREFIX}/${VBACKUP$K_HLBDIR}/vbackup.hlb")
endif()

#
#	The help libraries: doc/vbackuplib[_ru].md begin with the one topic
#	VBACKUP, so they are translated NOFACILITY - rooted at the project
#	name they would sink a level and "HELP VBACKUP /VERIFY" would find
#	nothing (SEARCH X-22).
#
option(VBACKUP$M_HELPLIB "Translate and install the help libraries" ON)

if ( VBACKUP$M_HELPLIB AND HELP_FOUND )
	help_add_library(NAME vbackuplib ALL NOFACILITY
		SOURCE		${CMAKE_CURRENT_SOURCE_DIR}/doc/vbackuplib.md
		OUTPUT		${CMAKE_CURRENT_BINARY_DIR}/vbackup.hlb
		IDENT		${VBACKUP$K_IDENT}
		DESTINATION	${VBACKUP$K_HLBDIR})

	help_add_library(NAME vbackuplib_ru ALL NOFACILITY
		SOURCE		${CMAKE_CURRENT_SOURCE_DIR}/doc/vbackuplib_ru.md
		OUTPUT		${CMAKE_CURRENT_BINARY_DIR}/vbackup_ru.hlb
		IDENT		${VBACKUP$K_IDENT}RU
		DESTINATION	${VBACKUP$K_HLBDIR}/ru)
elseif ( VBACKUP$M_HELPLIB )
	message(STATUS "VBACKUP: no HELP package found, the help libraries are not built")
endif()

#
#	Installation:
#
#		<prefix>/bin/vbackup
#		<prefix>/share/man/man1/vbackup.1
#		<prefix>/share/help/vbackup.hlb, ru/vbackup_ru.hlb
#		<prefix>/share/doc/vbackup/README.md, format.md, vmsbackup.md, vbackuplib[_ru].md
#
option(VBACKUP$M_INSTALL_DOC "Install the manual page and the documents" ON)

install(TARGETS vbackup vbkx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if ( VBACKUP$M_INSTALL_DOC )
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doc/vbackup.1.in ${CMAKE_CURRENT_BINARY_DIR}/vbackup.1 @ONLY)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doc/vbkx.1.in ${CMAKE_CURRENT_BINARY_DIR}/vbkx.1 @ONLY)

	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/vbackup.1 ${CMAKE_CURRENT_BINARY_DIR}/vbkx.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

	install(FILES
		${CMAKE_CURRENT_SOURCE_DIR}/README.md
		${CMAKE_CURRENT_SOURCE_DIR}/doc/format.md
		${CMAKE_CURRENT_SOURCE_DIR}/doc/vmsbackup.md
		${CMAKE_CURRENT_SOURCE_DIR}/doc/vbackuplib.md
		${CMAKE_CURRENT_SOURCE_DIR}/doc/vbackuplib_ru.md
		DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

#
#	The plugins of the file managers (stage 5): the files for a hand
#	installation always under share/vbackup/plugins; and where MC and
#	far2l are found, put into their places - uvbk into the extfs of MC,
#	the sections into mc.ext.ini and into the custom.ini of MultiArc
#	(between markers: the uninstallation takes them out exactly).
#
option(VBACKUP$M_PLUGINS "Install the file manager plugins where MC and far2l are found" ON)

install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/plugins/mc/uvbk DESTINATION ${CMAKE_INSTALL_DATADIR}/vbackup/plugins/mc)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugins/mc/mc.ext.ini.vbackup DESTINATION ${CMAKE_INSTALL_DATADIR}/vbackup/plugins/mc)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugins/far/vbackup.ini DESTINATION ${CMAKE_INSTALL_DATADIR}/vbackup/plugins/far)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugins/magic/vbackup.magic DESTINATION ${CMAKE_INSTALL_DATADIR}/vbackup/plugins/magic)

set(VBACKUP_MCEXTFS "")
set(VBACKUP_MCEXTINI "")
set(VBACKUP_FARCUSTOM "")
set(VBACKUP_MAGIC "")

if ( VBACKUP$M_PLUGINS )
	foreach ( D /usr/lib/mc/extfs.d /usr/libexec/mc/extfs.d /usr/local/libexec/mc/extfs.d )
		if ( IS_DIRECTORY ${D} AND NOT VBACKUP_MCEXTFS )
			set(VBACKUP_MCEXTFS ${D})
		endif()
	endforeach()

	foreach ( F /etc/mc/mc.ext.ini /usr/local/etc/mc/mc.ext.ini )
		if ( EXISTS ${F} AND NOT VBACKUP_MCEXTINI )
			set(VBACKUP_MCEXTINI ${F})
		endif()
	endforeach()

	foreach ( F /usr/share/far2l/Plugins/multiarc/plug/custom.ini /usr/local/share/far2l/Plugins/multiarc/plug/custom.ini )
		if ( EXISTS ${F} AND NOT VBACKUP_FARCUSTOM )
			set(VBACKUP_FARCUSTOM ${F})
		endif()
	endforeach()

	#	The local magic of file(1): MC knows a saveset by its contents, whatever its name
	if ( EXISTS /etc/magic )
		set(VBACKUP_MAGIC /etc/magic)
	endif()

	if ( VBACKUP_MCEXTFS )
		install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/plugins/mc/uvbk DESTINATION ${VBACKUP_MCEXTFS})
	endif()

	if ( VBACKUP_MCEXTINI )
		install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} -DMODE=add -DTARGET=\$ENV{DESTDIR}${VBACKUP_MCEXTINI}
			-DSNIPPET=${CMAKE_CURRENT_SOURCE_DIR}/plugins/mc/mc.ext.ini.vbackup \"-DBEFORE=[Default]\"
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugcfg.cmake)")
	endif()

	if ( VBACKUP_FARCUSTOM )
		install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} -DMODE=add -DTARGET=\$ENV{DESTDIR}${VBACKUP_FARCUSTOM}
			-DSNIPPET=${CMAKE_CURRENT_SOURCE_DIR}/plugins/far/vbackup.ini -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugcfg.cmake)")
	endif()

	if ( VBACKUP_MAGIC )
		install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} -DMODE=add -DTARGET=\$ENV{DESTDIR}${VBACKUP_MAGIC}
			-DSNIPPET=${CMAKE_CURRENT_SOURCE_DIR}/plugins/magic/vbackup.magic -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugcfg.cmake)")
	endif()

	message(STATUS "VBACKUP: plugins - MC extfs ${VBACKUP_MCEXTFS}, ${VBACKUP_MCEXTINI}; far2l ${VBACKUP_FARCUSTOM}; magic ${VBACKUP_MAGIC}")
endif()

if ( NOT TARGET uninstall )
	set(VBACKUP_PLUGCFG ${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugcfg.cmake)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake @ONLY)

	add_custom_target(uninstall
		COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake
		COMMENT "Removing the files listed in install_manifest.txt")
endif()

#
#	The kit: vbackup-<ident>.tar.gz, the directory within it named the same;
#	in a git tree what is committed only (cmake/kit.cmake)
#
set(VBACKUP$K_KIT "vbackup-${VBACKUP$K_IDENT}")

add_custom_target(kit
	COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DOUT=${CMAKE_CURRENT_BINARY_DIR} -DKIT=${VBACKUP$K_KIT}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/kit.cmake
	COMMENT "Making ${VBACKUP$K_KIT}.tar.gz")

if ( BUILD_TESTING )
	enable_testing()

	if ( VBACKUP$M_TESTS )
		add_subdirectory(test)
	endif()
endif()
