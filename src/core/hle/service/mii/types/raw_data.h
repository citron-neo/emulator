// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "core/hle/service/mii/mii_types.h"

namespace Service::Mii::RawData {

struct RandomMiiValues {
    std::array<u8, 188> values{};
};
static_assert(sizeof(RandomMiiValues) == 0xbc, "RandomMiiValues has incorrect size.");

struct RandomMiiData4 {
    u32 gender{};
    u32 age{};
    u32 race{};
    u32 values_count{};
    std::array<u32, 47> values{};
};
static_assert(sizeof(RandomMiiData4) == 0xcc, "RandomMiiData4 has incorrect size.");

struct RandomMiiData3 {
    u32 arg_1;
    u32 arg_2;
    u32 values_count;
    std::array<u32, 47> values{};
};
static_assert(sizeof(RandomMiiData3) == 0xc8, "RandomMiiData3 has incorrect size.");

struct RandomMiiData2 {
    u32 arg_1;
    u32 values_count;
    std::array<u32, 47> values{};
};
static_assert(sizeof(RandomMiiData2) == 0xc4, "RandomMiiData2 has incorrect size.");

extern const std::array<Service::Mii::DefaultMii, 2> BaseMii;
extern const std::array<Service::Mii::DefaultMii, 6> DefaultMii;

extern const std::array<u8, 62> EyeRotateLookup;
extern const std::array<u8, 24> EyebrowRotateLookup;

extern const std::array<RandomMiiData4, 18> RandomMiiFaceline;
extern const std::array<RandomMiiData3, 6> RandomMiiFacelineColor;
extern const std::array<RandomMiiData4, 18> RandomMiiFacelineWrinkle;
extern const std::array<RandomMiiData4, 18> RandomMiiFacelineMakeup;
extern const std::array<RandomMiiData4, 18> RandomMiiHairType;
extern const std::array<RandomMiiData3, 9> RandomMiiHairColor;
extern const std::array<RandomMiiData4, 18> RandomMiiEyeType;
extern const std::array<RandomMiiData2, 3> RandomMiiEyeColor;
extern const std::array<RandomMiiData4, 18> RandomMiiEyebrowType;
extern const std::array<RandomMiiData4, 18> RandomMiiNoseType;
extern const std::array<RandomMiiData4, 18> RandomMiiMouthType;
extern const std::array<RandomMiiData2, 3> RandomMiiGlassType;

u8 FromVer3GetFacelineColor(u8 color);
u8 FromVer3GetHairColor(u8 color);
u8 FromVer3GetEyeColor(u8 color);
u8 FromVer3GetMouthlineColor(u8 color);
u8 FromVer3GetGlassColor(u8 color);
u8 FromVer3GetGlassType(u8 type);

FacelineColor GetFacelineColorFromVer3(u32 color);
CommonColor GetHairColorFromVer3(u32 color);
CommonColor GetEyeColorFromVer3(u32 color);
CommonColor GetMouthColorFromVer3(u32 color);
CommonColor GetGlassColorFromVer3(u32 color);

} // namespace Service::Mii::RawData
