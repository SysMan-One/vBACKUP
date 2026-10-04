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
	${CMAKE_CURRENT_SOURCE_DIR}/test/*.c)

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
	lib/vbkrd.c)

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
#		<prefix>/share/doc/vbackup/README.md, format.md, vbackuplib[_ru].md
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
		${CMAKE_CURRENT_SOURCE_DIR}/doc/vbackuplib.md
		${CMAKE_CURRENT_SOURCE_DIR}/doc/vbackuplib_ru.md
		DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

if ( NOT TARGET uninstall )
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake @ONLY)

	add_custom_target(uninstall
		COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake
		COMMENT "Removing the files listed in install_manifest.txt")
endif()

#
#	The kit: vbackup-<ident>.tar.gz, the directory within it named the same
#
set(VBACKUP$K_KIT "vbackup-${VBACKUP$K_IDENT}")

add_custom_target(kit
	COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_CURRENT_BINARY_DIR}/kit
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/kit/${VBACKUP$K_KIT}
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/src     ${CMAKE_CURRENT_BINARY_DIR}/kit/${VBACKUP$K_KIT}/src
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/lib     ${CMAKE_CURRENT_BINARY_DIR}/kit/${VBACKUP$K_KIT}/lib
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/tools   ${CMAKE_CURRENT_BINARY_DIR}/kit/${VBACKUP$K_KIT}/tools
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}/kit/${VBACKUP$K_KIT}/include
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/doc     ${CMAKE_CURRENT_BINARY_DIR}/kit/${VBACKUP$K_KIT}/doc
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/test    ${CMAKE_CURRENT_BINARY_DIR}/kit/${VBACKUP$K_KIT}/test
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/cmake   ${CMAKE_CURRENT_BINARY_DIR}/kit/${VBACKUP$K_KIT}/cmake
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt ${CMAKE_CURRENT_SOURCE_DIR}/README.md
			${CMAKE_CURRENT_BINARY_DIR}/kit/${VBACKUP$K_KIT}/
	COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/kit
			${CMAKE_COMMAND} -E tar czf ${CMAKE_CURRENT_BINARY_DIR}/${VBACKUP$K_KIT}.tar.gz ${VBACKUP$K_KIT}
	COMMENT "Making ${VBACKUP$K_KIT}.tar.gz")

if ( BUILD_TESTING )
	enable_testing()

	if ( VBACKUP$M_TESTS )
		add_subdirectory(test)
	endif()
endif()
