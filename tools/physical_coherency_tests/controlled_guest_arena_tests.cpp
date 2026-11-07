// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
#include "controlled_guest_arena_test_access.inc"
using Arena = Common::ControlledGuestArena;
using Access = Common::ControlledGuestArenaTestAccess;
using Result = Arena::Result;
using Perm = Common::MemoryPermission;
using Stage = Common::SlowPathMetrics::Stage;
void PrintMetrics(const Arena& arena, Stage stage, const char* label) {
    const auto s = arena.Statistics(stage);
    std::cout << "METRICS " << label << " calls=" << s.calls << " host_ns=" << s.host_ns
              << " contention=" << s.contention << " refused=" << s.refused
              << " unknown=" << s.unknown << '\n';
}
static_assert(!std::is_default_constructible_v<Arena::Owner>);
static_assert(!std::is_copy_constructible_v<Arena::Owner>);
static_assert(!std::is_copy_constructible_v<Arena::Close>);
template <typename F> void Refused(F&& f) {
    bool refused{};
    try { f(); } catch (const std::runtime_error&) { refused = true; }
    assert(refused);
}
int main() {
    { // Original negative: mapping closure does not drain a pre-resolved raw writer.
        Common::HostMemory old(4096 * 8, 4096 * 4096, {}, 0, false, false, {}, true);
        old.Map(0, 0, 4096, Perm::ReadWrite, false);
        std::promise<void> resolved, go;
        auto ready = go.get_future();
        std::thread writer([&] {
            auto* p = old.VirtualBasePointer();
            resolved.set_value(); ready.wait(); p[0] = 91;
        });
        resolved.get_future().wait();
        auto frozen = old.TryFreezeMappingEntries();
        const bool held = frozen.has_value();
        go.set_value(); writer.join();
        assert(held && old.BackingBasePointer()[0] == 91);
        assert(!frozen->GuestBackingAdmission());
        assert(frozen->CancelNoEffects());
        std::cout << "PASS negative raw writer resolved before mapping closure still writes\n";
    }
    {
        Arena arena; auto owner = arena.RegisterOwner(); assert(owner);
        assert(Access::Registered(arena) == 1);
        assert(owner->Store(0, 10) && owner->Load(4096) == 10);
        auto operation = Access::Begin(*owner); assert(operation);
        auto close = arena.TryClose(8); assert(close);
        assert(close->Poll() == Result::Pending);
        assert(!close->TryFreezeHostMappings() && !close->CancelNoEffects());
        // Reject BEFORE bytes_mutex: a resolved writer is still holding it.
        assert(!owner->Store(0, 33) && !owner->Load(0));
        assert(!arena.RegisterOwner());
        assert(operation->Store(77)); operation.reset();
        assert(close->Poll() == Result::DrainedSubset);
        assert(!owner->Store(0, 55) && !arena.RegisterOwner());
        assert(close->TryFreezeHostMappings());
        auto& memory = Access::Backing(arena);
        Refused([&] { memory.Map(8192, 0, 4096, Perm::ReadWrite, false); });
        Refused([&] { memory.Map(0, 4096, 4096, Perm::ReadWrite, false); });
        Refused([&] { memory.Unmap(0, 4096, false); });
        Refused([&] { memory.Protect(0, 4096, Perm::ReadWrite); });
        assert(close->GuestBackingAdmission() == Result::Unknown);
        assert(close->CancelNoEffects());
        assert(close->Poll() == Result::Unknown && !close->CancelNoEffects());
        assert(owner->Load(4096) == 77);
        assert(arena.Statistics(Stage::Admission).calls >= 8);
        assert(arena.Statistics(Stage::Admission).refused >= 4);
        assert(arena.Statistics(Stage::Drainage).contention >= 1);
        assert(arena.Statistics(Stage::Drainage).unknown >= 1);
        assert(arena.Statistics(Stage::MappingFreeze).calls == 2);
        assert(arena.Statistics(Stage::MappingFreeze).refused == 1);
        assert(arena.Statistics(Stage::GpuCompletion).calls == 0);
        PrintMetrics(arena, Stage::Admission, "owner-admission");
        PrintMetrics(arena, Stage::Drainage, "owner-drainage");
        PrintMetrics(arena, Stage::MappingFreeze, "host-mapping-freeze");
        std::cout << "PASS real owner entry covers resolution through store; fixed alias freeze\n";
    }
    { // Exhausted polling never masquerades as drainage, even after writer exits.
        Arena arena; auto owner = arena.RegisterOwner(); auto op = Access::Begin(*owner);
        auto close = arena.TryClose(1); assert(close);
        assert(close->Poll() == Result::Unknown);
        assert(op->Store(42)); op.reset();
        assert(close->Poll() == Result::Unknown && !close->CancelNoEffects());
        assert(!close->TryFreezeHostMappings() && !owner->Store(0, 1));
    }
    for (int mode = 0; mode < 4; ++mode) {
        Arena arena; auto owner = arena.RegisterOwner(); auto op = Access::Begin(*owner);
        auto close = arena.TryClose(8); assert(close);
        if (mode == 0) owner.reset(); // early owner loss while pointer is retained
        if (mode == 1) arena.Retire();
        if (mode == 2) { try { throw std::runtime_error("abort"); } catch (...) { op.reset(); } }
        if (mode == 3) close.reset(); // unfinished closure never opens admission
        if (op) { assert(op->Store(22)); op.reset(); }
        if (close) {
            assert(close->Poll() == Result::Unknown);
            assert(!close->CancelNoEffects() && !close->TryFreezeHostMappings());
        }
        assert(!arena.RegisterOwner() && !arena.TryClose() &&
               "unfinished closure drop must keep owner admission closed");
    }
    {
        auto arena = std::make_unique<Arena>(); auto weak = Access::Lifetime(*arena);
        auto owner = arena->RegisterOwner(); auto op = Access::Begin(*owner);
        auto close = arena->TryClose(8);
        owner.reset(); arena.reset(); assert(!weak.expired());
        assert(op->Store(19)); op.reset(); assert(!weak.expired());
        assert(close->Poll() == Result::Unknown); close.reset(); assert(weak.expired());
    }
    {
        Arena arena; std::vector<Arena::Owner> owners;
        for (size_t i = 0; i < Arena::OwnerCapacity; ++i) {
            auto owner = arena.RegisterOwner(); assert(owner); owners.push_back(std::move(*owner));
        }
        assert(Access::Registered(arena) == Arena::OwnerCapacity);
        auto op = Access::Begin(owners[0]); assert(op);
        assert(!arena.RegisterOwner());
        assert(op->Store(12)); op.reset();
        assert(!arena.TryClose() && !owners[0].Store(0, 2));
    }
    {
        Arena arena; auto owner = arena.RegisterOwner();
        assert(!owner->Store(8192, 0) && !arena.TryClose());
    }
    { // Owner loss seals an idle cohort too; registration is never a caller census.
        Arena arena; auto owner = arena.RegisterOwner(); owner.reset();
        assert(Access::Registered(arena) == 0 && !arena.TryClose());
    }
    { // Idle drop must be tested independently of in-flight entry destruction.
        Arena arena; auto owner = arena.RegisterOwner();
        auto close = arena.TryClose(); assert(close);
        assert(close->Poll() == Result::DrainedSubset);
        close.reset();
        assert(!owner->Store(0, 63) && !arena.RegisterOwner() && !arena.TryClose());
    }
    { // Real data lock contention is separate from closed-admission refusal.
        Arena arena; auto owner = arena.RegisterOwner(); auto op = Access::Begin(*owner);
        bool wrote = true;
        std::thread contender([&] { wrote = owner->Store(0, 45); }); contender.join();
        assert(!wrote);
        assert(arena.Statistics(Stage::Admission).contention == 1);
        assert(op->Store(17)); op.reset();
        assert(!arena.TryClose());
    }
    { // Real existing mapping authority, with its mutation lock already held.
        Arena arena; auto owner = arena.RegisterOwner(); auto close = arena.TryClose();
        auto mutation = Access::HoldMappingMutation(arena);
        bool frozen = true;
        std::thread contender([&] { frozen = close->TryFreezeHostMappings(); }); contender.join();
        assert(!frozen);
        assert(arena.Statistics(Stage::MappingFreeze).contention == 1);
        assert(arena.Statistics(Stage::MappingFreeze).unknown == 1);
        PrintMetrics(arena, Stage::MappingFreeze, "host-mapping-contention");
        mutation.Finish();
        assert(close->Poll() == Result::Unknown && !close->CancelNoEffects());
    }
    std::cout << "PASS timeout, retirement, early owner release, exception, drop, capacity and lifetime quarantine; ordinary guest Unknown\n";
}
