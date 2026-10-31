// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <future>
#include <iostream>
#include <thread>
#include "core/hle/kernel/k_process.h"

namespace Core {
struct SchedulerTestAccess {
    static auto Hold(Kernel::ProcessOwnerRoster& roster) {
        return std::unique_lock{roster.state->mutex};
    }
};
}

int main() {
    using Kernel::KProcess;
    using Kernel::KScopedAutoObject;
    { // Close owns unregister; slab Free never invokes the C++ destructor.
        Kernel::KernelCore kernel;const Kernel::KAutoObject* identity{};
        bool member_destroyed = false;
        {
            KProcess owner{kernel};identity=&owner;
            owner.on_member_destroy = [&] {member_destroyed=true;};
            owner.Close();
            assert(owner.finalized && !member_destroyed);
            assert(kernel.UnregisterCount(identity)==1 && "Close must unregister exactly once");
            // Fixture storage later uses C++ cleanup. The consumed registration
            // cannot take responsibility for kernel unregistration a second time.
        }
        assert(member_destroyed && kernel.UnregisterCount(identity)==1 &&
               "Close must own the only unregister after consumed registration");
    }
    { // Direct construction/destruction never calls Close.
        Kernel::KernelCore kernel;const Kernel::KAutoObject* identity{};
        {
            KProcess owner{kernel,false};identity=&owner;
            assert(kernel.UnregisterCount(identity)==0);
        }
        assert(kernel.UnregisterCount(identity)==1 &&
               "direct C++ destruction must unregister exactly once");
    }
    { // Registered at construction, without OS wrapper publication or Initialize.
        Kernel::KernelCore kernel;
        KProcess first{kernel}, second{kernel};
        auto snapshot = kernel.roster.TryCapture();
        assert(snapshot && snapshot->IsCurrent() && snapshot->Size() == 2);
        assert(first.GetReferenceCount() == 2 && second.GetReferenceCount() == 2 &&
               "snapshot must own real references");
        assert(snapshot->Retains(&first) && snapshot->Retains(&second));
        first.Close();second.Close();
        assert(!first.finalized && !second.finalized);
        auto moved = std::move(*snapshot);
        assert(!snapshot->IsCurrent() && moved.IsCurrent());
        snapshot.reset();
        assert(!first.finalized && moved.Retains(&first));
        first.on_finalize = [&] {
            assert(!kernel.roster.TryCapture() && "retiring owner must refuse capture");
        };
        // Last references release after the snapshot leaves scope, without its lock.
    }
    { // Zero-ref constructors cannot be resurrected; no Close on failed TryOpen.
        Kernel::KernelCore kernel;
        KProcess constructing{kernel, false};
        assert(!KScopedAutoObject<KProcess>::TryOpen(&constructing));
        assert(!KScopedAutoObject<KProcess>::TryOpen(nullptr));
        assert(!kernel.roster.TryCapture() && constructing.GetReferenceCount() == 0);
        Kernel::KAutoObject::Create(&constructing);
        auto snapshot = kernel.roster.TryCapture();
        assert(snapshot && snapshot->Retains(&constructing));
        snapshot.reset();constructing.Close();
    }
    { // New construction invalidates old inventory while its old pins stay owned.
        Kernel::KernelCore kernel;KProcess first{kernel};
        auto before = kernel.roster.TryCapture();assert(before && before->IsCurrent());
        KProcess later{kernel};
        assert(!before->IsCurrent() && "new owner must invalidate prior inventory");
        assert(before->Retains(&first) && !before->Retains(&later));
        auto after = kernel.roster.TryCapture();assert(after && after->Size() == 2);
        before.reset();after.reset();first.Close();later.Close();
    }
    { // Normal C++ destruction remains retiring until ALL members finish cleanup.
        Kernel::KernelCore kernel;bool cleaned = false;
        {
            KProcess local{kernel, false};
            local.on_member_destroy = [&] {
                assert(!kernel.roster.TryCapture() && "member destruction must remain in inventory");
                cleaned = true;
            };
        }
        auto after = kernel.roster.TryCapture();
        assert(cleaned && after && after->IsCurrent() && after->Size() == 0);
    }
    { // Finalize failure leaves a durable retiring record; no destructor removes it.
        Kernel::KernelCore kernel;
        {
            KProcess failing{kernel};
            failing.on_finalize = [] { throw std::runtime_error("injected finalize failure"); };
            bool threw = false;
            try { failing.Close(); } catch (const std::runtime_error&) { threw = true; }
            assert(threw && !kernel.roster.TryCapture());
        }
        assert(!kernel.roster.TryCapture() && "failed retirement must not disappear");
    }
    { // Free/address reuse before old completion must preserve the new registration.
        Kernel::KernelCore kernel;KProcess original{kernel};
        u64 replacement{};
        original.on_free = [&] { replacement = kernel.RegisterProcessOwner(&original); };
        original.Close();
        auto after = kernel.roster.TryCapture();
        assert(!after && "old retirement must not erase reused address registration");
        // The replacement still has zero refs. Adopt it and prove it is in the roster.
        Kernel::KAutoObject::Create(&original);
        auto adopted = kernel.roster.TryCapture();
        assert(adopted && adopted->Retains(&original) && adopted->Size() == 1 &&
               "exact retirement must preserve replacement registration");
        adopted.reset();original.AdoptReplacementForTest(replacement);
        original.on_free = {};
        original.Close();
    }
    { // A partial capture releases its earlier pins outside the registry lock.
        Kernel::KernelCore kernel;KProcess first{kernel}, unfinished{kernel, false};
        unfinished.before_open = [&] { first.Close(); };
        first.on_finalize = [&] {
            // This takes the real mutex. It would deadlock if partial pin release
            // ran under TryCapture's lock after the second owner refused Open.
            const auto temporary = kernel.RegisterProcessOwner(&first);
            kernel.CompleteProcessOwnerRetirement(temporary);
        };
        assert(!kernel.roster.TryCapture());
        assert(first.finalized && first.GetReferenceCount() == 0 && unfinished.GetReferenceCount() == 0);
    }
    { // No waits on the registry lock; validation also refuses contention.
        Kernel::KernelCore kernel;KProcess first{kernel};
        auto snapshot = kernel.roster.TryCapture();assert(snapshot);
        std::promise<void> held, release;auto wait = release.get_future();
        std::thread holder{[&] {
            auto lock = Core::SchedulerTestAccess::Hold(kernel.roster);
            held.set_value();wait.wait();
        }};
        held.get_future().wait();
        const bool captured = kernel.roster.TryCapture().has_value();
        const bool current = snapshot->IsCurrent();
        release.set_value();holder.join();
        assert(!captured && !current && snapshot->IsCurrent());
        snapshot.reset();first.Close();
    }
    std::cout << "PASS process owner roster: actual references, lifecycle retirement, reuse, "
                 "failure retention and nonblocking snapshot validation; no guest admission\n";
}
