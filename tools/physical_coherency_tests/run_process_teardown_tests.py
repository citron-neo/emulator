#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Extracted TLS cleanup, owner teardown prefix and real mapping entry domain.

Kernel trees/PTEs/page buffers are injected; this is not native process acceptance.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m4-process-teardown'
out.mkdir(parents=True, exist_ok=True)
paths = ['src/core/hle/kernel/k_process.cpp', 'src/core/hle/kernel/k_thread_local_page.cpp',
         'src/core/memory.cpp', 'src/core/controlled_mapping_publication.h',
         'src/common/producer_drain.h', 'tools/physical_coherency_tests/run_process_teardown_tests.py']


def body(path, marker):
    text = (root / path).read_text()
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


delete = body(paths[0], 'Result KProcess::DeleteThreadLocalRegion(')
tls = body(paths[1], 'Result KThreadLocalPage::Finalize(')
owner = body(paths[0], 'void KProcess::Finalize()')
owner = owner[:owner.index('    // Finish using our system resource.')]
owner = owner.replace('void KProcess::Finalize()', 'void KProcess::FinalizePrefix()')
owner += '    assert(used_memory_size==0); m_memory.Finalize();\n}'
unmap = body(paths[2], 'void Memory::UnmapRegion(')
cleanup = body(paths[2], 'void Memory::UnmapRegionForTeardown(')
source = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include "common/alignment.h"
#include "core/controlled_mapping_publication.h"
using Result=int;
constexpr u64 PageSize=4096;
constexpr int ResultSuccess=0,ResultInvalidAddress=7,ResultInvalidState=8;
constexpr u64 GetInteger(u64 value) {return value;}
#define R_SUCCEED() return 0
#define R_FAILED(result) ((result)!=0)
#define R_UNLESS(test,result) do {if(!(test)) return result;} while(false)
#define R_TRY(expr) do {const auto r=(expr);if(r) return r;} while(false)
#define R_ASSERT(expr) do {if((expr)!=0) throw std::runtime_error("owner TLS cleanup failed");} while(false)
namespace Common {
using ProcessAddress=u64;
struct PageTable {bool mapped=true;};
}
struct Impl {
    unsigned calls{};unsigned fault{};
    void UnmapRegion(Common::PageTable& table,u64,u64,bool) {
        ++calls;
        if(fault==2) throw std::runtime_error("before effects");
        table.mapped=false;
        if(fault==3) throw std::runtime_error("unknown after effects");
    }
};
namespace Core::Memory {
struct Memory {
    using MappingTeardown=ControlledMappingPublication::Teardown;
    Impl* impl;
    ControlledMappingPublication mapping_publications;
    bool cpu_closed{},disposed{};
    Memory(Impl* target,bool controlled):impl{target},mapping_publications{controlled} {}
    void SealControlledCpuWrites() {cpu_closed=true;}
    auto BeginMappingTeardown() {assert(cpu_closed);return mapping_publications.BeginTeardown();}
    void ValidateMappingTeardown(const MappingTeardown& token) {mapping_publications.ValidateTeardown(token);}
    void ValidateMappingTeardown(const MappingTeardown* token) {mapping_publications.ValidateTeardown(token);}
    void Finalize() {mapping_publications.Retire();disposed=true;}
    void UnmapRegion(Common::PageTable&,Common::ProcessAddress,u64,bool);
    void UnmapRegionForTeardown(Common::PageTable&,Common::ProcessAddress,u64,bool,const MappingTeardown&);
};
// @UNMAP@
// @CLEANUP@
}
namespace Kernel {
using KProcessAddress=u64;using KPhysicalAddress=u64;
enum class KMemoryState {ThreadLocal};
struct KernelCore {KernelCore& System() {return *this;}};
struct KScopedSchedulerLock {explicit KScopedSchedulerLock(KernelCore&) {}};
struct KProcess;
static unsigned descriptor_frees,backing_frees;
struct KPageBuffer {
    static KPageBuffer* FromPhysicalAddress(KernelCore&,u64) {return nullptr;}
    static void Free(KernelCore&,KPageBuffer*) {++backing_frees;}
};
struct KThreadLocalPage {
    KProcess* m_owner;KernelCore* m_kernel;u64 m_virt_addr=PageSize;
    unsigned regions=1;
    void Release(u64) {assert(regions);--regions;}
    bool IsAllFree() const {return regions==0;}
    u64 GetAddress() const {return m_virt_addr;}
    static void Free(KernelCore&,KThreadLocalPage*) {++descriptor_frees;}
    Result Finalize(const Core::Memory::ControlledMappingPublication::Teardown* teardown=nullptr);
};
struct Tree {
    std::vector<KThreadLocalPage*> nodes;
    struct Iterator {
        Tree* tree;size_t index;
        KThreadLocalPage& operator*() const {return *tree->nodes.at(index);}
        KThreadLocalPage* operator->() const {return tree->nodes.at(index);}
        bool operator==(const Iterator& other) const {return tree==other.tree && index==other.index;}
        bool operator!=(const Iterator& other) const {return !(*this==other);}
    };
    Iterator end() {return {this,nodes.size()};}
    Iterator find_key(u64 address) {
        for(size_t i=0;i<nodes.size();++i) if(nodes[i]->m_virt_addr==address) return {this,i};
        return end();
    }
    void insert(KThreadLocalPage& page) {nodes.push_back(&page);}
    void erase(Iterator it) {nodes.erase(nodes.begin()+it.index);}
    bool empty() const {return nodes.empty();}
};
struct PageTable {
    Core::Memory::Memory& memory;Common::PageTable pte;
    bool finalized{};unsigned result_fault{};
    explicit PageTable(Core::Memory::Memory& memory_):memory{memory_} {}
    bool GetPhysicalAddress(u64* output,u64) {*output=PageSize;return pte.mapped;}
    Result UnmapPages(u64 address,size_t pages,KMemoryState,
                      const Core::Memory::Memory::MappingTeardown* token) {
        if(result_fault) return 9;
        if(token) memory.UnmapRegionForTeardown(pte,address,pages*PageSize,false,*token);
        else memory.UnmapRegion(pte,address,pages*PageSize,false);
        return 0;
    }
    void Finalize(const Core::Memory::Memory::MappingTeardown* token) {
        memory.ValidateMappingTeardown(token);assert(!pte.mapped && backing_frees==1);
        assert(!memory.disposed);finalized=true;
    }
};
struct KProcess {
    KernelCore m_kernel;Core::Memory::Memory m_memory;PageTable m_page_table;
    Tree m_partially_used_tlp_tree,m_fully_used_tlp_tree,m_finalizing_tlp_tree;
    u64 m_plr_address=PageSize;
    KProcess(Impl& impl,bool controlled):m_memory{&impl,controlled},m_page_table{m_memory} {}
    PageTable& GetPageTable() {return m_page_table;}
    size_t GetUsedNonSystemUserPhysicalMemorySize() {assert(backing_frees==1);return 0;}
    Result DeleteThreadLocalRegion(u64,const Core::Memory::Memory::MappingTeardown* teardown=nullptr);
    void FinalizePrefix();
};
// @TLS@
// @DELETE@
// @OWNER@
}
int main() {
    using Domain=Core::Memory::ControlledMappingPublication;
    for(bool controlled:{false,true}) for(bool full:{false,true}) {
        for(unsigned mode=0;mode<5;++mode) {
            Impl impl;Kernel::KProcess process{impl,controlled};
            Kernel::descriptor_frees=Kernel::backing_frees=0;
            Kernel::KThreadLocalPage page{&process,&process.m_kernel};
            (full ? process.m_fully_used_tlp_tree : process.m_partially_used_tlp_tree).insert(page);
            auto token=process.m_memory.mapping_publications.BeginTeardown();
            if(mode==1) process.m_page_table.result_fault=1;
            impl.fault=mode;
            Result result=-1;bool threw=false;
            try {result=process.DeleteThreadLocalRegion(PageSize,mode==4 ? nullptr : &token);}
            catch(const std::runtime_error&) {threw=true;}
            const bool success=mode==0 || (mode==4 && !controlled);
            if(success) {
                assert(!threw && result==0 && !process.m_page_table.pte.mapped &&
                       Kernel::descriptor_frees==1 && Kernel::backing_frees==1 &&
                       process.m_finalizing_tlp_tree.empty());
            } else {
                assert((threw || result!=0) && Kernel::descriptor_frees==0 &&
                       Kernel::backing_frees==0 && !process.m_finalizing_tlp_tree.empty() &&
                       "failed TLS cleanup must retain owner and backing");
                assert(process.DeleteThreadLocalRegion(PageSize,&token)==ResultInvalidState &&
                       "uncertain TLS cleanup must not silently retry");
            }
            assert(process.m_partially_used_tlp_tree.empty() && process.m_fully_used_tlp_tree.empty());
        }
        Impl impl;Kernel::KProcess process{impl,controlled};
        Kernel::descriptor_frees=Kernel::backing_frees=0;
        Kernel::KThreadLocalPage page{&process,&process.m_kernel};
        process.m_partially_used_tlp_tree.insert(page);
        process.FinalizePrefix();
        assert(process.m_plr_address==0 && process.m_page_table.finalized && process.m_memory.disposed);
    }
    {
        Impl impl;Kernel::KProcess process{impl,true};
        Kernel::descriptor_frees=Kernel::backing_frees=0;
        Kernel::KThreadLocalPage page{&process,&process.m_kernel};
        process.m_partially_used_tlp_tree.insert(page);
        Domain foreign{true};auto token=foreign.BeginTeardown();bool refused=false;
        try {process.DeleteThreadLocalRegion(PageSize,&token);}
        catch(const Domain::Refused&) {refused=true;}
        assert(refused && page.regions==1 && impl.calls==0 &&
               !process.m_partially_used_tlp_tree.empty() && process.m_finalizing_tlp_tree.empty() &&
               "foreign cleanup token must refuse before TLS effects");
        process.m_page_table.result_fault=1;refused=false;
        try {process.FinalizePrefix();} catch(const std::runtime_error&) {refused=true;}
        assert(refused && !process.m_page_table.finalized && !process.m_memory.disposed &&
               !process.m_finalizing_tlp_tree.empty() && Kernel::backing_frees==0 &&
               "failed TLS owner sequence must retain PageTable and Memory");
    }
    {
        Impl impl;Kernel::KProcess process{impl,true};
        Kernel::KThreadLocalPage page{&process,&process.m_kernel};
        process.m_plr_address=0;process.m_finalizing_tlp_tree.insert(page);
        bool refused=false;
        try {process.FinalizePrefix();} catch(const std::runtime_error&) {refused=true;}
        assert(refused && !process.m_page_table.finalized && !process.m_memory.disposed &&
               "prior TLS quarantine must stop process teardown in release builds");
    }
    {
        Domain first{true},second{true};auto token=first.BeginTeardown();
        Impl impl;Common::PageTable pte;Core::Memory::Memory memory{&impl,true};
        bool direct_refused=false;
        try {memory.UnmapRegionForTeardown(pte,PageSize,PageSize,false,token);}
        catch(const Domain::Refused&) {direct_refused=true;}
        assert(direct_refused && impl.calls==0 && pte.mapped &&
               "foreign unmap token must refuse before effects");
        bool refused=false;
        try {second.ValidateTeardown(token);} catch(const Domain::Refused&) {refused=true;}
        assert(refused && "foreign teardown entry must refuse");
        auto moved=std::move(token);refused=false;
        try {first.ValidateTeardown(token);} catch(const Domain::Refused&) {refused=true;}
        assert(refused && "moved teardown entry must refuse");first.ValidateTeardown(moved);
        std::thread other{[&] {
            bool wrong_thread=false;
            try {first.ValidateTeardown(moved);} catch(const Domain::Refused&) {wrong_thread=true;}
            assert(wrong_thread && "teardown entry must remain on owner thread");
        }};other.join();
        refused=false;
        try {auto entry=first.Begin();(void)entry;} catch(const Domain::Refused&) {refused=true;}
        assert(refused && "teardown must never reopen normal admission");
    }
    {
        Domain domain{true};std::promise<void> entered,release;
        auto release_signal=release.get_future();
        auto producer=std::async(std::launch::async,[&] {
            auto entry=domain.Begin();entered.set_value();release_signal.wait();entry.Finish();
        });entered.get_future().wait();
        std::promise<void> starting;
        auto teardown=std::async(std::launch::async,[&] {
            starting.set_value();auto token=domain.BeginTeardown();domain.ValidateTeardown(token);
        });starting.get_future().wait();
        assert(teardown.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout &&
               "teardown must join entered publications");
        release.set_value();producer.get();teardown.get();
    }
    std::cout<<"PASS owner teardown sequence, TLS retention, entry identity/thread/move, closed admission and lifetime join; injected kernel only\n";
}
'''
for marker, text in [('DELETE', delete), ('TLS', tls), ('OWNER', owner), ('UNMAP', unmap), ('CLEANUP', cleanup)]:
    source = source.replace('// @' + marker + '@', text)
command = [os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra',
           '-Werror', '-Wno-unused-parameter', '-I', str(root / 'src')]


def execute(name, text, expected=None):
    cpp = out / (name + '.cpp')
    cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(command + [str(cpp), '-o', str(exe)], check=True)
    for repeat in range(1, 2 if expected else 4):
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
        log = result.stdout + result.stderr
        (out / f'{name}-{repeat}.txt').write_text(log)
        if expected:
            assert result.returncode != 0 and 'assert' in log.lower() and expected in log, (name, log)
        else:
            assert result.returncode == 0 and 'PASS owner teardown sequence' in log, log
    print('PASS ' + name, flush=True)


execute('production-three-repeats', source)
ignored = delete.replace('R_TRY(page_to_free->Finalize(teardown));', 'page_to_free->Finalize(teardown);')
assert ignored != delete
execute('ignored-tls-result', source.replace(delete, ignored), 'failed TLS cleanup must retain owner and backing')
unguarded = cleanup.replace('    mapping_publications.ValidateTeardown(entry);', '')
assert unguarded != cleanup
execute('unguarded-cleanup', source.replace(cleanup, unguarded), 'foreign unmap token must refuse before effects')
(out / 'extraction.json').write_text(json.dumps({
    'positive_repeats': 3, 'mutants': ['ignored-tls-result', 'unguarded-cleanup'],
    'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in paths},
}, indent=2) + '\n')
