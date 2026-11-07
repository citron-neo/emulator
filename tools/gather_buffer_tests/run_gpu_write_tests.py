# SPDX-License-Identifier: GPL-3.0-or-later
"""Check production CPU-write dispatch with a real worker and fake buffer cache."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/gpu-write-tests'
out.mkdir(parents=True, exist_ok=True)


def extract(path, marker):
    text = (root / path).read_text(encoding='utf-8')
    start = text.index(marker)
    return text[start:text.index('\n}\n', start) + 3]


dispatch = extract('src/video_core/gpu_thread.cpp', 'void ThreadManager::RunOnGpuThread(')
write = extract('src/video_core/renderer_vulkan/vk_rasterizer.cpp',
                'bool RasterizerVulkan::RunBufferCacheCpuWrite(')
write += extract('src/video_core/renderer_vulkan/vk_rasterizer.cpp',
                 'std::optional<bool> RasterizerVulkan::TryRunBufferCacheCpuWrite(')
head = r'''
#include <atomic>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include "video_core/acknowledged_callback.h"
namespace Settings {
inline bool nce_enabled=false;
inline bool IsNceEnabled() { return nce_enabled; }
}
using DAddr = unsigned long long;
using u64 = unsigned long long;
struct ExecuteCallbackCommand { std::function<void()> callback; };
struct ThreadManager {
    struct ThreadState {
        bool stopped = false;
        bool joinable() const { return true; }
        const ThreadState& get_stop_token() const { return *this; }
        bool stop_requested() const { return stopped; }
    } thread;
    std::thread::id owner;
    int queued = 0;
    bool IsGpuThread() const { return owner == std::this_thread::get_id(); }
    void PushCommand(ExecuteCallbackCommand command, bool block) {
        assert(block);
        // Production admission moved into PushCommand so it is atomic with Stop.
        if (thread.stopped) return;
        ++queued;
        std::thread worker([&, command = std::move(command)] {
            owner = std::this_thread::get_id();
            command.callback();
        });
        worker.join();
        owner = {};
    }
    void RunOnGpuThread(std::function<void()>);
};
struct Cache {
    std::mutex mutex;
    ThreadManager& gpu;
    bool shared = true, gpu_dirty = false, pending_scatter = false;
    int waits = 0, writes = 0;
    bool HasDirectGuestBuffers() const { return shared; }
    bool CpuWriteNeedsGpuThread(DAddr, u64) const { return shared || pending_scatter; }
    bool IsRegionGpuModified(DAddr, u64) const { return gpu_dirty; }
    void WaitGuestBacking() {
        if (pending_scatter) { assert(gpu.IsGpuThread()); }
        if (shared) {
            assert(gpu.IsGpuThread()); // models scheduler barrier + Finish
            ++waits;
        }
    }
    void InvalidateGatherMemory(DAddr, u64) { WaitGuestBacking(); }
    void WriteMemory(DAddr, u64) { WaitGuestBacking(); ++writes; }
    bool OnCPUWrite(DAddr addr, u64 size) {
        WaitGuestBacking();
        if (gpu_dirty) return true;
        WriteMemory(addr, size);
        return false;
    }
};
struct RasterizerVulkan {
    ThreadManager gpu;
    Cache buffer_cache{std::mutex{}, gpu};
    bool RunBufferCacheCpuWrite(DAddr, u64, bool = false, bool = false);
    std::optional<bool> TryRunBufferCacheCpuWrite(DAddr, u64, bool, bool);
};
'''
tail = r'''
int main() {
    RasterizerVulkan rasterizer;
    assert(!rasterizer.RunBufferCacheCpuWrite(1000, 4, true));
    assert(rasterizer.gpu.queued == 1 && rasterizer.buffer_cache.writes == 1 &&
           rasterizer.buffer_cache.waits == 2);
    rasterizer.buffer_cache.gpu_dirty = true;
    assert(rasterizer.RunBufferCacheCpuWrite(1000, 4, true));
    assert(rasterizer.buffer_cache.writes == 1); // preserve GPU-modified return behavior
    rasterizer.RunBufferCacheCpuWrite(1000, 4);
    assert(rasterizer.buffer_cache.writes == 2 && rasterizer.gpu.queued == 3);
    rasterizer.buffer_cache.shared = false;
    rasterizer.RunBufferCacheCpuWrite(1000, 4);
    assert(rasterizer.gpu.queued == 3); // no shared backing: retain CPU-side bookkeeping
    rasterizer.buffer_cache.shared = true;
    rasterizer.gpu.RunOnGpuThread([&] {
        const int before = rasterizer.gpu.queued;
        rasterizer.RunBufferCacheCpuWrite(1000, 4);
        rasterizer.gpu.RunOnGpuThread([] {});
        assert(rasterizer.gpu.queued == before); // reentrant GPU callback cannot self-wait
    });
    assert(rasterizer.gpu.queued == 4 && rasterizer.buffer_cache.writes == 4);
    rasterizer.RunBufferCacheCpuWrite(1000, 4, false, true);
    assert(rasterizer.gpu.queued == 5 && rasterizer.buffer_cache.writes == 5);
    rasterizer.buffer_cache.shared = false;
    rasterizer.buffer_cache.pending_scatter = true;
    rasterizer.RunBufferCacheCpuWrite(1000, 4, false, true);
    assert(rasterizer.gpu.queued == 6 && rasterizer.buffer_cache.writes == 6);
    RasterizerVulkan clean;
    clean.buffer_cache.shared=false;
    const auto preserved=clean.TryRunBufferCacheCpuWrite(1000,4,true,false);
    assert(preserved.has_value() && !*preserved); // successful false != cancellation
    rasterizer.buffer_cache.shared = true;
    rasterizer.buffer_cache.pending_scatter = false;
    rasterizer.gpu.thread.stopped = true;
    assert(!rasterizer.TryRunBufferCacheCpuWrite(1000,4,true,false).has_value());
    rasterizer.RunBufferCacheCpuWrite(1000, 4);
    assert(rasterizer.gpu.queued == 6 && rasterizer.buffer_cache.writes == 6);
    std::cout << "PASS CPU writes: GPU-thread waits, unlocked cache dispatch, synchronous results, "
                 "GPU reentrancy and ordinary-memory fallback\n";
}
'''
cpp = out / 'gpu_write_tests.cpp'
cpp.write_text(head + dispatch + write + tail, encoding='utf-8')
exe = out / ('gpu_write_tests.exe' if os.name == 'nt' else 'gpu_write_tests')
subprocess.run([args.cxx, '-std=c++23', '-O0', '-Wall', '-Wextra', '-pthread', '-I', str(root / 'src'),
                str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=20)
