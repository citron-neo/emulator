#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual Operate/Memory unmap and page-group cleanup bodies; injected kernel/refcounts.

This is an exception-ordering fixture, not native allocator, process teardown,
physical ownership completeness or guest coherency acceptance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--expect-defect', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m4-process-unmap'
out.mkdir(parents=True, exist_ok=True)
paths = ['src/core/hle/kernel/k_page_table_base.cpp', 'src/core/hle/kernel/k_page_group.cpp',
         'src/core/memory.cpp', 'src/core/controlled_mapping_publication.h',
         'src/common/producer_drain.h', 'src/common/scope_exit.h',
         'tools/physical_coherency_tests/run_process_unmap_tests.py']


def body(path, marker):
    text = (root / path).read_text()
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


operate = body(paths[0], 'Result KPageTableBase::Operate(PageLinkedList* page_list, KProcessAddress virt_addr,\n')
unmap = body(paths[2], 'void Memory::UnmapRegion(')
cleanup = '\n'.join(body(paths[1], 'void KPageGroup::' + name + '(') for name in ['CloseAndReset', 'Finalize'])
prefix = r'''
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <utility>
#include "common/alignment.h"
#include "common/scope_exit.h"
#include "core/controlled_mapping_publication.h"
using Result=int;
constexpr u64 PageSize=4096;
constexpr u64 GetInteger(u64 value) {return value;}
#define R_SUCCEED() return 0
#define R_TRY(expr) do {const auto result=(expr);if(result) return result;} while(false)
namespace Common {
using ProcessAddress=u64;
enum class MemoryPermission {ReadWrite};
struct PageTable {bool mapped=true;}; // Kernel/PTE collaborator, not a real PageTable.
}
struct Impl {
    enum class Fault {None,BeforeEffects,AfterEffects} fault{};
    unsigned calls{};bool separate{};
    void UnmapRegion(Common::PageTable& table,u64,u64,bool separate_heap) {
        ++calls;separate=separate_heap;
        if(fault==Fault::BeforeEffects) throw std::runtime_error("injected pre-effect refusal");
        table.mapped=false;
        if(fault==Fault::AfterEffects) throw std::runtime_error("injected uncertain unmap");
    }
};
namespace Core::Memory {
struct Memory {
    using MappingTeardown=ControlledMappingPublication::Teardown;
    Impl* impl;
    ControlledMappingPublication mapping_publications{true};
    explicit Memory(Impl* target):impl{target} {}
    void UnmapRegion(Common::PageTable&,Common::ProcessAddress,u64,bool);
    void ValidateMappingTeardown(const MappingTeardown& token) {mapping_publications.ValidateTeardown(token);}
    void UnmapRegionForTeardown(Common::PageTable& table,u64 addr,u64 size,bool separate,const MappingTeardown& token) {
        ValidateMappingTeardown(token);impl->UnmapRegion(table,addr,size,separate);
    }
    void MapMemoryRegion(Common::PageTable&,u64,u64,u64,Common::MemoryPermission,bool) {}
    void ProtectRegion(Common::PageTable&,u64,u64,Common::MemoryPermission) {}
};
// @UNMAP@
}
namespace Kernel {
using KProcessAddress=u64;
using KPhysicalAddress=u64;
struct PageLinkedList {};
struct KPageProperties {unsigned perm{};};
enum class OperationType {Unmap,UnmapPhysical,Map,Separate,ChangePermissions,
    ChangePermissionsAndRefresh,ChangePermissionsAndRefreshAndFlush};
Common::MemoryPermission ConvertToMemoryPermission(unsigned) {return Common::MemoryPermission::ReadWrite;}
struct Refcounts {
    Common::PageTable* table;
    unsigned refs=1,closed{},opened{};bool closed_while_mapped=false;
    void Open(u64,size_t pages) {refs+=static_cast<unsigned>(pages);++opened;}
    void Close(u64,size_t pages) {
        assert(refs>=pages);refs-=static_cast<unsigned>(pages);++closed;
        closed_while_mapped|=table->mapped;
    }
};
struct KernelCore {
    Refcounts refs;
    explicit KernelCore(Common::PageTable& table):refs{&table} {}
    Refcounts& MemoryManager() {return refs;}
};
struct KBlockInfo {
    KBlockInfo* next{};
    auto* GetNext() const {return next;}
    u64 GetAddress() const {return 0x80000000;}
    size_t GetNumPages() const {return 1;}
};
struct KBlockInfoManager {
    unsigned freed{};
    void Free(KBlockInfo* block) {++freed;delete block;}
};
struct KPageGroup {
    KernelCore& m_kernel;
    KBlockInfo* m_first_block{};
    KBlockInfo* m_last_block{};
    KBlockInfoManager* m_manager;
    KPageGroup(KernelCore& kernel,KBlockInfoManager* manager):m_kernel{kernel},m_manager{manager} {}
    ~KPageGroup() {Finalize();}
    void CloseAndReset();void Finalize();
};
// @CLEANUP@
struct KPageTableBase {
    KernelCore& m_kernel;
    Common::PageTable* m_impl;
    Core::Memory::Memory* m_memory;
    KBlockInfoManager blocks;
    bool non_heap{};
    bool m_incomplete_map_groups{};
    bool IsLockedByCurrentThread() const {return true;}
    bool ContainsPages(u64 address,size_t count) const {return address==4096 && count==1;}
    auto* GetBlockInfoManager() {return &blocks;}
    Result MakePageGroup(KPageGroup& group,u64,size_t) {
        // MakePageGroup refuses non-heap backing, which has no allocator references.
        if(non_heap) return 7;
        // No physical Open, as in MakePageGroup.
        group.m_first_block=new KBlockInfo;group.m_last_block=group.m_first_block;
        return 0;
    }
    bool IsHeapPhysicalAddress(u64) const {return true;}
    Result Operate(PageLinkedList*,u64,size_t,u64,bool,KPageProperties,OperationType,bool,
                   const Core::Memory::Memory::MappingTeardown* teardown=nullptr);
};
// @OPERATE@
}
'''
tail = r'''
int main() {
    using O=Kernel::OperationType;
    for(auto operation:{O::Unmap,O::UnmapPhysical}) {
        for(unsigned mode=0;mode<5;++mode) {
            Common::PageTable mapping;Impl impl;Core::Memory::Memory memory{&impl};
            Kernel::KernelCore kernel{mapping};
            Kernel::KPageTableBase table{kernel,&mapping,&memory,{},false};
            auto closed=mode==0 ? memory.mapping_publications.TryClose() :
                std::optional<Common::ProducerDrain::Closure>{};
            if(mode==1) impl.fault=Impl::Fault::BeforeEffects;
            if(mode==2) impl.fault=Impl::Fault::AfterEffects;
            if(mode==4) {table.non_heap=true;kernel.refs.refs=0;}
            bool threw=false;Result result=-1;
            try {result=table.Operate(nullptr,4096,1,0,false,{},operation,false);}
            catch(const std::runtime_error&) {threw=true;}
            if(mode<=1) {
                assert(threw && mapping.mapped);
                assert(kernel.refs.refs==1 && kernel.refs.closed==0 &&
                       "unmap refusal released physical refs");
                assert(impl.calls==(mode==0 ? 0u : 1u));
            } else if(mode==2) {
                assert(threw && !mapping.mapped);
                assert(kernel.refs.refs==1 && kernel.refs.closed==0 &&
                       "uncertain unmap released physical refs");
            } else if(mode==3) {
                assert(!threw && result==0 && !mapping.mapped);
                assert(kernel.refs.refs==0 && kernel.refs.closed==1 &&
                       !kernel.refs.closed_while_mapped && "unmap cleanup must follow successful mutation");
                assert(impl.separate==(operation==O::UnmapPhysical));
            } else {
                assert(!threw && result==0 && !mapping.mapped && impl.calls==1 &&
                       kernel.refs.refs==0 && kernel.refs.closed==0 &&
                       "non-heap unmap must preserve existing behavior");
            }
            assert(table.blocks.freed==(mode==4 ? 0u : 1u) && kernel.refs.opened==0);
        }
    }
    for(unsigned mode=0;mode<4;++mode) {
        Common::PageTable mapping;Impl impl;Core::Memory::Memory memory{&impl};
        Kernel::KernelCore kernel{mapping};Kernel::KPageTableBase table{kernel,&mapping,&memory,{},false};
        table.m_incomplete_map_groups=true; // Disjoint pending group, TLS cleanup still allowed.
        Core::Memory::ControlledMappingPublication foreign{true};
        auto token=memory.mapping_publications.BeginTeardown();
        auto foreign_token=foreign.BeginTeardown();
        bool threw=false;
        try {assert(table.Operate(nullptr,4096,1,0,false,{},mode==1 ? O::Map :
                                  mode==3 ? O::UnmapPhysical : O::Unmap,false,
                                  mode==2 ? &foreign_token : &token)==0);}
        catch(const std::runtime_error&) {threw=true;}
        if(mode==1 || mode==2) {
            assert(threw && mapping.mapped && impl.calls==0 && kernel.refs.refs==1 &&
                   "invalid teardown operation must refuse before effects");
        } else assert(!threw && !mapping.mapped && impl.calls==1 && kernel.refs.refs==0 &&
                      kernel.refs.closed==1 && !kernel.refs.closed_while_mapped);
    }
    std::cout<<"PASS actual Unmap/UnmapPhysical cleanup: closed entry, pre-effect/uncertain exception, success ordering and non-heap control; injected kernel/refcounts, no guest lease\n";
}
'''
source = prefix.replace('// @UNMAP@', unmap).replace('// @CLEANUP@', cleanup).replace('// @OPERATE@', operate) + tail
command = [os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra',
           '-Werror', '-Wno-unused-parameter', '-I', str(root / 'src')]


