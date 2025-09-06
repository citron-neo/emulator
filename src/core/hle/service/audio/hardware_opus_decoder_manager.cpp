// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/audio/hardware_opus_decoder.h"
#include "core/hle/service/audio/hardware_opus_decoder_manager.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::Audio {

using namespace AudioCore::OpusDecoder;

IHardwareOpusDecoderManager::IHardwareOpusDecoderManager(Core::System& system_)
    : ServiceFramework{system_, "hwopus"}, system{system_}, impl{system} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IHardwareOpusDecoderManager::OpenHardwareOpusDecoder>, "OpenHardwareOpusDecoder"},
        {1, D<&IHardwareOpusDecoderManager::GetWorkBufferSize>, "GetWorkBufferSize"},
        {2, D<&IHardwareOpusDecoderManager::OpenHardwareOpusDecoderForMultiStream>, "OpenOpusDecoderForMultiStream"},
        {3, D<&IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStream>, "GetWorkBufferSizeForMultiStream"},
        {4, D<&IHardwareOpusDecoderManager::OpenHardwareOpusDecoderEx>, "OpenHardwareOpusDecoderEx"},
        {5, D<&IHardwareOpusDecoderManager::GetWorkBufferSizeEx>, "GetWorkBufferSizeEx"},
        {6, D<&IHardwareOpusDecoderManager::OpenHardwareOpusDecoderForMultiStreamEx>, "OpenHardwareOpusDecoderForMultiStreamEx"},
        {7, D<&IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStreamEx>, "GetWorkBufferSizeForMultiStreamEx"},
        {8, D<&IHardwareOpusDecoderManager::GetWorkBufferSizeExEx>, "GetWorkBufferSizeExEx"},
        {9, D<&IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStreamExEx>, "GetWorkBufferSizeForMultiStreamExEx"},
    };
    // clang-format on
    RegisterHandlers(functions);
}

IHardwareOpusDecoderManager::~IHardwareOpusDecoderManager() = default;

Result IHardwareOpusDecoderManager::OpenHardwareOpusDecoder(
    Out<SharedPointer<IHardwareOpusDecoder>> out_decoder, OpusParameters params, u32 tmem_size,
    InCopyHandle<Kernel::KTransferMemory> tmem_handle) {
    LOG_DEBUG(Service_Audio, "sample_rate {} channel_count {} transfer_memory_size {:#x}",
              params.sample_rate, params.channel_count, tmem_size);

    auto decoder{std::make_shared<IHardwareOpusDecoder>(system, impl.GetHardwareOpus())};
    OpusParametersEx ex{
        .sample_rate = params.sample_rate,
        .channel_count = params.channel_count,
        .use_large_frame_size = false,
    };
    R_TRY(decoder->Initialize(ex, tmem_handle.Get(), tmem_size));

    *out_decoder = decoder;
    R_SUCCEED();
}

Result IHardwareOpusDecoderManager::GetWorkBufferSize(Out<u32> out_size, OpusParameters params) {
    R_TRY(impl.GetWorkBufferSize(params, *out_size));
    LOG_DEBUG(Service_Audio, "sample_rate {} channel_count {} -- returned size {:#x}",
              params.sample_rate, params.channel_count, *out_size);
    R_SUCCEED();
}

Result IHardwareOpusDecoderManager::OpenHardwareOpusDecoderForMultiStream(
    Out<SharedPointer<IHardwareOpusDecoder>> out_decoder,
    InLargeData<OpusMultiStreamParameters, BufferAttr_HipcPointer> params, u32 tmem_size,
    InCopyHandle<Kernel::KTransferMemory> tmem_handle) {
    LOG_DEBUG(Service_Audio,
              "sample_rate {} channel_count {} total_stream_count {} stereo_stream_count {} "
              "transfer_memory_size {:#x}",
              params->sample_rate, params->channel_count, params->total_stream_count,
              params->stereo_stream_count, tmem_size);

    auto decoder{std::make_shared<IHardwareOpusDecoder>(system, impl.GetHardwareOpus())};

    OpusMultiStreamParametersEx ex{
        .sample_rate = params->sample_rate,
        .channel_count = params->channel_count,
        .total_stream_count = params->total_stream_count,
        .stereo_stream_count = params->stereo_stream_count,
        .use_large_frame_size = false,
        .mappings{},
    };
    std::memcpy(ex.mappings.data(), params->mappings.data(), sizeof(params->mappings));
    R_TRY(decoder->Initialize(ex, tmem_handle.Get(), tmem_size));

    *out_decoder = decoder;
    R_SUCCEED();
}

