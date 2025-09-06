// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/hex_util.h"
#include "core/file_sys/savedata_factory.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_save_data_info_reader.h"
#include "core/hle/service/filesystem/save_data_controller.h"

namespace Service::FileSystem {

ISaveDataInfoReader::ISaveDataInfoReader(Core::System& system_,
                                         std::shared_ptr<SaveDataController> save_data_controller_,
                                         FileSys::SaveDataSpaceId space)
    : ServiceFramework{system_, "ISaveDataInfoReader"}, save_data_controller{
                                                            save_data_controller_} {
    static const FunctionInfo functions[] = {
        {0, D<&ISaveDataInfoReader::ReadSaveDataInfo>, "ReadSaveDataInfo"},
    };
    RegisterHandlers(functions);

    FindAllSaves(space);
}

ISaveDataInfoReader::~ISaveDataInfoReader() = default;

static u64 stoull_be(std::string_view str) {
    if (str.size() != 16) {
        return 0;
    }

    const auto bytes = Common::HexStringToArray<0x8>(str);
    u64 out{};
    std::memcpy(&out, bytes.data(), sizeof(u64));

    return Common::swap64(out);
}

Result ISaveDataInfoReader::ReadSaveDataInfo(
    Out<u64> out_count, OutArray<SaveDataInfo, BufferAttr_HipcMapAlias> out_entries) {
    LOG_DEBUG(Service_FS, "called");

    // Calculate how many entries we can fit in the output buffer
    const u64 count_entries = out_entries.size();

    // Cap at total number of entries.
    const u64 actual_entries = std::min(count_entries, info.size() - next_entry_index);

    // Determine data start and end
    const auto* begin = reinterpret_cast<u8*>(info.data() + next_entry_index);
    const auto* end = reinterpret_cast<u8*>(info.data() + next_entry_index + actual_entries);
    const auto range_size = static_cast<std::size_t>(std::distance(begin, end));

    next_entry_index += actual_entries;

    // Write the data to memory
    std::memcpy(out_entries.data(), begin, range_size);
    *out_count = actual_entries;

    R_SUCCEED();
}

void ISaveDataInfoReader::FindAllSaves(FileSys::SaveDataSpaceId space) {
    FileSys::VirtualDir save_root{};
    const auto result = save_data_controller->OpenSaveDataSpace(&save_root, space);

    if (result != ResultSuccess || save_root == nullptr) {
        LOG_ERROR(Service_FS, "The save root for the space_id={:02X} was invalid!", space);
        return;
    }

    for (const auto& type : save_root->GetSubdirectories()) {
        if (type->GetName() == "save") {
            FindNormalSaves(space, type);
        } else if (space == FileSys::SaveDataSpaceId::Temporary) {
            FindTemporaryStorageSaves(space, type);
        }
    }
}

void ISaveDataInfoReader::FindNormalSaves(FileSys::SaveDataSpaceId space,
                                          const FileSys::VirtualDir& type) {
    for (const auto& save_id : type->GetSubdirectories()) {
        for (const auto& user_id : save_id->GetSubdirectories()) {
            // Skip non user id subdirectories
            if (user_id->GetName().size() != 0x20) {
                continue;
            }

            const auto save_id_numeric = stoull_be(save_id->GetName());
            auto user_id_numeric = Common::HexStringToArray<0x10>(user_id->GetName());
            std::reverse(user_id_numeric.begin(), user_id_numeric.end());

            if (save_id_numeric != 0) {
                // System Save Data
                info.emplace_back(SaveDataInfo{
                    0,
                    space,
                    FileSys::SaveDataType::System,
                    {},
                    user_id_numeric,
                    save_id_numeric,
                    0,
                    user_id->GetSize(),
                    {},
                    {},
                });

                continue;
            }

            for (const auto& title_id : user_id->GetSubdirectories()) {
                const auto device = std::all_of(user_id_numeric.begin(), user_id_numeric.end(),
                                                [](u8 val) { return val == 0; });
                info.emplace_back(SaveDataInfo{
                    0,
                    space,
                    device ? FileSys::SaveDataType::Device : FileSys::SaveDataType::Account,
                    {},
                    user_id_numeric,
                    save_id_numeric,
                    stoull_be(title_id->GetName()),
                    title_id->GetSize(),
                    {},
                    {},
                });
            }
        }
    }
}

void ISaveDataInfoReader::FindTemporaryStorageSaves(FileSys::SaveDataSpaceId space,
                                                    const FileSys::VirtualDir& type) {
    for (const auto& user_id : type->GetSubdirectories()) {
        // Skip non user id subdirectories
        if (user_id->GetName().size() != 0x20) {
            continue;
        }
        for (const auto& title_id : user_id->GetSubdirectories()) {
            if (!title_id->GetFiles().empty() || !title_id->GetSubdirectories().empty()) {
                auto user_id_numeric = Common::HexStringToArray<0x10>(user_id->GetName());
                std::reverse(user_id_numeric.begin(), user_id_numeric.end());

                info.emplace_back(SaveDataInfo{
                    0,
                    space,
                    FileSys::SaveDataType::Temporary,
                    {},
                    user_id_numeric,
                    stoull_be(type->GetName()),
                    stoull_be(title_id->GetName()),
                    title_id->GetSize(),
                    {},
                    {},
                });
            }
        }
    }
}

} // namespace Service::FileSystem
