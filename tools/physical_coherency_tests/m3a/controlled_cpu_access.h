// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <condition_variable>
#include <map>
#include <optional>
#include "protocol_common.h"

namespace M3A {
enum class CpuPhase { Admitted, PreNotified, Storing, Modified, Committed, Cancelled, Unknown };
struct CpuCommit {
    Id sequence{}, epoch{};
    std::vector<Range> modified;
};
class ControlledCPU {
    struct Access {
        Range range;
        bool write{};
        CpuPhase phase{CpuPhase::Admitted};
        std::vector<Range> dirty;
        std::thread::id creator;
    };
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<std::uint8_t> bytes;
        std::map<Id, Access> accesses;
        Id epoch{}, next{1}, commits{};
        bool sealed{};
        std::size_t waiters{};
    };
    std::shared_ptr<State> state;
    static Error Available(const State&, Range, bool);

public:
    class Lease {
        std::shared_ptr<State> state;
        Id id{};
        friend class ControlledCPU;
        Lease(std::shared_ptr<State> s, Id value) : state{std::move(s)}, id{value} {}

    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept;
        ~Lease();
        Error PreStore();
        Error Store(Id offset, const std::vector<std::uint8_t>& bytes,
                    std::optional<std::size_t> fail_after = {});
        std::pair<Error, CpuCommit> Commit();
        Error Cancel();
        Error CloseRead();
        std::vector<std::uint8_t> Read();
    };
    struct Result {
        Error error{};
        Lease lease;
    };
    ControlledCPU(std::vector<std::uint8_t> initial, Id epoch = 1);
    ControlledCPU(const ControlledCPU&) = delete;
    ControlledCPU& operator=(const ControlledCPU&) = delete;
    ~ControlledCPU();
    Result TryAcquire(Range range, bool write, bool controlled = true);
    Result WaitAcquire(Range range, bool write, bool controlled = true);
    Id PublishedCommits();
    void Seal();
    Error Drain();
    void WaitForWaiters(std::size_t n);
};
} // namespace M3A
