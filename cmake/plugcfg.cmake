#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		cmake/plugcfg.cmake
#
#	ABSTRACT:	Put the section of VBACKUP into the configuration file of
#			a file manager, or take it out again: mc.ext.ini of
#			Midnight Commander, custom.ini of the MultiArc of far2l.
#
#	DESCRIPTION:	cmake -DMODE=add|remove -DTARGET=<file> [-DSNIPPET=<file>]
#			      [-DBEFORE=<line>] -P plugcfg.cmake
#
#			The section goes between two marker lines, so that it is
#			put in once only and taken out exactly; BEFORE - the
#			line it is put in front of ([Default] of mc.ext.ini, the
#			catch-all), else it goes to the end.  A target that is
#			not there is left alone: the file manager is not installed.
#
#	AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE:	 4-OCT-2026
#
#	MODIFICATION HISTORY:
#
#		 4-OCT-2026	RRL	X-01 : Initial version.
#
#---

set(L_BEGIN "# >>> VBACKUP - put in by its installation, taken out by its uninstallation")
set(L_END   "# <<< VBACKUP")

if ( NOT EXISTS "${TARGET}" )
	message(STATUS "VBACKUP: ${TARGET} is not there - left alone")
	return()
endif()

file(READ "${TARGET}" L_TEXT)

#	Out first, in both modes: an older section is replaced, not doubled
string(FIND "${L_TEXT}" "${L_BEGIN}" L_B)
string(FIND "${L_TEXT}" "${L_END}" L_E)

if ( (L_B GREATER -1) AND (L_E GREATER L_B) )
	string(LENGTH "${L_END}" L_ELEN)
	math(EXPR L_AFTER "${L_E} + ${L_ELEN} + 1")
	string(SUBSTRING "${L_TEXT}" 0 ${L_B} L_HEAD)
	string(SUBSTRING "${L_TEXT}" ${L_AFTER} -1 L_TAIL)
	set(L_TEXT "${L_HEAD}${L_TAIL}")
endif()

if ( MODE STREQUAL "add" )
	file(READ "${SNIPPET}" L_SNIP)
	set(L_BLOCK "${L_BEGIN}\n${L_SNIP}\n${L_END}\n")

	if ( BEFORE )
		string(FIND "${L_TEXT}" "\n${BEFORE}" L_AT)
	else()
		set(L_AT -1)
	endif()

	#	Exactly the block goes in - so the removal leaves the file as it was
	if ( L_AT GREATER -1 )
		math(EXPR L_AT "${L_AT} + 1")
		string(SUBSTRING "${L_TEXT}" 0 ${L_AT} L_HEAD)
		string(SUBSTRING "${L_TEXT}" ${L_AT} -1 L_TAIL)
		set(L_TEXT "${L_HEAD}${L_BLOCK}${L_TAIL}")
	else()
		set(L_TEXT "${L_TEXT}${L_BLOCK}")
	endif()

	message(STATUS "VBACKUP: its section put into ${TARGET}")
else()
	message(STATUS "VBACKUP: its section taken out of ${TARGET}")
endif()

file(WRITE "${TARGET}" "${L_TEXT}")
