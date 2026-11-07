# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual GPU/NV owner publication; fixture renderer, channel engines and GMMU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--output-dir', type=Path)
p.add_argument('--control-prefix', action='append', default=[],
               help='Run only mutation controls matching any prefix; positives always run')
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = a.output_dir.resolve() if a.output_dir else root / '.diagnostics/m4-gpu-owner-publication'
out.mkdir(parents=True, exist_ok=True)

def read(path):
    return (root / path).read_text()

def method(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

gpu = read('src/video_core/gpu.cpp')
fixture = out / 'gpu/gpu_requests.cpp'
required = [method(gpu, '~Impl()'), method(gpu, 'void Shutdown()'), method(gpu, 'void NotifyShutdown()')]
if not fixture.exists() or not all(m in fixture.read_text() for m in required):
    subprocess.run([sys.executable, str(root / 'tools/gpu_lifecycle_tests/run_tests.py'),
                    '--cxx', a.cxx, '--repeat', str(a.repeat), '--output-dir', str(out / 'gpu')], check=True)
base = fixture.read_text().split('};\nvoid lifecycle()')[0]
assert all(m in base for m in required)
assert 'owner_publications.Open();' in gpu[:gpu.index('~Impl()')]
base = base.replace('struct Manager {', 'struct Manager {\n    Event* stopped=nullptr;')
stopped = 'if(worker.joinable()) { worker.request_stop(); worker.join(); }'
assert base.count(stopped) == 1
base = base.replace(stopped, stopped + '\n        if(stopped) stopped->Set();')

memory = method(read('src/video_core/memory_manager.cpp'),
                'bool MemoryManager::BindRasterizer(').replace('MemoryManager::', '')
memory = memory.replace('    auto entry = binding->TryBind(rasterizer_);',
                        '    if(before_raw_enter) before_raw_enter();\n    auto entry = binding->TryBind(rasterizer_);')
memory = memory.replace('    return true;', '    if(raw_bind_hook) raw_bind_hook();\n    return true;')
attach = method(read('src/video_core/memory_manager.cpp'),
                'bool MemoryManager::AttachRasterizer(').replace('MemoryManager::', '')
attach = attach.replace('    rasterizer = binding->Target();',
                        '    if(bind_hook) bind_hook();\n    ++binds;\n    rasterizer = binding->Target();')
fake_memory = r'''
struct TestAllocator {
    inline static int live=0,fail_after=-1;
    TestAllocator(u32,u32) {if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;++live;}
    ~TestAllocator() {--live;}
};
namespace Tegra {
struct MemoryManager {
    bool HasUnmapFailure() const {return false;}
    inline static int live=0,binds=0; inline static bool fail=false;
    inline static std::function<void()> bind_hook,raw_bind_hook,before_raw_enter;
    std::mutex rasterizer_binding_mutex;
    std::atomic<std::shared_ptr<RasterizerBindingState>> rasterizer_binding;
    VideoCore::Rasterizer* rasterizer=nullptr;
    bool counted=false;MemoryManager()=default;
    template<class System> MemoryManager(System&,u64,u64,u64,u64) {if(fail)throw std::bad_alloc();counted=true;++live;}
    std::function<void()> retire_hook;
    ~MemoryManager() {if(retire_hook)retire_hook();if(counted)--live;}
''' + memory.replace('VideoCore::RasterizerInterface*', 'VideoCore::Rasterizer*') + '\n' + attach + '\n};\n}\n'
channel_fixture = r'''
namespace Tegra {
struct CommandList {std::vector<int> entries;};
namespace Control {
struct ChannelState {
    inline static int live=0;inline static std::function<void()> create_hook;
    s32 bind_id;u64 program_id=0;bool initialized=false,initialization_failed=false;
    std::shared_ptr<MemoryManager> memory_manager;
    int init_calls=0,bind_calls=0;std::function<void()> init_hook;
    explicit ChannelState(s32 id):bind_id{id} {if(create_hook)create_hook();++live;}
    ~ChannelState() {--live;}
    template<class System,class Gpu> void Init(System&,Gpu&,u64 id) {
        ++init_calls;program_id=id;if(init_hook)init_hook();
    }
    void BindRasterizer(VideoCore::Rasterizer*) {++bind_calls;}
};
}
}
'''
base = base.replace('struct RasterizerInterface {', '''struct RasterizerInterface {
    int channel_inits=0;std::function<void()> initialize_channel;
    int channel_binds=0;template<class Channel> void BindChannel(Channel&) {++channel_binds;}
    template<class Channel> void InitializeChannel(Channel& channel) {
        Require(!channel.initialized,"channel advertised ready before backend initialization");
        ++channel_inits;if(initialize_channel)initialize_channel();
    }''')
declare = method(read('src/video_core/control/scheduler.cpp'), 'void Scheduler::DeclareChannel(')
declare = declare.replace('Scheduler::', '').replace('std::shared_ptr<ChannelState>', 'std::shared_ptr<Channel>')
declare = declare.replace('    std::unique_lock lk(scheduling_guard);',
                          '    if(declare_hook) declare_hook();\n    std::unique_lock lk(scheduling_guard);')
declare = declare[:-1] + '    ++declared;\n}'
base = base.replace('struct Scheduler {', '''struct Scheduler {
    int declared=0;std::mutex scheduling_guard;
    std::unordered_map<s32,std::shared_ptr<void>> channels;std::function<void()> declare_hook;
    template<class Channel>
''' + declare)
allocation_type = method(read('src/video_core/gpu.h'), '    struct ChannelAllocation {').replace(
    'Control::ChannelState', 'Tegra::Control::ChannelState') + ';\n'
base = base.replace('struct Host {', fake_memory + 'struct Host {', 1)
base = base.replace('struct Renderer {', 'struct Renderer {\n    auto ReadRasterizer() {return rasterizer;}')
base = base.replace('struct Host { Syncpoints points;', '''struct Host {
    Tegra::MemoryManager host_gmmu;
    struct {VideoCore::Rasterizer* target=nullptr;void BindInterface(VideoCore::Rasterizer* r) {target=r;}} interface;
    auto& GMMU() {return host_gmmu;}auto& MemoryManager() {return interface;}
    Syncpoints points;''')
base = base.replace('struct Impl {', channel_fixture + '''
''' + allocation_type + '''
struct Impl {
    int system=0,gpu=0;s32 new_channel_id=1;
    std::unordered_map<s32,std::shared_ptr<Tegra::Control::ChannelState>> channels;
    std::mutex channel_creation_mutex,channel_map_mutex;s32 bound_channel=-1;
    Tegra::Control::ChannelState* current_channel=nullptr;
''')
init = method(gpu, '    std::optional<Common::TerminalIngress::Entry>\n    InitAddressSpace(')
channel_init = method(gpu, '    std::optional<Common::TerminalIngress::Entry>\n    InitChannel(')
channel_init = channel_init.replace('Control::ChannelState', 'Tegra::Control::ChannelState')
assert 'initialized = true' not in method(read('src/video_core/control/channel_state.cpp'), 'void ChannelState::Init(')
allocation = method(gpu, '    std::optional<GPU::ChannelAllocation> AllocateChannel(').replace(
    'GPU::ChannelAllocation', 'ChannelAllocation')
binding = method(gpu, '    void BindChannel(s32 channel_id)').replace(
    'Control::ChannelState', 'Tegra::Control::ChannelState')
binding_entry = method(gpu, '    std::optional<Common::TerminalIngress::Entry> AcquireChannelBinding()')
renderer_bind = method(gpu, '    void BindRenderer(std::unique_ptr<VideoCore::RendererBase> renderer_)')
base += '\n' + renderer_bind + '\n' + binding_entry + '\n' + init + '\n' + channel_init + '\n' + allocation + '\n' + binding + '\n};\n'
public_init = method(gpu, 'std::optional<Common::TerminalIngress::Entry>\nGPU::InitAddressSpace(')
public_init = public_init.replace('GPU::', '')
public_renderer = method(gpu, 'void GPU::BindRenderer(').replace('GPU::', '')
public_binding_entry = method(gpu, 'std::optional<Common::TerminalIngress::Entry> GPU::AcquireChannelBinding(').replace('GPU::', '')
public_channel = method(gpu, 'std::optional<Common::TerminalIngress::Entry>\nGPU::InitChannel(')
public_channel = public_channel.replace('GPU::', '').replace('Control::ChannelState', 'Tegra::Control::ChannelState')
public_channel = public_channel.replace('bool allow_deferred)', 'bool allow_deferred = false)')
public_allocation = method(gpu, 'std::optional<GPU::ChannelAllocation> GPU::AllocateChannel(').replace(
    'GPU::ChannelAllocation', 'ChannelAllocation').replace('GPU::', '')
vm = method(read('src/core/hle/service/nvdrv/devices/nvhost_as_gpu.h'), '    struct VM {')
vm = vm.replace('Common::FlatAllocator<u32, 0, 32>', 'TestAllocator')
allocate = method(read('src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp'),
                  'NvResult nvhost_as_gpu::AllocAsEx(').replace('nvhost_as_gpu::', '')
commit = '    pending_vm.initialised = true;'
assert allocate.count(commit) == 1
allocate = allocate.replace(commit, '    if(commit_hook) commit_hook();\n' + commit)
caller = r'''
struct PublicGpu {Impl* impl;
    int submissions=0;void PushGPUEntries(s32,Tegra::CommandList&&) {++submissions;}
''' + public_renderer + '\n' + public_binding_entry + '\n' + public_init + '\n' + public_channel + '\n' + public_allocation + r'''
};
struct FixtureSystem {PublicGpu& facade;PublicGpu& GPU() {return facade;}};
enum class NvResult {Success,InvalidState,BadValue,AlreadyAllocated,BadParameter};
struct IoctlAllocAsEx {u32 big_page_size=0;u64 va_range_start=0,va_range_split=0,va_range_end=0;};
#define LOG_DEBUG(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define ASSERT_MSG(...) ((void)0)
struct AddressSpace {
    FixtureSystem& system;std::mutex mutex;std::function<void()> commit_hook;
''' + vm + ';\n    VM vm;\n    std::shared_ptr<Tegra::MemoryManager> gmmu;\n' + allocate + '\n};\n'
nv_channel = read('src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp')
setup = method(nv_channel, 'NvResult nvhost_gpu::SetupGPFIFOChannel(').replace('nvhost_gpu::', '')
setup = setup.replace('    channel_state->program_id = program_id;',
                      '    if(setup_hook) setup_hook();\n    channel_state->program_id = program_id;')
submit = method(nv_channel, 'NvResult nvhost_gpu::SubmitGPFIFOImpl(').replace('nvhost_gpu::', '')
submit = submit.replace('    flags.raw = 0;', '    if(submit_hook) submit_hook();\n    flags.raw = 0;')
caller += r'''
#define LOG_WARNING(...) ((void)0)
#define LOG_TRACE(...) ((void)0)
struct Fence {u32 id=0,value=0;};
struct Flag {bool enabled=false;bool Value() const {return enabled;}};
struct Flags {Flag fence_wait,increment_value,fence_increment,suppress_wfi;u32 raw=0;};
struct IoctlAllocGpfifoEx {Fence fence_out;};
struct IoctlSubmitGpfifo {Flags flags;Fence fence;};
using DeviceFD=int;
struct ChannelSyncpoints {
    int queries=0,increments=0;
    Fence GetSyncpointFence(u32 id) {++queries;return {id,9};}
    bool IsFenceSignalled(Fence) {return true;}
    u32 IncrementSyncpointMaxExt(u32,u32 n) {++increments;return n;}
};
auto BuildWaitCommandList(Fence) {return std::vector<int>{};}
auto BuildIncrementCommandList(Fence) {return std::vector<int>{};}
auto BuildIncrementWithWfiCommandList(Fence) {return std::vector<int>{};}
struct Process {u64 GetProgramId() {return 77;}};
struct Session {Process* process;};
struct Container {
    Process process;Session session{&process};Session* GetSession(int) {return &session;}
};
struct ChannelDevice {
    FixtureSystem& system;Container core;std::unordered_map<int,int> sessions;
    std::shared_ptr<Tegra::Control::ChannelState> channel_state;
    std::mutex channel_mutex;ChannelSyncpoints syncpoint_manager;u32 channel_syncpoint=1;
    std::function<void()> setup_hook,submit_hook;
    ChannelDevice(FixtureSystem& s,std::shared_ptr<Tegra::Control::ChannelState> c):system{s},channel_state{std::move(c)} {}
''' + setup + '\n' + submit + '\n};\n'

# Retain the actual allocation through the real factory, fd publication and
# session acknowledgement. Only external resource providers are substituted.
nv_module = read('src/core/hle/service/nvdrv/nvdrv.cpp')
nv_header = read('src/core/hle/service/nvdrv/nvdrv.h')
factory_start = nv_module.index('    builders["/dev/nvhost-gpu"] = ')
factory_end = nv_module.index('\n    };', factory_start) + len('\n    };')
factory = nv_module[factory_start:factory_end]
opened = method(nv_module, 'DeviceFD Module::Open(').replace('Module::', '').replace('NvCore::SessionId', 'int')
opened = opened.replace('    device->OnOpen(session_id, fd);',
                        '    if(open_hook) open_hook();\n    device->OnOpen(session_id, fd);')
module_destructor = method(nv_module, 'Module::~Module()').replace('Module::', '')
open_result = method(nv_header, '    struct OpenResult {') + ';'
get_device = method(nv_header, '    std::shared_ptr<T> GetDevice(')
ctor_start = nv_channel.index('nvhost_gpu::nvhost_gpu(')
ctor_end = nv_channel.index('\n}', ctor_start) + 2
device_ctor = nv_channel[ctor_start:ctor_end].replace('nvhost_gpu::', '').replace(
    'Core::System&', 'FixtureSystem&').replace('EventInterface&', 'CreationEvents&').replace(
    'NvCore::Container&', 'CreationContainer&')
device_destructor = method(nv_channel, 'nvhost_gpu::~nvhost_gpu()').replace('nvhost_gpu::', '')
on_open = method(nv_channel, 'void nvhost_gpu::OnOpen(').replace('nvhost_gpu::', '').replace('NvCore::SessionId', 'int')
on_open = on_open.replace('    sessions[fd] = session_id;',
                          '    if(session_hook) session_hook();\n    sessions[fd] = session_id;')
as_bind = method(read('src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp'),
                 'NvResult nvhost_as_gpu::BindChannel(').replace('nvhost_as_gpu::', '').replace(
    'GetDevice<nvhost_gpu>', 'GetDevice<Devices::nvhost_gpu>')
as_bind = as_bind.replace('    auto gpu_channel_device =',
                          '    if(binding_entry_hook) binding_entry_hook();\n    auto gpu_channel_device =')
as_bind = as_bind.replace('    return NvResult::Success;',
                          '    if(binding_commit_hook) binding_commit_hook();\n    return NvResult::Success;')
caller += r'''
void HardRequire(bool,const char*);
constexpr DeviceFD INVALID_NVDRV_FD=-1;
struct CreationSyncpoints {
    int live=0,allocated=0;
    u32 AllocateSyncpoint(bool) {++live;return ++allocated;}
    void FreeSyncpoint(u32) {--live;}
    ~CreationSyncpoints() {HardRequire(live==0,"module destroyed syncpoint provider before owned devices");}
};
struct CreationContainer {
    CreationSyncpoints syncpoints;int nvmap=0;
    CreationSyncpoints& GetSyncpointManager() {return syncpoints;}
    int& GetNvMapFile() {return nvmap;}
};
struct CreationEvents {
    int live=0,allocated=0;std::function<void()> create_hook;
    int CreateEvent(const char*) {if(create_hook)create_hook();++live;return ++allocated;}
    void FreeEvent(int) {--live;}
    ~CreationEvents() {HardRequire(live==0,"module destroyed event provider before owned devices");}
};
namespace Devices {
struct nvdevice {
    FixtureSystem& system;explicit nvdevice(FixtureSystem& s):system{s} {}
    virtual ~nvdevice()=default;virtual void OnOpen(int,DeviceFD)=0;
};
struct nvhost_gpu: nvdevice {
    std::mutex channel_mutex;
    CreationEvents& events_interface;CreationContainer& core;
    CreationSyncpoints& syncpoint_manager;int& nvmap;
    std::shared_ptr<Tegra::Control::ChannelState> channel_state;
    u32 channel_syncpoint=0;int sm_exception_breakpoint_int_report_event=0;
    int sm_exception_breakpoint_pause_report_event=0,error_notifier_event=0;
    std::unordered_map<DeviceFD,int> sessions;inline static std::function<void()> session_hook;
''' + device_ctor + '\n' + device_destructor + '\n' + on_open + r'''
};
}
struct Module {
    FixtureSystem& system;CreationContainer container;DeviceFD next_fd=1;
    std::shared_ptr<Common::ProducerDrain> dispatch_domain=std::make_shared<Common::ProducerDrain>();
    std::shared_ptr<Common::OwnedRetirementQueue> retirements=
        std::make_shared<Common::OwnedRetirementQueue>(dispatch_domain);
    using FilesContainerType=std::unordered_map<DeviceFD,std::shared_ptr<Devices::nvdevice>>;
    FilesContainerType open_files;CreationEvents events_interface;
''' + open_result + r'''
    std::unordered_map<std::string,std::function<OpenResult(DeviceFD)>> builders;
    std::function<void()> open_hook;
    explicit Module(FixtureSystem& system):system{system} {
''' + factory + '\n    }\n' + module_destructor + '\n' + opened + '\n    template<class T>\n' + get_device + r'''
};
struct IoctlBindChannel {DeviceFD fd;};
struct AddressBinder {
    Module& module;std::shared_ptr<Tegra::MemoryManager> gmmu;FixtureSystem& system;
    std::mutex mutex;std::function<void()> binding_entry_hook,binding_commit_hook;
    AddressBinder(Module& m,std::shared_ptr<Tegra::MemoryManager> memory):module{m},gmmu{std::move(memory)},system{m.system} {}
''' + as_bind + '\n};\n'
tests = r'''
void HardRequire(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAIL "<<message<<"\n";std::cerr.flush();std::_Exit(91);}
}
void ClosedAndFailure() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};gpu.rasterizer=&raster;
    PublicGpu facade{&gpu};FixtureSystem system{facade};AddressSpace device{system,{}, {}, {}, {}};
    IoctlAllocAsEx params;params.big_page_size=65536;
    const auto old_start=device.vm.va_range_start;
    const auto old_page=device.vm.big_page_size;
    gpu.NotifyShutdown();const auto binds=Tegra::MemoryManager::binds;
    auto result=device.AllocAsEx(params);
    // A missing-Close mutant would terminate in its destructor's Join; report
    // the earlier forbidden effect without unwinding through that invalid join.
    HardRequire(Tegra::MemoryManager::binds==binds,"closed publication bound rasterizer");
    Require(result==NvResult::InvalidState,"refused publication reported service success");
    Require(!device.gmmu && !device.vm.initialised && !device.vm.small_page_allocator &&
            !device.vm.big_page_allocator && device.vm.va_range_start==old_start && device.vm.big_page_size==old_page,
            "refused publication changed service owner state");
    Require(TestAllocator::live==0 && Tegra::MemoryManager::live==0,"refused publication leaked candidates");
}
void HealthyAndFailures() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};gpu.rasterizer=&raster;
    PublicGpu facade{&gpu};FixtureSystem system{facade};
    for(int failure=0;failure<3;++failure) {
        AddressSpace device{system,{}, {}, {}, {}};IoctlAllocAsEx params;
        TestAllocator::fail_after=failure<2?failure:-1;Tegra::MemoryManager::fail=failure==2;
        bool threw=false;try {device.AllocAsEx(params);}catch(const std::bad_alloc&) {threw=true;}
        TestAllocator::fail_after=-1;Tegra::MemoryManager::fail=false;
        Require(threw && !device.gmmu && !device.vm.initialised && !device.vm.small_page_allocator &&
                !device.vm.big_page_allocator,"candidate construction failure published owner state");
        Require(TestAllocator::live==0 && Tegra::MemoryManager::live==0,"construction failure leaked candidates");
    }
    AddressSpace device{system,{}, {}, {}, {}};IoctlAllocAsEx params;
    Require(device.AllocAsEx(params)==NvResult::Success,"healthy pre-Start publication refused");
    Require(device.gmmu && device.gmmu->rasterizer==&raster && device.vm.initialised &&
            device.vm.small_page_allocator && device.vm.big_page_allocator,"healthy publication lost owner state");
    Require(device.AllocAsEx(params)==NvResult::InvalidState,"duplicate publication accepted");
}
void RetainAcrossTeardown(bool pause_bind) {
    Host host;VideoCore::Rasterizer raster;auto gpu=std::make_unique<Impl>(host);gpu->rasterizer=&raster;
    gpu->renderer->rasterizer=&raster;PublicGpu facade{gpu.get()};FixtureSystem system{facade};
    AddressSpace device{system,{}, {}, {}, {}};IoctlAllocAsEx params;Event entered,release,stopped;
    auto block=[&] {entered.Set();release.Wait();};
    if(pause_bind)Tegra::MemoryManager::bind_hook=block;else device.commit_hook=block;
    gpu->gpu_thread.stopped=&stopped;
    auto publish=std::async(std::launch::async,[&] {return device.AllocAsEx(params);});entered.Wait();
    auto destroy=std::async(std::launch::async,[gpu=std::move(gpu)]() mutable {gpu.reset();});stopped.Wait();
    if(destroy.wait_for(100ms)==std::future_status::ready) {
        // Do not destroy a surviving Entry after a mutant has freed its owner.
        std::cerr<<"FAIL teardown escaped retained owner publication\n";std::cerr.flush();std::_Exit(91);
    }
    release.Set();Require(Ready(publish)==NvResult::Success,"entered publication lost commit");Ready(destroy);
    Tegra::MemoryManager::bind_hook={};device.commit_hook={};
    Require(device.gmmu && device.vm.initialised,"joined publication omitted service commit");
}
int main() {
    try {
        ClosedAndFailure();HealthyAndFailures();RetainAcrossTeardown(true);RetainAcrossTeardown(false);
        Require(TestAllocator::live==0 && Tegra::MemoryManager::live==0,"fixture owners leaked");
        std::cout<<"PASS retained GPU address-space publication: closed refusal, pre-Start success, staged failure, bind and caller commit join\n";
    }catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
'''
channel_tests = r'''
auto NewChannel(Impl& gpu,FixtureSystem& system,bool bound) {
    auto allocated=gpu.AllocateChannel();Require(bool(allocated),"healthy channel creation refused");
    auto channel=std::move(allocated->channel);
    if(bound)channel->memory_manager=std::make_shared<Tegra::MemoryManager>(system,37,0,17,12);
    return channel;
}
void ChannelClosed() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};gpu.rasterizer=&raster;
    PublicGpu facade{&gpu};FixtureSystem system{facade};
    std::vector<std::shared_ptr<Tegra::Control::ChannelState>> owners;
    for(bool ready:{false,true}) {auto c=NewChannel(gpu,system,true);c->initialized=ready;owners.push_back(c);}
    gpu.NotifyShutdown();
    size_t owner_index=0;
    for(bool ready:{false,true}) {
        auto channel=owners[owner_index++];channel->program_id=17;
        ChannelDevice device{system,channel};IoctlAllocGpfifoEx setup_params;setup_params.fence_out={3,55};
        const auto result=device.SetupGPFIFOChannel(setup_params,7);
        HardRequire(result==(ready?NvResult::AlreadyAllocated:NvResult::InvalidState) &&
                    channel->program_id==17 && setup_params.fence_out.value==55 &&
                    device.syncpoint_manager.queries==0 && channel->init_calls==0,
                    "closed setup published channel metadata");
        IoctlSubmitGpfifo submit_params;submit_params.flags.raw=42;submit_params.fence={3,8};
        HardRequire(device.SubmitGPFIFOImpl(submit_params,Tegra::CommandList{})==NvResult::InvalidState &&
                    submit_params.fence.id==3 && submit_params.fence.value==8 && submit_params.flags.raw==42 &&
                    device.syncpoint_manager.increments==0 && facade.submissions==0,
                    "closed submit changed fence or queue state");
    }
}
void ChannelHealthyDeferred() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};gpu.rasterizer=&raster;
    PublicGpu facade{&gpu};FixtureSystem system{facade};auto channel=NewChannel(gpu,system,false);
    ChannelDevice device{system,channel};IoctlAllocGpfifoEx params;
    Require(device.SetupGPFIFOChannel(params,7)==NvResult::Success && channel->program_id==77 &&
            !channel->initialized && !channel->initialization_failed && channel->init_calls==0,
            "healthy deferred setup changed initialization semantics");
    IoctlSubmitGpfifo submit_params;
    Require(device.SubmitGPFIFOImpl(submit_params,Tegra::CommandList{})==NvResult::InvalidState &&
            facade.submissions==0,"unbound channel submission accepted");
    channel->memory_manager=std::make_shared<Tegra::MemoryManager>(system,37,0,17,12);
    Require(device.SubmitGPFIFOImpl(submit_params,Tegra::CommandList{})==NvResult::Success &&
            channel->initialized && !channel->initialization_failed && channel->init_calls==1 &&
            channel->bind_calls==1 && raster.channel_inits==1 && facade.submissions==1,
            "healthy deferred channel failed to materialize");
    Require(device.SubmitGPFIFOImpl(submit_params,Tegra::CommandList{})==NvResult::Success &&
            channel->init_calls==1 && facade.submissions==2,"ready channel was reinitialized");
}
void ChannelFailures() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};gpu.rasterizer=&raster;
    PublicGpu facade{&gpu};FixtureSystem system{facade};
    for(bool backend:{false,true}) {
        auto channel=NewChannel(gpu,system,true);ChannelDevice device{system,channel};IoctlAllocGpfifoEx params;
        auto fail=[] {throw std::runtime_error("channel init failure");};
        if(backend)raster.initialize_channel=fail;else channel->init_hook=fail;
        bool threw=false;try {device.SetupGPFIFOChannel(params,7);}catch(const std::runtime_error&) {threw=true;}
        Require(threw && channel->initialization_failed && !channel->initialized,
                "partial channel failure lost retry isolation");
        channel->init_hook={};raster.initialize_channel={};const auto calls=channel->init_calls;
        IoctlSubmitGpfifo submit_params;
        Require(device.SetupGPFIFOChannel(params,7)==NvResult::InvalidState &&
                device.SubmitGPFIFOImpl(submit_params,Tegra::CommandList{})==NvResult::InvalidState &&
                channel->init_calls==calls && facade.submissions==0,
                "partial channel initialization was replayed");
    }
}
void ChannelRetain(int stage) {
    Host host;VideoCore::Rasterizer raster;auto gpu=std::make_unique<Impl>(host);gpu->rasterizer=&raster;
    gpu->renderer->rasterizer=&raster;PublicGpu facade{gpu.get()};FixtureSystem system{facade};
    auto channel=NewChannel(*gpu,system,true);ChannelDevice device{system,channel};IoctlAllocGpfifoEx params;
    if(stage==3)Require(device.SetupGPFIFOChannel(params,7)==NvResult::Success,"ready-channel fixture failed");
    Event entered,release,stopped;auto block=[&] {entered.Set();release.Wait();};
    if(stage==0)channel->init_hook=block;
    else if(stage==1)raster.initialize_channel=block;
    else if(stage==2)device.setup_hook=block;
    else device.submit_hook=block;
    gpu->gpu_thread.stopped=&stopped;
    auto publish=std::async(std::launch::async,[&] {
        if(stage<3)return device.SetupGPFIFOChannel(params,7);
        IoctlSubmitGpfifo submit_params;return device.SubmitGPFIFOImpl(submit_params,Tegra::CommandList{});
    });entered.Wait();
    auto destroy=std::async(std::launch::async,[gpu=std::move(gpu)]() mutable {gpu.reset();});stopped.Wait();
    HardRequire(destroy.wait_for(100ms)!=std::future_status::ready,"teardown escaped retained channel initialization");
    release.Set();Require(Ready(publish)==NvResult::Success,"entered channel publication lost service completion");
    Ready(destroy);channel->init_hook={};raster.initialize_channel={};device.setup_hook={};device.submit_hook={};
}
'''
creation_tests = r'''
void CreationClosedAndAliasScope() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};gpu.rasterizer=&raster;
    PublicGpu facade{&gpu};FixtureSystem system{facade};Module module{system};
    auto fd=module.Open("/dev/nvhost-gpu",7);
    Require(fd!=INVALID_NVDRV_FD && module.GetDevice<Devices::nvhost_gpu>(fd)->sessions.at(fd)==7,
            "healthy channel creation lost fd or session");
    gpu.NotifyShutdown();const auto next=gpu.new_channel_id;
    const auto count=gpu.channels.size();const auto declared=gpu.scheduler->declared;
    const auto files=module.open_files.size();const auto events=module.events_interface.allocated;
    const auto syncpoints=module.container.syncpoints.allocated;
    HardRequire(module.Open("/dev/nvhost-gpu",9)==INVALID_NVDRV_FD,
                "closed channel creation published device");
    Require(gpu.new_channel_id==next && gpu.channels.size()==count && gpu.scheduler->declared==declared &&
            module.open_files.size()==files && module.events_interface.allocated==events &&
            module.container.syncpoints.allocated==syncpoints,"closed creation changed owner inventory");
    AddressBinder binder{module,std::make_shared<Tegra::MemoryManager>(system,37,0,17,12)};
    IoctlBindChannel invalid{-1},valid{fd};
    Require(binder.BindChannel(invalid)==NvResult::InvalidState,"missing channel fd was accepted");
    const auto old=module.GetDevice<Devices::nvhost_gpu>(fd)->channel_state->memory_manager;
    HardRequire(binder.BindChannel(valid)==NvResult::InvalidState &&
            module.GetDevice<Devices::nvhost_gpu>(fd)->channel_state->memory_manager==old,
            "closed channel address-space binding published owner");
    // The external GMMU route is deliberately independent of this NV entry family.
    VideoCore::Rasterizer external_raster;const auto binds=Tegra::MemoryManager::binds;
    const auto before=binder.gmmu->rasterizer;
    Require(!binder.gmmu->BindRasterizer(&external_raster),"closed external GMMU accepted binding");
    HardRequire(binder.gmmu->rasterizer==before && Tegra::MemoryManager::binds==binds,
                "closed external GMMU changed rasterizer");
    std::cout<<"PASS closed external GMMU binding refuses without pointer publication\n";
}
void CreationFailures() {
    Host host;Impl gpu{host};
    for(bool declaration:{false,true}) {
        auto fail=[] {throw std::bad_alloc();};
        if(declaration)gpu.scheduler->declare_hook=fail;else Tegra::Control::ChannelState::create_hook=fail;
        bool threw=false;try {gpu.AllocateChannel();}catch(const std::bad_alloc&) {threw=true;}
        gpu.scheduler->declare_hook={};Tegra::Control::ChannelState::create_hook={};
        Require(threw && gpu.channels.empty() && gpu.scheduler->channels.empty() &&
                Tegra::Control::ChannelState::live==0,"failed channel declaration left owner inventory");
    }
    Require(bool(gpu.AllocateChannel()),"failed allocation prevented subsequent creation");
}
void CreationConcurrent() {
    Host host;Impl gpu{host};std::mutex ids_mutex;std::vector<s32> ids;
    std::vector<std::future<void>> producers;
    for(int i=0;i<8;++i)producers.push_back(std::async(std::launch::async,[&] {
        for(int j=0;j<16;++j) {
            auto allocation=gpu.AllocateChannel();Require(bool(allocation),"concurrent creation refused");
            std::scoped_lock lk{ids_mutex};ids.push_back(allocation->channel->bind_id);
        }
    }));
    for(auto& producer:producers)Ready(producer);
    std::sort(ids.begin(),ids.end());
    Require(ids.size()==128 && std::adjacent_find(ids.begin(),ids.end())==ids.end() &&
            gpu.channels.size()==128 && gpu.scheduler->channels.size()==128,
            "concurrent channel creation lost unique inventory");
}
void CreationLockOrder() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};gpu.rasterizer=&raster;
    auto existing=gpu.AllocateChannel();Event entered;
    std::unique_lock scheduler_lock{gpu.scheduler->scheduling_guard};
    gpu.scheduler->declare_hook=[&] {entered.Set();};
    auto allocation=std::async(std::launch::async,[&] {return gpu.AllocateChannel();});entered.Wait();
    auto binding=std::async(std::launch::async,[&] {gpu.BindChannel(existing->channel->bind_id);});
    const bool ready=binding.wait_for(100ms)==std::future_status::ready;
    scheduler_lock.unlock();Ready(binding);Require(bool(Ready(allocation)),"blocked allocation lost result");
    Require(ready,"channel map lock inverted scheduler binding");
    gpu.scheduler->declare_hook={};
}
void CreationRetain(int stage) {
    Host host;VideoCore::Rasterizer raster;auto gpu=std::make_unique<Impl>(host);gpu->rasterizer=&raster;
    gpu->renderer->rasterizer=&raster;PublicGpu facade{gpu.get()};FixtureSystem system{facade};Module module{system};
    Event entered,release,stopped;auto block=[&] {entered.Set();release.Wait();};
    if(stage==0)Tegra::Control::ChannelState::create_hook=block;
    else if(stage==1)gpu->scheduler->declare_hook=block;
    else if(stage==2)module.events_interface.create_hook=block;
    else if(stage==3)module.open_hook=block;
    else Devices::nvhost_gpu::session_hook=block;
    gpu->gpu_thread.stopped=&stopped;
    auto publish=std::async(std::launch::async,[&] {return module.Open("/dev/nvhost-gpu",7);});entered.Wait();
    auto destroy=std::async(std::launch::async,[gpu=std::move(gpu)]() mutable {gpu.reset();});stopped.Wait();
    HardRequire(destroy.wait_for(100ms)!=std::future_status::ready,"teardown escaped retained channel creation");
    release.Set();const auto fd=Ready(publish);
    Require(fd!=INVALID_NVDRV_FD && module.GetDevice<Devices::nvhost_gpu>(fd)->sessions.at(fd)==7,
            "joined creation lost fd or session commit");
    Ready(destroy);Tegra::Control::ChannelState::create_hook={};
    module.events_interface.create_hook={};module.open_hook={};Devices::nvhost_gpu::session_hook={};
}
'''

binding_tests = r'''
void BindingHealthy() {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};gpu.rasterizer=&raster;
    PublicGpu facade{&gpu};FixtureSystem system{facade};Module module{system};
    const auto fd=module.Open("/dev/nvhost-gpu",7);auto device=module.GetDevice<Devices::nvhost_gpu>(fd);
    AddressBinder binder{module,std::make_shared<Tegra::MemoryManager>(system,37,0,17,12)};
    IoctlBindChannel params{fd},invalid{-1};
    Require(binder.BindChannel(invalid)==NvResult::InvalidState,"missing channel binding accepted");
    Require(binder.BindChannel(params)==NvResult::Success && device->channel_state->memory_manager==binder.gmmu &&
            !device->channel_state->initialized && device->channel_state->init_calls==0,
            "healthy pre-Start binding changed initialization semantics");
    // Preserve the existing nullable assignment behavior; this is not an engine rebind proof.
    AddressBinder empty{module,{}};
    Require(empty.BindChannel(params)==NvResult::Success && !device->channel_state->memory_manager,
            "nullable pre-initialization binding changed semantics");
}
void BindingRetain(int stage) {
    Host host;VideoCore::Rasterizer raster;auto gpu=std::make_unique<Impl>(host);gpu->rasterizer=&raster;
    gpu->renderer->rasterizer=&raster;PublicGpu facade{gpu.get()};FixtureSystem system{facade};Module module{system};
    const auto fd=module.Open("/dev/nvhost-gpu",7);auto device=module.GetDevice<Devices::nvhost_gpu>(fd);
    device->channel_state->memory_manager=std::make_shared<Tegra::MemoryManager>(system,37,0,17,12);
    AddressBinder binder{module,std::make_shared<Tegra::MemoryManager>(system,37,0,17,12)};IoctlBindChannel params{fd};
    Event entered,release,stopped;auto block=[&] {entered.Set();release.Wait();};
    if(stage==0)binder.binding_entry_hook=block;
    else if(stage==1)binder.binding_commit_hook=block;
    else device->channel_state->memory_manager->retire_hook=[&] {
        auto locks=std::async(std::launch::async,[&] {std::scoped_lock lock(binder.mutex,device->channel_mutex);});
        HardRequire(locks.wait_for(100ms)==std::future_status::ready,"retired channel owner under service locks");
        Ready(locks);block();
    };
    gpu->gpu_thread.stopped=&stopped;
    auto bind=std::async(std::launch::async,[&] {return binder.BindChannel(params);});entered.Wait();
    auto destroy=std::async(std::launch::async,[gpu=std::move(gpu)]() mutable {gpu.reset();});stopped.Wait();
    HardRequire(destroy.wait_for(100ms)!=std::future_status::ready,"teardown escaped retained channel binding");
    release.Set();Require(Ready(bind)==NvResult::Success && device->channel_state->memory_manager==binder.gmmu,
                          "entered binding lost final owner acknowledgement");Ready(destroy);
}
void BindingLock(bool address_lock) {
    Host host;VideoCore::Rasterizer raster;Impl gpu{host};gpu.rasterizer=&raster;
    PublicGpu facade{&gpu};FixtureSystem system{facade};Module module{system};
    const auto fd=module.Open("/dev/nvhost-gpu",7);auto device=module.GetDevice<Devices::nvhost_gpu>(fd);
    AddressBinder binder{module,std::make_shared<Tegra::MemoryManager>(system,37,0,17,12)};IoctlBindChannel params{fd};
    std::unique_lock held{address_lock?binder.mutex:device->channel_mutex};Event entered;
    binder.binding_entry_hook=[&] {entered.Set();};
    auto bind=std::async(std::launch::async,[&] {return binder.BindChannel(params);});entered.Wait();
    const bool escaped=bind.wait_for(100ms)==std::future_status::ready;
    HardRequire(!escaped,address_lock?"channel binding bypassed address-space lock":"channel binding bypassed setup-submit lock");
    gpu.NotifyShutdown();auto join=std::async(std::launch::async,[&] {gpu.owner_publications.Join();});
    HardRequire(join.wait_for(100ms)!=std::future_status::ready,"teardown escaped channel binding lock wait");
    held.unlock();Require(Ready(bind)==NvResult::Success,"serialized channel binding refused");Ready(join);
}
'''


external_tests = r'''
void ExternalRawHealthy() {
    Host host;VideoCore::Rasterizer raster,other;Impl gpu{host};gpu.rasterizer=&raster;
    PublicGpu facade{&gpu};FixtureSystem system{facade};
    auto mm=std::make_shared<Tegra::MemoryManager>(system,37,0,17,12);
    Require(!mm->BindRasterizer(&raster) && !mm->rasterizer,"unenrolled external GMMU installed target");
    auto publication=gpu.InitAddressSpace(*mm);Require(bool(publication),"healthy GMMU enrollment refused");publication.reset();
    const auto binds=Tegra::MemoryManager::binds;
    Require(mm->BindRasterizer(&raster) && !mm->BindRasterizer(&other) && !mm->BindRasterizer(nullptr),
            "external GMMU accepted wrong target");
    Require(mm->rasterizer==&raster && Tegra::MemoryManager::binds==binds,"external GMMU rewrote enrolled pointer");
    auto alien=std::make_shared<Tegra::RasterizerBindingState>();alien->SetTarget(&other);
    Require(!mm->AttachRasterizer(alien) && mm->rasterizer==&raster,"GMMU enrollment replaced an existing owner");
    alien->Close();alien->Join();
    Tegra::MemoryManager::raw_bind_hook=[] {throw std::runtime_error("raw binding exception");};
    bool threw=false;try {mm->BindRasterizer(&raster);}catch(const std::runtime_error&) {threw=true;}
    Require(threw,"raw entry exception hook missing");Tegra::MemoryManager::raw_bind_hook={};
    gpu.NotifyShutdown();
    HardRequire(!mm->BindRasterizer(&raster),"closed external GMMU accepted original target");
    gpu.rasterizer_binding->Join();
}
void ExternalRawRetain(bool before_entry) {
    Host host;VideoCore::Rasterizer raster;auto gpu=std::make_unique<Impl>(host);gpu->rasterizer=&raster;
    gpu->renderer->rasterizer=&raster;PublicGpu facade{gpu.get()};FixtureSystem system{facade};
    auto mm=std::make_shared<Tegra::MemoryManager>(system,37,0,17,12);
    auto publication=gpu->InitAddressSpace(*mm);Require(bool(publication),"raw retain enrollment refused");publication.reset();
    Event entered,release,stopped;auto block=[&] {entered.Set();release.Wait();};
    if(before_entry)Tegra::MemoryManager::before_raw_enter=block;
    else Tegra::MemoryManager::raw_bind_hook=block;
    gpu->gpu_thread.stopped=&stopped;
    auto bind=std::async(std::launch::async,[&] {return mm->BindRasterizer(&raster);});entered.Wait();
    auto destroy=std::async(std::launch::async,[gpu=std::move(gpu)]() mutable {gpu.reset();});stopped.Wait();
    if(before_entry)Ready(destroy);
    else HardRequire(destroy.wait_for(100ms)!=std::future_status::ready,"teardown escaped external GMMU binding");
    release.Set();Require(Ready(bind)==!before_entry,"external GMMU late-entry/entered result changed");
    if(!before_entry)Ready(destroy);
    Tegra::MemoryManager::before_raw_enter={};Tegra::MemoryManager::raw_bind_hook={};
    Require(!mm->BindRasterizer(&raster) && mm->rasterizer==&raster,"retired GPU was used by external GMMU");
}
void RendererBootstrap(bool closed) {
    Host host;VideoCore::Rasterizer raster,other;Impl gpu{host};PublicGpu facade{&gpu};
    auto renderer=std::make_unique<VideoCore::Renderer>();renderer->rasterizer=&raster;
    if(closed)gpu.NotifyShutdown();
    bool threw=false;try {facade.BindRenderer(std::move(renderer));}catch(const std::runtime_error&) {threw=true;}
    Require(threw==closed,"renderer bootstrap ignored terminal refusal");
    if(closed) {Require(!host.GMMU().rasterizer && !host.interface.target,"closed renderer bootstrap published target");return;}
    Require(host.GMMU().rasterizer==&raster && host.interface.target==&raster &&
            host.GMMU().BindRasterizer(&raster),"renderer bootstrap did not enroll Host1x GMMU");
    auto duplicate=std::make_unique<VideoCore::Renderer>();duplicate->rasterizer=&other;threw=false;
    try {facade.BindRenderer(std::move(duplicate));}catch(const std::runtime_error&) {threw=true;}
    Require(threw && host.GMMU().rasterizer==&raster && host.GMMU().BindRasterizer(&raster),
            "duplicate renderer bootstrap replaced or closed current owner");
}
void RendererBootstrapRetain() {
    Host host;VideoCore::Rasterizer raster;auto gpu=std::make_unique<Impl>(host);PublicGpu facade{gpu.get()};
    Event entered,release,stopped;
    Tegra::MemoryManager::bind_hook=[&] {entered.Set();release.Wait();};gpu->gpu_thread.stopped=&stopped;
    auto renderer=std::make_unique<VideoCore::Renderer>();renderer->rasterizer=&raster;
    auto bind=std::async(std::launch::async,[&facade,renderer=std::move(renderer)]() mutable {facade.BindRenderer(std::move(renderer));});entered.Wait();
    auto destroy=std::async(std::launch::async,[gpu=std::move(gpu)]() mutable {gpu.reset();});stopped.Wait();
    HardRequire(destroy.wait_for(100ms)!=std::future_status::ready,"teardown escaped renderer enrollment");
    release.Set();Ready(bind);Ready(destroy);Tegra::MemoryManager::bind_hook={};
    Require(!host.GMMU().BindRasterizer(&raster),"late renderer enrollment reopened closed binding");
}
'''

tests = tests.replace('int main() {', channel_tests + creation_tests + binding_tests + external_tests + '\nint main() {')
final_check='        Require(TestAllocator::live==0 && Tegra::MemoryManager::live==0,"fixture owners leaked");'
assert tests.count(final_check)==1
tests = tests.replace(final_check,
                      '        ChannelClosed();ChannelHealthyDeferred();ChannelFailures();\n'
                      '        for(int stage=0;stage<4;++stage)ChannelRetain(stage);\n'
                      '        CreationClosedAndAliasScope();CreationFailures();CreationConcurrent();CreationLockOrder();\n'
                      '        for(int stage=0;stage<5;++stage)CreationRetain(stage);\n'
                      '        BindingHealthy();for(int stage=0;stage<3;++stage)BindingRetain(stage);\n'
                      '        BindingLock(true);BindingLock(false);\n'
                      '        ExternalRawHealthy();ExternalRawRetain(false);ExternalRawRetain(true);\n'
                      '        RendererBootstrap(false);RendererBootstrap(true);RendererBootstrapRetain();\n'
                      '        std::cout<<"PASS detached external GMMU binding: refusal, immutable target, close/join, late entry and retained state after GPU destruction\\n";\n'
                      '        std::cout<<"PASS retained channel address-space binding: closed refusal, cold/null assignment, lookup/replacement/ack join and both service locks\\n";\n'
                      '        Require(Tegra::Control::ChannelState::live==0,"channel owners leaked");\n'+final_check+
                      '\n        std::cout<<"PASS retained channel creation: refusal, rollback, concurrent IDs, lock order, fd/session join and provider retirement\\n";'+
                      '\n        std::cout<<"PASS retained channel initialization: deferred setup, ready commit, partial failure isolation and NV lease retention\\n";')
# Model the renderer bootstrap performed before the real NV service becomes available.
for form in ('gpu.', 'gpu->'):
    tests = tests.replace(form + 'rasterizer=&raster;', form + 'rasterizer=&raster;' +
                          form + 'rasterizer_binding->SetTarget(&raster);')
source = '#include <algorithm>\n#include <cstdlib>\n#include <unordered_map>\n#include "common/bit_util.h"\n#include "common/owned_retirement.h"\n#include "common/scope_exit.h"\n#define ASSERT(x) Require(bool(x),"missing channel binding")\n' + base + caller + tests
flags = [a.cxx, '-std=c++23', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-I', str(root / 'src')]
flags += [str(root/'src/common/terminal_fail_stop.cpp')]

def compile_source(name, text):
    cpp = out / (name + '.cpp');cpp.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe

exe = compile_source('publication', source)
for i in range(a.repeat):
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    log = result.stdout + result.stderr;(out / f'positive-{i}.log').write_text(log)
    assert result.returncode == 0 and all(marker in log for marker in (
        'PASS retained GPU address-space publication:', 'PASS retained channel initialization:',
        'PASS retained channel creation:', 'PASS retained channel address-space binding:',
        'PASS detached external GMMU binding:')), log
print(f'PASS retained GPU owner publication ({a.repeat} repetitions)', flush=True)
refusal = '    if (!publication) {\n        return NvResult::InvalidState;\n    }'
assert allocate.count(refusal) == 1
controls = [
    ('missing-close', source.replace('        owner_publications.Close();', ''), 'closed publication bound rasterizer'),
    ('missing-join', source.replace('        owner_publications.Join();', ''), 'teardown escaped retained owner publication'),
    ('early-caller-release', source.replace('    if(commit_hook) commit_hook();',
        '    publication.reset();\n    if(commit_hook) commit_hook();'), 'teardown escaped retained owner publication'),
    ('ignored-refusal', source.replace(allocate,allocate.replace(refusal, '')), 'refused publication reported service success'),
    ('unstaged-vm', source.replace('    VM pending_vm;', '    VM& pending_vm = vm;'), 'refused publication changed service owner state'),
]
channel_controls = [
    ('channel-premature-ready', source.replace(channel_init,channel_init.replace(
        '            rasterizer->InitializeChannel(to_init);',
        '            to_init.initialized = true;\n            rasterizer->InitializeChannel(to_init);')),
     'channel advertised ready before backend initialization'),
    ('channel-missing-failure-isolation', source.replace(channel_init,channel_init.replace(
        '            to_init.initialization_failed = true;', '')),'partial channel failure lost retry isolation'),
    ('channel-setup-ignored-refusal', source.replace(setup,setup.replace(refusal,'')),
     'closed setup published channel metadata'),
    ('channel-submit-ignored-refusal', source.replace(submit,submit.replace(refusal,'')),
     'closed submit changed fence or queue state'),
    ('channel-setup-early-release', source.replace(setup,setup.replace('    if(setup_hook) setup_hook();',
        '    publication.reset();\n    if(setup_hook) setup_hook();')),'teardown escaped retained channel initialization'),
    ('channel-submit-early-release', source.replace(submit,submit.replace('    if(submit_hook) submit_hook();',
        '    publication.reset();\n    if(submit_hook) submit_hook();')),'teardown escaped retained channel initialization'),
]
creation_controls = [
    ('creation-wrong-domain', source.replace(allocation,allocation.replace(
        '        auto publication = owner_publications.TryEnter();',
        '        static Common::TerminalIngress unrelated;\n'
        '        static const bool opened=(unrelated.Open(),true);(void)opened;\n'
        '        auto publication = unrelated.TryEnter();')),'closed channel creation published device'),
    ('creation-missing-rollback', source.replace(allocation,allocation.replace(
        '            channels.erase(channel_id);','')),'failed channel declaration left owner inventory'),
    ('creation-inverted-locks', source.replace(allocation,allocation.replace(
        '            scheduler->DeclareChannel(channel_state);',
        '            std::scoped_lock map_lock{channel_map_mutex};\n'
        '            scheduler->DeclareChannel(channel_state);')),'channel map lock inverted scheduler binding'),
    ('creation-factory-early-release', source.replace(factory,factory.replace(
        'open_files.emplace(fd, std::move(device)).first,\n                          std::move(allocation->publication)',
        'open_files.emplace(fd, std::move(device)).first')),'teardown escaped retained channel creation'),
    ('creation-open-early-release', source.replace(opened,opened.replace(
        '    if(open_hook) open_hook();','    opened.publication.reset();\n    if(open_hook) open_hook();')),
     'teardown escaped retained channel creation'),
    ('creation-missing-fd-retirement', source.replace(module_destructor,module_destructor.replace(
        '    open_files.clear();','')),'module destroyed event provider before owned devices'),
]
binding_controls = [
    ('binding-wrong-domain', source.replace(binding_entry,binding_entry.replace(
        '        return owner_publications.TryEnter();',
        '        static Common::TerminalIngress unrelated;\n'
        '        static const bool opened=(unrelated.Open(),true);(void)opened;\n'
        '        return unrelated.TryEnter();')),
     'closed channel address-space binding published owner'),
    ('binding-ignored-refusal', source.replace(as_bind,as_bind.replace(refusal,'')),
     'closed channel address-space binding published owner'),
    ('binding-entry-early-release', source.replace(as_bind,as_bind.replace(
        '    if(binding_entry_hook) binding_entry_hook();',
        '    publication.reset();\n    if(binding_entry_hook) binding_entry_hook();')),
     'teardown escaped retained channel binding'),
    ('binding-ack-early-release', source.replace(as_bind,as_bind.replace(
        '    if(binding_commit_hook) binding_commit_hook();',
        '    publication.reset();\n    if(binding_commit_hook) binding_commit_hook();')),
     'teardown escaped retained channel binding'),
    ('binding-retirement-under-lock', source.replace(as_bind,as_bind.replace(
        'previous = std::exchange(gpu_channel_device->channel_state->memory_manager, gmmu);',
        'gpu_channel_device->channel_state->memory_manager = gmmu;')),
     'retired channel owner under service locks'),
    ('binding-missing-address-lock', source.replace(as_bind,as_bind.replace(
        'std::scoped_lock lock(mutex, gpu_channel_device->channel_mutex);',
        'std::scoped_lock lock(gpu_channel_device->channel_mutex);')),
     'channel binding bypassed address-space lock'),
    ('binding-missing-channel-lock', source.replace(as_bind,as_bind.replace(
        'std::scoped_lock lock(mutex, gpu_channel_device->channel_mutex);',
        'std::scoped_lock lock(mutex);')),
     'channel binding bypassed setup-submit lock'),
]
external_controls = [
    ('external-missing-close', source.replace('        rasterizer_binding->Close();', '').replace(
        '        ClosedAndFailure();', '        ExternalRawHealthy();ClosedAndFailure();'),
     'closed external GMMU accepted original target'),
    ('external-missing-join', source.replace('        rasterizer_binding->Join();', ''),
     'teardown escaped external GMMU binding'),
    ('external-early-release', source.replace('    if(raw_bind_hook) raw_bind_hook();',
        '    entry.reset();\n    if(raw_bind_hook) raw_bind_hook();'),
     'teardown escaped external GMMU binding'),
    ('external-wrong-target', source.replace('binding->TryBind(rasterizer_)',
        'binding->TryBind((static_cast<void>(rasterizer_), binding->Target()))'), 'external GMMU accepted wrong target'),
    ('external-replaced-owner', source.replace('    if (current) return current == binding;',
        '    (void)current;'), 'GMMU enrollment replaced an existing owner'),
    ('renderer-early-release', source.replace('            if (!host1x.GMMU().AttachRasterizer(rasterizer_binding)) {',
        '            publication.reset();\n            if (!host1x.GMMU().AttachRasterizer(rasterizer_binding)) {'),
     'teardown escaped renderer enrollment'),
]
selected_controls = [c for c in controls + channel_controls + creation_controls + binding_controls + external_controls
                     if not a.control_prefix or any(c[0].startswith(prefix) for prefix in a.control_prefix)]
if not selected_controls:
    p.error('No mutation controls match --control-prefix')
for name, changed, expected in selected_controls:
    assert changed != source
    mutant = compile_source(name, changed)
    result = subprocess.run([str(mutant)], capture_output=True, text=True, timeout=30)
    log = result.stdout + result.stderr;(out / (name + '.log')).write_text(log)
    assert result.returncode != 0 and 'FAIL ' + expected in log, (name, log)
    print('PASS GPU publication control: ' + name, flush=True)
paths = ['src/video_core/gpu.cpp', 'src/video_core/gpu.h', 'src/video_core/memory_manager.cpp',
         'src/video_core/rasterizer_binding_state.h',
         'src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp',
         'src/core/hle/service/nvdrv/devices/nvhost_as_gpu.h', 'src/common/terminal_ingress.h',
         'src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp',
         'src/core/hle/service/nvdrv/devices/nvhost_gpu.h',
         'src/core/hle/service/nvdrv/nvdrv.cpp', 'src/core/hle/service/nvdrv/nvdrv.h',
         'src/video_core/control/scheduler.cpp',
         'src/video_core/control/channel_state.cpp', 'src/video_core/control/channel_state.h',
         'tools/gpu_lifecycle_tests/run_tests.py', 'tools/gpu_lifecycle_tests/run_owner_publication_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in paths},
    'positive_repeats': a.repeat, 'executed_controls': [c[0] for c in selected_controls],
    'control_prefixes': a.control_prefix, 'controls': [c[0] for c in controls],
    'channel_controls': [c[0] for c in channel_controls],
    'creation_controls': [c[0] for c in creation_controls],
    'binding_controls': [c[0] for c in binding_controls],
    'external_controls': [c[0] for c in external_controls],
    'actual': ['GPU publication API and forwarding', 'GPU destructor and NotifyShutdown',
               'MemoryManager public BindRasterizer and private AttachRasterizer; GPU renderer bootstrap', 'AllocAsEx and VM layout',
               'GPU channel initialization/allocation, terminal binding Entry and internal scheduler binding', 'Scheduler DeclareChannel',
               'NV SetupGPFIFOChannel and SubmitGPFIFOImpl', 'NV GPU constructor/destructor and OnOpen',
               'Module GPU builder, OpenResult, Open, GetDevice and destructor', 'NV address-space BindChannel'],
    'fixtures': ['System owner lifetime', 'renderer', 'allocator', 'GMMU allocation', 'GPU worker',
                 'channel engine allocation/binding', 'queue, event and syncpoint services'],
    'instrumentation': ['counter/hook before rasterizer assignment', 'hook before caller commit', 'event after fixture worker stop'],
    'boundary': 'Terminal address-space, channel initialization/creation and external address-space binding through service acknowledgement; pre-submit buffer reads, initialized-engine rebinding, queue acceptance/native completion remain independent; escaped Module/device references require external lifetime; Guest admission Unknown',
}, indent=2))
