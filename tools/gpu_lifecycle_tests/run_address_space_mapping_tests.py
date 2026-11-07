# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual NV mapping operations and GMMU callbacks versus terminal GPU teardown."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--emit-only', action='store_true', help='Emit the actual-body fixture without compiling/running')
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--case', choices=['closed', 'all'], default='all')
p.add_argument('--control-prefix', action='append', default=[])
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-address-space-mapping').resolve()
out.mkdir(parents=True, exist_ok=True)


def read(name):
    return (root / name).read_text(encoding='utf-8')


def method(text, marker):
    start = text.index(marker)
    pos = text.index('{', start)
    level, end = 1, pos + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


gpu = read('src/video_core/gpu.cpp')
subprocess.run([sys.executable, str(root / 'tools/gpu_lifecycle_tests/run_tests.py'),
                '--cxx', a.cxx, '--repeat', str(a.repeat), '--output-dir', str(out / 'gpu')], check=True)
base = (out / 'gpu/gpu_requests.cpp').read_text().split('};\nvoid lifecycle()')[0]
assert method(gpu, '~Impl()') in base and method(gpu, 'void NotifyShutdown()') in base
base = base.replace('struct Manager {', 'struct Manager {\n    Event* stopped=nullptr;')
base = base.replace('if(worker.joinable()) { worker.request_stop(); worker.join(); }',
                    'if(worker.joinable()) { worker.request_stop(); worker.join(); }\n'
                    '        if(stopped) stopped->Set();')
base = base.replace('    void Shutdown() {\n        if(life)', '''    bool retired=false;
    std::function<void()> mapping_hook;
    void MappingCallback() {if(mapping_hook)mapping_hook();Require(!retired,"callback used retired rasterizer");}
    void ModifyGPUMemory(size_t,u64,u64) {MappingCallback();}
    void UnmapMemory(u64,u64) {MappingCallback();}
    void Shutdown() {
        retired=true;
        if(life)''', 1)
entry = method(gpu, '    std::optional<Common::TerminalIngress::Entry> AcquireChannelBinding()')
base += '\n' + entry + '\n};\n'
forward = method(gpu, 'std::optional<Common::TerminalIngress::Entry> GPU::AcquireChannelBinding(')
forward = forward.replace('GPU::', '').replace('return impl->AcquireChannelBinding();',
    'auto result=impl->AcquireChannelBinding();if(result && acquired_hook)acquired_hook();return result;')

nv = read('src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp')
header = read('src/core/hle/service/nvdrv/devices/nvhost_as_gpu.h')
names = ['AllocateSpace', 'FreeSpace', 'Remap', 'MapBufferEx', 'UnmapBuffer']
bodies = {name: method(nv, 'NvResult nvhost_as_gpu::' + name + '(').replace('nvhost_as_gpu::', '')
          for name in names}
# Keep the terminal Entry alive at the final service-success acknowledgement.
for name in names:
    bodies[name] = bodies[name].replace('    return NvResult::Success;',
        '    if(ack_hook) ack_hook();\n    return NvResult::Success;')
free_mapping = method(nv, 'void nvhost_as_gpu::FreeMappingLocked(').replace('nvhost_as_gpu::', '')
memory = read('src/video_core/memory_manager.cpp')
mapping_methods = '\n'.join(method(memory, signature).replace('MemoryManager::', '') for signature in (
    'GPUVAddr MemoryManager::Map(', 'GPUVAddr MemoryManager::MapSparse(', 'void MemoryManager::Unmap('))
mapping_methods = mapping_methods.replace('bool is_big_pages)', 'bool is_big_pages = true)')
mapping_methods = mapping_methods.replace('PTEKind kind,', 'PTEKind kind = PTEKind::INVALID,')
notify = method(memory, 'void MemoryManager::NotifyMappingChange(').replace('MemoryManager::', '')
structures = '\n'.join(method(header, '    struct ' + name + ' {').replace(
    'NvCore::NvMap::Handle::Id', 'u32') + ';' for name in (
        'IoctlAllocSpace', 'IoctlFreeSpace', 'IoctlRemapEntry', 'IoctlMapBufferEx',
        'IoctlUnmapBuffer', 'Mapping', 'Allocation'))
