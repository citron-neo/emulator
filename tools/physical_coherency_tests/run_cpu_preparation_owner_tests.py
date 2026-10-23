#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual Memory preparation/owner bodies, real pins/PTEs, fixture System services.

Run run_first_registration_tests.py first. No GPU, guest exclusion or teardown
of a Memory object concurrently with its initial API entry is certified here.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
previous = root / '.diagnostics/m3b-first-registration'
for name, expected in json.loads((previous / 'extraction.json').read_text())['sha256'].items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected, ('stale prerequisite', name)
out = root / '.diagnostics/m4-cpu-preparation-owner'
stubs = out / 'stubs/core'
stubs.mkdir(parents=True, exist_ok=True)
prefix = (previous / 'first_registration.cpp').read_text().split('using DAddr=u64;')[0]
checkpoint = '        if (!physical || current_page_table != table || !mapping->IsCurrent() ||'
assert prefix.count(checkpoint) == 1
prefix = prefix.replace(checkpoint, '        ObservePrepared();\n' + checkpoint)
tail = r'''
int main() {
    using R = Common::ProducerDrain::Result;
    System system;
    Common::PageTable table;
    table.Resize(16, 12);
    Cpu cpu{system, &table};
    cpu.MapPages(table, 1, 1, DramMemoryMap::Base, Common::PageType::RasterizerCachedMemory);
    Core::Memory::Memory memory{&cpu};
    const auto owner = memory.controlled_owner;
    auto existing = memory.PrepareControlledWrite8(4103, 11);
    assert(existing);
    for (unsigned width : {1u, 2u, 4u, 8u}) {
        std::latch reached{1}, finish{1};
        prepared = [&] { reached.count_down(); finish.wait(); };
        Core::Memory::ControlledCpuWriteRequestPtr request;
        std::thread producer([&] {
            switch (width) {
            case 1: request = memory.PrepareControlledWrite8(4103, 77); break;
            case 2: request = memory.PrepareControlledWrite16(4103, 77); break;
            case 4: request = memory.PrepareControlledWrite32(4103, 77); break;
            case 8: request = memory.PrepareControlledWrite64(4103, 77); break;
            }
        });
        reached.wait(); // Actual cold preparation paused after physical pin, before validation.
        auto close = owner->TryCloseManagedStores(64);
        assert(close);
        const auto pending = close->Poll();
        const bool early_resume = close->ResumeNoEffects();
        // Track's real exclusive mutex is also the mutex Close must acquire.
        const bool registry_blocked = !owner->Track(existing);
        const bool new_entry_blocked = early_resume ? false : !memory.PrepareControlledWrite8(4103, 99);
        finish.count_down();
        producer.join(); // Do not leave a paused producer on a failing assertion.
        prepared = {};
        assert(pending == R::Pending && "preparation escaped real owner drainage");
        assert(!early_resume && new_entry_blocked);
        assert(registry_blocked && "preparation escaped Memory lifecycle mutex");
        assert(request && request->State() == Core::Memory::ControlledCpuWriteRequest::Phase::Prepared);
        assert(close->Poll() == R::DrainedSubset && close->ResumeNoEffects());
        assert(request->Cancel());
    }
    prepared = [] { throw std::runtime_error("pre-effect cold preparation failure"); };
    bool threw = false;
    try { (void)memory.PrepareControlledWrite8(4103, 77); }
    catch (const std::runtime_error&) { threw = true; }
    prepared = {};
    assert(threw);
    auto after_exception = owner->TryCloseManagedStores(8);
    assert(after_exception && after_exception->Poll() == R::DrainedSubset);
    assert(after_exception->ResumeNoEffects());
    auto fresh = memory.PrepareControlledWrite8(4103, 77);
    assert(fresh);
    memory.SealControlledCpuWrites();
    assert(fresh->State() == Core::Memory::ControlledCpuWriteRequest::Phase::Cancelled);
    bool called = false;
    prepared = [&] { called = true; };
    assert(!memory.PrepareControlledWrite8(4103, 77) && !called);
    prepared = {};
    std::cout << "PASS actual scalar preparation: four widths, pinned in-flight closure, lifecycle mutex, pre-effect exception release, close-before-entry and request cancellation; guest exclusion Unknown\n";
}
'''
source = prefix + tail
header = (root / 'src/core/controlled_cpu_page.h').read_text()
command = [os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
           '-Wno-unused-parameter', '-I', str(out / 'stubs'), '-I', str(previous / 'stubs'), '-I', str(root / 'src')]
