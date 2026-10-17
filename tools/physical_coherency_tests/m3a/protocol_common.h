// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace M3A {
using Id = std::uint64_t;
enum class Error {
    None,
    Blocked,
    Sealed,
    Dead,
    Stale,
    Reentrant,
    LockOrder,
    SelfWait,
    Invalid,
    Pending,
    Unknown,
    Allocation,
    Shutdown,
    DeviceLost
};
enum class Role { GPU, Device };
struct Key {
    Role role{};
    Id id{};
    auto operator<=>(const Key&) const = default;
};
struct Range {
    Id epoch{}, offset{}, size{};
    bool operator==(const Range&) const = default;
};
inline bool Overlap(Range a, Range b) {
    return a.epoch == b.epoch && a.size && b.size &&
           (a.offset <= b.offset ? b.offset - a.offset < a.size : a.offset - b.offset < b.size);
}
struct PteStack {
    std::array<Key, 32> keys{};
    std::size_t size{};
    bool empty() const {
        return size == 0;
    }
    Key back() const {
        return keys.at(size - 1);
    }
    void push_back(Key key) {
        keys.at(size) = key;
        ++size;
    }
    void pop_back() {
        --size;
    }
};
struct Context {
    unsigned metadata{}, callbacks{};
    bool gpu{};
    PteStack ptes; // bounded fixture nesting; constant-init TLS, no teardown allocator
};
extern thread_local Context context;
inline Error EntryCheck(bool mapping_callback_sensitive = true) {
    if (context.metadata || !context.ptes.empty())
        return Error::LockOrder;
    if (mapping_callback_sensitive && context.callbacks)
        return Error::Reentrant;
    return Error::None;
}
struct GpuContext {
    bool prior = context.gpu;
    GpuContext() {
        context.gpu = true;
    }
    ~GpuContext() {
        context.gpu = prior;
    }
};
struct MetadataMark {
    MetadataMark() {
        ++context.metadata;
    }
    ~MetadataMark() {
        --context.metadata;
    }
};
struct CallbackScope {
    CallbackScope() {
        if (context.metadata || !context.ptes.empty())
            throw std::logic_error("callback under metadata/PTE lock");
        ++context.callbacks;
    }
    ~CallbackScope() {
        --context.callbacks;
    }
};
// Fixture instrumentation: actual PTE mutex held, explicit GPU->device/id order.
// Production locks are not instrumented by including this header.
class PteLock {
    std::unique_lock<std::mutex> lock;

public:
    PteLock(std::mutex& mutex, Key key) {
        if (context.metadata || (!context.ptes.empty() && key <= context.ptes.back()))
            throw std::logic_error("metadata->PTE or reversed PTE nesting");
        lock = std::unique_lock{mutex};
        context.ptes.push_back(key);
    }
    ~PteLock() {
        context.ptes.pop_back();
    }
};
inline Error WaitCheck(bool ready) {
    if (context.metadata || !context.ptes.empty())
        return Error::LockOrder;
    return !ready && context.gpu ? Error::SelfWait : Error::None;
}
enum class Boundary { CallbackOnly, Recorded, Submitted, Completed, Visible, NoWork, Unknown };
enum class Retirement { Visible, ProvenCancelled, DeviceQuiescent };
} // namespace M3A
