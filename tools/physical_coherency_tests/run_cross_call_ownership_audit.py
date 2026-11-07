#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify cross-call ownership retention using extracted production bodies.

Injected PTE, allocator and tree collaborators; not native process acceptance.
Passing covers the stated transaction/bookkeeping cases, not complete guest ownership.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m4-cross-call-audit'
out.mkdir(parents=True, exist_ok=True)
paths = ['src/core/hle/kernel/k_page_table_base.cpp',
         'src/core/hle/kernel/k_page_group.h', 'src/core/memory.cpp',
         'src/core/controlled_mapping_publication.h', 'src/common/producer_drain.h',
         'src/core/hle/kernel/k_memory_block_manager.cpp',
         'tools/physical_coherency_tests/run_cross_call_ownership_audit.py']
paths.append('src/core/hle/kernel/k_page_table_base.h')


def extract(path, marker):
    text = (root / path).read_text()
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


operate = extract(paths[0], 'Result KPageTableBase::Operate(PageLinkedList* page_list, KProcessAddress virt_addr,\n                               size_t num_pages, const KPageGroup&')
scoped = extract(paths[1], 'class KScopedPageGroup {') + ';'
wrapper = extract(paths[2], 'void Memory::MapMemoryRegion(')
finalize = extract(paths[5], 'void KMemoryBlockManager::Finalize(')
pending = extract(paths[7], 'struct IncompleteMapGroup {') + ';'
cleanup = extract(paths[0], 'void KPageTableBase::FinalizeIncompleteMapGroups(')
unmap = extract(paths[2], 'void Memory::UnmapRegion(')
teardown_unmap = extract(paths[2], 'void Memory::UnmapRegionForTeardown(')
source = r'''
#include <array>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include "common/alignment.h"
#include "core/controlled_mapping_publication.h"
using Result=int;
constexpr u64 PageSize=4096;
constexpr u64 GetInteger(u64 value) {return value;}
#define R_SUCCEED() return 0
#define R_TRY(expr) do {const auto result=(expr);if(result) return result;} while(false)
namespace Common {
using ProcessAddress=u64;
using PhysicalAddress=u64;
enum class MemoryPermission {ReadWrite};
struct PageTable {std::array<bool,2> mapped{};};
}
struct Impl {
    unsigned calls{};
    bool throw_after_effect{};
    bool unmap_fault{};
    void UnmapRegion(Common::PageTable& table,u64,u64,bool) {
        table.mapped={false,false};if(unmap_fault) throw std::runtime_error("unknown unmap");
    }
    void MapMemoryRegion(Common::PageTable& table,u64,u64,u64 phys,
                         Common::MemoryPermission,bool) {
        ++calls;table.mapped.at(phys/PageSize)=true;
        if(throw_after_effect) throw std::runtime_error("injected uncertain map");
    }
};
namespace Core::Memory {
struct Memory {
    using MappingTeardown=ControlledMappingPublication::Teardown;
    Impl* impl;
    ControlledMappingPublication mapping_publications;
    Memory(Impl* target,bool controlled):impl{target},mapping_publications{controlled} {}
    void MapMemoryRegion(Common::PageTable&,Common::ProcessAddress,u64,u64,
                         Common::MemoryPermission,bool);
    void UnmapRegion(Common::PageTable&,Common::ProcessAddress,u64,bool);
    void UnmapRegionForTeardown(Common::PageTable&,Common::ProcessAddress,u64,bool,const MappingTeardown&);
    void ValidateMappingTeardown(const MappingTeardown* token) const {mapping_publications.ValidateTeardown(token);}
};
// @WRAPPER@
// @UNMAP@
// @TEARDOWN_UNMAP@
}
namespace Kernel {
using KProcessAddress=u64;
struct PageLinkedList {};
struct KPageProperties {unsigned perm{};};
enum class OperationType {MapGroup,MapFirstGroup,MapFirstGroupPhysical};
Common::MemoryPermission ConvertToMemoryPermission(unsigned) {return Common::MemoryPermission::ReadWrite;}
struct Node {
    unsigned index;
    Core::Memory::ControlledMappingPublication* close_before{};
    unsigned* calls{};
    size_t GetNumPages() const {
        // Deterministic closure BETWEEN completed Memory calls; no shared-lock recursion.
        if(close_before && calls && *calls) close_before->Retire();
        return 1;
    }
    u64 GetAddress() const {return index*PageSize;}
};
struct KernelCore {};
struct KBlockInfoManager {};
static std::array<unsigned,2> physical_refs;
static unsigned opens,first_opens,closes;
static bool fail_descriptor;
struct KPageGroup {
    std::vector<Node> nodes{{0, nullptr, nullptr},{1, nullptr, nullptr}};
    std::array<unsigned,2>& refs=physical_refs;
    unsigned &opens=Kernel::opens, &first_opens=Kernel::first_opens, &closes=Kernel::closes;
    KPageGroup() {physical_refs={};opens=first_opens=closes=0;}
    KPageGroup(KernelCore&,KBlockInfoManager*) {nodes.clear();}
    size_t GetNumPages() const {return 2;}
    auto begin() const {return nodes.begin();}
    auto end() const {return nodes.end();}
    void Open() const {++opens;for(auto& node:nodes) ++refs[node.index];}
    void OpenFirst() const {++first_opens;for(auto& node:nodes) {assert(refs[node.index]==0);++refs[node.index];}}
    void Close() const {++closes;for(auto& node:nodes) {assert(refs[node.index]);--refs[node.index];}}
    void CloseAndReset() {Close();nodes.clear();}
    Result AddBlock(u64 address,size_t) {
        if(fail_descriptor && address==PageSize) return 7;
        nodes.push_back({unsigned(address/PageSize),nullptr,nullptr});return 0;
    }
};
// @SCOPED@
struct KPageTableBase {
    Common::PageTable* m_impl;
    Core::Memory::Memory* m_memory;
    KernelCore m_kernel;
    KBlockInfoManager* m_block_info_manager{};
    // @PENDING@
    std::unique_ptr<IncompleteMapGroup> m_incomplete_map_groups;
    KPageTableBase(Common::PageTable* pte,Core::Memory::Memory* memory):m_impl{pte},m_memory{memory} {}
    void FinalizeIncompleteMapGroups(const Core::Memory::Memory::MappingTeardown* teardown=nullptr);
    bool IsLockedByCurrentThread() const {return true;}
    Result Operate(PageLinkedList*,u64,size_t,const KPageGroup&,KPageProperties,OperationType,bool);
};
// @OPERATE@
// @CLEANUP@
struct KMemoryBlock {
    u64 address;
    u64 GetAddress() const {return address;}
    u64 GetSize() const {return PageSize;}
};
struct Tree {
    std::vector<KMemoryBlock*> nodes;
    struct Iterator {
        Tree* tree;size_t index;
        KMemoryBlock& operator*() const {return *tree->nodes.at(index);}
        bool operator!=(const Iterator& other) const {return index!=other.index;}
    };
    Iterator begin() {return {this,0};}
    Iterator end() {return {this,nodes.size()};}
    Iterator erase(Iterator it) {nodes.erase(nodes.begin()+it.index);return it;}
    bool empty() const {return nodes.empty();}
};
struct KMemoryBlockSlabManager {
    unsigned freed{};
    void Free(KMemoryBlock*) {++freed;}
};
using BlockCallback=std::function<void(u64,u64)>;
struct KMemoryBlockManager {
    Tree m_memory_block_tree;
    void Finalize(KMemoryBlockSlabManager*,BlockCallback&&);
};
// @FINALIZE@
}
int main() {
    using O=Kernel::OperationType;
    for(auto operation:{O::MapGroup,O::MapFirstGroup,O::MapFirstGroupPhysical}) {
        // Success, refusal before first node, close between nodes, uncertain first node,
        // and ordinary untracked control.
        for(unsigned mode=0;mode<7;++mode) {
            Common::PageTable pte;Impl impl;
            Core::Memory::Memory memory{&impl,mode!=4 && mode!=5};
            Kernel::KPageTableBase table{&pte,&memory};Kernel::KPageGroup pg;
            if(mode==1) memory.mapping_publications.Retire();
            if(mode==2 || mode==4) {
                pg.nodes[1].close_before=&memory.mapping_publications;
                pg.nodes[1].calls=&impl.calls;
            }
            impl.throw_after_effect=mode==3 || mode==5;
            Kernel::fail_descriptor=mode==6;
            bool threw=false;Result result=-1;
            try {result=table.Operate(nullptr,PageSize,2,pg,{},operation,false);}
            catch(const std::runtime_error&) {threw=true;}
            assert(pg.opens==(operation==O::MapGroup ? 1u : 0u));
            assert(pg.first_opens==(operation==O::MapGroup ? 0u : 1u));
            if(mode==0 || mode==4) {
                assert(!threw && result==0 && impl.calls==2 && pte.mapped[0] && pte.mapped[1]);
                assert(pg.refs[0]==1 && pg.refs[1]==1 && pg.closes==0);
            } else if(mode==1) {
                assert(threw && impl.calls==0 && !pte.mapped[0] && !pte.mapped[1]);
                assert(pg.refs[0]==0 && pg.refs[1]==0 && pg.closes==1);
            } else if(mode==6) {
                assert(!threw && result==7 && impl.calls==0 && pg.refs[0]==0 &&
                       pg.refs[1]==0 && pg.closes==1 && !table.m_incomplete_map_groups);
            } else {
                assert(threw && impl.calls==1 && pte.mapped[0] && !pte.mapped[1]);
                assert(pg.refs[0]==1 && pg.refs[1]==1 && pg.closes==0 &&
                       table.m_incomplete_map_groups &&
                       "failed transaction must retain mapping references and record");
                bool refused=false;
                try {table.Operate(nullptr,PageSize,2,pg,{},operation,false);}
                catch(const std::runtime_error&) {refused=true;}
                assert(refused && impl.calls==1 && pg.refs[0]==1);
                refused=false;
                try {table.FinalizeIncompleteMapGroups();}
                catch(const Core::Memory::ControlledMappingPublication::Refused&) {refused=true;}
                if(mode==5) {
                    assert(!refused && !table.m_incomplete_map_groups && !pte.mapped[0] &&
                           pg.refs[0]==0 && pg.refs[1]==0 && pg.closes==1);
                } else assert(refused && table.m_incomplete_map_groups && pg.refs[0]==1);
                if(mode!=5) {
                    auto token=memory.mapping_publications.BeginTeardown();
                    impl.unmap_fault=true;bool cleanup_threw=false;
                    try {table.FinalizeIncompleteMapGroups(&token);}
                    catch(const std::runtime_error&) {cleanup_threw=true;}
                    assert(cleanup_threw && table.m_incomplete_map_groups &&
                           pg.refs[0]==1 && pg.refs[1]==1 && pg.closes==0 &&
                           "uncertain cleanup must retain pending group references");
                    // Explicit idempotent injected unmap recovery; no general retry claim.
                    impl.unmap_fault=false;
                    table.FinalizeIncompleteMapGroups(&token);
                    assert(!table.m_incomplete_map_groups && !pte.mapped[0] &&
                           pg.refs[0]==0 && pg.refs[1]==0 && pg.closes==1);
                }
            }
        }
    }
    for(unsigned failure=0;failure<5;++failure) {
        Kernel::KMemoryBlock a{PageSize},b{2*PageSize};
        Kernel::KMemoryBlockManager manager{{{&a,&b}}};
        Kernel::KMemoryBlockSlabManager slab;unsigned callbacks=0;bool threw=false;
        std::array<bool,2> effects{};
        const unsigned fail_at=failure<=2 ? 1 : 2;
        try {manager.Finalize(&slab,[&](u64 addr,u64) {
            ++callbacks;
            assert(manager.m_memory_block_tree.nodes.front()->address==addr &&
                   "Finalize must retain current record during callback");
            if(failure && callbacks==fail_at && failure%2==1)
                throw std::runtime_error("injected before effects");
            effects.at(addr/PageSize-1)=true;
            if(failure && callbacks==fail_at)
                throw std::runtime_error("injected after effects");
        });} catch(const std::runtime_error&) {threw=true;}
        if(failure) {
            assert(threw && callbacks==fail_at && slab.freed==fail_at-1 &&
                   manager.m_memory_block_tree.nodes.size()==3-fail_at &&
                   manager.m_memory_block_tree.nodes[0]==(fail_at==1 ? &a : &b) &&
                   "Finalize must retain failed and untouched records");
            assert(effects.at(fail_at-1)==(failure%2==0));
            // Explicitly idempotent collaborator: this is NOT arbitrary callback rollback.
            manager.Finalize(&slab,[&](u64 addr,u64) {effects.at(addr/PageSize-1)=true;});
            assert(slab.freed==2 && effects[0] && effects[1] && manager.m_memory_block_tree.empty());
        } else assert(!threw && callbacks==2 && slab.freed==2 && manager.m_memory_block_tree.empty());
    }
    std::cout<<"PASS cross-call audit: MapGroup references and Finalize records retained; injected collaborators only\n";
}
'''
for marker, body in [('WRAPPER', wrapper), ('SCOPED', scoped), ('OPERATE', operate),
                     ('FINALIZE', finalize), ('PENDING', pending), ('CLEANUP', cleanup),
                     ('UNMAP', unmap), ('TEARDOWN_UNMAP', teardown_unmap)]:
    source = source.replace('// @' + marker + '@', body)
