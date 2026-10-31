# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile current split-buffer helpers against a deterministic fake GPU runtime."""

import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description="Exercise split-buffer helpers with fake GPU memory.")
parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
parser.add_argument("--build-dir", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
p = args.build_dir or root / ".diagnostics" / "gather-buffer-tests"
p.mkdir(parents=True, exist_ok=True)
s = (root / 'src/video_core/buffer_cache/buffer_cache.h').read_text(encoding='utf-8')


def func(marker):
    marker_start = s.find(marker)
    if marker_start < 0:
        raise SystemExit(f"Cannot extract {marker!r}: requested marker is missing")
    start = s.rfind('template <class P>', 0, marker_start)
    if start < 0:
        raise SystemExit(f"Cannot extract {marker!r}: preceding template header is missing")
    closing = s.find('\n}\n', marker_start)
    if closing < 0:
        raise SystemExit(f"Cannot extract {marker!r}: closing brace is missing")
    end = closing + 3
    return s[start:end]


head = r'''

#include "video_core/buffer_cache/direct_guest_mapping.h"
#include "video_core/buffer_cache/gather_alias.h"
#include "video_core/buffer_cache/gpu_write_revision.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#define LOG_INFO(...) ((void)0)
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
using GPUVAddr = u64;
using DAddr = u64;
using BufferId = size_t;
constexpr u64 operator""_MiB(unsigned long long x) {
    return x * 1024 * 1024;
}
constexpr u64 operator""_KiB(unsigned long long x) {
    return x * 1024;
}
namespace boost::container {
template <class T, size_t N>
using small_vector = std::vector<T>;
}
namespace Core {
constexpr u64 DEVICE_PAGESIZE = 4;
}
constexpr u64 DEVICE_PAGESIZE = Core::DEVICE_PAGESIZE;
namespace Common {
template <class T>
T AlignUp(T x, u64 a) {
    return (x + a - 1) / a * a;
}
template <class T>
T AlignDown(T x, u64 a) {
    return x / a * a;
}
} // namespace Common
namespace VideoCore {
struct RasterizerDownloadArea {
    DAddr start_address = 0, end_address = 0;
    bool preemtive = false;
};
} // namespace VideoCore
struct ScopeExit {
    std::function<void()> f;
    ~ScopeExit() {
        f();
    }
};
struct ScopeMaker {
    template <class F>
    ScopeExit operator+(F f) {
        return {f};
    }
};
#define SCOPE_EXIT auto scope_exit = ScopeMaker{} + [&]()
struct BufferCopy {
    u64 src_offset, dst_offset, size;
};
using namespace VideoCommon;
namespace Common {
struct GuestBackingRange { int fd; u64 offset, allocation_size; };
}
namespace Settings {
inline bool nce_enabled = false;
inline bool IsNceEnabled() { return nce_enabled; }
struct Flag { bool enabled = false; bool GetValue() const { return enabled; } };
struct Values { Flag use_uma_zero_copy; };
inline Values values;
}
struct Bytes {
    std::vector<u8> storage;
    u8* alias = nullptr;
    explicit Bytes(u64 size = 0) : storage(size) {}
    u64 size() const { return storage.size(); }
    u8* data() { return alias ? alias : storage.data(); }
    u8* begin() { return data(); }
    u8* end() { return data() + size(); }
    u8& operator[](u64 offset) { return data()[offset]; }
};
struct Runtime;
struct FakeBuffer {
    VideoCommon::GpuWriteRevision gpu_write_revision;
    static inline bool fail_next = false;
    DAddr base = 0;
    std::vector<u64> page_gpu_tick;
    Bytes bytes;
    bool IsDirectGuest() const { return bytes.alias != nullptr; }
    FakeBuffer() = default;
    FakeBuffer(Runtime&, DAddr d, u64 size) : base(d), page_gpu_tick((size + 4095) / 4096), bytes(size) {
        if (fail_next) {
            fail_next = false;
            throw std::bad_alloc();
        }
    }
    u64 SizeBytes() const {
        return bytes.size();
    }
    u32 Offset(DAddr d) {
        return u32(d - base);
    }
    u64 CpuAddr() const { return base; }
    bool IsInBounds(u64 address, u64 size) const {
        return address >= base && address - base <= SizeBytes() &&
               size <= SizeBytes() - (address - base);
    }
    void IncreaseStreamScore(int) {}
    int StreamScore() const { return 0; }
    void MarkUsage(u64, u64, u64) {}
};
struct Runtime {
    // This legacy fixture has no physical identity plan; retain the merge hook
    // and count its invocation instead of silently excluding the extracted body.
    size_t invalidated_physical_origins = 0;
    void InvalidatePhysicalOrigin(FakeBuffer&) { ++invalidated_physical_origins; }
    bool direct_enabled = false;
    std::vector<u8> backing = std::vector<u8>(16384);
    bool TryDirectGuestBuffer(FakeBuffer& b, Common::GuestBackingRange range) {
        if (!direct_enabled || range.fd != 1 || range.offset + b.SizeBytes() > backing.size()) return false;
        b.bytes.alias = backing.data() + range.offset;
        return true;
    }
    int barriers_to_cpu = 0, finishes = 0;
    void GuestMemoryBarrier(bool to_cpu) { barriers_to_cpu += to_cpu; }
    void Finish() { ++finishes; }
    bool fail_copy = false;
    u64 CurrentTick() const { return 20; }
    int copies = 0;
    std::vector<BufferCopy> writes;
    template <class C>
    void CopyBuffer(FakeBuffer& dst, FakeBuffer& src, const C& copies_, bool) {
        if (fail_copy) {
            fail_copy = false;
            throw std::runtime_error("copy failed");
        }
        for (auto c : copies_) {
            if (c.src_offset + c.size > src.bytes.size() ||
                c.dst_offset + c.size > dst.bytes.size())
                throw std::runtime_error("out of bounds");
            std::memmove(dst.bytes.data() + c.dst_offset, src.bytes.data() + c.src_offset, c.size);
            writes.push_back(c);
        }
        ++copies;
    }
    void ClearBuffer(FakeBuffer& b, u32 off, u64 size, u32 value) {
        std::fill(b.bytes.begin() + off, b.bytes.begin() + off + size, u8(value));
    }
};
struct Memory {
    u64 id = 1, generation = 2;
    std::shared_ptr<std::mutex> mapping_guard = std::make_shared<std::mutex>();
    auto AcquireMappingRead() const { return std::unique_lock{*mapping_guard}; }
    std::map<u64, u64> pages;
    u64 MapGeneration() {
        return generation;
    }
    u64 GetID() {
        return id;
    }
    std::optional<DAddr> GpuToCpuAddress(u64 x) {
        auto it = pages.find(Common::AlignDown(x, 4));
        if (it == pages.end())
            return {};
        return it->second + x % 4;
    }
    size_t MaxContinuousRange(u64 x, u64 size) {
        auto start = GpuToCpuAddress(x);
        if (!start)
            return 0;
        u64 count = std::min<u64>(4 - x % 4, size);
        while (count < size) {
            auto next = GpuToCpuAddress(x + count);
            if (!next || *next != *start + count)
                break;
            count += std::min<u64>(4, size - count);
        }
        return count;
    }
};
struct Tracker {
    std::set<u64> gpu, cpu, pre;
    bool IsRegionGpuModified(u64 d, u64 n) {
        for (u64 i = 0; i < n; ++i)
            if (gpu.contains(d + i))
                return true;
        return false;
    }
    bool IsRegionPreflushable(u64 d, u64 n) {
        for (u64 i = 0; i < n; ++i)
            if (pre.contains(d + i))
                return true;
        return false;
    }
    void MarkRegionAsGpuModified(u64 d, u64 n) {
        for (u64 i = 0; i < n; ++i)
            gpu.insert(d + i);
    }
    void MarkRegionAsCpuModified(u64 d, u64 n) {
        for (u64 i = 0; i < n; ++i) {
            cpu.insert(d + i);
            gpu.erase(d + i);
        }
    }
    void MarkRegionAsPreflushable(u64 d, u64 n) {
        for (u64 i = 0; i < n; ++i)
            pre.insert(d + i);
    }
};
struct RangeSet {
    std::vector<std::pair<u64, u64>> ranges;
    void Add(u64 addr, u64 size) { ranges.emplace_back(addr, addr + size); }
    void Subtract(u64, u64) {}
    template <class F> void ForEachInRange(u64 addr, u64 size, F f) {
        for (const auto& [lo, hi] : ranges) {
            const u64 start = std::max(lo, addr), end = std::min(hi, addr + size);
            if (start < end) f(start, end);
        }
    }
};
struct Binding {
    DAddr device_addr = 0;
    u32 size = 0;
    BufferId buffer_id = 0;
    GPUVAddr gather_gpu_addr = 0, gpu_addr = 0;
    u32 gather_size = 0, gather_offset = 0;
};
using TextureBufferBinding = Binding;
constexpr size_t NUM_STAGES = 2;
constexpr BufferId NULL_BUFFER_ID = 0;
template <class F>
void ForEachEnabledBit(u32 mask, F f) {
    for (u32 i = 0; i < 32; ++i)
        if (mask & (1u << i))
            f(i);
}
struct Channel {
    bool has_deleted_buffers = false;
    u32 enabled_compute_uniform_buffer_mask = 0, enabled_compute_storage_buffers = 0;
    u32 written_compute_storage_buffers = 0;
    std::array<Binding, 4> compute_uniform_buffers{}, compute_storage_buffers{};
    std::array<u32, NUM_STAGES> enabled_uniform_buffer_masks{}, enabled_storage_buffers{};
    std::array<u32, NUM_STAGES> written_storage_buffers{};
    std::array<std::array<Binding, 4>, NUM_STAGES> uniform_buffers{}, storage_buffers{};
    u32 enabled_compute_texture_buffers = 0, written_compute_texture_buffers = 0;
    std::array<Binding, 4> compute_texture_buffers{};
    std::array<u32, NUM_STAGES> enabled_texture_buffers{}, written_texture_buffers{};
    std::array<std::array<Binding, 4>, NUM_STAGES> texture_buffers{};
};
template <class P>
struct BufferCache {
    using Buffer = FakeBuffer;
    std::mutex mutex;
    struct DeviceMemory {
        u64 generation = 2;
        std::shared_ptr<std::mutex> mapping_guard = std::make_shared<std::mutex>();
        std::map<u64, u64> physical;
        u64 MapGeneration() const { return generation; }
        std::optional<u64> GetPhysicalAddressForAlias(u64 d) {
            auto it = physical.find(d);
            return it == physical.end() ? d : it->second;
        }
        template <typename Func> void ReadPhysicalAliases(Func&& query) {
            std::scoped_lock lock{*mapping_guard};
            query(MapGeneration(), [this](u64 d) { return GetPhysicalAddressForAlias(d); });
        }
        std::optional<Common::GuestBackingRange> GetDriverBackingRange(u64 d, [[maybe_unused]] u64 size) {
            auto it = physical.find(d);
            if (it == physical.end()) return {};
            return Common::GuestBackingRange{1, it->second, 16384};
        }
    } device_memory;
    Memory* gpu_memory;
    Runtime runtime;
    Tracker memory_tracker;
    Channel channel;
    Channel* channel_state = &channel;
    RangeSet gpu_modified_ranges, uncommitted_gpu_modified_ranges;
    struct Slots : std::deque<Buffer> {
        using std::deque<Buffer>::deque;
        bool contains(BufferId id) const { return id < this->size(); }
    };
    Slots slot_buffers{Buffer{}};
    std::map<u64, BufferId> ids;
    std::map<u64, u8> ram;
    struct Ring {
        std::vector<Buffer> retired;
        void Push(Buffer&& b) {
            retired.push_back(std::move(b));
        }
    } delayed_destruction_ring;
    struct GatherChunk {
        DAddr device_addr;
        u32 size, offset;
        u8 dirty;
    };
    struct GatherEntry {
        std::unique_ptr<Buffer> buffer;
        std::vector<GatherChunk> chunks;
        u64 last_used = 0;
        u32 prepared_size = 0;
        GuestMappingIdentity identity;
        u64 device_generation = 0;
        bool scatter_pending = false;
        bool aliases_checked = false;
    };
    void ValidateGatherAliases(std::span<const GatherAliasRange>);
    std::vector<GatherAliasRange> checked_alias_ranges;
    GuestMappingIdentity checked_alias_identity;
    u64 checked_alias_device_generation = ~u64{0};
    std::unordered_map<u64, GatherEntry> gather_buffers;
    u64 gather_used_memory = 0, frame_tick = 0;
    bool has_direct_guest_buffers = false;
    bool CpuWriteNeedsGpuThread(u64, u64) const;
    std::vector<u64> scatter_pending, pending_gather_writes;
    bool scatter_busy = false;
    u64 unmap_readback_count{}, unmap_readback_bytes{}, unmap_readback_time_ns{};
    u64 cpu_write_preserve_count{}, cpu_write_preserve_time_ns{};
    u64 scatter_skip = 0;
    struct ContiguityKey {
        u64 gpu_addr;
        u32 size;
        bool operator==(const ContiguityKey&) const = default;
    };
    struct ContiguityKeyHash {
        size_t operator()(const ContiguityKey& k) const {
            return k.gpu_addr ^ (u64(k.size) << 40);
        }
    };
    mutable std::unordered_map<ContiguityKey, bool, ContiguityKeyHash> contiguity_cache;
    mutable u64 contiguity_cache_generation = ~0ull;
    mutable size_t contiguity_cache_address_space = ~size_t{};
    static bool IsDirectGuest(const Buffer& b) { return b.IsDirectGuest(); }
    void GuestMemoryBarrier(bool to_cpu) { runtime.GuestMemoryBarrier(to_cpu); }
    int cpu_begins = 0, cpu_ends = 0;
    std::set<const Buffer*> cpu_owned;
    bool NeedsGuestCpuAccess(const Buffer& b) const { return !cpu_owned.contains(&b); }
    void BeginGuestCpuAccess(Buffer& b) {
        cpu_begins += b.IsDirectGuest();
        if (b.IsDirectGuest()) cpu_owned.insert(&b);
    }
    void EndGuestCpuAccess(Buffer& b) {
        cpu_ends += b.IsDirectGuest();
        cpu_owned.erase(&b);
    }
    void WaitGuestBacking(u64, u64);
    void ResolveCachedBinding(Binding&);
    int fallback_readbacks = 0;
    void TestReadback(Buffer&);
    template <class F> void ForEachBufferInRange(u64 d, u64 n, F f) {
        for (const auto& [address, id] : ids) {
            if (address < d + n && d < address + slot_buffers[id].SizeBytes()) f(id, slot_buffers[id]);
        }
    }
    void DeleteBuffer(BufferId id, bool = false) {
        for (auto it = ids.begin(); it != ids.end();) {
            if (it->second == id) it = ids.erase(it); else ++it;
        }
    }
    void DownloadMemory(u64, u64) { runtime.Finish(); }
    void DownloadBufferMemory(Buffer& buffer, u64 d, u64 n, bool = false) {
        for (u64 i = 0; i < n; ++i) {
            if (memory_tracker.gpu.contains(d + i) && !memory_tracker.cpu.contains(d + i)) {
                ram[d + i] = buffer.bytes[buffer.Offset(d + i)];
                memory_tracker.gpu.erase(d + i);
            }
        }
    }
    void MarkWrittenBuffer(BufferId, u64 d, u64 n) {
        NotifyGatherWrite(d, n);
        memory_tracker.MarkRegionAsGpuModified(d, n);
    }
    std::optional<bool> TryDirectDMACopy(u64, u64, u64);
    bool IsRegionPageTableAddressable(u64 d, u64 n) {
        return d != 0 && d + n < 1ull << 39;
    }
    bool IsRegionRegistered(u64, u64) {
        return true;
    }
    BufferId FindBuffer(u64 d, u32 size) {
        ResolveScatter(d, size);
        auto it = ids.find(d);
        if (it != ids.end())
            return it->second;
        BufferId id = slot_buffers.size();
        slot_buffers.emplace_back(runtime, d, size);
        for (u32 i = 0; i < size; ++i)
            slot_buffers.back().bytes[i] = ram[d + i];
        if (auto backing = device_memory.GetDriverBackingRange(d, size)) {
            has_direct_guest_buffers |= runtime.TryDirectGuestBuffer(slot_buffers.back(), *backing);
        }
        ids[d] = id;
        return id;
    }
    void TouchBuffer(Buffer&, BufferId) {}
    void SynchronizeBuffer(Buffer& b, u64 d, u32 size) {
        bool dirty = false;
        for (u32 i = 0; i < size; ++i)
            dirty |= memory_tracker.cpu.contains(d + i);
        if (dirty) {
            for (u32 i = 0; i < size; ++i) {
                b.bytes[b.Offset(d + i)] = ram[d + i];
                memory_tracker.cpu.erase(d + i);
            }
            NotifyGatherWrite(d, size);
        }
    }
    void ClearDownload(u64, u64) {}
    u64 WidenHotWrite(u64, u64 size) { return size; }
    void MarkGpuUse(Buffer&, u64, u64);
    bool IsRegionGpuModified(u64 d, u64 size) {
        return memory_tracker.IsRegionGpuModified(d, size);
    }
    int dma_copies = 0, dma_clears = 0;
    bool DMACopyImpl(u64, u64, u64) {
        ++dma_copies;
        return true;
    }
    bool DMAClear(u64, u64, u32) {
        ++dma_clears;
        return true;
    }
    Buffer& GatherBuffer(u64, u32, bool = true);
    void ResolveGatherChunks(u64, u32);
    void NotifyGatherWrite(u64, u64, u8 = 1, u64 = 0);
    void FlushGatherWrites();
    void ScatterGatherEntry(u64, GatherEntry&);
    void ResolveScatter(u64, u64);
    bool IsGpuRangeContiguous(u64, u32) const;
    void TrimGatherBuffers(bool);
    void InvalidateGatherMemory(u64, u64);
    void WriteMemory(u64, u64);
    void ResetGatherBindings(bool);
    void PrepareGatherBindings(bool);
    boost::container::small_vector<Binding*, 32> GatherBindings(bool);
    bool CanJoinDirectGuest(BufferId);
    void JoinOverlap(BufferId, BufferId, bool);
    bool DMACopySplit(u64, u64, u64);
    bool DMAClearSplit(u64, u64, u32);
    std::optional<VideoCore::RasterizerDownloadArea> GetFlushArea(u64, u64);
};
'''
names = [
    'GatherBindings', 'ResetGatherBindings', 'ValidateGatherAliases', 'PrepareGatherBindings', 'ResolveGatherChunks',
    'NotifyGatherWrite', 'GatherBuffer', 'FlushGatherWrites', 'ScatterGatherEntry',
    'ResolveScatter', 'IsGpuRangeContiguous', 'TrimGatherBuffers', 'InvalidateGatherMemory',
    'ResolveCachedBinding', 'CpuWriteNeedsGpuThread', 'WaitGuestBacking', 'WriteMemory', 'MarkGpuUse', 'CanJoinDirectGuest', 'JoinOverlap', 'TryDirectDMACopy', 'DMACopySplit', 'DMAClearSplit', 'GetFlushArea',
]
uma = (root / 'src/video_core/buffer_cache/uma.h').read_text(encoding='utf-8')
uma = uma[uma.index('namespace VideoCommon::Uma'): ] + '\nnamespace Uma = VideoCommon::Uma;\n'
parts = [head, uma] + [func('BufferCache<P>::' + name + '(') for name in names]
texture_pass = (root / 'src/shader_recompiler/ir_opt/texture_pass.cpp').read_text(encoding='utf-8')
descriptor_start = texture_pass.index('u32 DynamicDescriptorCount(')
descriptor_end = texture_pass.index('\n}\n', descriptor_start) + 3
parts.append('constexpr u32 DESCRIPTOR_SIZE = 8;\n' + texture_pass[descriptor_start:descriptor_end])
readback_start = s.index('    if (IsDirectGuest(buffer)) {', s.index('void BufferCache<P>::DownloadBufferMemory(Buffer& buffer, DAddr'))
readback_end = s.index('\n    }', readback_start) + len('\n    }')
parts.append('template <class P>\nvoid BufferCache<P>::TestReadback(Buffer& buffer) {\n' +
             s[readback_start:readback_end] + '\n    ++fallback_readbacks;\n}\n')
# Exercise the current reactive filtering lambda with a controlled download sink.
start = s.index('const auto add_preflushable =')
end = s.index('\n                        };', start) + len('\n                        };')
parts.append(r"""
std::vector<std::pair<u64, u64>> filter(Tracker& memory_tracker, u64 start, u64 end) {
    std::vector<std::pair<u64, u64>> result;
    auto add_download = [&](u64 lo, u64 hi) { result.emplace_back(lo, hi); };
""" + s[start:end] + '\nadd_preflushable(start, end); return result;\n}')
rasterizer = (root / 'src/video_core/renderer_vulkan/vk_rasterizer.cpp').read_text(encoding='utf-8')
start = rasterizer.index('VideoCore::RasterizerDownloadArea RasterizerVulkan::GetFlushArea')
end = rasterizer.index('\n}\n', start) + 3
parts.append(r"""
struct TextureCache {
 std::mutex mutex;
 std::optional<VideoCore::RasterizerDownloadArea> area;
 auto GetFlushArea(u64,u64){return area;}
};
struct RasterizerVulkan {
 std::shared_mutex shutdown_mutex;bool is_shutting_down=false;
 TextureCache texture_cache;BufferCache<int>& buffer_cache;
 explicit RasterizerVulkan(BufferCache<int>& cache):buffer_cache(cache){}
 VideoCore::RasterizerDownloadArea GetFlushArea(u64,u64);
};
""" + rasterizer[start:end])

main = r'''

void check(bool ok, const char* label) {
    if (!ok)
        throw std::runtime_error(label);
    std::cout << "PASS " << label << '\n';
}
Memory split(u64 id = 1) {
    Memory m;
    m.id = id;
    m.pages = {{64, 1008}, {68, 1004}, {72, 1000}};
    return m;
}
void seed(BufferCache<int>& c) {
    for (u64 d = 1000; d < 1012; ++d)
        c.ram[d] = u8(d - 990);
}
int main() {
    {
        Memory aliases;
        aliases.pages = {{64, 1000}, {68, 1000}};
        BufferCache<int> cache;
        cache.gpu_memory = &aliases;
        cache.channel.enabled_compute_uniform_buffer_mask = 1;
        cache.channel.compute_uniform_buffers[0] = Binding{
            .size=8, .gather_gpu_addr=64, .gpu_addr=64};
        bool rejected = false;
        try { cache.PrepareGatherBindings(true); }
        catch (const UnsupportedGatherMapping&) { rejected = true; }
        check(!rejected, "read-only uniform physical aliases do not require shader write alias semantics");
        seed(cache);
        auto& read_only = cache.GatherBuffer(64, 8, false);
        check(read_only.bytes[0] == read_only.bytes[4] && !cache.gather_buffers.at(64).aliases_checked &&
              !cache.gather_buffers.at(64).scatter_pending && cache.scatter_pending.empty(),
              "read-only gather materializes both aliases without enabling scatter");
        const int copies = cache.runtime.copies;
        rejected = false;
        try { cache.GatherBuffer(64, 8, true); }
        catch (const UnsupportedGatherMapping&) { rejected = true; }
        check(rejected && cache.runtime.copies == copies && !cache.gather_buffers.at(64).scatter_pending,
              "read-only cached gather revalidates and rejects a later writable binding");
        cache.channel.enabled_compute_storage_buffers = 1;
        cache.channel.compute_storage_buffers[0] = cache.channel.compute_uniform_buffers[0];
        cache.PrepareGatherBindings(true);
        check(true, "read-only compute storage aliases are accepted");
        cache.channel.written_compute_storage_buffers = 1;
        rejected = false;
        try { cache.PrepareGatherBindings(true); }
        catch (const UnsupportedGatherMapping&) { rejected = true; }
        check(rejected, "compute write mask cannot reuse read-only alias validation");
        cache.channel.enabled_uniform_buffer_masks[0] = 1;
        cache.channel.uniform_buffers[0][0] = cache.channel.compute_uniform_buffers[0];
        cache.PrepareGatherBindings(false);
        cache.channel.enabled_storage_buffers[0] = 1;
        cache.channel.storage_buffers[0][0] = cache.channel.compute_uniform_buffers[0];
        cache.PrepareGatherBindings(false);
        check(true, "read-only graphics storage aliases are accepted");
        cache.channel.written_storage_buffers[0] = 1;
        rejected = false;
        try { cache.PrepareGatherBindings(false); }
        catch (const UnsupportedGatherMapping&) { rejected = true; }
        check(rejected, "graphics write mask cannot reuse read-only alias validation");
    }
    {
        Memory mapping;
        mapping.pages = {{64, 1000}, {68, 1008}, {128, 1000}};
        BufferCache<int> cache;
        cache.gpu_memory = &mapping;
        cache.channel.enabled_compute_uniform_buffer_mask = 1;
        cache.channel.compute_uniform_buffers[0] = Binding{
            .size=5, .gather_gpu_addr=65, .gpu_addr=65};
        cache.channel.enabled_compute_storage_buffers = 1;
        cache.channel.written_compute_storage_buffers = 1;
        cache.channel.compute_storage_buffers[0] = Binding{.size=1, .gpu_addr=128};
        cache.PrepareGatherBindings(true);
        check(cache.channel.compute_uniform_buffers[0].gather_gpu_addr == 64,
              "read-only alignment padding is excluded from shader alias checks");
        cache.channel.compute_storage_buffers[0].size = 2;
        bool rejected = false;
        try { cache.PrepareGatherBindings(true); }
        catch (const UnsupportedGatherMapping&) { rejected = true; }
        check(rejected, "read-only scratch aliasing an actual contiguous GPU write still stops");
    }
    {
        Memory aliases;
        aliases.pages = {{64, 1000}, {68, 1000}};
        BufferCache<int> cache;
        cache.gpu_memory = &aliases;
        seed(cache);
        bool rejected = false;
        std::string reason;
        try { cache.GatherBuffer(64, 8); }
        catch (const UnsupportedGatherMapping& e) { rejected = true; reason = e.what(); }
        check(rejected && cache.runtime.copies == 0 && !cache.gather_buffers.at(64).buffer,
              "split physical aliases rejected before gather copies or shader dispatch");
        check(reason.find("physical_byte=1000") != std::string::npos &&
              reason.find("gpu_byte=68") != std::string::npos &&
              reason.find("other_gpu_byte=64") != std::string::npos &&
              reason.find("writable=1") != std::string::npos,
              "alias stop reports exact physical/GPU bytes and write flags");

        // Original 99-versus-10 regression: a pre-existing pending entry must
        // fail before either whole-chunk scatter, retaining the GPU-written 99.
        const auto id = cache.FindBuffer(1000, 4);
        auto& entry = cache.gather_buffers.at(64);
        entry.buffer = std::make_unique<FakeBuffer>(cache.runtime, 0, 8);
        std::fill(entry.buffer->bytes.begin(), entry.buffer->bytes.end(), 10);
        entry.buffer->bytes[0] = 99;
        entry.chunks = {{1000, 4, 0, 1}, {1000, 4, 4, 1}};
        entry.scatter_pending = true;
        cache.scatter_pending = {64};
        rejected = false;
        try { cache.ResolveScatter(0, 0); }
        catch (const UnsupportedGatherMapping&) { rejected = true; }
        check(rejected && cache.runtime.copies == 0 && entry.buffer->bytes[0] == 99 &&
              entry.scatter_pending && cache.scatter_pending == std::vector<u64>{64} &&
              !cache.scatter_busy && cache.slot_buffers[id].bytes[0] == 10,
              "original 99-versus-10 scatter rejected without copying or discarding pending writes");
        check(cache.CpuWriteNeedsGpuThread(1000, 4) && !cache.CpuWriteNeedsGpuThread(2000, 4),
              "pending fallback scatter routes only overlapping CPU writes to GPU thread");
        entry.device_generation = cache.device_memory.MapGeneration();
        rejected = false;
        try { cache.GatherBuffer(64, 4); }
        catch (const UnsupportedGatherMapping&) { rejected = true; }
        check(rejected && cache.runtime.copies == 0 && entry.buffer->bytes[0] == 99 &&
              entry.scatter_pending && !entry.aliases_checked,
              "valid smaller binding cannot bypass rejection of old pending aliased writes");
    }
    {
        Memory mapping;
        mapping.pages = {{64, 1000}, {68, 1004}, {128, 2000}, {132, 2008}};
        BufferCache<int> cache;
        cache.gpu_memory = &mapping;
        const std::array ranges{GatherAliasRange{64, 8, false}, GatherAliasRange{128, 8, true}};
        cache.ValidateGatherAliases(ranges);
        cache.device_memory.physical[2000] = 1000;
        cache.device_memory.generation += 2;
        bool rejected = false;
        try { cache.ValidateGatherAliases(ranges); }
        catch (const UnsupportedGatherMapping&) { rejected = true; }
        check(rejected && cache.runtime.copies == 0,
              "different device/GPU addresses aliasing physical memory invalidate cached validation");
        cache.channel.enabled_compute_uniform_buffer_mask = 1;
        cache.channel.enabled_compute_storage_buffers = 1;
        cache.channel.written_compute_storage_buffers = 1;
        cache.channel.compute_uniform_buffers[0] = Binding{.size=8, .gpu_addr=64};
        cache.channel.compute_storage_buffers[0] = Binding{
            .size=8, .gather_gpu_addr=128, .gpu_addr=128};
        rejected = false;
        try { cache.PrepareGatherBindings(true); }
        catch (const UnsupportedGatherMapping&) { rejected = true; }
        check(rejected && cache.runtime.copies == 0 && cache.gather_buffers.empty(),
              "scratch aliasing a contiguous binding rejected before preparing either GPU view");
        GatherAliasValidator validator{4};
        validator.Add(0, 64, 4, true);
        validator.Add(1, 65, 2, true); // same GPU byte, overlapping shared binding
        rejected = false;
        try { validator.Add(2, 128, 2, false); }
        catch (const UnsupportedGatherMapping&) { rejected = true; }
        check(rejected, "partial scratch-to-contiguous physical alias rejected");
    }
    auto m = split();
    BufferCache<int> c;
    c.gpu_memory = &m;
    seed(c);
    auto& scratch = c.GatherBuffer(64, 12);
    check(scratch.bytes[0] == 18 && scratch.bytes[4] == 14 && scratch.bytes[8] == 10,
          "descending physical chunks gathered in GPU order");
    check(!Uma::CanWriteDirectly(true, c.slot_buffers[c.ids.at(1008)].page_gpu_tick, 0, 4, 20),
          "gather GPU read prevents a concurrent direct UMA write");
    check(Uma::CanWriteDirectly(true, c.slot_buffers[c.ids.at(1008)].page_gpu_tick, 0, 4, 21),
          "gather source allows direct UMA writes after GPU completion");
    std::fill(scratch.bytes.begin(), scratch.bytes.end(), 99);
    c.pending_gather_writes = {64};
    c.FlushGatherWrites();
    c.GatherBuffer(64, 8);
    check(!Uma::CanWriteDirectly(true, c.slot_buffers[c.ids.at(1000)].page_gpu_tick, 0, 4, 20),
          "scatter GPU write prevents a concurrent direct UMA write");
    check(c.slot_buffers[c.ids.at(1000)].bytes[0] == 99 &&
              c.gather_buffers.at(64).buffer->bytes[0] == 99,
          "shrinking view preserves all old pending writes");
    std::fill(c.gather_buffers.at(64).buffer->bytes.begin(),
              c.gather_buffers.at(64).buffer->bytes.end(), 88);
    c.pending_gather_writes = {64};
    c.FlushGatherWrites();
    for (u64 d = 1008; d < 1012; ++d) {
        c.ram[d] = 42;
    }
    c.WriteMemory(1008, 4);
    auto& after = c.GatherBuffer(64, 8);
    check(after.bytes[0] == 42 && after.bytes[4] == 88,
          "CPU write overrides touched chunk while preserving other GPU writes");
    Memory a;
    a.id = 10;
    a.pages = {{64, 2000}, {68, 2004}};
    auto b = split(11);
    BufferCache<int> cache;
    cache.gpu_memory = &a;
    check(cache.IsGpuRangeContiguous(64, 8), "first address space contiguous");
    cache.gpu_memory = &b;
    check(!cache.IsGpuRangeContiguous(64, 8),
          "same generation in another address space cannot hit cache");
    auto g = split();
    BufferCache<int> views;
    views.gpu_memory = &g;
    seed(views);
    views.channel.enabled_compute_storage_buffers = 3;
    views.channel.compute_storage_buffers[0] = {1008, 8, 0, 0, 64};
    views.channel.compute_storage_buffers[1] = {1004, 8, 0, 0, 68};
    views.ResetGatherBindings(true);
    views.PrepareGatherBindings(true);
    auto x = views.channel.compute_storage_buffers[0], y = views.channel.compute_storage_buffers[1];
    check(x.gather_gpu_addr == 64 && y.gather_gpu_addr == 64 && x.gather_size == 12 &&
              y.gather_offset == 4,
          "overlapping writable views share allocation with original offsets");
    auto& shared = views.GatherBuffer(x.gather_gpu_addr, x.gather_size);
    shared.bytes[y.gather_offset] = 77;
    check(views.GatherBuffer(y.gather_gpu_addr, y.gather_size).bytes[4] == 77,
          "second view observes same-command scratch writes");
    views.pending_gather_writes = {64};
    views.FlushGatherWrites();
    views.InvalidateGatherMemory(1004, 4);
    check(views.gather_buffers.empty() && views.gather_used_memory == 0 &&
              !views.delayed_destruction_ring.retired.empty(),
          "unmap releases scratch through deferred retirement");
    g.pages.erase(68);
    ++g.generation;
    ++g.generation;
    BufferCache<int> holes;
    holes.gpu_memory = &g;
    seed(holes);
    auto& hole = holes.GatherBuffer(65, 7);
    check(hole.bytes[0] == 19 && hole.bytes[3] == 0 && hole.bytes[6] == 0,
          "unaligned unmapped hole is skipped and zero filled");
    auto trim_mem = split();
    BufferCache<int> trim;
    trim.gpu_memory = &trim_mem;
    for (u64 i = 0; i < 140; ++i) {
        trim_mem.pages[100 + i * 4] = 5000 + i * 8;
        trim.GatherBuffer(100 + i * 4, 4);
    }
    trim.TrimGatherBuffers(false);
    check(trim.gather_buffers.size() == 128, "scratch cache entry budget enforced");
    trim.TrimGatherBuffers(true);
    check(trim.gather_buffers.empty() && trim.gather_used_memory == 0,
          "critical pressure retires scratch allocations");

    Memory growth_mem;
    growth_mem.pages = {{64, 1008}};
    BufferCache<int> growth;
    growth.gpu_memory = &growth_mem;
    seed(growth);
    growth.GatherBuffer(64, 4);
    auto& larger = growth.GatherBuffer(64, 8);
    check(larger.bytes[0] == 18 && larger.bytes[4] == 0,
          "growing into a hole preserves mapped data with unchanged layout");
    std::fill(larger.bytes.begin(), larger.bytes.end(), 91);
    growth.pending_gather_writes = {64};
    growth.FlushGatherWrites();
    check(growth.GatherBuffer(64, 12).bytes[0] == 91,
          "growth into a hole scatters pending writes before replacing allocation");
    auto* old_buffer = growth.gather_buffers.at(64).buffer.get();
    auto old_bytes = growth.gather_used_memory;
    FakeBuffer::fail_next = true;
    bool allocation_failed = false;
    try {
        growth.GatherBuffer(64, 16);
    } catch (const std::bad_alloc&) {
        allocation_failed = true;
    }
    check(allocation_failed && growth.gather_buffers.at(64).buffer.get() == old_buffer &&
              growth.gather_used_memory == old_bytes,
          "allocation failure retains previous scratch and memory accounting");
    const auto scatter_dst = growth.FindBuffer(1008, 4);
    const auto scatter_revision = growth.slot_buffers[scatter_dst].gpu_write_revision.Snapshot();
    growth.pending_gather_writes = {64};
    growth.FlushGatherWrites();
    growth.runtime.fail_copy = true;
    bool copy_failed = false;
    try {
        growth.ResolveScatter(0, 0);
    } catch (const std::runtime_error&) {
        copy_failed = true;
    }
    check(copy_failed && !growth.scatter_busy && growth.gather_buffers.at(64).scatter_pending,
          "failed scatter preserves pending writes and releases recursion guard");
    check(growth.slot_buffers[scatter_dst].gpu_write_revision.Snapshot() != scatter_revision,
          "failed scatter changes destination write intent before uncertain copy effects");
    growth.ResolveScatter(0, 0);
    check(!growth.gather_buffers.at(64).scatter_pending, "failed scatter can be retried");
    check(DynamicDescriptorCount(512, 3, 32) == 32,
          "descriptor base at configured count boundary retains dynamic indexing");
    check(DynamicDescriptorCount(8192, 3, 32) == 32,
          "descriptor base beyond configured limit is valid within constant buffer");
    check(DynamicDescriptorCount(65528, 3, 32) == 1 && DynamicDescriptorCount(65520, 3, 32) == 2,
          "actual constant-buffer end limits descriptor count");
    auto direct_memory = split();
    direct_memory.pages = {{64, 1000}, {68, 1008}};
    BufferCache<int> direct_cache;
    direct_cache.gpu_memory = &direct_memory;
    direct_cache.runtime.direct_enabled = true;
    direct_cache.device_memory.physical = {{1000, 100}, {1008, 104}, {3000, 200}, {3008, 204}};
    direct_cache.runtime.backing[100] = 11;
    auto& direct_view = direct_cache.GatherBuffer(64, 8);
    check(direct_view.IsDirectGuest() && direct_cache.runtime.copies == 0 && direct_view.bytes[0] == 11,
          "physically contiguous split mapping selects shared backing without gather copies");
    direct_view.bytes[4] = 93;
    direct_cache.pending_gather_writes = {64};
    direct_cache.FlushGatherWrites();
    direct_cache.ResolveScatter(0, 0);
    check(direct_cache.runtime.backing[104] == 93 && direct_cache.runtime.copies == 0,
          "GPU writes are preserved through shared backing without scatter");
    direct_cache.GatherBuffer(64, 4);
    check(direct_cache.runtime.backing[104] == 93, "binding shrink does not discard GPU writes");
    direct_memory.pages[72] = 1016;
    direct_cache.device_memory.physical[1016] = 108;
    direct_memory.generation += 2;
    direct_cache.GatherBuffer(64, 12);
    check(direct_cache.runtime.backing[104] == 93 && !direct_cache.delayed_destruction_ring.retired.empty(),
          "binding growth preserves GPU writes and retains old direct allocation");
    direct_memory.id = 77;
    direct_memory.generation += 2;
    direct_cache.GatherBuffer(64, 8);
    check(direct_cache.gather_buffers.at(64).identity.address_space == 77,
          "identical guest VA in another address space rebuilds its view");
    direct_cache.device_memory.generation += 2;
    direct_cache.GatherBuffer(64, 8);
    check(direct_cache.gather_buffers.at(64).device_generation == direct_cache.device_memory.generation,
          "device mapping generation invalidates direct views");
    Settings::values.use_uma_zero_copy.enabled = true;
    direct_memory.pages[128] = 3000;
    direct_memory.pages[132] = 3008;
    check(direct_cache.TryDirectDMACopy(64, 128, 8) == std::optional<bool>{true} &&
          direct_cache.runtime.backing[204] == 93,
          "split DMA reuses direct mappings on both sides");
    const int before_overlap = direct_cache.runtime.copies;
    check(direct_cache.TryDirectDMACopy(64, 68, 8) == std::optional<bool>{false} &&
          direct_cache.runtime.copies == before_overlap && direct_cache.runtime.finishes != 0,
          "overlapping backing DMA falls back before any copies and waits for GPU");
    // Another guest VA aliases physical source bytes although DAddr ranges differ.
    direct_memory.pages[160] = 4000;
    direct_cache.device_memory.physical[4000] = 100;
    check(direct_cache.TryDirectDMACopy(64, 160, 4) == std::optional<bool>{false},
          "DMA detects physical aliases across different guest VAs");
    direct_cache.device_memory.physical.erase(3008);
    const int before_missing = direct_cache.runtime.copies;
    check(direct_cache.TryDirectDMACopy(64, 128, 8) == std::nullopt &&
          direct_cache.runtime.copies == before_missing,
          "one unsupported DMA segment prevents partial direct copy");
    const int before_fallback = direct_cache.runtime.copies;
    check(!direct_cache.DMACopySplit(64, 128, 8) && direct_cache.dma_copies == 0 &&
          direct_cache.runtime.copies == before_fallback,
          "unsupported whole DMA plan does not retry fast paths or partially copy fallback pieces");
    direct_memory.generation |= 1;
    check(direct_cache.TryDirectDMACopy(64, 128, 8) == std::nullopt,
          "odd GPU mapping generation declines direct DMA");
    direct_memory.generation += 1;
    direct_cache.device_memory.generation |= 1;
    check(direct_cache.TryDirectDMACopy(64, 128, 8) == std::nullopt,
          "odd device mapping generation declines direct DMA");
    direct_cache.device_memory.generation += 1;
    check(direct_cache.TryDirectDMACopy(999, 128, 8) == std::nullopt,
          "unmapped DMA piece declines direct DMA without forcing a guest snapshot");
    Settings::values.use_uma_zero_copy.enabled = false;
    BufferCache<int> fallback;
    fallback.gpu_memory = &direct_memory;
    auto& scratch_fallback = fallback.GatherBuffer(64, 8);
    check(!scratch_fallback.IsDirectGuest() && fallback.runtime.copies != 0,
          "unsupported memory keeps gather/scatter fallback");
    BufferCache<int> mirror;
    mirror.gpu_memory = &direct_memory;
    mirror.runtime.direct_enabled = true;
    mirror.device_memory.physical = {{1000, 100}, {1008, 104}};
    auto& mirror_view = mirror.GatherBuffer(64, 8);
    mirror_view.bytes[0] = 81;
    mirror.pending_gather_writes = {64};
    mirror.FlushGatherWrites();
    // Simulate a broader cache allocation becoming a scratch/fallback mirror.
    mirror.slot_buffers[mirror.ids.at(1000)].bytes.alias = nullptr;
    mirror.ResolveScatter(0, 0);
    check(mirror.slot_buffers[mirror.ids.at(1000)].bytes[0] == 81,
          "direct writes refresh a later staged mirror without losing shared guest writes");
    check(!mirror.GatherBuffer(64, 8).IsDirectGuest(),
          "direct view downgrades safely when a cached segment becomes a staged mirror");
    BufferCache<int> joined;
    joined.gpu_memory = &direct_memory;
    const auto old_id = joined.FindBuffer(1000, 8);
    auto& old_staged = joined.slot_buffers[old_id];
    std::fill(old_staged.bytes.begin(), old_staged.bytes.end(), 12);
    std::fill(old_staged.bytes.begin() + 4, old_staged.bytes.end(), 91);
    joined.runtime.direct_enabled = true;
    joined.device_memory.physical[1000] = 100;
    joined.runtime.backing[100] = 77; // CPU data is newer than the clean staged bytes.
    joined.slot_buffers.emplace_back(joined.runtime, 1000, 16);
    const auto new_id = joined.slot_buffers.size() - 1;
    joined.runtime.TryDirectGuestBuffer(joined.slot_buffers[new_id], {1, 100, 16384});
    joined.has_direct_guest_buffers = true;
    joined.gpu_modified_ranges.Add(1004, 4);
    check(joined.CanJoinDirectGuest(old_id), "aligned staged dirty ranges allow direct joins");
    joined.JoinOverlap(new_id, old_id, false);
    check(joined.runtime.invalidated_physical_origins == 1,
          "merge invalidates unproved physical representation inheritance");
    check(joined.runtime.backing[100] == 77 && joined.runtime.backing[104] == 91 &&
          joined.runtime.writes.size() == 1 && joined.runtime.writes.back().size == 4,
          "staged to direct overlap copies only GPU-written bytes and preserves newer CPU data");
    joined.ids[1000] = new_id;
    joined.WaitGuestBacking(1000, 8);
    check(joined.runtime.finishes == 1 && joined.runtime.barriers_to_cpu == 1 && joined.cpu_begins == 1,
          "merged GPU writes require barrier and completion before CPU access resumes");
    joined.WaitGuestBacking(1000, 8);
    check(joined.runtime.finishes == 1 && joined.runtime.barriers_to_cpu == 1 && joined.cpu_begins == 1,
          "repeated CPU writes without intervening GPU use do not repeat queue waits");
    joined.MarkGpuUse(joined.slot_buffers[new_id], 0, 8);
    joined.WaitGuestBacking(1000, 8);
    check(joined.runtime.finishes == 2 && joined.runtime.barriers_to_cpu == 2 && joined.cpu_begins == 2,
          "GPU reuse restores the barrier and completion requirement for the next CPU write");
    BufferCache<int> unaligned;
    const auto unaligned_id = unaligned.FindBuffer(1000, 8);
    unaligned.gpu_modified_ranges.Add(1001, 2);
    check(!unaligned.CanJoinDirectGuest(unaligned_id),
          "sub-word GPU dirty ranges retain staged fallback instead of overwriting CPU bytes");
    auto cached_memory = split();
    BufferCache<int> cached;
    cached.gpu_memory = &cached_memory;
    auto& cached_scratch = cached.GatherBuffer(64, 8);
    cached_scratch.bytes[0] = 67;
    cached.pending_gather_writes = {64};
    cached.FlushGatherWrites();
    Binding cached_vertex{1008, 4, cached.ids.at(1008)};
    cached.ResolveCachedBinding(cached_vertex);
    check(cached.slot_buffers[cached_vertex.buffer_id].bytes[0] == 67 &&
          !cached.gather_buffers.at(64).scatter_pending,
          "cached binding resolves pending scatter before retrieving its buffer");
    auto other_manager = direct_memory;
    other_manager.pages[64] = 1008;
    direct_cache.gpu_memory = &other_manager;
    direct_cache.GatherBuffer(64, 4);
    check(direct_cache.gather_buffers.at(64).identity.memory_manager == &other_manager &&
          direct_cache.gather_buffers.at(64).buffer->bytes[0] == 93,
          "different MemoryManager with identical VA and generation revalidates backing");
    direct_cache.gpu_memory = &direct_memory;
    const int previous_finish = direct_cache.runtime.finishes;
    direct_cache.MarkGpuUse(direct_cache.slot_buffers[direct_cache.ids.at(1000)], 0, 4);
    direct_cache.WaitGuestBacking(1000, 4);
    check(direct_cache.runtime.finishes > previous_finish && direct_cache.runtime.barriers_to_cpu > 0,
          "CPU access waits for GPU reads and writes to shared backing");
    direct_cache.TestReadback(direct_cache.slot_buffers[direct_cache.ids.at(1000)]);
    check(direct_cache.fallback_readbacks == 0 && direct_cache.cpu_begins != 0,
          "direct readback retains GPU completion and CPU ownership without copy-back");
    fallback.TestReadback(scratch_fallback);
    check(fallback.fallback_readbacks == 1, "scratch readback remains enabled");
    const GuestMappingIdentity stable{&direct_cache, 1, 2};
    check(!ZeroCopyEligibility::SupportsWholeAllocationCpuSync(1ull << 30) &&
          !ZeroCopyEligibility::SupportsWholeAllocationCpuSync(0) &&
          ZeroCopyEligibility::SupportsWholeAllocationCpuSync(
              ZeroCopyEligibility::MaxWholeAllocationCpuSyncSize) &&
          !ZeroCopyEligibility::SupportsWholeAllocationCpuSync(
              ZeroCopyEligibility::MaxWholeAllocationCpuSyncSize + 1),
          "whole-allocation cache maintenance is bounded independently of the binding size");
    ZeroCopyEligibility eligible{true, true, true, true, true, true, true, true, 4, 16384};
    const std::array physical_segments{DirectGuestSegment{100, 0, 4}, DirectGuestSegment{104, 4, 4}};
    check(eligible.Select(stable, stable, physical_segments, 8) == GuestMappingMode::Direct,
          "complete aligned coherent mapping is eligible");
    eligible.coherent = false;
    check(eligible.Select(stable, stable, physical_segments, 8) == GuestMappingMode::Scratch,
          "non-coherent host-visible mmap aliases retain scratch and readback");
    eligible.coherent = true;
    eligible.unified = false;
    check(eligible.Select(stable, stable, physical_segments, 8) == GuestMappingMode::Scratch,
          "discrete GPU selects scratch fallback");
    auto dma = split();
    dma.pages[128] = 3000;
    dma.pages[132] = 3008;
    BufferCache<int> d;
    d.gpu_memory = &dma;
    check(d.DMACopySplit(64, 128, 8) && d.dma_copies == 2,
          "non-contiguous DMA copy executes complete plan");
    d.dma_copies = 0;
    check(!d.DMACopySplit(64, 68, 8) && d.dma_copies == 0,
          "overlapping DMA falls back before modifying any source");
    dma.pages.erase(132);
    check(!d.DMACopySplit(64, 128, 8) && d.dma_copies == 0,
          "DMA mapping hole rejects whole plan before execution");
    check(d.DMAClearSplit(64, 2, 5) && d.dma_clears == 2,
          "DMA clear splits byte count into word counts");
    BufferCache<int> reads;
    reads.gpu_memory = &m;
    reads.memory_tracker.MarkRegionAsGpuModified(1008, 4);
    auto area = reads.GetFlushArea(1008, 4);
    check(!area->preemtive, "first CPU read requires buffer flush");
    reads.memory_tracker.MarkRegionAsGpuModified(1008, 4);
    check(!reads.GetFlushArea(1008, 4)->preemtive,
          "preflushable flag cannot bypass newer GPU writes");
    RasterizerVulkan raster(reads);
    raster.texture_cache.area = VideoCore::RasterizerDownloadArea{1000, 1020, true};
    auto merged = raster.GetFlushArea(1008, 4);
    check(!merged.preemtive && merged.start_address == 1000 && merged.end_address == 1020,
          "texture result cannot hide buffer writes on CPU read");
    Tracker tracker;
    check(filter(tracker, 4, 4 + 512_KiB).empty(), "reactive flush skips unread large ranges");
    tracker.MarkRegionAsPreflushable(8, 4);
    auto filtered = filter(tracker, 4, 4 + 512_KiB);
    check(filtered.size() == 1 && filtered[0] == std::pair<u64, u64>{8, 12},
          "reactive flush retains only CPU-read pages");
    check(filter(tracker, 4, 4 + 256_KiB).size() == 1, "small GPU-written ranges still downloaded");
}
'''
(p/'phase2_tests.cpp').write_text('\n'.join(parts)+main,encoding='utf-8')

source = p / "phase2_tests.cpp"
executable = p / ("gather_buffer_tests.exe" if os.name == "nt" else "gather_buffer_tests")
subprocess.run([args.cxx, "-std=c++23", "-O0", "-Wall", "-Wextra", "-I", str(root / "src"), str(source), "-o", str(executable)], check=True)
subprocess.run([str(executable)], check=True)
