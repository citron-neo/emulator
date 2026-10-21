// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/assert.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "video_core/control/channel_state.h"
#include "video_core/dma_pusher.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/kepler_memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/engines/puller.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace Tegra::Engines {

Puller::Puller(GPU& gpu_, MemoryManager& memory_manager_, DmaPusher& dma_pusher_,
               Control::ChannelState& channel_state_)
    : gpu{gpu_}, memory_manager{memory_manager_}, dma_pusher{dma_pusher_}, channel_state{
                                                                               channel_state_} {}

Puller::~Puller() = default;

void Puller::ProcessBindMethod(const MethodCall& method_call) {
    // Bind the current subchannel to the desired engine id.
    LOG_DEBUG(HW_GPU, "Binding subchannel {} to engine {}", method_call.subchannel,
              method_call.argument);
    const auto engine_id = static_cast<EngineID>(method_call.argument);
    bound_engines[method_call.subchannel] = engine_id;
    switch (engine_id) {
    case EngineID::FERMI_TWOD_A:
        dma_pusher.BindSubchannel(channel_state.fermi_2d.get(), method_call.subchannel,
                                  EngineTypes::Fermi2D);
        break;
    case EngineID::MAXWELL_B:
        dma_pusher.BindSubchannel(channel_state.maxwell_3d.get(), method_call.subchannel,
                                  EngineTypes::Maxwell3D);
        break;
    case EngineID::KEPLER_COMPUTE_B:
        dma_pusher.BindSubchannel(channel_state.kepler_compute.get(), method_call.subchannel,
                                  EngineTypes::KeplerCompute);
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        dma_pusher.BindSubchannel(channel_state.maxwell_dma.get(), method_call.subchannel,
                                  EngineTypes::MaxwellDMA);
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        dma_pusher.BindSubchannel(channel_state.kepler_memory.get(), method_call.subchannel,
                                  EngineTypes::KeplerMemory);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine {:04X}", engine_id);
        break;
    }
}

void Puller::ProcessFenceActionMethod() {
    switch (regs.fence_action.op) {
    case Puller::FenceOperation::Acquire:
        // UNIMPLEMENTED_MSG("Channel Scheduling pending.");
        // WaitFence(regs.fence_action.syncpoint_id, regs.fence_value);
        rasterizer->ReleaseFences();
        break;
    case Puller::FenceOperation::Increment:
        rasterizer->SignalSyncPoint(regs.fence_action.syncpoint_id);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented operation {}", regs.fence_action.op.Value());
        break;
    }
}

void Puller::ProcessSemaphoreTriggerMethod() {
    // MAXWELL_CHANNEL_GPFIFO_A (clb06f.h): SEMAPHORED_OPERATION is 4:0, RELEASE_SIZE is 24:24
    // (0 = 16 bytes: payload, zero, timestamp; 1 = 4 bytes: payload only).
    static constexpr u32 semaphore_operation_mask = 0x1F;
    static constexpr u32 release_size_4byte = 1U << 24;
    const auto op =
        static_cast<GpuSemaphoreOperation>(regs.semaphore_trigger & semaphore_operation_mask);
    if (op == GpuSemaphoreOperation::WriteLong) {
        const GPUVAddr sequence_address{regs.semaphore_address.SemaphoreAddress()};
        const u32 payload = regs.semaphore_sequence;
        // IsAFence: write the release once the host GPU has finished the work before it, like
        // SemaphoreRelease below. Without it the write waits in SyncOperation for some later
        // fence, so a guest polling the semaphore without a fence of its own never sees it.
        auto flags = VideoCommon::QueryPropertiesFlags::IsAFence;
        if ((regs.semaphore_trigger & release_size_4byte) == 0) {
            flags |= VideoCommon::QueryPropertiesFlags::HasTimeout;
        }
        rasterizer->Query(sequence_address, VideoCommon::QueryType::Payload, flags, payload, 0);
    } else if (op == GpuSemaphoreOperation::AcquireEqual ||
               op == GpuSemaphoreOperation::AcquireGequal ||
               op == GpuSemaphoreOperation::AcquireMask) {
        const GPUVAddr address{regs.semaphore_address.SemaphoreAddress()};
        const u32 payload = regs.semaphore_sequence;
        regs.acquire_source = true;
        regs.acquire_value = payload;
        if (op != GpuSemaphoreOperation::AcquireMask) {
            regs.acquire_active = true;
            regs.acquire_mode = op == GpuSemaphoreOperation::AcquireGequal;
        }
        StartAcquire(address, payload, op);
    } else {
        LOG_ERROR(HW_GPU, "Invalid semaphore operation {:#x}", static_cast<u32>(op));
    }
}

