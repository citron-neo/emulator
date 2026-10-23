// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <bit>
#include <chrono>
#include <optional>
#include <vector>
#include "video_core/physical_memory.h"

namespace VideoCommon {

// Access intent and historical geometry only. Never a data owner, resource lease,
// dispatch permission, or reason to change synchronization/fallback selection.
enum class ShadowKind : size_t {
    Buffer,
    Scratch,
    Texel,
    Texture,
    RenderTarget,
    DirectBuffer,
    Count
};
struct ShadowCoverage {
    u64 observations{}, write_intents{}, registered_writes{}, unresolved{};
    u64 bytes{}, max_range{}, registered_segments{}, split_observations{};
    std::array<u64, 8> size_buckets{};
    u64 sampled_out{}, sampled_out_writes{}, deduplicated{}, deduplicated_writes{};
    u64 inspections{}, registrations{}, unresolved_writes{};
};
// Fixed log2 histogram. Percentiles are bucket upper bounds, not exact samples.
struct ShadowLatency {
    std::array<u64, 65> buckets{};
    u64 count{};
    void Add(u64 ns) noexcept {
        ++buckets[std::bit_width(ns ? ns - 1 : 0)];
        ++count;
    }
    u64 Percentile(u64 percent) const noexcept {
        if (!count)
            return 0;
        const u64 rank = count / 100 * percent + (count % 100 * percent + 99) / 100;
        u64 accumulated = 0;
        for (size_t i = 0; i < buckets.size(); ++i) {
            accumulated += buckets[i];
            if (accumulated >= rank)
                return i == 64 ? ~u64{0} : u64{1} << i;
        }
        return ~u64{0};
    }
};
struct ShadowCounters {
    std::array<ShadowCoverage, static_cast<size_t>(ShadowKind::Count)> coverage{};
    u64 dispatches{}, queries{}, query_ns{}, max_dispatch_query_ns{}, validation_ns{};
    u64 observer_ns{}, max_dispatch_observer_ns{};
    u64 cache_hits{}, cache_invalidations{}, snapshot_invalidations{}, holes{}, budget_drops{};
    u64 historical_aliases{}, different_daddr_aliases{}, different_manager_aliases{};
    u64 provenance_mismatches{}, comparison_drops{}, errors{}, evictions{};
    u64 outside_dispatch_queries{}, max_dispatch_queries{};
    u64 generation_requeries{}, generation_invalidations{}, lifetime_invalidations{};
    u64 registered_observations{}, readonly_alias_pairs{}, writable_alias_pairs{};
    u64 alias_observations{}, alias_samples{}, cardinality_drops{};
    u64 max_distinct_gpu_refs{}, max_distinct_device_refs{}, max_readonly_refs{},
        max_writable_refs{};
    u64 sample_epoch{}, sample_offset{}, sample_length{}, sample_gpu_refs{}, sample_device_refs{};
    u64 sample_readonly_refs{}, sample_writable_refs{};
    u64 active_entries{}, peak_entries{}, metadata_bytes{}, peak_metadata_bytes{};
    u64 gpu_lock_wait_ns{}, device_lock_wait_ns{}, walk_ns{};
    u64 validation_lock_wait_ns{}, provenance_walk_ns{};
    ShadowLatency resolve_latency, lock_latency, walk_latency, dispatch_latency;
    u64 sampling_period{}, sampled_dispatches{}, skipped_dispatches{};
    u64 outside_observations{}, outside_sampled{}, outside_sampled_out{};
    u64 lookup_hint_hits{}, lookup_fallbacks{}, geometry_comparisons{}, device_span_steps{};
    u64 timed_observations{};
};

// GPU-thread confined. Bounded metadata; contains no backing allocation, driver
// handle, MemoryManager pointer or producer lifetime reference. Shared stamps only
// retain inert lifetime tokens. Sampling loss is explicit in counters.
class PhysicalShadow {
public:
    static constexpr size_t Capacity = 128;
    static constexpr u64 MaxRange = 64ULL << 20;
    static constexpr size_t MaxSegments = 256;
    static constexpr u64 SamplingPeriod = 64;

    // Period=1 is useful for isolated exact-coverage tests; renderer uses 64.
    explicit PhysicalShadow(u64 period = SamplingPeriod)
        : sampling_period{std::max<u64>(1, period)} {
        counters.sampling_period = sampling_period;
        TrackMetadata();
    }

