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
