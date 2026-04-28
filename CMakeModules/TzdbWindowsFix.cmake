# SPDX-FileCopyrightText: 2025 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# CMakeModules/TzdbWindowsFix.cmake
#
# tzdb_to_nx builds 'zic' (the IANA timezone compiler) from C source using the
# host 'cc'.  On MSYS2/CLANG64 the host cc is Clang with Windows headers,
# which lack link(), symlink(), readlink(), and only expose a 1-arg mkdir().
# The build fails unconditionally on WIN32 with errors like:
#
#   zic.c:1455: error: call to undeclared function 'link'
#   zic.c:1486: error: call to undeclared function 'symlink'
#   zic.c:1535: error: call to undeclared function 'readlink'
#   zic.c:3912: error: too many arguments to function call (mkdir)
#
# The tzdb_to_nx README explicitly documents this limitation:
#   "This makes use a lot of Unix system calls...so it likely requires a bit
#    of work to port to a non-POSIX platform, such as Windows."
#
# The pre-built release artifact for firmware 17.0.0 already exists at
# https://github.com/lat9nq/tzdb_to_nx/releases/tag/221202 and is what
# CITRON_DOWNLOAD_TIME_ZONE_DATA downloads.  Force that path on WIN32.
#
# USAGE in dependencies.cmake — add ONE line before the tzdb_to_nx CPMAddPackage:
#
#   include(TzdbWindowsFix)       # <-- add this
#   if (NOT CITRON_TZDB_USE_CPM)  # <-- guard CPMAddPackage with this variable
#     CPMAddPackage(
#       NAME tzdb_to_nx
#       ...
#     )
#   endif()
#
# externals/nx_tzdb/CMakeLists.txt already has a branch for CITRON_DOWNLOAD_TIME_ZONE_DATA;
# this module forces it ON on WIN32 so that branch is taken automatically.

if (WIN32)
    # Force the pre-built download path.  This sets the same cache variable that
    # the user can also set manually, so the logic in nx_tzdb/CMakeLists.txt
    # needs no structural changes — just the condition extended (see below).
    set(CITRON_DOWNLOAD_TIME_ZONE_DATA ON CACHE BOOL
        "Pre-built timezone data (forced ON on WIN32: source build requires POSIX)"
        FORCE)
    # Signal to dependencies.cmake that CPMAddPackage should be skipped.
    set(CITRON_TZDB_USE_CPM FALSE)
    message(STATUS "[tzdb] WIN32 host: skipping tzdb_to_nx source build, "
                   "using pre-built release artifact instead.")
else()
    set(CITRON_TZDB_USE_CPM TRUE)
endif()
