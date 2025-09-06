// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/settings.h"
#include "video_core/host1x/codecs/codec.h"
#include "video_core/host1x/codecs/h264.h"
#include "video_core/host1x/codecs/vp8.h"
#include "video_core/host1x/codecs/vp9.h"
#include "video_core/host1x/host1x.h"
#include "video_core/memory_manager.h"

namespace Tegra {

Codec::Codec(Host1x::Host1x& host1x_, const Host1x::NvdecCommon::NvdecRegisters& regs)
    : host1x(host1x_), state{regs}, h264_decoder(std::make_unique<Decoder::H264>(host1x)),
      vp8_decoder(std::make_unique<Decoder::VP8>(host1x)),
      vp9_decoder(std::make_unique<Decoder::VP9>(host1x)) {}

Codec::~Codec() = default;

void Codec::Initialize() {
    initialized = decode_api.Initialize(current_codec);
}

void Codec::SetTargetCodec(Host1x::NvdecCommon::VideoCodec codec) {
    if (current_codec != codec) {
        current_codec = codec;
        LOG_INFO(Service_NVDRV, "NVDEC video codec initialized to {}", GetCurrentCodecName());
    }
}

void Codec::Decode() {
    const bool is_first_frame = !initialized;
    if (is_first_frame) {
        Initialize();
    }

    if (!initialized) {
        return;
    }

    // Assemble bitstream.
    bool vp9_hidden_frame = false;
    size_t configuration_size = 0;
    const auto packet_data = [&]() {
        switch (current_codec) {
        case Tegra::Host1x::NvdecCommon::VideoCodec::H264:
            return h264_decoder->ComposeFrame(state, &configuration_size, is_first_frame);
        case Tegra::Host1x::NvdecCommon::VideoCodec::VP8:
            return vp8_decoder->ComposeFrame(state);
        case Tegra::Host1x::NvdecCommon::VideoCodec::VP9:
            vp9_decoder->ComposeFrame(state);
            vp9_hidden_frame = vp9_decoder->WasFrameHidden();
            return vp9_decoder->GetFrameBytes();
        default:
            ASSERT(false);
            return std::span<const u8>{};
        }
    }();

    // Send assembled bitstream to decoder.
    if (!decode_api.SendPacket(packet_data, configuration_size)) {
        return;
    }

    // Only receive/store visible frames.
    if (vp9_hidden_frame) {
        return;
    }

    // Receive output frames from decoder.
    decode_api.ReceiveFrames(frames);

    while (frames.size() > 10) {
        LOG_DEBUG(HW_GPU, "ReceiveFrames overflow, dropped frame");
        frames.pop();
    }
}

std::unique_ptr<FFmpeg::Frame> Codec::GetCurrentFrame() {
    // Sometimes VIC will request more frames than have been decoded.
    // in this case, return a blank frame and don't overwrite previous data.
    if (frames.empty()) {
        return {};
    }

    auto frame = std::move(frames.front());
    frames.pop();
    return frame;
}

Host1x::NvdecCommon::VideoCodec Codec::GetCurrentCodec() const {
    return current_codec;
}

std::string_view Codec::GetCurrentCodecName() const {
    switch (current_codec) {
    case Host1x::NvdecCommon::VideoCodec::None:
        return "None";
    case Host1x::NvdecCommon::VideoCodec::H264:
        return "H264";
    case Host1x::NvdecCommon::VideoCodec::VP8:
        return "VP8";
    case Host1x::NvdecCommon::VideoCodec::H265:
        return "H265";
    case Host1x::NvdecCommon::VideoCodec::VP9:
        return "VP9";
    default:
        return "Unknown";
    }
}
} // namespace Tegra
