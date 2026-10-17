// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <limits>
#include <memory>
#include "video_core/renderer_vulkan/vk_transfer_content.h"

namespace Vulkan {

// Immutable bounded ancestry, not admission, execution, synchronization or physical authority.
// Root command IDs always refer to ParentContent(), never the new copy submission's local graph.
class ReplicaCopyProvenance {
public:
    using Index = TransferContent::Index;
    using Offset = TransferContent::Offset;
    static constexpr Index MaxHops = 4;
    static std::optional<ReplicaCopyProvenance> Build(std::shared_ptr<const TransferContent> parent,
        Index source, Index destination, Offset source_offset, Offset destination_offset, Offset size,
        Offset destination_size, Index source_fill, Offset source_root, Index destination_fill, Offset destination_root,
        std::shared_ptr<const ReplicaCopyProvenance> source_copy = {}) {
        if (!parent || source == destination || !size || (source_offset | destination_offset | size) % 4 ||
            destination_offset >= destination_size || size > destination_size - destination_offset ||
            (source_copy && (source_copy->Depth() >= MaxHops || source_copy->ParentContent() != parent ||
                             source_copy->DestinationBuffer() != source)) ||
            !(source_copy ? source_copy->HasRoot(source_offset, size, source_fill, source_root)
                          : parent->HasRoot(source, source_offset, size, source_fill, source_root)) ||
            !parent->HasRoot(destination, 0, destination_size, destination_fill, destination_root)) return {};
        return ReplicaCopyProvenance{std::move(parent), source, destination, source_offset,
            destination_offset, size, destination_size, std::move(source_copy)};
    }
    bool HasRoot(Offset offset, Offset size, Index fill, Offset root_offset) const noexcept {
        if (!size || offset >= destination_size || size > destination_size - offset ||
            size > std::numeric_limits<Offset>::max() - root_offset) return false;
        Offset covered{};
        const auto check = [&](Offset begin, Offset end, bool copied) {
            begin = std::max(begin, offset);
            end = std::min(end, offset + size);
            if (begin >= end) return true;
            const auto parent_offset = copied ? source_offset + begin - destination_offset : begin;
            if (!(copied && source_copy
                    ? source_copy->HasRoot(parent_offset, end - begin, fill, root_offset + begin - offset)
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
    Index Depth() const noexcept { return depth; }
    Index SourceBuffer() const noexcept { return source; }
    Index DestinationBuffer() const noexcept { return destination; }
    bool MatchesCopy(Offset source_begin, Offset destination_begin, Offset size) const noexcept {
        return source_begin == source_offset && destination_begin == destination_offset && size == length;
    }
private:
    ReplicaCopyProvenance(std::shared_ptr<const TransferContent> parent_, Index source_, Index destination_,
        Offset source_offset_, Offset destination_offset_, Offset length_, Offset destination_size_,
        std::shared_ptr<const ReplicaCopyProvenance> source_copy_)
        : parent{std::move(parent_)}, source{source_}, destination{destination_}, source_offset{source_offset_},
          destination_offset{destination_offset_}, length{length_}, destination_size{destination_size_},
          source_copy{std::move(source_copy_)}, depth{source_copy ? source_copy->Depth() + 1 : 1} {}
    std::shared_ptr<const TransferContent> parent;
    Index source, destination;
    Offset source_offset, destination_offset, length, destination_size;
    std::shared_ptr<const ReplicaCopyProvenance> source_copy;
    Index depth;
};
} // namespace Vulkan
