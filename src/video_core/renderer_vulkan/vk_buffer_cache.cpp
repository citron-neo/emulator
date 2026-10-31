// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <cerrno>
#include <span>
#include <stdexcept>
#include <vector>
#ifdef __linux__
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif
#include "common/settings.h"

#include "common/alignment.h"
#include "common/common_types.h"
#include "common/literals.h"
#include "common/logging.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"


#include "video_core/renderer_vulkan/maxwell_to_vk.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

using namespace Common::Literals;

namespace Vulkan {
namespace {
#ifdef __linux__
bool SyncGuestDmaBuf(int fd, u64 flags) {
    dma_buf_sync sync{.flags = flags};
    int result;
    do {
        result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
    } while (result != 0 && (errno == EINTR || errno == EAGAIN));
    return result == 0;
}
#endif

VkBufferCopy MakeBufferCopy(const VideoCommon::BufferCopy& copy) {
    return VkBufferCopy{
        .srcOffset = copy.src_offset,
        .dstOffset = copy.dst_offset,
        .size = copy.size,
    };
}

VkIndexType IndexTypeFromNumElements(const Device& device, u32 num_elements) {
    if (num_elements <= 0xff && device.IsExtIndexTypeUint8Supported()) {
        return VK_INDEX_TYPE_UINT8_EXT;
    }
    if (num_elements <= 0xffff) {
        return VK_INDEX_TYPE_UINT16;
    }
    return VK_INDEX_TYPE_UINT32;
}

size_t BytesPerIndex(VkIndexType index_type) {
    switch (index_type) {
    case VK_INDEX_TYPE_UINT8_EXT:
        return 1;
    case VK_INDEX_TYPE_UINT16:
        return 2;
    case VK_INDEX_TYPE_UINT32:
        return 4;
    default:
        assert(false && "Invalid index type={}" && (void(index_type), true));
        return 1;
    }
}

VkBufferUsageFlags BufferUsage(const Device& device) {
    VkBufferUsageFlags flags =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (device.IsExtTransformFeedbackSupported()) {
        flags |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
    }
    if (device.IsExtConditionalRendering()) {
        flags |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
    }

    return flags;
}

vk::Buffer CreateBuffer(const Device& device, const MemoryAllocator& memory_allocator, u64 size) {
    const VkBufferUsageFlags flags = BufferUsage(device);
    const VkBufferCreateInfo buffer_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = flags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    // On unified memory the cache writes guest data straight into the buffer when the GPU is
    // done with it (BufferCache::MappedUploadMemory); otherwise uploads go through staging.
    if (vk::Buffer unified = memory_allocator.CreateUnifiedBuffer(buffer_ci)) {
        return unified;
    }
    return memory_allocator.CreateBuffer(buffer_ci, MemoryUsage::DeviceLocal);
}

} // Anonymous namespace

namespace {
class AliasTransfer final : public VideoCommon::ControlledAliasTransfer {
public:
    AliasTransfer(BufferCacheRuntime& runtime_, Scheduler& scheduler_, StagedBufferPin source_,
                  StagedBufferPin destination_, std::span<const VideoCommon::AliasCopyRange> copies_,
                  vk::Buffer readback_, u64 bytes_)
        : runtime{runtime_}, scheduler{scheduler_}, source{std::move(source_)},
          destination{std::move(destination_)}, copies{copies_.begin(), copies_.end()},
          readback{std::move(readback_)}, bytes{bytes_} {}
    const void* SourceIdentity() const override { return source ? source->Identity() : nullptr; }
    const void* DestinationIdentity() const override {
        return destination ? destination->Identity() : nullptr;
    }
    bool Submit() override {
        if (submitted || retired || !source || !destination ||
            !source->IsUsable() || !destination->IsUsable()) return false;
        submitted = true; // Recording failures cannot authorize replay.
        try {
            std::vector<VideoCommon::BufferCopy> transfer, observe_source, observe_destination;
            u64 packed{};
            for (const auto& copy : copies) {
                transfer.push_back({source->Offset() + copy.source_offset,
                                    destination->Offset() + copy.destination_offset, copy.size});
                observe_source.push_back({source->Offset() + copy.source_offset, packed, copy.size});
                observe_destination.push_back({destination->Offset() + copy.destination_offset,
                                               bytes + packed, copy.size});
                packed += copy.size;
            }
            runtime.CopyBuffer(destination->Handle(), source->Handle(), transfer, true, false);
            runtime.CopyBuffer(*readback, source->Handle(), observe_source, true, false);
            runtime.CopyBuffer(*readback, destination->Handle(), observe_destination, true, false);
            scheduler.Record([](vk::CommandBuffer command) {
                const VkMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .pNext = nullptr, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
                command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                                        0, barrier);
            });
            submission = scheduler.FlushChecked();
            return bool(submission);
        } catch (...) { return false; }
    }
    VideoCommon::ControlledTransferPoll Poll() override {
        using Result = VideoCommon::ControlledTransferPoll;
        if (retired || !submitted || !source || !destination || !source->IsUsable() ||
            !destination->IsUsable() || !submission) return Result::Unknown;
        if (verified) return Result::Complete;
        const auto status = scheduler.PollChecked(submission);
        if (status == VK_NOT_READY) return Result::Pending;
        if (status != VK_SUCCESS || readback.InvalidateChecked() != VK_SUCCESS ||
            std::memcmp(readback.Mapped().data(), readback.Mapped().data() + bytes, bytes) != 0)
            return Result::Unknown;
        verified = true;
        return Result::Complete;
    }
    void RetireAfterDeviceIdle() override {
        retired = true;
        source.reset();
        destination.reset();
        readback.reset();
        submission.reset();
    }
private:
    BufferCacheRuntime& runtime;
    Scheduler& scheduler;
    std::optional<StagedBufferPin> source, destination;
    const std::vector<VideoCommon::AliasCopyRange> copies;
    vk::Buffer readback;
    const u64 bytes;
    Scheduler::Submission submission;
    bool submitted{}, verified{}, retired{};
};

class ControlledTransfer final : public VideoCommon::ControlledBufferTransfer {
public:
    ControlledTransfer(BufferCacheRuntime& runtime_, Scheduler& scheduler_, StagedBufferPin target_,
                       vk::Buffer upload_, vk::Buffer readback_)
        : runtime{runtime_}, scheduler{scheduler_}, target{std::move(target_)},
          upload{std::move(upload_)}, readback{std::move(readback_)} {}

