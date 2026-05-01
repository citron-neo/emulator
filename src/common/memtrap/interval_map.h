// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 citron Emulator Project
//
// Adapted from Skyline emulator (MPL-2.0):
//   app/src/main/cpp/skyline/common/interval_map.h
//   © 2022 Skyline Team and Contributors
//
// Redistribution under GPL-2.0-or-later is permitted because the original
// MPL-2.0 grant allows relicensing combined works under a compatible license.

#pragma once

#include <algorithm>
#include <iterator>
#include <list>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/common_types.h"

namespace Common::MemTrap {

template <typename AddressType, typename EntryType>
class IntervalMap {
public:
    struct Interval {
        AddressType start{};
        AddressType end{};

        Interval() = default;
        Interval(AddressType start_, AddressType end_) : start{start_}, end{end_} {}

        size_t Size() const {
            return static_cast<size_t>(end - start);
        }

        Interval Align(size_t alignment) const {
            const auto align_down = [alignment](AddressType addr) {
                if constexpr (std::is_pointer_v<AddressType>) {
                    using Pointee = std::remove_pointer_t<AddressType>;
                    const auto raw = reinterpret_cast<uintptr_t>(addr);
                    return reinterpret_cast<AddressType>(raw - (raw % alignment));
                } else {
                    return Common::AlignDown(addr, alignment);
                }
            };
            const auto align_up = [alignment](AddressType addr) {
                if constexpr (std::is_pointer_v<AddressType>) {
                    using Pointee = std::remove_pointer_t<AddressType>;
                    const auto raw = reinterpret_cast<uintptr_t>(addr);
                    const auto rem = raw % alignment;
                    const auto adjusted = rem == 0 ? raw : raw + (alignment - rem);
                    return reinterpret_cast<AddressType>(adjusted);
                } else {
                    return Common::AlignUp(addr, alignment);
                }
            };
            return Interval{align_down(start), align_up(end)};
        }

        bool operator==(const Interval& other) const {
            return start == other.start && end == other.end;
        }

        bool operator<(AddressType address) const {
            return start < address;
        }
    };

private:
    struct EntryGroup {
        std::vector<Interval> intervals;
        EntryType value;

        EntryGroup(Interval interval, EntryType value_)
            : intervals(1, interval), value(std::move(value_)) {}

        template <typename T>
        EntryGroup(std::span<std::span<T>> regions, EntryType value_) : value(std::move(value_)) {
            intervals.reserve(regions.size());
            for (const auto& region : regions) {
                auto* base = region.data();
                intervals.emplace_back(base, base + region.size());
            }
        }
    };

    std::list<EntryGroup> groups;

public:
    using GroupHandle = typename std::list<EntryGroup>::iterator;

private:
    struct Entry : public Interval {
        GroupHandle group{};

        Entry(AddressType start_, AddressType end_, GroupHandle group_)
            : Interval{start_, end_}, group{group_} {}

        bool operator==(const GroupHandle& other) const {
            return group == other;
        }
    };

    /// Whether any of the supplied references already point to value owned by `group`.
    static bool IsGroupInEntries(GroupHandle group,
                                 const std::vector<std::reference_wrapper<EntryType>>& entries) {
        for (const auto& entry : entries) {
            auto* entry_ptr = reinterpret_cast<const u8*>(&entry.get());
            const auto* candidate_group =
                reinterpret_cast<const EntryGroup*>(entry_ptr - offsetof(EntryGroup, value));
            if (candidate_group == &*group) {
                return true;
            }
        }
        return false;
    }

    std::vector<Entry> entries;

public:
    IntervalMap() = default;
    IntervalMap(const IntervalMap&) = delete;
    IntervalMap(IntervalMap&&) = delete;
    IntervalMap& operator=(const IntervalMap&) = delete;
    IntervalMap& operator=(IntervalMap&&) = delete;

    template <typename T>
    GroupHandle Insert(std::span<std::span<T>> regions, EntryType value)
        requires std::is_pointer_v<AddressType>
    {
        GroupHandle group{groups.emplace(groups.begin(), regions, std::move(value))};
        for (const auto& region : regions) {
            auto* base = region.data();
            auto* limit = base + region.size();
            entries.emplace(std::lower_bound(entries.begin(), entries.end(), base), base, limit,
                            group);
        }
        return group;
    }

    void Remove(GroupHandle group) {
        for (auto it = entries.begin(); it != entries.end();) {
            if (it->group == group) {
                it = entries.erase(it);
            } else {
                ++it;
            }
        }
        groups.erase(group);
    }

