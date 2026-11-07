# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production alias snapshots and GPU remap callback lock ordering."""
import argparse
import ast
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/mapping-snapshot-tests'
out.mkdir(parents=True, exist_ok=True)

def literal(path, name):
    tree = ast.parse(path.read_text())
    return next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
                and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))

def function(source, marker, template=False):
    pos = source.index(marker)
    start = source.rfind('template <', 0, pos) if template else pos
    return source[start:source.index('\n}\n', pos) + 3]

def method(source, marker, template=False):
    pos = source.index(marker)
    start = source.rfind('template <', 0, pos) if template else pos
    return source[start:source.index('\n    }\n', pos) + 7]

def run(name, code, rejected_assertion=None):
    cpp = out / (name + '.cpp')
    cpp.write_text(code)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([args.cxx, '-std=c++23', '-O0', '-pthread', '-I', str(root / 'src'),
                    str(cpp), '-o', str(exe)], check=True)
    if rejected_assertion is None:
        subprocess.run([str(exe)], check=True, timeout=20)
    else:
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
        if result.returncode == 0 or rejected_assertion not in result.stderr:
            raise RuntimeError('Mutant did not fail the intended assertion: ' + result.stderr)
        print('PASS reservation guard negative control rejected before successful publication')

fixture = Path(__file__).with_name('run_tests.py')
head = '#include <thread>\n#include <future>\n#include <cassert>\n' + literal(fixture, 'head')
head = head.replace('using DAddr = u64;', 'using DAddr = u64; using PAddr = u64;')
head = head.replace('u64 id = 1, generation = 2;', 'u64 id = 1; std::atomic<u64> generation{2};')
head = head.replace('u64 generation = 2;', 'std::atomic<u64> generation{2};')
# Use the actual device snapshot API, with only its pointer/physical-page primitives mocked.
head = head.replace('std::shared_ptr<std::mutex> mapping_guard = std::make_shared<std::mutex>();',
                    'mutable std::mutex mapping_guard;')
head = head.replace('std::unique_lock{*mapping_guard}', 'std::unique_lock{mapping_guard}')
start = head.index('template <typename Func> void ReadPhysicalAliases(')
end = head.index('\n        }\n', start) + 11
device = (root / 'src/core/device_memory_manager.h').read_text()
actual = method(device, 'void ReadPhysicalAliases(', True)
head = head[:start] + actual + head[end:]
start = head.index('struct DeviceMemory {') + len('struct DeviceMemory {')
head = head[:start] + r'''
        u64 device_as_size=16384;
        template <typename T> T* GetPointer(u64 d) { return d<device_as_size ? reinterpret_cast<T*>(1) : nullptr; }
        u64 GetPhysicalRawAddressFromDAddr(u64 d) {
            auto it=physical.find(d); return it==physical.end() ? d : it->second;
        }
''' + head[start:]
source = (root / 'src/video_core/buffer_cache/buffer_cache.h').read_text()
body = ''.join(function(source, 'BufferCache<P>::' + name + '(', True)
               for name in literal(fixture, 'names'))
uma = (root / 'src/video_core/buffer_cache/uma.h').read_text()
uma = uma[uma.index('namespace VideoCommon::Uma'):] + '\nnamespace Uma = VideoCommon::Uma;\n'
tail = r'''
int main() {
 using namespace std::chrono_literals;
 Memory mapping; mapping.pages={{64,1000},{68,1004},{128,2000}};
 BufferCache<int> cache; cache.gpu_memory=&mapping;
 const std::array ranges{GatherAliasRange{64,4,true,true}, GatherAliasRange{128,4,false,true}};
 cache.ValidateGatherAliases(ranges);
 std::promise<void> entered, release; auto ready=entered.get_future(); auto resume=release.get_future();
 std::thread writer([&] {
  std::scoped_lock lock{cache.device_memory.mapping_guard};
  ++cache.device_memory.generation; entered.set_value(); resume.wait();
  cache.device_memory.physical[2000]=1000; ++cache.device_memory.generation;
 });
 ready.wait();
 auto reader=std::async(std::launch::async,[&] {
  try { cache.ValidateGatherAliases(ranges); return false; }
  catch (const UnsupportedGatherMapping& e) {
   assert(std::string(e.what()).find("physical_byte=1000")!=std::string::npos); return true;
  }
 });
 assert(reader.wait_for(30ms)==std::future_status::timeout); // odd epoch waits, never misreports alias
 release.set_value(); writer.join(); assert(reader.get());
 assert(cache.runtime.copies==0 && cache.checked_alias_device_generation==2);
 cache.device_memory.physical.clear(); cache.device_memory.generation+=2;
 cache.ValidateGatherAliases(ranges); assert(cache.checked_alias_device_generation==6);
 std::promise<void> gpu_entered,gpu_release;
 auto gpu_ready=gpu_entered.get_future(); auto gpu_resume=gpu_release.get_future();
 std::thread gpu_writer([&] {
  auto lock=mapping.AcquireMappingRead(); ++mapping.generation;
  gpu_entered.set_value(); gpu_resume.wait(); mapping.pages[128]=3000; ++mapping.generation;
 });
 gpu_ready.wait();
 auto gpu_reader=std::async(std::launch::async,[&] { cache.ValidateGatherAliases(ranges); });
 assert(gpu_reader.wait_for(30ms)==std::future_status::timeout);
 gpu_release.set_value(); gpu_writer.join(); gpu_reader.get();
 assert(cache.checked_alias_identity.generation==4);
 std::cout << "PASS production alias snapshots: concurrent device/GPU update waits, real writable alias rejection, cache generation invalidation\n";
}
'''
run('aliases', head + uma + body + tail)