    const void* DestinationIdentity() const override {
        return target ? target->Identity() : nullptr;
    }

    bool Submit(const Tegra::PhysicalAccessPlan& plan) override {
        if (retired || submitted || !target || !target->IsUsable() ||
            plan.Size() != target->Size() || upload.Mapped().size() < plan.Size() ||
            readback.Mapped().size() < plan.Size())
            return false;
        u64 covered = 0;
        for (const auto& segment : plan.Segments()) {
            const auto bytes = segment.storage.Bytes();
            if (!segment.storage.OwnerAlive() || segment.logical_offset != covered ||
                covered > plan.Size() || bytes.size() > plan.Size() - covered)
                return false;
            std::memcpy(upload.Mapped().data() + covered, bytes.data(), bytes.size());
            covered += bytes.size();
        }
        if (covered != plan.Size() || upload.FlushChecked() != VK_SUCCESS) return false;
        // All buffers are dedicated retained allocations, never shared stream/GC slots.
        // Once recording starts any failure is uncertain; this object cannot replay effects.
        submitted = true;
        try {
            const std::array copy{VideoCommon::BufferCopy{0, target->Offset(), plan.Size()}};
            runtime.CopyBuffer(target->Handle(), *upload, copy, true, false);
            const std::array download{VideoCommon::BufferCopy{target->Offset(), 0, plan.Size()}};
            runtime.CopyBuffer(*readback, target->Handle(), download, true, false);
            scheduler.Record([](vk::CommandBuffer cmdbuf) {
                static constexpr VkMemoryBarrier HOST_READ{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                };
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                                       0, HOST_READ);
            });
            submission = scheduler.FlushChecked();
            return bool(submission);
        } catch (...) {
            return false;
        }
    }

    VideoCommon::ControlledTransferPoll Poll(const Tegra::PhysicalAccessPlan& plan) override {
        using Result = VideoCommon::ControlledTransferPoll;
        if (retired || !target || !target->IsUsable() || !submitted || !submission)
            return Result::Unknown;
        if (verified) return Result::Complete;
        const auto result = scheduler.PollChecked(submission);
        if (result == VK_NOT_READY) return Result::Pending;
        if (result != VK_SUCCESS || readback.InvalidateChecked() != VK_SUCCESS)
            return Result::Unknown;
        for (const auto& segment : plan.Segments()) {
            const auto bytes = segment.storage.Bytes();
            if (segment.logical_offset > readback.Mapped().size() ||
                bytes.size() > readback.Mapped().size() - segment.logical_offset ||
                std::memcmp(readback.Mapped().data() + segment.logical_offset, bytes.data(),
                            bytes.size()) != 0)
                return Result::Unknown;
        }
        verified = true;
        return Result::Complete;
    }

    bool SubmitPreserve() override {
        if (retired || submitted || !target || !target->IsUsable() ||
            readback.Mapped().size() < target->Size())
            return false;
        submitted = preserving = true;
        try {
            const std::array copies{VideoCommon::BufferCopy{target->Offset(), 0, target->Size()}};
            runtime.CopyBuffer(*readback, target->Handle(), copies, true, false);
            scheduler.Record([](vk::CommandBuffer cmdbuf) {
                static constexpr VkMemoryBarrier PRESERVE_HOST_READ{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                };
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                                       0, PRESERVE_HOST_READ);
            });
            submission = scheduler.FlushChecked();
            return bool(submission);
        } catch (...) {
            return false;
        }
    }

    VideoCommon::ControlledTransferPoll PollPreserve() override {
        using Result = VideoCommon::ControlledTransferPoll;
        if (retired || !preserving || !target || !target->IsUsable() || !submission)
            return Result::Unknown;
        if (verified) return Result::Complete;
        const auto result = scheduler.PollChecked(submission);
        if (result == VK_NOT_READY) return Result::Pending;
        if (result != VK_SUCCESS || readback.InvalidateChecked() != VK_SUCCESS)
            return Result::Unknown;
        verified = true;
        return Result::Complete;
    }

    std::span<const u8> PreservedBytes() const override {
        return preserving && verified && !retired ? readback.Mapped() : std::span<const u8>{};
    }

    void RetireAfterDeviceIdle() override {
        retired = true;
        target.reset();
        upload.reset();
        readback.reset();
        submission.reset();
    }

private:
    BufferCacheRuntime& runtime;
    Scheduler& scheduler;
    std::optional<StagedBufferPin> target;
    vk::Buffer upload, readback;
    Scheduler::Submission submission;
    bool submitted{}, verified{}, retired{}, preserving{};
};
} // namespace

bool BufferCacheRuntime::SupportsControlledTransfers() const {
    return device.HasTimelineSemaphore() && !staged_pin_domain->IsSealed();
}

std::shared_ptr<Scheduler::PrivateProducerDomain> BufferCacheRuntime::CreatePrivateProducerDomain(u32 replicas,u64 size) {
    if(replicas<2 || replicas>3 || !size || size%4 || !SupportsControlledTransfers() ||
       device.HasDebuggingToolAttached()) return {};
    try {
        auto state=PrivateProducerGuard::Create(size);
        if(!state) return {};
        auto domain=std::shared_ptr<Scheduler::PrivateProducerDomain>(new Scheduler::PrivateProducerDomain(scheduler,state));
        for(u32 slot=0;slot<replicas;++slot) {
            // Never register in BufferCache, publish host mappings, export a
            // guest address or return a native handle to the caller.
            Buffer buffer{*this,slot*PrivateProducerGuard::State::Page,size};
            auto pin=buffer.PinStagedRepresentation(0,size);
            if(!pin || !pin->CopyProvenanceEligible()) return {};
            auto physical=VideoCommon::PhysicalWriteIdentity::Capture(*state,slot*PrivateProducerGuard::State::Page,size,pin->Identity());
            if(!physical) return {};
            domain->replicas.push_back({*pin,std::make_shared<const VideoCommon::PhysicalWriteIdentity>(*physical)});
            state->identities[slot]=pin->Identity();
        }
        state->count=replicas;
        if(!domain->Initialize()) return {};
        return domain;
    } catch(...) {return {};}
}

