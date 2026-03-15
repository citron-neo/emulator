// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
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
        {"auto", AudioEngine::Auto},     {"cubeb", AudioEngine::Cubeb}, {"sdl2", AudioEngine::Sdl2},
        {"openal", AudioEngine::OpenAL}, {"null", AudioEngine::Null},   {"oboe", AudioEngine::Oboe},
    };
}

template <>
inline u32 EnumMetadata<AudioEngine>::Index() {
    // This is just a sufficiently large number that is more than the number of other enums declared
    // here
    return 100;
}

ENUM(AudioMode, Mono, Stereo, Surround);

ENUM(Language, Japanese, EnglishAmerican, French, German, Italian, Spanish, Chinese, Korean, Dutch,
     Portuguese, Russian, Taiwanese, EnglishBritish, FrenchCanadian, SpanishLatin,
     ChineseSimplified, ChineseTraditional, PortugueseBrazilian);

ENUM(Region, Japan, Usa, Europe, Australia, China, Korea, Taiwan);

ENUM(TimeZone, Auto, Default, Cet, Cst6Cdt, Cuba, Eet, Egypt, Eire, Est, Est5Edt, Gb, GbEire, Gmt,
     GmtPlusZero, GmtMinusZero, GmtZero, Greenwich, Hongkong, Hst, Iceland, Iran, Israel, Jamaica,
     Japan, Kwajalein, Libya, Met, Mst, Mst7Mdt, Navajo, Nz, NzChat, Poland, Portugal, Prc, Pst8Pdt,
     Roc, Rok, Singapore, Turkey, Uct, Universal, Utc, WSu, Wet, Zulu);

ENUM(AnisotropyMode, Automatic, Default, X2, X4, X8, X16);

ENUM(AstcDecodeMode, Cpu, Gpu, CpuAsynchronous);

ENUM(AstcRecompression, Uncompressed, Bc1, Bc3);

ENUM(VSyncMode, Immediate, Mailbox, Fifo, FifoRelaxed);

ENUM(VramUsageMode, Conservative, Aggressive, HighEnd, Insane);

ENUM(RendererBackend, Vulkan, Null);

ENUM(GpuAccuracy, Low, Normal, High, Extreme);

ENUM(CpuBackend, Dynarmic, Nce);

ENUM(CpuAccuracy, Auto, Accurate, Unsafe, Paranoid, UltraLow);

ENUM(MemoryLayout, Memory_4Gb, Memory_6Gb, Memory_8Gb, Memory_10Gb, Memory_12Gb, Memory_14Gb, Memory_16Gb);

ENUM(ConfirmStop, Ask_Always, Ask_Based_On_Game, Ask_Never);

ENUM(FullscreenMode, Borderless, Exclusive);

ENUM(NvdecEmulation, Off, Cpu, Gpu);

ENUM(ResolutionSetup, Res1_4X, Res1_2X, Res3_4X, Res1X, Res3_2X, Res2X, Res3X, Res4X, Res5X, Res6X, Res7X, Res8X, Res5_4X, Res7_4X);

ENUM(ScalingFilter, NearestNeighbor, Bilinear, Bicubic, Gaussian, ScaleForce, ScaleFx, Lanczos, Fsr, Fsr2, CRTEasyMode, CRTRoyale, Cas, MaxEnum);

ENUM(AntiAliasing, None, Fxaa, Smaa, Taa, MaxEnum);

ENUM(FSR2QualityMode, Quality, Balanced, Performance, UltraPerformance);

ENUM(FrameSkipping, Disabled, Enabled, MaxEnum);

ENUM(FrameSkippingMode, Adaptive, Fixed, MaxEnum);

ENUM(AspectRatio, R16_9, R4_3, R21_9, R16_10, R32_9, Stretch);

ENUM(ConsoleMode, Handheld, Docked);

ENUM(AppletMode, HLE, LLE);

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

// FIXED: VRAM leak prevention - GC aggressiveness levels
enum class GCAggressiveness : u32 {
    Off = 0,   // Disable automatic GC (not recommended)
    Light = 1, // Light GC - gentle eviction of old textures/buffers
};

template <>
inline std::vector<std::pair<std::string, GCAggressiveness>>
EnumMetadata<GCAggressiveness>::Canonicalizations() {
    return {
        {"Off", GCAggressiveness::Off},
        {"Light", GCAggressiveness::Light},
    };
}

template <>
inline u32 EnumMetadata<GCAggressiveness>::Index() {
    return 27;
}

// FIXED: Android Adreno 740 native ASTC eviction
// Controls texture cache eviction strategy on Android devices with native ASTC support
enum class AndroidAstcMode : u32 {
    Auto = 0,       // Auto-detect based on GPU capabilities (recommended)
    Native = 1,     // Force native ASTC - use compressed size for eviction
    Decompress = 2, // Force decompression - use decompressed size (PC-style eviction)
};

template <>
inline std::vector<std::pair<std::string, AndroidAstcMode>>
EnumMetadata<AndroidAstcMode>::Canonicalizations() {
    return {
        {"Auto", AndroidAstcMode::Auto},
        {"Native", AndroidAstcMode::Native},
        {"Decompress", AndroidAstcMode::Decompress},
    };
}

template <>
inline u32 EnumMetadata<AndroidAstcMode>::Index() {
    return 28;
}

enum class SpirvShaderOptimization : u32 {
    Off,
    Auto,
};

template <>
inline std::vector<std::pair<std::string, SpirvShaderOptimization>>
EnumMetadata<SpirvShaderOptimization>::Canonicalizations() {
    return {
        {"Off", SpirvShaderOptimization::Off},
        {"Auto", SpirvShaderOptimization::Auto},
    };
}

template <>
inline u32 EnumMetadata<SpirvShaderOptimization>::Index() {
    return 29;
}

enum class SpirvOptimizeMode : u32 {
    Never,
    Always,
    BestEffort,
};

template <>
inline std::vector<std::pair<std::string, SpirvOptimizeMode>>
EnumMetadata<SpirvOptimizeMode>::Canonicalizations() {
    return {
        {"Never", SpirvOptimizeMode::Never},
        {"Always", SpirvOptimizeMode::Always},
        {"BestEffort", SpirvOptimizeMode::BestEffort},
    };
}

template <>
inline u32 EnumMetadata<SpirvOptimizeMode>::Index() {
    return 30;
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
