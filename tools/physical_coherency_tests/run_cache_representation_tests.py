# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose generated actual GPU/device/HostMemory and actual Vulkan payload fixture inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--boost-include',type=Path)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
out=root/'.diagnostics/m3b-cache-representation'
out.mkdir(parents=True,exist_ok=True)
gpu_dir=root/'.diagnostics/m3b-physical-pin'
pin_dir=root/'.diagnostics/m3b-representation-pin'
# The workflow runs both generating suites immediately before this test. Reject stale inputs.
for path in [gpu_dir/'gpu_plan_extraction.json',pin_dir/'extraction.json']:
    for name,digest in json.loads(path.read_text())['sha256'].items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:
            raise RuntimeError('Stale generated fixture; rerun GPU-plan and representation-pin suites: '+name)
gpu=(gpu_dir/'gpu_plan.cpp').read_text().split('int main() {')[0]
pin=(pin_dir/'representation_pin.cpp').read_text().split('int main() {')[0]
pin=pin.replace('namespace Common {struct GuestBackingRange {};}','')
base=(root/'src/video_core/buffer_cache/buffer_cache_base.h').read_text()
cache=(root/'src/video_core/buffer_cache/buffer_cache.h').read_text()
def member(marker):
    start=base.index(marker)
    opening=base.index('{',start)
    depth=1;end=opening+1
    while depth:
        depth+=(base[end]=='{')-(base[end]=='}');end+=1
    return base[start:end]+'\n'
start=cache.index('template <class P>\ntemplate <typename B>\nauto BufferCache<P>::PinExistingDmaRepresentation(')
method=cache[start:cache.index('\n}',start)+2]
checkpoint='    const bool current = device_memory.ReadPhysicalMappingSnapshot('
if method.count(checkpoint)!=1:
    raise RuntimeError('Cannot instrument post-resource-pin mapping checkpoint')
method=method.replace(checkpoint,'    ObserveRepresentationReady();\n'+checkpoint)
shell=r'''
namespace VideoCommon {
using BufferId=Common::SlotId;
constexpr BufferId NULL_BUFFER_ID{0};
std::function<void()> representation_ready;
void ObserveRepresentationReady() {if(representation_ready) representation_ready();}
struct Policy {using Buffer=Vulkan::Buffer;};
template<class P> class BufferCache {
public:
    using Buffer=typename P::Buffer;
    static constexpr u32 CACHING_PAGEBITS=16;
    static constexpr u64 CACHING_PAGE_COUNT=1ULL<<12;
    static constexpr u64 CACHING_ADDRESS_SPACE_SIZE=CACHING_PAGE_COUNT<<CACHING_PAGEBITS;
    DeviceFixture& device_memory;
    Common::SlotVector<Buffer> slot_buffers;
    std::vector<u32> page_table=std::vector<u32>(CACHING_PAGE_COUNT);
    bool scatter_busy=false;
    struct Gather {bool scatter_pending=false;};
    std::unordered_map<u64,Gather> gather_buffers;
    BufferCache(DeviceFixture& device,Vulkan::BufferCacheRuntime& runtime):device_memory{device} {
        const auto null=slot_buffers.insert(runtime,NullBufferParams{});assert(null==NULL_BUFFER_ID);
    }
    template<typename B=Buffer>
    auto PinExistingDmaRepresentation(DAddr,const Tegra::PhysicalAccessPlan&)
        -> decltype(std::declval<const B&>().PinStagedRepresentation(u64{},u64{}));
'''+member('[[nodiscard]] static constexpr bool IsRegionPageTableAddressable(')+member(
    '[[nodiscard]] BufferId GetPageBufferId(')+'};\n'+method+'\n}\n'
