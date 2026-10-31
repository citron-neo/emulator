# SPDX-License-Identifier: GPL-2.0-or-later
# Actual channel engine construction and NV binding, with deterministic fixture engines.
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-channel-rebind').resolve()
out.mkdir(parents=True, exist_ok=True)


def method(path, marker):
    text = (root / path).read_text(encoding='utf-8')
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


channel_init = method('src/video_core/control/channel_state.cpp', 'void ChannelState::Init(')
channel_bind = method('src/video_core/control/channel_state.cpp', 'void ChannelState::BindRasterizer(')
init = method('src/video_core/gpu.cpp', '    std::optional<Common::TerminalIngress::Entry>\n    InitChannel(')
entry = method('src/video_core/gpu.cpp', '    std::optional<Common::TerminalIngress::Entry> AcquireChannelBinding()')
bind = method('src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp', 'NvResult nvhost_as_gpu::BindChannel(')
setup = method('src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp', 'NvResult nvhost_gpu::SetupGPFIFOChannel(')
# Observer immediately before taking either service lock, never replacing the lock.
bind = bind.replace('    std::shared_ptr<Tegra::MemoryManager> previous;',
                    '    if(bind_entered) bind_entered();\n    std::shared_ptr<Tegra::MemoryManager> previous;')
head = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include "common/terminal_ingress.h"
using namespace std::chrono_literals;
using u64=std::uint64_t;using u32=std::uint32_t;using s32=std::int32_t;using GPUVAddr=u64;
void Require(bool good,const char* marker) {if(!good) {std::cerr<<"FAIL "<<marker<<"\n";std::cerr.flush();std::_Exit(91);}}
template<class T> auto Ready(std::future<T>& f) {Require(f.wait_for(2s)==std::future_status::ready,"interleaving did not finish");return f.get();}
#define LOG_DEBUG(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
namespace Tegra {struct GPU;}
namespace Core {struct System {Tegra::GPU* gpu=nullptr;Tegra::GPU& GPU() {return *gpu;}};}
namespace VideoCore {struct RasterizerInterface {std::function<void()> initialize;
    template<class C> void InitializeChannel(C&) {if(initialize)initialize();}
};}
namespace Tegra {
struct MemoryManager {
    bool unmap_failed=false;
    bool HasUnmapFailure() const {return unmap_failed;}
    inline static int live=0;int id;int writes=0;
    explicit MemoryManager(int value):id{value} {++live;}~MemoryManager() {--live;}
    void BindRasterizer(VideoCore::RasterizerInterface*) {}
};
struct Engine {
    inline static int fail_at=-1,built=0;
    MemoryManager& memory;
    explicit Engine(MemoryManager& m):memory{m} {if(++built==fail_at)throw std::runtime_error("engine allocation failure");}
    void BindRasterizer(VideoCore::RasterizerInterface*) {}
};
struct DmaPusher:Engine {
    template<class C>DmaPusher(Core::System&,GPU&,MemoryManager& m,C&):Engine{m} {}
    void AcquireSemaphore(GPUVAddr,u32,bool) {}
};
namespace Engines {
struct Maxwell3D:Engine {Maxwell3D(Core::System&,MemoryManager& m):Engine{m} {}
    template<class F>void SetSemaphoreAcquire(F) {}
};
struct Fermi2D:Engine {using Engine::Engine;};
struct KeplerCompute:Engine {KeplerCompute(Core::System&,MemoryManager& m):Engine{m} {}};
struct MaxwellDMA:Engine {MaxwellDMA(Core::System&,MemoryManager& m):Engine{m} {}};
struct KeplerMemory:Engine {KeplerMemory(Core::System&,MemoryManager& m):Engine{m} {}};
}
namespace Control {
struct ChannelState {
    s32 bind_id=1;u64 program_id=0;
    // Preserve production declaration order: manager outlives its DMA pusher.
    std::unique_ptr<Engines::Maxwell3D> maxwell_3d;
    std::unique_ptr<Engines::Fermi2D> fermi_2d;
    std::unique_ptr<Engines::KeplerCompute> kepler_compute;
    std::unique_ptr<Engines::MaxwellDMA> maxwell_dma;
    std::unique_ptr<Engines::KeplerMemory> kepler_memory;
    std::shared_ptr<MemoryManager> memory_manager;
    std::unique_ptr<DmaPusher> dma_pusher;
    bool initialized=false,initialization_failed=false;
    void Init(Core::System&,GPU&,u64);
    void BindRasterizer(VideoCore::RasterizerInterface*);
};
'''
head += channel_init + '\n' + channel_bind + r'''
}
struct GPU {
    Core::System& system;GPU& gpu=*this;VideoCore::RasterizerInterface* rasterizer;
    Common::TerminalIngress owner_publications;
    GPU(Core::System& s,VideoCore::RasterizerInterface& r):system{s},rasterizer{&r} {s.gpu=this;owner_publications.Open();}
    ~GPU() {owner_publications.Close();owner_publications.Join();}
''' + init + '\n' + entry + r'''
};
}
using DeviceFD=int;
enum class NvResult {Success,InvalidState,AlreadyAllocated};
struct Fence {u32 value=0;};
struct IoctlAllocGpfifoEx {Fence fence_out;};
struct IoctlBindChannel {DeviceFD fd;};
struct Process {u64 GetProgramId() {return 77;}};
struct Session {Process* process;};
struct Container {Process process;Session session{&process};Session* GetSession(int) {return &session;}};
struct Syncpoints {Fence GetSyncpointFence(u32) {return {9};}};
struct nvhost_gpu {
    Core::System& system;Container core;Syncpoints syncpoint_manager;u32 channel_syncpoint=1;
    std::unordered_map<int,int> sessions;std::mutex channel_mutex;
    std::shared_ptr<Tegra::Control::ChannelState> channel_state=std::make_shared<Tegra::Control::ChannelState>();
    explicit nvhost_gpu(Core::System& s):system{s} {}
    NvResult SetupGPFIFOChannel(IoctlAllocGpfifoEx&,DeviceFD);
};
''' + setup + r'''
struct Module {
    std::shared_ptr<nvhost_gpu> device;
    template<class T>std::shared_ptr<T> GetDevice(int fd) {return fd==1?device:nullptr;}
};
struct nvhost_as_gpu {
    Core::System& system;Module& module;std::mutex mutex;
    std::shared_ptr<Tegra::MemoryManager> gmmu;std::function<void()> bind_entered;
    nvhost_as_gpu(Core::System& s,Module& m,std::shared_ptr<Tegra::MemoryManager> memory):system{s},module{m},gmmu{std::move(memory)} {}
    NvResult BindChannel(IoctlBindChannel&);
};
'''
tests = r'''
struct Fixture {
    Core::System system;VideoCore::RasterizerInterface raster;Tegra::GPU gpu{system,raster};
    Module module{std::make_shared<nvhost_gpu>(system)};
    nvhost_as_gpu original{system,module,std::make_shared<Tegra::MemoryManager>(1)};
    nvhost_as_gpu replacement{system,module,std::make_shared<Tegra::MemoryManager>(2)};
    IoctlBindChannel params{1};
    Fixture() {Tegra::Engine::built=0;Tegra::Engine::fail_at=-1;}
    auto& channel() {return *module.device->channel_state;}
    void BindOriginal() {Require(original.BindChannel(params)==NvResult::Success,"cold original binding refused");}
    void Setup(bool failed) {
        if(failed)Tegra::Engine::fail_at=Tegra::Engine::built+3;
        IoctlAllocGpfifoEx request;bool threw=false;
        try {Require(module.device->SetupGPFIFOChannel(request,1)==NvResult::Success,"channel setup refused");}
        catch(const std::runtime_error&) {threw=true;}
        Tegra::Engine::fail_at=-1;
        Require(threw==failed,"partial initialization control did not execute");
        Require(channel().dma_pusher && channel().maxwell_3d,"actual engine construction prefix missing");
        Require(&channel().dma_pusher->memory==original.gmmu.get() &&
                &channel().maxwell_3d->memory==original.gmmu.get(),"actual engine construction lost GMMU identity");
    }
};
void QuarantinedAddressSpace() {
    Fixture f;f.original.gmmu->unmap_failed=true;
    Require(f.original.BindChannel(f.params)==NvResult::InvalidState && !f.channel().memory_manager,
            "quarantined GMMU was attached to a cold channel");
    f.original.gmmu->unmap_failed=false;f.BindOriginal();f.Setup(false);
    f.original.gmmu->unmap_failed=true;
    Require(f.original.BindChannel(f.params)==NvResult::InvalidState &&
            f.channel().memory_manager==f.original.gmmu,"quarantined GMMU accepted ready idempotent binding");
}
void ColdAndNoop() {
    Fixture f;IoctlAllocGpfifoEx deferred;
    Require(f.module.device->SetupGPFIFOChannel(deferred,1)==NvResult::Success &&
            !f.channel().initialized && !f.channel().initialization_failed &&
            !f.channel().memory_manager && f.channel().program_id==77,
            "deferred setup before address binding changed semantics");
    f.BindOriginal();
    Require(f.replacement.BindChannel(f.params)==NvResult::Success && f.channel().memory_manager==f.replacement.gmmu,
            "cold address-space replacement changed semantics");
    nvhost_as_gpu empty{f.system,f.module,{}};
    Require(empty.BindChannel(f.params)==NvResult::Success && !f.channel().memory_manager,"cold null replacement changed semantics");
}
void Materialized(bool failed,bool null_target) {
    Fixture f;f.BindOriginal();f.Setup(failed);
    std::weak_ptr<Tegra::MemoryManager> old=f.original.gmmu;
    f.original.gmmu.reset(); // the channel is now the original manager's sole shared owner
    nvhost_as_gpu empty{f.system,f.module,{}};auto& target=null_target?empty:f.replacement;
    Require(target.BindChannel(f.params)==NvResult::InvalidState,
            null_target?"materialized channel accepted null GMMU":
                        (failed?"partial channel rebound its GMMU":"ready channel rebound its GMMU"));
    Require(!old.expired() && f.channel().memory_manager->id==1 &&
            &f.channel().dma_pusher->memory==f.channel().memory_manager.get(),"engine GMMU owner was not retained");
    Require(f.channel().initialized==!failed && f.channel().initialization_failed==failed,"binding changed initialization state");
    // Same address with another control block is not a retained manager owner.
    auto borrowed=std::shared_ptr<Tegra::MemoryManager>(f.channel().memory_manager.get(),[](auto*) {});
    nvhost_as_gpu impostor{f.system,f.module,borrowed};
    Require(impostor.BindChannel(f.params)==NvResult::InvalidState && !old.expired(),
            "same-address channel binding replaced real owner");
    auto different_address=std::shared_ptr<Tegra::MemoryManager>(f.channel().memory_manager,f.replacement.gmmu.get());
    nvhost_as_gpu aliased{f.system,f.module,different_address};
    Require(aliased.BindChannel(f.params)==NvResult::InvalidState && f.channel().memory_manager->id==1,
            "shared-owner channel binding changed GMMU address");
    const auto built=Tegra::Engine::built;
    auto retained=old.lock();nvhost_as_gpu same{f.system,f.module,retained};
    Require(same.BindChannel(f.params)==NvResult::Success,"same-owner materialized binding was rejected");
    Require(f.channel().initialization_failed==failed,"same-owner binding cleared failure isolation");
    Require(Tegra::Engine::built==built,"same-owner binding reconstructed engines");
    if(failed)Require(!f.gpu.InitChannel(f.channel(),99,false),"binding enabled replay after partial initialization");
}
void RaceInitialization(bool failed) {
    Fixture f;f.BindOriginal();std::latch constructing{1},finish_init{1},binding{1};
    f.raster.initialize=[&] {constructing.count_down();finish_init.wait();if(failed)throw std::runtime_error("backend initialization failure");};
    IoctlAllocGpfifoEx request;
    auto setup_task=std::async(std::launch::async,[&] {
        try {Require(f.module.device->SetupGPFIFOChannel(request,1)==NvResult::Success,"concurrent setup refused");}
        catch(const std::runtime_error&) {Require(failed,"unexpected backend exception");}
    });constructing.wait();
    f.replacement.bind_entered=[&] {binding.count_down();};
    auto bind_task=std::async(std::launch::async,[&] {return f.replacement.BindChannel(f.params);});binding.wait();
    Require(bind_task.wait_for(100ms)!=std::future_status::ready,"binding bypassed in-flight channel initialization");
    f.gpu.owner_publications.Close();
    auto join_task=std::async(std::launch::async,[&] {f.gpu.owner_publications.Join();});
    Require(join_task.wait_for(100ms)!=std::future_status::ready,"terminal join escaped in-flight initialization");
    finish_init.count_down();Ready(setup_task);
    Require(Ready(bind_task)==NvResult::InvalidState,
            failed?"binding ignored completed partial initialization":"binding ignored completed ready initialization");
    Require(f.channel().memory_manager==f.original.gmmu,"initialization race changed engine owner");
    Ready(join_task);
}
int main() {
    QuarantinedAddressSpace();ColdAndNoop();
    for(bool failed:{false,true})for(bool null_target:{false,true})Materialized(failed,null_target);
    RaceInitialization(false);RaceInitialization(true);
    Require(Tegra::MemoryManager::live==0,"fixture manager owners leaked");
    std::cout<<"PASS materialized channel binding: actual engine owner identity, ready/partial/null refusal, same-owner idempotence and serialized initialization\n";
}
'''
source = head + bind + tests
flags = [a.cxx, '-std=c++23', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src')]


def compile_source(name, text):
    cpp = out / (name + '.cpp');cpp.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe


exe = compile_source('channel-rebind', source)
for index in range(a.repeat):
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
    log=result.stdout+result.stderr;(out/f'positive-{index}.log').write_text(log,encoding='utf-8')
    assert result.returncode==0 and 'PASS materialized channel binding:' in log, log
print(f'PASS materialized channel binding ({a.repeat} repetitions)',flush=True)
ready_guard = 'channel.initialized || channel.initialization_failed'
assert ready_guard in bind
identity_guard = '!same_gmmu_owner'
assert identity_guard in bind
materialized_guard = '''        if ((channel.initialized || channel.initialization_failed) && !same_gmmu_owner) {
            return NvResult::InvalidState;
        }
