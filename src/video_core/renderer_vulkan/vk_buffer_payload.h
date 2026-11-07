// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Buffer;
class StagedBufferPin;

/// One-way cancellation boundary, not ownership of VkDevice/VMA or a completion fence.
/// Sealing rejects new pins and disables handle lookup without dropping existing payloads.
class StagedBufferPinDomain {
public:
    void Seal() noexcept { sealed.store(true, std::memory_order_release); }
    [[nodiscard]] bool IsSealed() const noexcept {
        return sealed.load(std::memory_order_acquire);
    }

private:
    std::atomic_bool sealed{false};
};

/// Owns the actual VMA buffer/allocation used by staged DMA copies. This does not own the
/// renderer's VkDevice/VMA allocator: callers must release all pins before those domains retire.
class StagedBufferPayload {
public:
    StagedBufferPayload(vk::Buffer resource, u64 size_bytes,
                        std::shared_ptr<const StagedBufferPinDomain> pin_domain)
        : buffer{std::move(resource)}, size{size_bytes}, domain{std::move(pin_domain)} {}

private:
    friend class Buffer;
    friend class StagedBufferPin;
    vk::Buffer buffer;
    const u64 size;
    const std::shared_ptr<const StagedBufferPinDomain> domain;
    std::atomic_bool content_eligible{true}; // Membership cancellation, not native lifetime.
    std::atomic_bool host_pointer_exposed{}; // Escaped writable mappings cannot be excluded by Scheduler recording.
};

/// Immutable byte interval and real staged-buffer ownership. Not cache membership, physical
/// footprint proof, content preservation, admission or device completion. GPU/cache-thread only
/// during acquisition; no cache lock is retained. Direct/imported representations are excluded.
class StagedBufferPin {
public:
    [[nodiscard]] VkBuffer Handle() const noexcept {
        return IsUsable() ? *payload->buffer : VK_NULL_HANDLE;
    }
    /// False after domain seal. This does not release resources or acknowledge device completion.
    [[nodiscard]] bool IsUsable() const noexcept {
        return payload && payload->domain && !payload->domain->IsSealed() && payload->buffer;
    }
    [[nodiscard]] u64 Offset() const noexcept { return offset; }
    [[nodiscard]] bool ContentEligible() const noexcept {
        return IsUsable() && payload->content_eligible.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool CopyProvenanceEligible() const noexcept {
        return ContentEligible() && !payload->host_pointer_exposed.load(std::memory_order_acquire);
    }
    [[nodiscard]] u64 Size() const noexcept { return size; }
    [[nodiscard]] const void* Identity() const noexcept { return payload.get(); }
    // Ownership only; no allocation, mapping, content or completion qualification.
    [[nodiscard]] std::shared_ptr<const void> RetainedOwner() const noexcept { return payload; }

    [[nodiscard]] static std::optional<StagedBufferPin> Build(
        const std::shared_ptr<StagedBufferPayload>& owner, u64 offset_bytes, u64 size_bytes) {
        if (!owner || !owner->domain || owner->domain->IsSealed() || !owner->buffer ||
            !size_bytes || offset_bytes >= owner->size ||
            size_bytes > owner->size - offset_bytes) {
            return std::nullopt;
        }
        return StagedBufferPin{owner, offset_bytes, size_bytes};
    }

private:
    StagedBufferPin(std::shared_ptr<const StagedBufferPayload> owner, u64 offset_, u64 size_)
        : payload{std::move(owner)}, offset{offset_}, size{size_} {}
    std::shared_ptr<const StagedBufferPayload> payload;
    u64 offset, size;
};

} // namespace Vulkan