    /// All entries overlapping with `interval`, plus all intervals they recursively cover with the
    /// page-aligned semantics required by trap-handler reprotection passes.
    template <size_t Alignment>
    std::pair<std::vector<std::reference_wrapper<EntryType>>, std::vector<Interval>>
    GetAlignedRecursiveRange(Interval interval) {
        std::vector<std::reference_wrapper<EntryType>> query_entries;
        std::vector<Interval> intervals;

        interval = interval.Align(Alignment);

        auto entry = std::lower_bound(entries.begin(), entries.end(), interval.end);
        const bool exclusive_entry =
            entry == entries.begin() || std::prev(entry) == entries.begin() ||
            std::prev(entry, 2)->start >= interval.end;

        while (entry != entries.begin() && (--entry)->start < interval.end) {
            if (entry->end <= interval.start || IsGroupInEntries(entry->group, query_entries)) {
                continue;
            }

            query_entries.emplace_back(entry->group->value);

            for (const auto& entry_interval : entry->group->intervals) {
                auto aligned_entry_interval = entry_interval.Align(Alignment);

                if (exclusive_entry || entry_interval == *entry) {
                    // Case (1)/(3): add all entries overlapping with the current interval, plus
                    // their exclusive intervals recursively.
                    auto recursed_entry = std::lower_bound(entries.begin(), entries.end(),
                                                           aligned_entry_interval.end);
                    while (recursed_entry != entries.begin() &&
                           (--recursed_entry)->start < aligned_entry_interval.end) {
                        if (recursed_entry->end > aligned_entry_interval.start &&
                            recursed_entry->group != entry->group &&
                            !IsGroupInEntries(recursed_entry->group, query_entries)) {
                            query_entries.emplace_back(recursed_entry->group->value);

                            for (const auto& entry_interval2 : recursed_entry->group->intervals) {
                                auto aligned_entry_interval2 = entry_interval2.Align(Alignment);
                                bool exclusive = true;

                                auto recursed2 = std::lower_bound(entries.begin(), entries.end(),
                                                                  aligned_entry_interval2.end);
                                while (recursed2 != entries.begin() &&
                                       (--recursed2)->start < aligned_entry_interval2.end) {
                                    if (recursed2->end > aligned_entry_interval2.start &&
                                        recursed2->group != recursed_entry->group &&
                                        recursed2->group != entry->group) {
                                        exclusive = false;
                                        break;
                                    }
                                }

                                if (exclusive) {
                                    intervals.emplace(
                                        std::lower_bound(intervals.begin(), intervals.end(),
                                                         aligned_entry_interval2.end),
                                        aligned_entry_interval2);
                                }
                            }
                        }
                    }

                    intervals.emplace(std::lower_bound(intervals.begin(), intervals.end(),
                                                       aligned_entry_interval.start),
                                      aligned_entry_interval);
                } else {
                    // Case (2): add this interval only if no other entry shares it.
                    bool exclusive = true;
                    auto recursed_entry = std::lower_bound(entries.begin(), entries.end(),
                                                           aligned_entry_interval.end);
                    while (recursed_entry != entries.begin() &&
                           (--recursed_entry)->start < aligned_entry_interval.end) {
                        if (recursed_entry->end > aligned_entry_interval.start &&
                            recursed_entry->group != entry->group) {
                            exclusive = false;
                            break;
                        }
                    }

                    if (exclusive) {
                        intervals.emplace(std::lower_bound(intervals.begin(), intervals.end(),
                                                           aligned_entry_interval.start),
                                          aligned_entry_interval);
                    }
                }
            }
        }

        // Coalesce adjacent intervals.
        for (auto it = intervals.begin(); it != intervals.end();) {
            auto next = std::next(it);
            if (next != intervals.end() && it->end >= next->start) {
                if (it->start > next->start) {
                    it->start = next->start;
                }
                if (it->end < next->end) {
                    it->end = next->end;
                }
                it = std::prev(intervals.erase(next));
            } else {
                ++it;
            }
        }

        return std::pair{std::move(query_entries), std::move(intervals)};
    }

    template <size_t Alignment>
    std::pair<std::vector<std::reference_wrapper<EntryType>>, std::vector<Interval>>
    GetAlignedRecursiveRange(AddressType address) {
        return GetAlignedRecursiveRange<Alignment>(Interval{address, address + 1});
    }

    /// Pointers to all entries overlapping the supplied interval.
    std::vector<std::reference_wrapper<EntryType>> GetRange(Interval interval) {
        std::vector<std::reference_wrapper<EntryType>> result;
        for (auto entry = std::lower_bound(entries.begin(), entries.end(), interval.end);
             entry != entries.begin() && (--entry)->start < interval.end;) {
            if (entry->end > interval.start && !IsGroupInEntries(entry->group, result)) {
                result.emplace_back(entry->group->value);
            }
        }
        return result;
    }
};

} // namespace Common::MemTrap