'''
assert bind.count(materialized_guard) == 1
controls = [
    ('quarantined-gmmu-binding',source.replace(
        '        if (gmmu && gmmu->HasUnmapFailure()) return NvResult::InvalidState;', ''),
     'quarantined GMMU was attached to a cold channel'),
    ('omit-ready',source.replace(ready_guard,'channel.initialization_failed'),'ready channel rebound its GMMU'),
    ('omit-partial',source.replace(ready_guard,'channel.initialized'),'partial channel rebound its GMMU'),
    ('reject-same-owner',source.replace(identity_guard,'(!same_gmmu_owner || channel.memory_manager == gmmu)'),'same-owner materialized binding was rejected'),
    ('ignore-materialized-refusal',source.replace(materialized_guard,'        (void)channel; (void)same_gmmu_owner;\n'),'ready channel rebound its GMMU'),
    ('allow-ready-null',source.replace(identity_guard,'(gmmu && !same_gmmu_owner)'),
     'materialized channel accepted null GMMU'),
    ('same-owner-clears-failure',source.replace(
        'previous = std::exchange(gpu_channel_device->channel_state->memory_manager, gmmu);',
        'gpu_channel_device->channel_state->initialization_failed = false;\n'
        '        previous = std::exchange(gpu_channel_device->channel_state->memory_manager, gmmu);'),
     'same-owner binding cleared failure isolation'),
    ('same-address-wrong-owner',source.replace(
        '                                     !channel.memory_manager.owner_before(gmmu) &&\n'
        '                                     !gmmu.owner_before(channel.memory_manager)', '                                     true'),
     'same-address channel binding replaced real owner'),
    ('shared-owner-wrong-address',source.replace(
        '                                     channel.memory_manager.get() == gmmu.get() &&\n', ''),
     'shared-owner channel binding changed GMMU address'),
    ('bypass-initialization-lock',source.replace(
        'std::scoped_lock lock(mutex, gpu_channel_device->channel_mutex);','std::scoped_lock lock(mutex);'),
     'binding bypassed in-flight channel initialization'),
]
for name,changed,expected in controls:
    assert changed!=source,name
    mutant=compile_source(name,changed)
    result=subprocess.run([str(mutant)],capture_output=True,text=True,timeout=20)
    log=result.stdout+result.stderr;(out/(name+'.log')).write_text(log,encoding='utf-8')
    assert result.returncode!=0 and 'FAIL '+expected in log,(name,log)
    print('PASS materialized binding control: '+name,flush=True)
paths=['src/video_core/control/channel_state.cpp','src/video_core/control/channel_state.h',
       'src/video_core/gpu.cpp','src/video_core/gpu.h','src/common/terminal_ingress.h',
       'src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp',
       'src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp',
       'tools/gpu_lifecycle_tests/run_channel_rebind_tests.py']
(out/'extraction.json').write_text(json.dumps({
    'sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in paths},
    'positive_repeats':a.repeat,'controls':[v[0] for v in controls],
    'actual':['ChannelState Init/BindRasterizer','GPU InitChannel/AcquireChannelBinding','NV BindChannel/SetupGPFIFOChannel'],
    'fixtures':['System/Module/fd lookup','engine construction and references','renderer/backend initialization','syncpoints'],
    'boundary':'Reject unsafe different-GMMU replacement after materialization or partial effects. No native engine execution, reversible stop, completion or guest admission.'
},indent=2),encoding='utf-8')
