// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <limits>
#include <utility>

#include <boost/icl/interval.hpp>
#include <boost/icl/interval_base_set.hpp>
#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>
#include <boost/icl/split_interval_map.hpp>
#include <boost/pool/pool.hpp>
#include <boost/pool/pool_alloc.hpp>
#include <boost/pool/poolfwd.hpp>

#include "common/range_sets.h"

namespace Common {

namespace {
template <class T>
using RangeSetsAllocator =
    boost::fast_pool_allocator<T, boost::default_user_allocator_new_delete,
                               boost::details::pool::default_mutex, 1024, 2048>;
}

template <typename AddressType>
struct RangeSet<AddressType>::RangeSetImpl {
    using IntervalSet = boost::icl::interval_set<
        AddressType, std::less, ICL_INTERVAL_INSTANCE(ICL_INTERVAL_DEFAULT, AddressType, std::less),
        RangeSetsAllocator>;
    using IntervalType = typename IntervalSet::interval_type;

    RangeSetImpl() = default;
    ~RangeSetImpl() = default;

    void Add(AddressType base_address, size_t size) {
        AddressType end_address = base_address + static_cast<AddressType>(size);
        IntervalType interval{base_address, end_address};
        m_ranges_set.add(interval);
    }

    void Subtract(AddressType base_address, size_t size) {
        AddressType end_address = base_address + static_cast<AddressType>(size);
        IntervalType interval{base_address, end_address};
        m_ranges_set.subtract(interval);
    }

    template <typename Func>
    void ForEach(Func&& func) const {
        if (m_ranges_set.empty()) {
            return;
        }
        auto it = m_ranges_set.begin();
        auto end_it = m_ranges_set.end();
        for (; it != end_it; it++) {
            const AddressType inter_addr_end = it->upper();
            const AddressType inter_addr = it->lower();
            func(inter_addr, inter_addr_end);
        }
    }

    template <typename Func>
    void ForEachInRange(AddressType base_addr, size_t size, Func&& func) const {
        if (m_ranges_set.empty()) {
            return;
        }
        const AddressType start_address = base_addr;
        const AddressType end_address = start_address + size;
        const RangeSetImpl::IntervalType search_interval{start_address, end_address};
        auto it = m_ranges_set.lower_bound(search_interval);
        if (it == m_ranges_set.end()) {
            return;
        }
        auto end_it = m_ranges_set.upper_bound(search_interval);
        for (; it != end_it; it++) {
            AddressType inter_addr_end = it->upper();
            AddressType inter_addr = it->lower();
            if (inter_addr_end > end_address) {
                inter_addr_end = end_address;
            }
            if (inter_addr < start_address) {
                inter_addr = start_address;
            }
            func(inter_addr, inter_addr_end);
        }
    }

    IntervalSet m_ranges_set;
};

template <typename AddressType>
struct OverlapRangeSet<AddressType>::OverlapRangeSetImpl {
    using IntervalSet = boost::icl::split_interval_map<
        AddressType, s32, boost::icl::partial_enricher, std::less, boost::icl::inplace_plus,
        boost::icl::inter_section,
        ICL_INTERVAL_INSTANCE(ICL_INTERVAL_DEFAULT, AddressType, std::less), RangeSetsAllocator>;
    using IntervalType = typename IntervalSet::interval_type;

    OverlapRangeSetImpl() = default;
    ~OverlapRangeSetImpl() = default;

    void Add(AddressType base_address, size_t size) {
        AddressType end_address = base_address + static_cast<AddressType>(size);
        IntervalType interval{base_address, end_address};
        m_split_ranges_set += std::make_pair(interval, 1);
    }

    template <bool has_on_delete, typename Func>
    void Subtract(AddressType base_address, size_t size, s32 amount,
                  [[maybe_unused]] Func&& on_delete) {
        if (m_split_ranges_set.empty()) {
            return;
        }
        AddressType end_address = base_address + static_cast<AddressType>(size);
        IntervalType interval{base_address, end_address};
        bool any_removals = false;
        m_split_ranges_set += std::make_pair(interval, -amount);
        do {
            any_removals = false;
            auto it = m_split_ranges_set.lower_bound(interval);
            if (it == m_split_ranges_set.end()) {
                return;
            }
            auto end_it = m_split_ranges_set.upper_bound(interval);
            for (; it != end_it; it++) {
                if (it->second <= 0) {
                    if constexpr (has_on_delete) {
                        if (it->second == 0) {
                            on_delete(it->first.lower(), it->first.upper());
                        }
                    }
                    any_removals = true;
                    m_split_ranges_set.erase(it);
                    break;
                }
            }
        } while (any_removals);
    }

    template <typename Func>
    void ForEach(Func&& func) const {
        if (m_split_ranges_set.empty()) {
            return;
        }
        auto it = m_split_ranges_set.begin();
        auto end_it = m_split_ranges_set.end();
        for (; it != end_it; it++) {
            const AddressType inter_addr_end = it->first.upper();
            const AddressType inter_addr = it->first.lower();
            func(inter_addr, inter_addr_end, it->second);
        }
    }