std::shared_ptr<VideoCommon::ControlledBufferTransfer> BufferCacheRuntime::
    PrepareControlledTransfer(const Buffer& buffer) {
    std::erase_if(controlled_transfers, [](const auto& weak) { return weak.expired(); });
    if (!SupportsControlledTransfers() || controlled_transfers.size() >= 256) return {};
    auto target = buffer.PinStagedRepresentation(0, buffer.SizeBytes());
    if (!target) return {};
    VkBufferCreateInfo ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                          .pNext = nullptr,
                          .flags = 0,
                          .size = buffer.SizeBytes(),
                          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                          .queueFamilyIndexCount = 0,
                          .pQueueFamilyIndices = nullptr};
    auto upload = memory_allocator.CreateBuffer(ci, MemoryUsage::Upload);
    ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    auto readback = memory_allocator.CreateBuffer(ci, MemoryUsage::Download);
    auto transfer = std::make_shared<ControlledTransfer>(*this, scheduler, std::move(*target),
                                                         std::move(upload), std::move(readback));
    controlled_transfers.push_back(transfer);
    return transfer;
}

void BufferCacheRuntime::RetireControlledTransfersAfterDeviceIdle() {
    for (const auto& weak : controlled_transfers)
        if (const auto transfer = weak.lock()) transfer->RetireAfterDeviceIdle();
    controlled_transfers.clear();
    for (const auto& weak : alias_transfers)
        if (const auto transfer = weak.lock()) transfer->RetireAfterDeviceIdle();
    alias_transfers.clear();
}

std::shared_ptr<VideoCommon::ControlledAliasTransfer> BufferCacheRuntime::PrepareAliasTransfer(
    StagedBufferPin source, StagedBufferPin destination,
    std::span<const VideoCommon::AliasCopyRange> copies) {
    std::erase_if(alias_transfers, [](const auto& weak) { return weak.expired(); });
    if (!SupportsControlledTransfers() || alias_transfers.size() >= 256 || copies.empty() ||
        copies.size() > 64 || !source.IsUsable() || !destination.IsUsable() ||
        source.Identity() == destination.Identity()) return {};
    u64 bytes{};
    for (const auto& copy : copies) {
        if (!copy.size || copy.source_offset >= source.Size() ||
            copy.size > source.Size() - copy.source_offset ||
            copy.destination_offset >= destination.Size() ||
            copy.size > destination.Size() - copy.destination_offset ||
            ((source.Offset() + copy.source_offset) |
             (destination.Offset() + copy.destination_offset) | copy.size) % 4 ||
            copy.size > 256 * 1024 - bytes) return {};
        bytes += copy.size;
    }
    const VkBufferCreateInfo ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr, .flags = 0, .size = 2 * bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0, .pQueueFamilyIndices = nullptr};
    auto readback = memory_allocator.CreateBuffer(ci, MemoryUsage::Download);
    if (!readback || readback.Mapped().size() < 2 * bytes) return {};
    auto transfer = std::make_shared<AliasTransfer>(*this, scheduler, std::move(source),
        std::move(destination), copies, std::move(readback), bytes);
    alias_transfers.push_back(transfer);
    return transfer;
}

