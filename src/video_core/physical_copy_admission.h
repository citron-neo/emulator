// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "video_core/physical_access_plan.h"

namespace Tegra {

struct PhysicalCopyAdmission {
    std::shared_ptr<Common::PhysicalRangeAdmission> domain;
    Common::PhysicalRangeAdmission::Ticket ticket;
};

/// Enqueue a prepared read-source/write-destination bundle on the actual backing's domain.
/// No access is granted until TryStart succeeds. Preparation allocates: call outside cache/PTE
/// locks. The representation owner must retain the proved matching GPU payloads; this helper
/// cannot prove their contents, physical association, Vulkan completion or CPU registration.
[[nodiscard]] inline std::optional<PhysicalCopyAdmission> EnqueuePhysicalCopyAdmission(
    const PhysicalCopyPlan& plan, std::shared_ptr<const void> representations) {
    using Domain = Common::PhysicalRangeAdmission;
    if (!representations || !plan.source.Size() || !plan.destination.Size() ||
        plan.source.Segments().empty() || plan.destination.Segments().empty() ||
        plan.source.Segments().size() > Domain::MaxAccesses ||
        plan.destination.Segments().size() > Domain::MaxAccesses - plan.source.Segments().size()) {
        return std::nullopt;
    }
    const auto& backing = plan.source.Segments().front().storage.Epoch();
    if (!backing || !backing->alive.load(std::memory_order_acquire)) return std::nullopt;
    std::vector<Domain::Access> accesses;
    accesses.reserve(plan.source.Segments().size() + plan.destination.Segments().size());
    const auto append = [&](const PhysicalAccessPlan& physical, Domain::Mode mode) {
        u64 covered = 0;
        for (const auto& segment : physical.Segments()) {
            const auto& pin = segment.storage;
            if (!pin || !pin.OwnerAlive() || pin.Epoch() != backing || pin.Bytes().empty() ||
                segment.logical_offset != covered || covered > physical.Size() ||
                pin.Bytes().size() > physical.Size() - covered) return false;
            accesses.push_back({pin.PhysicalOffset(), pin.Bytes().size(), mode});
            covered += pin.Bytes().size();
        }
        return covered == physical.Size();
    };
    if (!append(plan.source, Domain::Mode::Read) || !append(plan.destination, Domain::Mode::Write)) {
        return std::nullopt;
    }
    struct Resources {
        PhysicalCopyPlan storage;
        std::shared_ptr<const void> representations;
    };
    auto owner = std::make_shared<const Resources>(Resources{plan, std::move(representations)});
    auto ticket = backing->ranges->Enqueue(accesses, std::move(owner));
    if (!ticket) return std::nullopt;
    return PhysicalCopyAdmission{backing->ranges, std::move(ticket)};
}

} // namespace Tegra