mm = (root / 'src/video_core/memory_manager.cpp').read_text()
header = (root / 'src/video_core/memory_manager.h').read_text()
head = r'''
#include <array>
#include <atomic>
#include <cassert>
#include <future>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t; using GPUVAddr=u64; using DAddr=u64;
#define LOG_ERROR(...) ((void)0)
enum class PTEKind { INVALID, Pitch };
struct Rasterizer {
 std::function<void()> change=[] {}; std::function<void()> unmap=[] {};
 void ModifyGPUMemory(size_t,u64,u64) { change(); } void UnmapMemory(u64,u64) { unmap(); }
};
struct MemoryManager {
 enum class EntryType : u64 { Free, Reserved, Mapped };
 std::mutex mapping_update_guard,guard; mutable std::mutex page_table_guard;
 std::atomic<u64> map_generation{0};
 std::array<EntryType,128> small{},big{};
 // Reservation/allocation is a fixture; the extracted production PageTableOp
 // must still check its result before publishing any PTEs.
 struct {
  std::array<u32,128> entries{}; bool allow=true; unsigned reservations{};
  bool ReserveRange(u64,size_t) { ++reservations; return allow; }
  u32& operator[](size_t index) { return entries[index]; }
  const u32& operator[](size_t index) const { return entries[index]; }
 } page_table;
 std::array<u32,128> big_page_table_dev{};
 std::array<bool,128> continuous{};
 std::vector<std::pair<u64,size_t>> page_stash;
 struct { void Map(u64,u64,PTEKind) {} } kind_map;
 struct { std::array<u8,4096> data{}; template <typename T> T* GetPointer(u64 d) { return reinterpret_cast<T*>(data.data()+d); } } memory;
 Rasterizer* rasterizer; const size_t unique_identifier=7;
 u64 address_space_size=512, page_size=4, big_page_size=16, page_mask=3,big_page_mask=15;
 static constexpr u64 cpu_page_bits=2;
 template <bool B> EntryType GetEntry(u64 p) const { return (B?big:small)[p/(B?16:4)]; }
 template <bool B> void SetEntry(u64 p,EntryType e) { (B?big:small)[p/(B?16:4)]=e; }
 template <bool B> size_t PageEntryIndex(u64 p) const { return p/(B?16:4); }
 void SetBigPageContinuous(size_t p,bool b) { continuous[p]=b; }
 template <bool> void GetSubmappedRangeImpl(u64,size_t,std::vector<std::pair<u64,size_t>>& out) { out.push_back({1000,8}); }
 bool IsWithinGPUAddressRange(u64 p) const { return p<address_space_size; }
 u64 MapGeneration() const { return map_generation.load(); }
'''
head += method(header, 'auto AcquireMappingRead() const')
head += r'''
 template <bool B,EntryType E> void NotifyMappingChange(u64,size_t);
 template <EntryType E> u64 PageTableOp(u64,u64,size_t,PTEKind);
 template <EntryType E> u64 BigPageTableOp(u64,u64,size_t,PTEKind);
 u64 Map(u64,u64,size_t,PTEKind,bool); u64 MapSparse(u64,size_t,bool); void Unmap(u64,size_t);
 std::optional<u64> GpuToCpuAddress(u64) const;
};
'''
body = ''.join(function(mm, marker, template) for marker, template in [
    ('void MemoryManager::NotifyMappingChange(', True),
    ('GPUVAddr MemoryManager::PageTableOp(', True),
    ('GPUVAddr MemoryManager::BigPageTableOp(', True),
    ('GPUVAddr MemoryManager::Map(', False),
    ('GPUVAddr MemoryManager::MapSparse(', False),
    ('void MemoryManager::Unmap(', False),
    ('std::optional<DAddr> MemoryManager::GpuToCpuAddress(GPUVAddr gpu_addr) const', False)])
