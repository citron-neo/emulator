# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual DMA decoder and channel scheduler; engine, guest memory and puller are fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m3b-dma-admission'
out.mkdir(parents=True, exist_ok=True)

header = (root / 'src/video_core/dma_pusher.h').read_text()
header = header[header.index('namespace Tegra {'):]
source = (root / 'src/video_core/dma_pusher.cpp').read_text()
source = source[source.index('namespace Tegra {'):]
scheduler_header = (root / 'src/video_core/control/scheduler.h').read_text()
scheduler_header = scheduler_header[scheduler_header.index('namespace Tegra {'):]
scheduler = (root / 'src/video_core/control/scheduler.cpp').read_text()
scheduler = scheduler[scheduler.index('namespace Tegra::Control {'):]
head = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>
#include "common/bit_field.h"
#include "common/common_types.h"
#include "video_core/engines/engine_interface.h"
#define ASSERT_OR_EXECUTE(a, b) do {if (!(a)) {b;}} while (0)
namespace Common { template<class T> using ScratchBuffer = std::vector<T>; }
namespace Settings {bool IsGPULevelNormal() {return true;}}
namespace Core {struct System {bool IsPoweredOn() const {return true;}};}
namespace VideoCore {struct RasterizerInterface {};}
namespace Tegra {
class DmaPusher;
class GPU {public: void FlushCommands() {} void OnCommandListEnd() {} void BindChannel(int) {}};
class MemoryManager {public: bool IsMemoryDirty(u64,u64) {return false;}};
namespace Control {struct ChannelState;}
namespace Memory {
enum class GuestMemoryFlags {SafeRead, UnsafeRead};
template<class T, GuestMemoryFlags> struct GpuGuestMemory {
    GpuGuestMemory(MemoryManager&,u64,size_t,Common::ScratchBuffer<T>*) {}
    operator std::span<const T>() const {return {};}
};
}
namespace Engines {
struct Puller {
    struct MethodCall {u32 method, argument, subchannel, count;};
    static inline bool ready = false;
    bool pending = false;
    Puller(GPU&,MemoryManager&,DmaPusher&,Control::ChannelState&) {}
    bool HasPendingAcquire() const {return pending;}
    bool RetryPendingAcquire(bool) {if (ready) pending=false; return !pending;}
    void AcquireSemaphore(u64,u32,bool) {pending=true;}
    void BindRasterizer(VideoCore::RasterizerInterface*) {}
    void CallPullerMethod(MethodCall call) {if (call.method==0x1a) pending=true;}
    void CallMultiMethod(u32 method,u32,const u32*,u32,u32) {if(method==0x1a) pending=true;}
};
}
}
'''
channel = r'''
namespace Tegra::Control {
struct ChannelState {int bind_id{}; std::unique_ptr<DmaPusher> dma_pusher;};
}
'''
tests = r'''
using namespace Tegra;
void Require(bool value,const char* reason) {if(!value) {std::cerr<<reason<<'\n'; std::exit(1);}}
struct Engine : Engines::EngineInterface {
    std::vector<std::pair<u32,u32>> effects;
    Engine() {execution_mask.set();}
    void CallMethod(u32 method,u32 arg,bool) override {effects.emplace_back(method,arg);}
    void CallMultiMethod(u32 method,const u32* args,u32 n,u32) override {
        for(u32 i=0;i<n;++i) effects.emplace_back(method,args[i]);
    }
};
CommandHeader H(u32 method,u32 count,SubmissionMode mode) {
    CommandHeader h{};h.method.Assign(method);h.arg_count.Assign(count);h.mode.Assign(mode);return h;
}
CommandHeader A(u32 value) {CommandHeader h{};h.argument=value;return h;}
struct Environment {
    Core::System system;GPU gpu;MemoryManager memory;Control::Scheduler scheduler{gpu};
    Engine engine,other_engine;
    std::shared_ptr<Control::ChannelState> first=std::make_shared<Control::ChannelState>();
    std::shared_ptr<Control::ChannelState> second=std::make_shared<Control::ChannelState>();
    Environment() {
        first->bind_id=1;second->bind_id=2;
        first->dma_pusher=std::make_unique<DmaPusher>(system,gpu,memory,*first);
        second->dma_pusher=std::make_unique<DmaPusher>(system,gpu,memory,*second);
        first->dma_pusher->BindSubchannel(&engine,0,Engines::EngineTypes::Maxwell3D);
        second->dma_pusher->BindSubchannel(&other_engine,0,Engines::EngineTypes::Maxwell3D);
        scheduler.DeclareChannel(first);scheduler.DeclareChannel(second);
    }
    void Push(int lane,std::vector<CommandHeader> words) {scheduler.Push(lane,CommandList{std::move(words)});}
};
int main() {
    {
        Environment e;bool ready=false;
        e.first->dma_pusher->SetMethodAdmission([&](u32,u32 method,std::span<const u32>,u32) {
            return method!=0x101 || ready;
        });
        e.Push(1,{H(0x100,3,SubmissionMode::Increasing),A(11),A(22),A(33)});
        Require(e.engine.effects==std::vector<std::pair<u32,u32>>{{0x100,11}},"park executed or replayed a method");
        bool rejected=false;
        try {e.first->dma_pusher->SetMethodAdmission({});} catch(const std::logic_error&) {rejected=true;}
        Require(rejected,"an in-flight admission callback could be replaced");
        e.Push(1,{H(0x110,44,SubmissionMode::Inline)});
        e.Push(2,{H(0x200,55,SubmissionMode::Inline)});
        e.scheduler.RetrySuspended(false);
        Require(e.engine.effects.size()==1 && e.other_engine.effects.size()==1,"blocked lane stranded other lane or its tail overtook");
        ready=true;e.scheduler.RetrySuspended(false);
        Require(e.engine.effects==std::vector<std::pair<u32,u32>>{{0x100,11},{0x101,22},{0x102,33},{0x110,44}},"increasing continuation lost, reordered or replayed words");
        Require(!e.scheduler.HasSuspendedChannels(),"completed channel remained parked");
    }
    for(auto mode:{SubmissionMode::NonIncreasing,SubmissionMode::IncreaseOnce}) {
        Environment e;bool ready=false;
        e.first->dma_pusher->SetMethodAdmission([&](u32,u32,std::span<const u32>,u32){return ready;});
        e.Push(1,{H(0x100,3,mode),A(1),A(2),A(3)});
        Require(e.engine.effects.empty(),"blocked NI batch had effects");
        ready=true;e.scheduler.RetrySuspended(false);
        const u32 tail=mode==SubmissionMode::IncreaseOnce ? 0x101 : 0x100;
        Require(e.engine.effects==std::vector<std::pair<u32,u32>>{{0x100,1},{tail,2},{tail,3}},"NI/increase-once state changed across park");
    }
    {
        Environment e;bool ready=false;
        e.first->dma_pusher->SetMethodAdmission([&](u32,u32,std::span<const u32> args,u32){Require(args.size()==1&&args[0]==7,"inline argument changed");return ready;});
        e.Push(1,{H(0x100,7,SubmissionMode::Inline)});
        Require(e.engine.effects.empty(),"inline method executed before admission");
        ready=true;e.scheduler.RetrySuspended(false);
        Require(e.engine.effects==std::vector<std::pair<u32,u32>>{{0x100,7}},"inline continuation replayed or was lost");
    }
    {
        Environment e;Engines::Puller::ready=false;
        e.Push(1,{H(0x1a,0,SubmissionMode::Inline),H(0x100,9,SubmissionMode::Inline)});
        Require(e.engine.effects.empty()&&e.scheduler.HasSuspendedChannels(),"semaphore wait bypassed");
        e.scheduler.RetrySuspended(false);Require(e.engine.effects.empty(),"unready semaphore resumed");
        Engines::Puller::ready=true;e.scheduler.RetrySuspended(false);
        Require(e.engine.effects==std::vector<std::pair<u32,u32>>{{0x100,9}},"semaphore tail lost or replayed");
    }
    std::cout<<"PASS actual DMA/scheduler: pre-effect park, independent lane, ordered tail, increasing/NI/inline/increase-once, semaphore compatibility\n";
}
'''
cpp = out / 'dma_admission.cpp'
cpp.write_text(head + header + source + channel + scheduler_header + scheduler + tests)
exe = out / ('dma_admission.exe' if os.name == 'nt' else 'dma_admission')
subprocess.run([a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror',
                '-Wno-unused-parameter','-I',str(root/'src'),str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
baseline = source.replace('if (method_admission &&', 'if (false && method_admission &&')
baseline = baseline.replace('if (method_admission) {', 'if (false && method_admission) {')
if baseline == source:
    raise RuntimeError('Negative control did not remove admission checks')
control = out / ('dma_admission_baseline.exe' if os.name == 'nt' else 'dma_admission_baseline')
cpp.write_text(head + header + baseline + channel + scheduler_header + scheduler + tests)
subprocess.run([a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror',
                '-Wno-unused-parameter','-I',str(root/'src'),str(cpp),'-o',str(control)],check=True)
result = subprocess.run([str(control)],capture_output=True,text=True,timeout=30)
if result.returncode == 0 or 'park executed or replayed' not in result.stderr:
    raise RuntimeError('Negative control did not fail the stale execution assertion')
cpp.write_text(head + header + source + channel + scheduler_header + scheduler + tests)
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {path: hashlib.sha256((root/path).read_bytes()).hexdigest() for path in [
        'src/video_core/dma_pusher.h','src/video_core/dma_pusher.cpp',
        'src/video_core/control/scheduler.h','src/video_core/control/scheduler.cpp']},
    'fixtures': ['Core/System/GPU shell', 'guest command reads', 'Puller semaphore dependency',
                 'engine effects and ChannelState ownership'],
    'not_validated': ['production physical admission adapter', 'GPU transfer completion',
                      'mapping/resource pins', 'NCE/JIT', 'Vulkan driver'],
},indent=2))
print('PASS negative control: bypassing preflight executes the blocked method')