bool BufferCacheRuntime::TryDirectGuestBuffer(Buffer& target, Common::GuestBackingRange backing) {
#ifdef __linux__
    if (!Settings::values.use_uma_zero_copy.GetValue() || !device.IsIntegrated() ||
        !device.CanImportGuestDmaBuf() || backing.fd < 0 || target.IsDirectGuest()) {
        return false;
    }
    if (!VideoCommon::ZeroCopyEligibility::SupportsWholeAllocationCpuSync(backing.allocation_size)) {
        if (!reported_large_guest_backing) {
            LOG_INFO(Render_Vulkan,
                     "UMA zero-copy fallback: dma-buf CPU synchronization covers {} bytes; "
                     "whole-allocation limit is {} bytes",
                     backing.allocation_size,
                     VideoCommon::ZeroCopyEligibility::MaxWholeAllocationCpuSyncSize);
            reported_large_guest_backing = true;
        }
        return false;
    }
    constexpr auto handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    const auto usage = BufferUsage(device);
    const auto external = device.GetPhysical().GetExternalBufferProperties(usage, handle_type);
    const auto features = external.externalMemoryProperties.externalMemoryFeatures;
    // Dedicated exports cannot be rebound at arbitrary offsets in a guest chunk.
    if (!(features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
        (features & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) ||
        !(external.externalMemoryProperties.compatibleHandleTypes & handle_type)) {
        return false;
    }
    const auto& logical = device.GetLogical();
    const auto& vk = logical.Dispatch();
    if (!vk.vkGetMemoryFdPropertiesKHR) {
        return false;
    }
    VkMemoryFdPropertiesKHR fd_properties{.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    if (vk.vkGetMemoryFdPropertiesKHR(*logical, handle_type, backing.fd, &fd_properties) != VK_SUCCESS) {
        return false;
    }
    auto mapping = std::make_unique<DirectGuestMapping>();
    const VkExternalMemoryBufferCreateInfo external_ci{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO, .handleTypes = handle_type};
    const VkBufferCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &external_ci,
        .size = target.SizeBytes(), .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer raw_buffer{};
    if (vk.vkCreateBuffer(*logical, &ci, nullptr, &raw_buffer) != VK_SUCCESS) {
        return false;
    }
    mapping->buffer = vk::Handle<VkBuffer, VkDevice, vk::DeviceDispatch>{raw_buffer, *logical, vk};
    VkMemoryDedicatedRequirements dedicated{.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    const auto requirements = logical.GetBufferMemoryRequirements(raw_buffer, &dedicated);
    if (dedicated.requiresDedicatedAllocation || requirements.size > backing.allocation_size ||
        backing.offset > backing.allocation_size - requirements.size) {
        return false;
    }
    const auto properties = device.GetPhysical().GetMemoryProperties().memoryProperties;
    constexpr auto required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    u32 type = properties.memoryTypeCount;
    for (u32 i = 0; i < properties.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & fd_properties.memoryTypeBits & (1U << i)) &&
            (properties.memoryTypes[i].propertyFlags & required) == required) {
            type = i;
            break;
        }
    }
    if (type == properties.memoryTypeCount) {
        return false; // non-coherent mmap aliases have no cache-ownership protocol yet
    }
    const VideoCommon::ZeroCopyEligibility eligibility{
        true, true, true, true, true, true, true, true, requirements.alignment, backing.allocation_size};
    const VideoCommon::GuestMappingIdentity identity{&target, 0, 0};
    const std::array segments{VideoCommon::DirectGuestSegment{backing.offset, 0, target.SizeBytes()}};
    if (eligibility.Select(identity, identity, segments, target.SizeBytes()) !=
        VideoCommon::GuestMappingMode::Direct) {
        return false;
    }
    const int imported_fd = dup(backing.fd);
    if (imported_fd < 0) {
        return false;
    }
    const VkImportMemoryFdInfoKHR import{
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .handleType = handle_type, .fd = imported_fd};
    mapping->memory = logical.TryAllocateMemory({
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &import,
        .allocationSize = backing.allocation_size, .memoryTypeIndex = type});
    if (!mapping->memory) {
        close(imported_fd); // ownership transfers only on successful import
        return false;
    }
    if (vk.vkBindBufferMemory(*logical, raw_buffer, *mapping->memory, backing.offset) != VK_SUCCESS) {
        return false;
    }
    // VkMemory HOST_COHERENT describes Vulkan mappings. The original guest mmap
    // aliases additionally obey the dma-buf CPU-access protocol.
    if (!guest_cpu_access.contains(backing.fd)) {
        GuestMemoryBarrier(true);
        scheduler.Finish();
        if (!SyncGuestDmaBuf(backing.fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW)) {
            return false;
        }
    }
    guest_cpu_access.insert(backing.fd);
    mapping->backing = backing;
    mapping->properties = properties.memoryTypes[type].propertyFlags;
    target.views.clear();
    target.payload.reset();
    target.direct = std::move(mapping);
    return true;
#else
    (void)target;
    (void)backing;
    return false;
#endif
}

bool BufferCacheRuntime::NeedsGuestCpuAccess(const Buffer& buffer) const {
#ifdef __linux__
    const auto* backing = buffer.DirectBacking();
    return backing && !guest_cpu_access.contains(backing->fd);
#else
    (void)buffer;
    return false;
#endif
}

void BufferCacheRuntime::BeginGuestCpuAccess(const Buffer& buffer) {
#ifdef __linux__
    const auto* backing = buffer.DirectBacking();
    if (!backing || guest_cpu_access.contains(backing->fd)) { return; }
    if (!SyncGuestDmaBuf(backing->fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW)) {
        // Once submitted shared writes exist, silently downgrading would lose data.
        throw vk::Exception(VK_ERROR_INVALID_EXTERNAL_HANDLE);
    }
    guest_cpu_access.insert(backing->fd);
#else
    (void)buffer;
#endif
}

void BufferCacheRuntime::EndGuestCpuAccess(const Buffer& buffer) {
#ifdef __linux__
    const auto* backing = buffer.DirectBacking();
    if (!backing || !guest_cpu_access.contains(backing->fd)) { return; }
    if (!SyncGuestDmaBuf(backing->fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW)) {
        throw vk::Exception(VK_ERROR_INVALID_EXTERNAL_HANDLE);
    }
    guest_cpu_access.erase(backing->fd);
#else
    (void)buffer;
#endif
}

BufferCacheRuntime::~BufferCacheRuntime() {
    SealStagedPins();
#ifdef __linux__
    for (const int fd : guest_cpu_access) {
        if (!SyncGuestDmaBuf(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW)) {
            LOG_WARNING(Render_Vulkan, "Could not end guest dma-buf CPU access at shutdown");
        }
    }
#endif
}

void BufferCacheRuntime::GuestMemoryBarrier(bool to_cpu) {
    const VkMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VkAccessFlags(to_cpu ? VK_ACCESS_MEMORY_WRITE_BIT
                                            : VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT),
        .dstAccessMask = VkAccessFlags(to_cpu ? VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT
                                            : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT)};
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([barrier, to_cpu](vk::CommandBuffer cmdbuf) {
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                               to_cpu ? VK_PIPELINE_STAGE_HOST_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               0, barrier);
    });
}

void BufferCacheRuntime::CleanupUnusedBuffers() {
    // This function is kept for compatibility but no longer performs mode-specific cleanup
}

Buffer::Buffer(BufferCacheRuntime& runtime, VideoCommon::NullBufferParams null_params)
    : VideoCommon::BufferBase(null_params), tracker{4096} {
    if (runtime.device.HasNullDescriptor()) {
        return;
    }
    device = &runtime.device;
    payload = std::make_shared<StagedBufferPayload>(runtime.CreateNullBuffer(), SizeBytes(),
                                                   runtime.staged_pin_domain);
    is_null = true;
}

Buffer::Buffer(BufferCacheRuntime& runtime, DAddr cpu_addr_, u64 size_bytes_)
    : VideoCommon::BufferBase(cpu_addr_, size_bytes_), device{&runtime.device},
      payload{std::make_shared<StagedBufferPayload>(
          CreateBuffer(*device, runtime.memory_allocator, SizeBytes()), SizeBytes(),
          runtime.staged_pin_domain)},
      tracker{SizeBytes()} {
    runtime.CapturePhysicalOrigin(*this);
    if (runtime.device.HasDebuggingToolAttached()) {
        payload->buffer.SetObjectNameEXT(fmt::format("Buffer 0x{:x}", CpuAddr()).c_str());
    }
}

VkBufferView Buffer::View(u32 offset, u32 size, VideoCore::Surface::PixelFormat format) {
    if (!device) {
        // Null buffer supported, return a null descriptor
        return VK_NULL_HANDLE;
    } else if (is_null) {
        // Null buffer not supported, adjust offset and size
        offset = 0;
        size = 0;
    }
    const auto it{std::ranges::find_if(views, [offset, size, format](const BufferView& view) {
        return offset == view.offset && size == view.size && format == view.format;
    })};
    if (it != views.end()) {
        return *it->handle;
    }
    views.push_back({
        .offset = offset,
        .size = size,
        .format = format,
        .handle = device->GetLogical().CreateBufferView({
            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .buffer = Handle(),
            .format = MaxwellToVK::SurfaceFormat(*device, FormatType::Buffer, false, format).format,
            .offset = offset,
            .range = size,
        }),
    });
    return *views.back().handle;
}

class QuadIndexBuffer {
public:
    QuadIndexBuffer(const Device& device_, MemoryAllocator& memory_allocator_,
                    Scheduler& scheduler_, StagingBufferPool& staging_pool_)
        : device{device_}, memory_allocator{memory_allocator_}, scheduler{scheduler_},
          staging_pool{staging_pool_} {}

