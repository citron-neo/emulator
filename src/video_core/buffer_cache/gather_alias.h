// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/common_types.h"

namespace VideoCommon {

class UnsupportedGatherMapping final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct GatherAliasRange {
    u64 gpu_addr;
    u32 size;
    bool scratch;
    bool writable{true};
    bool operator==(const GatherAliasRange&) const = default;
};

// Inputs fit within one physical page. Different GPU positions cannot represent
// the same writable byte in a dense scratch allocation. Read-only aliases can
// safely hold identical copies. Identical GPU positions remain
// legal only within the same representation: PrepareGatherBindings gives
// overlapping storage/uniform views one shared scratch allocation, while
// image/texel views still use independent cached buffers.
class GatherAliasValidator {
public:
    explicit GatherAliasValidator(u64 page_size_) : page_size{page_size_} {}

    void Add(u64 physical, u64 gpu, u64 size, bool scratch, bool writable = true) {
        auto& page = pages[physical / page_size];
        for (const Segment& other : page) {
            const u64 begin = std::max(physical, other.physical);
            const u64 end = std::min(physical + size, other.physical + other.size);
            if ((scratch || other.scratch) && (writable || other.writable) && begin < end &&
                (scratch != other.scratch ||
                 gpu + (begin - physical) != other.gpu + (begin - other.physical))) {
                throw UnsupportedGatherMapping(
                    "Split GPU buffer has overlapping physical aliases; dense scratch cannot "
                    "preserve shader alias semantics: physical_byte=" + std::to_string(begin) +
                    " gpu_byte=" + std::to_string(gpu + begin - physical) +
                    " other_gpu_byte=" + std::to_string(other.gpu + begin - other.physical) +
                    " scratch=" + std::to_string(scratch) +
                    " other_scratch=" + std::to_string(other.scratch) +
                    " writable=" + std::to_string(writable) +
                    " other_writable=" + std::to_string(other.writable));
            }
        }
        page.push_back({physical, gpu, size, scratch, writable});
    }

private:
    struct Segment {
        u64 physical;
        u64 gpu;
        u64 size;
        bool scratch;
        bool writable;
    };
    u64 page_size;
    std::unordered_map<u64, std::vector<Segment>> pages;
};

} // namespace VideoCommon
