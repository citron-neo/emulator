# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract changed production contracts; no Vulkan/NCE hardware execution is implied."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/pr375-pr380/contracts'
out.mkdir(parents=True, exist_ok=True)
sources = {}


def source(path):
    text = (root / path).read_text()
    sources[path] = hashlib.sha256((root / path).read_bytes()).hexdigest()
    return text


def function(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


host = source('src/common/host_memory.cpp')
buffer = source('src/video_core/renderer_vulkan/vk_buffer_cache.h')
nce = source('src/core/arm/nce/arm_nce.cpp')
# Compile the existing narrow recovery block, including its reads and PC/register changes.
start = nce.index('        // Some guest object wrappers')
end = nce.index('        // Do what dynarmic', start)
recovery = nce[start:end]

head = r'''
#include <cassert>
#include <charconv>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>
#include "common/host_memory.h"
#include "video_core/buffer_cache/usage_tracker.h"
#define LOG_WARNING(...) ((void)0)
using Common::HostMapping;
using Common::MemoryPermission;
namespace Common {
'''
body = function(host, 'bool ParseHostMapping(') + '}\n'
body += '''struct Buffer {
    VideoCommon::UsageTracker tracker{16384};
    u64 usage_tick{};
'''
body += function(buffer, 'bool IsRegionUsed(')
body += function(buffer, 'void MarkUsage(') + '};\n'
# fstat is the sole OS seam for this metadata test. Real mmap/fstat/export behavior is
# covered separately by the Linux HostMemory runners, not by this extraction.
body += r'''
struct FileStat { u64 st_dev{},st_ino{}; };
#define stat FileStat
u64 major(u64 d) {return d >> 32;}
u64 minor(u64 d) {return d & 0xffffffff;}
int fstat(int fd, FileStat* st) {
    if(fd < 0) return -1;
    *st={9,static_cast<u64>(fd)};
    return 0;
}
struct Registry {
    struct BackingFile {u64 device,inode;};
    std::vector<std::optional<BackingFile>> backing_files{128};
    mutable std::mutex backing_mutex;
'''
body += function(host, 'bool RecordBackingFile(')
body += function(host, 'std::optional<size_t> BackingChunkOf(') + '};\n#undef stat\n'
body += r'''
struct Context { u64 regs[31]{}, pc{}; };
struct Info {void* si_addr;};
bool Recover(Context& host_ctx, Info* info) {
'''
body += recovery + 'return false;\n}\n'
tail = r'''
int main() {
    Buffer b;
    b.MarkUsage(128,64,7);
    assert(b.IsRegionUsed(128,64,7));
    assert(!b.IsRegionUsed(128,64,8));
    b.MarkUsage(256,64,7);
    assert(b.IsRegionUsed(128,64,7) && b.IsRegionUsed(256,64,7));
    b.MarkUsage(512,64,8);
    assert(!b.IsRegionUsed(128,64,8) && !b.IsRegionUsed(256,64,8));
    assert(b.IsRegionUsed(512,64,8));
    b.MarkUsage(512,64,9); // identical range after Reset must still be tracked
    assert(b.IsRegionUsed(512,64,9));
    std::cout << "PASS actual submission-tick tracking and identical-range reset\n";

    HostMapping m{};
    assert(Common::ParseHostMapping("7000-8000 rw-s 00003000 00:09 4600 /dmabuf:",m));
    auto next=m; next.start=0x8000; next.end=0x9000; next.offset=0x4000;
    assert(Common::ContinuesMapping(m,next));
    next.perms=MemoryPermission::Read;
    assert(!Common::ContinuesMapping(m,next));
    next.perms=m.perms; next.inode++;
    assert(!Common::ContinuesMapping(m,next));
    assert(!Common::ParseHostMapping("7000-8000 rw-s",next));
    Registry r;
    {std::lock_guard lock{r.backing_mutex}; assert(r.RecordBackingFile(99,4600));}
    assert(r.BackingChunkOf(m)==99); // first 99 slots cold, beyond old fixed bound
    {std::lock_guard lock{r.backing_mutex}; assert(!r.RecordBackingFile(0,-1));}
    assert(r.BackingChunkOf(m)==99);
    std::thread publish([&]{std::lock_guard lock{r.backing_mutex};
        assert(r.RecordBackingFile(1,4601));});
    for(int i=0;i<1000;++i) assert(r.BackingChunkOf(m)==99);
    publish.join(); m.inode=4601;
    assert(r.BackingChunkOf(m)==1);
    std::cout << "PASS mapping parser/permissions and sparse file publication under metadata lock\n";

    // CBZ X0, +16 followed by LDR W8,[X0,#8]. Backing instruction bytes are fixture data.
    alignas(4) u32 code[8]{0xb4000080,0xb9400808};
    Context ctx; ctx.pc=reinterpret_cast<u64>(&code[1]); ctx.regs[0]=0x123400;
    const u64 target=reinterpret_cast<u64>(&code[4]);
    Info info{reinterpret_cast<void*>(ctx.regs[0]+8)};
    assert(Recover(ctx,&info) && ctx.regs[0]==0 && ctx.pc==target);
    ctx.pc=reinterpret_cast<u64>(&code[1]); ctx.regs[0]=0x123400;
    info.si_addr=reinterpret_cast<void*>(ctx.regs[0]+16);
    assert(!Recover(ctx,&info) && ctx.regs[0]==0x123400);
    info.si_addr=reinterpret_cast<void*>(ctx.regs[0]+8); code[0]=0xd503201f;
    assert(!Recover(ctx,&info));
    std::cout << "PASS exact NCE null-wrapper recovery retains X0/PC behavior and rejects other faults\n";
}
'''
cpp = out / 'contracts.cpp'
cpp.write_text(head + body + tail)
exe = out / ('contracts.exe' if os.name == 'nt' else 'contracts')
subprocess.run([args.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=30)
(out / 'extraction.json').write_text(json.dumps({
    'production_sha256': sources,
    'fixtures': ['fstat response', 'instruction bytes and NCE context', 'buffer containing real UsageTracker'],
    'not_validated': ['Linux/Android concurrent MAP_FIXED', 'native NCE signal handling', 'Vulkan driver'],
}, indent=2))