    virtual ~QuadIndexBuffer() = default;

    void UpdateBuffer(u32 num_indices_) {
        if (num_indices_ <= num_indices) {
            return;
        }

        scheduler.Finish();

        num_indices = num_indices_;
        index_type = IndexTypeFromNumElements(device, num_indices);

        const u32 num_quads = GetQuadsNum(num_indices);
        const u32 num_triangle_indices = num_quads * 6;
        const u32 num_first_offset_copies = 4;
        const size_t bytes_per_index = BytesPerIndex(index_type);
        const size_t size_bytes = num_triangle_indices * bytes_per_index * num_first_offset_copies;
        const VkBufferCreateInfo buffer_ci = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = size_bytes,
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };
        buffer = memory_allocator.CreateBuffer(buffer_ci, MemoryUsage::DeviceLocal);
        if (device.HasDebuggingToolAttached()) {
            buffer.SetObjectNameEXT("Quad LUT");
        }

        const bool host_visible = buffer.IsHostVisible();
        const StagingBufferRef staging = [&] {
            if (host_visible) {
                return StagingBufferRef{};
            }
            return staging_pool.Request(size_bytes, MemoryUsage::Upload);
        }();

        u8* staging_data = host_visible ? buffer.Mapped().data() : staging.mapped_span.data();
        const size_t quad_size = bytes_per_index * 6;

        for (u32 first = 0; first < num_first_offset_copies; ++first) {
            for (u32 quad = 0; quad < num_quads; ++quad) {
                MakeAndUpdateIndices(staging_data, quad_size, quad, first);
                staging_data += quad_size;
            }
        }

        if (!host_visible) {
            scheduler.RequestOutsideRenderPassOperationContext();
            scheduler.Record([src_buffer = staging.buffer, src_offset = staging.offset,
                              dst_buffer = *buffer, size_bytes](vk::CommandBuffer cmdbuf) {
                const VkBufferCopy copy{
                    .srcOffset = src_offset,
                    .dstOffset = 0,
                    .size = size_bytes,
                };
                const VkBufferMemoryBarrier write_barrier{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_INDEX_READ_BIT,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = dst_buffer,
                    .offset = 0,
                    .size = size_bytes,
                };
                cmdbuf.CopyBuffer(src_buffer, dst_buffer, copy);
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, write_barrier);
            });
        } else {
            buffer.Flush();
        }
    }

    void BindBuffer(u32 first) {
        const VkIndexType index_type_ = index_type;
        const size_t sub_first_offset = static_cast<size_t>(first % 4) * GetQuadsNum(num_indices);
        const size_t offset =
            (sub_first_offset + GetQuadsNum(first)) * 6ULL * BytesPerIndex(index_type);
        scheduler.Record([buffer_ = *buffer, index_type_, offset](vk::CommandBuffer cmdbuf) {
            cmdbuf.BindIndexBuffer(buffer_, offset, index_type_);
        });
    }

protected:
    virtual u32 GetQuadsNum(u32 num_indices) const = 0;

    virtual void MakeAndUpdateIndices(u8* staging_data, size_t quad_size, u32 quad, u32 first) = 0;

    const Device& device;
    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;
    StagingBufferPool& staging_pool;

    vk::Buffer buffer{};
    MemoryCommit memory_commit{};
    VkIndexType index_type{};
    u32 num_indices = 0;
};

class QuadArrayIndexBuffer : public QuadIndexBuffer {
public:
    QuadArrayIndexBuffer(const Device& device_, MemoryAllocator& memory_allocator_,
                         Scheduler& scheduler_, StagingBufferPool& staging_pool_)
        : QuadIndexBuffer(device_, memory_allocator_, scheduler_, staging_pool_) {}

    ~QuadArrayIndexBuffer() = default;

private:
    u32 GetQuadsNum(u32 num_indices_) const override {
        return num_indices_ / 4;
    }

    template <typename T>
    static std::array<T, 6> MakeIndices(u32 quad, u32 first) {
        std::array<T, 6> indices{0, 1, 2, 0, 2, 3};
        for (T& index : indices) {
            index = static_cast<T>(first + index + quad * 4);
        }
        return indices;
    }

    void MakeAndUpdateIndices(u8* staging_data, size_t quad_size, u32 quad, u32 first) override {
        switch (index_type) {
        case VK_INDEX_TYPE_UINT8_EXT:
            std::memcpy(staging_data, MakeIndices<u8>(quad, first).data(), quad_size);
            break;
        case VK_INDEX_TYPE_UINT16:
            std::memcpy(staging_data, MakeIndices<u16>(quad, first).data(), quad_size);
            break;
        case VK_INDEX_TYPE_UINT32:
            std::memcpy(staging_data, MakeIndices<u32>(quad, first).data(), quad_size);
            break;
        default:
            assert(false);
            break;
        }
    }
};

class QuadStripIndexBuffer : public QuadIndexBuffer {
public:
    QuadStripIndexBuffer(const Device& device_, MemoryAllocator& memory_allocator_,
                         Scheduler& scheduler_, StagingBufferPool& staging_pool_)
        : QuadIndexBuffer(device_, memory_allocator_, scheduler_, staging_pool_) {}

    ~QuadStripIndexBuffer() = default;

private:
    u32 GetQuadsNum(u32 num_indices_) const override {
        return num_indices_ >= 4 ? (num_indices_ - 2) / 2 : 0;
    }

    template <typename T>
    static std::array<T, 6> MakeIndices(u32 quad, u32 first) {
        std::array<T, 6> indices{0, 3, 1, 0, 2, 3};
        for (T& index : indices) {
            index = static_cast<T>(first + index + quad * 2);
        }
        return indices;
    }

    void MakeAndUpdateIndices(u8* staging_data, size_t quad_size, u32 quad, u32 first) override {
        switch (index_type) {
        case VK_INDEX_TYPE_UINT8_EXT:
            std::memcpy(staging_data, MakeIndices<u8>(quad, first).data(), quad_size);
            break;
        case VK_INDEX_TYPE_UINT16:
            std::memcpy(staging_data, MakeIndices<u16>(quad, first).data(), quad_size);
            break;
        case VK_INDEX_TYPE_UINT32:
            std::memcpy(staging_data, MakeIndices<u32>(quad, first).data(), quad_size);
            break;
        default:
            assert(false);
            break;
        }
    }
};