def execute(name, text, expected=None):
    path = out / (name + '.cpp')
    path.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(command + [str(path), '-o', str(exe)], check=True)
    for repeat in range(1, 2 if expected else 4):
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
        log = result.stdout + result.stderr
        (out / f'{name}-{repeat}.txt').write_text(log)
        if expected:
            assert result.returncode != 0 and 'assert' in log.lower() and expected in log, (name, result.returncode, log)
        else:
            assert result.returncode == 0 and 'PASS actual Unmap/UnmapPhysical cleanup:' in log, log
    print('PASS ' + ('expected assertion: ' + name if expected else 'three actual cleanup repeats'), flush=True)


if args.expect_defect:
    execute('baseline-defect', source, 'unmap refusal released physical refs')
    print('Baseline defect reproduced; no fixed acceptance claimed')
else:
    execute('production', source)
    close = '        pages_to_close.CloseAndReset();'
    assert operate.count(close) == 1
    group = '        KPageGroup pages_to_close(m_kernel, this->GetBlockInfoManager());'
    assert operate.count(group) == 1
    legacy = operate.replace(close, '').replace(group,
        group + '\n        SCOPE_EXIT {\n' + close + '\n        };')
    execute('old-scope-exit', source.replace(operate, legacy), 'unmap refusal released physical refs')
    execute('missing-success-cleanup', source.replace(close, ''),
            'unmap cleanup must follow successful mutation')
    (out / 'extraction.json').write_text(json.dumps({
        'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in paths},
        'positive_repeats': 3, 'assertion_mutants': ['old-scope-exit', 'missing-success-cleanup'],
        'boundary': 'Actual Operate/Memory wrapper and KPageGroup cleanup bodies; injected mapping/kernel/refcounts, not a native allocator or guest lease'
    }, indent=2) + '\n')
