#+++
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#	MODULE:		test/CMakeLists.txt
#	ABSTRACT:	Tests of the VBACKUP facility: the unit tests of the
#			saveset core and the functional test of the utility.
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#	CREATION DATE:	 3-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 6-OCT-2026	RRL	X-17 : winutil.sh - vbackup.exe under wine.
#
#		 5-OCT-2026	RRL	X-14 : damage.sh and smoke.sh with /PARITY.
#
#		 5-OCT-2026	RRL	X-13 : VMS, the savesets of OpenVMS BACKUP.
#
#		 4-OCT-2026	RRL	X-05 : WCX, the WCX plugin natively; WCXWIN, its Windows
#					builds under wine.
#
#		 4-OCT-2026	RRL	X-05 : PLUGINS, the plugins of MC and far2l.
#
#		 4-OCT-2026	RRL	X-04 : WIN, vbkx.exe under wine.
#
#		 4-OCT-2026	RRL	X-03 : GEEKS, vbkx-go and vbkx-rs, when they are built;
#					vbkx-pl, when there is a perl.
#
#		 3-OCT-2026	RRL	X-02 : DAMAGE, the saveset under random damage; VBKX
#					to SMOKE and DAMAGE.
#
#		 3-OCT-2026	RRL	X-01 : Initial version.
#---

add_executable(units units.c)
target_link_libraries(units PRIVATE vbkcore vbackup_options)

#	The scratch trees go under the build tree: an ext4/xfs one, where
#	user xattrs and chattr flags exist - a tmpfs /tmp has neither
add_test(NAME units COMMAND units ${CMAKE_CURRENT_BINARY_DIR}/units.d)

add_test(NAME smoke
	COMMAND ${CMAKE_COMMAND} -E env
		VBACKUP=$<TARGET_FILE:vbackup>
		VBKX=$<TARGET_FILE:vbkx>
		SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/smoke.d
		sh ${CMAKE_CURRENT_SOURCE_DIR}/smoke.sh)

#	Fewer rounds than a run by hand (ROUNDS=30 is the default): it is a test of every build
add_test(NAME damage
	COMMAND ${CMAKE_COMMAND} -E env
		VBACKUP=$<TARGET_FILE:vbackup>
		VBKX=$<TARGET_FILE:vbkx>
		SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/damage.d
		ROUNDS=15
		sh ${CMAKE_CURRENT_SOURCE_DIR}/damage.sh)

#	The savesets of OpenVMS BACKUP (test/vms): VBACKUP and vbkx against BACKUP's own listing
add_test(NAME vms
	COMMAND ${CMAKE_COMMAND} -E env
		VBACKUP=$<TARGET_FILE:vbackup>
		VBKX=$<TARGET_FILE:vbkx>
		SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/vms.d
		sh ${CMAKE_CURRENT_SOURCE_DIR}/vms.sh)

#	The extractors for the geeks, those that the toolchains here could build
if ( VBACKUP_GEEKS )
	set(L_ENV VBACKUP=$<TARGET_FILE:vbackup> VBKX=$<TARGET_FILE:vbkx> SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/geeks.d)

	if ( "go" IN_LIST VBACKUP_GEEKS )
		list(APPEND L_ENV VBKXGO=${CMAKE_BINARY_DIR}/vbkx-go)
	endif()

	if ( "rs" IN_LIST VBACKUP_GEEKS )
		list(APPEND L_ENV VBKXRS=${CMAKE_BINARY_DIR}/vbkx-rs)
	endif()

	#	Nothing to build: the script itself, run by perl
	if ( "pl" IN_LIST VBACKUP_GEEKS )
		list(APPEND L_ENV VBKXPL=${CMAKE_SOURCE_DIR}/tools/perl/vbkx.pl)
	endif()

	add_test(NAME geeks COMMAND ${CMAKE_COMMAND} -E env ${L_ENV} sh ${CMAKE_CURRENT_SOURCE_DIR}/geeks.sh)
endif()

#	vbkx.exe under wine: only when both are there (a few seconds)
if ( VBACKUP_WINVBKX AND VBACKUP_WINE )
	add_test(NAME win COMMAND ${CMAKE_COMMAND} -E env VBACKUP=$<TARGET_FILE:vbackup> VBKX=$<TARGET_FILE:vbkx>
		VBKXEXE=${VBACKUP_WINVBKX} WINE=${VBACKUP_WINE} SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/win.d
		sh ${CMAKE_CURRENT_SOURCE_DIR}/win.sh)
