# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

if (NOT DEFINED BUNDLE_BIN_DIR)
    message(FATAL_ERROR "BUNDLE_BIN_DIR is required")
endif()
if (NOT DEFINED PATCHELF_EXE OR NOT EXISTS "${PATCHELF_EXE}")
    message(FATAL_ERROR "patchelf is required to make the Linux bundle portable")
endif()

set(_bin_dir "${BUNDLE_BIN_DIR}")
set(_lib_dir "${_bin_dir}/lib")
set(_plugins_dir "${_bin_dir}/plugins")
file(MAKE_DIRECTORY "${_lib_dir}")

if (DEFINED RUNTIME_DEPENDENCY_DIRS)
    string(REPLACE "|" ";" _runtime_dependency_dirs "${RUNTIME_DEPENDENCY_DIRS}")
endif()
list(APPEND _runtime_dependency_dirs "${_lib_dir}")
list(REMOVE_DUPLICATES _runtime_dependency_dirs)

foreach(_candidate citron citron-cmd citron-room shader_tool)
    if (EXISTS "${_bin_dir}/${_candidate}")
        list(APPEND _executables "${_bin_dir}/${_candidate}")
    endif()
endforeach()

file(GLOB _bundled_libraries "${_lib_dir}/*.so" "${_lib_dir}/*.so.*")
if (EXISTS "${_plugins_dir}")
    file(GLOB_RECURSE _bundled_modules "${_plugins_dir}/*.so")
endif()

if (_executables OR _bundled_libraries OR _bundled_modules)
    file(GET_RUNTIME_DEPENDENCIES
        RESOLVED_DEPENDENCIES_VAR _resolved_dependencies
        UNRESOLVED_DEPENDENCIES_VAR _unresolved_dependencies
        CONFLICTING_DEPENDENCIES_PREFIX _conflicting
        EXECUTABLES ${_executables}
        LIBRARIES ${_bundled_libraries}
        MODULES ${_bundled_modules}
        DIRECTORIES ${_runtime_dependency_dirs}
        PRE_EXCLUDE_REGEXES
            [[^libgcc_s\.so\..*]]
            [[^libstdc\+\+\.so\..*]]
            [[^libglapi\.so\..*]]
            [[^libGL\.so\..*]]
            [[^libEGL\.so\..*]]
            [[^libvulkan\.so\..*]]
            [[^libXau\.so\..*]]
            [[^libXdmcp\.so\..*]]
            [[^libxcb\.so\..*]]
        POST_EXCLUDE_REGEXES
            [[/ld-linux[^/]*\.so(\..*)?$]]
            [[/libc\.so(\..*)?$]]
            [[/libm\.so(\..*)?$]]
            [[/libdl\.so(\..*)?$]]
            [[/libpthread\.so(\..*)?$]]
            [[/librt\.so(\..*)?$]]
            [[/libresolv\.so(\..*)?$]]
            [[/libutil\.so(\..*)?$]]
            [[/libnss_[^/]*\.so(\..*)?$]]
            [[/libanl\.so(\..*)?$]]
            [[/libgcc_s\.so(\..*)?$]]
            [[/libstdc\+\+\.so(\..*)?$]]
    )
endif()

foreach(_dependency ${_resolved_dependencies})
    get_filename_component(_dependency_name "${_dependency}" NAME)
    if (NOT EXISTS "${_lib_dir}/${_dependency_name}")
        file(COPY "${_dependency}" DESTINATION "${_lib_dir}" FOLLOW_SYMLINK_CHAIN)
    endif()
endforeach()

if (_unresolved_dependencies)
    list(REMOVE_DUPLICATES _unresolved_dependencies)
    message(WARNING "Unresolved Linux bundle dependencies: ${_unresolved_dependencies}")
endif()

function(_citron_is_elf output_var file_path)
    execute_process(
        COMMAND readelf -h "${file_path}"
        RESULT_VARIABLE _readelf_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if (_readelf_result EQUAL 0)
        set(${output_var} TRUE PARENT_SCOPE)
    else()
        set(${output_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(_citron_set_rpath file_path rpath)
    _citron_is_elf(_is_elf "${file_path}")
    if (NOT _is_elf)
        return()
    endif()

    execute_process(
        COMMAND "${PATCHELF_EXE}" --force-rpath --set-rpath "${rpath}" "${file_path}"
        RESULT_VARIABLE _patchelf_result
        OUTPUT_VARIABLE _patchelf_stdout
        ERROR_VARIABLE _patchelf_stderr
    )
    if (NOT _patchelf_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to set RPATH on ${file_path}\n"
            "${_patchelf_stdout}\n${_patchelf_stderr}")
    endif()
endfunction()

foreach(_executable ${_executables})
    _citron_set_rpath("${_executable}" "$ORIGIN/lib")
endforeach()

file(GLOB _final_libraries "${_lib_dir}/*.so" "${_lib_dir}/*.so.*")
foreach(_library ${_final_libraries})
    _citron_set_rpath("${_library}" "$ORIGIN")
endforeach()

if (EXISTS "${_plugins_dir}")
    file(GLOB_RECURSE _final_modules "${_plugins_dir}/*.so")
    foreach(_module ${_final_modules})
        _citron_set_rpath("${_module}" "$ORIGIN/../../lib")
    endforeach()
endif()

set(_rpath_failures)
foreach(_file ${_executables} ${_final_libraries} ${_final_modules})
    _citron_is_elf(_is_elf "${_file}")
    if (NOT _is_elf)
        continue()
    endif()

    execute_process(
        COMMAND readelf -d "${_file}"
        OUTPUT_VARIABLE _dynamic_section
        ERROR_QUIET
    )
    if (_dynamic_section MATCHES "(RPATH|RUNPATH)[^\n]*\\[/" OR
            _dynamic_section MATCHES "(RPATH|RUNPATH)[^\n]*:/")
        list(APPEND _rpath_failures "${_file}")
    endif()
endforeach()

if (_rpath_failures)
    list(REMOVE_DUPLICATES _rpath_failures)
    message(FATAL_ERROR "Absolute RPATH/RUNPATH remains in Linux bundle: ${_rpath_failures}")
endif()

message(STATUS "Linux bundle dependencies copied and RPATHs normalized in ${_bin_dir}")
