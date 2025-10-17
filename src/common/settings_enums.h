// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <utility>
#include <vector>
#include "common/common_types.h"

namespace Settings {

template <typename T>
struct EnumMetadata {
    static std::vector<std::pair<std::string, T>> Canonicalizations();
    static u32 Index();
};

#define PAIR_45(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_46(N, __VA_ARGS__))
#define PAIR_44(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_45(N, __VA_ARGS__))
#define PAIR_43(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_44(N, __VA_ARGS__))
#define PAIR_42(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_43(N, __VA_ARGS__))
#define PAIR_41(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_42(N, __VA_ARGS__))
#define PAIR_40(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_41(N, __VA_ARGS__))
#define PAIR_39(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_40(N, __VA_ARGS__))
#define PAIR_38(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_39(N, __VA_ARGS__))
#define PAIR_37(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_38(N, __VA_ARGS__))
#define PAIR_36(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_37(N, __VA_ARGS__))
#define PAIR_35(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_36(N, __VA_ARGS__))
#define PAIR_34(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_35(N, __VA_ARGS__))
#define PAIR_33(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_34(N, __VA_ARGS__))
#define PAIR_32(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_33(N, __VA_ARGS__))
#define PAIR_31(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_32(N, __VA_ARGS__))
#define PAIR_30(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_31(N, __VA_ARGS__))
#define PAIR_29(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_30(N, __VA_ARGS__))
#define PAIR_28(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_29(N, __VA_ARGS__))
#define PAIR_27(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_28(N, __VA_ARGS__))
#define PAIR_26(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_27(N, __VA_ARGS__))
#define PAIR_25(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_26(N, __VA_ARGS__))
#define PAIR_24(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_25(N, __VA_ARGS__))
#define PAIR_23(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_24(N, __VA_ARGS__))
#define PAIR_22(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_23(N, __VA_ARGS__))
#define PAIR_21(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_22(N, __VA_ARGS__))
#define PAIR_20(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_21(N, __VA_ARGS__))
#define PAIR_19(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_20(N, __VA_ARGS__))
#define PAIR_18(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_19(N, __VA_ARGS__))
#define PAIR_17(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_18(N, __VA_ARGS__))
#define PAIR_16(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_17(N, __VA_ARGS__))
#define PAIR_15(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_16(N, __VA_ARGS__))
#define PAIR_14(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_15(N, __VA_ARGS__))
#define PAIR_13(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_14(N, __VA_ARGS__))
#define PAIR_12(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_13(N, __VA_ARGS__))
#define PAIR_11(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_12(N, __VA_ARGS__))
#define PAIR_10(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_11(N, __VA_ARGS__))
#define PAIR_9(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_10(N, __VA_ARGS__))
#define PAIR_8(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_9(N, __VA_ARGS__))
#define PAIR_7(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_8(N, __VA_ARGS__))
#define PAIR_6(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_7(N, __VA_ARGS__))
#define PAIR_5(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_6(N, __VA_ARGS__))
#define PAIR_4(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_5(N, __VA_ARGS__))
#define PAIR_3(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_4(N, __VA_ARGS__))
#define PAIR_2(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_3(N, __VA_ARGS__))
#define PAIR_1(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_2(N, __VA_ARGS__))
#define PAIR(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_1(N, __VA_ARGS__))

#define ENUM(NAME, ...)                                                                            \
    enum class NAME : u32 { __VA_ARGS__ };                                                         \
    template <>                                                                                    \
    inline std::vector<std::pair<std::string, NAME>> EnumMetadata<NAME>::Canonicalizations() {     \
        return {PAIR(NAME, __VA_ARGS__)};                                                          \
    }                                                                                              \
    template <>                                                                                    \
    inline u32 EnumMetadata<NAME>::Index() {                                                       \
        return __COUNTER__;                                                                        \
    }

// AudioEngine must be specified discretely due to having existing but slightly different
// canonicalizations
// TODO (lat9nq): Remove explicit definition of AudioEngine/sink_id
enum class AudioEngine : u32 {
    Auto,
    Cubeb,
    Sdl2,
    OpenAL,
    Null,
    Oboe,
};

template <>
inline std::vector<std::pair<std::string, AudioEngine>>
EnumMetadata<AudioEngine>::Canonicalizations() {
    return {
        {"auto", AudioEngine::Auto}, {"cubeb", AudioEngine::Cubeb}, {"sdl2", AudioEngine::Sdl2},
        {"openal", AudioEngine::OpenAL}, {"null", AudioEngine::Null}, {"oboe", AudioEngine::Oboe},
    };
}

template <>
inline u32 EnumMetadata<AudioEngine>::Index() {
    // This is just a sufficiently large number that is more than the number of other enums declared
    // here
    return 100;
}

enum class AudioMode : u32 {
    Mono = 0,
    Stereo = 1,
    Surround = 2,
};

template <>
inline std::vector<std::pair<std::string, AudioMode>>
EnumMetadata<AudioMode>::Canonicalizations() {
    return {
        {"Mono", AudioMode::Mono},
        {"Stereo", AudioMode::Stereo},
        {"Surround", AudioMode::Surround},
    };
}

template <>
inline u32 EnumMetadata<AudioMode>::Index() {
    return 0;
}

enum class Language : u32 {
    Japanese = 0,
    EnglishAmerican = 1,
    French = 2,
    German = 3,
    Italian = 4,
    Spanish = 5,
    Chinese = 6,
    Korean = 7,
    Dutch = 8,
    Portuguese = 9,
    Russian = 10,
    Taiwanese = 11,
    EnglishBritish = 12,
    FrenchCanadian = 13,
    SpanishLatin = 14,
    ChineseSimplified = 15,
    ChineseTraditional = 16,
    PortugueseBrazilian = 17,
};

template <>
inline std::vector<std::pair<std::string, Language>>
EnumMetadata<Language>::Canonicalizations() {
    return {
        {"Japanese", Language::Japanese},
        {"EnglishAmerican", Language::EnglishAmerican},
        {"French", Language::French},
        {"German", Language::German},
        {"Italian", Language::Italian},
        {"Spanish", Language::Spanish},
        {"Chinese", Language::Chinese},
        {"Korean", Language::Korean},
        {"Dutch", Language::Dutch},
        {"Portuguese", Language::Portuguese},
        {"Russian", Language::Russian},
        {"Taiwanese", Language::Taiwanese},
        {"EnglishBritish", Language::EnglishBritish},
        {"FrenchCanadian", Language::FrenchCanadian},
        {"SpanishLatin", Language::SpanishLatin},
        {"ChineseSimplified", Language::ChineseSimplified},
        {"ChineseTraditional", Language::ChineseTraditional},
        {"PortugueseBrazilian", Language::PortugueseBrazilian},
    };
}

template <>
inline u32 EnumMetadata<Language>::Index() {
    return 1;
}

enum class Region : u32 {
    Japan = 0,
    Usa = 1,
    Europe = 2,
    Australia = 3,
    China = 4,
    Korea = 5,
    Taiwan = 6,
};

template <>
inline std::vector<std::pair<std::string, Region>>
EnumMetadata<Region>::Canonicalizations() {
    return {
        {"Japan", Region::Japan},
        {"Usa", Region::Usa},
        {"Europe", Region::Europe},
        {"Australia", Region::Australia},
        {"China", Region::China},
        {"Korea", Region::Korea},
        {"Taiwan", Region::Taiwan},
    };
}

template <>
inline u32 EnumMetadata<Region>::Index() {
    return 2;
}

enum class TimeZone : u32 {
    Auto = 0,
    Default = 1,
    Cet = 2,
    Cst6Cdt = 3,
    Cuba = 4,
    Eet = 5,
    Egypt = 6,
    Eire = 7,
    Est = 8,
    Est5Edt = 9,
    Gb = 10,
    GbEire = 11,
    Gmt = 12,
    GmtPlusZero = 13,
    GmtMinusZero = 14,
    GmtZero = 15,
    Greenwich = 16,
    Hongkong = 17,
    Hst = 18,
    Iceland = 19,
    Iran = 20,
    Israel = 21,
    Jamaica = 22,
    Japan = 23,
    Kwajalein = 24,
    Libya = 25,
    Met = 26,
    Mst = 27,
    Mst7Mdt = 28,
    Navajo = 29,
    Nz = 30,
    NzChat = 31,
    Poland = 32,
    Portugal = 33,
    Prc = 34,
    Pst8Pdt = 35,
    Roc = 36,
    Rok = 37,
    Singapore = 38,
    Turkey = 39,
    Uct = 40,
    Universal = 41,
    Utc = 42,
    WSu = 43,
    Wet = 44,
    Zulu = 45,
};

template <>
inline std::vector<std::pair<std::string, TimeZone>>
EnumMetadata<TimeZone>::Canonicalizations() {
    return {
        {"Auto", TimeZone::Auto},
        {"Default", TimeZone::Default},
        {"Cet", TimeZone::Cet},
        {"Cst6Cdt", TimeZone::Cst6Cdt},
        {"Cuba", TimeZone::Cuba},
        {"Eet", TimeZone::Eet},
        {"Egypt", TimeZone::Egypt},
        {"Eire", TimeZone::Eire},
        {"Est", TimeZone::Est},
        {"Est5Edt", TimeZone::Est5Edt},
        {"Gb", TimeZone::Gb},
        {"GbEire", TimeZone::GbEire},
        {"Gmt", TimeZone::Gmt},
        {"GmtPlusZero", TimeZone::GmtPlusZero},
        {"GmtMinusZero", TimeZone::GmtMinusZero},
        {"GmtZero", TimeZone::GmtZero},
        {"Greenwich", TimeZone::Greenwich},
        {"Hongkong", TimeZone::Hongkong},
        {"Hst", TimeZone::Hst},
        {"Iceland", TimeZone::Iceland},
        {"Iran", TimeZone::Iran},
        {"Israel", TimeZone::Israel},
        {"Jamaica", TimeZone::Jamaica},
        {"Japan", TimeZone::Japan},
        {"Kwajalein", TimeZone::Kwajalein},
        {"Libya", TimeZone::Libya},
        {"Met", TimeZone::Met},
        {"Mst", TimeZone::Mst},
        {"Mst7Mdt", TimeZone::Mst7Mdt},
        {"Navajo", TimeZone::Navajo},
        {"Nz", TimeZone::Nz},
        {"NzChat", TimeZone::NzChat},
        {"Poland", TimeZone::Poland},
        {"Portugal", TimeZone::Portugal},
        {"Prc", TimeZone::Prc},
        {"Pst8Pdt", TimeZone::Pst8Pdt},
        {"Roc", TimeZone::Roc},
        {"Rok", TimeZone::Rok},
        {"Singapore", TimeZone::Singapore},
        {"Turkey", TimeZone::Turkey},
        {"Uct", TimeZone::Uct},
        {"Universal", TimeZone::Universal},
        {"Utc", TimeZone::Utc},
        {"WSu", TimeZone::WSu},
        {"Wet", TimeZone::Wet},
        {"Zulu", TimeZone::Zulu},
    };
}

template <>
inline u32 EnumMetadata<TimeZone>::Index() {
    return 3;
}

enum class AnisotropyMode : u32 {
    Automatic = 0,
    Default = 1,
    X2 = 2,
    X4 = 3,
    X8 = 4,
    X16 = 5,
};

template <>
inline std::vector<std::pair<std::string, AnisotropyMode>>
EnumMetadata<AnisotropyMode>::Canonicalizations() {
    return {
        {"Automatic", AnisotropyMode::Automatic},
        {"Default", AnisotropyMode::Default},
        {"X2", AnisotropyMode::X2},
        {"X4", AnisotropyMode::X4},
        {"X8", AnisotropyMode::X8},
        {"X16", AnisotropyMode::X16},
    };
}

template <>
inline u32 EnumMetadata<AnisotropyMode>::Index() {
    return 4;
}

enum class AstcDecodeMode : u32 {
    Cpu = 0,
    Gpu = 1,
    CpuAsynchronous = 2,
};

template <>
inline std::vector<std::pair<std::string, AstcDecodeMode>>
EnumMetadata<AstcDecodeMode>::Canonicalizations() {
    return {
        {"Cpu", AstcDecodeMode::Cpu},
        {"Gpu", AstcDecodeMode::Gpu},
        {"CpuAsynchronous", AstcDecodeMode::CpuAsynchronous},
    };
}

template <>
inline u32 EnumMetadata<AstcDecodeMode>::Index() {
    return 5;
}

enum class AstcRecompression : u32 {
    Uncompressed = 0,
    Bc1 = 1,
    Bc3 = 2,
};

template <>
inline std::vector<std::pair<std::string, AstcRecompression>>
EnumMetadata<AstcRecompression>::Canonicalizations() {
    return {
        {"Uncompressed", AstcRecompression::Uncompressed},
        {"Bc1", AstcRecompression::Bc1},
        {"Bc3", AstcRecompression::Bc3},
    };
}

template <>
inline u32 EnumMetadata<AstcRecompression>::Index() {
    return 6;
}

enum class VSyncMode : u32 {
    Immediate = 0,
    Mailbox = 1,
    Fifo = 2,
    FifoRelaxed = 3,
};

template <>
inline std::vector<std::pair<std::string, VSyncMode>>
EnumMetadata<VSyncMode>::Canonicalizations() {
    return {
        {"Immediate", VSyncMode::Immediate},
        {"Mailbox", VSyncMode::Mailbox},
        {"Fifo", VSyncMode::Fifo},
        {"FifoRelaxed", VSyncMode::FifoRelaxed},
    };
}

template <>
inline u32 EnumMetadata<VSyncMode>::Index() {
    return 7;
}

enum class VramUsageMode : u32 {
    Conservative = 0,
    Aggressive = 1,
    HighEnd = 2,
    Insane = 3,
};

template <>
inline std::vector<std::pair<std::string, VramUsageMode>>
EnumMetadata<VramUsageMode>::Canonicalizations() {
    return {
        {"Conservative", VramUsageMode::Conservative},
        {"Aggressive", VramUsageMode::Aggressive},
        {"HighEnd", VramUsageMode::HighEnd},
        {"Insane", VramUsageMode::Insane},
    };
}

template <>
inline u32 EnumMetadata<VramUsageMode>::Index() {
    return 8;
}

enum class RendererBackend : u32 {
    OpenGL = 0,
    Vulkan = 1,
    Null = 2,
};

template <>
inline std::vector<std::pair<std::string, RendererBackend>>
EnumMetadata<RendererBackend>::Canonicalizations() {
    return {
        {"OpenGL", RendererBackend::OpenGL},
        {"Vulkan", RendererBackend::Vulkan},
        {"Null", RendererBackend::Null},
    };
}

template <>
inline u32 EnumMetadata<RendererBackend>::Index() {
    return 9;
}

enum class ShaderBackend : u32 {
    Glsl = 0,
    Glasm = 1,
    SpirV = 2,
};

template <>
inline std::vector<std::pair<std::string, ShaderBackend>>
EnumMetadata<ShaderBackend>::Canonicalizations() {
    return {
        {"Glsl", ShaderBackend::Glsl},
        {"Glasm", ShaderBackend::Glasm},
        {"SpirV", ShaderBackend::SpirV},
    };
}

template <>
inline u32 EnumMetadata<ShaderBackend>::Index() {
    return 10;
}

enum class GpuAccuracy : u32 {
    Low = 0,
    Normal = 1,
    High = 2,
    Extreme = 3,
};

template <>
inline std::vector<std::pair<std::string, GpuAccuracy>>
EnumMetadata<GpuAccuracy>::Canonicalizations() {
    return {
        {"Low", GpuAccuracy::Low},
        {"Normal", GpuAccuracy::Normal},
        {"High", GpuAccuracy::High},
        {"Extreme", GpuAccuracy::Extreme},
    };
}

template <>
inline u32 EnumMetadata<GpuAccuracy>::Index() {
    return 11;
}

enum class CpuBackend : u32 {
    Dynarmic = 0,
    Nce = 1,
};

template <>
inline std::vector<std::pair<std::string, CpuBackend>>
EnumMetadata<CpuBackend>::Canonicalizations() {
    return {
        {"Dynarmic", CpuBackend::Dynarmic},
        {"Nce", CpuBackend::Nce},
    };
}

template <>
inline u32 EnumMetadata<CpuBackend>::Index() {
    return 12;
}

enum class CpuAccuracy : u32 {
    Auto = 0,
    Accurate = 1,
    Unsafe = 2,
    Paranoid = 3,
};

template <>
inline std::vector<std::pair<std::string, CpuAccuracy>>
EnumMetadata<CpuAccuracy>::Canonicalizations() {
    return {
        {"Auto", CpuAccuracy::Auto},
        {"Accurate", CpuAccuracy::Accurate},
        {"Unsafe", CpuAccuracy::Unsafe},
        {"Paranoid", CpuAccuracy::Paranoid},
    };
}

template <>
inline u32 EnumMetadata<CpuAccuracy>::Index() {
    return 13;
}

enum class MemoryLayout : u32 {
    Memory_4Gb = 0,
    Memory_6Gb = 1,
    Memory_8Gb = 2,
    Memory_10Gb = 3,
    Memory_12Gb = 4,
    Memory_14Gb = 5,
    Memory_16Gb = 6,
};

template <>
inline std::vector<std::pair<std::string, MemoryLayout>>
EnumMetadata<MemoryLayout>::Canonicalizations() {
    return {
        {"Memory_4Gb", MemoryLayout::Memory_4Gb},
        {"Memory_6Gb", MemoryLayout::Memory_6Gb},
        {"Memory_8Gb", MemoryLayout::Memory_8Gb},
        {"Memory_10Gb", MemoryLayout::Memory_10Gb},
        {"Memory_12Gb", MemoryLayout::Memory_12Gb},
        {"Memory_14Gb", MemoryLayout::Memory_14Gb},
        {"Memory_16Gb", MemoryLayout::Memory_16Gb},
    };
}

template <>
inline u32 EnumMetadata<MemoryLayout>::Index() {
    return 14;
}

enum class ConfirmStop : u32 {
    Ask_Always = 0,
    Ask_Based_On_Game = 1,
    Ask_Never = 2,
};

template <>
inline std::vector<std::pair<std::string, ConfirmStop>>
EnumMetadata<ConfirmStop>::Canonicalizations() {
    return {
        {"Ask_Always", ConfirmStop::Ask_Always},
        {"Ask_Based_On_Game", ConfirmStop::Ask_Based_On_Game},
        {"Ask_Never", ConfirmStop::Ask_Never},
    };
}

template <>
inline u32 EnumMetadata<ConfirmStop>::Index() {
    return 15;
}

enum class FullscreenMode : u32 {
    Borderless = 0,
    Exclusive = 1,
};

template <>
inline std::vector<std::pair<std::string, FullscreenMode>>
EnumMetadata<FullscreenMode>::Canonicalizations() {
    return {
        {"Borderless", FullscreenMode::Borderless},
        {"Exclusive", FullscreenMode::Exclusive},
    };
}

template <>
inline u32 EnumMetadata<FullscreenMode>::Index() {
    return 16;
}

enum class NvdecEmulation : u32 {
    Off = 0,
    Cpu = 1,
    Gpu = 2,
};

template <>
inline std::vector<std::pair<std::string, NvdecEmulation>>
EnumMetadata<NvdecEmulation>::Canonicalizations() {
    return {
        {"Off", NvdecEmulation::Off},
        {"Cpu", NvdecEmulation::Cpu},
        {"Gpu", NvdecEmulation::Gpu},
    };
}

template <>
inline u32 EnumMetadata<NvdecEmulation>::Index() {
    return 17;
}

// ResolutionSetup must be specified discretely due to needing signed values
enum class ResolutionSetup : s32 {
    Res1_4X = -1,
    Res1_2X = 0,
    Res3_4X = 1,
    Res1X = 2,
    Res3_2X = 3,
    Res2X = 4,
    Res3X = 5,
    Res4X = 6,
    Res5X = 7,
    Res6X = 8,
    Res7X = 9,
    Res8X = 10,
};

template <>
inline std::vector<std::pair<std::string, ResolutionSetup>>
EnumMetadata<ResolutionSetup>::Canonicalizations() {
    return {
        {"Res1_4X", ResolutionSetup::Res1_4X},
        {"Res1_2X", ResolutionSetup::Res1_2X},
        {"Res3_4X", ResolutionSetup::Res3_4X},
        {"Res1X", ResolutionSetup::Res1X},
        {"Res3_2X", ResolutionSetup::Res3_2X},
        {"Res2X", ResolutionSetup::Res2X},
        {"Res3X", ResolutionSetup::Res3X},
        {"Res4X", ResolutionSetup::Res4X},
        {"Res5X", ResolutionSetup::Res5X},
        {"Res6X", ResolutionSetup::Res6X},
        {"Res7X", ResolutionSetup::Res7X},
        {"Res8X", ResolutionSetup::Res8X},
    };
}

template <>
inline u32 EnumMetadata<ResolutionSetup>::Index() {
    return 101;
}

enum class ScalingFilter : u32 {
    NearestNeighbor = 0,
    Bilinear = 1,
    Bicubic = 2,
    Gaussian = 3,
    ScaleForce = 4,
    ScaleFx = 5,
    Lanczos = 6,
    Fsr = 7,
    Fsr2 = 8,
    MaxEnum = 9,
};

template <>
inline std::vector<std::pair<std::string, ScalingFilter>>
EnumMetadata<ScalingFilter>::Canonicalizations() {
    return {
        {"NearestNeighbor", ScalingFilter::NearestNeighbor},
        {"Bilinear", ScalingFilter::Bilinear},
        {"Bicubic", ScalingFilter::Bicubic},
        {"Gaussian", ScalingFilter::Gaussian},
        {"ScaleForce", ScalingFilter::ScaleForce},
        {"ScaleFx", ScalingFilter::ScaleFx},
        {"Lanczos", ScalingFilter::Lanczos},
        {"Fsr", ScalingFilter::Fsr},
        {"Fsr2", ScalingFilter::Fsr2},
        {"MaxEnum", ScalingFilter::MaxEnum},
    };
}

template <>
inline u32 EnumMetadata<ScalingFilter>::Index() {
    return 18;
}

enum class AntiAliasing : u32 {
    None = 0,
    Fxaa = 1,
    Smaa = 2,
    Taa = 3,
    MaxEnum = 4,
};

template <>
inline std::vector<std::pair<std::string, AntiAliasing>>
EnumMetadata<AntiAliasing>::Canonicalizations() {
    return {
        {"None", AntiAliasing::None},
        {"Fxaa", AntiAliasing::Fxaa},
        {"Smaa", AntiAliasing::Smaa},
        {"Taa", AntiAliasing::Taa},
        {"MaxEnum", AntiAliasing::MaxEnum},
    };
}

template <>
inline u32 EnumMetadata<AntiAliasing>::Index() {
    return 19;
}

enum class FSR2QualityMode : u32 {
    Quality = 0,
    Balanced = 1,
    Performance = 2,
    UltraPerformance = 3,
};

template <>
inline std::vector<std::pair<std::string, FSR2QualityMode>>
EnumMetadata<FSR2QualityMode>::Canonicalizations() {
    return {
        {"Quality", FSR2QualityMode::Quality},
        {"Balanced", FSR2QualityMode::Balanced},
        {"Performance", FSR2QualityMode::Performance},
        {"UltraPerformance", FSR2QualityMode::UltraPerformance},
    };
}

template <>
inline u32 EnumMetadata<FSR2QualityMode>::Index() {
    return 20;
}

enum class FrameSkipping : u32 {
    Disabled = 0,
    Enabled = 1,
    MaxEnum = 2,
};

template <>
inline std::vector<std::pair<std::string, FrameSkipping>>
EnumMetadata<FrameSkipping>::Canonicalizations() {
    return {
        {"Disabled", FrameSkipping::Disabled},
        {"Enabled", FrameSkipping::Enabled},
        {"MaxEnum", FrameSkipping::MaxEnum},
    };
}

template <>
inline u32 EnumMetadata<FrameSkipping>::Index() {
    return 21;
}

enum class FrameSkippingMode : u32 {
    Adaptive = 0,
    Fixed = 1,
    MaxEnum = 2,
};

template <>
inline std::vector<std::pair<std::string, FrameSkippingMode>>
EnumMetadata<FrameSkippingMode>::Canonicalizations() {
    return {
        {"Adaptive", FrameSkippingMode::Adaptive},
        {"Fixed", FrameSkippingMode::Fixed},
        {"MaxEnum", FrameSkippingMode::MaxEnum},
    };
}

template <>
inline u32 EnumMetadata<FrameSkippingMode>::Index() {
    return 22;
}

enum class AspectRatio : u32 {
    R16_9 = 0,
    R4_3 = 1,
    R21_9 = 2,
    R16_10 = 3,
    R32_9 = 4,
    Stretch = 5,
};

template <>
inline std::vector<std::pair<std::string, AspectRatio>>
EnumMetadata<AspectRatio>::Canonicalizations() {
    return {
        {"R16_9", AspectRatio::R16_9},
        {"R4_3", AspectRatio::R4_3},
        {"R21_9", AspectRatio::R21_9},
        {"R16_10", AspectRatio::R16_10},
        {"R32_9", AspectRatio::R32_9},
        {"Stretch", AspectRatio::Stretch},
    };
}

template <>
inline u32 EnumMetadata<AspectRatio>::Index() {
    return 23;
}

enum class ConsoleMode : u32 {
    Handheld = 0,
    Docked = 1,
};

template <>
inline std::vector<std::pair<std::string, ConsoleMode>>
EnumMetadata<ConsoleMode>::Canonicalizations() {
    return {
        {"Handheld", ConsoleMode::Handheld},
        {"Docked", ConsoleMode::Docked},
    };
}

template <>
inline u32 EnumMetadata<ConsoleMode>::Index() {
    return 24;
}

enum class AppletMode : u32 {
    HLE = 0,
    LLE = 1,
};

template <>
inline std::vector<std::pair<std::string, AppletMode>>
EnumMetadata<AppletMode>::Canonicalizations() {
    return {
        {"HLE", AppletMode::HLE},
        {"LLE", AppletMode::LLE},
    };
}

template <>
inline u32 EnumMetadata<AppletMode>::Index() {
    return 25;
}

enum class ExtendedDynamicState : u32 {
    Disabled = 0,
    EDS1 = 1,
    EDS2 = 2,
    EDS3 = 3,
};

template <>
inline std::vector<std::pair<std::string, ExtendedDynamicState>>
EnumMetadata<ExtendedDynamicState>::Canonicalizations() {
    return {
        {"Disabled", ExtendedDynamicState::Disabled},
        {"EDS1", ExtendedDynamicState::EDS1},
        {"EDS2", ExtendedDynamicState::EDS2},
        {"EDS3", ExtendedDynamicState::EDS3},
    };
}

template <>
inline u32 EnumMetadata<ExtendedDynamicState>::Index() {
    return 26;
}

template <typename Type>
inline std::string CanonicalizeEnum(Type id) {
    const auto group = EnumMetadata<Type>::Canonicalizations();
    for (auto& [name, value] : group) {
        if (value == id) {
            return name;
        }
    }
    return "unknown";
}

template <typename Type>
inline Type ToEnum(const std::string& canonicalization) {
    const auto group = EnumMetadata<Type>::Canonicalizations();
    for (auto& [name, value] : group) {
        if (name == canonicalization) {
            return value;
        }
    }
    return {};
}
} // namespace Settings

#undef ENUM
#undef PAIR
#undef PAIR_1
#undef PAIR_2
#undef PAIR_3
#undef PAIR_4
#undef PAIR_5
#undef PAIR_6
#undef PAIR_7
#undef PAIR_8
#undef PAIR_9
#undef PAIR_10
#undef PAIR_12
#undef PAIR_13
#undef PAIR_14
#undef PAIR_15
#undef PAIR_16
#undef PAIR_17
#undef PAIR_18
#undef PAIR_19
#undef PAIR_20
#undef PAIR_22
#undef PAIR_23
#undef PAIR_24
#undef PAIR_25
#undef PAIR_26
#undef PAIR_27
#undef PAIR_28
#undef PAIR_29
#undef PAIR_30
#undef PAIR_32
#undef PAIR_33
#undef PAIR_34
#undef PAIR_35
#undef PAIR_36
#undef PAIR_37
#undef PAIR_38
#undef PAIR_39
#undef PAIR_40
#undef PAIR_42
#undef PAIR_43
#undef PAIR_44
#undef PAIR_45
