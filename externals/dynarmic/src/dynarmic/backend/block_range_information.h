// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <set>

#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>
#include "dynarmic/common/container/unordered_map.h"
#include "dynarmic/common/container/unordered_set.h"

#include "dynarmic/ir/location_descriptor.h"

namespace Dynarmic::Backend {

template<typename P>
class BlockRangeInformation {
public:
    void AddRange(boost::icl::discrete_interval<P> range, IR::LocationDescriptor location);
    void ClearCache();
    ::Common::unordered_set<IR::LocationDescriptor> InvalidateRanges(const boost::icl::interval_set<P>& ranges);
    boost::icl::interval_map<P, ::Common::unordered_set<IR::LocationDescriptor>> block_ranges;
};

}  // namespace Dynarmic::Backend
