#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual Memory outer wrappers/Impl mapping bodies, real HostMemory/PTEs; fixture System.

Pauses are test observations between actual PTE and VMA effects. No ordinary guest,
NCE, service closure, all-alias freeze or GPU admission is certified.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--boost-include', type=Path)
parser.add_argument('--compile-only', action='store_true', help='Compile the production fixture without runtime acceptance')
args = parser.parse_args()
if sys.platform != 'linux' and not args.compile_only:
    sys.exit('Linux PTE/VMA remapping semantics required; use --compile-only for a syntax probe')
out = root / '.diagnostics/m4-mapping-publication'
stubs = out / 'stubs'
for directory in ['common', 'fmt', 'core']:
    (stubs / directory).mkdir(parents=True, exist_ok=True)
(stubs / 'common/logging.h').write_text('''#pragma once
namespace Common::Log {inline void Stop() {}}
template<class... T> void TestLog(const char*,T&&...) {}
#define LOG_CRITICAL(category,...) TestLog(__VA_ARGS__)
#define LOG_ERROR(category,...) TestLog(__VA_ARGS__)
#define LOG_WARNING(category,...) TestLog(__VA_ARGS__)
#define LOG_INFO(category,...) TestLog(__VA_ARGS__)
#define LOG_DEBUG(category,...) TestLog(__VA_ARGS__)
''')
(stubs / 'common/assert.h').write_text('''#pragma once
#include <cassert>
#include <cstdlib>
#define ASSERT(x) assert(x)
#define ASSERT_MSG(x, ...) assert(x)
#define UNIMPLEMENTED_MSG(...) std::abort()
''')
(stubs / 'fmt/format.h').write_text('''#pragma once
#include <string>
namespace fmt {
template<class T> struct formatter;
struct format_parse_context {constexpr const char* begin() const {return nullptr;}};
template<class Out,class... T> Out format_to(Out out,const char*,T&&...) {return out;}
template<class... T> std::string format(const char* text,T&&...) {return text;}
}
''')
paths = ['src/core/memory.cpp', 'src/core/memory.h',
         'src/core/controlled_mapping_publication.h', 'src/common/producer_drain.h',
         'src/common/host_memory.cpp', 'src/common/page_table.cpp',
         'tools/physical_coherency_tests/run_cpu_mapping_tests.py',
         'tools/physical_coherency_tests/run_mapping_publication_tests.py']
current = (root / paths[0]).read_text()
header = (root / paths[2]).read_text()


def method(marker, closing='\n    }'):
    begin = current.index(marker)
    return current[begin:current.index(closing, begin) + len(closing)]


tree = ast.parse((root / paths[-2]).read_text())
head = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
            and any(isinstance(t, ast.Name) and t.id == 'head' for t in n.targets))
head = head.replace('struct Cpu {', '''
#include "core/controlled_mapping_publication.h"
#include <chrono>
constexpr u64 CITRON_PAGEMASK=4095;
std::function<void()> publication_observer;
void ObservePublication() {if(publication_observer) publication_observer();}
namespace Settings {
struct Flag {bool GetValue() const {return false;}};
struct Values {Flag use_reactive_flushing;} values;
}
''' + method('static inline bool AddressSpaceContains(', '\n}') + '\nstruct Cpu {')
head += method('    void MapPages(') + '\n'
head += method('    [[nodiscard]] inline u8* GetPointerFromDebugMemory(') + '\n'
head += method('    [[nodiscard]] inline u8* GetPointerFromRasterizerCachedMemory(') + '\n'
names = ['SetCurrentPageTable', 'MapMemoryRegion', 'UnmapRegion', 'ProtectRegion',
         'RasterizerMarkRegionCached', 'MarkRegionDebug']
for name in names:
    body = method('    void ' + name + '(')
    if name == 'SetCurrentPageTable':
        marker = '        current_page_table = &process.GetPageTable().GetImpl();'
        body = body.replace(marker, marker + '\n        ObservePublication();')
    elif name in ['MapMemoryRegion', 'UnmapRegion']:
        body = body.replace('        if (current_page_table->fastmem_arena)',
                            '        ObservePublication();\n        if (current_page_table->fastmem_arena)')
    elif name == 'ProtectRegion':
        body = body.replace('        u64 protect_bytes = 0, protect_begin = 0;',
                            '        ObservePublication();\n        u64 protect_bytes = 0, protect_begin = 0;')
    else:
        body = body.replace('        const auto mutation = current_page_table->MutateMappings();',
                            '        ObservePublication();\n        const auto mutation = current_page_table->MutateMappings();')
    assert body.count('ObservePublication();') == 1, name
    head += body + '\n'
