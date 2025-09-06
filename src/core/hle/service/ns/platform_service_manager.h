// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/set/settings_types.h"

namespace Service {

namespace FileSystem {
class FileSystemController;
} // namespace FileSystem

namespace NS {

enum class FontArchives : u64 {
    Extension = 0x0100000000000810,
    Standard = 0x0100000000000811,
    Korean = 0x0100000000000812,
    ChineseTraditional = 0x0100000000000813,
    ChineseSimple = 0x0100000000000814,
};

enum class SharedFontType : u32 {
    JapanUSEuropeStandard = 0,
    ChineseSimplified = 1,
    ExtendedChineseSimplified = 2,
    ChineseTraditional = 3,
    KoreanHangul = 4,
    NintendoExtended = 5,
};

enum class LoadState : u32 {
    Loading = 0,
    Loaded = 1,
};

constexpr std::array<std::pair<FontArchives, const char*>, 7> SHARED_FONTS{
    std::make_pair(FontArchives::Standard, "nintendo_udsg-r_std_003.bfttf"),
    std::make_pair(FontArchives::ChineseSimple, "nintendo_udsg-r_org_zh-cn_003.bfttf"),
    std::make_pair(FontArchives::ChineseSimple, "nintendo_udsg-r_ext_zh-cn_003.bfttf"),
    std::make_pair(FontArchives::ChineseTraditional, "nintendo_udjxh-db_zh-tw_003.bfttf"),
    std::make_pair(FontArchives::Korean, "nintendo_udsg-r_ko_003.bfttf"),
    std::make_pair(FontArchives::Extension, "nintendo_ext_003.bfttf"),
    std::make_pair(FontArchives::Extension, "nintendo_ext2_003.bfttf"),
};

void DecryptSharedFontToTTF(const std::vector<u32>& input, std::vector<u8>& output);
void EncryptSharedFont(const std::vector<u32>& input, std::vector<u8>& output, std::size_t& offset);

class IPlatformServiceManager final : public ServiceFramework<IPlatformServiceManager> {
public:
    explicit IPlatformServiceManager(Core::System& system_, const char* service_name_);
    ~IPlatformServiceManager() override;

private:
    Result RequestLoad(SharedFontType type);
    Result GetLoadState(Out<LoadState> out_load_state, SharedFontType type);
    Result GetSize(Out<u32> out_size, SharedFontType type);
    Result GetSharedMemoryAddressOffset(Out<u32> out_shared_memory_offset, SharedFontType type);
    Result GetSharedMemoryNativeHandle(
        OutCopyHandle<Kernel::KSharedMemory> out_shared_memory_native_handle);
    Result GetSharedFontInOrderOfPriority(OutArray<u32, BufferAttr_HipcMapAlias> out_font_codes,
                                          OutArray<u32, BufferAttr_HipcMapAlias> out_font_offsets,
                                          OutArray<u32, BufferAttr_HipcMapAlias> out_font_sizes,
                                          Out<bool> out_fonts_are_loaded, Out<u32> out_font_count,
                                          Set::LanguageCode language_code);

    // [S2] Switch 2 functions
    Result Unknown2010(/* Unknown parameters */);
    Result Unknown2020(/* Unknown parameters */);
    Result OpenFont(/* Out<SharedPointer<IFontService>> out_font_service */);

    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace NS

} // namespace Service
