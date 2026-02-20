# SPDX-FileCopyrightText: 2025 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Profile-Guided Optimization (PGO) Support
#
# This module provides functions to enable Profile-Guided Optimization (PGO) for Citron.
# PGO is a two-stage compiler optimization technique:
#   1. GENERATE stage: Build with instrumentation to collect profiling data during runtime
#   2. USE stage: Rebuild using the collected profiling data to optimize hot paths
#
# Usage:
#   set(CITRON_ENABLE_PGO_GENERATE ON)  # First build: generate profiling data
#   set(CITRON_ENABLE_PGO_USE ON)       # Second build: use profiling data
#   set(CITRON_PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo-profiles")  # Optional: custom profile directory

# PGO profile directory - where .pgd/.profraw/.profdata files are stored
if(NOT DEFINED CITRON_PGO_PROFILE_DIR)
    set(CITRON_PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo-profiles" CACHE PATH "Directory to store PGO profile data")
endif()

option(CITRON_PGO_EXACT "Use /GENPROFILE:EXACT for maximum precision PGO (slower instrumented runs)" OFF)

# Create the profile directory if it doesn't exist
file(MAKE_DIRECTORY "${CITRON_PGO_PROFILE_DIR}")

# Apply /GL globally so ALL compilation units (libraries included) emit MSIL for
# the linker to instrument (GENERATE) or optimize (USE). Without this, only the
# executable target's own sources are visible to PGO -- the hot code in video_core,
# shader_recompiler, core, etc. would be skipped entirely.
if(MSVC)
    if(CITRON_ENABLE_PGO_GENERATE OR CITRON_ENABLE_PGO_USE)
        add_compile_options(/GL)
        message(STATUS "PGO: Enabled /GL globally for all compilation units")
    endif()
endif()

# GCC/Clang: apply profile flags globally so library targets are also instrumented
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CITRON_ENABLE_PGO_GENERATE)
        add_compile_options(-fprofile-generate -fprofile-dir=${CITRON_PGO_PROFILE_DIR})
        add_link_options(-fprofile-generate -fprofile-dir=${CITRON_PGO_PROFILE_DIR})
        message(STATUS "PGO: Enabled -fprofile-generate globally for all compilation units")
    elseif(CITRON_ENABLE_PGO_USE)
        add_compile_options(-fprofile-use -fprofile-correction -fprofile-dir=${CITRON_PGO_PROFILE_DIR})
        add_link_options(-fprofile-use -fprofile-dir=${CITRON_PGO_PROFILE_DIR})
        message(STATUS "PGO: Enabled -fprofile-use globally for all compilation units")
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(CITRON_ENABLE_PGO_GENERATE)
        add_compile_options(-fprofile-instr-generate)
        add_link_options(-fprofile-instr-generate)
        message(STATUS "PGO: Enabled -fprofile-instr-generate globally for all compilation units")
    elseif(CITRON_ENABLE_PGO_USE)
        set(PROFDATA_FILE "${CITRON_PGO_PROFILE_DIR}/default.profdata")
        if(NOT EXISTS "${PROFDATA_FILE}")
            file(GLOB profraw_files "${CITRON_PGO_PROFILE_DIR}/*.profraw")
            if(profraw_files)
                find_program(LLVM_PROFDATA llvm-profdata)
                if(LLVM_PROFDATA)
                    message(STATUS "PGO: Merging .profraw files into ${PROFDATA_FILE}")
                    execute_process(
                        COMMAND ${LLVM_PROFDATA} merge -output=${PROFDATA_FILE} ${profraw_files}
                        RESULT_VARIABLE merge_result
                        OUTPUT_QUIET
                        ERROR_QUIET
                    )
                    if(NOT merge_result EQUAL 0)
                        message(WARNING "PGO: Failed to merge .profraw files. PGO USE will be degraded.")
                    endif()
                else()
                    message(WARNING "PGO: llvm-profdata not found. Cannot merge .profraw files.")
                endif()
            endif()
        endif()
        if(EXISTS "${PROFDATA_FILE}")
            add_compile_options(-fprofile-instr-use=${PROFDATA_FILE})
            add_link_options(-fprofile-instr-use=${PROFDATA_FILE})
            message(STATUS "PGO: Enabled -fprofile-instr-use globally with ${PROFDATA_FILE}")
        else()
            message(WARNING "PGO: No profile data found. USE stage will have no effect.")
        endif()
    endif()