head += '''};
namespace Core::Memory {
struct Memory {
    Cpu* impl;
    ControlledMappingPublication mapping_publications;
    explicit Memory(Cpu* cpu, bool managed=true):impl{cpu},mapping_publications{managed} {}
    void SetCurrentPageTable(Kernel::KProcess&);
    void MapMemoryRegion(Common::PageTable&,Common::ProcessAddress,u64,Common::PhysicalAddress,Common::MemoryPermission,bool);
    void UnmapRegion(Common::PageTable&,Common::ProcessAddress,u64,bool);
    void ProtectRegion(Common::PageTable&,Common::ProcessAddress,u64,Common::MemoryPermission);
    void RasterizerMarkRegionCached(Common::ProcessAddress,u64,bool);
    void MarkRegionDebug(Common::ProcessAddress,u64,bool);
    void SealControlledMappings();
};
'''
wrappers = ''.join(method('void Memory::' + name + '(', '\n}') + '\n' for name in names)
head += wrappers + method('void Memory::SealControlledMappings()', '\n}') + '\n}\n'
tail = r'''
int main() {
    using R=Common::ProducerDrain::Result;
    using Perm=Common::MemoryPermission;
    constexpr u64 P=4096, B=DramMemoryMap::Base;
    for(unsigned mode=0;mode<6;++mode) {
        System system; Common::PageTable table; table.Resize(16,12);
        Cpu cpu{system,&table}; cpu.buffer=&system.device.buffer;
        table.fastmem_arena=cpu.buffer->VirtualBasePointer();
        Core::Memory::Memory memory{&cpu}; Kernel::KProcess process{&table};
        memory.MapMemoryRegion(table,P,P,B,Perm::ReadWrite,false);
        system.device.buffer.BackingBasePointer()[0]=10;
        system.device.buffer.BackingBasePointer()[P]=55;
        auto invoke=[&] {
            switch(mode) {
            case 0: memory.MapMemoryRegion(table,P,P,B+P,Perm::ReadWrite,false); break;
            case 1: memory.UnmapRegion(table,P,P,false); break;
            case 2: memory.ProtectRegion(table,P,P,Perm::Read); break;
            case 3: memory.RasterizerMarkRegionCached(P,P,true); break;
            case 4: memory.MarkRegionDebug(P,P,true); break;
            case 5: memory.SetCurrentPageTable(process); break;
            }
        };
        const auto before=table.SnapshotMapping(P); assert(before);
        auto closed=memory.mapping_publications.TryClose(64); assert(closed);
        unsigned observations=0; publication_observer=[&]{++observations;};
        bool refused=false;
        try {invoke();} catch(const std::runtime_error&) {refused=true;}
        publication_observer={};
        assert(refused && observations==0 && "outer entry refusal must precede effects");
        assert(before->IsCurrent() && table.SnapshotMapping(P)->type==Common::PageType::Memory);
        assert(table.fastmem_arena[P]==10);
        assert(closed->Poll()==R::DrainedSubset && closed->ResumeNoEffects());

        std::latch reached{1},release{1};
        publication_observer=[&]{reached.count_down();release.wait();};
        std::exception_ptr error;
        std::thread producer([&]{try {invoke();}catch(...){error=std::current_exception();}});
        reached.wait();
        auto draining=memory.mapping_publications.TryClose(64);
        const auto pending=draining ? draining->Poll() : R::Unknown;
        const bool early=draining && draining->ResumeNoEffects();
        // Actual MapPages has published its PTE while the real old VMA still contains 10.
        const bool split_map=mode!=0 ||
            (table.SnapshotMapping(P)->physical_page==B+P && table.fastmem_arena[P]==10);
        release.count_down(); producer.join(); publication_observer={};
        assert(!error && split_map);
        assert(pending==R::Pending && !early && "outer publication escaped drainage");
        assert(draining->Poll()==R::DrainedSubset && draining->ResumeNoEffects());
        if(mode==0) assert(table.fastmem_arena[P]==55);
        if(mode==1) assert(!table.SnapshotMapping(P));
        if(mode==3) assert(table.SnapshotMapping(P)->type==Common::PageType::RasterizerCachedMemory);
        if(mode==4) assert(table.SnapshotMapping(P)->type==Common::PageType::DebugMemory);
        if(mode==5) assert(!table.fastmem_arena);
    }
    { // Partial outer publication cannot be relabelled a healthy drained subset.
        System system;Common::PageTable table;table.Resize(16,12);
        Cpu cpu{system,&table};cpu.buffer=&system.device.buffer;
        table.fastmem_arena=cpu.buffer->VirtualBasePointer();
        Core::Memory::Memory memory{&cpu};
        memory.MapMemoryRegion(table,P,P,B,Perm::ReadWrite,false);
        publication_observer=[] {throw std::runtime_error("after PTE, before VMA");};
        bool threw=false;
        try {memory.MapMemoryRegion(table,P,P,B+P,Perm::ReadWrite,false);}
        catch(const std::runtime_error&) {threw=true;}
        publication_observer={};
        assert(threw && table.SnapshotMapping(P)->physical_page==B+P);
        assert(!memory.mapping_publications.TryClose() && "partial effects must seal publication");
        bool refused=false;
        try {memory.UnmapRegion(table,P,P,false);} catch(const std::runtime_error&) {refused=true;}
        assert(refused);
    }
    { // Default-off and explicit owner retirement.
        Core::Memory::ControlledMappingPublication ordinary;
        assert(!ordinary.TryClose() && "ordinary publication must remain unenrolled");
        {auto entry=ordinary.Begin();} // Untracked drop cannot create a new state.
        ordinary.Retire(); assert(!ordinary.TryClose());
        Core::Memory::ControlledMappingPublication controlled{true};
        auto close=controlled.TryClose();assert(close);
        controlled.Retire();
        assert(close->Poll()==R::Unknown && !close->ResumeNoEffects() && "retirement must seal old closure");
        assert(controlled.GuestBackingAdmission()==R::Unknown);
    }
    { // Omitted-owner counterexample: a closed Memory is not a PageTable-wide freeze.
        System system;Common::PageTable table;table.Resize(16,12);
        Cpu cpu{system,&table};cpu.buffer=&system.device.buffer;
        table.fastmem_arena=cpu.buffer->VirtualBasePointer();
        Core::Memory::Memory controlled{&cpu},other{&cpu,false};
        controlled.MapMemoryRegion(table,P,P,B,Perm::ReadWrite,false);
        auto closed=controlled.mapping_publications.TryClose();assert(closed);
        assert(closed->Poll()==R::DrainedSubset);
        other.MapMemoryRegion(table,P,P,B+P,Perm::ReadWrite,false);
        assert(table.SnapshotMapping(P)->physical_page==B+P);
        assert(closed->Poll()==R::DrainedSubset);
        assert(controlled.mapping_publications.GuestBackingAdmission()==R::Unknown);
        assert(closed->ResumeNoEffects());
    }
    { // Retirement must join the actual entry lock before its owner can be destroyed.
        Core::Memory::ControlledMappingPublication controlled{true};
        std::latch entered{1},release{1};
        std::thread publisher([&] {
            auto entry=controlled.Begin();entered.count_down();release.wait();entry.Finish();
        });
        entered.wait();
        auto retired=std::async(std::launch::async,[&]{controlled.Retire();});
        bool sealed=false;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
        while(!sealed && std::chrono::steady_clock::now()<deadline) {
            try {auto probe=controlled.Begin();probe.Finish();}
            catch(const std::runtime_error&) {sealed=true;}
            std::this_thread::yield();
        }
        const bool early=retired.wait_for(std::chrono::milliseconds{100})==std::future_status::ready;
        release.count_down(); publisher.join(); retired.get();
        assert(sealed && !early && "retirement escaped lifetime join");
    }
    std::cout<<"PASS six actual outer mapping entries: pre-effect refusal, PTE/VMA split drainage, partial-publication quarantine, retirement and default-off; guest admission Unknown\n";
}
'''
source = head + tail
command = [os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-pthread',
           '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-I', str(stubs), '-I', str(root / 'src')]