cpp = out / 'audit.cpp'
cpp.write_text(source)
exe = out / ('audit.exe' if os.name == 'nt' else 'audit')
command = [os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-pthread',
           '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-I', str(root / 'src')]
subprocess.run(command + [str(cpp), '-o', str(exe)], check=True)
for repeat in range(1, 4):
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
    (out / f'audit-{repeat}.txt').write_text(result.stdout + result.stderr)
    assert result.returncode == 0 and 'PASS cross-call audit:' in result.stdout, result
    print(result.stdout.strip(), flush=True)
# Restore the original defects; only the corresponding assertion is a valid kill.
retained = operate.replace('            spg.CancelClose();', '            // mutant: lose mapping references')
reordered = finalize.replace('        it = m_memory_block_tree.erase(it);\n', '')
reordered = reordered.replace('        block_callback(block->GetAddress(), block->GetSize());',
                              '        it = m_memory_block_tree.erase(it);\n'
                              '        block_callback(block->GetAddress(), block->GetSize());')
controls = [('close-retained-prefix', source.replace(operate, retained),
             'failed transaction must retain mapping references and record'),
            ('erase-before-callback', source.replace(finalize, reordered),
             'Finalize must retain current record during callback')]
for name, changed, expected in controls:
    assert changed != source, name
    control_cpp = out / (name + '.cpp')
    control_cpp.write_text(changed)
    control_exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(command + [str(control_cpp), '-o', str(control_exe)], check=True)
    result = subprocess.run([str(control_exe)], capture_output=True, text=True, timeout=15)
    log = result.stdout + result.stderr
    (out / (name + '.txt')).write_text(log)
    assert result.returncode != 0 and 'assert' in log.lower() and expected in log, (name, log)
    print('PASS audit sensitivity control: ' + name, flush=True)
(out / 'extraction.json').write_text(json.dumps({
    'status': 'mapping-reference-retention-and-finalize-bookkeeping-fixed',
    'repeats': 3,
    'sensitivity_controls': [name for name, _, _ in controls],
    'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in paths},
}, indent=2) + '\n')
