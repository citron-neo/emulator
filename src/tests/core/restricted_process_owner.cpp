// SPDX-License-Identifier: GPL-2.0-or-later
// Full native System/Kernel/KProcess/KEvent linkage; no fixture object types.
#include <future>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include "common/settings.h"
#include "core/restricted_process_owner.h"
#include "core/restricted_process_renderer.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "video_core/renderer_base.h"

TEST_CASE("Restricted native owner survives loss of external roots", "[native-restricted-owner]") {
    using Owner = Core::RestrictedProcessOwner;
    using Phase = Owner::Retirement;
    using R = Kernel::ProcessCohortStop::Result;
    using D = Common::ProducerDrain::Result;
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
    Settings::values.use_uma_zero_copy.SetValue(false);

    Owner::Lease missing;
    REQUIRE_FALSE(missing.Signal());
    REQUIRE_FALSE(missing.Clear());
    REQUIRE_FALSE(missing.Observe());
    REQUIRE_FALSE(missing.TryStopCohort());

    Settings::values.use_multi_core.SetValue(false);
    REQUIRE_FALSE(Owner::TryCreate());
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(false);
    REQUIRE_FALSE(Owner::TryCreate());
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);

    auto root = Owner::TryCreate();
    REQUIRE(root.has_value());
    auto observation = root->ObserveRetirement();
    auto lease = root->TryRetain();
    REQUIRE(lease.has_value());
    REQUIRE(lease->Observe()->initialized);
    REQUIRE(lease->Observe()->process == 2); // Actual event retains its KProcess.
    REQUIRE(lease->Observe()->event == 2);   // Actual readable event retains parent.
    REQUIRE_FALSE(Owner::TryCreate());       // Refuse a second native TLS binding.

    auto wrong_thread = std::async(std::launch::async, [&] {
        return lease->Signal() || lease->Clear() || lease->Observe().has_value() ||
               lease->TryStopCohort().has_value();
    });
    REQUIRE_FALSE(wrong_thread.get());
    REQUIRE(observation->retirement == Phase::Live);
    REQUIRE(lease->Signal());
    REQUIRE(lease->Clear());

    REQUIRE_FALSE(lease->TryStopCohort(0));
    REQUIRE_FALSE(lease->TryStopCohort(4097));
    auto cohort = lease->TryStopCohort(256);
    REQUIRE(cohort.has_value());
    auto result = cohort->Poll();
    for (unsigned i = 0; result == R::Pending && i < 64; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
        result = cohort->Poll();
    }
    REQUIRE(result == R::BackendsStopped);
    REQUIRE(cohort->GuestBackingAdmission() == D::Unknown);
    REQUIRE_FALSE(std::async(std::launch::async, [&] { return cohort->Quarantine(); }).get());
    REQUIRE(cohort->CancelNoEffects());
    REQUIRE_FALSE(cohort->CancelNoEffects());
    REQUIRE_FALSE(cohort->Quarantine());
    REQUIRE(cohort->Poll() == R::Unknown);

    REQUIRE(root->CloseAdmission());
    REQUIRE_FALSE(root->TryRetain());
    REQUIRE_FALSE(lease->TryStopCohort());
    root.reset();
    REQUIRE(observation->retirement == Phase::Live);
    REQUIRE(lease->Signal());
    REQUIRE(lease->Clear());
    lease.reset();
    // Only the cohort's own strong foundation remains; no test-scope System,
    // Window, KProcess or KEvent variable is keeping any borrow valid.
    if (observation->retirement != Phase::Live) {
        // A missing-foundation mutant must be detected before unwinding an
        // actual cohort whose borrowed Kernel may already have retired.
        std::fputs("NATIVE OWNER WITNESS: cohort foundation retired early\n", stderr);
        std::_Exit(91);
    }
    REQUIRE(observation->retirement == Phase::Live);
    cohort.reset();
    REQUIRE(observation->retirement == Phase::Released);
    REQUIRE_FALSE(Owner::TryCreate()); // Even released native Kernel TLS is not reused.
}

// Each terminal control runs in its own native process. Exit 86 and the exact
// stage/reason are required; a crash, timeout or other exit is a failed control.
TEST_CASE("Restricted owner rejects foreign final release", "[.][native-owner-terminal]") {
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
    auto root = Core::RestrictedProcessOwner::TryCreate();
    REQUIRE(root);
    std::thread worker{[root = std::move(root)]() mutable { root.reset(); }};
    worker.join();
    FAIL("foreign final release returned");
}

TEST_CASE("Restricted cohort rejects foreign release", "[.][native-cohort-terminal]") {
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
    auto root = Core::RestrictedProcessOwner::TryCreate();
    REQUIRE(root);
    auto lease = root->TryRetain();
    REQUIRE(lease);
    auto cohort = lease->TryStopCohort();
    REQUIRE(cohort);
    std::thread worker{[cohort = std::move(cohort)]() mutable { cohort.reset(); }};
    worker.join();
    FAIL("foreign cohort release returned");
}

// These filters also require separate processes: native Kernel TLS is not reused.
TEST_CASE("Restricted foundation outlives an Unknown native cohort", "[.][native-owner-unknown]") {
    using Owner = Core::RestrictedProcessOwner;
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
    auto root = Owner::TryCreate();
    REQUIRE(root);
    auto observation = root->ObserveRetirement();
    auto lease = root->TryRetain();
    REQUIRE(lease);
    auto cohort = lease->TryStopCohort();
    REQUIRE(cohort);
    REQUIRE(cohort->Quarantine());
    REQUIRE(cohort->Poll() == Kernel::ProcessCohortStop::Result::Unknown);
    REQUIRE_FALSE(cohort->CancelNoEffects());
    root.reset();
    lease.reset();
    REQUIRE(observation->retirement == Owner::Retirement::Live);
    cohort.reset();
    REQUIRE(observation->retirement == Owner::Retirement::Released);
}

TEST_CASE("Restricted foundation outlives an unpolled native cohort", "[.][native-owner-unpolled]") {
    using Owner = Core::RestrictedProcessOwner;
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
    auto root = Owner::TryCreate();
    REQUIRE(root);
    auto observation = root->ObserveRetirement();
    auto lease = root->TryRetain();
    REQUIRE(lease);
    auto cohort = lease->TryStopCohort();
    REQUIRE(cohort);
    root.reset();
    lease.reset();
    REQUIRE(observation->retirement == Owner::Retirement::Live);
    cohort.reset();
    REQUIRE(observation->retirement == Owner::Retirement::Released);
}

TEST_CASE("Restricted cold backend never manufactures fence completion", "[.][native-owner-forbidden-gpu]") {
    class Window final : public Core::Frontend::EmuWindow {
    public:
        std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
            return std::make_unique<Core::Frontend::GraphicsContext>();
        }
        bool IsShown() const override { return false; }
    } window;
    auto renderer = Core::Detail::CreateRestrictedProcessRenderer(window);
    renderer->ReadRasterizer()->SignalFence([] { std::_Exit(92); });
    FAIL("restricted backend accepted a fence");
}
