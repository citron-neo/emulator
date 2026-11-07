// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "video_core/physical_access_plan.h"

namespace VideoCommon {

enum class ControlledTransferPoll { Pending, Complete, Unknown };

/// Backend-owned actual destination, upload and readback allocations. No cache membership is
/// implied. Submit reads immutable physical pins only; Complete requires checked submission,
/// device visibility and an actual readback matching those same physical bytes.
class ControlledBufferTransfer {
public:
    virtual ~ControlledBufferTransfer() = default;
    [[nodiscard]] virtual const void* DestinationIdentity() const = 0;
    [[nodiscard]] virtual bool Submit(const Tegra::PhysicalAccessPlan& plan) = 0;
    [[nodiscard]] virtual ControlledTransferPoll Poll(const Tegra::PhysicalAccessPlan& plan) = 0;
    /// Separate, one-shot GPU preservation. The caller owns admission and must validate all
    /// pinned destinations before applying this checked, host-visible readback to CPU backing.
    [[nodiscard]] virtual bool SubmitPreserve() { return false; }
    [[nodiscard]] virtual ControlledTransferPoll PollPreserve() {
        return ControlledTransferPoll::Unknown;
    }
    [[nodiscard]] virtual std::span<const u8> PreservedBytes() const { return {}; }
    /// Renderer owner only, after all producers joined and checked successful device idle.
    virtual void RetireAfterDeviceIdle() = 0;
};

/// Dedicated retained staged-to-staged copy plus checked source/destination readback.
/// Owners grant neither cache membership nor physical/content authority.
class ControlledAliasTransfer {
public:
    virtual ~ControlledAliasTransfer() = default;
    virtual const void* SourceIdentity() const = 0;
    virtual const void* DestinationIdentity() const = 0;
    virtual bool Submit() = 0;
    virtual ControlledTransferPoll Poll() = 0;
    virtual void RetireAfterDeviceIdle() = 0;
};

} // namespace VideoCommon