endif()

#	vbackup.exe under wine, both ways (half a minute); its objects for the look at its imports
if ( VBACKUP_WINEXE AND VBACKUP_WINE )
	find_program(VBACKUP_MINGWNM x86_64-w64-mingw32-nm)

	add_test(NAME winutil COMMAND ${CMAKE_COMMAND} -E env VBACKUP=$<TARGET_FILE:vbackup> VBACKUPEXE=${VBACKUP_WINEXE}
		WINE=${VBACKUP_WINE} OBJDIR=${CMAKE_BINARY_DIR}/winexe/CMakeFiles/vbackup.dir NM=${VBACKUP_MINGWNM}
		UNITSEXE=${CMAKE_BINARY_DIR}/winexe/units.exe
		SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/winutil.d sh ${CMAKE_CURRENT_SOURCE_DIR}/winutil.sh)
endif()

#
#	The WCX plugin through test/wcx_harness.c: the Linux .so always; the
#	Windows ones under wine, with a harness built by the same MinGW
#
add_executable(wcx_harness wcx_harness.c)
target_link_libraries(wcx_harness PRIVATE ${CMAKE_DL_LIBS} vbackup_options)
add_dependencies(wcx_harness vbkwcx)

add_test(NAME wcx COMMAND ${CMAKE_COMMAND} -E env VBACKUP=$<TARGET_FILE:vbackup> VBKX=$<TARGET_FILE:vbkx>
	SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/wcx.d
	sh ${CMAKE_CURRENT_SOURCE_DIR}/wcx.sh $<TARGET_FILE:wcx_harness> $<TARGET_FILE:vbkwcx>)

if ( VBACKUP_WINE AND (VBACKUP_WCX64 OR VBACKUP_WCX32) )
	set(L_PAIRS "")

	if ( VBACKUP_WCX64 )
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wcx_harness64.exe
			COMMAND ${VBACKUP_MINGW} -O2 -Wall -Wextra -o ${CMAKE_CURRENT_BINARY_DIR}/wcx_harness64.exe
				${CMAKE_CURRENT_SOURCE_DIR}/wcx_harness.c -static -lshell32
			DEPENDS wcx_harness.c
			COMMENT "Building wcx_harness64.exe (MinGW-w64)")
		add_custom_target(wcx-harness64 ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/wcx_harness64.exe)
		list(APPEND L_PAIRS ${CMAKE_CURRENT_BINARY_DIR}/wcx_harness64.exe ${VBACKUP_WCX64})
	endif()

	if ( VBACKUP_WCX32 )
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wcx_harness32.exe
			COMMAND ${VBACKUP_MINGW32} -O2 -Wall -Wextra -o ${CMAKE_CURRENT_BINARY_DIR}/wcx_harness32.exe
				${CMAKE_CURRENT_SOURCE_DIR}/wcx_harness.c -static -lshell32
			DEPENDS wcx_harness.c
			COMMENT "Building wcx_harness32.exe (MinGW-w64, 32 bits)")
		add_custom_target(wcx-harness32 ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/wcx_harness32.exe)
		list(APPEND L_PAIRS ${CMAKE_CURRENT_BINARY_DIR}/wcx_harness32.exe ${VBACKUP_WCX32})
	endif()

	add_test(NAME wcxwin COMMAND ${CMAKE_COMMAND} -E env VBACKUP=$<TARGET_FILE:vbackup> VBKX=$<TARGET_FILE:vbkx>
		WINE=${VBACKUP_WINE} SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/wcxwin.d
		sh ${CMAKE_CURRENT_SOURCE_DIR}/wcx.sh ${L_PAIRS})
endif()
#	The plugins of the file managers: the commands always; the real mc and far2l when tmux and they are there
add_test(NAME plugins
	COMMAND ${CMAKE_COMMAND} -E env
		VBACKUP=$<TARGET_FILE:vbackup>
		VBKX=$<TARGET_FILE:vbkx>
		SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/plugins.d
		sh ${CMAKE_CURRENT_SOURCE_DIR}/plugins.sh)