    template <typename Func>
    void ForEachInRange(AddressType base_address, size_t size, Func&& func) const {
        if (m_split_ranges_set.empty()) {
            return;
        }
        const AddressType start_address = base_address;
        const AddressType end_address = start_address + size;
        const OverlapRangeSetImpl::IntervalType search_interval{start_address, end_address};
        auto it = m_split_ranges_set.lower_bound(search_interval);
        if (it == m_split_ranges_set.end()) {
            return;
        }
        auto end_it = m_split_ranges_set.upper_bound(search_interval);
        for (; it != end_it; it++) {
            auto& inter = it->first;
            AddressType inter_addr_end = inter.upper();
            AddressType inter_addr = inter.lower();
            if (inter_addr_end > end_address) {
                inter_addr_end = end_address;
            }
            if (inter_addr < start_address) {
                inter_addr = start_address;
            }
            func(inter_addr, inter_addr_end, it->second);
        }
    }

    IntervalSet m_split_ranges_set;
};

template <typename AddressType>
RangeSet<AddressType>::RangeSet() {
    m_impl = std::make_unique<RangeSet<AddressType>::RangeSetImpl>();
}

template <typename AddressType>
RangeSet<AddressType>::~RangeSet() = default;

template <typename AddressType>
RangeSet<AddressType>::RangeSet(RangeSet&& other) {
    m_impl = std::make_unique<RangeSet<AddressType>::RangeSetImpl>();
    m_impl->m_ranges_set = std::move(other.m_impl->m_ranges_set);
}

template <typename AddressType>
RangeSet<AddressType>& RangeSet<AddressType>::operator=(RangeSet&& other) {
    m_impl->m_ranges_set = std::move(other.m_impl->m_ranges_set);
}

template <typename AddressType>
void RangeSet<AddressType>::Add(AddressType base_address, size_t size) {
    m_impl->Add(base_address, size);
}

template <typename AddressType>
void RangeSet<AddressType>::Subtract(AddressType base_address, size_t size) {
    m_impl->Subtract(base_address, size);
}

template <typename AddressType>
void RangeSet<AddressType>::Clear() {
    m_impl->m_ranges_set.clear();
}

template <typename AddressType>
bool RangeSet<AddressType>::Empty() const {
    return m_impl->m_ranges_set.empty();
}

template <typename AddressType>
template <typename Func>
void RangeSet<AddressType>::ForEach(Func&& func) const {
    m_impl->ForEach(std::move(func));
}

template <typename AddressType>
template <typename Func>
void RangeSet<AddressType>::ForEachInRange(AddressType base_address, size_t size,
                                           Func&& func) const {
    m_impl->ForEachInRange(base_address, size, std::move(func));
}

template <typename AddressType>
OverlapRangeSet<AddressType>::OverlapRangeSet() {
    m_impl = std::make_unique<OverlapRangeSet<AddressType>::OverlapRangeSetImpl>();
}

template <typename AddressType>
OverlapRangeSet<AddressType>::~OverlapRangeSet() = default;

template <typename AddressType>
OverlapRangeSet<AddressType>::OverlapRangeSet(OverlapRangeSet&& other) {
    m_impl = std::make_unique<OverlapRangeSet<AddressType>::OverlapRangeSetImpl>();
    m_impl->m_split_ranges_set = std::move(other.m_impl->m_split_ranges_set);
}

template <typename AddressType>
OverlapRangeSet<AddressType>& OverlapRangeSet<AddressType>::operator=(OverlapRangeSet&& other) {
    m_impl->m_split_ranges_set = std::move(other.m_impl->m_split_ranges_set);
}

template <typename AddressType>
void OverlapRangeSet<AddressType>::Add(AddressType base_address, size_t size) {
    m_impl->Add(base_address, size);
}

template <typename AddressType>
void OverlapRangeSet<AddressType>::Subtract(AddressType base_address, size_t size) {
    m_impl->template Subtract<false>(base_address, size, 1, [](AddressType, AddressType) {});
}

template <typename AddressType>
template <typename Func>
void OverlapRangeSet<AddressType>::Subtract(AddressType base_address, size_t size,
                                            Func&& on_delete) {
    m_impl->template Subtract<true, Func>(base_address, size, 1, std::move(on_delete));
}

template <typename AddressType>
void OverlapRangeSet<AddressType>::DeleteAll(AddressType base_address, size_t size) {
    m_impl->template Subtract<false>(base_address, size, std::numeric_limits<s32>::max(),
                                     [](AddressType, AddressType) {});
}

template <typename AddressType>
void OverlapRangeSet<AddressType>::Clear() {
    m_impl->m_split_ranges_set.clear();
}

template <typename AddressType>
bool OverlapRangeSet<AddressType>::Empty() const {
    return m_impl->m_split_ranges_set.empty();
}

template <typename AddressType>
template <typename Func>
void OverlapRangeSet<AddressType>::ForEach(Func&& func) const {
    m_impl->ForEach(func);
}

template <typename AddressType>
template <typename Func>
void OverlapRangeSet<AddressType>::ForEachInRange(AddressType base_address, size_t size,
                                                  Func&& func) const {
    m_impl->ForEachInRange(base_address, size, std::move(func));
}

} // namespace Common