void Puller::StartAcquire(GPUVAddr address, u32 payload, GpuSemaphoreOperation op) {
    if (AcquirePasses(op, memory_manager.Read<u32>(address), payload)) {
        return;
    }
    // The release may be one this or another channel made earlier that still waits for a
    // host fence; make those writes first.
    rasterizer->ReleaseFences();
    if (AcquirePasses(op, memory_manager.Read<u32>(address), payload)) {
        return;
    }
    // dev_pbdma.ref.txt (Volta, NV_UDMA_SEM_EXECUTE; open-gpu-doc has no Maxwell host
    // manual): after a failed acquire Host processes no further methods of the channel,
    // switches to another channel and polls the semaphore when it comes back. The pusher stops
    // this channel here and the GPU thread retries it (RetryPendingAcquire).
    pending_acquire = PendingAcquire{address, payload, op, std::chrono::steady_clock::now()};
}

void Puller::AcquireSemaphore(GPUVAddr address, u32 payload, bool greater_or_equal) {
    StartAcquire(address, payload,
                 greater_or_equal ? GpuSemaphoreOperation::AcquireGequal
                                  : GpuSemaphoreOperation::AcquireEqual);
}

constexpr bool Puller::AcquirePasses(GpuSemaphoreOperation op, u32 word, u32 payload) {
    switch (op) {
    case GpuSemaphoreOperation::AcquireEqual:
        return word == payload;
    case GpuSemaphoreOperation::AcquireGequal:
        // Unsigned compare, the same test as before this change.
        return word >= payload;
    case GpuSemaphoreOperation::AcquireMask:
        // dev_pbdma.ref.txt (Volta) ACQ_AND: passes when (semaphore & payload) != 0.
        return (word & payload) != 0;
    default:
        return true;
    }
}

bool Puller::RetryPendingAcquire(bool flush) {
    static_assert(AcquirePasses(GpuSemaphoreOperation::AcquireEqual, 1, 1));
    static_assert(!AcquirePasses(GpuSemaphoreOperation::AcquireEqual, 2, 1));
    static_assert(AcquirePasses(GpuSemaphoreOperation::AcquireGequal, 2, 1));
    static_assert(!AcquirePasses(GpuSemaphoreOperation::AcquireGequal, 0, 1));
    static_assert(AcquirePasses(GpuSemaphoreOperation::AcquireMask, 3, 1));
    static_assert(!AcquirePasses(GpuSemaphoreOperation::AcquireMask, 2, 1));
    static_assert(!AcquirePasses(GpuSemaphoreOperation::AcquireMask, 0, 1));
    if (!pending_acquire) {
        return true;
    }
    const PendingAcquire& acquire = *pending_acquire;
    if (AcquirePasses(acquire.op, memory_manager.Read<u32>(acquire.address), acquire.payload)) {
        pending_acquire.reset();
        return true;
    }
    if (flush) {
        rasterizer->ReleaseFences();
        if (AcquirePasses(acquire.op, memory_manager.Read<u32>(acquire.address), acquire.payload)) {
            pending_acquire.reset();
            return true;
        }
    }
    // Not hardware behaviour. Host keeps polling and, with an acquire timeout set, raises an
    // interrupt for the driver (dev_pbdma.ref.txt, ACQUIRE_DEADLINE); it does not run past the
    // acquire. The emulator gives up after this long so that a release it never writes stalls
    // the channel for a while instead of forever. Before this change every acquire passed at once.
    static constexpr auto give_up_after = std::chrono::seconds{1};
    const auto waited = std::chrono::steady_clock::now() - acquire.since;
    if (waited < give_up_after) {
        return false;
    }
    LOG_WARNING(HW_GPU,
                "Semaphore acquire not satisfied after {} ms, continuing: address {:#x} op {:#x} "
                "payload {:#x} value {:#x}",
                std::chrono::duration_cast<std::chrono::milliseconds>(waited).count(),
                acquire.address, static_cast<u32>(acquire.op), acquire.payload,
                memory_manager.Read<u32>(acquire.address));
    pending_acquire.reset();
    return true;
}