BufferCacheRuntime::BufferCacheRuntime(const Device& device_, MemoryAllocator& memory_allocator_,
                                       Scheduler& scheduler_, StagingBufferPool& staging_pool_,
                                       GuestDescriptorQueue& guest_descriptor_queue_,
                                       ComputePassDescriptorQueue& compute_pass_descriptor_queue,
                                       DescriptorPool& descriptor_pool)
    : device{device_}, memory_allocator{memory_allocator_}, scheduler{scheduler_},
      staging_pool{staging_pool_}, guest_descriptor_queue{guest_descriptor_queue_},
      quad_index_pass(device, scheduler, descriptor_pool, staging_pool,
                      compute_pass_descriptor_queue) {
    if (!device.HasRobustBufferAccess2()) {
        LOG_WARNING(Render_Vulkan, "Device lacks robustBufferAccess2, constant buffers will be "
                                   "zero padded on every bind");
    } else if (device.GetRobustUniformBufferAccessSizeAlignment() > 1) {
        LOG_WARNING(Render_Vulkan,
                    "Constant buffers whose size is not a multiple of {} bytes will be zero padded",
                    device.GetRobustUniformBufferAccessSizeAlignment());
    }
    // Built on every driver: vulkan_uint8.comp needs no 8-bit or 16-bit storage feature, and a
    // device without VK_EXT_index_type_uint8 has to go through it.
    uint8_pass = std::make_unique<Uint8Pass>(device, scheduler, descriptor_pool, staging_pool,
                                             compute_pass_descriptor_queue);
    quad_array_index_buffer = std::make_shared<QuadArrayIndexBuffer>(device_, memory_allocator_,
                                                                     scheduler_, staging_pool_);
    quad_strip_index_buffer = std::make_shared<QuadStripIndexBuffer>(device_, memory_allocator_,
                                                                     scheduler_, staging_pool_);
}

StagingBufferRef BufferCacheRuntime::UploadStagingBuffer(size_t size) {
    return staging_pool.Request(size, MemoryUsage::Upload);
}

std::span<u16> BufferCacheRuntime::BindMappedUint16IndexBuffer(u32 num_indices) {
    const StagingBufferRef ref =
        staging_pool.Request(num_indices * sizeof(u16), MemoryUsage::Upload);
    scheduler.Record([buffer = ref.buffer, offset = ref.offset](vk::CommandBuffer cmdbuf) {
        cmdbuf.BindIndexBuffer(buffer, offset, VK_INDEX_TYPE_UINT16);
    });
    return {reinterpret_cast<u16*>(ref.mapped_span.data()), num_indices};
}

StagingBufferRef BufferCacheRuntime::DownloadStagingBuffer(size_t size, bool deferred) {
    return staging_pool.Request(size, MemoryUsage::Download, deferred);
}

void BufferCacheRuntime::FreeDeferredStagingBuffer(StagingBufferRef& ref) {
    staging_pool.FreeDeferred(ref);
}

u64 BufferCacheRuntime::GetDeviceLocalMemory() const {
    return device.GetDeviceLocalMemory();
}

u64 BufferCacheRuntime::GetDeviceMemoryUsage() const {
    return device.GetDeviceMemoryUsage();
}

GCDownloadRef BufferCacheRuntime::TryGCDownloadStagingBuffer(size_t size) {
    return staging_pool.RequestGCDownload(size);
}

bool BufferCacheRuntime::CanReportMemoryUsage() const {
    return device.CanReportMemoryUsage();
}

u32 BufferCacheRuntime::GetStorageBufferAlignment() const {
    return static_cast<u32>(device.GetStorageBufferAlignment());
}

u64 BufferCacheRuntime::CurrentTick() const noexcept {
    return scheduler.CurrentTick();
}

u64 BufferCacheRuntime::KnownGpuTick() const noexcept {
    return scheduler.GetMasterSemaphore().KnownGpuTick();
}

void BufferCacheRuntime::TickFrame([[maybe_unused]] Common::SlotVector<Buffer>& slot_buffers) noexcept {
    // Usage tracking belongs to a submission (Buffer::MarkUsage) and starts over on its own.
    // Clearing it here, between submissions, forgot draws already recorded in the open command
    // buffer, and an upload could then be moved ahead of them.
}

void BufferCacheRuntime::Finish() {
    scheduler.Finish();
}

bool BufferCacheRuntime::CanReorderUpload(const Buffer& buffer,
                                          std::span<const VideoCommon::BufferCopy> copies) {
    if (Settings::values.disable_buffer_reorder) {
        return false;
    }
    const bool can_use_upload_cmdbuf =
        std::ranges::all_of(copies, [&](const VideoCommon::BufferCopy& copy) {
            return !buffer.IsRegionUsed(copy.dst_offset, copy.size, scheduler.CurrentTick());
        });
    return can_use_upload_cmdbuf;
}

bool BufferCacheRuntime::IsRenderPassActive() const noexcept {
    return scheduler.IsRenderPassActive();
}

void BufferCacheRuntime::CopyBuffer(VkBuffer dst_buffer, VkBuffer src_buffer,
                                    std::span<const VideoCommon::BufferCopy> copies, bool barrier,
                                    bool can_reorder_upload) {
    if (dst_buffer == VK_NULL_HANDLE || src_buffer == VK_NULL_HANDLE) return;
    boost::container::small_vector<VkBufferCopy, 8> vk_copies(copies.size());
    std::ranges::transform(copies, vk_copies.begin(), MakeBufferCopy);
    scheduler.RecordBufferCopy(dst_buffer, src_buffer, {vk_copies.data(), vk_copies.size()}, barrier,
        src_buffer == staging_pool.StreamBuf() && can_reorder_upload);
}

void BufferCacheRuntime::PreCopyBarrier() {
    static constexpr VkMemoryBarrier READ_BARRIER{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.RecordProvenanceBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      READ_BARRIER);
}

void BufferCacheRuntime::PostCopyBarrier() {
    static constexpr VkMemoryBarrier WRITE_BARRIER{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
    };
    scheduler.RequestOutsideRenderPassOperationContext();
    // Batched readback also needs device writes visible to the CPU after completion.
    scheduler.RecordProvenanceBarrier(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, WRITE_BARRIER);
}

