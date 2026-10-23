// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <utility>
#include "controlled_cpu_access.h"

namespace M3A {
ControlledCPU::ControlledCPU(std::vector<std::uint8_t> initial, Id epoch)
    : state{std::make_shared<State>()} {
    state->bytes = std::move(initial);
    state->epoch = epoch;
}
ControlledCPU::~ControlledCPU() {
    Seal();
}
Error ControlledCPU::Available(const State& s, Range r, bool write) {
    if (s.sealed)
        return Error::Sealed;
    if (r.epoch != s.epoch)
        return Error::Dead;
    if (!r.size || r.offset > s.bytes.size() || r.size > s.bytes.size() - r.offset)
        return Error::Invalid;
    for (const auto& [id, a] : s.accesses) {
        (void)id;
        if (!Overlap(r, a.range))
            continue;
        if (a.phase == CpuPhase::Unknown)
            return Error::Unknown;
        if (write || a.write)
            return Error::Blocked;
    }
    return Error::None;
}
ControlledCPU::Result ControlledCPU::TryAcquire(Range r, bool write, bool controlled) {
    if (auto e = EntryCheck(); e != Error::None)
        return {e, {}};
    if (!controlled)
        return {Error::Invalid, {}};
    try {
        std::lock_guard lock{state->mutex};
        MetadataMark mark;
        if (auto e = Available(*state, r, write); e != Error::None)
            return {e, {}};
        const Id id = state->next;
        state->accesses.emplace(
            id, Access{r, write, CpuPhase::Admitted, {}, std::this_thread::get_id()});
        ++state->next;
        state->cv.notify_all();
        return {Error::None, Lease{state, id}};
    } catch (const std::bad_alloc&) {
        return {Error::Allocation, {}};
    }
}
ControlledCPU::Result ControlledCPU::WaitAcquire(Range r, bool write, bool controlled) {
    if (auto e = EntryCheck(); e != Error::None)
        return {e, {}};
    for (;;) {
        auto result = TryAcquire(r, write, controlled);
        if (result.error != Error::Blocked)
            return result;
        if (context.gpu)
            return {Error::SelfWait, {}};
        std::unique_lock lock{state->mutex};
        MetadataMark mark;
        for (const auto& [id, a] : state->accesses) {
            (void)id;
            if (a.creator == std::this_thread::get_id() && Overlap(r, a.range) &&
                (write || a.write))
                return {Error::SelfWait, {}};
        }
        ++state->waiters;
        state->cv.notify_all();
        state->cv.wait(lock, [&] { return Available(*state, r, write) != Error::Blocked; });
        --state->waiters;
        state->cv.notify_all();
    }
}
ControlledCPU::Lease::Lease(Lease&& other) noexcept
    : state{std::move(other.state)}, id{std::exchange(other.id, 0)} {}
ControlledCPU::Lease::~Lease() {
    if (!id)
        return;
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    auto& access = state->accesses.at(id);
    if (access.phase == CpuPhase::Modified || access.phase == CpuPhase::Storing ||
        access.phase == CpuPhase::Unknown)
        access.phase = CpuPhase::Unknown;
    else
        state->accesses.erase(id);
    state->cv.notify_all(); // uncertainty remains quarantined, not falsely committed/cancelled
}
Error ControlledCPU::Lease::PreStore() {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    auto& a = state->accesses.at(id);
    if (!a.write || a.phase != CpuPhase::Admitted)
        return Error::Invalid;
    a.phase = CpuPhase::PreNotified;
    return Error::None;
}
Error ControlledCPU::Lease::Store(Id offset, const std::vector<std::uint8_t>& bytes,
                                  std::optional<std::size_t> fail_after) {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    Range range;
    {
        std::lock_guard lock{state->mutex};
        MetadataMark mark;
        auto& a = state->accesses.at(id);
        if (!a.write || (a.phase != CpuPhase::PreNotified && a.phase != CpuPhase::Modified) ||
            bytes.empty() || offset > a.range.size || bytes.size() > a.range.size - offset)
            return Error::Invalid;
        try {
            a.dirty.push_back({a.range.epoch, a.range.offset + offset, bytes.size()});
        } catch (const std::bad_alloc&) {
            return Error::Allocation; // no store and no phase publication
        }
        range = a.range;
        a.phase = CpuPhase::Storing;
    }
    const auto count = std::min(bytes.size(), fail_after.value_or(bytes.size()));
    std::copy_n(bytes.begin(), count, state->bytes.begin() + range.offset + offset);
    {
        std::lock_guard lock{state->mutex};
        MetadataMark mark;
        auto& a = state->accesses.at(id);
        a.phase = fail_after ? CpuPhase::Unknown : CpuPhase::Modified;
        state->cv.notify_all();
    }
    return fail_after ? Error::Unknown : Error::None;
}
std::pair<Error, CpuCommit> ControlledCPU::Lease::Commit() {
    if (auto e = EntryCheck(); e != Error::None)
        return {e, {}};
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    auto& a = state->accesses.at(id);
    if (a.phase == CpuPhase::Unknown)
        return {Error::Unknown, {}};
    if (a.phase != CpuPhase::Modified)
        return {Error::Invalid, {}};
    CpuCommit event;
    try {
        event = {state->commits + 1, state->epoch, a.dirty};
    } catch (const std::bad_alloc&) {
        return {Error::Allocation, {}}; // retain exclusive Modified lease; commit may be retried
    }
    ++state->commits;
    state->accesses.erase(id);
    id = 0;
    state->cv.notify_all();
    return {Error::None, std::move(event)};
}
Error ControlledCPU::Lease::Cancel() {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    auto& a = state->accesses.at(id);
    if (a.phase != CpuPhase::Admitted && a.phase != CpuPhase::PreNotified)
        return Error::Invalid;
    state->accesses.erase(id);
    id = 0;
    state->cv.notify_all();
    return Error::None;
}
Error ControlledCPU::Lease::CloseRead() {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    std::lock_guard lock{state->mutex};
    if (state->accesses.at(id).write)
        return Error::Invalid;
    state->accesses.erase(id);
    id = 0;
    state->cv.notify_all();
    return Error::None;
}
std::vector<std::uint8_t> ControlledCPU::Lease::Read() {
    Range r;
    {
        std::lock_guard lock{state->mutex};
        const auto& a = state->accesses.at(id);
        if (a.write || a.phase != CpuPhase::Admitted)
            throw std::logic_error("read requires read lease");
        r = a.range;
    }
    return {state->bytes.begin() + r.offset, state->bytes.begin() + r.offset + r.size};
}
Id ControlledCPU::PublishedCommits() {
    std::lock_guard lock{state->mutex};
    return state->commits;
}
void ControlledCPU::Seal() {
    std::lock_guard lock{state->mutex};
    state->sealed = true;
    state->cv.notify_all();
}
Error ControlledCPU::Drain() {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    Seal();
    std::unique_lock lock{state->mutex};
    MetadataMark mark;
    for (const auto& [id, a] : state->accesses) {
        (void)id;
        if (a.phase == CpuPhase::Unknown)
            return Error::Unknown;
        if (a.creator == std::this_thread::get_id())
            return Error::SelfWait;
    }
    if (context.gpu && !state->accesses.empty())
        return Error::SelfWait;
    ++state->waiters;
    state->cv.notify_all();
    state->cv.wait(lock, [&] {
        return state->accesses.empty() ||
               std::any_of(state->accesses.begin(), state->accesses.end(),
                           [](const auto& x) { return x.second.phase == CpuPhase::Unknown; });
    });
    --state->waiters;
    return state->accesses.empty() ? Error::None : Error::Unknown;
}
void ControlledCPU::WaitForWaiters(std::size_t n) {
    std::unique_lock lock{state->mutex};
    state->cv.wait(lock, [&] { return state->waiters >= n; });
}
} // namespace M3A
