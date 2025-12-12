# SPDX-FileCopyrightText: 2017 yuzu Emulator Project
# SPDX-FileCopyrightText: 2025 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# This function downloads a binary library package from our external repo.
# Params:
#   remote_path: path to the file to download, relative to the remote repository root
#   prefix_var: name of a variable which will be set with the path to the extracted contents
set(CURRENT_MODULE_DIR ${CMAKE_CURRENT_LIST_DIR})
function(download_bundled_external remote_path lib_name prefix_var)

set(package_base_url "https://git.citron-emu.org/Citron/")
set(package_repo "no_platform")
set(package_extension "no_platform")
if (WIN32)
    set(package_repo "ext-windows-bin/raw/branch/master/")
    set(package_extension ".7z")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(package_repo "ext-linux-bin/raw/branch/main/")
    set(package_extension ".tar.xz")
elseif (ANDROID)
    # All Android binaries are on the main branch
    # Files are named with architecture suffix (e.g., ffmpeg-android-v5.1.LTS-aarch64.tar.xz)
    set(package_repo "ext-android-bin/raw/branch/main/")
    set(package_extension ".tar.xz")
else()
    message(FATAL_ERROR "No package available for this platform")
endif()
set(package_url "${package_base_url}${package_repo}")

set(prefix "${CMAKE_BINARY_DIR}/externals/${lib_name}")
if (NOT EXISTS "${prefix}")
    message(STATUS "Downloading binaries for ${lib_name}...")
    file(DOWNLOAD
        ${package_url}${remote_path}${lib_name}${package_extension}
        "${CMAKE_BINARY_DIR}/externals/${lib_name}${package_extension}" SHOW_PROGRESS)
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${CMAKE_BINARY_DIR}/externals/${lib_name}${package_extension}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/externals")
endif()
message(STATUS "Using bundled binaries at ${prefix}")
set(${prefix_var} "${prefix}" PARENT_SCOPE)
endfunction()

function(download_moltenvk_external platform version)
    set(MOLTENVK_DIR "${CMAKE_BINARY_DIR}/externals/MoltenVK")
    set(MOLTENVK_TAR "${CMAKE_BINARY_DIR}/externals/MoltenVK.tar")
    # Use Ryujinx MoltenVK build which is compiled with an older Metal SDK
    # This avoids MSL 3.2 bugs with thread_scope_subgroup and fixes text rendering issues
    string(TOLOWER "${platform}" MOLTENVK_ASSET_PLATFORM)
    if (NOT EXISTS ${MOLTENVK_DIR})
        if (NOT EXISTS ${MOLTENVK_TAR})
            file(DOWNLOAD https://github.com/V380-Ori/Ryujinx.MoltenVK/releases/download/${version}-ryujinx/MoltenVK-${MOLTENVK_ASSET_PLATFORM}.tar
                ${MOLTENVK_TAR} SHOW_PROGRESS)
        endif()

        execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${MOLTENVK_TAR}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/externals")
    endif()

    # Add the MoltenVK library path to the prefix so find_library can locate it.
    list(APPEND CMAKE_PREFIX_PATH "${MOLTENVK_DIR}/MoltenVK/dylib/${platform}")
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} PARENT_SCOPE)
endfunction()

# Determine installation parameters for OS, architecture, and compiler
function(determine_qt_parameters target host_out type_out arch_out arch_path_out host_type_out host_arch_out host_arch_path_out)
    if (WIN32)
        set(host "windows")
        set(type "desktop")

        if (NOT tool)
            if (MINGW)
                set(arch "win64_mingw")
                set(arch_path "mingw_64")
            elseif (MSVC)
                if ("arm64" IN_LIST ARCHITECTURE)
                    set(arch_path "msvc2019_arm64")
                elseif ("x86_64" IN_LIST ARCHITECTURE)
                    set(arch_path "msvc2019_64")
                else()
                    message(FATAL_ERROR "Unsupported bundled Qt architecture. Enable USE_SYSTEM_QT and provide your own.")
                endif()
                set(arch "win64_${arch_path}")

                if (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "AMD64")
                    set(host_arch_path "msvc2019_64")
                elseif (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "ARM64")
                    set(host_arch_path "msvc2019_64")
                endif()
                set(host_arch "win64_${host_arch_path}")
            else()
                message(FATAL_ERROR "Unsupported bundled Qt toolchain. Enable USE_SYSTEM_QT and provide your own.")
            endif()
        endif()
    elseif (APPLE)
        set(host "mac")
        set(type "desktop")
        set(arch "clang_64")
        set(arch_path "macos")
    else()
        set(host "linux")
        set(type "desktop")
        set(arch "gcc_64")
        set(arch_path "linux")
    endif()

    set(${host_out} "${host}" PARENT_SCOPE)
    set(${type_out} "${type}" PARENT_SCOPE)
    set(${arch_out} "${arch}" PARENT_SCOPE)
    set(${arch_path_out} "${arch_path}" PARENT_SCOPE)
    if (DEFINED host_type)
        set(${host_type_out} "${host_type}" PARENT_SCOPE)
    else()
        set(${host_type_out} "${type}" PARENT_SCOPE)
    endif()
    if (DEFINED host_arch)
        set(${host_arch_out} "${host_arch}" PARENT_SCOPE)
    else()
        set(${host_arch_out} "${arch}" PARENT_SCOPE)
    endif()
    if (DEFINED host_arch_path)
        set(${host_arch_path_out} "${host_arch_path}" PARENT_SCOPE)
    else()
        set(${host_arch_path_out} "${arch_path}" PARENT_SCOPE)
    endif()
endfunction()

