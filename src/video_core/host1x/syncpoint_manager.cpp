// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "video_core/host1x/syncpoint_manager.h"

namespace Tegra {

namespace Host1x {

SyncpointManager::ActionHandle SyncpointManager::RegisterAction(std::atomic<u32>& syncpoint, std::vector<RegisteredAction>& action_storage, u32 expected_value, std::function<void()>&& action) {
    if (syncpoint.load(std::memory_order_acquire) >= expected_value) {
        action();
        return {};
    }

    std::unique_lock lk(guard);
    if (syncpoint.load(std::memory_order_relaxed) >= expected_value) {
        action();
        return {};
    }
    const auto it = std::ranges::upper_bound(action_storage, expected_value, {},
                                             &RegisteredAction::expected_value);
    const ActionHandle id = ++next_action_id;
    action_storage.insert(it, RegisteredAction{id, expected_value, std::move(action)});
    return id;
}

void SyncpointManager::DeregisterAction(std::vector<RegisteredAction>& action_storage, ActionHandle handle) {
    if (handle == ActionHandle{}) {
        return;
    }
    // Captures can own resource-retirement callbacks. Release them after guard,
    // including on cancellation; their destructors may re-enter this manager.
    std::function<void()> retired_action;
    std::unique_lock lk(guard);
    // Gone already if the action ran
    const auto it = std::ranges::find(action_storage, handle, &RegisteredAction::id);
    if (it != action_storage.end()) {
        retired_action = std::move(it->action);
        action_storage.erase(it);
    }
}

void SyncpointManager::DeregisterGuestAction(u32 syncpoint_id, ActionHandle handle) {
    DeregisterAction(guest_action_storage[syncpoint_id], handle);
}

void SyncpointManager::DeregisterHostAction(u32 syncpoint_id, ActionHandle handle) {
    DeregisterAction(host_action_storage[syncpoint_id], handle);
}

void SyncpointManager::IncrementGuest(u32 syncpoint_id) {
    Increment(syncpoints_guest[syncpoint_id], wait_guest_cv, guest_action_storage[syncpoint_id]);
}

void SyncpointManager::IncrementHost(u32 syncpoint_id) {
    Increment(syncpoints_host[syncpoint_id], wait_host_cv, host_action_storage[syncpoint_id]);
}

void SyncpointManager::WaitGuest(u32 syncpoint_id, u32 expected_value) {
    Wait(syncpoints_guest[syncpoint_id], wait_guest_cv, expected_value);
}

void SyncpointManager::WaitHost(u32 syncpoint_id, u32 expected_value) {
    Wait(syncpoints_host[syncpoint_id], wait_host_cv, expected_value);
}

void SyncpointManager::Increment(std::atomic<u32>& syncpoint, std::condition_variable& wait_cv, std::vector<RegisteredAction>& action_storage) {
    auto new_value{syncpoint.fetch_add(1, std::memory_order_acq_rel) + 1};

    // Declare before lk so completed captures retire outside guard, also when a
    // later callback throws. Execution/order of callbacks remains under guard.
    std::vector<std::function<void()>> retired_actions;
    std::unique_lock lk(guard);
    const auto ready_end = std::ranges::upper_bound(action_storage, new_value, {},
                                                    &RegisteredAction::expected_value);
    // Reserve before any callback effect/erasure; moving functions then cannot
    // allocate midway through retirement. A failed reserve leaves actions intact.
    retired_actions.reserve(static_cast<size_t>(ready_end - action_storage.begin()));
    auto it = action_storage.begin();
    while (it != action_storage.end()) {
        if (it->expected_value > new_value) {
            break;
        }
        it->action();
        retired_actions.push_back(std::move(it->action));
        it = action_storage.erase(it);
    }
    wait_cv.notify_all();
}

void SyncpointManager::Wait(std::atomic<u32>& syncpoint, std::condition_variable& wait_cv, u32 expected_value) {
    const auto pred = [&]() { return syncpoint.load(std::memory_order_acquire) >= expected_value; };
    if (pred()) {
        return;
    }

    std::unique_lock lk(guard);
    wait_cv.wait(lk, pred);
}

} // namespace Host1x

} // namespace Tegra
