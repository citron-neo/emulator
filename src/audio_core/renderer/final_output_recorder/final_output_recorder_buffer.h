// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"
#include "common/swap.h"

namespace AudioCore {

struct FinalOutputRecorderBuffer {
    /* 0x00 */ FinalOutputRecorderBuffer* next;
    /* 0x08 */ VAddr samples;
    /* 0x10 */ u64 capacity;
    /* 0x18 */ u64 size;
    /* 0x20 */ u64 offset;
    /* 0x28 */ u64 end_timestamp;
};
static_assert(sizeof(FinalOutputRecorderBuffer) == 0x30,
              "FinalOutputRecorderBuffer is an invalid size");

struct FinalOutputRecorderParameter {
    /* 0x0 */ s32_le sample_rate;
    /* 0x4 */ u16_le channel_count;
    /* 0x6 */ u16_le reserved;
};
static_assert(sizeof(FinalOutputRecorderParameter) == 0x8,
              "FinalOutputRecorderParameter is an invalid size");

struct FinalOutputRecorderParameterInternal {
    /* 0x0 */ u32_le sample_rate;
    /* 0x4 */ u32_le channel_count;
    /* 0x8 */ u32_le sample_format;
    /* 0xC */ u32_le state;
};
static_assert(sizeof(FinalOutputRecorderParameterInternal) == 0x10,
              "FinalOutputRecorderParameterInternal is an invalid size");

} // namespace AudioCore
