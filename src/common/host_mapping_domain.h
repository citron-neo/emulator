// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include "common/common_types.h"

namespace Common {

// Cold HostMemory mutation ledger. This covers only this owner's mapping entry
// points: it is neither a CPU PTE freeze nor a guest/GPU exclusion capability.
class HostMappingDomain {
public:
    static constexpr size_t Capacity = 64;
    struct Alias {
        size_t virtual_offset{}, physical_offset{}, size{};
        u32 permissions{};
    };
    enum Escape : u32 { CanonicalPointer = 1, VirtualPointer = 2, PhysicalPin = 4,
                        ChunkLease = 8, DriverFd = 16 };

private:
    struct State {
        std::mutex mutex;
        std::array<Alias, Capacity> aliases{};
        size_t count{};
        size_t backing_size{};
        u64 generation{};
        bool complete{true}, alive{true}, closed{}, sealed{};
        std::atomic<u32> escapes{};
    };
    std::shared_ptr<State> state;
    bool managed;

public:
    // Immutable metadata only. Canonical backing is always an additional alias;
    // no CPU pointer, fd, storage pin, permission change or content authority.
    class Freeze {
        friend class HostMappingDomain;
        std::shared_ptr<State> state;
        std::array<Alias, Capacity> aliases;
        size_t count, backing_size;
        u64 generation;
        bool released{};
        explicit Freeze(std::shared_ptr<State> s)
            : state{std::move(s)}, aliases{state->aliases}, count{state->count},
              backing_size{state->backing_size}, generation{state->generation} {}
    public:
        Freeze(Freeze&& other) noexcept
            : state{std::move(other.state)}, aliases{other.aliases}, count{other.count},
              backing_size{other.backing_size}, generation{other.generation},
              released{other.released} {}
        Freeze(const Freeze&) = delete;
        ~Freeze() {
            if (state && !released) {
                std::lock_guard lock{state->mutex};
                state->sealed = true; // Drop never silently reopens mutation entry.
            }
        }
        std::span<const Alias> Aliases() const { return {aliases.data(), count}; }
        size_t CanonicalBackingSize() const { return backing_size; }
        u32 EscapedOwnerFamilies() const {
            return state ? state->escapes.load(std::memory_order_acquire) : ~u32{};
        }
        bool MappingEntriesClosed() const {
            if (!state || released) return false;
            std::lock_guard lock{state->mutex};
            return state->alive && state->closed && !state->sealed &&
                   state->generation == generation;
        }
        // CPU-only cancellation. No method can attach GPU work or release Unknown.
        bool CancelNoEffects() {
            if (!state || released) return false;
            std::lock_guard lock{state->mutex};
            if (!state->alive || state->sealed || !state->closed ||
                state->generation != generation) return false;
            state->closed = false;
            released = true;
            return true;
        }
        static constexpr bool GuestBackingAdmission() { return false; }
    };

    // Held across the actual syscall/publication, not just its preceding census.
    // HostMemory calls Finish only after successful operation and ledger update.
    class Mutation {
        friend class HostMappingDomain;
        std::shared_ptr<State> state;
        std::unique_lock<std::mutex> lock;
        bool finished{};
        explicit Mutation(std::shared_ptr<State> s)
            : state{std::move(s)} {
            if (!state) return; // Unmanaged legacy arena: no new mutex or ledger.
            lock = std::unique_lock<std::mutex>{state->mutex};
            if (state->closed || !state->alive)
                throw std::runtime_error("HostMemory mapping entries are frozen");
        }
        void Replace(size_t begin, size_t length, const std::optional<Alias>& mapped,
                     const std::optional<u32>& permissions) {
            if (!state || !state->complete) return;
            if (length > std::numeric_limits<size_t>::max() - begin) {
                state->complete = false;
                return;
            }
            const auto end = begin + length;
            std::array<Alias, Capacity> next{};
            size_t count = 0;
            const auto add = [&](Alias alias) {
                if (!alias.size) return;
                if (count == Capacity) { state->complete = false; return; }
                next[count++] = alias;
            };
            for (size_t i = 0; i < state->count; ++i) {
                const auto old = state->aliases[i];
                const auto old_end = old.virtual_offset + old.size;
                const auto lo = std::max(begin, old.virtual_offset);
                const auto hi = std::min(end, old_end);
                if (lo >= hi) { add(old); continue; }
                add({old.virtual_offset, old.physical_offset, lo - old.virtual_offset,
                     old.permissions});
                if (permissions)
                    add({lo, old.physical_offset + lo - old.virtual_offset, hi - lo,
                         *permissions});
                add({hi, old.physical_offset + hi - old.virtual_offset, old_end - hi,
                     old.permissions});
            }
            if (mapped) add(*mapped);
            if (!state->complete) return; // Never publish a truncated census.
            std::sort(next.begin(), next.begin() + count, [](const Alias& a, const Alias& b) {
                return a.virtual_offset < b.virtual_offset;
            });
            state->aliases = next;
            state->count = count;
        }
    public:
        Mutation(Mutation&&) = default;
        Mutation(const Mutation&) = delete;
        ~Mutation() { if (state && !finished) state->complete = false; }
        void Map(size_t va, size_t pa, size_t size, u32 permissions) {
            Replace(va, size, Alias{va, pa, size, permissions}, {});
        }
        void Unmap(size_t va, size_t size) { Replace(va, size, {}, {}); }
        void Protect(size_t va, size_t size, u32 permissions) {
            Replace(va, size, {}, permissions);
        }
        void Unsupported() { if (state) state->complete = false; }
        void Finish() {
            if (!state) { finished = true; return; }
            if (state->generation == std::numeric_limits<u64>::max()) state->complete = false;
            else ++state->generation;
            finished = true;
        }
    };

    explicit HostMappingDomain(bool managed_ = true)
        : state{managed_ ? std::make_shared<State>() : nullptr}, managed{managed_} {}
    HostMappingDomain(HostMappingDomain&&) noexcept = default;
    HostMappingDomain& operator=(HostMappingDomain&& other) noexcept {
        if (this != &other) { Retire(); state = std::move(other.state); managed = other.managed; }
        return *this;
    }
    HostMappingDomain(const HostMappingDomain&) = delete;
    ~HostMappingDomain() { Retire(); }
    bool IsManaged() const noexcept { return managed; }
    void Initialize(size_t backing_size, bool supported) {
        if (!state) return; // No adoption after any untracked historical use.
        std::lock_guard lock{state->mutex};
        state->backing_size = backing_size;
        state->complete = supported;
    }
    Mutation BeginMutation() const {
        if (!managed) return Mutation{nullptr};
        if (!state) throw std::runtime_error("HostMemory mapping owner was moved");
        return Mutation{state};
    }
    std::optional<Freeze> TryFreeze(bool* contended = nullptr) const {
        if (contended) *contended = false; // Output diagnostics, never an admission input.
        if (!state) return {};
        std::unique_lock lock{state->mutex, std::try_to_lock};
        if (!lock && contended) *contended = true;
        if (!lock || !state->alive || !state->complete || state->closed || state->sealed)
            return {};
        state->closed = true;
        return Freeze{state};
    }
    void ObserveEscape(Escape family) const noexcept {
        // Pointer getters are hot: only the first observation needs an RMW.
        if (state && !(state->escapes.load(std::memory_order_relaxed) & family))
            state->escapes.fetch_or(family, std::memory_order_release);
    }
private:
    void Retire() noexcept {
        if (state) {
            std::lock_guard lock{state->mutex};
            state->alive = false;
            state->closed = true;
            state->sealed = true;
        }
    }
};
} // namespace Common
