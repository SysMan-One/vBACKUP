#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		cmake/kit.cmake
#
#	ABSTRACT:	The kit, vbackup-<ident>.tar.gz: what is committed, and
#			nothing else.
#
#	DESCRIPTION:	cmake -DSRC=<source tree> -DOUT=<build tree> -DKIT=<name>
#			      -P kit.cmake
#
#			In a git working tree the kit is made by git archive of
#			HEAD: a file that is not committed - a half-written manual,
#			a binary built in place, a document laid there to be read -
#			never gets into it.  What is changed and not committed is
#			not in the kit either, and that is said.  Without git (a tree
#			unpacked from a kit, which holds nothing else) the
#			directories of the product are copied as before.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 5-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 5-OCT-2026	RRL	X-01 : Initial version.
#
#---

set(L_TGZ "${OUT}/${KIT}.tar.gz")

find_program(L_GIT git)

if ( L_GIT )
	execute_process(COMMAND ${L_GIT} -C ${SRC} rev-parse --is-inside-work-tree
		OUTPUT_VARIABLE L_INSIDE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE L_RC)
endif()

if ( L_GIT AND (L_RC EQUAL 0) AND (L_INSIDE STREQUAL "true") )
	execute_process(COMMAND ${L_GIT} -C ${SRC} status --porcelain --untracked-files=no
		OUTPUT_VARIABLE L_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND ${L_GIT} -C ${SRC} rev-parse --short HEAD
		OUTPUT_VARIABLE L_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE)

	if ( L_DIRTY )
		message(WARNING "VBACKUP: changes not committed are not in the kit:\n${L_DIRTY}")
	endif()

	file(REMOVE ${L_TGZ})
	execute_process(COMMAND ${L_GIT} -C ${SRC} archive --format=tar.gz --prefix=${KIT}/ -o ${L_TGZ} HEAD
		RESULT_VARIABLE L_RC)

	if ( NOT (L_RC EQUAL 0) )
		message(FATAL_ERROR "VBACKUP: git archive failed (${L_RC})")
	endif()

	message(STATUS "VBACKUP: ${KIT}.tar.gz made by git archive of ${L_HEAD}")
	return()
endif()

#	No git: the directories of the product, the binaries built in place left out
set(L_DIR "${OUT}/kit/${KIT}")

file(REMOVE_RECURSE ${OUT}/kit)
file(MAKE_DIRECTORY ${L_DIR})

foreach(L_D src lib tools include doc test cmake plugins)
	file(COPY ${SRC}/${L_D} DESTINATION ${L_DIR})
endforeach()

file(COPY ${SRC}/CMakeLists.txt ${SRC}/README.md DESTINATION ${L_DIR})
file(REMOVE_RECURSE ${L_DIR}/tools/go/vbkx-go ${L_DIR}/tools/rust/vbkx-rs ${L_DIR}/tools/rust/vbkx-rs-check ${L_DIR}/tools/rust/target)

execute_process(COMMAND ${CMAKE_COMMAND} -E tar czf ${L_TGZ} ${KIT} WORKING_DIRECTORY ${OUT}/kit RESULT_VARIABLE L_RC)

if ( NOT (L_RC EQUAL 0) )
	message(FATAL_ERROR "VBACKUP: the kit could not be packed (${L_RC})")
endif()

message(STATUS "VBACKUP: ${KIT}.tar.gz made from the source tree (no git)")