void Puller::ProcessSemaphoreRelease() {
    const GPUVAddr sequence_address{regs.semaphore_address.SemaphoreAddress()};
    const u32 payload = regs.semaphore_release;
    rasterizer->Query(sequence_address, VideoCommon::QueryType::Payload,
                      VideoCommon::QueryPropertiesFlags::IsAFence, payload, 0);
}

void Puller::ProcessSemaphoreAcquire() {
    u32 word = memory_manager.Read<u32>(regs.semaphore_address.SemaphoreAddress());
    const auto value = regs.semaphore_acquire;
    while (word != value) {
        regs.acquire_active = true;
        regs.acquire_value = value;
        rasterizer->ReleaseFences();
        word = memory_manager.Read<u32>(regs.semaphore_address.SemaphoreAddress());
        // TODO(kemathe73) figure out how to do the acquire_timeout
        regs.acquire_mode = false;
        regs.acquire_source = false;
    }
}

/// Calls a GPU puller method.
void Puller::CallPullerMethod(const MethodCall& method_call) {
    regs.reg_array[method_call.method] = method_call.argument;
    const auto method = static_cast<BufferMethods>(method_call.method);

    switch (method) {
    case BufferMethods::BindObject: {
        ProcessBindMethod(method_call);
        break;
    }
    case BufferMethods::Nop:
    case BufferMethods::SemaphoreAddressHigh:
    case BufferMethods::SemaphoreAddressLow:
    case BufferMethods::SemaphoreSequencePayload:
    case BufferMethods::SyncpointPayload:
    case BufferMethods::WrcacheFlush:
        break;
    case BufferMethods::RefCnt:
        rasterizer->SignalReference();
        break;
    case BufferMethods::SyncpointOperation:
        ProcessFenceActionMethod();
        break;
    case BufferMethods::WaitForIdle:
        rasterizer->WaitForIdle();
        break;
    case BufferMethods::SemaphoreOperation: {
        ProcessSemaphoreTriggerMethod();
        break;
    }
    case BufferMethods::NonStallInterrupt: {
        LOG_ERROR(HW_GPU, "Special puller engine method NonStallInterrupt not implemented");
        break;
    }
    case BufferMethods::MemOpA:
    case BufferMethods::MemOpC:
        // Operands only; the operation is triggered by MEM_OP_B or MEM_OP_D.
        break;
    case BufferMethods::MemOpB:
        // The pre-gm20x trigger, kept as it was for a guest that still sends it.
        rasterizer->InvalidateGPUCache();
        break;
    case BufferMethods::MemOpD: {
        // MEM_OP_D OPERATION (bits 31:27, clb06f.h; on gm20x MEM_OP_C/D replace MEM_OP_A/B).
        // An L2 sysmem invalidate makes the GPU read what the CPU wrote, which is what
        // InvalidateGPUCache does. MEMBAR, TLB invalidates, L2 flushes and comptag cleans need
        // nothing here: GPU writes reach guest memory through the caches' own downloads, and the
        // GPU page table is read directly.
        constexpr u32 L2_SYSMEM_INVALIDATE = 0xe;
        if ((method_call.argument >> 27) == L2_SYSMEM_INVALIDATE) {
            rasterizer->InvalidateGPUCache();
        }
        break;
    }
    case BufferMethods::SemaphoreAcquire: {
        ProcessSemaphoreAcquire();
        break;
    }
    case BufferMethods::SemaphoreRelease: {
        ProcessSemaphoreRelease();
        break;
    }
    case BufferMethods::Yield: {
        // TODO(Kmather73): Research and implement this method.
        LOG_ERROR(HW_GPU, "Special puller engine method Yield not implemented");
        break;
    }
    default:
        LOG_ERROR(HW_GPU, "Special puller engine method {:X} not implemented", method);
        break;
    }
}

