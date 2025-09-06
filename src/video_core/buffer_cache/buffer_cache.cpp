// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/microprofile.h"
#include "video_core/buffer_cache/buffer_cache_base.h"
#include "video_core/control/channel_state_cache.inc"

namespace VideoCommon {

MICROPROFILE_DEFINE(GPU_PrepareBuffers, "GPU", "Prepare buffers", MP_RGB(224, 128, 128));
MICROPROFILE_DEFINE(GPU_BindUploadBuffers, "GPU", "Bind and upload buffers", MP_RGB(224, 128, 128));
MICROPROFILE_DEFINE(GPU_DownloadMemory, "GPU", "Download buffers", MP_RGB(224, 128, 128));

template class VideoCommon::ChannelSetupCaches<VideoCommon::BufferCacheChannelInfo>;

} // namespace VideoCommon
