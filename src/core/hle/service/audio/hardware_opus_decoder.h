// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "audio_core/opus/decoder.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

class IHardwareOpusDecoder final : public ServiceFramework<IHardwareOpusDecoder> {
public:
    explicit IHardwareOpusDecoder(Core::System& system_,
                                  AudioCore::OpusDecoder::HardwareOpus& hardware_opus);
    ~IHardwareOpusDecoder() override;

    Result Initialize(const AudioCore::OpusDecoder::OpusParametersEx& params,
                      Kernel::KTransferMemory* transfer_memory, u64 transfer_memory_size);
    Result Initialize(const AudioCore::OpusDecoder::OpusMultiStreamParametersEx& params,
                      Kernel::KTransferMemory* transfer_memory, u64 transfer_memory_size);

private:
    Result DecodeInterleavedOld(OutBuffer<BufferAttr_HipcMapAlias> out_pcm_data,
                                Out<u32> out_data_size, Out<u32> out_sample_count,
                                InBuffer<BufferAttr_HipcMapAlias> opus_data);
    Result SetContext(InBuffer<BufferAttr_HipcMapAlias> decoder_context);
    Result DecodeInterleavedForMultiStreamOld(OutBuffer<BufferAttr_HipcMapAlias> out_pcm_data,
                                              Out<u32> out_data_size, Out<u32> out_sample_count,
                                              InBuffer<BufferAttr_HipcMapAlias> opus_data);
    Result SetContextForMultiStream(InBuffer<BufferAttr_HipcMapAlias> decoder_context);
    Result DecodeInterleavedWithPerfOld(
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
        Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
        InBuffer<BufferAttr_HipcMapAlias> opus_data);
    Result DecodeInterleavedForMultiStreamWithPerfOld(
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
        Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
        InBuffer<BufferAttr_HipcMapAlias> opus_data);
    Result DecodeInterleavedWithPerfAndResetOld(
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
        Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
        InBuffer<BufferAttr_HipcMapAlias> opus_data, bool reset);
    Result DecodeInterleavedForMultiStreamWithPerfAndResetOld(
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
        Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
        InBuffer<BufferAttr_HipcMapAlias> opus_data, bool reset);
    Result DecodeInterleaved(
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
        Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
        InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> opus_data,
        bool reset);
    Result DecodeInterleavedForMultiStream(
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
        Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
        InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> opus_data,
        bool reset);

    std::unique_ptr<AudioCore::OpusDecoder::OpusDecoder> impl;
    Common::ScratchBuffer<u8> output_data;
};

} // namespace Service::Audio
