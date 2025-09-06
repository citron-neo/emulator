// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <queue>
#include "common/common_types.h"
#include "video_core/host1x/ffmpeg/ffmpeg.h"
#include "video_core/host1x/nvdec_common.h"

namespace Tegra {

namespace Decoder {
class H264;
class VP8;
class VP9;
} // namespace Decoder

namespace Host1x {
class Host1x;
} // namespace Host1x

class Codec {
public:
    explicit Codec(Host1x::Host1x& host1x, const Host1x::NvdecCommon::NvdecRegisters& regs);
    ~Codec();

    /// Initialize the codec, returning success or failure
    void Initialize();

    /// Sets NVDEC video stream codec
    void SetTargetCodec(Host1x::NvdecCommon::VideoCodec codec);

    /// Call decoders to construct headers, decode AVFrame with ffmpeg
    void Decode();

    /// Returns next decoded frame
    [[nodiscard]] std::unique_ptr<FFmpeg::Frame> GetCurrentFrame();

    /// Returns the value of current_codec
    [[nodiscard]] Host1x::NvdecCommon::VideoCodec GetCurrentCodec() const;

    /// Return name of the current codec
    [[nodiscard]] std::string_view GetCurrentCodecName() const;

private:
    bool initialized{};
    Host1x::NvdecCommon::VideoCodec current_codec{Host1x::NvdecCommon::VideoCodec::None};
    FFmpeg::DecodeApi decode_api;

    Host1x::Host1x& host1x;
    const Host1x::NvdecCommon::NvdecRegisters& state;
    std::unique_ptr<Decoder::H264> h264_decoder;
    std::unique_ptr<Decoder::VP8> vp8_decoder;
    std::unique_ptr<Decoder::VP9> vp9_decoder;

    std::queue<std::unique_ptr<FFmpeg::Frame>> frames{};
};

} // namespace Tegra
