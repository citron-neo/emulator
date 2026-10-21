# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

function(target_architecture_specific_sources project arch)
    if (NOT MULTIARCH_BUILD)
        target_sources("${project}" PRIVATE ${ARGN})
        return()
    endif()

    foreach(input_file IN LISTS ARGN)
        if(input_file MATCHES ".cpp$")
            if(NOT IS_ABSOLUTE ${input_file})
                set(input_file "${CMAKE_CURRENT_SOURCE_DIR}/${input_file}")
            endif()

            set(output_file "${CMAKE_CURRENT_BINARY_DIR}/arch_gen/${input_file}")
            add_custom_command(
                OUTPUT "${output_file}"
                COMMAND ${CMAKE_COMMAND} "-Darch=${arch}"
                                         "-Dinput_file=${input_file}"
                                         "-Doutput_file=${output_file}"
                                         -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/impl/TargetArchitectureSpecificSourcesWrapFile.cmake"
                DEPENDS "${input_file}"
                VERBATIM
            )
            target_sources(${project} PRIVATE "${output_file}")
        endif()
    endforeach()
endfunction()
