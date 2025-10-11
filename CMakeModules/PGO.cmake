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

# Create the profile directory if it doesn't exist
file(MAKE_DIRECTORY "${CITRON_PGO_PROFILE_DIR}")

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

# Function to configure PGO for a specific target
function(citron_configure_pgo target_name)
    if(NOT TARGET ${target_name})
        message(WARNING "Target ${target_name} does not exist, skipping PGO configuration")
        return()
    endif()

    # Only configure PGO if either GENERATE or USE is enabled
    if(NOT CITRON_ENABLE_PGO_GENERATE AND NOT CITRON_ENABLE_PGO_USE)
        return()
    endif()

    # Ensure both stages are not enabled at the same time
    if(CITRON_ENABLE_PGO_GENERATE AND CITRON_ENABLE_PGO_USE)
        message(FATAL_ERROR "Cannot enable both CITRON_ENABLE_PGO_GENERATE and CITRON_ENABLE_PGO_USE simultaneously. Please build twice: first with GENERATE, then with USE.")
    endif()

    message(STATUS "Configuring PGO for target: ${target_name}")

    # MSVC-specific PGO
    if(MSVC)
        if(CITRON_ENABLE_PGO_GENERATE)
            message(STATUS "  [${target_name}] MSVC PGO: GENERATE stage")
            # Use FASTGENPROFILE for faster profiling with similar accuracy
            # GENPROFILE provides more detailed profiling but is slower
            # You can change FASTGENPROFILE to GENPROFILE for more accuracy but slower profiling
            target_compile_options(${target_name} PRIVATE /GL)
            target_link_options(${target_name} PRIVATE 
                /LTCG
                /FASTGENPROFILE
                /PGD:"${CITRON_PGO_PROFILE_DIR}/${target_name}.pgd"
            )
            
            # Copy PGO runtime DLLs to output directory
            citron_copy_pgo_runtime_dlls(${target_name})
        elseif(CITRON_ENABLE_PGO_USE)
            message(STATUS "  [${target_name}] MSVC PGO: USE stage")
            # Check if profile data exists in pgo-profiles directory
            set(PGD_FILE "${CITRON_PGO_PROFILE_DIR}/${target_name}.pgd")
            
            # Also check in the output directory (where MSVC creates them during GENERATE)
            get_target_property(TARGET_OUTPUT_DIR ${target_name} RUNTIME_OUTPUT_DIRECTORY)
            if(NOT TARGET_OUTPUT_DIR)
                set(TARGET_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
            endif()
            set(PGD_FILE_OUTPUT "${TARGET_OUTPUT_DIR}/${target_name}.pgd")
            
            if(EXISTS "${PGD_FILE}")
                target_compile_options(${target_name} PRIVATE /GL)
                # Use the profile directory path
                file(TO_NATIVE_PATH "${PGD_FILE}" PGD_FILE_NATIVE)
                target_link_options(${target_name} PRIVATE 
                    /LTCG
                    "/USEPROFILE:PGD=${PGD_FILE_NATIVE}"
                )
                message(STATUS "  [${target_name}] Using profile data: ${PGD_FILE}")
            elseif(EXISTS "${PGD_FILE_OUTPUT}")
                target_compile_options(${target_name} PRIVATE /GL)
                # Use the output directory path
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

    # GCC-specific PGO
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(CITRON_ENABLE_PGO_GENERATE)
            message(STATUS "  [${target_name}] GCC PGO: GENERATE stage")
            target_compile_options(${target_name} PRIVATE 
                -fprofile-generate="${CITRON_PGO_PROFILE_DIR}"
            )
            target_link_options(${target_name} PRIVATE 
                -fprofile-generate="${CITRON_PGO_PROFILE_DIR}"
            )
        elseif(CITRON_ENABLE_PGO_USE)
            message(STATUS "  [${target_name}] GCC PGO: USE stage")
            # Check if profile data exists
            file(GLOB profile_files "${CITRON_PGO_PROFILE_DIR}/*.gcda")
            if(profile_files)
                target_compile_options(${target_name} PRIVATE 
                    -fprofile-use="${CITRON_PGO_PROFILE_DIR}"
                    -fprofile-correction  # Handle inconsistencies in profile data
                )
                target_link_options(${target_name} PRIVATE 
                    -fprofile-use="${CITRON_PGO_PROFILE_DIR}"
                )
                message(STATUS "  [${target_name}] Using profile data from: ${CITRON_PGO_PROFILE_DIR}")
            else()
                message(WARNING "No profile data found for ${target_name} in ${CITRON_PGO_PROFILE_DIR}. PGO USE stage will be skipped.")
            endif()
        endif()

    # Clang-specific PGO
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if(CITRON_ENABLE_PGO_GENERATE)
            message(STATUS "  [${target_name}] Clang PGO: GENERATE stage")
            target_compile_options(${target_name} PRIVATE 
                -fprofile-generate="${CITRON_PGO_PROFILE_DIR}"
            )
            target_link_options(${target_name} PRIVATE 
                -fprofile-generate="${CITRON_PGO_PROFILE_DIR}"
            )
        elseif(CITRON_ENABLE_PGO_USE)
            message(STATUS "  [${target_name}] Clang PGO: USE stage")
            
            # For Clang, we need to merge .profraw files into .profdata first
            set(PROFDATA_FILE "${CITRON_PGO_PROFILE_DIR}/default.profdata")
            
            # Check if merged profile data exists, if not try to create it
            if(NOT EXISTS "${PROFDATA_FILE}")
                file(GLOB profraw_files "${CITRON_PGO_PROFILE_DIR}/*.profraw")
                if(profraw_files)
                    find_program(LLVM_PROFDATA llvm-profdata)
                    if(LLVM_PROFDATA)
                        message(STATUS "  [${target_name}] Merging .profraw files into ${PROFDATA_FILE}")
                        execute_process(
                            COMMAND ${LLVM_PROFDATA} merge -output=${PROFDATA_FILE} ${profraw_files}
                            RESULT_VARIABLE merge_result
                            OUTPUT_QUIET
                            ERROR_QUIET
                        )
                        if(NOT merge_result EQUAL 0)
                            message(WARNING "Failed to merge profile data for ${target_name}. PGO USE stage will be skipped.")
                            return()
                        endif()
                    else()
                        message(WARNING "llvm-profdata not found. Cannot merge profile data. PGO USE stage will be skipped.")
                        message(STATUS "  Please run: llvm-profdata merge -output=${PROFDATA_FILE} ${CITRON_PGO_PROFILE_DIR}/*.profraw")
                        return()
                    endif()
                else()
                    message(WARNING "No .profraw files found in ${CITRON_PGO_PROFILE_DIR}. PGO USE stage will be skipped.")
                    return()
                endif()
            endif()
            
            if(EXISTS "${PROFDATA_FILE}")
                target_compile_options(${target_name} PRIVATE 
                    -fprofile-use="${PROFDATA_FILE}"
                )
                target_link_options(${target_name} PRIVATE 
                    -fprofile-use="${PROFDATA_FILE}"
                )
                message(STATUS "  [${target_name}] Using profile data: ${PROFDATA_FILE}")
            endif()
        endif()
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
        message(STATUS "Next steps:")
        message(STATUS "  1. Run the built citron executable")
        message(STATUS "  2. Play games/perform typical operations to generate profile data")
        message(STATUS "  3. Exit citron")
        message(STATUS "  4. Profile data will be saved to: ${CITRON_PGO_PROFILE_DIR}")
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(STATUS "  5. For Clang: Merge profile data with:")
            message(STATUS "     llvm-profdata merge -output=${CITRON_PGO_PROFILE_DIR}/default.profdata ${CITRON_PGO_PROFILE_DIR}/*.profraw")
        endif()
        message(STATUS "  6. Rebuild with: cmake -DCITRON_ENABLE_PGO_GENERATE=OFF -DCITRON_ENABLE_PGO_USE=ON")
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

