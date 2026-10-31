// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <utility>

namespace VideoCommon {

enum class CallbackOutcome { Completed, Cancelled, Failed };

// Dispatch must return only after execution, rejection, or cancellation of its
// blocking queue wait. Queue acknowledgement alone is NOT callback completion.
// No GPU/device completion is implied; the action must establish visibility.
template <typename Dispatch>
CallbackOutcome RunAcknowledgedCallback(Dispatch&& dispatch, std::function<void()> action) {
    struct Request {
        enum class Admission { Pending, Running, Cancelled };
        std::atomic<Admission> admission{Admission::Pending};
        std::promise<void> completion;
    };
    auto request = std::make_shared<Request>();
    auto completion = request->completion.get_future();
    bool dispatch_failed = false;
    try {
        dispatch([request, callback_action = std::move(action)] {
            auto expected = Request::Admission::Pending;
            if (!request->admission.compare_exchange_strong(expected, Request::Admission::Running)) {
                return; // cancellation won; never touch the callback's receiver
            }
            try {
                callback_action();
                request->completion.set_value();
            } catch (...) {
                request->completion.set_exception(std::current_exception());
                throw; // preserve the GPU worker's existing failure handling
            }
        });
    } catch (...) {
        // Dispatch may throw after retaining or starting the action. Use the
        // same admission race below: cancel Pending, or join Running before
        // reporting failure. Never leave a runnable action behind the caller.
        dispatch_failed = true;
    }
    auto expected = Request::Admission::Pending;
    if (request->admission.compare_exchange_strong(expected, Request::Admission::Cancelled)) {
        // Includes rejected admission and failed-mode queue acknowledgement.
        // A retained queued closure cannot subsequently start the action.
        return dispatch_failed ? CallbackOutcome::Failed : CallbackOutcome::Cancelled;
    }
    // A shutdown wakeup may precede an active callback's return. Wait for the
    // actual callback here, without holding cache/PTE or queue locks.
    try {
        completion.get();
        return dispatch_failed ? CallbackOutcome::Failed : CallbackOutcome::Completed;
    } catch (...) {
        return CallbackOutcome::Failed;
    }
}

} // namespace VideoCommon
