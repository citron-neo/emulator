#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Emit full production HostMemory counterfactuals, without changing the checkout."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output-dir', required=True, type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = a.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
header = (root / 'src/common/host_memory.h').read_text()
source = root / 'src/common/host_memory.cpp'
controls = []
parts = []
for name, marker in [
    ('backing_mutable', '[[nodiscard]] u8* BackingBasePointer() noexcept {'),
    ('backing_const', '[[nodiscard]] const u8* BackingBasePointer() const noexcept {'),
    ('virtual_mutable', '[[nodiscard]] u8* VirtualBasePointer() noexcept {'),
    ('virtual_const', '[[nodiscard]] const u8* VirtualBasePointer() const noexcept {'),
    ('windows_enrollment', None),
    ('replacement_epoch', None),
]:
    directory = out / name
    directory.mkdir(exist_ok=True)
    includes = root / 'src'
    cpp = source
    if marker:
        start = header.index(marker)
        end = header.index('\n    }', start)
        guard = header.index('        if (!impl) return nullptr;\n', start)
        assert guard < end
        changed = header[:guard] + header[guard:].replace('        if (!impl) return nullptr;\n', '', 1)
        includes = directory / 'include'
        target = includes / 'common/host_memory.h'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(changed, encoding='utf-8')
        controls.append(dict(name=name, marker=marker, original_sha256=hashlib.sha256((root/'src/common/host_memory.h').read_bytes()).hexdigest(),
                             changed_sha256=hashlib.sha256(target.read_bytes()).hexdigest(), expected='moved-from-pointer'))
    elif name == 'replacement_epoch':
        original = root / 'src/common/published_guest_chunk.h'
        text = original.read_text()
        needle = '            Invalidate();\n            epoch = std::move(other.epoch);'
        assert text.count(needle) == 1
        includes = directory / 'include'
        target = includes / 'common/published_guest_chunk.h'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text.replace(needle, '            epoch = std::move(other.epoch);'), encoding='utf-8')
        controls.append(dict(name=name, original_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
                             changed_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                             expected='replaced-epoch-still-live'))
    else:
        text = source.read_text()
        needle = 'mapping_domain.Initialize(backing_size, false);'
        assert text.count(needle) == 1
        cpp = directory / 'host_memory.cpp'
        cpp.write_text(text.replace(needle, 'mapping_domain.Initialize(backing_size, true);'), encoding='utf-8')
        controls.append(dict(name=name, original_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                             changed_sha256=hashlib.sha256(cpp.read_bytes()).hexdigest(), expected='unsupported-windows-freeze-refusal'))
    target_name = 'mapping_owner_' + name
    parts.append(f'''
    add_executable({target_name} EXCLUDE_FROM_ALL "{cpp.as_posix()}"
        "{(root/'src/tests/common/windows_mapping_owner.cpp').as_posix()}")
    set_property(TARGET {target_name} PROPERTY UNITY_BUILD OFF)
    target_include_directories({target_name} BEFORE PRIVATE "{includes.as_posix()}")
    foreach(_property COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES)
        get_directory_property(_values DIRECTORY "{root.as_posix()}/src/tests" ${{_property}})
        set_property(TARGET {target_name} APPEND PROPERTY ${{_property}} "${{_values}}")
    endforeach()
    target_link_libraries({target_name} PRIVATE common Boost::headers fmt::fmt
        ${{PLATFORM_LIBRARIES}} Threads::Threads)
''')
overlay = out / 'mapping_controls.cmake'
overlay.write_text('''get_property(_added GLOBAL PROPERTY MAPPING_OWNER_CONTROLS_ADDED)
if(_added)
    return()
endif()
set_property(GLOBAL PROPERTY MAPPING_OWNER_CONTROLS_ADDED TRUE)
function(add_mapping_owner_controls)
    if(NOT WIN32 OR ENABLE_UNITY_BUILD)
        message(FATAL_ERROR "Mapping owner controls require Windows separate production objects")
    endif()
''' + ''.join(parts) + '''endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL add_mapping_owner_controls)
''', encoding='utf-8')
(out / 'controls.json').write_text(json.dumps(dict(controls=controls,
    overlay_sha256=hashlib.sha256(overlay.read_bytes()).hexdigest(),
    boundary='Actual HostMemory.cpp and common library/native OS; one omitted getter guard or constructor support counterfactual'), indent=2)+'\n', encoding='utf-8')
print(overlay)
