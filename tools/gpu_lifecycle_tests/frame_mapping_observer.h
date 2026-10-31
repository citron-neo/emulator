// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <functional>
#include <mutex>
inline thread_local std::function<void()> on_wait;
// Observe a real mutex wait before allowing the opposing operation to proceed.
class ObservedMutex {
    std::mutex mutex;
public:
    void lock() {
        if (mutex.try_lock()) return;
        if (on_wait) on_wait();
        mutex.lock();
    }
    bool try_lock() { return mutex.try_lock(); }
    void unlock() { mutex.unlock(); }
};
