#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Emit one full-link native missing-foundation control without changing source files."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output-dir', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = a.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
source = root / 'src/core/restricted_process_owner.cpp'
original = source.read_text(encoding='utf-8')
needle = '    cancelled = closure->CancelNoEffects();'
assert original.count(needle) == 1
mutant = out / 'missing_foundation.cpp'
mutant.write_text(original.replace(needle, needle + '\n    if (cancelled) owner.reset();'), encoding='utf-8')
# Explicit object definitions satisfy the owner symbols before the core static
# archive is searched. The full original core/System/kernel remains linked.
# Unity must remain disabled so no original owner definition shares a core object.
overlay = out / 'native_owner_control.cmake'
overlay.write_text(f'''if(NOT CMAKE_SOURCE_DIR STREQUAL "{root.as_posix()}")
    return()
endif()
get_property(_owner_control_added GLOBAL PROPERTY NATIVE_OWNER_CONTROL_ADDED)
if(_owner_control_added)
    return()
endif()
set_property(GLOBAL PROPERTY NATIVE_OWNER_CONTROL_ADDED TRUE)
function(native_owner_cpp23 directory)
    get_property(_options DIRECTORY "${{directory}}" PROPERTY COMPILE_OPTIONS)
    list(REMOVE_ITEM _options /std:c++20)
    set_property(DIRECTORY "${{directory}}" PROPERTY COMPILE_OPTIONS "${{_options}}")
    get_property(_targets DIRECTORY "${{directory}}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_type "${{_target}}" TYPE)
        if(_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            get_target_property(_options "${{_target}}" COMPILE_OPTIONS)
            if(_options)
                list(REMOVE_ITEM _options /std:c++20)
                set_property(TARGET "${{_target}}" PROPERTY COMPILE_OPTIONS "${{_options}}")
            endif()
        endif()
    endforeach()
    get_property(_children DIRECTORY "${{directory}}" PROPERTY SUBDIRECTORIES)
    foreach(_child IN LISTS _children)
        native_owner_cpp23("${{_child}}")
    endforeach()
endfunction()
function(add_native_owner_control)
    if(ENABLE_UNITY_BUILD)
        message(FATAL_ERROR "Native owner control requires separate production objects")
    endif()
    add_executable(restricted_owner_missing_foundation EXCLUDE_FROM_ALL
        "{mutant.as_posix()}" "{(root / 'src/tests/core/restricted_process_owner.cpp').as_posix()}")
    set_property(TARGET restricted_owner_missing_foundation PROPERTY UNITY_BUILD OFF)
    foreach(_property COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES)
        get_directory_property(_native_property DIRECTORY "{root.as_posix()}/src/tests" ${{_property}})
        set_property(TARGET restricted_owner_missing_foundation APPEND PROPERTY
            ${{_property}} "${{_native_property}}")
    endforeach()
    target_link_libraries(restricted_owner_missing_foundation PRIVATE common core
        Vulkan::Headers fmt::fmt ${{PLATFORM_LIBRARIES}} Catch2::Catch2WithMain Threads::Threads)
    if(MSVC)
        # The root requests C++23, but src currently appends /std:c++20 after it.
        # Local acceptance-only correction; no tracked global build rule changes.
        native_owner_cpp23("{root.as_posix()}/src")
        get_target_property(_options restricted_owner_missing_foundation COMPILE_OPTIONS)
        list(REMOVE_ITEM _options /std:c++20)
        set_property(TARGET restricted_owner_missing_foundation PROPERTY COMPILE_OPTIONS "${{_options}}")
        # Existing make_shared<const u8>(0) in vk_rasterizer.h:350 triggers
        # C4242/C4244 under /WX. Null-owner acceptance never executes this renderer.
        # Keep this local exception on its instantiating TU, not on core/tests.
        set_property(SOURCE "{root.as_posix()}/src/video_core/renderer_vulkan/vk_rasterizer.cpp"
            TARGET_DIRECTORY video_core APPEND PROPERTY COMPILE_OPTIONS /wd4242 /wd4244)
    endif()
endfunction()
cmake_language(DEFER DIRECTORY "${{CMAKE_SOURCE_DIR}}" CALL add_native_owner_control)
''', encoding='utf-8')
manifest = dict(source='src/core/restricted_process_owner.cpp',
                original_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                replacement=needle + '\n    if (cancelled) owner.reset();',
                mutant_sha256=hashlib.sha256(mutant.read_bytes()).hexdigest(),
                cmake_overlay_sha256=hashlib.sha256(overlay.read_bytes()).hexdigest(),
                expected_exit=91, required_marker='NATIVE OWNER WITNESS: cohort foundation retired early',
                boundary='Full native kernel/System link; no extracted bodies or replacement kernel types')
manifest['local_build_exceptions'] = [
    'Remove inherited /std:c++20 from src targets to honor the root C++23 requirement',
    'Disable C4242/C4244 only in vk_rasterizer.cpp for existing make_shared<const u8>(0); Null runtime only',
]
(out / 'control.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print(overlay)