/// Calls a GPU engine method.
void Puller::CallEngineMethod(const MethodCall& method_call) {
    const EngineID engine = bound_engines[method_call.subchannel];

    switch (engine) {
    case EngineID::FERMI_TWOD_A:
        channel_state.fermi_2d->CallMethod(method_call.method, method_call.argument,
                                           method_call.IsLastCall());
        break;
    case EngineID::MAXWELL_B:
        channel_state.maxwell_3d->CallMethod(method_call.method, method_call.argument,
                                             method_call.IsLastCall());
        break;
    case EngineID::KEPLER_COMPUTE_B:
        channel_state.kepler_compute->CallMethod(method_call.method, method_call.argument,
                                                 method_call.IsLastCall());
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        channel_state.maxwell_dma->CallMethod(method_call.method, method_call.argument,
                                              method_call.IsLastCall());
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        channel_state.kepler_memory->CallMethod(method_call.method, method_call.argument,
                                                method_call.IsLastCall());
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine");
        break;
    }
}

/// Calls a GPU engine multivalue method.
void Puller::CallEngineMultiMethod(u32 method, u32 subchannel, const u32* base_start, u32 amount,
                                   u32 methods_pending) {
    const EngineID engine = bound_engines[subchannel];

    switch (engine) {
    case EngineID::FERMI_TWOD_A:
        channel_state.fermi_2d->CallMultiMethod(method, base_start, amount, methods_pending);
        break;
    case EngineID::MAXWELL_B:
        channel_state.maxwell_3d->CallMultiMethod(method, base_start, amount, methods_pending);
        break;
    case EngineID::KEPLER_COMPUTE_B:
        channel_state.kepler_compute->CallMultiMethod(method, base_start, amount, methods_pending);
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        channel_state.maxwell_dma->CallMultiMethod(method, base_start, amount, methods_pending);
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        channel_state.kepler_memory->CallMultiMethod(method, base_start, amount, methods_pending);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine");
        break;
    }
}

/// Calls a GPU method.
void Puller::CallMethod(const MethodCall& method_call) {
    LOG_TRACE(HW_GPU, "Processing method {:08X} on subchannel {}", method_call.method,
              method_call.subchannel);

    assert(method_call.subchannel < bound_engines.size());

    if (ExecuteMethodOnEngine(method_call.method)) {
        CallEngineMethod(method_call);
    } else {
        CallPullerMethod(method_call);
    }
}

/// Calls a GPU multivalue method.
void Puller::CallMultiMethod(u32 method, u32 subchannel, const u32* base_start, u32 amount,
                             u32 methods_pending) {
    LOG_TRACE(HW_GPU, "Processing method {:08X} on subchannel {}", method, subchannel);

    assert(subchannel < bound_engines.size());

    if (ExecuteMethodOnEngine(method)) {
        CallEngineMultiMethod(method, subchannel, base_start, amount, methods_pending);
    } else {
        for (u32 i = 0; i < amount; i++) {
            CallPullerMethod(MethodCall{
                method,
                base_start[i],
                subchannel,
                methods_pending - i,
            });
        }
    }
}

void Puller::BindRasterizer(VideoCore::RasterizerInterface* rasterizer_) {
    rasterizer = rasterizer_;
}

/// Determines where the method should be executed.
[[nodiscard]] bool Puller::ExecuteMethodOnEngine(u32 method) {
    const auto buffer_method = static_cast<BufferMethods>(method);
    return buffer_method >= BufferMethods::NonPullerMethods;
}

} // namespace Tegra::Engines
