// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <queue>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/host1x/nvdec_common.h"

extern "C" {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

namespace FFmpeg {

class Packet;
class Frame;
class Decoder;
class HardwareContext;
class DecoderContext;
class DeinterlaceFilter;

// Wraps an AVPacket, a container for compressed bitstream data.
class Packet {
public:
    CITRON_NON_COPYABLE(Packet);
    CITRON_NON_MOVEABLE(Packet);

    explicit Packet(std::span<const u8> data);
    ~Packet();

    AVPacket* GetPacket() const {
        return m_packet;
    }

private:
    AVPacket* m_packet{};
};

// Wraps an AVFrame, a container for audio and video stream data.
class Frame {
public:
    CITRON_NON_COPYABLE(Frame);
    CITRON_NON_MOVEABLE(Frame);

    explicit Frame();
    ~Frame();

    int GetWidth() const {
        return m_frame->width;
    }

    int GetHeight() const {
        return m_frame->height;
    }

    AVPixelFormat GetPixelFormat() const {
        return static_cast<AVPixelFormat>(m_frame->format);
    }

    int GetStride(int plane) const {
        return m_frame->linesize[plane];
    }

    int* GetStrides() const {
        return m_frame->linesize;
    }

    u8* GetData(int plane) const {
        return m_frame->data[plane];
    }

    u8** GetPlanes() const {
        return m_frame->data;
    }

    void SetFormat(int format) {
        m_frame->format = format;
    }

    AVFrame* GetFrame() const {
        return m_frame;
    }

private:
    AVFrame* m_frame{};
};

// Wraps an AVCodec, a type containing information about a codec.
class Decoder {
public:
    CITRON_NON_COPYABLE(Decoder);
    CITRON_NON_MOVEABLE(Decoder);

    explicit Decoder(Tegra::Host1x::NvdecCommon::VideoCodec codec);
    ~Decoder() = default;

    bool SupportsDecodingOnDevice(AVPixelFormat* out_pix_fmt, AVHWDeviceType type) const;

    const AVCodec* GetCodec() const {
        return m_codec;
    }

private:
    const AVCodec* m_codec{};
};

// Wraps AVBufferRef for an accelerated decoder.
class HardwareContext {
public:
    CITRON_NON_COPYABLE(HardwareContext);
    CITRON_NON_MOVEABLE(HardwareContext);

    static std::vector<AVHWDeviceType> GetSupportedDeviceTypes();

    explicit HardwareContext() = default;
    ~HardwareContext();

    bool InitializeForDecoder(DecoderContext& decoder_context, const Decoder& decoder);

    AVBufferRef* GetBufferRef() const {
        return m_gpu_decoder;
    }

private:
    bool InitializeWithType(AVHWDeviceType type);

    AVBufferRef* m_gpu_decoder{};
};

// Wraps an AVCodecContext.
class DecoderContext {
public:
    CITRON_NON_COPYABLE(DecoderContext);
    CITRON_NON_MOVEABLE(DecoderContext);

    explicit DecoderContext(const Decoder& decoder);
    ~DecoderContext();

    void InitializeHardwareDecoder(const HardwareContext& context, AVPixelFormat hw_pix_fmt);
    bool OpenContext(const Decoder& decoder);
    bool SendPacket(const Packet& packet);
    std::unique_ptr<Frame> ReceiveFrame(bool* out_is_interlaced);

    AVCodecContext* GetCodecContext() const {
        return m_codec_context;
    }

private:
    AVCodecContext* m_codec_context{};
};

// Wraps an AVFilterGraph.
class DeinterlaceFilter {
public:
    CITRON_NON_COPYABLE(DeinterlaceFilter);
    CITRON_NON_MOVEABLE(DeinterlaceFilter);

    explicit DeinterlaceFilter(const Frame& frame);
    ~DeinterlaceFilter();

    bool AddSourceFrame(const Frame& frame);
    std::unique_ptr<Frame> DrainSinkFrame();

private:
    AVFilterGraph* m_filter_graph{};
    AVFilterContext* m_source_context{};
    AVFilterContext* m_sink_context{};
    bool m_initialized{};
};

class DecodeApi {
public:
    CITRON_NON_COPYABLE(DecodeApi);
    CITRON_NON_MOVEABLE(DecodeApi);

    DecodeApi() = default;
    ~DecodeApi() = default;

    bool Initialize(Tegra::Host1x::NvdecCommon::VideoCodec codec);
    void Reset();

    bool SendPacket(std::span<const u8> packet_data, size_t configuration_size);
    void ReceiveFrames(std::queue<std::unique_ptr<Frame>>& frame_queue);

private:
    std::optional<FFmpeg::Decoder> m_decoder;
    std::optional<FFmpeg::DecoderContext> m_decoder_context;
    std::optional<FFmpeg::HardwareContext> m_hardware_context;
    std::optional<FFmpeg::DeinterlaceFilter> m_deinterlace_filter;
};

} // namespace FFmpeg
