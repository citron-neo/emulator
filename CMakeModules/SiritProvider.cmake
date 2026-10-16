# SPDX-License-Identifier: GPL-2.0-or-later
include_guard(GLOBAL)

# Sirit calls find_package even when the parent has already created the target.
function(citron_prepare_sirit_headers)
    if (NOT TARGET SPIRV-Headers::SPIRV-Headers)
        if (NOT TARGET SPIRV-Headers)
            message(FATAL_ERROR "Sirit requires the project SPIRV-Headers target")
        endif()
        add_library(SPIRV-Headers::SPIRV-Headers ALIAS SPIRV-Headers)
    endif()

    # Reuse an installed package config when the provider supplied one.
    if (EXISTS "${SPIRV-Headers_DIR}/SPIRV-HeadersConfig.cmake")
        return()
    endif()

    # Source providers create the target without an installed package config.
    # A configure-time config bridges that target to Sirit's find_package call;
    # export(TARGETS) files cannot be included until CMake's generation phase.
    set(config_dir "${CMAKE_BINARY_DIR}/providers/SPIRV-Headers")
    file(MAKE_DIRECTORY "${config_dir}")
    file(WRITE "${config_dir}/SPIRV-HeadersConfig.cmake"
        "if(NOT TARGET SPIRV-Headers::SPIRV-Headers)\n"
        "    message(FATAL_ERROR \"Parent SPIRV-Headers target is missing\")\n"
        "endif()\n")
    set(SPIRV-Headers_DIR "${config_dir}" PARENT_SCOPE)
endfunction()
