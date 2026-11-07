#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Whole HostMemory/native common controls; Windows enrollment is test-only."""
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
source = root / 'src/common/host_memory.cpp'
original = source.read_text(encoding='utf-8')
needle = 'mapping_domain.Initialize(backing_size, false);'
assert original.count(needle) == 1
enrolled = original.replace(needle, 'mapping_domain.Initialize(backing_size, true);')
plans = [('metadata', None, 42, 'COMPLETE native alias exclusion/refusal teardown')]
for name, method, marker in [
    ('map', 'void HostMemory::Map(', 'closed-map-was-not-refused'),
    ('unmap', 'void HostMemory::Unmap(', 'closed-unmap-was-not-refused'),
    ('protect', 'void HostMemory::Protect(', 'closed-protect-was-not-refused'),
    ('clear', 'void HostMemory::ClearBackingRegion(', 'closed-clear-was-not-refused'),
    ('pin', 'std::optional<PinnedPhysicalMemory> HostMemory::PinPhysicalRange(', 'closed-pin-was-not-refused'),
]:
    plans.append((name, method, 91, 'ALIAS FREEZE CHECK: ' + marker))
plans.append(('guest_admission', None, 91, 'ALIAS FREEZE CHECK: unproved-physical-admission'))
parts = []
records = []
for name, method, code, marker in plans:
    directory = out / name
    directory.mkdir(exist_ok=True)
    includes = root / 'src'
    text = enrolled
    if method:
        start = text.index(method)
        guard = text.index('auto mutation = mapping_domain.BeginMutation();', start)
        assert text.index('\n}', start) > guard
        replacement = 'auto mutation = Common::HostMappingDomain{false}.BeginMutation();'
        if name == 'map':
            # Initial census maps physical offset zero. Skip only the later,
            # nonzero-offset remap request, so this is an ingress control rather
            # than an earlier missing-census failure.
            replacement = ('auto mutation = host_offset != 0 ? '
                           'Common::HostMappingDomain{false}.BeginMutation() : '
                           'mapping_domain.BeginMutation();')
        text = text[:guard] + text[guard:].replace(
            'auto mutation = mapping_domain.BeginMutation();', replacement, 1)
    if name == 'guest_admission':
        text = original
        header = root / 'src/common/host_mapping_domain.h'
        content = header.read_text(encoding='utf-8')
        before = 'bool GuestBackingAdmission() { return false; }'
        assert content.count(before) == 1
        includes = directory / 'include'
        target = includes / 'common/host_mapping_domain.h'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content.replace(before, 'bool GuestBackingAdmission() { return true; }'), encoding='utf-8')
    cpp = directory / 'host_memory.cpp'
    cpp.write_text(text, encoding='utf-8')
    records.append(dict(name=name, expected_exit=code, marker=marker,
                        arguments=[] if name == 'guest_admission' else ['--metadata-counterfactual'],
                        cpp_sha256=hashlib.sha256(cpp.read_bytes()).hexdigest(),
                        header_sha256={p.relative_to(includes).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                       for p in includes.rglob('*.h')} if name == 'guest_admission' else {},
                        diagnostic_enrollment=name != 'guest_admission', omitted_method=method))
    target_name = 'alias_freeze_' + name
    parts.append(f'''
    add_executable({target_name} EXCLUDE_FROM_ALL "{cpp.as_posix()}"
        "{(root/'src/tests/common/windows_alias_freeze.cpp').as_posix()}")
    set_property(TARGET {target_name} PROPERTY UNITY_BUILD OFF)
    target_include_directories({target_name} BEFORE PRIVATE "{includes.as_posix()}")
    foreach(_property COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES)
        get_directory_property(_values DIRECTORY "{root.as_posix()}/src/tests" ${{_property}})
        set_property(TARGET {target_name} APPEND PROPERTY ${{_property}} "${{_values}}")
    endforeach()
    target_link_libraries({target_name} PRIVATE common Boost::headers fmt::fmt ${{PLATFORM_LIBRARIES}} Threads::Threads)
''')
overlay = out / 'alias_controls.cmake'
overlay.write_text('''get_property(_added GLOBAL PROPERTY ALIAS_FREEZE_CONTROLS_ADDED)
if(_added)
    return()
endif()
set_property(GLOBAL PROPERTY ALIAS_FREEZE_CONTROLS_ADDED TRUE)
function(add_alias_freeze_controls)
    if(NOT WIN32 OR ENABLE_UNITY_BUILD)
        message(FATAL_ERROR "Windows separate production objects required")
    endif()
''' + ''.join(parts) + '''endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL add_alias_freeze_controls)
''',encoding='utf-8')
(out/'controls.json').write_text(json.dumps(dict(controls=records,
    original_cpp_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    overlay_sha256=hashlib.sha256(overlay.read_bytes()).hexdigest(),
    boundary='Metadata enrollment is counterfactual only; no physical/Windows support enabled in production'),indent=2)+'\n',encoding='utf-8')
print(overlay)
