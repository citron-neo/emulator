// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <stdexcept>
#include "reference_memory.h"

namespace Oracle {
ReferenceMemory::ReferenceMemory(Bytes bytes) : initial{std::move(bytes)} {}
EventId ReferenceMemory::Accept(std::vector<Store> stores, std::vector<EventId> after) {
    for (const auto& s : stores)
        if (s.offset > initial.size() || s.data.size() > initial.size() - s.offset)
            throw std::invalid_argument("oracle store outside memory");
    for (auto id : after)
        if (!journal.contains(id) || journal.at(id).cancelled)
            throw std::invalid_argument("oracle dependency missing");
    journal.emplace(++next, Event{std::move(stores), std::move(after)});
    return next;
}
bool ReferenceMemory::Complete(EventId id) {
    auto& e = journal.at(id);
    if (e.cancelled)
        return false;
    for (auto dep : e.after)
        if (!journal.at(dep).completed || journal.at(dep).cancelled)
            return false;
    e.completed = true;
    return true;
}
bool ReferenceMemory::Cancel(EventId id) {
    auto& e = journal.at(id);
    if (e.completed)
        return false;
    for (const auto& [other, p] : journal)
        if (other != id && !p.cancelled &&
            std::find(p.after.begin(), p.after.end(), id) != p.after.end())
            return false;
    e.cancelled = true;
    return true;
}
std::optional<Bytes> ReferenceMemory::Read(const std::vector<Range>& logical) const {
    Bytes memory = initial;
    std::vector<bool> unavailable(initial.size());
    // Recompute directly in guest sequence. Later stores supersede earlier
    // completed contents; an unfinished latest store makes its bytes unreadable.
    for (const auto& [id, e] : journal) {
        (void)id;
        if (e.cancelled)
            continue;
        for (const auto& store : e.stores)
            for (std::size_t i = 0; i < store.data.size(); ++i) {
                unavailable[store.offset + i] = !e.completed;
                if (e.completed)
                    memory[store.offset + i] = store.data[i];
            }
    }
    Bytes out;
    for (const auto& r : logical) {
        if (r.offset > initial.size() || r.size > initial.size() - r.offset)
            return {};
        for (std::size_t i = 0; i < r.size; ++i) {
            if (unavailable[r.offset + i])
                return {};
            out.push_back(memory[r.offset + i]);
        }
    }
    return out;
}
std::vector<Store> ReferenceMemory::DMASnapshot(const std::vector<Range>& src,
                                                const std::vector<Range>& dst) const {
    const auto owned = Read(src);
    if (!owned)
        throw std::logic_error("oracle DMA source unavailable");
    std::vector<Store> out;
    std::size_t logical = 0;
    for (const auto& r : dst) {
        if (r.size > owned->size() - logical)
            throw std::invalid_argument("oracle DMA lengths");
        out.push_back(
            {r.offset, Bytes(owned->begin() + logical, owned->begin() + logical + r.size)});
        logical += r.size;
    }
    if (logical != owned->size())
        throw std::invalid_argument("oracle DMA lengths");
    return out;
}
void ReferenceMemory::Reset(Bytes bytes) {
    initial = std::move(bytes);
    journal.clear();
    next = 0;
}
} // namespace Oracle