command += [str(root / name) for name in ['src/common/host_memory.cpp', 'src/common/page_table.cpp', 'src/common/virtual_buffer.cpp']]
if os.name == 'nt':
    command += [str(root / 'src/common/dynamic_library.cpp')]

def execute(name, text, injected=None, positive=False):
    path = out / (name + '.cpp')
    path.write_text(text)
    override = stubs / 'controlled_cpu_page.h'
    if injected is not None:
        override.write_text(injected)
    try:
        exe = out / (name + ('.exe' if os.name == 'nt' else ''))
        subprocess.run(command + [str(path), '-o', str(exe)], check=True)
        for repeat in range(1, 4 if positive else 2):
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            log = result.stdout + result.stderr
            (out / f'{name}-{repeat}.txt').write_text(log)
            if positive:
                assert result.returncode == 0 and 'PASS actual scalar preparation:' in log, log
            else:
                assert result.returncode != 0 and 'assert' in log.lower() and 'preparation escaped' in log, (name, result.returncode, log)
    finally:
        override.unlink(missing_ok=True)
    print('PASS ' + ('three actual preparation repeats' if positive else 'assertion mutant: ' + name), flush=True)

entry = '    const auto execution = managed_stores.TryEnter();\n    if (!execution) return {};'
assert header.count(entry) == 1
execute('uncounted-preparation', source, header.replace(entry, '    // Mutant: preparation not counted.'))
locked = '    auto request = prepare(*memory);\n    lock.unlock();'
assert header.count(locked) == 1
execute('unprotected-preparation', source, header.replace(locked, '    lock.unlock();\n    auto request = prepare(*memory);'))
current = (root / 'src/core/memory.cpp').read_text()
marker = 'ControlledCpuWriteRequestPtr Memory::PrepareControlledWrite('
start = current.index(marker)
method = current[start:current.index('\n}', start) + 2]
assert source.count(method) == 1
old = '''ControlledCpuWriteRequestPtr Memory::PrepareControlledWrite(
    Common::ProcessAddress target, ControlledCpuWriteRequest::Value value) {
    const u64 address = GetInteger(target) & 0xffffffffffffULL;
    const auto size = std::visit([](const auto& scalar) { return sizeof(scalar); }, value);
    if (!controlled_owner || !controlled_owner->TryVisit([](Memory&) {})) return {};
    if (size > CITRON_PAGESIZE - address % CITRON_PAGESIZE) return {};
    auto page = PrepareControlledCpuPage(address & ~(CITRON_PAGESIZE - 1));
    if (!page || page->mapping.type != Common::PageType::RasterizerCachedMemory || !page->IsCurrentForRegistration()) return {};
    auto request = ControlledCpuWriteRequestPtr{new ControlledCpuWriteRequest{std::move(*page), address, std::move(value), controlled_owner}};
    return controlled_owner->Track(request) ? request : ControlledCpuWriteRequestPtr{};
}'''
execute('old-empty-visit', source.replace(method, old))
execute('production', source, positive=True)
paths = ['src/core/memory.cpp', 'src/core/controlled_cpu_page.h', 'src/common/producer_drain.h',
         'src/common/host_memory.cpp', 'src/common/page_table.cpp',
         'tools/physical_coherency_tests/run_cpu_preparation_owner_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in paths},
    'assertion_mutants': ['uncounted-preparation', 'unprotected-preparation', 'old-empty-visit'],
    'positive_repeats': 3,
    'boundary': 'Actual Memory/owner preparation bodies and real HostMemory/PageTable; fixture System services, not ordinary guest exclusion.'
}, indent=2) + '\n')
