# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production indexed-indirect range estimation with inclusive limits."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/index-range-tests'
out.mkdir(parents=True, exist_ok=True)
s = (root / 'src/video_core/engines/maxwell_3d.cpp').read_text()
start = s.index('size_t Maxwell3D::EstimateIndexBufferSize()')
body = s[start:s.index('\n}\n', start) + 3]
head = r'''
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
using GPUVAddr=uint64_t; using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
namespace Common { unsigned Log2Ceil64(uint64_t x) { return std::bit_width(x-1); } }
struct Maxwell3D {
 struct Index {
  u64 start=0x1000, end=0x1005; unsigned bytes=2;
  u64 StartAddress() { return start; } u64 EndAddress() { return end; }
  unsigned FormatSizeInBytes() { return bytes; }
 };
 struct { Index index_buffer; } regs;
 struct { size_t mapped=6;
  size_t GetMemoryLayoutSize(u64,size_t limit) { return std::min(mapped,limit); }
 } memory_manager;
 u32 vertices=4; u32 GetMaxCurrentVertices() { return vertices; }
 size_t EstimateIndexBufferSize();
};
'''
tail = r'''
int main() {
 Maxwell3D m;
 assert(m.EstimateIndexBufferSize()==3);
 m.vertices=4096; m.regs.index_buffer.end=0x2001; m.memory_manager.mapped=4098;
 assert(m.EstimateIndexBufferSize()==2049);
 m.regs.index_buffer.end=0x2003; m.regs.index_buffer.bytes=4; m.memory_manager.mapped=4100;
 assert(m.EstimateIndexBufferSize()==1025);
 m.vertices=0; assert(m.EstimateIndexBufferSize()==1025);
 m.regs.index_buffer.end=0xfff; assert(m.EstimateIndexBufferSize()==0);
 m.regs.index_buffer.start=0; m.regs.index_buffer.end=~u64{0};
 assert(m.EstimateIndexBufferSize()==1025); // no inclusive-extent wraparound
 std::cout << "PASS indexed indirect: inclusive limits, final tracking page, vertex pulling, reversed and full-width ranges\n";
}
'''
p = out / 'tests.cpp'
p.write_text(head + body + tail)
exe = out / ('tests.exe' if os.name == 'nt' else 'tests')
subprocess.run([args.cxx, '-std=c++23', '-Wall', '-Wextra', str(p), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