# Download Qt binaries for a specific configuration.
function(download_qt_configuration prefix_out target host type arch arch_path base_path)
    if (target MATCHES "tools_.*")
        set(tool ON)
    else()
        set(tool OFF)
    endif()

    set(install_args -c "${CURRENT_MODULE_DIR}/aqt_config.ini")
    if (tool)
        set(prefix "${base_path}/Tools")
        set(install_args ${install_args} install-tool --outputdir ${base_path} ${host} desktop ${target})
    else()
        set(prefix "${base_path}/${target}/${arch_path}")
        set(install_args ${install_args} install-qt --outputdir ${base_path} ${host} ${type} ${target} ${arch}
                -m qtmultimedia --archives qttranslations qttools qtsvg qtbase)
    endif()

    if (NOT EXISTS "${prefix}")
        message(STATUS "Downloading Qt binaries for ${target}:${host}:${type}:${arch}:${arch_path}")
        set(AQT_PREBUILD_BASE_URL "https://github.com/miurahr/aqtinstall/releases/download/v3.3.0")
        if (WIN32)
            set(aqt_path "${base_path}/aqt.exe")
            if (NOT EXISTS "${aqt_path}")
                file(DOWNLOAD
                        ${AQT_PREBUILD_BASE_URL}/aqt.exe
                        ${aqt_path} SHOW_PROGRESS)
            endif()
            execute_process(COMMAND ${aqt_path} ${install_args}
                    WORKING_DIRECTORY ${base_path}
                    RESULT_VARIABLE aqt_result)
            if (NOT aqt_result EQUAL 0)
                message(WARNING "aqt install failed, trying without multimedia module")
                set(install_args -c "${CURRENT_MODULE_DIR}/aqt_config.ini" install-qt --outputdir ${base_path} ${host} ${type} ${target} ${arch} --archives qttranslations qttools qtsvg qtbase)
                execute_process(COMMAND ${aqt_path} ${install_args}
                        WORKING_DIRECTORY ${base_path})
            endif()
        elseif (APPLE)
            set(aqt_path "${base_path}/aqt-macos")
            if (NOT EXISTS "${aqt_path}")
                file(DOWNLOAD
                        ${AQT_PREBUILD_BASE_URL}/aqt-macos
                        ${aqt_path} SHOW_PROGRESS)
            endif()
            execute_process(COMMAND chmod +x ${aqt_path})
            execute_process(COMMAND ${aqt_path} ${install_args}
                    WORKING_DIRECTORY ${base_path})
        else()
            set(aqt_install_path "${base_path}/aqt")
            file(MAKE_DIRECTORY "${aqt_install_path}")

            execute_process(COMMAND python3 -m pip install --target=${aqt_install_path} aqtinstall
                    WORKING_DIRECTORY ${base_path})
            execute_process(COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${aqt_install_path} python3 -m aqt ${install_args}
                    WORKING_DIRECTORY ${base_path})
        endif()

        message(STATUS "Downloaded Qt binaries for ${target}:${host}:${type}:${arch}:${arch_path} to ${prefix}")
    endif()

    set(${prefix_out} "${prefix}" PARENT_SCOPE)
endfunction()

# This function downloads Qt using aqt.
# The path of the downloaded content will be added to the CMAKE_PREFIX_PATH.
# QT_TARGET_PATH is set to the Qt for the compile target platform.
# QT_HOST_PATH is set to a host-compatible Qt, for running tools.
# Params:
#   target: Qt dependency to install. Specify a version number to download Qt, or "tools_(name)" for a specific build tool.
function(download_qt target)
    determine_qt_parameters("${target}" host type arch arch_path host_type host_arch host_arch_path)

    get_external_prefix(qt base_path)
    file(MAKE_DIRECTORY "${base_path}")

    download_qt_configuration(prefix "${target}" "${host}" "${type}" "${arch}" "${arch_path}" "${base_path}")
    if (DEFINED host_arch_path AND NOT "${host_arch_path}" STREQUAL "${arch_path}")
        download_qt_configuration(host_prefix "${target}" "${host}" "${host_type}" "${host_arch}" "${host_arch_path}" "${base_path}")
    else()
        set(host_prefix "${prefix}")
    endif()

    set(QT_TARGET_PATH "${prefix}" CACHE STRING "")
    set(QT_HOST_PATH "${host_prefix}" CACHE STRING "")

    list(APPEND CMAKE_PREFIX_PATH "${prefix}")
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} PARENT_SCOPE)
endfunction()

function(download_moltenvk)
set(MOLTENVK_PLATFORM "macOS")

set(MOLTENVK_DIR "${CMAKE_BINARY_DIR}/externals/MoltenVK")
set(MOLTENVK_TAR "${CMAKE_BINARY_DIR}/externals/MoltenVK.tar")
if (NOT EXISTS ${MOLTENVK_DIR})
if (NOT EXISTS ${MOLTENVK_TAR})
    file(DOWNLOAD https://github.com/KhronosGroup/MoltenVK/releases/download/v1.2.10-rc2/MoltenVK-all.tar
    ${MOLTENVK_TAR} SHOW_PROGRESS)
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${MOLTENVK_TAR}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/externals")
endif()

# Add the MoltenVK library path to the prefix so find_library can locate it.
list(APPEND CMAKE_PREFIX_PATH "${MOLTENVK_DIR}/MoltenVK/dylib/${MOLTENVK_PLATFORM}")
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} PARENT_SCOPE)
endfunction()

function(get_external_prefix lib_name prefix_var)
    set(${prefix_var} "${CMAKE_BINARY_DIR}/externals/${lib_name}" PARENT_SCOPE)
endfunction()