vm = method(header, '    struct VM {').replace('Common::FlatAllocator<u32, 0, 32>', 'Allocator') + ';'

fixture = r'''
struct PublicGpu {Impl* impl;std::function<void()> acquired_hook;
    inline static std::function<void()> shutdown_observer;
    void NotifyShutdown() {if(shutdown_observer)shutdown_observer();impl->NotifyShutdown();}
''' + forward + r'''
};
struct System {PublicGpu& gpu;PublicGpu& GPU() {return gpu;}};
enum class NvResult {Success,InvalidState,BadValue,NotImplemented,InsufficientMemory};
#define LOG_DEBUG(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define ASSERT_MSG(...) ((void)0)
#define ASSERT(...) ((void)0)
#define UNIMPLEMENTED_MSG(...) ((void)0)
#define INSERT_PADDING_WORDS(n)
enum class MappingFlags : u32 {None=0,Fixed=1,Sparse=2,Remap=256};
inline MappingFlags operator&(MappingFlags a,MappingFlags b) {return MappingFlags(u32(a)&u32(b));}
namespace Tegra {
enum class PTEKind {INVALID};
struct MemoryManager {
    enum class EntryType {Free,Reserved,Mapped};
    VideoCore::Rasterizer* rasterizer;
    std::atomic_bool unmap_failed{};
    bool HasUnmapFailure() const {return unmap_failed.load();}
    void QuarantineUnmap() {unmap_failed=true;}
    std::vector<std::pair<u64,u64>> subranges;
    std::mutex mapping_update_guard,page_table_guard;
    u64 address_space_size=1ULL<<40,page_size=4096,big_page_size=65536;
    size_t unique_identifier=17;int generation=0;
    std::map<u64,EntryType> pages;
    std::vector<std::pair<u64,u64>> page_stash;
    auto AcquireMappingRead() {return std::unique_lock{page_table_guard};}
    template<bool> EntryType GetEntry(u64 address) {auto it=pages.find(address);return it==pages.end()?EntryType::Free:it->second;}
    template<EntryType E> GPUVAddr PageTableOp(GPUVAddr gpu,DAddr,size_t size,PTEKind) {
        ++generation;if(size)pages[gpu]=E;return gpu;
    }
    template<EntryType E> GPUVAddr BigPageTableOp(GPUVAddr gpu,DAddr dev,size_t size,PTEKind kind) {
        return PageTableOp<E>(gpu,dev,size,kind);
    }
    template<bool> void GetSubmappedRangeImpl(u64 addr,size_t size,auto& output) {
        if(!subranges.empty()) {output.insert(output.end(),subranges.begin(),subranges.end());return;}
        if(GetEntry<true>(addr)==EntryType::Mapped)output.emplace_back(addr,size);
    }
    template<bool is_big_page,EntryType entry_type>
''' + notify + '\n' + mapping_methods + r'''
};
}
struct Allocator {
    int allocations=0,frees=0;std::function<void()> hook;
    u32 Allocate(u32) {if(hook)hook();++allocations;return 16;}
    void AllocateFixed(u32,u32) {if(hook)hook();++allocations;}
    void Free(u32,u32) {if(hook)hook();++frees;}
};
struct NvMap {
    struct Handle {u64 orig_size=65536,align=65536;};
    std::shared_ptr<Handle> handle=std::make_shared<Handle>();
    int lookups=0,pins=0,unpins=0;std::function<void()> hook;
    auto GetHandle(u32 id) {if(hook)hook();++lookups;return id==1?handle:nullptr;}
    DAddr PinHandle(u32,bool) {if(hook)hook();++pins;return 0x400000;}
    void UnpinHandle(u32) {if(hook)hook();++unpins;}
};
struct AddressSpace {
''' + structures + '\n' + vm + r'''
    System& system;std::mutex mutex;NvMap nvmap;
    VM vm;std::shared_ptr<Tegra::MemoryManager> gmmu;
    std::map<u64,Allocation> allocation_map;
    std::map<u64,std::shared_ptr<Mapping>> mapping_map;
    std::function<void()> ack_hook;
    explicit AddressSpace(System& s,VideoCore::Rasterizer& r):system{s},gmmu{std::make_shared<Tegra::MemoryManager>()} {
        gmmu->rasterizer=&r;vm.initialised=true;vm.big_page_size=65536;vm.big_page_size_bits=16;
        vm.small_page_allocator=std::make_shared<Allocator>();vm.big_page_allocator=std::make_unique<Allocator>();
    }
''' + '\n'.join(bodies.values()) + '\n' + free_mapping + r'''
};
'''