if args.boost_include:
    command += ['-I', str(args.boost_include)]
command += [str(root / name) for name in ['src/common/host_memory.cpp', 'src/common/page_table.cpp', 'src/common/virtual_buffer.cpp']]
if os.name == 'nt':
    command += [str(root / 'src/common/dynamic_library.cpp')]


def execute(name, text, override=None, positive=False):
    path = out / (name + '.cpp')
    path.write_text(text)
    injected = stubs / 'core/controlled_mapping_publication.h'
    if override is not None:
        injected.write_text(override)
    try:
        exe = out / (name + ('.exe' if os.name == 'nt' else ''))
        subprocess.run(command + [str(path), '-o', str(exe)], check=True)
        if args.compile_only:
            print('PASS fixture compilation only; runtime and mutation acceptance not attempted', flush=True)
            return
        for repeat in range(1, 4 if positive else 2):
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            log = result.stdout + result.stderr
            (out / f'{name}-{repeat}.txt').write_text(log)
            if positive:
                assert result.returncode == 0 and 'PASS six actual outer mapping entries:' in log, log
            else:
                expected = ('outer entry refusal must precede effects' if name.startswith('unguarded-') else
                    'outer publication escaped drainage' if name == 'probe-before-publication' else
                    'partial effects must seal publication' if name == 'partial-publication-not-sealed' else
                    'retirement must seal old closure' if name == 'retirement-not-sealed' else
                    'retirement escaped lifetime join' if name == 'retirement-not-joined' else
                    'ordinary publication must remain unenrolled')
                assert result.returncode != 0 and 'assert' in log.lower() and expected in log, (name, result.returncode, log)
        print('PASS ' + ('three actual publication repeats' if positive else 'assertion mutant: ' + name), flush=True)
    finally:
        injected.unlink(missing_ok=True)


