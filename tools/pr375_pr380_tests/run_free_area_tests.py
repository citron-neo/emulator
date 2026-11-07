# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual ASLR candidate selection and R_ASSERT in Release with mocked queries."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/pr375-pr380/free-area'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/core/hle/kernel/k_page_table_base.cpp').read_text()
start = source.index('KProcessAddress KPageTableBase::FindFreeArea(')
opening = source.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
function = source[start:end]
result = (root / 'src/core/hle/result.h').read_text()
macro = result[result.index('#define R_ASSERT(res_expr)'):].splitlines()
macro_end = next(i for i, line in enumerate(macro) if not line.rstrip().endswith('\\'))
macro = '\n'.join(macro[:macro_end + 1])
# Keep the real ASSERT expansion: Release builds may report and continue when
# use_debug_asserts is disabled. Only logging and its runtime handler are fixtures.
assert_source = (root / 'src/common/assert.h').read_text()
assert_macro = assert_source[assert_source.index('#define ASSERT(_a_)'):].splitlines()
assert_end = next(i for i, line in enumerate(assert_macro) if not line.rstrip().endswith('\\'))
assert_macro = '\n'.join(assert_macro[:assert_end + 1])
head = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
using KProcessAddress = uint64_t;
constexpr size_t PageSize = 4096;
auto GetInteger(KProcessAddress value) { return value; }
namespace Common {
auto AlignDown(KProcessAddress value, size_t alignment) {return value & ~(alignment-1);}
}
struct KSystemControl {
static size_t GenerateRandomRange(size_t, size_t) {return 0;}
};
enum class KMemoryState { Free, Occupied };
struct KMemoryInfo {
    uint64_t address, size;
    KMemoryState m_state;
    auto GetAddress() const {return address;}
    auto GetLastAddress() const {return address + size - 1;}
};
namespace Svc {struct PageInfo {};}
#define R_SUCCEEDED(r) ((r) == 0)
#define R_FAILED(r) ((r) != 0)
'''
head += r'''
#define CITRON_NO_INLINE
#define LOG_CRITICAL(...) do {} while (false)
int assertion_failures{};
void assert_fail_impl() { ++assertion_failures; }
''' + assert_macro + '\n' + macro + r'''
struct Manager {
    mutable int fallback_calls{};
    KProcessAddress FindFreeArea(KProcessAddress, size_t, size_t, size_t, size_t, size_t) const {
        ++fallback_calls;
        return 0x9000;
    }
};
struct KPageTableBase {
    bool aslr = true;
    int failures{};
    mutable int queries{};
    Manager m_memory_block_manager;
    bool IsAslrEnabled() const {return aslr;}
    int QueryInfoImpl(KMemoryInfo* info, Svc::PageInfo*, KProcessAddress) const {
        ++queries;
        // Poison a failed output with an apparently usable range: consuming it is a bug.
        *info = {0x1000, 0x10000, KMemoryState::Free};
        return queries <= failures ? 1 : 0;
    }
    KProcessAddress FindFreeArea(KProcessAddress, size_t, size_t, size_t, size_t, size_t) const;
};
'''
tail = r'''
void Check(bool value) { if (!value) std::abort(); }
int main() {
    KPageTableBase normal;
    Check(normal.FindFreeArea(0x2000, 16, 1, PageSize, 0, 0) == 0x2000);
    Check(normal.queries == 1 && normal.m_memory_block_manager.fallback_calls == 0);
    Check(assertion_failures == 0);
    KPageTableBase retry; retry.failures = 1;
    Check(retry.FindFreeArea(0x2000, 16, 1, PageSize, 0, 0) == 0x2000);
    Check(retry.queries == 2 && retry.m_memory_block_manager.fallback_calls == 0);
    Check(assertion_failures == 1);
    KPageTableBase failure; failure.failures = 8;
    Check(failure.FindFreeArea(0x2000, 16, 1, PageSize, 0, 0) == 0x9000);
    Check(failure.queries == 8 && failure.m_memory_block_manager.fallback_calls == 1);
    Check(assertion_failures == 9);
    KPageTableBase no_aslr; no_aslr.aslr = false;
    Check(no_aslr.FindFreeArea(0x2000, 16, 1, PageSize, 0, 0) == 0x9000);
    Check(no_aslr.queries == 0 && no_aslr.m_memory_block_manager.fallback_calls == 1);
    Check(assertion_failures == 9);
    std::cout << "PASS Release FindFreeArea: success, failed-candidate retry, failure fallback, ASLR disabled\n";
}
'''
cpp = out / 'free_area.cpp'
cpp.write_text(head + function + tail)
exe = out / ('free_area.exe' if os.name == 'nt' else 'free_area')
subprocess.run([args.cxx, '-std=c++23', '-O3', '-DNDEBUG', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
baseline = function.replace('                if (R_FAILED(query_result)) {\n                    continue;\n                }\n', '')
if baseline == function:
    raise RuntimeError('Could not remove the failed-query guard for the negative control')
cpp.write_text(head + baseline + tail)
baseline_exe = out / ('free_area_baseline.exe' if os.name == 'nt' else 'free_area_baseline')
subprocess.run([args.cxx, '-std=c++23', '-O3', '-DNDEBUG', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(baseline_exe)], check=True)
result = subprocess.run([str(baseline_exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
if result.returncode == 0:
    raise RuntimeError('Regression fixture did not reject the baseline')
cpp.write_text(head + function + tail)
print('PASS negative control rejects consuming a failed query result')