    void BeginDispatch() noexcept {
        in_dispatch = true;
        dispatch_operation = ++operation;
        dispatch_sampled = Select(++dispatch_sequence, 0x9e3779b97f4a7c15ULL);
        dispatch_query_ns = dispatch_queries = 0;
        dispatch_observer_ns = 0;
    }
    void EndDispatch() noexcept {
        if (!in_dispatch) {
            return;
        }
        ++counters.dispatches;
        counters.sampled_dispatches += dispatch_sampled;
        counters.skipped_dispatches += !dispatch_sampled;
        counters.max_dispatch_query_ns =
            std::max(counters.max_dispatch_query_ns, dispatch_query_ns);
        counters.max_dispatch_queries = std::max(counters.max_dispatch_queries, dispatch_queries);
        counters.max_dispatch_observer_ns =
            std::max(counters.max_dispatch_observer_ns, dispatch_observer_ns);
        // Don't dilute selected-operation quantiles with skipped zero samples.
        if (dispatch_sampled)
            counters.dispatch_latency.Add(dispatch_observer_ns);
        in_dispatch = false;
    }
    const ShadowCounters& Counters() const noexcept {
        return counters;
    }

    template <class Memory>
    void Observe(Memory& memory, GPUVAddr address, u64 size, ShadowKind kind, bool writable,
                 std::optional<DAddr> expected_device = {}) noexcept {
        auto& coverage = counters.coverage[static_cast<size_t>(kind)];
        ++coverage.observations;
        coverage.write_intents += writable;
        coverage.bytes += size;
        coverage.max_range = std::max(coverage.max_range, size);
        constexpr std::array<u64, 7> limits{64, 1024, 16384, 65536, 262144, 1048576, 16777216};
        ++coverage.size_buckets[std::ranges::upper_bound(limits, size) - limits.begin()];
        const u64 current_operation = in_dispatch ? dispatch_operation : ++operation;
        const bool selected =
            in_dispatch ? dispatch_sampled : Select(++outside_sequence, 0xd1b54a32d192ed03ULL);
        if (!in_dispatch) {
            ++counters.outside_observations;
            counters.outside_sampled += selected;
            counters.outside_sampled_out += !selected;
        }
        if (!selected) {
            ++coverage.sampled_out;
            coverage.sampled_out_writes += writable;
            return; // No clock, cache lookup, mapping lock or resolver on skipped inputs.
        }
        ++coverage.inspections;
        ++counters.timed_observations;
        struct Measure {
            PhysicalShadow& shadow;
            Clock::time_point start{Clock::now()};
            ~Measure() {
                const u64 elapsed = NsSince(start);
                shadow.counters.observer_ns += elapsed;
                if (shadow.in_dispatch) {
                    shadow.dispatch_observer_ns += elapsed;
                }
            }
        } measure{*this};
        try {
            const size_t hint = Hint(memory.GetID(), address, size, kind);
            auto* cache_slot = Find(memory.GetID(), address, size, kind, hint);
            Entry* entry = cache_slot ? &**cache_slot : nullptr;
            if (entry && entry->registered && entry->last_operation == current_operation &&
                entry->last_writable == writable &&
                entry->last_expected_device == expected_device &&
                UnchangedForSkip(entry->physical.stamp)) {
                // Skip only: no paired-lock validation or new physical registration.
                --coverage.inspections;
                ++coverage.deduplicated;
                coverage.deduplicated_writes += writable;
                return;
            }
            if (!address || !size || size > MaxRange) {
                ++counters.budget_drops;
                ++coverage.unresolved;
                coverage.unresolved_writes += writable;
                return;
            }
            if (entry) {
                const auto start = Clock::now();
                const bool current = Tegra::ValidatePhysicalSnapshot(
                    entry->physical, &counters.validation_lock_wait_ns);
                counters.validation_ns += NsSince(start);
                if (current) {
                    ++counters.cache_hits;
                } else {
                    ++counters.cache_invalidations;
                    const auto& stamp = entry->physical.stamp;
                    if (!stamp.gpu_address_space->alive.load() ||
                        !stamp.device_address_space->alive.load() || !stamp.backing->alive.load()) {
                        ++counters.lifetime_invalidations;
                    } else if (stamp.gpu_address_space->generation.load() !=
                                   stamp.gpu_mapping_generation ||
                               stamp.device_address_space->generation.load() !=
                                   stamp.device_mapping_generation) {
                        ++counters.generation_invalidations;
                        ++counters.generation_requeries;
                    }
                    entry = nullptr;
                }
            }
            if (!entry) {
                auto physical = ResolveTimed(memory, address, size);
                TrackMetadata(physical.segments.capacity() * sizeof(Tegra::PhysicalSegment));
                if (physical.status != Tegra::PhysicalResolveStatus::Success ||
                    std::ranges::any_of(physical.segments,
                                        [](const auto& s) { return !s.physical; })) {
                    ++counters.holes;
                    ++coverage.unresolved;
                    coverage.unresolved_writes += writable;
                    return;
                }
                if (physical.segments.size() > MaxSegments) {
                    ++counters.budget_drops;
                    ++coverage.unresolved;
                    coverage.unresolved_writes += writable;
                    return;
                }
                Entry next{memory.GetID(), address, size, std::move(physical), {}, kind};
                // Capture DAddr provenance under the same pair of mapping locks,
                // only if the original physical stamp still matches. This avoids
                // assembling a physical/DAddr pair that never existed together.
                const auto validate_start = Clock::now();
                const bool captured = CaptureDeviceRanges(memory, next);
                TrackMetadata(next.physical.segments.capacity() * sizeof(Tegra::PhysicalSegment) +
                              next.device.capacity() * sizeof(DeviceRange));
                counters.validation_ns += NsSince(validate_start);
                if (!captured) {
                    ++counters.snapshot_invalidations;
                    ++coverage.unresolved;
                    coverage.unresolved_writes += writable;
                    return;
                }
                if (next.device.size() > MaxSegments) {
                    ++counters.budget_drops;
                    ++coverage.unresolved;
                    coverage.unresolved_writes += writable;
                    return;
                }
                // Replace stale geometry in place. Retaining an older same-key
                // entry first in the array would turn every lookup into a miss.
                auto& slot = cache_slot ? *cache_slot : entries[cursor++ % Capacity];
                counters.evictions += !cache_slot && slot.has_value();
                counters.active_entries += !slot.has_value();
                counters.peak_entries = std::max(counters.peak_entries, counters.active_entries);
                if (slot)
                    retained_bytes -= Payload(*slot);
                slot = std::move(next);
                retained_bytes += Payload(*slot);
                hints[hint] = static_cast<u16>(&slot - entries.data() + 1);
                entry = &*slot;
                TrackMetadata();
            }
            // A cached texture/texel resource is identified by its actual DAddr
            // span. Its old GPU VA may now name different storage. Don't register
            // that unrelated translation as the resource's physical identity.
            if (expected_device &&
                (entry->device.size() != 1 || entry->device.front().address != *expected_device)) {
                ++counters.provenance_mismatches;
                ++coverage.unresolved;
                coverage.unresolved_writes += writable;
                entry->registered = false;
                return;
            }
            coverage.registered_writes += writable;
            ++coverage.registrations;
            coverage.registered_segments += entry->physical.segments.size();
            coverage.split_observations += entry->physical.segments.size() > 1;
            entry->registered = true;
            entry->writable_seen |= writable;
            entry->last_operation = current_operation;
            entry->last_writable = writable;
            entry->last_expected_device = expected_device;
            ++counters.registered_observations;
            Compare(*entry);
            SampleCardinality(*entry);
        } catch (...) {
            // Diagnostics must not reject work or alter emulation on OOM/error.
            ++counters.errors;
            ++coverage.unresolved;
            coverage.unresolved_writes += writable;
        }
    }

private:
    using Clock = std::chrono::steady_clock;
    struct DeviceRange {
        u64 logical{}, address{}, length{};
    };
    struct Entry {
        size_t manager{};
        u64 address{}, size{};
        Tegra::PhysicalResolveResult physical;
        std::vector<DeviceRange> device;
        ShadowKind kind{};
        bool registered{};
        bool writable_seen{};
        u64 last_operation{};
        bool last_writable{};
        std::optional<DAddr> last_expected_device;
    };
    static u64 Mix(u64 x) noexcept {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }
    static bool UnchangedForSkip(const Tegra::MappingStamp& stamp) noexcept {
        // Cheap historical hint, NOT a coherent mapping snapshot. A remap after
        // these loads may make the skipped event unknown; it never authorizes work.
        return stamp.gpu_address_space->alive.load(std::memory_order_acquire) &&
               stamp.device_address_space->alive.load(std::memory_order_acquire) &&
               stamp.backing->alive.load(std::memory_order_acquire) &&
               stamp.gpu_address_space->generation.load(std::memory_order_acquire) ==
                   stamp.gpu_mapping_generation &&
               stamp.device_address_space->generation.load(std::memory_order_acquire) ==
                   stamp.device_mapping_generation;
    }
    bool Select(u64 sequence, u64 salt) const noexcept {
        // First event is always inspected; hashed sequences avoid a fixed every-N
        // phase locking to a particular render pass. Deterministic, not unbiased inference.
        return sequence == 1 || Mix(sequence ^ salt) % sampling_period == 0;
    }
    static size_t Hint(size_t manager, u64 address, u64 size, ShadowKind kind) noexcept {
        return Mix(manager ^ std::rotl(address, 13) ^ std::rotl(size, 31) ^
                   static_cast<u64>(kind)) &
               255;
    }
    std::optional<Entry>* Find(size_t manager, u64 address, u64 size, ShadowKind kind,
                               size_t hint) noexcept {
        const auto matches = [&](const auto& candidate) {
            return candidate && candidate->manager == manager && candidate->address == address &&
                   candidate->size == size && candidate->kind == kind;
        };
        if (hints[hint] && matches(entries[hints[hint] - 1])) {
            ++counters.lookup_hint_hits;
            return &entries[hints[hint] - 1];
        }
        ++counters.lookup_fallbacks;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (matches(entries[i])) {
                hints[hint] = static_cast<u16>(i + 1);
                return &entries[i];
            }
        }
        return nullptr;
    }
    static u64 Payload(const Entry& entry) noexcept {
        return entry.physical.segments.capacity() * sizeof(Tegra::PhysicalSegment) +
               entry.device.capacity() * sizeof(DeviceRange);
    }
    static u64 NsSince(Clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    }
    template <class Memory>
    Tegra::PhysicalResolveResult ResolveTimed(Memory& memory, u64 address, u64 size) {
        const auto start = Clock::now();
        Tegra::PhysicalResolveTiming timing;
        ++counters.queries;
        const auto record = [&] {
            const u64 elapsed = NsSince(start);
            counters.query_ns += elapsed;
            counters.gpu_lock_wait_ns += timing.gpu_lock_wait_ns;
            counters.device_lock_wait_ns += timing.device_lock_wait_ns;
            counters.walk_ns += timing.walk_ns;
            counters.resolve_latency.Add(elapsed);
            counters.lock_latency.Add(timing.gpu_lock_wait_ns + timing.device_lock_wait_ns);
            counters.walk_latency.Add(timing.walk_ns);
            if (in_dispatch) {
                dispatch_query_ns += elapsed;
                ++dispatch_queries;
            } else {
                ++counters.outside_dispatch_queries;
            }
        };
        try {
            auto result = memory.ResolvePhysicalRanges(address, size, &timing);
            record();
            return result;
        } catch (...) {
            record();
            throw;
        }
    }
    template <class Memory>
    bool CaptureDeviceRanges(Memory& memory, Entry& entry) {
        const auto& stamp = entry.physical.stamp;
        if (!stamp.gpu_address_space || !stamp.device_address_space ||
            stamp.gpu_address_space == stamp.device_address_space) {
            return false;
        }
        const auto gpu_lock = [&] {
            Common::MappingTimer timer{&counters.validation_lock_wait_ns};
            return std::unique_lock{stamp.gpu_address_space->mutex};
        }();
        const auto device_lock = [&] {
            Common::MappingTimer timer{&counters.validation_lock_wait_ns};
            return std::unique_lock{stamp.device_address_space->mutex};
        }();
        Common::MappingTimer walk_timer{&counters.provenance_walk_ns};
        if (!Tegra::Detail::MappingStampCurrentLocked(stamp)) {
            return false;
        }
        // GMMU's smallest supported page is 4 KiB; splitting here is also safe
        // for big pages. No GetPointer, guest read, backing creation or GPU call.
        constexpr u64 page = 4096;
        for (u64 offset = 0; offset < entry.size;) {
            const auto device = memory.GpuToCpuAddress(entry.address + offset);
            if (!device) {
                return false;
            }
            const u64 length =
                std::min(entry.size - offset, page - (entry.address + offset) % page);
            if (!entry.device.empty() &&
                entry.device.back().address + entry.device.back().length == *device) {
                entry.device.back().length += length;
            } else {
                entry.device.push_back({offset, *device, length});
                if (entry.device.size() > MaxSegments) {
                    return true; // explicit caller budget drop, never a successful registration
                }
            }
            offset += length;
        }
        return true;
    }
    static u64 DeviceAt(const Entry& entry, u64 logical) {
        for (const auto& range : entry.device) {
            if (logical >= range.logical && logical - range.logical < range.length) {
                return range.address + logical - range.logical;
            }
        }
        return 0;
    }
    static DeviceRange DeviceSpanAt(const Entry& entry, u64 logical) {
        for (const auto& range : entry.device) {
            if (logical >= range.logical && logical - range.logical < range.length)
                return {logical, range.address + logical - range.logical,
                        range.length - (logical - range.logical)};
        }
        return {}; // Budget loss instead of inferring absent provenance equals address zero.
    }
    void Compare(const Entry& current) {
        size_t comparisons = 0;
        bool any_alias = false;
        for (const auto& old : entries) {
            if (!old || !old->registered || &*old == &current) {
                continue;
            }
            bool alias = false, different_device = false;
            for (const auto& a : current.physical.segments) {
                for (const auto& b : old->physical.segments) {
                    if (++comparisons > 4096) {
                        ++counters.comparison_drops;
                        return;
                    }
                    ++counters.geometry_comparisons;
                    if (Tegra::PhysicalRangesOverlap(*a.physical, *b.physical)) {
                        alias = true;
                        any_alias = true;
                        u64 physical =
                            std::max(a.physical->physical_offset, b.physical->physical_offset);
                        const u64 end = std::min(a.physical->physical_offset + a.length,
                                                 b.physical->physical_offset + b.length);
                        while (physical < end && !different_device) {
                            if (++comparisons > 4096) {
                                ++counters.comparison_drops;
                                return;
                            }
                            const u64 logical_a =
                                a.logical_offset + physical - a.physical->physical_offset;
                            const u64 logical_b =
                                b.logical_offset + physical - b.physical->physical_offset;
                            const auto device_a = DeviceSpanAt(current, logical_a);
                            const auto device_b = DeviceSpanAt(*old, logical_b);
                            ++counters.device_span_steps;
                            if (!device_a.length || !device_b.length) {
                                ++counters.comparison_drops;
                                return;
                            }
                            different_device |= device_a.address != device_b.address;
                            // Equal linear DAddr translations remain equal throughout
                            // both captured spans. Check every span boundary, not every page.
                            physical +=
                                std::min({end - physical, device_a.length, device_b.length});
                        }
                    }
                }
            }
            counters.historical_aliases += alias;
            counters.readonly_alias_pairs += alias && !current.writable_seen && !old->writable_seen;
            counters.writable_alias_pairs += alias && (current.writable_seen || old->writable_seen);
            counters.different_daddr_aliases += alias && different_device;
            counters.different_manager_aliases +=
                alias &&
                current.physical.stamp.gpu_address_space != old->physical.stamp.gpu_address_space;
        }
        counters.alias_observations += any_alias;
    }
    void TrackMetadata(u64 transient = 0) noexcept {
        const u64 bytes = sizeof(*this) + retained_bytes;
        counters.metadata_bytes = bytes;
        counters.peak_metadata_bytes = std::max(counters.peak_metadata_bytes, bytes + transient);
    }
    // Sample exact constant-membership intervals at segment/alias starts. Reference
    // cardinalities are distinct token+address pairs, not descriptor counts.
    // Historical window only, bounded scans; no claim of complete/live topology.
    void SampleCardinality(const Entry& current) noexcept {
        struct Ref {
            const Common::MappingEpoch* token{};
            u64 address{};
            bool writable{};
        };
        size_t scans = 0, samples = 0;
        std::array<u64, 8> anchors{};
        for (const auto& segment : current.physical.segments) {
            const u64 begin = segment.physical->physical_offset;
            const u64 end = begin + segment.length;
            size_t anchor_count = 1;
            anchors[0] = begin;
            for (const auto& other : entries) {
                if (!other || !other->registered)
                    continue;
                for (const auto& s : other->physical.segments) {
                    if (++scans > 4096) {
                        ++counters.cardinality_drops;
                        return;
                    }
                    if (Tegra::PhysicalRangesOverlap(*segment.physical, *s.physical)) {
                        const u64 point = std::max(begin, s.physical->physical_offset);
                        if (std::find(anchors.begin(), anchors.begin() + anchor_count, point) ==
                            anchors.begin() + anchor_count) {
                            if (anchor_count == anchors.size()) {
                                ++counters.cardinality_drops;
                                continue;
                            }
                            anchors[anchor_count++] = point;
                        }
                    }
                }
            }
            for (size_t p = 0; p < anchor_count; ++p) {
                if (++samples > 8) {
                    ++counters.cardinality_drops;
                    return;
                }
                const u64 point = anchors[p];
                if (point >= end)
                    continue;
                u64 slice_end = end;
                std::array<Ref, 256> gpu_refs{}, device_refs{};
                size_t gpu_count = 0, device_count = 0;
                const auto add = [&](auto& refs, size_t& count, const Common::MappingEpoch* token,
                                     u64 address, bool writable) {
                    for (size_t i = 0; i < count; ++i) {
                        if (refs[i].token == token && refs[i].address == address) {
                            refs[i].writable |= writable;
                            return true;
                        }
                    }
                    if (count == refs.size())
                        return false;
                    refs[count++] = {token, address, writable};
                    return true;
                };
                for (const auto& other : entries) {
                    if (!other || !other->registered)
                        continue;
                    for (const auto& s : other->physical.segments) {
                        if (++scans > 4096) {
                            ++counters.cardinality_drops;
                            return;
                        }
                        if (s.physical->backing_epoch != segment.physical->backing_epoch)
                            continue;
                        if (point < s.physical->physical_offset) {
                            slice_end = std::min(slice_end, s.physical->physical_offset);
                            continue;
                        }
                        if (point - s.physical->physical_offset >= s.length)
                            continue;
                        slice_end = std::min(slice_end, s.physical->physical_offset + s.length);
                        const u64 logical = s.logical_offset + point - s.physical->physical_offset;
                        for (const auto& range : other->device) {
                            if (logical >= range.logical &&
                                logical - range.logical < range.length) {
                                slice_end = std::min(slice_end, point + range.length -
                                                                    (logical - range.logical));
                                break;
                            }
                        }
                        if (!add(gpu_refs, gpu_count, other->physical.stamp.gpu_address_space.get(),
                                 other->address + logical, other->writable_seen) ||
                            !add(device_refs, device_count,
                                 other->physical.stamp.device_address_space.get(),
                                 DeviceAt(*other, logical), other->writable_seen)) {
                            ++counters.cardinality_drops;
                            return;
                        }
                    }
                }
                const u64 writers = std::count_if(gpu_refs.begin(), gpu_refs.begin() + gpu_count,
                                                  [](const auto& ref) { return ref.writable; });
                ++counters.alias_samples;
                counters.max_distinct_gpu_refs =
                    std::max<u64>(counters.max_distinct_gpu_refs, gpu_count);
                counters.max_distinct_device_refs =
                    std::max<u64>(counters.max_distinct_device_refs, device_count);
                counters.max_writable_refs = std::max(counters.max_writable_refs, writers);
                counters.max_readonly_refs =
                    std::max<u64>(counters.max_readonly_refs, gpu_count - writers);
                if (gpu_count >= counters.sample_gpu_refs) {
                    counters.sample_epoch = segment.physical->backing_epoch;
                    counters.sample_offset = point;
                    counters.sample_length = slice_end - point;
                    counters.sample_gpu_refs = gpu_count;
                    counters.sample_device_refs = device_count;
                    counters.sample_writable_refs = writers;
                    counters.sample_readonly_refs = gpu_count - writers;
                }
            }
        }
    }
    std::array<std::optional<Entry>, Capacity> entries;
    std::array<u16, 256> hints{}; // Collision/eviction hints always verify the complete key.
    size_t cursor{};
    bool in_dispatch{};
    bool dispatch_sampled{};
    u64 sampling_period{}, operation{}, dispatch_operation{}, dispatch_sequence{},
        outside_sequence{}, retained_bytes{};
    u64 dispatch_query_ns{}, dispatch_queries{}, dispatch_observer_ns{};
    ShadowCounters counters;
};

} // namespace VideoCommon
