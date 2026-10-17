# SPDX-License-Identifier: GPL-2.0-or-later
"""Controlled F2 orderings with real prepared Memory stores, BufferCache bodies and tracker.

HostMemory and PageTable are real. Allocation/driver completion, device aliases and cached-tag
fanout are fixtures. This is roadmap step 3, not normal-game enrollment or Vulkan evidence.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--boost-include', type=Path)
p.add_argument('--repeat', type=int, default=10)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m3b-first-registration'
stubs = out / 'stubs'
for directory in ['common', 'core', 'fmt', 'video_core/host1x']:
    (stubs / directory).mkdir(parents=True, exist_ok=True)
(stubs / 'common/logging.h').write_text('''#pragma once
namespace Common::Log {inline void Stop() {}}
template<class... T> void TestLog(const char*,T&&...) {}
#define LOG_INFO(category,...) TestLog(__VA_ARGS__)
#define LOG_WARNING(category,...) TestLog(__VA_ARGS__)
#define LOG_ERROR(category,...) TestLog(__VA_ARGS__)
#define LOG_CRITICAL(category,...) TestLog(__VA_ARGS__)
#define LOG_DEBUG(category,...) TestLog(__VA_ARGS__)
''')
(stubs / 'common/assert.h').write_text('''#pragma once
#include <cassert>
#include <cstdlib>
#define ASSERT(x) assert(x)
#define ASSERT_MSG(x, ...) assert(x)
#define UNIMPLEMENTED_MSG(...) std::abort()
''')
(stubs / 'fmt/format.h').write_text('''#pragma once
#include <string>
namespace fmt {
template<class T> struct formatter;
struct format_parse_context {constexpr const char* begin() const {return nullptr;}};
template<class Out,class... T> Out format_to(Out out,const char*,T&&...) {return out;}
template<class... T> std::string format(const char* text,T&&...) {return text;}
}
''')
(stubs / 'video_core/host1x/gpu_device_memory_manager.h').write_text('''#pragma once
namespace Core {inline constexpr unsigned long long DEVICE_PAGESIZE=4096;}
''')

inputs = set()


def read(path):
    inputs.add(path)
    return (root / path).read_text()


def method(path, marker, closing='\n    }'):
    text = read(path)
    start = text.index(marker)
    return text[start:text.index(closing, start) + len(closing)]


tree = ast.parse(read('tools/physical_coherency_tests/run_cpu_mapping_tests.py'))
head = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
            and any(isinstance(t, ast.Name) and t.id == 'head' for t in n.targets))
cpu = '#include <bit>\n#include "common/swap.h"\n#define LOG_ERROR(...) ((void)0)\n#define CITRON_PROFILE_MEM_SCOPE(...) ((void)0)\n' + head.replace(
    'using Core::Memory::PreparedCpuPage;', 'using Core::Memory::PreparedCpuPage;\nusing Core::Memory::ScopedCpuWrite;')
cpu += r'''
    size_t legacy_pointer_lookups=0,legacy_callbacks=0;
    template<class Unmapped,class Cached> u8* GetPointerImpl(u64 address,Unmapped,Cached cached) {
        if(Core::Memory::ScopedCpuWrite::Current()) {
            std::cerr<<"bound ordinary Write bypassed its transaction\n";std::exit(88);
        }
        ++legacy_pointer_lookups;
        const auto mapping=current_page_table->SnapshotMapping(address&~u64{4095});assert(mapping);
        if(mapping->type==Common::PageType::RasterizerCachedMemory) cached();
        return system.device.buffer.BackingBasePointer()+mapping->physical_page-DramMemoryMap::Base+(address&4095);
    }
    void HandleRasterizerWrite(u64,u64) {++legacy_callbacks;}
'''
for marker in ['    void SetCurrentPageTable(',
               '    std::optional<PreparedCpuPage> PrepareControlledCpuPage(',
               '    void MapPages(',
               '    template <typename T>\n    inline PreparedCpuWriteResult Write(',
               '    template <typename T>\n    inline PreparedCpuWriteResult\n    WriteCached(',
               '    template <typename T>\n    [[nodiscard]] bool TryBoundCpuWrite(',
               '    template <typename T>\n    inline void Write(']:
    cpu += method('src/core/memory.cpp', marker) + '\n'
cpu += '};\n'
cpu += '''
namespace Core::Memory {
struct Memory {
    Cpu* impl;
    std::shared_ptr<CpuWriteOwner> controlled_owner{std::make_shared<CpuWriteOwner>(this)};
    void SealControlledCpuWrites();
    ~Memory() { SealControlledCpuWrites(); }
    std::optional<PreparedCpuPage> PrepareControlledCpuPage(Common::ProcessAddress) const;
    ControlledCpuWriteRequestPtr PrepareControlledWrite(Common::ProcessAddress,ControlledCpuWriteRequest::Value);
'''
for bits in [8, 16, 32, 64]:
    cpu += f'    ControlledCpuWriteRequestPtr PrepareControlledWrite{bits}(Common::ProcessAddress,u{bits});\n'
    cpu += f'    void Write{bits}(Common::ProcessAddress,u{bits});\n'
    cpu += f'    PreparedCpuWriteResult WriteOrdinaryControlled{bits}(const PreparedCpuPage&,Common::PhysicalRegistrationBundle::CachedWrite&,Common::ProcessAddress,u{bits});\n'
cpu += '};\n'
cpu += method('src/core/memory.cpp', 'void Memory::SealControlledCpuWrites()', closing='\n}') + '\n'
cpu += method('src/core/memory.cpp', 'std::optional<PreparedCpuPage> Memory::PrepareControlledCpuPage(', closing='\n}') + '\n'
cpu += method('src/core/memory.cpp', 'ControlledCpuWriteRequestPtr Memory::PrepareControlledWrite(', closing='\n}') + '\n'
for bits in [8, 16, 32, 64]:
    cpu += method('src/core/memory.cpp', f'ControlledCpuWriteRequestPtr Memory::PrepareControlledWrite{bits}(', closing='\n}') + '\n'
    cpu += method('src/core/memory.cpp', f'void Memory::Write{bits}(', closing='\n}') + '\n'
    cpu += method('src/core/memory.cpp', f'PreparedCpuWriteResult Memory::WriteOrdinaryControlled{bits}(', closing='\n}') + '\n'
cpu += '}\n'
legacy = r'''
#include <bit>
#define LOG_ERROR(...) ((void)0)
#define CITRON_PROFILE_MEM_SCOPE(...) ((void)0)
struct LegacyCpu {
    Cpu& cpu;
    template<class T> bool TryBoundCpuWrite(Common::ProcessAddress,const T&) {return false;} // Binding disabled for the defect control.
    template<class Unmapped,class Cached> u8* GetPointerImpl(u64 address,Unmapped,Cached cached) {
        // Resolver collaborator: retain the initially uncached target, then pause immediately
        // before the actual ordinary Memory::Write memcpy. It holds no prepared execution guard.
        const auto mapping=cpu.current_page_table->SnapshotMapping(address&~u64{4095});
        assert(mapping);
        auto* pointer=cpu.system.device.buffer.BackingBasePointer()+
            mapping->physical_page-DramMemoryMap::Base+(address&4095);
        if(mapping->type==Common::PageType::RasterizerCachedMemory) cached();
        Common::ObserveCpuStore();
        return pointer;
    }
    void HandleRasterizerWrite(u64,u64) {assert(false);}
'''
legacy += method('src/core/memory.cpp',
                 '    template <typename T>\n    inline void Write(') + '\n};\n'

physical = read('src/common/physical_cpu_page.h')
store = '        std::memcpy(storage.Bytes().data() + offset, &value, sizeof(T));'
assert physical.count(store) == 1
(stubs / 'common/physical_cpu_page.h').write_text(
    'namespace Common {void ObserveCpuStore();}\n' +
    physical.replace(store, '        ObserveCpuStore();\n' + store))
bundle = read('src/common/physical_registration_bundle.h')
cached_store = '        std::memcpy(page.storage.Bytes().data() + offset, &value, sizeof(T));'
assert bundle.count(cached_store) == 1
instrumented_bundle = bundle.replace(cached_store, '        ObserveCpuStore();\n' + cached_store)
(stubs / 'common/physical_registration_bundle.h').write_text(instrumented_bundle)
gate = read('src/common/physical_page_gate.h')
assert gate.count('            Publication result;') == 1
(stubs / 'common/physical_page_gate.h').write_text(
    'namespace Common {void ObserveSnapshot();}\n' + gate.replace(
        '            Publication result;', '            ObserveSnapshot();\n            Publication result;'))

prefix = r'''
#include <array>
#include <map>
#include <stdexcept>
#include <vector>
#include <boost/container/small_vector.hpp>
#include "video_core/buffer_cache/memory_tracker_base.h"
using DAddr=u64;
constexpr u64 DEVICE_PAGESIZE=4096, C=65536, A=65536, ALIAS=3*C, B=0x80000000;
using Result=Core::Memory::PreparedCpuWriteResult;
using Bundle=Common::PhysicalRegistrationBundle;
using BufferId=size_t;
constexpr BufferId NULL_BUFFER_ID=0;
bool count_hot_allocations=false;
size_t hot_allocations=0;
std::function<void()> snapshot_hook;
namespace Common {void ObserveSnapshot() {if(snapshot_hook) snapshot_hook();}}
struct DeviceTracker {
    Cpu& cpu;
    int markings=0;
    std::function<void()> before_mark=[] {};
    void UpdatePagesCachedCount(u64 address,u64 size,int delta) {
        before_mark(); ++markings;
        const auto mutation=cpu.current_page_table->MutateMappings();
        const auto relative=address>=ALIAS?address-ALIAS:address-A;
        // Device alias/process fanout is a fixture; PTE storage/mutation tokens are real.
        for(u64 off=relative;off<relative+size;off+=4096) {
            for(u64 base:{u64{4096},u64{32*4096}}) {
                auto& entry=cpu.current_page_table->entries[(base+off)/4096];
                const auto pointer=entry.pointer.Pointer();
                entry.pointer.Store(pointer,delta>0?Common::PageType::RasterizerCachedMemory:
                                                   Common::PageType::Memory);
            }
        }
    }
};
struct BufferCopy {u64 src_offset,dst_offset,size;};
struct FakeBuffer {
    u64 base;
    std::vector<u8> bytes=std::vector<u8>(C,0);
    size_t lru{};
    u64 CpuAddr() const {return base;}
    u64 SizeBytes() const {return bytes.size();}
    void setLRUID(size_t n) {lru=n;}
    size_t getLRUID() const {return lru;}
    void ImmediateUpload(u64 offset,std::span<const u8> input) {
        assert(offset<=bytes.size() && input.size()<=bytes.size()-offset);
        std::memcpy(bytes.data()+offset,input.data(),input.size());
    }
};
struct Lru {size_t Insert(size_t id,u64) {return id;} void Free(size_t) {}}
;
template<class P> struct BufferCache {
    using Buffer=FakeBuffer;
    DeviceTracker device_tracker;
    VideoCommon::MemoryTrackerBase<DeviceTracker> memory_tracker;
    struct DeviceMemory {
        Common::HostMemory& backing;
        template<class T> T* GetPointer(u64 address) {
            auto offset=address>=ALIAS?address-ALIAS:address-A;
            assert(offset<C);return reinterpret_cast<T*>(backing.BackingBasePointer()+offset);
        }
        void ReadBlockUnsafe(u64 address,void* dest,u64 size,const char*) {
            std::memcpy(dest,GetPointer<u8>(address),size);
        }
    } device_memory;
    std::vector<FakeBuffer> slot_buffers{{0},{A},{ALIAS}};
    Lru lru_cache;
    u64 frame_tick=0,total_used_memory=0,buffer_count=0,large_buffer_memory=0,large_buffer_count=0;
    static constexpr u64 LARGE_BUFFER_THRESHOLD=8ULL<<20,CACHING_PAGESIZE=C;
    static constexpr bool USE_MEMORY_MAPS_FOR_UPLOADS=false;
    bool scatter_busy=false;
    struct Gather {bool scatter_pending=false;};
    std::map<u64,Gather> gather_buffers;
    std::map<u64,size_t> page_table;
    std::function<void()> before_upload=[] {};
    explicit BufferCache(Cpu& cpu):device_tracker{cpu},memory_tracker{device_tracker},
        device_memory{cpu.system.device.buffer} {}
    bool IsRegionPageTableAddressable(u64 address,u64 size) const {
        return size && address<1ULL<<39 && size<=(1ULL<<39)-address;
    }
    void SetPageBufferId(u64 page,size_t id) {page_table[page]=id;}
    bool IsRegionRegistered(u64 address,u64 size) {
        return size && page_table[address/C]!=0 && page_table[(address+size-1)/C]!=0;
    }
    bool IsDirectGuest(Buffer&) {return false;}
    void EndGuestCpuAccess(Buffer&) {}
    void NotifyGatherWrite(u64,u64) {}
    void GuestMemoryBarrier(bool) {assert(false);}
    bool IsRangeGranular(u64,u64) {return true;}
    std::vector<u8> scratch;
    std::span<u8> ImmediateBuffer(u64 size) {scratch.resize(size);return scratch;}
    void Register(BufferId);
    template<bool insert> void ChangeRegister(BufferId);
    bool SynchronizeBuffer(Buffer&,u64,u32);
    void CheckControlledSynchronization(u64,u64) {} // No production transaction is enrolled here.
    void UploadMemory(Buffer&,u64,u64,std::span<BufferCopy>);
    void ImmediateUploadMemory(Buffer&,u64,std::span<const BufferCopy>);
    void MappedUploadMemory(Buffer&,u64,std::span<BufferCopy>) {assert(false);}
    template<class Store> Result CommitControlledCpuWrite(Bundle::CachedWrite&,
        std::span<const DAddr>,u64,Store&&);
};
'''
cache = ''
for marker in ['void BufferCache<P>::Register(', 'void BufferCache<P>::ChangeRegister(',
               'bool BufferCache<P>::SynchronizeBuffer(', 'void BufferCache<P>::UploadMemory(',
               'void BufferCache<P>::ImmediateUploadMemory(',
               'BufferCache<P>::CommitControlledCpuWrite(']:
    text = read('src/video_core/buffer_cache/buffer_cache.h')
    start = text.index(marker)
    template_start = text.rfind('template <class P>', 0, start)
    cache += text[template_start:text.index('\n}', start) + 2] + '\n'
# The fixture's DeviceMemory is nested in its template; production's concrete device is not.
cache = cache.replace('device_memory.GetPointer<u8>', 'device_memory.template GetPointer<u8>')

tail = r'''
struct Scenario {
    System system;
    Common::PageTable table;
    Cpu cpu{system,&table};
    std::unique_ptr<BufferCache<int>> cache;
    std::vector<Common::PreparedPhysicalCpuPage> pages;
    Scenario() {
        table.Resize(20,12);
        cpu.MapPages(table,1,16,B,Common::PageType::Memory);
        cpu.MapPages(table,32,16,B,Common::PageType::Memory);
        std::memset(system.device.buffer.BackingBasePointer(),10,C);
        for(size_t i=0;i<16;++i) {
            auto page=system.device.buffer.PreparePhysicalCpuPage(i*4096);assert(page);
            pages.push_back(*page);
        }
        // The final alias must deduplicate to the SAME backing/page gate.
        pages.push_back(pages.back());
        cache=std::make_unique<BufferCache<int>>(cpu);
    }
    u8 backing(size_t offset) {return system.device.buffer.BackingBasePointer()[offset];}
    void upload() {
        cache->before_upload();
        cache->Register(1);cache->Register(2);
        cache->SynchronizeBuffer(cache->slot_buffers[1],A,C);
        cache->SynchronizeBuffer(cache->slot_buffers[2],ALIAS,C);
    }
    bool final(size_t offset,u8 expected) {
        return backing(offset)==77 && cache->slot_buffers[1].bytes[offset]==expected &&
            cache->slot_buffers[2].bytes[offset]==expected &&
            !cache->memory_tracker.IsRegionCpuModified(A,C) &&
            !cache->memory_tracker.IsRegionCpuModified(ALIAS,C) &&
            !cache->memory_tracker.IsRegionGpuModified(A,C);
    }
};
int writer_first(bool handshake,size_t page_index) {
    auto s=std::make_unique<Scenario>();const auto offset=page_index*4096+7;
    // Alternate real CPU VAs, including the physical alias on the last covered page.
    const u64 va=(page_index==15?32*4096:4096)+offset;
    auto view=s->cpu.PrepareControlledCpuPage(va&~u64{4095});assert(view);
    std::latch entered{1},release{1};
    before_cpu_store=[&]{entered.count_down();release.wait();};
    Result result=Result::Invalid;
    std::thread writer([&]{
        if(handshake) result=s->cpu.Write<u8>(*view,va,77);
        else {LegacyCpu legacy{s->cpu};legacy.Write<u8>(va,77);result=Result::Stored;}
    });
    entered.wait();
    std::optional<Bundle> registration;
    bool incorrectly_granted=false;
    if(handshake) {
        auto pending=Bundle::Begin(s->pages,std::make_shared<int>(1));assert(pending);
        registration.emplace(std::move(*pending));
        incorrectly_granted=registration->TryStart()!=Bundle::Start::Draining;
        assert(!s->cache->IsRegionRegistered(A,C) && s->cache->device_tracker.markings==0);
    }
    if(!handshake) {
        // The actual ordinary Write has retained its uncached pointer; real tracker state and
        // cached-tag fanout advance before that Write's actual memcpy. No handshake is used.
        s->upload();
    }
    // Release is independent of registration, upload and tracker callbacks.
    std::thread independent_release([&]{release.count_down();});
    writer.join();independent_release.join();before_cpu_store={};
    assert(result==Result::Stored);
    if(incorrectly_granted) {std::cerr<<"registration skipped second/last page writer\n";return 71;}
    if(handshake) {
        assert(registration->TryStart()==Bundle::Start::Granted);
        s->cache->device_tracker.before_mark=[&]{assert(registration->Granted());};
        s->cache->before_upload=[&]{assert(registration->Granted());assert(s->backing(offset)==77);};
        s->upload();
        assert(!registration->BeginCachedWrite());
        assert(registration->CompleteWithExternalEvidence()); // fixture synchronous copy ONLY
        if(!s->final(offset,77)) return 70;
    } else if(!s->final(offset,10)) return 73;
    return 0;
}
int registration_first() {
    auto s=std::make_unique<Scenario>();constexpr size_t offset=15*4096+7;
    auto early=s->cpu.PrepareControlledCpuPage(4096+15*4096);assert(early);
    auto registration=Bundle::Begin(s->pages,std::make_shared<int>(1));assert(registration);
    assert(registration->TryStart()==Bundle::Start::Granted);
    assert(s->cpu.Write<u8>(*early,4096+offset,77)==Result::NeedSlow && s->backing(offset)==10);
    assert(!registration->BeginCachedWrite());
    s->upload();
    assert(registration->CompleteWithExternalEvidence()); // fixture completion, NOT Vulkan
    auto view=s->cpu.PrepareControlledCpuPage(32*4096+15*4096);assert(view);
    assert(view->mapping.type==Common::PageType::RasterizerCachedMemory);
    assert(s->cpu.Write<u8>(*view,32*4096+offset,77)==Result::NeedSlow);
    auto write=registration->BeginCachedWrite();assert(write);
    assert(write->TryStart()==Common::PhysicalRangeAdmission::Start::Granted);
    const std::array<DAddr,2> aliases{A+offset,ALIAS+offset};
    // Stale prepared CPU permission is rejected even though storage remains pinned/admitted.
    const auto stale=s->cache->CommitControlledCpuWrite(*write,aliases,1,[&] {
        return s->cpu.WriteCached<u8>(*early,*write,4096+offset,77);
    });
    assert(stale==Result::Stale && s->backing(offset)==10);
    assert(!s->cache->memory_tracker.IsRegionCpuModified(A+offset,1));
    // Unsupported GPU-dirty preservation must refuse before invoking an actual store.
    s->cache->memory_tracker.MarkRegionAsGpuModified(A+offset,1);
    bool invoked=false;
    assert(s->cache->CommitControlledCpuWrite(*write,aliases,1,[&] {
        invoked=true;return s->cpu.WriteCached<u8>(*view,*write,32*4096+offset,77);
    })==Result::NeedSlow);
    assert(!invoked && s->backing(offset)==10);
    s->cache->memory_tracker.UnmarkRegionAsGpuModified(A+offset,1);
    // Snapshot ownership and a tracked store never overlap, and neither spins.
    snapshot_hook=[&] {
        if(s->cpu.WriteCached<u8>(*view,*write,32*4096+offset,77)!=Result::NeedSlow) {
            std::cerr<<"cached write overlapped cumulative observation\n";std::exit(75);
        }
    };
    assert(view->physical.Published());snapshot_hook={};
    before_cpu_store=[&]{assert(!view->physical.Published());};
    const auto result=s->cache->CommitControlledCpuWrite(*write,aliases,1,[&] {
        assert(write->Granted());
        const auto result=s->cpu.WriteCached<u8>(*view,*write,32*4096+offset,77);
        assert(s->backing(offset)==77);
        // No pre-store tracker mutation stands in for a successful actual store.
        assert(!s->cache->memory_tracker.IsRegionCpuModified(A+offset,1));
        return result;
    });
    before_cpu_store={};
    assert(result==Result::Stored && s->backing(offset)==77 && write->Granted());
    if(!s->cache->memory_tracker.IsRegionCpuModified(A+offset,1) ||
       !s->cache->memory_tracker.IsRegionCpuModified(ALIAS+offset,1)) {
        std::cerr<<"cached store 77 missing real tracker publication\n";return 72;
    }
    // An actual conflicting reader must stay parked THROUGH post-store publication.
    const auto domain=s->pages.back().Epoch()->ranges;
    const std::array access{Common::PhysicalRangeAdmission::Access{
        15*4096,4096,Common::PhysicalRangeAdmission::Mode::Read}};
    auto reader=domain->Enqueue(access,std::make_shared<int>(1));assert(reader);
    assert(domain->TryStart(reader)==Common::PhysicalRangeAdmission::Start::Park);
    assert(write->CompleteAfterTrackerPublication());
    assert(domain->TryStart(reader)==Common::PhysicalRangeAdmission::Start::Granted);
    s->cache->SynchronizeBuffer(s->cache->slot_buffers[1],A,C);
    s->cache->SynchronizeBuffer(s->cache->slot_buffers[2],ALIAS,C);
    if(!s->final(offset,77)) return 74;
    assert(domain->Complete(reader));
    const auto publication=view->physical.Published();assert(publication && publication->sequence==1);
    for(size_t byte=0;byte<4096;++byte) assert(publication->Contains(byte)==(byte==7));
    return 0;
}
void publication_failure() {
    auto s=std::make_unique<Scenario>();constexpr size_t offset=7;
    auto registration=Bundle::Begin(s->pages,std::make_shared<int>(1));assert(registration);
    assert(registration->TryStart()==Bundle::Start::Granted);
    s->upload();assert(registration->CompleteWithExternalEvidence());
    auto view=s->cpu.PrepareControlledCpuPage(4096);assert(view);
    auto write=registration->BeginCachedWrite();assert(write);
    assert(write->TryStart()==Common::PhysicalRangeAdmission::Start::Granted);
    const std::array<DAddr,2> aliases{A+offset,ALIAS+offset};
    s->cache->device_tracker.before_mark=[] {throw std::runtime_error("publication failed");};
    bool failed=false;
    try {
        (void)s->cache->CommitControlledCpuWrite(*write,aliases,1,[&] {
            return s->cpu.WriteCached<u8>(*view,*write,4096+offset,77);
        });
    } catch(const std::runtime_error&) {failed=true;}
    assert(failed && s->backing(offset)==77 && !write->Granted());
    assert(!write->CompleteAfterTrackerPublication());
    const auto domain=s->pages.front().Epoch()->ranges;
    const std::array access{Common::PhysicalRangeAdmission::Access{
        0,4096,Common::PhysicalRangeAdmission::Mode::Read}};
    auto reader=domain->Enqueue(access,std::make_shared<int>(1));assert(reader);
    assert(domain->TryStart(reader)==Common::PhysicalRangeAdmission::Start::Park);
    assert(domain->Cancel(reader)); // Unknown footprint/resources intentionally remain retained.
}
int main(int argc,char** argv) {
    const int repeats=argc>1?std::atoi(argv[1]):1;
    for(int n=0;n<repeats;++n) {
        for(size_t page:{size_t{1},size_t{15}}) {
            if(auto error=writer_first(true,page)) return error;
            if(auto error=writer_first(false,page)) return error;
        }
        if(auto error=registration_first()) return error;
    }
    publication_failure();
    std::cout<<"PASS writer-first: backing=77 upload=77 tracker=clean; registration-first: NeedSlow -> actual cached store77 -> real tracker dirty -> upload77 -> tracker clean; disabled handshake: backing=77 upload=10 tracker=clean; repeat="<<repeats<<"\n";
}
'''
source = cpu + legacy + prefix + cache + tail
cpp = out / 'first_registration.cpp'
cpp.write_text(source)
cmd = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
       '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-I', str(stubs),
       '-I', str(root / 'src')]
if a.boost_include:
    cmd += ['-I', str(a.boost_include)]
cmd += [str(root / 'src/common/host_memory.cpp'), str(root / 'src/common/page_table.cpp'),
        str(root / 'src/common/virtual_buffer.cpp')]
if os.name == 'nt':
    cmd += [str(root / 'src/common/dynamic_library.cpp')]
exe = out / ('first_registration.exe' if os.name == 'nt' else 'first_registration')


def run(cpp_path):
    subprocess.run(cmd + [str(cpp_path), '-o', str(exe)], check=True)
    return subprocess.run([str(exe), str(a.repeat)], text=True, capture_output=True, timeout=60)


result = run(cpp)
print(result.stdout, end='')
if result.returncode:
    raise RuntimeError(f'first registration failed ({result.returncode}): {result.stderr}')
publication = '        memory_tracker.MarkRegionAsCpuModified(address, size);'
assert source.count(publication) == 1
control = out / 'missing_tracker.cpp'
control.write_text(source.replace(publication, '        /* negative control: missing publication */'))
result = run(control)
if result.returncode != 72 or 'missing real tracker publication' not in result.stderr:
    raise RuntimeError(f'missing tracker control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative control: actual cached store77 without real tracker publication fails (72)')
marker = '        for (const auto& page : resources->pages) {\n            if (!page.gate->CloseForRegistration())'
assert bundle.count(marker) == 1
(stubs / 'common/physical_registration_bundle.h').write_text(
    instrumented_bundle.replace(marker, '        resources->pages.erase(resources->pages.begin()+1,resources->pages.end());\n' + marker))
try:
    result = run(cpp)
    if result.returncode != 71 or 'skipped second/last page writer' not in result.stderr:
        raise RuntimeError(f'start-page-only control failed incorrectly: {result.returncode} {result.stderr}')
finally:
    (stubs / 'common/physical_registration_bundle.h').write_text(instrumented_bundle)
print('PASS negative control: start-page-only closure misses the independently entered writer (71)')
observation = '        if (page.gate->observation.test_and_set(std::memory_order_acquire))'
assert instrumented_bundle.count(observation) == 1
(stubs / 'common/physical_registration_bundle.h').write_text(
    instrumented_bundle.replace(observation, '        if (false)'))
try:
    result = run(cpp)
    if result.returncode != 75 or 'overlapped cumulative observation' not in result.stderr:
        raise RuntimeError(f'observation control failed incorrectly: {result.returncode} {result.stderr}')
finally:
    (stubs / 'common/physical_registration_bundle.h').write_text(instrumented_bundle)
print('PASS negative control: missing observation exclusion permits a tracked store during snapshot (75)')
for path in ['src/common/physical_page_gate.h', 'src/common/physical_range_admission.h',
             'src/core/controlled_cpu_page.h', 'src/common/host_memory.cpp',
             'src/common/page_table.cpp', 'src/video_core/buffer_cache/memory_tracker_base.h',
             'src/video_core/buffer_cache/word_manager.h']:
    inputs.add(path)
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sorted(inputs)},
    'real': ['HostMemory and PageTable', 'prepared, ordinary Memory::Write and WriteCached bodies',
             'physical page/range admission and exact byte publication',
             'BufferCache Register/ChangeRegister, SynchronizeBuffer/UploadMemory/ImmediateUploadMemory',
             'MemoryTrackerBase and WordManager CPU/GPU/tracked bits'],
    'fixtures': ['buffer allocation, LRU/statistics and synchronous upload/completion',
                 'stable device aliases, legacy pointer resolver and cached-tag fanout',
                 'no real Vulkan driver, no production first-registration caller installed'],
}, indent=2))
