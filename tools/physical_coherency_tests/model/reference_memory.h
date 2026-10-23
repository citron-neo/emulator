// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

// Independent oracle: no model include, versions, cells, producers, replicas,
// ownership or transfer logic. Replays guest-ordered byte stores from initial
// memory. Completion gates observations, never changes replay order.
namespace Oracle {
using Bytes = std::vector<std::uint8_t>;
using EventId = std::uint64_t;
struct Store {
    std::size_t offset{};
    Bytes data;
};
struct Range {
    std::size_t offset{}, size{};
};
class ReferenceMemory {
public:
    explicit ReferenceMemory(Bytes initial);
    EventId Accept(std::vector<Store> stores, std::vector<EventId> after = {});
    bool Complete(EventId event);
    bool Cancel(EventId event);
    std::optional<Bytes> Read(const std::vector<Range>& logical) const;
    std::vector<Store> DMASnapshot(const std::vector<Range>& src,
                                   const std::vector<Range>& dst) const;
    void Reset(Bytes initial);

private:
    struct Event {
        std::vector<Store> stores;
        std::vector<EventId> after;
        bool completed{}, cancelled{};
    };
    Bytes initial;
    EventId next{};
    std::map<EventId, Event> journal;
};
} // namespace Oracle
