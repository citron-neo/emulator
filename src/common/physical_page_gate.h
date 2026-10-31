// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "common/common_types.h"

namespace Common {

class PreparedPhysicalCpuPage;
class PhysicalPageRegistration;
class PhysicalRegistrationBundle;

/// Cold lookup registry, scoped to ONE backing epoch. Nodes hold metadata only, never VA keys,
/// storage or a renderer. Prepared CPU pages retain the actual storage separately.
class PhysicalPageGates {
public:
    static constexpr u64 PageSize = 4096;
    static constexpr size_t MaxPages = 65536;
    /// Monotonic cumulative observation, not an owned dirty batch. No bit clearing or sequence
    /// acknowledgement is provided. Snapshot requires closed fast entry and drainage; explicit
    /// tracked stores additionally exclude snapshots with a nonblocking observation guard.
    struct Publication {
        std::array<u64, PageSize / 64> modified_bytes{};
        u64 sequence{};
        [[nodiscard]] bool Contains(size_t byte) const {
            return byte < PageSize && ((modified_bytes[byte / 64] >> (byte % 64)) & 1);
        }
    };
    class Node {
        friend class PhysicalPageGates;
        friend class PreparedPhysicalCpuPage;
        friend class PhysicalPageRegistration;
        friend class PhysicalRegistrationBundle;
        static constexpr u64 Tracked = 1, Closing = 2, One = 4;
        explicit Node(std::shared_ptr<std::atomic_bool> stop) : closed{std::move(stop)} {}
        bool TryEnter() noexcept {
            if (closed->load(std::memory_order_acquire)) return false;
            auto value = state.load(std::memory_order_acquire);
            while (!(value & (Tracked | Closing))) {
                if (value > std::numeric_limits<u64>::max() - One) return false;
                if (state.compare_exchange_weak(value, value + One, std::memory_order_acquire)) {
                    if (closed->load(std::memory_order_acquire)) { Exit(); return false; }
                    return true;
                }
            }
            return false;
        }
        void Exit() noexcept { state.fetch_sub(One, std::memory_order_release); }
        bool CloseForRegistration() noexcept {
            if (closed->load(std::memory_order_acquire)) return false;
            auto value = state.load(std::memory_order_acquire);
            while (!(value & (Tracked | Closing))) {
                if (state.compare_exchange_weak(value, value | Closing,
                                                std::memory_order_acq_rel)) return true;
            }
            return false;
        }
        bool Drained() const noexcept {
            return !closed->load(std::memory_order_acquire) &&
                   state.load(std::memory_order_acquire) == Closing;
        }
        bool PublishTracked() noexcept {
            u64 expected = Closing;
            return !closed->load(std::memory_order_acquire) &&
                   state.compare_exchange_strong(expected, Tracked, std::memory_order_release);
        }
        void Publish(size_t offset, size_t count) noexcept {
            while (count) {
                const auto bit = offset % 64;
                const auto length = std::min<size_t>(count, 64 - bit);
                const u64 mask = length == 64 ? ~u64{} : ((u64{1} << length) - 1) << bit;
                modified[offset / 64].fetch_or(mask, std::memory_order_relaxed);
                offset += length; count -= length;
            }
            published.fetch_add(1, std::memory_order_release);
        }
        std::optional<Publication> Snapshot() const noexcept {
            if (observation.test_and_set(std::memory_order_acquire)) return std::nullopt;
            ObservationExit observation_exit{&observation};
            const auto value = state.load(std::memory_order_acquire);
            if (closed->load(std::memory_order_acquire) || value / One ||
                !(value & (Tracked | Closing))) return std::nullopt;
            Publication result;
            for (size_t i = 0; i < modified.size(); ++i)
                result.modified_bytes[i] = modified[i].load(std::memory_order_acquire);
            result.sequence = published.load(std::memory_order_acquire);
            return result;
        }
        struct ObservationExit {
            std::atomic_flag* flag;
            ~ObservationExit() { flag->clear(std::memory_order_release); }
        };
        // Only snapshots and explicit tracked stores participate. Fast entry stays unchanged.
        // Never spin: a concurrent observation makes the controlled cached store retry.
        mutable std::atomic_flag observation = ATOMIC_FLAG_INIT;
        std::atomic<u64> state{};
        std::array<std::atomic<u64>, PageSize / 64> modified{};
        std::atomic<u64> published{};
        const std::shared_ptr<std::atomic_bool> closed;
    };

    [[nodiscard]] std::shared_ptr<Node> Prepare(u64 physical_page_offset) {
        if (physical_page_offset % PageSize) return {};
        std::scoped_lock lock{mutex};
        if (closed->load(std::memory_order_acquire)) return {};
        if (const auto it = pages.find(physical_page_offset); it != pages.end()) return it->second;
        if (pages.size() >= MaxPages) return {};
        auto node = std::shared_ptr<Node>{new Node{closed}};
        pages.emplace(physical_page_offset, node);
        return node;
    }
    void Seal() noexcept { closed->store(true, std::memory_order_release); }

private:
    std::mutex mutex;
    std::map<u64, std::shared_ptr<Node>> pages;
    std::shared_ptr<std::atomic_bool> closed{std::make_shared<std::atomic_bool>(false)};
};

} // namespace Common
