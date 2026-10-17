# SPDX-License-Identifier: GPL-2.0-or-later
"""Isolated CPU workload: compare production M1.0 with M1.1; never a device FPS test."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
parser.add_argument('--frames', type=int, default=500)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m11-cost' / ('windows' if os.name == 'nt' else 'linux')
out.mkdir(parents=True, exist_ok=True)
# Reuse generated extracted production GMMU fixture from the correctness runner.
fixture = root / '.diagnostics/physical-shadow-tests' / ('windows' if os.name == 'nt' else 'linux') / 'tests.cpp'
if not fixture.exists():
    raise SystemExit('Run run_tests.py first to generate production fixture.')
head = fixture.read_text().split('#define FMT_HEADER_ONLY')[0]
head = head.replace('#include "video_core/physical_shadow.h"', '')
body = r'''
#include "video_core/physical_shadow.h"
int main(int argc, char** argv) {
    const int frames = argc > 1 ? std::stoi(argv[1]) : 500;
    constexpr u64 G=0x10000, D=0x10000, Size=16ULL<<20;
    DeviceFixture device;
    Tegra::MemoryManager memory{device};
    device.Map(D, 0x5000, Size);
    for (u64 i=0; i<4; ++i) memory.Map(G + i*2*Size, D, Size);
    VideoCommon::PhysicalShadow shadow;
    const auto start=std::chrono::steady_clock::now();
    for (int f=0; f<frames; ++f) {
        shadow.BeginDispatch();
        for (int repeat=0; repeat<4; ++repeat) {
            for (u64 i=0; i<4; ++i) {
                shadow.Observe(memory, G+i*2*Size, Size,
                               VideoCommon::ShadowKind::RenderTarget, true, D);
            }
        }
        shadow.EndDispatch();
    }
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    const auto& s=shadow.Counters();
    std::cout << "elapsed_ns=" << ns << " inputs=" << s.coverage[4].observations
              << " writes=" << s.coverage[4].write_intents << " queries=" << s.queries
              << " registered=" << s.registered_observations << " comparison_drops=" << s.comparison_drops;
#ifdef M11
    std::cout << " selected_dispatches=" << s.sampled_dispatches << " span_steps=" << s.device_span_steps
              << " sampled_out=" << s.coverage[4].sampled_out << " dedup=" << s.coverage[4].deduplicated;
#endif
    std::cout << '\n';
}
'''
cpp = out / 'bench.cpp'
cpp.write_text(head + body)
baseline = out / 'baseline/video_core'
baseline.mkdir(parents=True, exist_ok=True)
old = subprocess.run(['git', 'show', 'ead8a452ca:src/video_core/physical_shadow.h'], cwd=root,
                     capture_output=True, text=True, check=True).stdout
(baseline / 'physical_shadow.h').write_text(old)
for mode in ['baseline', 'm11']:
    exe = out / (mode + '-bench' + ('.exe' if os.name == 'nt' else ''))
    cmd = [args.cxx, '-std=c++23', '-O2', '-pthread']
    if mode == 'baseline': cmd += ['-I', str(baseline.parent)]
    else: cmd += ['-DM11']
    subprocess.run(cmd + ['-I', str(root / 'src'), str(cpp), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe), str(args.frames)], capture_output=True, text=True, check=True, timeout=60)
    (out / (mode + '-result.txt')).write_text(result.stdout)
    print(mode, result.stdout.strip())
