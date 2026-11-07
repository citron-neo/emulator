# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify unsupported gather aliases stop the production GPU command loop."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/gpu-stop-tests'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/video_core/gpu_thread.cpp').read_text()
start = source.index('static void RunThread(')
body = source[start:source.index('\n}\n', start) + 3]
head = r'''
#include <atomic>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <thread>
#include <variant>
#include <vector>
#include "video_core/buffer_cache/gather_alias.h"
#include "video_core/buffer_cache/controlled_conflict.h"
#define LOG_ERROR(...) ((void)0)
#define CITRON_PROFILE_SCOPE(...) ((void)0)
#define VK_ERROR_OUT_OF_DEVICE_MEMORY -2
#define VK_ERROR_OUT_OF_HOST_MEMORY -1
namespace Common {
 enum class ThreadPriority { Critical };
 void SetCurrentThreadName(const char*) {}
 void SetCurrentThreadPriority(ThreadPriority) {}
}
namespace VideoCommon { struct InvalidImageSize : std::runtime_error { using runtime_error::runtime_error; }; }
namespace Vulkan::vk {
 struct Exception : std::runtime_error { using runtime_error::runtime_error; int GetResult() const { return -2; } };
}
namespace Core {
 enum class SystemResultStatus { ErrorVideoCore };
 struct System {
  struct Gpu { int notified=0; bool conflict=false;
   bool HasControlledProducerConflict() const {return conflict;}
   void NotifyShutdown() { ++notified; } void TickWork() {} } gpu;
  int exited=0; void RegisterHostThread() {} Gpu& GPU() { return gpu; }
  void Exit(SystemResultStatus) { ++exited; }
 };
 namespace Frontend { struct GraphicsContext { int Acquire() { return 0; } }; }
}
namespace VideoCore {
 struct RasterizerInterface { void FlushRegion(int,int) {} void OnCacheInvalidation(int,int) {} };
 struct RendererBase { RasterizerInterface rasterizer; RasterizerInterface* ReadRasterizer() { return &rasterizer; } };
}
namespace Tegra::Control {
 struct Scheduler { bool HasSuspendedChannels() { return false; } void RetrySuspended(bool) {} void Push(int,std::vector<int>) {} };
}
struct SubmitListCommand { int channel; std::vector<int> entries; };
struct GPUTickCommand {};
struct ExecuteCallbackCommand { std::function<void()> callback; };
struct ContinueCpuWriteCommand { std::function<bool()> poll; };
struct FlushRegionCommand { int addr,size; };
struct InvalidateRegionCommand { int addr,size; };
struct SynchronizeCommand {};
struct CommandDataContainer {
 std::variant<std::monostate,SubmitListCommand,GPUTickCommand,ExecuteCallbackCommand,ContinueCpuWriteCommand,FlushRegionCommand,
              InvalidateRegionCommand,SynchronizeCommand> data;
 int fence=0; bool block=true;
};
struct SynchState {
 struct Queue {
  std::vector<CommandDataContainer> entries;
  size_t pos=0; std::stop_source* stop;
  bool TryPop(CommandDataContainer& c) { if (pos==entries.size()) return false; c=entries[pos++]; return true; }
  void PopWait(CommandDataContainer& c,std::stop_token) { if (!TryPop(c)) stop->request_stop(); }
 } queue;
 std::atomic<int> signaled_fence{0}; std::mutex write_lock; std::condition_variable cv;
};
'''
tail = r'''
int main() {
 for(unsigned mode=0;mode<3;++mode) {
 std::stop_source stop;
 Core::System system; VideoCore::RendererBase renderer;
 Core::Frontend::GraphicsContext context; Tegra::Control::Scheduler scheduler;
 SynchState state; state.queue.stop=&stop;
 int before=0, affected=0, after=0;
 state.queue.entries.push_back({ExecuteCallbackCommand{[&] { ++before; }},1});
 state.queue.entries.push_back({ExecuteCallbackCommand{[&] {
  if(mode==1) throw VideoCommon::ControlledProducerConflict("readback isolation violation");
  if(mode==2) {system.gpu.conflict=true; return;}
  VideoCommon::GatherAliasValidator v{4};
  v.Add(1000,64,4,true);
  v.Add(1000,68,4,true); // throw before affected gather/scatter/dispatch
  ++affected;
 }},2});
 state.queue.entries.push_back({ExecuteCallbackCommand{[&] { ++after; }},3});
 state.queue.entries.push_back({SynchronizeCommand{},4});
 RunThread(stop.get_token(),system,renderer,context,scheduler,state);
 assert(before==1 && affected==0 && after==0);
 assert(system.gpu.notified==1 && system.exited==1);
 assert(state.signaled_fence==4 && state.queue.pos==4);
 std::cout << (mode==0 ? "PASS unsupported physical alias" :
               mode==1 ? "PASS controlled producer conflict" : "PASS fencing-worker conflict publication")
           << ": explicit shutdown, no affected/later dispatch, queued fences retired\n";
 }
}
'''
p = out / 'tests.cpp'
p.write_text(head + body + tail)
exe = out / ('tests.exe' if os.name == 'nt' else 'tests')
subprocess.run([args.cxx, '-std=c++23', '-O0', '-pthread', '-I', str(root / 'src'),
                str(p), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=20)