tests = r'''
void HardRequire(bool ok,const char* message) {
    if(!ok) {std::cerr<<"FAIL "<<message<<"\n";std::cerr.flush();std::_Exit(91);}
}
constexpr u64 Address=0x100000;
// Variants: sparse allocate, free with child, remap mapped/sparse, map dynamic/fixed/remap,
// ordinary/sparse unmap, non-sparse allocate.
void Prepare(AddressSpace& as,int variant) {
    as.allocation_map[Address]={{},65536,65536,true,true};
    auto m=std::make_shared<AddressSpace::Mapping>(1,0x400000,Address,65536,false,true,variant==8);
    as.mapping_map[Address]=m;
    if(variant==1)as.allocation_map[Address].mappings.push_back(m);
    as.gmmu->pages[Address]=(variant==2 || (variant>=4 && variant<=6))
        ?Tegra::MemoryManager::EntryType::Reserved:Tegra::MemoryManager::EntryType::Mapped;
}
NvResult Invoke(AddressSpace& as,int variant) {
    if(variant==0 || variant==9) {
        AddressSpace::IoctlAllocSpace p{};p.pages=1;p.page_size=65536;
        p.flags=variant==0?MappingFlags::Sparse:MappingFlags::None;p.offset=0;return as.AllocateSpace(p);
    }
    if(variant==1) {AddressSpace::IoctlFreeSpace p{Address,1,65536};return as.FreeSpace(p);}
    if(variant==2 || variant==3) {
        AddressSpace::IoctlRemapEntry p{};p.handle=variant==2?1:0;p.as_offset_big_pages=16;p.big_pages=1;
        return as.Remap(std::span{&p,1});
    }
    if(variant>=4 && variant<=6) {
        AddressSpace::IoctlMapBufferEx p{};p.handle=1;p.mapping_size=65536;p.offset=Address;
        p.flags=variant==5?MappingFlags::Fixed:variant==6?MappingFlags::Remap:MappingFlags::None;
        return as.MapBufferEx(p);
    }
    AddressSpace::IoctlUnmapBuffer p{Address};return as.UnmapBuffer(p);
}
auto Snapshot(AddressSpace& as) {
    std::vector<std::tuple<u64,u32,DAddr,u64,u64,bool,bool,bool>> mappings;
    for(const auto& [offset,m]:as.mapping_map)
        mappings.emplace_back(offset,m->handle,m->ptr,m->offset,m->size,m->fixed,m->big_page,m->sparse_alloc);
    std::vector<std::tuple<u64,u64,u32,bool,bool,std::vector<u64>>> allocations;
    for(const auto& [offset,a]:as.allocation_map) {
        std::vector<u64> children;for(const auto& child:a.mappings)children.push_back(child->offset);
        allocations.emplace_back(offset,a.size,a.page_size,a.sparse,a.big_pages,std::move(children));
    }
    return std::tuple{as.gmmu->generation,as.gmmu->pages,as.nvmap.lookups,as.nvmap.pins,as.nvmap.unpins,
        as.vm.small_page_allocator->allocations,as.vm.small_page_allocator->frees,
        as.vm.big_page_allocator->allocations,as.vm.big_page_allocator->frees,mappings,allocations};
}
void Closed() {
    for(int variant=0;variant<10;++variant) {
        Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
        AddressSpace as{system,raster};Prepare(as,variant);auto before=Snapshot(as);gpu.NotifyShutdown();
        auto result=Invoke(as,variant);
        HardRequire(result==NvResult::InvalidState,"closed NV mapping reported success");
        HardRequire(Snapshot(as)==before,"closed NV mapping changed ownership or page tables");
    }
    std::cout<<"PASS terminal NV mapping refusal before allocator, pin, callback or metadata effects\n";
}
void Healthy() {
    for(int variant=0;variant<10;++variant) {
        Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
        AddressSpace as{system,raster};Prepare(as,variant);auto before=Snapshot(as);
        Require(Invoke(as,variant)==NvResult::Success,"healthy NV mapping refused");
        Require(Snapshot(as)!=before,"healthy NV mapping lost effects");
    }
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
    AddressSpace as{system,raster};as.vm.initialised=false;
    for(int v=0;v<10;++v)Require(Invoke(as,v)==NvResult::BadValue,"healthy invalid VM result changed");
    std::cout<<"PASS healthy sparse, ordinary, fixed, dynamic and remap operations retain results/effects\n";
}
void MissingFreeSpace() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
    AddressSpace as{system,raster};Prepare(as,1);const auto before=Snapshot(as);
    for(const auto& [pages,page_size]:{std::pair{1u,65536u},std::pair{0u,0u}}) {
        AddressSpace::IoctlFreeSpace params{Address+65536,pages,page_size};
        Require(as.FreeSpace(params)==NvResult::BadValue,"missing FreeSpace allocation reported success");
        Require(Snapshot(as)==before,"missing FreeSpace allocation changed owner inventory");
    }
    Require(Invoke(as,1)==NvResult::Success,"invalid FreeSpace request poisoned valid owner");
    std::cout<<"PASS missing FreeSpace allocation refuses without inventory or allocator effects\n";
}
void Retain(int variant,int stage) {
    Host host;VideoCore::Rasterizer raster;auto gpu=std::make_unique<Impl>(host);
    gpu->renderer->rasterizer=&raster;PublicGpu facade{gpu.get(),{}};System system{facade};
    AddressSpace as{system,raster};Prepare(as,variant);Event entered,release,stopped;
    auto block=[&] {entered.Set();release.Wait();};
    if(stage==0)facade.acquired_hook=block;
    if(stage==1)as.gmmu->rasterizer->mapping_hook=block;
    if(stage==2)as.nvmap.hook=block;
    if(stage==3)as.ack_hook=block;
    gpu->gpu_thread.stopped=&stopped;
    auto operation=std::async(std::launch::async,[&] {return Invoke(as,variant);});entered.Wait();
    auto destroy=std::async(std::launch::async,[gpu=std::move(gpu)]() mutable {gpu.reset();});stopped.Wait();
    HardRequire(destroy.wait_for(100ms)!=std::future_status::ready,"teardown escaped admitted NV mapping");
    release.Set();Require(Ready(operation)==NvResult::Success,"entered mapping lost completion");Ready(destroy);
    Require(raster.retired,"renderer retirement did not complete");
}
void LockWait() {
    Host host;VideoCore::Rasterizer raster;auto gpu=std::make_unique<Impl>(host);
    PublicGpu facade{gpu.get(),{}};System system{facade};AddressSpace as{system,raster};Prepare(as,2);
    Event entered,release;facade.acquired_hook=[&] {entered.Set();release.Wait();};
    std::unique_lock service_lock{as.mutex};auto before=Snapshot(as);
    auto operation=std::async(std::launch::async,[&] {return Invoke(as,2);});entered.Wait();release.Set();
    HardRequire(operation.wait_for(100ms)!=std::future_status::ready,"Remap bypassed AS serialization");
    gpu->NotifyShutdown();auto join=std::async(std::launch::async,[&] {gpu->owner_publications.Join();});
    HardRequire(join.wait_for(100ms)!=std::future_status::ready,"mapping lock wait escaped terminal Entry");
    Require(Snapshot(as)==before,"mapping mutated under another AS operation");
    service_lock.unlock();Require(Ready(operation)==NvResult::Success,"admitted lock waiter lost operation");Ready(join);
}

void ExceptionsAndBatch() {
    for(int variant:{0,1,2,4,7}) {
        Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
        AddressSpace as{system,raster};Prepare(as,variant);
        auto fail=[] {throw std::runtime_error("injected callback failure");};
        if(variant==0)raster.mapping_hook=fail;else as.nvmap.hook=fail;
        bool failed=false;try {failed=Invoke(as,variant)==NvResult::InvalidState;}catch(const std::runtime_error&) {failed=true;}
        Require(failed,"injected mapping exception did not execute");
        raster.mapping_hook={};as.nvmap.hook={};gpu.NotifyShutdown();
        auto joined=std::async(std::launch::async,[&] {gpu.owner_publications.Join();});Ready(joined);
        // This proves guard release, not rollback of earlier allocator/pin effects.
    }
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
    AddressSpace as{system,raster};Prepare(as,2);as.allocation_map[Address].size=131072;
    as.gmmu->pages[Address+65536]=Tegra::MemoryManager::EntryType::Reserved;
    std::array<AddressSpace::IoctlRemapEntry,2> entries{};
    for(size_t i=0;i<entries.size();++i) {entries[i].handle=1;entries[i].as_offset_big_pages=16+u32(i);entries[i].big_pages=1;}
    Event first,release;int callbacks=0;
    raster.mapping_hook=[&] {if(++callbacks==1) {first.Set();release.Wait();}};
    auto operation=std::async(std::launch::async,[&] {return as.Remap(entries);});first.Wait();gpu.NotifyShutdown();
    auto newcomer=std::async(std::launch::async,[&] {return Invoke(as,9);});
    Require(Ready(newcomer)==NvResult::InvalidState,"closed mapping waited behind an active AS operation");
    auto join=std::async(std::launch::async,[&] {gpu.owner_publications.Join();});
    HardRequire(join.wait_for(100ms)!=std::future_status::ready,"mapping batch released terminal Entry early");
    release.Set();Require(Ready(operation)==NvResult::Success,"entered remap batch stopped between entries");Ready(join);
    Require(callbacks==2 && as.gmmu->generation==2 && as.nvmap.pins==2,"entered remap batch lost second mapping");
    raster.mapping_hook={};
    std::cout<<"PASS exception guard release and admitted multi-entry remap across Close\n";
}

int main(int argc,char** argv) {
    try {
        Closed();if(argc>1 && std::string{argv[1]}=="closed")return 0;
        Healthy();MissingFreeSpace();for(int v=0;v<10;++v) {Retain(v,0);Retain(v,3);}
        for(int v:{0,1,2,3,4,5,6,7,8})Retain(v,1);
        for(int v:{1,2,4,5,7,8})Retain(v,2);
        LockWait();ExceptionsAndBatch();std::cout<<"PASS retained NV mapping through entry, locks, backend callbacks, pins and acknowledgement\n";
    }catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
'''
source = '#include \"video_core/unmap_failure.h\"\n#include <array>\n#include <bit>\n#include <map>\n#include <span>\n#include <tuple>\n#include <cstdlib>\n#include "common/alignment.h"\n#include "common/swap.h"\n' + base + fixture + tests
flags = [a.cxx, '-std=c++23', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src')]
flags += [str(root/'src/common/terminal_fail_stop.cpp')]


def compile_source(name, text):
    cpp = out / (name + '.cpp')
    cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe


if a.emit_only:
    (out / 'mapping.cpp').write_text(source)
    print('Emitted actual NV/GMMU mapping fixture')
    raise SystemExit(0)
exe = compile_source('mapping', source)
for i in range(a.repeat):
    result = subprocess.run([str(exe), a.case], capture_output=True, text=True, timeout=45)
    log = result.stdout + result.stderr
    (out / f'positive-{i}.log').write_text(log)
    assert result.returncode == 0 and 'PASS terminal NV mapping refusal' in log, log
    if a.case == 'all':
        assert 'PASS retained NV mapping through entry' in log, log
print(f'PASS actual NV/GMMU mapping lifecycle ({a.repeat} repetitions)', flush=True)
controls = []
refusal = '    if (!publication) {\n        return NvResult::InvalidState;\n    }'
if a.case == 'all':
    for name in names:
        body = bodies[name]
        assert body.count(refusal) == 1
        controls.append((name + '-ignored-refusal', source.replace(body, body.replace(refusal, '')),
                         'closed NV mapping reported success'))
        controls.append((name + '-early-release', source.replace(body, body.replace(
            '    if(ack_hook) ack_hook();', '    publication.reset();\n    if(ack_hook) ack_hook();')),
                         'teardown escaped admitted NV mapping'))
    remap = bodies['Remap']
    controls += [
        ('FreeSpace-inserting-lookup', source.replace(bodies['FreeSpace'], bodies['FreeSpace'].replace(
            'allocation_map.at(params.offset)', 'allocation_map[params.offset]')),
         'missing FreeSpace allocation changed owner inventory'),
        ('Remap-missing-lock', source.replace(remap, remap.replace('    std::scoped_lock lock(mutex);', '')),
         'Remap bypassed AS serialization'),
        ('missing-owner-join', source.replace('        owner_publications.Join();', ''),
         'teardown escaped admitted NV mapping'),
    ]
selected = [c for c in controls if not a.control_prefix or any(c[0].startswith(x) for x in a.control_prefix)]
for name, changed, expected in selected:
    assert changed != source
    mutant = compile_source(name, changed)
    result = subprocess.run([str(mutant), 'all'], capture_output=True, text=True, timeout=45)
    log = result.stdout + result.stderr
    (out / (name + '.log')).write_text(log)
    assert result.returncode != 0 and 'FAIL ' + expected in log, (name, log)
    print('PASS NV mapping control: ' + name, flush=True)
paths = ['src/video_core/gpu.cpp', 'src/video_core/gpu.h', 'src/video_core/memory_manager.cpp',
         'src/video_core/memory_manager.h', 'src/video_core/rasterizer_binding_state.h',
         'src/common/terminal_ingress.h', 'src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp',
         'src/core/hle/service/nvdrv/devices/nvhost_as_gpu.h',
         'tools/gpu_lifecycle_tests/run_tests.py', 'tools/gpu_lifecycle_tests/run_address_space_mapping_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in paths},
    'positive_repeats': a.repeat, 'case': a.case, 'executed_controls': [c[0] for c in selected],
    'generated_source_sha256': hashlib.sha256(source.encode()).hexdigest(),
    'actual': ['GPU destructor, NotifyShutdown, publication Entry and public forwarding',
               'five NV mapping methods and FreeMappingLocked', 'NV Mapping/Allocation/VM/ioctl layouts',
               'GMMU Map, MapSparse, Unmap and NotifyMappingChange bodies', 'TerminalIngress and detached binding state'],
    'fixtures': ['System and service facade lifetime', 'renderer/worker', 'allocator/nvmap',
                 'page-table data/operations and submapped-range walk'],
    'boundary': 'NV owned mapping calls through synchronous callback return, metadata and acknowledgement only. Direct GMMU operations, scalar/block/unsafe memory and escaped pointers, backing pins, native completion and reversible guest exclusion remain unproved; admission Unknown.',
}, indent=2), encoding='utf-8')
