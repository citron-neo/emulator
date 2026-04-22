// I hate human beings
#pragma once
#include <cstdint>
#include <thread>
#include <atomic>

namespace Common {
// fuck recursive mutex
struct ThreadIdMutex {
    std::atomic<uint32_t> id{0};
    std::atomic<uint32_t> count{0};

    [[nodiscard]] inline uint32_t get_id() noexcept {
        static thread_local std::thread::id tls_id = std::this_thread::get_id();
        return uint32_t(std::hash<std::thread::id>{}(tls_id));
    }

    inline void lock() noexcept {
        while (!try_lock())
#ifdef ARCHITECTURE_x86_64
            asm volatile ("pause");
#elif defined(ARCHITECTURE_arm64)
            asm volatile ("yield");
#endif
    }

    inline void unlock() noexcept {
        --count;
        if (count <= 0)
            id.store(0, std::memory_order_release);
    }

    [[nodiscard]] inline bool try_lock() noexcept {
        uint32_t expected = count > 0 ? get_id() : 0;
        bool result = id.compare_exchange_strong(expected, get_id(), std::memory_order_acquire);;
        if (result)
            ++count;
        return result;
    }
};
}
