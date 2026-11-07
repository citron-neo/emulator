// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include "common/published_guest_chunk.h"

namespace Vulkan {

enum class ChunkImportRejection : size_t {
    None,
    Unpublished,
    InactiveBacking,
    Closed,
    Platform,
    NotExportable,
    HandleType,
    DeviceIdentity,
    BufferContract,
    ExternalProperties,
    Dedicated,
    MemoryRequirements,
    MemoryTypes,
    FdSize,
    FdDup,
    CreateBuffer,
    AllocateMemory,
    BindBuffer,
    Exception,
    Count
};
inline const char* ChunkImportRejectionName(ChunkImportRejection reason) {
    constexpr std::array names{"none",
                               "unpublished",
                               "inactive_backing",
                               "closed",
                               "platform",
                               "not_exportable",
                               "handle_type",
                               "device_identity",
                               "buffer_contract",
                               "external_properties",
                               "dedicated",
                               "memory_requirements",
                               "memory_types",
                               "fd_size",
                               "fd_dup",
                               "create_buffer",
                               "allocate_memory",
                               "bind_buffer",
                               "exception"};
    static_assert(names.size() == static_cast<size_t>(ChunkImportRejection::Count));
    return names.at(static_cast<size_t>(reason));
}

struct ChunkImportObservation {
    u64 generation{}, chunk{}, allocation_size{}, alignment{};
    u32 memory_type{}, memory_properties{};
};

struct ChunkImportStats {
    u64 device_epoch{}, backing_generation{};
    u64 import_requests{}, import_cache_hits{}, imports_created{}, import_failures{};
    u64 active_imports{}, peak_active_imports{}, imported_logical_bytes{};
    u64 import_duration_ns_total{}, import_duration_ns_max{}, fd_dup_failures{};
    u64 cache_entries_released{};
    u64 peak_process_fd_count{},
        peak_process_vma_count{}; // diagnostics sampled, not absolute peaks
    s32 last_vk_result{};
    std::array<u64, static_cast<size_t>(ChunkImportRejection::Count)> rejections{};
};

// Backend resources never leave this cache; callers only receive observations,
// so a diagnostic reference cannot outlive VkDevice or become a guest binding.
// A single lock serializes import publication, generation replacement and Close.
template <class Backend>
class GuestChunkImportCache {
public:
    explicit GuestChunkImportCache(Backend& backend_) : backend{backend_} {
        stats.device_epoch = next_device_epoch.fetch_add(1, std::memory_order_relaxed);
    }
    ~GuestChunkImportCache() {
        Close();
    }

    std::optional<ChunkImportObservation> Request(Common::GuestChunkLease chunk) {
        std::scoped_lock lock{mutex};
        ++stats.import_requests;
        if (closed)
            return Reject(ChunkImportRejection::Closed);
        if (!chunk)
            return Reject(ChunkImportRejection::Unpublished);
        if (!chunk->owner)
            return Reject(ChunkImportRejection::InactiveBacking);
        if (!chunk->Alive()) {
            if (chunk->epoch && stats.backing_generation == chunk->epoch->generation)
                ReleaseEntries();
            return Reject(ChunkImportRejection::InactiveBacking);
        }
        const u64 generation = chunk->epoch->generation;
        backing_epoch = chunk->epoch;
        if (stats.backing_generation != generation) {
            ReleaseEntries();
            stats.backing_generation = generation;
            publication_failed = false;
        }
        if (publication_failed)
            return Reject(ChunkImportRejection::Exception);
        if (auto it = entries.find(chunk->index); it != entries.end()) {
            if (!it->second.resource)
                return Reject(it->second.reason);
            ++stats.import_cache_hits;
            return it->second.resource->info;
        }
        const auto start = std::chrono::steady_clock::now();
        typename Backend::Result result;
        try {
            result = backend.Create(chunk);
        } catch (...) {
            result.reason = ChunkImportRejection::Exception;
        }
        const u64 duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        stats.import_duration_ns_total += duration;
        stats.import_duration_ns_max = std::max(stats.import_duration_ns_max, duration);
        stats.last_vk_result = result.vk_result;
        if (closed.load(std::memory_order_acquire) || !chunk->Alive()) {
            result.resource.reset();
            ++stats.import_failures;
            return Reject(closed ? ChunkImportRejection::Closed
                                 : ChunkImportRejection::InactiveBacking);
        }
        if (!result.resource) {
            ++stats.import_failures;
            stats.fd_dup_failures += result.reason == ChunkImportRejection::FdDup;
            try {
                entries.emplace(chunk->index, Entry{nullptr, result.reason});
            } catch (...) {
                publication_failed = true;
                return Reject(ChunkImportRejection::Exception);
            }
            return Reject(result.reason);
        }
        const auto observation = result.resource->info;
        // Map insertion may throw; the local resource then destroys its buffer,
        // memory and backing lease in that order. No half entry is published.
        try {
            entries.emplace(chunk->index,
                            Entry{std::move(result.resource), ChunkImportRejection::None});
        } catch (...) {
            publication_failed = true;
            ++stats.import_failures;
            return Reject(ChunkImportRejection::Exception);
        }
        ++stats.imports_created;
        ++stats.active_imports;
        stats.peak_active_imports = std::max(stats.peak_active_imports, stats.active_imports);
        stats.imported_logical_bytes += chunk->size;
        return observation;
    }

    void Close() {
        closed.store(true, std::memory_order_release);
        std::scoped_lock lock{mutex};
        ReleaseEntries();
    }
    void SetGeneration(u64 generation) {
        std::scoped_lock lock{mutex};
        if (!closed && stats.backing_generation != generation) {
            ReleaseEntries();
            stats.backing_generation = generation;
            publication_failed = false;
        }
    }
    ChunkImportStats GetStats() {
        std::scoped_lock lock{mutex};
        const auto epoch = backing_epoch.lock();
        if (!entries.empty() && (!epoch || !epoch->alive.load(std::memory_order_acquire))) {
            ReleaseEntries();
        }
        return stats;
    }
    bool IsClosed() const {
        return closed.load(std::memory_order_acquire);
    }

private:
    std::optional<ChunkImportObservation> Reject(ChunkImportRejection reason) {
        ++stats.rejections[static_cast<size_t>(reason)];
        return std::nullopt;
    }
    void ReleaseEntries() {
        stats.cache_entries_released += entries.size();
        entries.clear();
        stats.active_imports = stats.imported_logical_bytes = 0;
    }
    struct Entry {
        std::unique_ptr<typename Backend::Resource> resource;
        ChunkImportRejection reason;
    };
    Backend& backend;
    mutable std::mutex mutex;
    std::atomic<bool> closed{};
    bool publication_failed{};
    std::map<u64, Entry> entries;
    ChunkImportStats stats;
    std::weak_ptr<const Common::GuestBackingEpoch> backing_epoch;
    inline static std::atomic<u64> next_device_epoch{1};
};

} // namespace Vulkan