namespace {
auto MakeBufferClear(VkBuffer dest_buffer, u32 offset, size_t size, u32 value) {
    static constexpr VkMemoryBarrier READ_BARRIER{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    static constexpr VkMemoryBarrier WRITE_BARRIER{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
    };

    return [dest_buffer, offset, size, value](vk::CommandBuffer cmdbuf) {
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0, READ_BARRIER);
        cmdbuf.FillBuffer(dest_buffer, offset, size, value);
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               0, WRITE_BARRIER);
        return true;
    };
}
} // namespace

void BufferCacheRuntime::ClearBuffer(VkBuffer dest_buffer, u32 offset, size_t size, u32 value) {
    scheduler.RecordBufferFill(dest_buffer, offset, size, value);
}

Scheduler::Write BufferCacheRuntime::ClearBufferChecked(StagedBufferPin pin, u64 intent_revision, u32 value,
                                                       std::function<bool()> emit) {
    if (!pin.IsUsable() || !pin.Size() || pin.Offset() > std::numeric_limits<u32>::max() ||
        ((pin.Offset() | pin.Size()) % 4) != 0) return {};
    auto owner = std::make_shared<StagedBufferPin>(std::move(pin));
    scheduler.RequestOutsideRenderPassSyncedOperationContext();
    return scheduler.RecordCheckedWrite(owner, owner->Identity(), owner->Offset(), owner->Size(), intent_revision,
        [clear = MakeBufferClear(owner->Handle(), static_cast<u32>(owner->Offset()), owner->Size(), value),
         owner, should_emit = std::move(emit)](vk::CommandBuffer cmdbuf) {
            if (!owner->IsUsable()) throw std::runtime_error("checked writer pin sealed before emission");
            if (should_emit && !should_emit()) return false;
            return clear(cmdbuf);
        });
}

void BufferCacheRuntime::BeginStorageWriteObservation(bool enabled) {
    storage_write_footprints.clear();
    storage_observation_active = enabled;
    storage_observation_valid = true;
    if (enabled) {
        try { storage_write_footprints.reserve(64); }
        catch (...) { storage_observation_valid = false; }
    }
}

void BufferCacheRuntime::CapturePhysicalOrigin(Buffer& buffer) const noexcept {
    if (buffer.physical_origin_attempted) return;
    buffer.physical_origin_attempted = true; // Never rebase an existing/merged allocation.
    if (!observation_memory || !buffer.CpuAddr() || buffer.IsDirectGuest()) return;
    try {
        const auto pin = buffer.PinStagedRepresentation(0, buffer.SizeBytes());
        if (!pin) return;
        auto origin = VideoCommon::PhysicalWriteIdentity::Capture(
            *observation_memory, buffer.CpuAddr(), buffer.SizeBytes(), pin->Identity());
        if (origin) buffer.physical_origin =
            std::make_shared<const VideoCommon::PhysicalWriteIdentity>(std::move(*origin));
    } catch (...) {} // Diagnostics cannot reject native allocation/rendering.
}

std::shared_ptr<const VideoCommon::PhysicalWriteIdentity>
BufferCacheRuntime::PhysicalIdentityForBinding(const Buffer& buffer, u64 offset, u64 size) const noexcept {
    if (!observation_memory || !buffer.physical_origin) return {};
    try {
        const auto pin = buffer.PinStagedRepresentation(offset, size);
        if (!pin || buffer.physical_origin->Representation() != pin->Identity()) return {};
        auto range = buffer.physical_origin->Slice(offset, size);
        if (!range || !range->MatchesCurrent(*observation_memory)) return {};
        return std::make_shared<const VideoCommon::PhysicalWriteIdentity>(std::move(*range));
    } catch (...) { return {}; }
}

void BufferCacheRuntime::EndStorageWriteObservation() {
    storage_observation_active = false;
    storage_write_footprints.clear();
}

void BufferCacheRuntime::BindStorageBuffer(Buffer& buffer, u32 offset, u32 size, bool is_written) {
    // Capture the actual descriptor resource here, before a later binding can
    // merge/retire its cache representation. No cold mapping or allocation here.
    if (storage_observation_active && storage_observation_valid && is_written && size) {
        const auto pin = buffer.PinStagedRepresentation(offset, size);
        const auto revision = buffer.gpu_write_revision.Snapshot();
        const auto physical = PhysicalIdentityForBinding(buffer, offset, size);
        if (!buffer.CpuAddr() || !pin || !revision || !physical || storage_write_footprints.size() == 64) {
            storage_observation_valid = false; // Scratch/direct/overflow is not observed coverage.
        } else {
            storage_write_footprints.push_back({pin->RetainedOwner(), pin->Identity(),
                pin->Offset(), pin->Size(), *revision, physical,
                {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                 VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}});
        }
    }
    BindBuffer(buffer.Handle(), offset, size);
}

void BufferCacheRuntime::BindIndexBuffer(PrimitiveTopology topology, IndexFormat index_format,
                                         u32 base_vertex, u32 num_indices, VkBuffer buffer,
                                         u32 offset, [[maybe_unused]] u32 size) {
    VkIndexType vk_index_type = MaxwellToVK::IndexFormat(index_format);
    VkDeviceSize vk_offset = offset;
    VkBuffer vk_buffer = buffer;
    if (topology == PrimitiveTopology::Quads || topology == PrimitiveTopology::QuadStrip) {
        vk_index_type = VK_INDEX_TYPE_UINT32;
        std::tie(vk_buffer, vk_offset) =
            quad_index_pass.Assemble(index_format, num_indices, base_vertex, buffer, offset,
                                     topology == PrimitiveTopology::QuadStrip);
    } else if (vk_index_type == VK_INDEX_TYPE_UINT8_EXT && !device.IsExtIndexTypeUint8Supported()) {
        vk_index_type = VK_INDEX_TYPE_UINT16;
        std::tie(vk_buffer, vk_offset) = uint8_pass->Assemble(num_indices, buffer, offset);
    }
    if (vk_buffer == VK_NULL_HANDLE) {
        // Vulkan doesn't support null index buffers. Replace it with our own null buffer.
        ReserveNullBuffer();
        vk_buffer = *null_buffer;
    }
    scheduler.Record([vk_buffer, vk_offset, vk_index_type](vk::CommandBuffer cmdbuf) {
        cmdbuf.BindIndexBuffer(vk_buffer, vk_offset, vk_index_type);
    });
}