tail=r'''
int main() {
    constexpr u64 P=4096,C=1ULL<<16,G=0x10000,D=0x20000;
    DeviceFixture device;Tegra::MemoryManager memory{device};
    device.Map(D,0,2*C);memory.Map(G,D,2*C);
    Vulkan::BufferCacheRuntime runtime;
    VideoCommon::BufferCache<VideoCommon::Policy> cache{device,runtime};
    const auto id=cache.slot_buffers.insert(runtime,D,2*C);
    cache.page_table[D>>16]=id.index+1;cache.page_table[(D>>16)+1]=id.index+1;
    auto plan=memory.PinPhysicalRanges(G+10,P+10);assert(plan);
    auto pin=cache.PinExistingDmaRepresentation(D+10,*plan);assert(pin);
    assert(pin->Offset()==10 && pin->Size()==P+10);
    const auto handle=pin->Handle();const auto identity=pin->Identity();
    // Bounds/cache membership, not allocation or content synchronization.
    assert(!cache.PinExistingDmaRepresentation(0,*plan));
    assert(!cache.PinExistingDmaRepresentation(D+2*C-1,*plan));
    cache.scatter_busy=true;assert(!cache.PinExistingDmaRepresentation(D+10,*plan));cache.scatter_busy=false;
    cache.gather_buffers[0].scatter_pending=true;
    assert(!cache.PinExistingDmaRepresentation(D+10,*plan));cache.gather_buffers.clear();
    device.Map(D,P);
    if(cache.PinExistingDmaRepresentation(D+10,*plan)) {
        std::cerr<<"mismatched physical representation accepted\n";return 44;
    }
    device.Map(D,0);
    // Noncontiguous PTE within an extent must be checked, not just the first page.
    device.Map(D+P,8*P);assert(!cache.PinExistingDmaRepresentation(D+10,*plan));device.Map(D+P,P);
    // Device remap AFTER resource acquisition, before final generation validation.
    VideoCommon::representation_ready=[&]{device.Map(D,4*P);};
    if(cache.PinExistingDmaRepresentation(D+10,*plan)) {
        std::cerr<<"post-pin remap accepted\n";return 45;
    }
    VideoCommon::representation_ready={};device.Map(D,0);
    // A GVA remap does not redirect old storage association to the new target.
    memory.Unmap(G,P);memory.Map(G,D+P,P);
    auto old=cache.PinExistingDmaRepresentation(D+10,*plan);
    assert(old && old->Identity()==identity && old->Handle()==handle);
    memory.Unmap(G,P);memory.Map(G,D,P);
    auto large=memory.PinPhysicalRanges(G,2*C);assert(large);
    cache.page_table[(D>>16)+1]=0;
    assert(!cache.PinExistingDmaRepresentation(D,*large));
    cache.page_table[(D>>16)+1]=id.index+1;
    auto moved=std::move(*large);
    assert(!cache.PinExistingDmaRepresentation(D,*large)); // moved-from plan must not match
    assert(cache.PinExistingDmaRepresentation(D,moved));
    DeviceFixture other;Tegra::MemoryManager other_memory{other};
    other.Map(D,0);other_memory.Map(G,D,P);
    auto foreign=other_memory.PinPhysicalRanges(G,1);assert(foreign);
    assert(!cache.PinExistingDmaRepresentation(D,*foreign));
    // Actual SlotVector/ring and Vulkan owners retain old payload through cache removal/reuse.
    VideoCommon::DelayedDestructionRing<Vulkan::Buffer,8> ring;
    ring.Push(std::move(cache.slot_buffers[id]));cache.slot_buffers.erase(id);ring.Flush();
    assert(!cache.PinExistingDmaRepresentation(D+10,*plan)); // stale page-table slot id
    const auto reused=cache.slot_buffers.insert(runtime,D,2*C);assert(reused==id);
    auto replacement=cache.PinExistingDmaRepresentation(D+10,*plan);assert(replacement);
    assert(replacement->Identity()!=identity && replacement->Handle()!=handle && !deaths[handle]);
    runtime.SealStagedPins();
    assert(!cache.PinExistingDmaRepresentation(D+10,*plan));
    assert(!pin->Handle() && !old->Handle());
    pin.reset();old.reset();replacement.reset();
    assert(deaths[handle]==1);
    std::cout<<"PASS existing-cache physical correspondence: actual device/GPU mapping, HostMemory pins, cache lookup and Vulkan payload; page walk, remap checkpoint, old targets, slot reuse, pending-scatter refusal and seal\n";
}
'''
cpp=out/'cache_representation.cpp'
cpp.write_text(gpu+pin+shell+tail)
cmd=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror',
     '-Wno-unused-parameter','-Wno-missing-field-initializers',
     '-I',str(gpu_dir/'stubs'),'-I',str(root/'src'),
     '-I',str(root/'externals/Vulkan-Headers/include')]
if a.boost_include:cmd+=['-I',str(a.boost_include)]
cmd+=[str(root/'src/common/host_memory.cpp')]
if os.name=='nt':cmd+=[str(root/'src/common/dynamic_library.cpp')]
exe=out/('cache_representation.exe' if os.name=='nt' else 'cache_representation')
subprocess.run(cmd+[str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
control=out/('cache_representation_control.exe' if os.name=='nt' else 'cache_representation_control')
for needle,replacement,exit_code,message in [
    ('!Tegra::Detail::MatchesDevicePhysicalPlanLocked(\n'
     '                    physical, device_addr, Core::DEVICE_PAGESIZE, backing, backing_size, translate)',
     'false',44,'mismatched physical representation accepted'),
    ('if (!current) {','if (false && !current) {',45,'post-pin remap accepted')]:
    if method.count(needle)!=1:raise RuntimeError('Cannot form association negative control')
    control_cpp=out/f'control_{exit_code}.cpp'
    control_cpp.write_text(gpu+pin+shell.replace(method,method.replace(needle,replacement))+tail)
    subprocess.run(cmd+[str(control_cpp),'-o',str(control)],check=True)
    result=subprocess.run([str(control)],capture_output=True,text=True,timeout=30)
    if result.returncode!=exit_code or message not in result.stderr:
        raise RuntimeError('Association negative control did not expose '+message)
    print('PASS negative control: '+message)
(out/'extraction.json').write_text(json.dumps({
    'sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in [
        'src/video_core/buffer_cache/buffer_cache.h','src/video_core/buffer_cache/buffer_cache_base.h',
        'src/video_core/physical_access_plan.h']},
    'generated_inputs': [str(gpu_dir/'gpu_plan_extraction.json'),str(pin_dir/'extraction.json')],
    'actual':['existing cache lookup/pin method and page-id validation','device physical-plan match',
              'GPU/device resolver/mutators','HostMemory','Vulkan staged ownership/Release'],
    'fixtures':['PTE and cache table storage/registration','buffer construction and VMA destroy callback',
                'post-resource-pin rendezvous','no real cache-content synchronization'],
    'not_proven':['cache content authority','immutable registration epoch','admission',
                  'device shutdown/final-pin drain','Vulkan submission/completion','F2'],
},indent=2))