endif()

# Function to copy MSVC PGO runtime DLLs
function(citron_copy_pgo_runtime_dlls target_name)
    if(NOT MSVC)
        return()
    endif()

    # Find the Visual Studio installation directory
    get_filename_component(MSVC_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(MSVC_DIR "${MSVC_DIR}" DIRECTORY)
    get_filename_component(MSVC_DIR "${MSVC_DIR}" DIRECTORY)

    # Common locations for PGO runtime DLLs
    set(PGO_DLL_PATHS
        "${MSVC_DIR}/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT/pgort140.dll"
        "${MSVC_DIR}/VC/Redist/MSVC/*/x86/Microsoft.VC*.CRT/pgort140.dll"
        "${MSVC_DIR}/VC/Tools/MSVC/*/bin/Hostx64/x64/pgort140.dll"
        "${MSVC_DIR}/VC/Tools/MSVC/*/bin/Hostx64/x86/pgort140.dll"
        "${MSVC_DIR}/VC/Tools/MSVC/*/bin/Hostx86/x64/pgort140.dll"
        "${MSVC_DIR}/VC/Tools/MSVC/*/bin/Hostx86/x86/pgort140.dll"
    )

    # Find the PGO runtime DLL
    set(PGO_DLL_FOUND FALSE)
    foreach(dll_pattern ${PGO_DLL_PATHS})
        file(GLOB PGO_DLL_CANDIDATES ${dll_pattern})
        if(PGO_DLL_CANDIDATES)
            list(GET PGO_DLL_CANDIDATES 0 PGO_DLL_PATH)
            set(PGO_DLL_FOUND TRUE)
            break()
        endif()
    endforeach()

    if(PGO_DLL_FOUND)
        message(STATUS "  [${target_name}] Found PGO runtime DLL: ${PGO_DLL_PATH}")

        # Get the target's output directory
        get_target_property(TARGET_OUTPUT_DIR ${target_name} RUNTIME_OUTPUT_DIRECTORY)
        if(NOT TARGET_OUTPUT_DIR)
            get_target_property(TARGET_OUTPUT_DIR ${target_name} RUNTIME_OUTPUT_DIRECTORY_DEBUG)
        endif()
        if(NOT TARGET_OUTPUT_DIR)
            set(TARGET_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
        endif()

        # Copy the DLL to the output directory
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PGO_DLL_PATH}"
            "${TARGET_OUTPUT_DIR}/"
            COMMENT "Copying PGO runtime DLL for ${target_name}"
        )
    else()
        message(WARNING "PGO runtime DLL (pgort140.dll) not found. The instrumented build may not run properly.")
        message(STATUS "  Please ensure Visual Studio is properly installed with PGO support.")
        message(STATUS "  You may need to install the 'MSVC v143 - VS 2022 C++ x64/x86 build tools' component.")
    endif()
endfunction()

