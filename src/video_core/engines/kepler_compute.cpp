// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bitset>
#include "common/assert.h"
#include "common/logging.h"
#include "core/core.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/invalid_gpu_address.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines {

KeplerCompute::KeplerCompute(Core::System& system_, MemoryManager& memory_manager_)
    : system{system_}, memory_manager{memory_manager_}, upload_state{memory_manager, regs.upload} {
    execution_mask.reset();
    execution_mask[KEPLER_COMPUTE_REG_INDEX(exec_upload)] = true;
    execution_mask[KEPLER_COMPUTE_REG_INDEX(data_upload)] = true;
    execution_mask[KEPLER_COMPUTE_REG_INDEX(launch)] = true;
}

KeplerCompute::~KeplerCompute() = default;

void KeplerCompute::BindRasterizer(VideoCore::RasterizerInterface* rasterizer_) {
    rasterizer = rasterizer_;
    upload_state.BindRasterizer(rasterizer);
}

void KeplerCompute::ConsumeSinkImpl() {
    for (auto [method, value] : method_sink) {
        regs.reg_array[method] = value;
    }
    method_sink.clear();
}

void KeplerCompute::CallMethod(u32 method, u32 method_argument, [[maybe_unused]] bool is_last_call) {
    assert(method < Regs::NUM_REGS &&
           "Invalid KeplerCompute register, increase the size of the Regs structure");

    regs.reg_array[method] = method_argument;

    switch (method) {
    case KEPLER_COMPUTE_REG_INDEX(exec_upload): {
        upload_state.ProcessExec(regs.exec_upload.linear != 0);
        const bool contiguous_destination =
            regs.exec_upload.linear != 0 &&
            (regs.upload.line_count == 1 || regs.upload.dest.pitch == regs.upload.line_length_in);
        upload_source.Begin(upload_state.ExecTargetAddress(), upload_state.GetUploadSize(),
                            contiguous_destination);
        break;
    }
    case KEPLER_COMPUTE_REG_INDEX(data_upload): {
        ProcessUpload(&method_argument, 1);
        break;
    }
    case KEPLER_COMPUTE_REG_INDEX(launch): {
        const GPUVAddr launch_desc_loc = regs.launch_desc_loc.Address();
        constexpr size_t qmd_size = LaunchParams::NUM_LAUNCH_PARAMETERS * sizeof(u32);
        if (!memory_manager.IsWithinGPUAddressRange(launch_desc_loc, qmd_size)) {
            LOG_ERROR(HW_GPU, "QMD outside GPU address space: address=0x{:x}, bytes={}",
                      launch_desc_loc, qmd_size);
            throw VideoCommon::InvalidGpuAddress{};
        }

        constexpr size_t dispatch_size = 3 * sizeof(u32);
        const GPUVAddr dispatch_address = launch_desc_loc + offsetof(LaunchParams, grid_dim_x);
        for (const auto& data : uploads) {
            // A later overwrite must not reuse an earlier upload's GPU source.
            // Non-contiguous destinations cannot be represented by this range;
            // conservatively fall back to the assembled descriptor for them.
            if (!data.contiguous || data.Overlaps(dispatch_address, dispatch_size)) {
                indirect_compute.reset();
            }
            const auto source = data.IndirectAddress(dispatch_address, dispatch_size);
            if (source && memory_manager.IsMemoryDirty(*source, dispatch_size)) {
                indirect_compute = source;
            }
        }
        uploads.clear();
        ProcessLaunch();
        indirect_compute = std::nullopt;
        break;
    }
    default:
        break;
    }
}

void KeplerCompute::CallMultiMethod(u32 method, const u32* base_start, u32 amount,
                                    u32 methods_pending) {
    switch (method) {
    case KEPLER_COMPUTE_REG_INDEX(data_upload):
        ProcessUpload(base_start, amount);
        return;
    default:
        for (u32 i = 0; i < amount; i++) {
            CallMethod(method, base_start[i], methods_pending - i <= 1);
        }
        break;
    }
}

void KeplerCompute::ProcessUpload(const u32* data, size_t count) {
    const auto source = upload_source.Append(current_dma_segment, count * sizeof(u32));
    upload_state.ProcessData(data, count);
    if (source) {
        uploads.push_back(*source);
    }
}

void KeplerCompute::ProcessLaunch() {
    const GPUVAddr launch_desc_loc = regs.launch_desc_loc.Address();
    memory_manager.ReadBlockUnsafe(launch_desc_loc, &launch_description,
                                   LaunchParams::NUM_LAUNCH_PARAMETERS * sizeof(u32),
                                   "KeplerCompute.launch_description");
    rasterizer->DispatchCompute();
    ReleaseQmdSemaphores();
}

// A QMD can ask for up to two semaphore releases when its grid completes (SEMAPHORE_RELEASE_ENABLE0
// and 1, clb1c0qmd.h). They are queued like the DMA engine queues its semaphore release, after the
// dispatch, so a guest that waits on one sees it once the work is done.
void KeplerCompute::ReleaseQmdSemaphores() {
    const std::array enabled{launch_description.release0_enable.Value() != 0,
                             launch_description.release1_enable.Value() != 0};
    for (size_t i = 0; i < enabled.size(); ++i) {
        if (!enabled[i]) {
            continue;
        }
        const LaunchParams::Release& release = launch_description.releases[i];
        if (release.reduction_enable != 0) {
            if (!reduction_reported) {
                reduction_reported = true;
                LOG_WARNING(HW_GPU, "QMD semaphore release with reduction op {} is not translated",
                            release.reduction_op.Value());
            }
            continue;
        }
        VideoCommon::QueryPropertiesFlags flags{VideoCommon::QueryPropertiesFlags::IsAFence};
        if (release.one_word == 0) {
            flags |= VideoCommon::QueryPropertiesFlags::HasTimeout;
        }
        rasterizer->Query(release.Address(), VideoCommon::QueryType::Payload, flags,
                          release.payload, 0);
    }
}

Texture::TICEntry KeplerCompute::GetTICEntry(u32 tic_index) const {
    const GPUVAddr tic_address_gpu{regs.tic.Address() + tic_index * sizeof(Texture::TICEntry)};

    Texture::TICEntry tic_entry;
    memory_manager.ReadBlockUnsafe(tic_address_gpu, &tic_entry, sizeof(Texture::TICEntry),
                                   "KeplerCompute.TIC");
    return tic_entry;
}

Texture::TSCEntry KeplerCompute::GetTSCEntry(u32 tsc_index) const {
    const GPUVAddr tsc_address_gpu{regs.tsc.Address() + tsc_index * sizeof(Texture::TSCEntry)};

    Texture::TSCEntry tsc_entry;
    memory_manager.ReadBlockUnsafe(tsc_address_gpu, &tsc_entry, sizeof(Texture::TSCEntry),
                                   "KeplerCompute.TSC");
    return tsc_entry;
}

} // namespace Tegra::Engines
