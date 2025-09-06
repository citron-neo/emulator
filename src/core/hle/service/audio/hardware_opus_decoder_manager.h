// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "audio_core/opus/decoder_manager.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

class IHardwareOpusDecoder;

using AudioCore::OpusDecoder::OpusMultiStreamParameters;
using AudioCore::OpusDecoder::OpusMultiStreamParametersEx;
using AudioCore::OpusDecoder::OpusParameters;
using AudioCore::OpusDecoder::OpusParametersEx;

class IHardwareOpusDecoderManager final : public ServiceFramework<IHardwareOpusDecoderManager> {
public:
    explicit IHardwareOpusDecoderManager(Core::System& system_);
    ~IHardwareOpusDecoderManager() override;

private:
    Result OpenHardwareOpusDecoder(Out<SharedPointer<IHardwareOpusDecoder>> out_decoder,
                                   OpusParameters params, u32 tmem_size,
                                   InCopyHandle<Kernel::KTransferMemory> tmem_handle);
    Result GetWorkBufferSize(Out<u32> out_size, OpusParameters params);
    Result OpenHardwareOpusDecoderForMultiStream(
        Out<SharedPointer<IHardwareOpusDecoder>> out_decoder,
        InLargeData<OpusMultiStreamParameters, BufferAttr_HipcPointer> params, u32 tmem_size,
        InCopyHandle<Kernel::KTransferMemory> tmem_handle);
    Result GetWorkBufferSizeForMultiStream(
        Out<u32> out_size, InLargeData<OpusMultiStreamParameters, BufferAttr_HipcPointer> params);
    Result OpenHardwareOpusDecoderEx(Out<SharedPointer<IHardwareOpusDecoder>> out_decoder,
                                     OpusParametersEx params, u32 tmem_size,
                                     InCopyHandle<Kernel::KTransferMemory> tmem_handle);
    Result GetWorkBufferSizeEx(Out<u32> out_size, OpusParametersEx params);
    Result OpenHardwareOpusDecoderForMultiStreamEx(
        Out<SharedPointer<IHardwareOpusDecoder>> out_decoder,
        InLargeData<OpusMultiStreamParametersEx, BufferAttr_HipcPointer> params, u32 tmem_size,
        InCopyHandle<Kernel::KTransferMemory> tmem_handle);
    Result GetWorkBufferSizeForMultiStreamEx(
        Out<u32> out_size, InLargeData<OpusMultiStreamParametersEx, BufferAttr_HipcPointer> params);
    Result GetWorkBufferSizeExEx(Out<u32> out_size, OpusParametersEx params);
    Result GetWorkBufferSizeForMultiStreamExEx(
        Out<u32> out_size, InLargeData<OpusMultiStreamParametersEx, BufferAttr_HipcPointer> params);

    Core::System& system;
    AudioCore::OpusDecoder::OpusDecoderManager impl;
};

} // namespace Service::Audio