# Function to configure PGO linker flags for executable targets.
# /GL is already applied globally above; this function handles the linker-side
# flags (/GENPROFILE, /USEPROFILE) which only apply to the final link step.
function(citron_configure_pgo target_name)
    if(NOT TARGET ${target_name})
        message(WARNING "Target ${target_name} does not exist, skipping PGO configuration")
        return()
    endif()

    if(NOT CITRON_ENABLE_PGO_GENERATE AND NOT CITRON_ENABLE_PGO_USE)
        return()
    endif()

    if(CITRON_ENABLE_PGO_GENERATE AND CITRON_ENABLE_PGO_USE)
        message(FATAL_ERROR "Cannot enable both CITRON_ENABLE_PGO_GENERATE and CITRON_ENABLE_PGO_USE simultaneously. Please build twice: first with GENERATE, then with USE.")
    endif()

    message(STATUS "Configuring PGO for target: ${target_name}")

    if(MSVC)
        if(CITRON_ENABLE_PGO_GENERATE)
            message(STATUS "  [${target_name}] MSVC PGO: GENERATE stage")
            if(CITRON_PGO_EXACT)
                set(PGO_GEN_FLAG "/GENPROFILE:EXACT")
                message(STATUS "  [${target_name}] Using EXACT counters (maximum precision)")
            else()
                set(PGO_GEN_FLAG "/GENPROFILE")
            endif()
            target_link_options(${target_name} PRIVATE
                /LTCG
                ${PGO_GEN_FLAG}
                /PGD:"${CITRON_PGO_PROFILE_DIR}/${target_name}.pgd"
            )
            citron_copy_pgo_runtime_dlls(${target_name})
        elseif(CITRON_ENABLE_PGO_USE)
            message(STATUS "  [${target_name}] MSVC PGO: USE stage")
            set(PGD_FILE "${CITRON_PGO_PROFILE_DIR}/${target_name}.pgd")
            get_target_property(TARGET_OUTPUT_DIR ${target_name} RUNTIME_OUTPUT_DIRECTORY)
            if(NOT TARGET_OUTPUT_DIR)
                set(TARGET_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
            endif()
            set(PGD_FILE_OUTPUT "${TARGET_OUTPUT_DIR}/${target_name}.pgd")

            if(EXISTS "${PGD_FILE}")
                file(TO_NATIVE_PATH "${PGD_FILE}" PGD_FILE_NATIVE)
                target_link_options(${target_name} PRIVATE
                    /LTCG
                    "/USEPROFILE:PGD=${PGD_FILE_NATIVE}"
                )
                message(STATUS "  [${target_name}] Using profile data: ${PGD_FILE}")
            elseif(EXISTS "${PGD_FILE_OUTPUT}")
                file(TO_NATIVE_PATH "${PGD_FILE_OUTPUT}" PGD_FILE_NATIVE)
                target_link_options(${target_name} PRIVATE
                    /LTCG
                    "/USEPROFILE:PGD=${PGD_FILE_NATIVE}"
                )
                message(STATUS "  [${target_name}] Using profile data: ${PGD_FILE_OUTPUT}")
            else()
                message(WARNING "Profile data not found for ${target_name}. Checked:")
                message(STATUS "  - ${PGD_FILE}")
                message(STATUS "  - ${PGD_FILE_OUTPUT}")
                message(WARNING "PGO USE stage will be skipped.")
            endif()
        endif()

    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # GCC/Clang flags are applied globally above; nothing target-specific needed
        message(STATUS "  [${target_name}] PGO flags applied globally")
    else()
        message(WARNING "PGO is not supported for compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()

# Helper function to print PGO instructions
function(citron_print_pgo_instructions)
    if(CITRON_ENABLE_PGO_GENERATE)
        message(STATUS "")
        message(STATUS "=================================================================")
        message(STATUS "PGO GENERATE Stage")
        message(STATUS "=================================================================")
        message(STATUS "Citron has been built with profiling instrumentation.")
        message(STATUS "")
        message(STATUS "Training guide for best results:")
        message(STATUS "  1. Run the built citron executable")
        message(STATUS "  2. Launch a game and play past initial loading (shader compilation is a key hot path)")
        message(STATUS "  3. Play for at least 5-10 minutes per game")
        message(STATUS "  4. Test 2-3 different games for broader coverage")
        message(STATUS "  5. Navigate menus, settings, and game list to profile the UI")
        message(STATUS "  6. Exit citron cleanly (File -> Exit or Ctrl+Q)")
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(STATUS "  7. For Clang: Move .profraw files to the profile directory:")
            message(STATUS "     mv default*.profraw ${CITRON_PGO_PROFILE_DIR}/")
            message(STATUS "  8. Merge profiles:")
            message(STATUS "     llvm-profdata merge -output=${CITRON_PGO_PROFILE_DIR}/default.profdata ${CITRON_PGO_PROFILE_DIR}/*.profraw")
        else()
            message(STATUS "  7. Profile data will be saved to: ${CITRON_PGO_PROFILE_DIR}")
        endif()
        message(STATUS "  Then rebuild with: cmake -DCITRON_ENABLE_PGO_GENERATE=OFF -DCITRON_ENABLE_PGO_USE=ON .")
        message(STATUS "=================================================================")
        message(STATUS "")
    elseif(CITRON_ENABLE_PGO_USE)
        message(STATUS "")
        message(STATUS "=================================================================")
        message(STATUS "PGO USE Stage")
        message(STATUS "=================================================================")
        message(STATUS "Citron is being optimized using profile data from: ${CITRON_PGO_PROFILE_DIR}")
        message(STATUS "This build will be significantly faster than standard builds.")
        message(STATUS "=================================================================")
        message(STATUS "")
    endif()
endfunction()