tail = r'''
int main() {
 using namespace std::chrono_literals;
 Rasterizer rasterizer; MemoryManager mm; mm.rasterizer=&rasterizer;
 std::mutex renderer_cache; std::unique_lock rendering{renderer_cache};
 std::promise<void> callback; auto entered=callback.get_future(); std::atomic<int> calls{0};
 rasterizer.change=[&] {
  assert((mm.MapGeneration()&1)==0);
  if (++calls==1) callback.set_value();
  std::scoped_lock cache{renderer_cache};
  auto read=mm.AcquireMappingRead(); // callback cannot hold mutation lock
  assert((mm.MapGeneration()&1)==0);
 };
 auto writer=std::async(std::launch::async,[&] { mm.Map(64,1000,8,PTEKind::Pitch,false); });
 assert(entered.wait_for(2s)==std::future_status::ready);
 { auto read=mm.AcquireMappingRead(); assert(mm.MapGeneration()==0 && !mm.GpuToCpuAddress(64)); }
 rendering.unlock(); assert(writer.wait_for(2s)==std::future_status::ready); writer.get();
 assert(mm.MapGeneration()==2 && mm.GpuToCpuAddress(65)==1001 && calls==2);
 auto read=mm.AcquireMappingRead();
 auto sparse=std::async(std::launch::async,[&] { mm.MapSparse(64,8,false); });
 assert(sparse.wait_for(30ms)==std::future_status::timeout && mm.MapGeneration()==2);
 read.unlock(); sparse.get(); assert(mm.MapGeneration()==4 && !mm.GpuToCpuAddress(64));
 mm.Map(64,1000,8,PTEKind::Pitch,false);
 bool preserved=false;
 rasterizer.unmap=[&] { auto old=mm.AcquireMappingRead(); assert(mm.GpuToCpuAddress(64)==1000); preserved=true; };
 mm.Unmap(64,8); assert(preserved && mm.MapGeneration()==10 && !mm.GpuToCpuAddress(64));
 mm.Map(128,2000,16,PTEKind::Pitch,true);
 assert(mm.MapGeneration()==12 && mm.GpuToCpuAddress(129)==2001 && mm.continuous[8]);
 mm.MapSparse(128,16,true); assert(mm.MapGeneration()==14 && !mm.GpuToCpuAddress(128));
 assert(mm.page_table.reservations==2); // small mapped calls only
 mm.page_table.allow=false;
 assert(mm.Map(192,3000,8,PTEKind::Pitch,false)==0);
 assert(mm.MapGeneration()==16 && !mm.GpuToCpuAddress(192) && !mm.GpuToCpuAddress(196));
 assert(mm.page_table[48]==0 && mm.page_table[49]==0 && mm.page_table.reservations==3);
 mm.page_table.allow=true;
 assert(mm.Map(192,3000,8,PTEKind::Pitch,false)==192);
 assert(mm.MapGeneration()==18 && mm.GpuToCpuAddress(192)==3000 && mm.GpuToCpuAddress(196)==3004);
 assert(mm.page_table.reservations==4);
 std::cout << "PASS production GPU remap: invalidation before odd epoch, callback/cache lock order, small/big/sparse/unmap, reservation refusal/retry, old writeback mapping\n";
}
'''
run('remap', head + body + tail)

reservation_guard = """        if (!page_table.ReserveRange(gpu_addr, size)) {
            return 0;
        }"""
assert body.count(reservation_guard) == 1
run('remap-reservation-mutant', head + body.replace(reservation_guard, '        (void)page_table.ReserveRange(gpu_addr, size);') + tail,
    'mm.Map(192,3000,8,PTEKind::Pitch,false)==0')
