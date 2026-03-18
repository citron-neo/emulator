// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

//
// TODO: remove this file when jthread is supported by all compilation targets
//

#pragma once

#include <version>
#include <chrono>
#include <condition_variable>
#if __has_include(<stop_token>)
#include <stop_token>
#endif
#include <thread>
#include <utility>

namespace Common {
template <typename Condvar, typename Lock, typename Pred>
void CondvarWait(Condvar& cv, std::unique_lock<Lock>& lk, std::stop_token token, Pred&& pred) {
    cv.wait(lk, token, std::forward<Pred>(pred));
}
template <typename Rep, typename Period>
bool StoppableTimedWait(std::stop_token token, const std::chrono::duration<Rep, Period>& rel_time) {
    std::condition_variable_any cv;
    std::mutex m;

    // Perform the timed wait.
    std::unique_lock lk{m};
    return !cv.wait_for(lk, token, rel_time, [&] { return token.stop_requested(); });
}
} // namespace Common
