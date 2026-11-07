// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <span>
#include <vector>
#include "video_core/renderer_vulkan/vk_transfer_content.h"

namespace Vulkan {

// Immutable bounded ancestry, not admission, execution, synchronization or physical authority.
// Root command IDs always refer to ParentContent(), never the new copy submission's local graph.
class ReplicaCopyProvenance {
public:
    using Index = TransferContent::Index;
    using Offset = TransferContent::Offset;
    struct RootSpan {
        Offset offset{}, size{};
        Index fill{};
        Offset root_offset{};
        bool operator==(const RootSpan&) const = default;
    };
    static constexpr Index MaxHops = 4;
    static constexpr Index MaxRootSpans = TransferContent::MaxSpans;
    struct Region {
        Index source{};
        Offset source_offset{}, destination_offset{}, size{};
        std::shared_ptr<const ReplicaCopyProvenance> source_copy;
        std::vector<RootSpan> roots;
    };
    static std::optional<ReplicaCopyProvenance> BuildBatch(std::shared_ptr<const TransferContent> parent,
        Index destination, Offset begin, Offset size, Offset extent, std::span<const Region> regions,
        std::shared_ptr<const ReplicaCopyProvenance> destination_copy, std::span<const RootSpan> complements) {
        if (!parent || regions.empty() || regions.size() > TransferContent::MaxCommands || !size ||
            begin >= extent || size > extent - begin || (begin | size) % 4 ||
            (destination_copy && (destination_copy->ParentContent() != parent ||
                destination_copy->DestinationBuffer() != destination || destination_copy->Depth() >= MaxHops))) return {};
        Offset cursor=begin;size_t root_budget{};
        Index ancestry=destination_copy ? destination_copy->Depth() : 0;
        for (const auto& region : regions) {
            if (region.source == destination || region.destination_offset < cursor || region.destination_offset > begin + size || !region.size ||
                (region.source_offset | region.destination_offset | region.size) % 4 ||
                region.size > begin + size - region.destination_offset || region.size > std::numeric_limits<Offset>::max() - region.source_offset ||
                region.roots.empty() || (root_budget += region.roots.size()) > MaxRootSpans ||
                (region.source_copy && (region.source_copy->ParentContent() != parent ||
                    region.source_copy->DestinationBuffer() != region.source || region.source_copy->Depth() >= MaxHops))) return {};
            Offset source_cursor=region.source_offset;
            for (const auto& part : region.roots) {
                if (part.offset != source_cursor || !part.size || part.size > region.source_offset + region.size - source_cursor ||
                    !(region.source_copy ? region.source_copy->HasRoot(part.offset,part.size,part.fill,part.root_offset)
                        : parent->HasRoot(region.source,part.offset,part.size,part.fill,part.root_offset))) return {};
                source_cursor += part.size;
            }
            if (source_cursor != region.source_offset + region.size) return {};
            ancestry=std::max(ancestry,region.source_copy ? region.source_copy->Depth() : 0u);
            cursor = region.destination_offset + region.size;
        }
        if (cursor != begin + size || complements.size() > MaxRootSpans) return {};
        // Merge the exact write union with independently proven old destination pieces.
        // A bounding window is not a write footprint: internal holes must be retained too.
        Offset retained{};
        size_t next_region{};
        const auto skip_writes = [&] {
            while (next_region < regions.size() && regions[next_region].destination_offset == retained) {
                retained += regions[next_region++].size;
            }
        };
        for (const auto& part : complements) {
            skip_writes();
            const auto limit = next_region < regions.size() ? regions[next_region].destination_offset : extent;
            if (part.offset != retained || !part.size || part.size > limit - retained ||
                !(destination_copy ? destination_copy->HasRoot(part.offset,part.size,part.fill,part.root_offset)
                    : parent->HasRoot(destination,part.offset,part.size,part.fill,part.root_offset))) return {};
            retained += part.size;
        }
        skip_writes();
        if (retained != extent) return {};
        ReplicaCopyProvenance plan{std::move(parent),0,destination,0,begin,size,extent,{}, {},std::move(destination_copy),complements};
        plan.regions.assign(regions.begin(),regions.end()); // Own every source partition before recording.
        plan.depth=ancestry+1;
        return plan;
    }
    bool IsBatch() const noexcept { return !regions.empty(); }
    std::span<const Region> Regions() const noexcept { return regions; }
    static std::optional<ReplicaCopyProvenance> Build(std::shared_ptr<const TransferContent> parent,
        Index source, Index destination, Offset source_offset, Offset destination_offset, Offset size,
        Offset destination_size, Index source_fill, Offset source_root, Index destination_fill, Offset destination_root,
        std::shared_ptr<const ReplicaCopyProvenance> source_copy = {}, std::span<const RootSpan> roots = {},
        std::shared_ptr<const ReplicaCopyProvenance> destination_copy = {}, std::span<const RootSpan> complements = {}) {
        if (!parent || source == destination || !size || (source_offset | destination_offset | size) % 4 ||
            size > std::numeric_limits<Offset>::max() - source_offset ||
            destination_offset >= destination_size || size > destination_size - destination_offset ||
            (source_copy && (source_copy->Depth() >= MaxHops || source_copy->ParentContent() != parent ||
                             source_copy->DestinationBuffer() != source)) ||
            (destination_copy && (destination_copy->Depth() >= MaxHops || destination_copy->ParentContent() != parent ||
                                  destination_copy->DestinationBuffer() != destination))) return {};
        const RootSpan single{source_offset, size, source_fill, source_root};
        if (roots.empty()) roots = {&single, 1};
        if (roots.size() > MaxRootSpans) return {};
        Offset cursor = source_offset;
        for (const auto& part : roots) {
            if (part.offset != cursor || !part.size || part.size > source_offset + size - cursor ||
                !(source_copy ? source_copy->HasRoot(part.offset, part.size, part.fill, part.root_offset)
                              : parent->HasRoot(source, part.offset, part.size, part.fill, part.root_offset))) return {};
            cursor += part.size;
        }
        if (cursor != source_offset + size) return {};
        std::array<RootSpan, 2> legacy_complements{};
        Index legacy_count{};
        if (!destination_copy && complements.empty()) {
            if (!parent->HasRoot(destination, 0, destination_size, destination_fill, destination_root)) return {};
            if (destination_offset) legacy_complements[legacy_count++] = {0, destination_offset, destination_fill, destination_root};
            if (destination_offset + size < destination_size) legacy_complements[legacy_count++] = {
                destination_offset + size, destination_size - destination_offset - size,
                destination_fill, destination_root + destination_offset + size};
            complements = {legacy_complements.data(), legacy_count};
        }
        if (complements.size() > MaxRootSpans) return {};
        Offset preserved{};
        for (const auto& part : complements) {
            if (preserved == destination_offset) preserved += size;
            if (part.offset != preserved || !part.size || part.size > destination_size - preserved ||
                (part.offset < destination_offset + size && destination_offset < part.offset + part.size) ||
                !(destination_copy ? destination_copy->HasRoot(part.offset, part.size, part.fill, part.root_offset)
                                   : parent->HasRoot(destination, part.offset, part.size, part.fill, part.root_offset))) return {};
            preserved += part.size;
        }
        if (preserved == destination_offset) preserved += size;
        if (preserved != destination_size) return {};
        return ReplicaCopyProvenance{std::move(parent), source, destination, source_offset,
            destination_offset, size, destination_size, std::move(source_copy), roots, std::move(destination_copy), complements};
    }
    bool HasRoot(Offset offset, Offset size, Index fill, Offset root_offset) const noexcept {
        if (!size || offset >= destination_size || size > destination_size - offset ||
            size > std::numeric_limits<Offset>::max() - root_offset) return false;
        if (IsBatch()) {
            Offset covered{};
            for (const auto& region : regions) {
                const auto begin=std::max(offset,region.destination_offset);
                const auto end=std::min(offset+size,region.destination_offset+region.size);
                if (begin>=end) continue;
                const auto projection=region.source_offset+begin-region.destination_offset;
                if (!(region.source_copy ? region.source_copy->HasRoot(projection,end-begin,fill,root_offset+begin-offset)
                    : parent->HasRoot(region.source,projection,end-begin,fill,root_offset+begin-offset))) return false;
                covered+=end-begin;
            }
            for (const auto& part : DestinationRoots()) {
                const auto begin=std::max(offset,part.offset),end=std::min(offset+size,part.offset+part.size);
                if (begin>=end) continue;
                if (!(destination_copy ? destination_copy->HasRoot(begin,end-begin,fill,root_offset+begin-offset)
                    : parent->HasRoot(destination,begin,end-begin,fill,root_offset+begin-offset))) return false;
                covered+=end-begin;
            }
            return covered==size;
        }
        Offset covered{};
        const auto check = [&](Offset begin, Offset end, bool copied) {
            begin = std::max(begin, offset);
            end = std::min(end, offset + size);
            if (begin >= end) return true;
            const auto parent_offset = copied ? source_offset + begin - destination_offset : begin;
            if (!(copied && source_copy
                    ? source_copy->HasRoot(parent_offset, end - begin, fill, root_offset + begin - offset)
                    : !copied && destination_copy
                    ? destination_copy->HasRoot(parent_offset, end - begin, fill, root_offset + begin - offset)
                    : parent->HasRoot(copied ? source : destination, parent_offset, end - begin,
                                      fill, root_offset + begin - offset))) return false;
            covered += end - begin;
            return true;
        };
        return check(0, destination_offset, false) &&
            check(destination_offset, destination_offset + length, true) &&
            check(destination_offset + length, destination_size, false) && covered == size;
    }
    const std::shared_ptr<const TransferContent>& ParentContent() const noexcept { return parent; }
    const std::shared_ptr<const ReplicaCopyProvenance>& SourceCopy() const noexcept { return source_copy; }
    const std::shared_ptr<const ReplicaCopyProvenance>& DestinationCopy() const noexcept { return destination_copy; }
    std::span<const RootSpan> SourceRoots() const noexcept { return {source_roots.data(), root_count}; }
    std::span<const RootSpan> DestinationRoots() const noexcept { return {destination_roots.data(), complement_count}; }
    Index Depth() const noexcept { return depth; }
    Index SourceBuffer() const noexcept { return source; }
    Index DestinationBuffer() const noexcept { return destination; }
    bool MatchesCopy(Offset source_begin, Offset destination_begin, Offset size) const noexcept {
        return source_begin == source_offset && destination_begin == destination_offset && size == length;
    }
private:
    ReplicaCopyProvenance(std::shared_ptr<const TransferContent> parent_, Index source_, Index destination_,
        Offset source_offset_, Offset destination_offset_, Offset length_, Offset destination_size_,
        std::shared_ptr<const ReplicaCopyProvenance> source_copy_, std::span<const RootSpan> roots_,
        std::shared_ptr<const ReplicaCopyProvenance> destination_copy_, std::span<const RootSpan> complements_)
        : parent{std::move(parent_)}, source{source_}, destination{destination_}, source_offset{source_offset_},
          destination_offset{destination_offset_}, length{length_}, destination_size{destination_size_},
          source_copy{std::move(source_copy_)}, destination_copy{std::move(destination_copy_)},
          depth{std::max(source_copy ? source_copy->Depth() : 0u, destination_copy ? destination_copy->Depth() : 0u) + 1},
          root_count{static_cast<Index>(roots_.size())}, complement_count{static_cast<Index>(complements_.size())} {
        std::copy(roots_.begin(), roots_.end(), source_roots.begin());
        std::copy(complements_.begin(), complements_.end(), destination_roots.begin());
    }
    std::shared_ptr<const TransferContent> parent;
    Index source, destination;
    Offset source_offset, destination_offset, length, destination_size;
    std::shared_ptr<const ReplicaCopyProvenance> source_copy;
    std::shared_ptr<const ReplicaCopyProvenance> destination_copy;
    Index depth;
    std::array<RootSpan, MaxRootSpans> source_roots{};
    Index root_count{};
    std::array<RootSpan, MaxRootSpans> destination_roots{};
    Index complement_count{};
    std::vector<Region> regions;
};
} // namespace Vulkan