void BufferCacheRuntime::BindQuadIndexBuffer(PrimitiveTopology topology, u32 first, u32 count) {
    if (count == 0) {
        ReserveNullBuffer();
        scheduler.Record([this](vk::CommandBuffer cmdbuf) {
            cmdbuf.BindIndexBuffer(*null_buffer, 0, VK_INDEX_TYPE_UINT32);
        });
        return;
    }

    if (topology == PrimitiveTopology::Quads) {
        quad_array_index_buffer->UpdateBuffer(first + count);
        quad_array_index_buffer->BindBuffer(first);
    } else if (topology == PrimitiveTopology::QuadStrip) {
        quad_strip_index_buffer->UpdateBuffer(first + count);
        quad_strip_index_buffer->BindBuffer(first);
    }
}

void BufferCacheRuntime::BindVertexBuffer(u32 index, VkBuffer buffer, u32 offset, u32 size,
                                          u32 stride) {
    if (index >= device.GetMaxVertexInputBindings()) {
        return;
    }
    if (device.IsExtExtendedDynamicStateSupported()) {
        scheduler.Record([index, buffer, offset, size, stride](vk::CommandBuffer cmdbuf) {
            const VkDeviceSize vk_offset = buffer != VK_NULL_HANDLE ? offset : 0;
            const VkDeviceSize vk_size = buffer != VK_NULL_HANDLE ? size : VK_WHOLE_SIZE;
            const VkDeviceSize vk_stride = stride;
            cmdbuf.BindVertexBuffers2EXT(index, 1, &buffer, &vk_offset, &vk_size, &vk_stride);
        });
    } else {
        if (!device.HasNullDescriptor() && buffer == VK_NULL_HANDLE) {
            ReserveNullBuffer();
            buffer = *null_buffer;
            offset = 0;
        }
        scheduler.Record([index, buffer, offset](vk::CommandBuffer cmdbuf) {
            cmdbuf.BindVertexBuffer(index, buffer, offset);
        });
    }
}

void BufferCacheRuntime::BindVertexBuffers(VideoCommon::HostBindings<Buffer>& bindings) {
    boost::container::small_vector<VkBuffer, 32> buffer_handles;
    for (u32 index = 0; index < bindings.buffers.size(); ++index) {
        auto handle = bindings.buffers[index]->Handle();
        if (handle == VK_NULL_HANDLE) {
            bindings.offsets[index] = 0;
            bindings.sizes[index] = VK_WHOLE_SIZE;
            if (!device.HasNullDescriptor()) {
                ReserveNullBuffer();
                handle = *null_buffer;
            }
        }
        buffer_handles.push_back(handle);
    }
    const u32 device_max = device.GetMaxVertexInputBindings();
    const u32 min_binding = std::min(bindings.min_index, device_max);
    const u32 max_binding = std::min(bindings.max_index, device_max);
    const u32 binding_count = max_binding - min_binding;
    if (binding_count == 0) {
        return;
    }
    if (device.IsExtExtendedDynamicStateSupported()) {
        scheduler.Record([bindings_ = std::move(bindings),
                          buffer_handles_ = std::move(buffer_handles),
                          binding_count](vk::CommandBuffer cmdbuf) {
            cmdbuf.BindVertexBuffers2EXT(bindings_.min_index, binding_count, buffer_handles_.data(),
                                         bindings_.offsets.data(), bindings_.sizes.data(),
                                         bindings_.strides.data());
        });
    } else {
        scheduler.Record([bindings_ = std::move(bindings),
                          buffer_handles_ = std::move(buffer_handles),
                          binding_count](vk::CommandBuffer cmdbuf) {
            cmdbuf.BindVertexBuffers(bindings_.min_index, binding_count, buffer_handles_.data(),
                                     bindings_.offsets.data());
        });
    }
}

void BufferCacheRuntime::BindTransformFeedbackBuffer(u32 index, VkBuffer buffer, u32 offset,
                                                     u32 size) {
    if (!device.IsExtTransformFeedbackSupported()) {
        // Already logged in the rasterizer
        return;
    }
    if (buffer == VK_NULL_HANDLE) {
        // Vulkan doesn't support null transform feedback buffers.
        // Replace it with our own null buffer.
        ReserveNullBuffer();
        buffer = *null_buffer;
        offset = 0;
        size = 0;
    }
    scheduler.Record([index, buffer, offset, size](vk::CommandBuffer cmdbuf) {
        const VkDeviceSize vk_offset = offset;
        const VkDeviceSize vk_size = size;
        cmdbuf.BindTransformFeedbackBuffersEXT(index, 1, &buffer, &vk_offset, &vk_size);
    });
}

void BufferCacheRuntime::BindTransformFeedbackBuffers(VideoCommon::HostBindings<Buffer>& bindings) {
    if (!device.IsExtTransformFeedbackSupported()) {
        // Already logged in the rasterizer
        return;
    }
    boost::container::small_vector<VkBuffer, 4> buffer_handles;
    for (u32 index = 0; index < bindings.buffers.size(); ++index) {
        buffer_handles.push_back(bindings.buffers[index]->Handle());
    }
    scheduler.Record([bindings_ = std::move(bindings),
                      buffer_handles_ = std::move(buffer_handles)](vk::CommandBuffer cmdbuf) {
        cmdbuf.BindTransformFeedbackBuffersEXT(0, static_cast<u32>(buffer_handles_.size()),
                                               buffer_handles_.data(), bindings_.offsets.data(),
                                               bindings_.sizes.data());
    });
}

void BufferCacheRuntime::ReserveNullBuffer() {
    if (!null_buffer) {
        null_buffer = CreateNullBuffer();
    }
}

vk::Buffer BufferCacheRuntime::CreateNullBuffer() {
    VkBufferCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = 4,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    if (device.IsExtTransformFeedbackSupported()) {
        create_info.usage |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
    }
    vk::Buffer ret = memory_allocator.CreateBuffer(create_info, MemoryUsage::DeviceLocal);
    if (device.HasDebuggingToolAttached()) {
        ret.SetObjectNameEXT("Null buffer");
    }

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([buffer = *ret](vk::CommandBuffer cmdbuf) {
        cmdbuf.FillBuffer(buffer, 0, VK_WHOLE_SIZE, 0);
    });

    return ret;
}

} // namespace Vulkan
