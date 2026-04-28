# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# CMakeModules/openssl_build.cmake — Build OpenSSL from source for cross-compilation
#
# OpenSSL uses Perl/Configure, not CMake.  This module downloads the source via
# CPM and builds it with execute_process during cmake configure.
#
# Native MSYS2 builds also prefer the downloaded source build so behavior
# stays consistent across Windows and Linux-to-Windows cross builds.

set(_OPENSSL_VERSION "3.4.1")
set(_OPENSSL_INSTALL "${CMAKE_BINARY_DIR}/externals/openssl-install")
function(_citron_detect_openssl_libdir out_var)
    set(_detected "")
    foreach(_candidate_libdir lib64 lib)
        if (EXISTS "${_OPENSSL_INSTALL}/${_candidate_libdir}/libssl.a")
            set(_detected "${_candidate_libdir}")
            break()
        endif()
    endforeach()
    set(${out_var} "${_detected}" PARENT_SCOPE)
endfunction()

function(_citron_publish_openssl_imports)
    _citron_detect_openssl_libdir(_OPENSSL_PUBLISH_LIBDIR)
    if (NOT _OPENSSL_PUBLISH_LIBDIR)
        message(WARNING "[OpenSSL] Static libraries not found under ${_OPENSSL_INSTALL}/{lib64,lib}")
        return()
    endif()

    set(OPENSSL_ROOT_DIR "${_OPENSSL_INSTALL}" CACHE PATH "" FORCE)
    set(OPENSSL_INCLUDE_DIR "${_OPENSSL_INSTALL}/include" CACHE PATH "" FORCE)
    set(OPENSSL_SSL_LIBRARY "${_OPENSSL_INSTALL}/${_OPENSSL_PUBLISH_LIBDIR}/libssl.a" CACHE FILEPATH "" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${_OPENSSL_INSTALL}/${_OPENSSL_PUBLISH_LIBDIR}/libcrypto.a" CACHE FILEPATH "" FORCE)
    set(OPENSSL_FOUND TRUE CACHE BOOL "" FORCE)

    if (NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
    endif()
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "ws2_32;crypt32")

    if (NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
    endif()
    set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "OpenSSL::Crypto")
endfunction()

_citron_detect_openssl_libdir(_OPENSSL_LIBDIR)

# If a previous configure left stale OpenSSL cache entries behind, clear them
# before we decide whether to use the cached install or rebuild.
if (CMAKE_CROSSCOMPILING)
    unset(OPENSSL_FOUND CACHE)
    unset(OPENSSL_ROOT_DIR CACHE)
    unset(OPENSSL_INCLUDE_DIR CACHE)
    unset(OPENSSL_SSL_LIBRARY CACHE)
    unset(OPENSSL_CRYPTO_LIBRARY CACHE)
endif()

# Reuse a previously built cross OpenSSL only when the install tree is intact.
if (_OPENSSL_LIBDIR)
    _citron_publish_openssl_imports()
    message(STATUS "[OpenSSL] Using cached static build at ${_OPENSSL_INSTALL}")
    return()
endif()

# ── Download source via CPM ──────────────────────────────────────────────────
CPMAddPackage(
    NAME openssl_src
    URL "https://github.com/openssl/openssl/releases/download/openssl-${_OPENSSL_VERSION}/openssl-${_OPENSSL_VERSION}.tar.gz"
    DOWNLOAD_ONLY YES
)

if (NOT openssl_src_ADDED)
    message(WARNING "[OpenSSL] Source download failed — OpenSSL will not be available")
    return()
endif()

# ── Build from source ────────────────────────────────────────────────────────
find_program(_PERL perl REQUIRED)
if (NOT _PERL)
    message(FATAL_ERROR "[OpenSSL] Perl is required to build OpenSSL from source")
endif()

message(STATUS "[OpenSSL] Building OpenSSL ${_OPENSSL_VERSION} from source (static, mingw64)...")

# OpenSSL's Configure script often generates broken relative paths in the Makefile
# when the source and build directories are on different drives (e.g. source on C:,
# build on D:).  To avoid this, we copy the source into the build directory.
set(_OPENSSL_BUILD_DIR "${CMAKE_BINARY_DIR}/externals/openssl-build")
set(_OPENSSL_LOCAL_SRC "${_OPENSSL_BUILD_DIR}/src")

if (NOT EXISTS "${_OPENSSL_LOCAL_SRC}/Configure")
    message(STATUS "[OpenSSL] Copying source to build directory to avoid cross-drive path issues...")
    file(REMOVE_RECURSE "${_OPENSSL_LOCAL_SRC}")
    file(COPY "${openssl_src_SOURCE_DIR}/" DESTINATION "${_OPENSSL_LOCAL_SRC}")
endif()

# Determine the OpenSSL and cross-compile prefix
set(_OPENSSL_TARGET "mingw64")
set(_OPENSSL_CROSS "")
set(_OPENSSL_CC "${CMAKE_C_COMPILER}")
set(_OPENSSL_AR "${CMAKE_AR}")
set(_OPENSSL_RANLIB "${CMAKE_RANLIB}")
set(_OPENSSL_RC "${CMAKE_RC_COMPILER}")

if (CMAKE_C_COMPILER)
    get_filename_component(_CC_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(_CC_NAME "${CMAKE_C_COMPILER}" NAME)
    # Extract cross-prefix: x86_64-w64-mingw32-clang -> x86_64-w64-mingw32-
    string(REGEX REPLACE "clang[^/]*$" "" _OPENSSL_CROSS "${_CC_NAME}")
endif()

# OpenSSL prefixes the compiler name with --cross-compile-prefix, so the tool
# names passed to Configure must be the unprefixed executable names.
if (WIN32 OR _OPENSSL_CROSS)
    set(_OPENSSL_CC clang)
    set(_OPENSSL_AR llvm-ar)
    set(_OPENSSL_RANLIB llvm-ranlib)
    set(_OPENSSL_RC windres)
endif()

file(MAKE_DIRECTORY "${_OPENSSL_BUILD_DIR}")

# Configure
execute_process(
    COMMAND ${_PERL} "${_OPENSSL_LOCAL_SRC}/Configure"
        ${_OPENSSL_TARGET}
        --prefix=${_OPENSSL_INSTALL}
        --cross-compile-prefix=${_OPENSSL_CROSS}
        no-shared
        no-tests
        no-docs
        no-apps
        "CC=${_OPENSSL_CC}"
        "AR=${_OPENSSL_AR}"
        "RANLIB=${_OPENSSL_RANLIB}"
        "RC=${_OPENSSL_RC}"
    WORKING_DIRECTORY "${_OPENSSL_BUILD_DIR}"
    RESULT_VARIABLE _ssl_config_result
    OUTPUT_QUIET
)

if (NOT _ssl_config_result EQUAL 0)
    message(WARNING "[OpenSSL] Configure failed (exit ${_ssl_config_result})")
    return()
endif()

# Build + install (just libraries, no apps)
include(ProcessorCount)
ProcessorCount(_NPROC)
if (_NPROC EQUAL 0)
    set(_NPROC 4)
endif()

execute_process(
    COMMAND make -j${_NPROC} build_libs
    WORKING_DIRECTORY "${_OPENSSL_BUILD_DIR}"
    RESULT_VARIABLE _ssl_build_result
    OUTPUT_QUIET ERROR_QUIET
)

if (NOT _ssl_build_result EQUAL 0)
    message(WARNING "[OpenSSL] Build failed (exit ${_ssl_build_result})")
    return()
endif()

execute_process(
    COMMAND make install_sw
    WORKING_DIRECTORY "${_OPENSSL_BUILD_DIR}"
    RESULT_VARIABLE _ssl_install_result
    OUTPUT_QUIET ERROR_QUIET
)

if (NOT _ssl_install_result EQUAL 0)
    message(WARNING "[OpenSSL] Install failed (exit ${_ssl_install_result})")
    return()
endif()

message(STATUS "[OpenSSL] Successfully built static OpenSSL ${_OPENSSL_VERSION}")

_citron_publish_openssl_imports()
