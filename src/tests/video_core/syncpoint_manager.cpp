// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "video_core/host1x/syncpoint_manager.h"

using Tegra::Host1x::SyncpointManager;

TEST_CASE("SyncpointManager: cancelling one waiter keeps the others", "[video_core][host1x]") {
    auto manager = std::make_unique<SyncpointManager>();
    constexpr u32 id = 2;
    bool later_fired = false;
    bool earlier_fired = false;

    // The second waiter sorts in front of the first, which moves the first one in storage
    const auto later = manager->RegisterHostAction(id, 2, [&] { later_fired = true; });
    manager->RegisterHostAction(id, 1, [&] { earlier_fired = true; });
    manager->DeregisterHostAction(id, later);

    manager->IncrementHost(id);
    REQUIRE(earlier_fired);
    manager->IncrementHost(id);
    REQUIRE_FALSE(later_fired);
}

TEST_CASE("SyncpointManager: actions run in value order and only once", "[video_core][host1x]") {
    auto manager = std::make_unique<SyncpointManager>();
    constexpr u32 id = 5;
    int order[3]{};
    int next = 0;

    const auto third = manager->RegisterGuestAction(id, 3, [&] { order[2] = ++next; });
    manager->RegisterGuestAction(id, 1, [&] { order[0] = ++next; });
    manager->RegisterGuestAction(id, 2, [&] { order[1] = ++next; });

    for (int i = 0; i < 3; ++i) {
        manager->IncrementGuest(id);
    }
    REQUIRE(order[0] == 1);
    REQUIRE(order[1] == 2);
    REQUIRE(order[2] == 3);

    // The action ran, so its handle is stale and deregistering it does nothing
    manager->DeregisterGuestAction(id, third);
    manager->IncrementGuest(id);
    REQUIRE(next == 3);
}

TEST_CASE("SyncpointManager: a reached value runs the action at once", "[video_core][host1x]") {
    auto manager = std::make_unique<SyncpointManager>();
    constexpr u32 id = 7;
    manager->IncrementHost(id);

    bool fired = false;
    const auto handle = manager->RegisterHostAction(id, 1, [&] { fired = true; });
    REQUIRE(fired);
    REQUIRE(handle == SyncpointManager::ActionHandle{});
    manager->DeregisterHostAction(id, handle);
}
