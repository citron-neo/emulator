# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual GMMU/NV unmap failure propagation, retained resources and partial prefixes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--case', choices=['all','pin','cleanup','maprace'], default='all')
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-unmap-ownership').resolve()
out.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root / 'tools/gpu_lifecycle_tests/run_address_space_mapping_tests.py'),
                '--emit-only', '--repeat', str(a.repeat), '--cxx', a.cxx,
                '--output-dir', str(out / 'mapping')], check=True)


def method(text, marker):
    start = text.index(marker);pos = text.index('{', start);depth, end = 1, pos + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}');end += 1
    return text[start:end]


source = (out / 'mapping/mapping.cpp').read_text()
source = source.replace(method(source, 'int main('), '')
nvmap = (root / 'src/core/hle/service/nvdrv/core/nvmap.cpp').read_text()
unmap_handle = method(nvmap, 'bool NvMap::UnmapHandle(').replace('NvMap::', '')
unpin_handle = method(nvmap, 'void NvMap::UnpinHandle(').replace('NvMap::', '')
pin_handle = method(nvmap, 'DAddr NvMap::PinHandle(').replace('NvMap::', '')
free_handle = method(nvmap, 'std::optional<NvMap::FreeInfo> NvMap::FreeHandle(').replace('NvMap::', '')
fixture = r'''
#define LOG_CRITICAL(...) ((void)0)
namespace NvCore {struct SessionId {int id=0;};}
struct HandleMap {
    struct Handle {
        using Id=u32;std::mutex mutex;int dupes=1,internal_dupes=0;int pins=1;
        u32 id=1,pin_virt_address=0x100000;u64 aligned_size=65536,d_address=0x400000,address=0x800000,size=65536;
        bool in_heap=false;NvCore::SessionId session_id;
        struct {struct {int Value() {return 0;}} map_uncached;} flags;
        std::optional<std::list<std::shared_ptr<Handle>>::iterator> unmap_queue_entry;
    };
    struct FreeInfo {u64 address=0,size=0;bool was_uncached=false,can_unlock=false;};
    struct Host {
        Tegra::MemoryManager& gmmu;int frees=0,smmu_unmaps=0,smmu_frees=0,shutdowns=0;
        std::function<void()> shutdown_hook;
        explicit Host(Tegra::MemoryManager& memory):gmmu{memory} {}
        auto& GMMU() {return gmmu;}auto& Allocator() {return *this;}auto& MemoryManager() {return *this;}
        auto& System() {return *this;}auto& GPU() {return *this;}
        bool IsPoweredOn() {return false;}bool IsAsync() {return true;}void SynchronizeGPUThread() {}
        void NotifyShutdown() {if(shutdown_hook)shutdown_hook();++shutdowns;}
        std::function<void()> allocate_hook;
        u32 Allocate(u32) {if(allocate_hook)allocate_hook();return 16;}u64 Allocate(size_t) {return 0;}
        void Map(u64,u64,size_t,int,bool) {}
        void Free(u32,u32) {++frees;}void Free(u64,size_t) {++smmu_frees;}
        void Unmap(u64,size_t) {++smmu_unmaps;}
    } host1x;
    struct Core {
        struct Mapper {void Unmap(u64,size_t) {} bool IsInBounds(u64,size_t) {return false;}u64 Map(u64,size_t) {return 0;}} mapper;
        struct Session {Mapper* mapper;bool has_preallocated_area=false;int asid=0;} session{&mapper};
        Session* GetSession(NvCore::SessionId) {return &session;}
    } core;
    struct Queue : std::list<std::shared_ptr<Handle>> {
        bool fail_push=false;
        void push_back(std::shared_ptr<Handle> value) {
            if(fail_push)throw std::bad_alloc();
            std::list<std::shared_ptr<Handle>>::push_back(std::move(value));
        }
    } unmap_queue;
    std::map<u32,std::shared_ptr<Handle>> handles;
    std::mutex unmap_queue_lock;
    static constexpr size_t BIG_PAGE_SIZE=65536;
    explicit HandleMap(Tegra::MemoryManager& memory):host1x{memory} {}
    auto GetHandle(u32 id) {auto it=handles.find(id);return it==handles.end()?std::shared_ptr<Handle>{}:it->second;}
    bool TryRemoveHandle(const Handle& handle) {
        if(handle.dupes==0 && handle.internal_dupes==0) {handles.erase(handle.id);return true;}return false;
    }
''' + unmap_handle + '\n' + pin_handle + '\n' + unpin_handle + '\n' + free_handle + r'''
};
using VideoCommon::CallbackOutcome;
void LocalFailure(CallbackOutcome outcome,bool free_space) {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
    AddressSpace as{system,raster};const int variant=free_space?1:7;Prepare(as,variant);
    const auto before=Snapshot(as);
    PublicGpu::shutdown_observer=[&] {
        auto locks=std::async(std::launch::async,[&] {if(!as.mutex.try_lock())return false;as.mutex.unlock();return true;});
        HardRequire(Ready(locks),"NV shutdown destroyed callbacks under AS lock");
    };
    raster.mapping_hook=[&] {throw VideoCommon::UnmapFailure{outcome,outcome==CallbackOutcome::Failed};};
    Require(Invoke(as,variant)==NvResult::InvalidState,"NV unmap failure reported service success");
    PublicGpu::shutdown_observer={};
    HardRequire(as.vm.big_page_allocator->frees==0 && as.nvmap.unpins==0 &&
                as.mapping_map.size()==1 && as.allocation_map.size()==1,
                "failed unmap released mapping, allocator or pin owners");
    Require(as.gmmu->HasUnmapFailure() && as.gmmu->generation==0 && as.gmmu->page_stash.empty(),
            "failed unmap committed PTEs or lost quarantine");
    Require(Snapshot(as)==before,"failed first unmap changed service ownership");
    raster.mapping_hook={};for(int v=0;v<10;++v)
        Require(Invoke(as,v)==NvResult::InvalidState,"failed address space admitted a new mapping");
    bool refused=false;try {as.gmmu->MapSparse(Address,65536);}catch(const VideoCommon::UnmapFailure&) {refused=true;}
    HardRequire(refused,"quarantined direct GMMU mapping reopened");
}
void PhysicalPrefix() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
    AddressSpace as{system,raster};Prepare(as,7);as.gmmu->subranges={{0x400000,4096},{0x500000,4096}};
    int callbacks=0;raster.mapping_hook=[&] {if(++callbacks==2)throw VideoCommon::UnmapFailure{CallbackOutcome::Cancelled,false};};
    bool caught=false;try {as.gmmu->Unmap(Address,65536);}catch(const VideoCommon::UnmapFailure& failure) {
        caught=failure.Outcome()==CallbackOutcome::Cancelled && failure.PotentialEffects();
    }
    HardRequire(caught,"cancelled physical suffix hid prior callback effects");
    Require(callbacks==2 && as.gmmu->generation==0 && as.gmmu->HasUnmapFailure(),"partial physical unmap committed or lost prefix");
}
void BatchPrefix() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
    AddressSpace as{system,raster};Prepare(as,1);as.allocation_map[Address].size=131072;
    auto second=std::make_shared<AddressSpace::Mapping>(1,0x500000,Address+65536,65536,false,true,false);
    as.allocation_map[Address].mappings.push_back(second);as.mapping_map[Address+65536]=second;
    as.gmmu->pages[Address+65536]=Tegra::MemoryManager::EntryType::Mapped;
    raster.mapping_hook=[&] {
        if(as.gmmu->generation!=0)throw VideoCommon::UnmapFailure{CallbackOutcome::Cancelled,false};
    };
    AddressSpace::IoctlFreeSpace params{Address,2,65536};
    Require(as.FreeSpace(params)==NvResult::InvalidState,"partial free batch reported success");
    HardRequire(as.mapping_map.size()==1 && as.mapping_map.contains(Address+65536) && as.nvmap.unpins==1 &&
                as.vm.big_page_allocator->frees==1 && as.allocation_map.size()==1,
                "partial free batch released failed suffix or allocation");
    Require(as.gmmu->HasUnmapFailure(),"partial free batch lost terminal quarantine");
}
void LowArea(bool success) {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
    AddressSpace as{system,raster};Prepare(as,7);HandleMap map{*as.gmmu};
    auto handle=std::make_shared<HandleMap::Handle>();map.handles[1]=handle;
    map.unmap_queue.push_back(handle);handle->unmap_queue_entry=map.unmap_queue.begin();
    map.host1x.shutdown_hook=[&] {
        auto locks=std::async(std::launch::async,[&] {
            std::unique_lock one{handle->mutex,std::try_to_lock};std::unique_lock two{map.unmap_queue_lock,std::try_to_lock};
            return one.owns_lock() && two.owns_lock();
        });HardRequire(Ready(locks),"low-area shutdown destroyed callbacks under handle locks");
    };
    if(!success)raster.mapping_hook=[] {throw VideoCommon::UnmapFailure{CallbackOutcome::Cancelled,false};};
    auto result=map.FreeHandle(1,false);
    if(success) {
        Require(result.has_value() && map.handles.empty() && map.unmap_queue.empty() &&
                map.host1x.frees==1 && map.host1x.smmu_unmaps==1 && map.host1x.smmu_frees==1,
                "healthy low-area free lost release effects");
    } else {
        HardRequire(!result && map.handles.contains(1) && map.unmap_queue.size()==1 && handle->dupes==1 && handle->pins==1 &&
                    handle->pin_virt_address!=0 && handle->d_address!=0 && map.host1x.frees==0 &&
                    map.host1x.smmu_unmaps==0 && map.host1x.shutdowns==1,
                    "failed low-area free discarded retained owners");
    }
}

void LowAreaReclaim() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
    AddressSpace as{system,raster};Prepare(as,7);HandleMap map{*as.gmmu};
    auto old=std::make_shared<HandleMap::Handle>();old->pins=0;map.handles[1]=old;
    auto target=std::make_shared<HandleMap::Handle>();target->id=2;target->pins=0;target->pin_virt_address=0;target->d_address=0;map.handles[2]=target;
    map.unmap_queue.push_back(old);old->unmap_queue_entry=map.unmap_queue.begin();
    raster.mapping_hook=[] {throw VideoCommon::UnmapFailure{CallbackOutcome::Cancelled,false};};
    map.host1x.shutdown_hook=[&] {
        auto locks=std::async(std::launch::async,[&] {
            std::unique_lock one{old->mutex,std::try_to_lock},two{target->mutex,std::try_to_lock},three{map.unmap_queue_lock,std::try_to_lock};
            return one.owns_lock() && two.owns_lock() && three.owns_lock();
        });HardRequire(Ready(locks),"reclaim shutdown destroyed callbacks under nested handle locks");
    };
    HardRequire(map.PinHandle(2,false)==0 && map.unmap_queue.size()==1 && old->d_address!=0 &&
                old->pin_virt_address!=0 && target->pins==0 && target->d_address==0 && map.host1x.shutdowns==1,
                "failed reclaim released owners or pinned a new handle");
}


void PinQueueFailure() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
    AddressSpace as{system,raster};Prepare(as,7);HandleMap map{*as.gmmu};
    auto handle=std::make_shared<HandleMap::Handle>();map.handles[1]=handle;map.unmap_queue.fail_push=true;
    bool threw=false;try {map.UnpinHandle(1);}catch(const std::bad_alloc&) {threw=true;}
    HardRequire(threw && handle->pins==1 && map.unmap_queue.empty(),"failed queue allocation dropped last pin");
    map.unmap_queue.fail_push=false;map.UnpinHandle(1);
    Require(handle->pins==0 && map.unmap_queue.size()==1,"healthy last unpin lost queue ownership");
}
void CleanupFailure() {
    for(int variant:{1,7}) {
        Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
        AddressSpace as{system,raster};Prepare(as,variant);
        as.nvmap.hook=[] {throw std::bad_alloc();};
        bool failed=false;try {failed=Invoke(as,variant)==NvResult::InvalidState;}catch(...) {}
        HardRequire(failed && as.gmmu->HasUnmapFailure() && as.mapping_map.size()==1 && as.allocation_map.size()==1,
                    "post-callback cleanup failure escaped quarantine");
    }
}


void LowAreaMapRace() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};PublicGpu facade{&gpu,{}};System system{facade};
    AddressSpace as{system,raster};Prepare(as,7);HandleMap map{*as.gmmu};
    auto handle=std::make_shared<HandleMap::Handle>();handle->pin_virt_address=0;map.handles[1]=handle;
    map.host1x.allocate_hook=[&] {as.gmmu->unmap_failed=true;};
    bool refused=false;try {refused=map.PinHandle(1,true)==0;}catch(const VideoCommon::UnmapFailure&) {}
    HardRequire(refused && handle->pins==1 && handle->pin_virt_address==0 && as.gmmu->generation==0,
                "quarantine race escaped low-area pin");
}

int main(int argc,char** argv) {
    if(argc>1 && std::string{argv[1]}=="maprace") {LowAreaMapRace();std::cout<<"PASS checked unmap ownership: isolated low-area race\n";return 0;}
    if(argc>1 && std::string{argv[1]}=="pin") {PinQueueFailure();std::cout<<"PASS checked unmap ownership: isolated pin allocation\n";return 0;}
    if(argc>1 && std::string{argv[1]}=="cleanup") {CleanupFailure();std::cout<<"PASS checked unmap ownership: isolated cleanup failure\n";return 0;}
    try {
        for(auto outcome:{CallbackOutcome::Cancelled,CallbackOutcome::Failed}) {LocalFailure(outcome,false);LocalFailure(outcome,true);}
        PhysicalPrefix();BatchPrefix();LowArea(false);LowArea(true);LowAreaReclaim();PinQueueFailure();CleanupFailure();LowAreaMapRace();
        std::cout<<"PASS checked unmap ownership: NV failure, physical/batch prefixes, GMMU quarantine, low-area handle/queue retention\n";
    }catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}
'''
source += '\n' + fixture
flags = [a.cxx, '-std=c++23', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src')]


def compile_source(name, text):
    cpp = out / (name + '.cpp');cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe


exe = compile_source('ownership', source)
for i in range(a.repeat):
    result = subprocess.run([str(exe), a.case], capture_output=True, text=True, timeout=40)
    log = result.stdout + result.stderr;(out / f'positive-{i}.log').write_text(log)
    assert result.returncode == 0 and 'PASS checked unmap ownership:' in log, log
print(f'PASS checked unmap ownership ({a.repeat} repetitions)', flush=True)
controls = [
    ('low-area-ignored-map-refusal', source.replace('if (!map_low_area()) return 0;', '(void)map_low_area();'),
     'quarantine race escaped low-area pin'),
    ('cleanup-missing-quarantine', source.replace('        gmmu->QuarantineUnmap();', ''),
     'post-callback cleanup failure escaped quarantine'),
    ('last-pin-before-queue', source.replace('        unmap_queue.push_back(handle_description);',
     '        --handle_description->pins;\n        unmap_queue.push_back(handle_description);'),
     'failed queue allocation dropped last pin'),
    ('NV-cancel-under-lock', source.replace('        lock.unlock();', ''),
     'NV shutdown destroyed callbacks under AS lock'),
    ('low-area-cancel-under-lock', source.replace('                        queueLock.unlock();', ''),
     'low-area shutdown destroyed callbacks under handle locks'),
    ('missing-quarantine', source.replace('        unmap_failed.store(true, std::memory_order_release);', ''),
     'failed unmap committed PTEs or lost quarantine'),
    ('lost-prefix-effects', source.replace('failure.PotentialEffects() || completed_ranges != 0',
     '(static_cast<void>(completed_ranges), failure.PotentialEffects())'), 'cancelled physical suffix hid prior callback effects'),
    ('reopened-direct-mapping', source.replace('    if (HasUnmapFailure()) {', '    if (false && HasUnmapFailure()) {'),
     'quarantined direct GMMU mapping reopened'),
    ('low-area-lost-duplicate', source.replace('                        ++handle_description->dupes;', '                        /* omitted restore */;'),
     'failed low-area free discarded retained owners'),
]
if a.case != 'all':
    controls = [
    ('low-area-ignored-map-refusal', source.replace('if (!map_low_area()) return 0;', '(void)map_low_area();'),
     'quarantine race escaped low-area pin'),
    ('cleanup-missing-quarantine', source.replace('        gmmu->QuarantineUnmap();', ''),
     'post-callback cleanup failure escaped quarantine'),
    ('last-pin-before-queue', source.replace('        unmap_queue.push_back(handle_description);',
     '        --handle_description->pins;\n        unmap_queue.push_back(handle_description);'),
     'failed queue allocation dropped last pin'),]
for name, changed, expected in controls:
    assert changed != source
    mutant = compile_source(name, changed)
    result = subprocess.run([str(mutant)], capture_output=True, text=True, timeout=40)
    log = result.stdout + result.stderr;(out / (name + '.log')).write_text(log)
    assert result.returncode != 0 and 'FAIL ' + expected in log, (name, log)
    print('PASS unmap ownership control: '+name, flush=True)
paths = ['src/video_core/memory_manager.cpp', 'src/video_core/memory_manager.h', 'src/video_core/unmap_failure.h',
         'src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp', 'src/core/hle/service/nvdrv/devices/nvhost_as_gpu.h',
         'src/core/hle/service/nvdrv/core/nvmap.cpp', 'src/core/hle/service/nvdrv/core/nvmap.h',
         'tools/gpu_lifecycle_tests/run_address_space_mapping_tests.py', 'tools/gpu_lifecycle_tests/run_unmap_ownership_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in paths},
    'positive_repeats': a.repeat, 'executed_controls': [c[0] for c in controls],
    'actual': ['GMMU mapping/Unmap/callback bodies', 'five NV mapping methods and FreeMappingLocked',
               'NvMap UnmapHandle and FreeHandle bodies'],
    'fixtures': ['renderer failures, NV pins/allocator, GMMU table operations', 'low-area handle/System/SMMU bookkeeping'],
    'boundary': 'Retained remaining owners and terminal mapping quarantine, not rollback, complete producer coverage or native completion.'
}, indent=2), encoding='utf-8')
