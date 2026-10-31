# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual CPU dispatch through actual fallback scatter on a real worker."""
import argparse
import ast
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/cpu-scatter-tests'
out.mkdir(parents=True, exist_ok=True)

def literal(path, name):
    tree = ast.parse(path.read_text())
    return next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
                and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))

def extract(path, marker, template=False):
    s = path.read_text()
    position = s.index(marker)
    start = s.rfind('template <class P>', 0, position) if template else position
    return s[start:s.index('\n}\n', position) + 3]

fixture = Path(__file__).with_name('run_tests.py')
head = literal(fixture, 'head')
head = '#include <thread>\n#include <cassert>\n#include "video_core/acknowledged_callback.h"\n' + head
head = head.replace('void WriteMemory(u64, u64);',
                    'void WriteMemory(u64, u64);\n    bool OnCPUWrite(u64, u64);')
head = head.replace('int copies = 0;',
    'int copies = 0, off_thread_copies = 0; std::function<bool()> on_gpu = [] { return false; };')
head = head.replace('if (fail_copy) {', 'if (!on_gpu()) ++off_thread_copies;\n        if (fail_copy) {', 1)
source = root / 'src/video_core/buffer_cache/buffer_cache.h'
uma = (root / 'src/video_core/buffer_cache/uma.h').read_text()
uma = uma[uma.index('namespace VideoCommon::Uma'):] + '\nnamespace Uma = VideoCommon::Uma;\n'
body = ''.join(extract(source, 'BufferCache<P>::' + name + '(', True)
               for name in literal(fixture, 'names'))
body += extract(source, 'BufferCache<P>::OnCPUWrite(', True)
thread = literal(Path(__file__).with_name('run_gpu_write_tests.py'), 'head')
thread = thread[thread.index('struct ExecuteCallbackCommand'):thread.index('struct Cache {')]
body += thread + extract(root / 'src/video_core/gpu_thread.cpp', 'void ThreadManager::RunOnGpuThread(')
body += '''struct RasterizerVulkan {
 ThreadManager gpu; BufferCache<int>& buffer_cache;
 bool RunBufferCacheCpuWrite(DAddr,u64,bool=false,bool=false);
 std::optional<bool> TryRunBufferCacheCpuWrite(DAddr,u64,bool,bool);
};
'''
body += extract(root / 'src/video_core/renderer_vulkan/vk_rasterizer.cpp',
                'bool RasterizerVulkan::RunBufferCacheCpuWrite(')
body += extract(root / 'src/video_core/renderer_vulkan/vk_rasterizer.cpp',
                'std::optional<bool> RasterizerVulkan::TryRunBufferCacheCpuWrite(')
tail = r'''
int main() {
 Memory m; m.pages={{64,1008},{68,1004},{72,1000}};
 BufferCache<int> cache; cache.gpu_memory=&m;
 for (u64 d=1000;d<1012;++d) cache.ram[d]=10;
 RasterizerVulkan r{{},cache}; cache.runtime.on_gpu=[&] { return r.gpu.IsGpuThread(); };
 r.gpu.RunOnGpuThread([&] {
  auto& scratch=cache.GatherBuffer(64,12);
  std::fill(scratch.bytes.begin(),scratch.bytes.end(),99);
  cache.pending_gather_writes={64}; cache.FlushGatherWrites();
 });
 cache.runtime.off_thread_copies=0;
 const auto before=r.gpu.queued;
 r.RunBufferCacheCpuWrite(2000,4);
 assert(r.gpu.queued==before && cache.scatter_pending.size()==1);
 r.RunBufferCacheCpuWrite(1008,4,false,true);
 assert(r.gpu.queued==before+1 && cache.runtime.off_thread_copies==0);
 assert(cache.scatter_pending.empty() && cache.gather_buffers.empty());
 assert(cache.slot_buffers[cache.ids.at(1000)].bytes[0]==99);
 r.RunBufferCacheCpuWrite(1008,4);
 assert(r.gpu.queued==before+1); // bookkeeping-only write needs no GPU queue round trip
 std::cout << "PASS complete CPU unmap -> pending fallback scatter: GPU worker copies, preserved writes, unaffected CPU fast path\n";
}
'''
p = out / 'tests.cpp'
p.write_text(head + uma + body + tail)
exe = out / ('tests.exe' if os.name == 'nt' else 'tests')
subprocess.run([args.cxx, '-std=c++23', '-O0', '-pthread', '-I', str(root / 'src'),
                str(p), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=20)