Result IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStream(
    Out<u32> out_size, InLargeData<OpusMultiStreamParameters, BufferAttr_HipcPointer> params) {
    R_TRY(impl.GetWorkBufferSizeForMultiStream(*params, *out_size));
    LOG_DEBUG(Service_Audio, "size {:#x}", *out_size);
    R_SUCCEED();
}

Result IHardwareOpusDecoderManager::OpenHardwareOpusDecoderEx(
    Out<SharedPointer<IHardwareOpusDecoder>> out_decoder, OpusParametersEx params, u32 tmem_size,
    InCopyHandle<Kernel::KTransferMemory> tmem_handle) {
    LOG_DEBUG(Service_Audio, "sample_rate {} channel_count {} transfer_memory_size {:#x}",
              params.sample_rate, params.channel_count, tmem_size);

    auto decoder{std::make_shared<IHardwareOpusDecoder>(system, impl.GetHardwareOpus())};
    R_TRY(decoder->Initialize(params, tmem_handle.Get(), tmem_size));

    *out_decoder = decoder;
    R_SUCCEED();
}

Result IHardwareOpusDecoderManager::GetWorkBufferSizeEx(Out<u32> out_size,
                                                        OpusParametersEx params) {
    R_TRY(impl.GetWorkBufferSizeEx(params, *out_size));
    LOG_DEBUG(Service_Audio, "size {:#x}", *out_size);
    R_SUCCEED();
}

Result IHardwareOpusDecoderManager::OpenHardwareOpusDecoderForMultiStreamEx(
    Out<SharedPointer<IHardwareOpusDecoder>> out_decoder,
    InLargeData<OpusMultiStreamParametersEx, BufferAttr_HipcPointer> params, u32 tmem_size,
    InCopyHandle<Kernel::KTransferMemory> tmem_handle) {
    LOG_DEBUG(Service_Audio,
              "sample_rate {} channel_count {} total_stream_count {} stereo_stream_count {} "
              "use_large_frame_size {}"
              "transfer_memory_size {:#x}",
              params->sample_rate, params->channel_count, params->total_stream_count,
              params->stereo_stream_count, params->use_large_frame_size, tmem_size);

    auto decoder{std::make_shared<IHardwareOpusDecoder>(system, impl.GetHardwareOpus())};

    R_TRY(decoder->Initialize(*params, tmem_handle.Get(), tmem_size));

    *out_decoder = decoder;
    R_SUCCEED();
}

Result IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStreamEx(
    Out<u32> out_size, InLargeData<OpusMultiStreamParametersEx, BufferAttr_HipcPointer> params) {
    R_TRY(impl.GetWorkBufferSizeForMultiStreamEx(*params, *out_size));
    LOG_DEBUG(Service_Audio,
              "sample_rate {} channel_count {} total_stream_count {} stereo_stream_count {} "
              "use_large_frame_size {} -- returned size {:#x}",
              params->sample_rate, params->channel_count, params->total_stream_count,
              params->stereo_stream_count, params->use_large_frame_size, *out_size);
    R_SUCCEED();
}

Result IHardwareOpusDecoderManager::GetWorkBufferSizeExEx(Out<u32> out_size,
                                                          OpusParametersEx params) {
    R_TRY(impl.GetWorkBufferSizeExEx(params, *out_size));
    LOG_DEBUG(Service_Audio, "size {:#x}", *out_size);
    R_SUCCEED();
}

Result IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStreamExEx(
    Out<u32> out_size, InLargeData<OpusMultiStreamParametersEx, BufferAttr_HipcPointer> params) {
    R_TRY(impl.GetWorkBufferSizeForMultiStreamExEx(*params, *out_size));
    LOG_DEBUG(Service_Audio, "size {:#x}", *out_size);
    R_SUCCEED();
}

} // namespace Service::Audio
