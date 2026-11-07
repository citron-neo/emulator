// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <queue>
#include <thread>
#include <variant>
#include <boost/container/small_vector.hpp>
#include <boost/version.hpp>
#include "common/bounded_threadsafe_queue.h"
// puller.h expects this declaration from its caller in the production include order.
namespace Tegra { class GPU; }
#include "video_core/dma_pusher.h"

namespace Allocation {
thread_local bool count = false;
thread_local std::size_t calls = 0;
thread_local std::size_t bytes = 0;
}
void* operator new(std::size_t size) {
    if (Allocation::count) {
        ++Allocation::calls;
        Allocation::bytes += size;
    }
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

struct NvFence { u32 id; u32 value; };
#include "builders.h"

namespace {
using Clock = std::chrono::steady_clock;
enum class Path { None, Wait, Increment, Wfi, WaitIncrement, WaitWfi };
constexpr std::array paths{Path::None, Path::Wait, Path::Increment, Path::Wfi,
                           Path::WaitIncrement, Path::WaitWfi};
constexpr std::array names{"none", "wait", "increment", "wfi", "wait_increment", "wait_wfi"};
std::atomic<u64> sink{};

template <bool LegacyStorage>
using List = std::conditional_t<LegacyStorage, Legacy::CommandList, Tegra::CommandList>;

template <bool Old>
auto Fence(Path path, NvFence fence) {
    if constexpr (Old) {
        if (path == Path::Wait) return LegacyBuilders::BuildWaitCommandList(fence);
        if (path == Path::Increment) return LegacyBuilders::BuildIncrementCommandList(fence);
        return LegacyBuilders::BuildIncrementWithWfiCommandList(fence);
    } else {
        if (path == Path::Wait) return CurrentBuilders::BuildWaitCommandList(fence);
        if (path == Path::Increment) return CurrentBuilders::BuildIncrementCommandList(fence);
        return CurrentBuilders::BuildIncrementWithWfiCommandList(fence);
    }
}

// Match the SubmitListCommand -> CommandData -> CommandDataContainer move boundaries.
template <bool Old>
struct Submit {
    s32 channel;
    List<Old> entries;
    Submit(s32 channel_, List<Old>&& entries_) : channel{channel_}, entries{std::move(entries_)} {}
};
template <bool Old>
struct Container {
    using Data = std::variant<std::monostate, Submit<Old>>;
    Data data;
    u64 fence{};
    bool block{};
    Container() = default;
    Container(Data&& data_, u64 fence_, bool block_)
        : data{std::move(data_)}, fence{fence_}, block{block_} {}
};

template <bool Old>
class Lifecycle {
public:
    explicit Lifecycle(bool threaded_) : threaded{threaded_} {
        if (threaded) worker = std::thread([this] {
            Container<Old> next;
            for (;;) {
                queue.PopWait(next);
                if (std::holds_alternative<std::monostate>(next.data)) break;
                Consume(next);
                std::scoped_lock lock{ack_mutex};
                completed = next.fence;
                ack.notify_one();
            }
        });
    }
    ~Lifecycle() {
        if (threaded) {
            typename Container<Old>::Data stop;
            queue.EmplaceWait(std::move(stop), 0, false);
            worker.join();
        }
    }
    void Push(List<Old>&& entries) {
        typename Container<Old>::Data data{Submit<Old>{0, std::move(entries)}};
        queue.EmplaceWait(std::move(data), ++submitted, false);
        if (!threaded) {
            if (!queue.TryPop(next)) std::abort();
            Consume(next);
        }
    }
    void Complete() {
        if (threaded) {
            std::unique_lock lock{ack_mutex};
            ack.wait(lock, [this] { return completed == submitted; });
        }
    }
private:
    void Consume(Container<Old>& command) {
        auto& entries = std::get<Submit<Old>>(command.data).entries;
        dma.push(std::move(entries));
        // Read every payload word so copies and builders cannot be optimized away.
        u64 checksum = 0;
        for (const auto& h : dma.front().command_lists) checksum += h.raw;
        for (const auto& h : dma.front().prefetch_command_list) checksum += h.argument;
        dma.pop();
        sink.fetch_add(checksum, std::memory_order_relaxed);
    }
    Common::MPSCQueue<Container<Old>> queue;
    Container<Old> next;
    std::queue<List<Old>> dma;
    bool threaded;
    std::thread worker;
    std::mutex ack_mutex;
    std::condition_variable ack;
    u64 submitted{}, completed{};
};

template <bool Old>
void SubmitSubmission(Lifecycle<Old>& lifecycle, std::size_t size, Path path, u32 sequence) {
    const NvFence fence{7, sequence};
    List<Old> entries{size};
    // Same sized construction + memcpy as SubmitGPFIFOBase1/2, excluding guest memory access.
    std::array<Tegra::CommandListHeader, 512> input;
    for (std::size_t i = 0; i < size; ++i) input[i].raw = (u64{sequence} << 32) + i;
    if (size) std::memcpy(entries.command_lists.data(), input.data(), size * sizeof(input[0]));
    if (path == Path::Wait || path == Path::WaitIncrement || path == Path::WaitWfi)
        lifecycle.Push(List<Old>{Fence<Old>(Path::Wait, fence)});
    lifecycle.Push(std::move(entries));
    if (path == Path::Increment || path == Path::WaitIncrement)
        lifecycle.Push(List<Old>{Fence<Old>(Path::Increment, fence)});
    if (path == Path::Wfi || path == Path::WaitWfi)
        lifecycle.Push(List<Old>{Fence<Old>(Path::Wfi, fence)});
    lifecycle.Complete();
}

template <bool Old>
u64 Report(std::size_t size, Path path, bool threaded) {
    const u64 checksum_before = sink.load();
    auto lifecycle = std::make_unique<Lifecycle<Old>>(threaded);
    for (u32 i = 0; i < 200; ++i) SubmitSubmission(*lifecycle, size, path, i);
    // Counts use a separate serial pass, including queue/deque allocations, with identical work.
    double allocations = 0, bytes = 0;
    if (!threaded) {
        Allocation::calls = Allocation::bytes = 0;
        Allocation::count = true;
        for (u32 i = 0; i < 1000; ++i) SubmitSubmission(*lifecycle, size, path, i);
        Allocation::count = false;
        allocations = Allocation::calls / 1000.0;
        bytes = Allocation::bytes / 1000.0;
    }
    constexpr std::size_t repeats = 7;
    const u32 iterations = threaded ? 2000 : 20000;
    std::array<double, repeats> times;
    for (auto& time : times) {
        const auto start = Clock::now();
        for (u32 i = 0; i < iterations; ++i) SubmitSubmission(*lifecycle, size, path, i);
        time = std::chrono::duration<double, std::nano>(Clock::now() - start).count() / iterations;
    }
    std::ranges::sort(times);
    std::printf("%s,%s,%zu,%s,", Old ? "small_vector" : "vector",
        threaded ? "threaded" : "serial", size, names[static_cast<unsigned>(path)]);
    if (threaded) std::printf(",,"); // Counts are measured in the separate serial pass.
    else std::printf("%.3f,%.3f,", allocations, bytes);
    std::printf("%.1f,%.1f,%.1f\n", times[repeats / 2], times.front(), times.back());
    return sink.load() - checksum_before;
}

template <bool Old>
void ReportBuilder(Path path) {
    Allocation::calls = Allocation::bytes = 0;
    Allocation::count = true;
    {
        auto words = Fence<Old>(path, {7, 42});
        u64 checksum = 0;
        for (const auto& word : words) checksum += word.argument;
        sink.fetch_add(checksum, std::memory_order_relaxed);
    }
    Allocation::count = false;
    const auto calls = Allocation::calls, bytes = Allocation::bytes;
    std::array<double, 7> times;
    for (auto& time : times) {
        const auto start = Clock::now();
        for (u32 i = 0; i < 100000; ++i) {
            auto words = Fence<Old>(path, {7, i});
            u64 checksum = 0;
            for (const auto& word : words) checksum += word.argument;
            sink.fetch_add(checksum, std::memory_order_relaxed);
        }
        time = std::chrono::duration<double, std::nano>(Clock::now() - start).count() / 100000;
    }
    std::ranges::sort(times);
    std::printf("%s,builder,0,%s,%zu,%zu,%.1f,%.1f,%.1f\n", Old ? "small_vector" : "vector",
        names[static_cast<unsigned>(path)], calls, bytes, times[3], times.front(), times.back());
}

void ValidateBuilders() {
    for (auto path : {Path::Wait, Path::Increment, Path::Wfi}) {
        const auto current = Fence<false>(path, {7, 42});
        const auto old = Fence<true>(path, {7, 42});
        const std::size_t expected = path == Path::Wait ? 4 : path == Path::Increment ? 6 : 8;
        if (current.size() != expected || old.size() != expected) std::abort();
        for (std::size_t i = 0; i < current.size(); ++i)
            if (current[i].argument != old[i].argument) std::abort();
    }
}
} // namespace

int main() {
    ValidateBuilders();
    std::fprintf(stderr, "Boost=%s, sizeof(CommandList): vector=%zu small_vector=%zu\n",
        BOOST_LIB_VERSION, sizeof(Tegra::CommandList), sizeof(Legacy::CommandList));
    std::puts("storage,mode,entries,path,allocations,allocated_bytes,median_ns,min_ns,max_ns");
    for (auto path : {Path::Wait, Path::Increment, Path::Wfi}) {
        ReportBuilder<false>(path);
        ReportBuilder<true>(path);
    }
    for (bool threaded : {false, true})
        for (std::size_t size : {0, 1, 4, 16, 64, 128, 256, 512})
            for (auto path : paths) {
                u64 current, old;
                // Alternate storage order across paths to reduce systematic warm-cache bias.
                if (static_cast<unsigned>(path) % 2 == 0) {
                    current = Report<false>(size, path, threaded);
                    old = Report<true>(size, path, threaded);
                } else {
                    old = Report<true>(size, path, threaded);
                    current = Report<false>(size, path, threaded);
                }
                if (current != old) {
                    std::fputs("Submission payload checksum mismatch\n", stderr);
                    return 1;
                }
            }
    std::fprintf(stderr, "checksum=%llu\n", static_cast<unsigned long long>(sink.load()));
}
