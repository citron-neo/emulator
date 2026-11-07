// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace Vulkan {

// One graphics-queue submission, main command buffer only. Written by its recording
// worker; inspected only after checked completion. Positions describe recorded
// command placement, never a content version or a global producer ordering.
class SyncProvenance {
public:
    using Position = std::uint32_t;
    struct Access {
        VkPipelineStageFlags stages{};
        VkAccessFlags accesses{};
    };
    Position RecordAccess() noexcept { return ++position; }
    void RecordBarrier(VkPipelineStageFlags source, VkPipelineStageFlags destination,
                       const VkMemoryBarrier& memory) noexcept {
        const auto at = ++position;
        if (count == barriers.size()) {
            intact = false;
            return;
        }
        barriers[count++] = {at, {source, memory.srcAccessMask},
                                  {destination, memory.dstAccessMask}};
    }
    bool Proves(Position before, Access source, Position after, Access destination) const noexcept {
        if (!intact || !before || before >= after || after > position ||
            !Stages(source.stages) || !Stages(destination.stages)) return false;
        constexpr VkAccessFlags writes = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        constexpr VkAccessFlags reads = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        if (!(source.accesses & writes) || !destination.accesses ||
            (source.accesses & ~(reads | writes)) || (destination.accesses & ~(reads | writes)))
            return false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& barrier = barriers[i];
            if (before < barrier.at && barrier.at < after &&
                CoversStages(barrier.source.stages, source.stages) &&
                CoversStages(barrier.destination.stages, destination.stages) &&
                CoversAccess(barrier.source.accesses, source.accesses & writes) &&
                CoversAccess(barrier.destination.accesses, destination.accesses)) return true;
        }
        return false;
    }
    // Requires an independently proved same-queue predecessor; this journal alone grants no cross-submit edge.
    bool ProvesEarlierSubmission(Access source, Position after, Access destination) const noexcept {
        if (!intact || !after || after > position || !SupportedAccess(source) || !SupportedAccess(destination) ||
            !(source.accesses & (VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT))) return false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& barrier = barriers[i];
            if (barrier.at < after && CoversStages(barrier.source.stages, source.stages) &&
                CoversStages(barrier.destination.stages, destination.stages) &&
                CoversAccess(barrier.source.accesses, source.accesses) &&
                CoversAccess(barrier.destination.accesses, destination.accesses)) return true;
        }
        return false;
    }
    bool ProvesRelease(Position before, Access source, Access destination) const noexcept {
        if (!intact || !before || before > position || !SupportedAccess(source) || !SupportedAccess(destination) ||
            !(source.accesses & (VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT))) return false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& barrier = barriers[i];
            if (before < barrier.at && CoversStages(barrier.source.stages, source.stages) &&
                CoversStages(barrier.destination.stages, destination.stages) &&
                CoversAccess(barrier.source.accesses, source.accesses) &&
                CoversAccess(barrier.destination.accesses, destination.accesses)) return true;
        }
        return false;
    }
private:
    static bool SupportedAccess(Access access) noexcept {
        constexpr VkAccessFlags known_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        return access.accesses && !(access.accesses & ~known_access) && Stages(access.stages);
    }
    static constexpr VkPipelineStageFlags graphics =
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
        VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    static constexpr VkPipelineStageFlags known =
        graphics | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    static VkPipelineStageFlags Stages(VkPipelineStageFlags stages) noexcept {
        if (stages & ~(known | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT))
            return 0;
        if (stages & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) return known;
        if (stages & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
            stages = (stages & ~VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) | graphics;
        return (stages & ~known) ? 0 : stages;
    }
    static bool CoversStages(VkPipelineStageFlags scope, VkPipelineStageFlags required) noexcept {
        const auto normalized = Stages(required);
        return normalized && (Stages(scope) & normalized) == normalized;
    }
    static bool CoversAccess(VkAccessFlags scope, VkAccessFlags required) noexcept {
        if (scope & VK_ACCESS_MEMORY_WRITE_BIT)
            scope |= VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        if (scope & VK_ACCESS_MEMORY_READ_BIT)
            scope |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        return (scope & required) == required;
    }
    struct Barrier { Position at; Access source, destination; };
    std::array<Barrier, 256> barriers{};
    std::size_t count{};
    Position position{};
    bool intact = true;
};
} // namespace Vulkan
