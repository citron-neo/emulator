// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/block_range_information.h"

#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>
#include "dynarmic/common/common_types.h"
#include "dynarmic/common/container/unordered_set.h"

namespace Dynarmic::Backend {

template<typename P>
void BlockRangeInformation<P>::AddRange(boost::icl::discrete_interval<P> range, IR::LocationDescriptor location) {
    block_ranges.add(std::make_pair(range, ::Common::unordered_set<IR::LocationDescriptor>{location}));
}

template<typename P>
void BlockRangeInformation<P>::ClearCache() {
    block_ranges.clear();
}

template<typename P>
::Common::unordered_set<IR::LocationDescriptor> BlockRangeInformation<P>::InvalidateRanges(const boost::icl::interval_set<P>& ranges) {
    ::Common::unordered_set<IR::LocationDescriptor> erase_locations;
    for (auto invalidate_interval : ranges) {
        auto pair = block_ranges.equal_range(invalidate_interval);
        for (auto it = pair.first; it != pair.second; ++it)
            for (const auto& descriptor : it->second)
                erase_locations.insert(descriptor);
    }
    // TODO: EFFICIENCY: Remove ranges that are to be erased.
    return erase_locations;
}

template class BlockRangeInformation<u32>;
template class BlockRangeInformation<u64>;

}  // namespace Dynarmic::Backend