execute('production', source, positive=True)
if args.compile_only:
    sys.exit(0)
for name in names:
    body = method('void Memory::' + name + '(', '\n}')
    omitted = body.replace('    auto publication = mapping_publications.Begin();\n', '').replace(
        '    auto publication = mapping_publications.BeginCold();\n', '').replace('    publication.Finish();\n', '')
    assert body != omitted
    execute('unguarded-' + name, source.replace(body, omitted))
early = wrappers.replace('    auto publication = mapping_publications.Begin();',
                         '    {auto publication = mapping_publications.Begin();publication.Finish();}')
early = early.replace('    auto publication = mapping_publications.BeginCold();',
                       '    {auto publication = mapping_publications.BeginCold();publication.Finish();}')
early = early.replace('    publication.Finish();\n', '')
execute('probe-before-publication', source.replace(wrappers, early))
guard = 'if (state && !finished) state->entries.Seal();'
assert header.count(guard) == 1
execute('partial-publication-not-sealed', source, header.replace(guard, 'if (state && !finished) {}'))
retire = '        state->entries.Seal();\n        std::unique_lock lifetime'
assert header.count(retire) == 1
execute('retirement-not-sealed', source, header.replace(retire, '        std::unique_lock lifetime'))
join = '        std::unique_lock lifetime{state->lifetime};'
assert header.count(join) == 1
execute('retirement-not-joined', source, header.replace(join, '        std::unique_lock<std::shared_mutex> lifetime;'))
execute('ordinary-autoenrollment', source, header.replace('enrolled ? std::make_shared<State>() : nullptr',
                                                       '(static_cast<void>(enrolled), std::make_shared<State>())'))
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in paths},
    'positive_repeats': 3,
    'assertion_mutants': ['unguarded-' + name for name in names] +
        ['probe-before-publication', 'partial-publication-not-sealed', 'retirement-not-sealed',
         'retirement-not-joined', 'ordinary-autoenrollment'],
    'boundary': 'Extracted actual Memory mapping wrappers/Impl bodies and real HostMemory/PageTable, fixture System; no guest/GPU capability.'
}, indent=2) + '\n')
