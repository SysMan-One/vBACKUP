#+++
#
#	FACILITY:	VBACKUP - OpenVMS BACKUP-style saveset utility for Linux
#
#	MODULE:		cmake/mingw-w64.cmake
#
#	ABSTRACT:	The toolchain for the cross build of vbkx.exe, the
#			stand-alone extractor for Windows, with MinGW-w64:
#
#			    cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
#			    cmake --build build-win
#
#			CMakeLists.txt sees WIN32 and builds VBKX alone.
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

set(CMAKE_SYSTEM_NAME		Windows)
set(CMAKE_SYSTEM_PROCESSOR	x86_64)

set(CMAKE_C_COMPILER		x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER		x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH	/usr/x86_64-w64-mingw32)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM	NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY	ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE	ONLY)
