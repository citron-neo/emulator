// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Based on dkms-hid-nintendo implementation, CTCaer joycon toolkit and dekuNukem reverse
// engineering https://github.com/nicman23/dkms-hid-nintendo/blob/master/src/hid-nintendo.c
// https://github.com/CTCaer/jc_toolkit
// https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering

#pragma once

#include <vector>

#include "input_common/helpers/joycon_protocol/common_protocol.h"
#include "input_common/helpers/joycon_protocol/joycon_types.h"

namespace Common::Input {
enum class DriverResult;
}

namespace InputCommon::Joycon {

class NfcProtocol final : private JoyconCommonProtocol {
public:
    explicit NfcProtocol(std::shared_ptr<JoyconHandle> handle);

    Common::Input::DriverResult EnableNfc();

    Common::Input::DriverResult DisableNfc();

    Common::Input::DriverResult StartNFCPollingMode();

    Common::Input::DriverResult StopNFCPollingMode();

    Common::Input::DriverResult GetTagInfo(Joycon::TagInfo& tag_info);

    Common::Input::DriverResult ReadAmiibo(std::vector<u8>& data);

    Common::Input::DriverResult WriteAmiibo(std::span<const u8> data);

    Common::Input::DriverResult ReadMifare(std::span<const MifareReadChunk> read_request,
                                           std::span<MifareReadData> out_data);

    Common::Input::DriverResult WriteMifare(std::span<const MifareWriteChunk> write_request);

    bool HasAmiibo();

    bool IsEnabled() const;

    bool IsPolling() const;

private:
    // Number of times the function will be delayed until it outputs valid data
    static constexpr std::size_t AMIIBO_UPDATE_DELAY = 15;

    struct TagFoundData {
        u8 type;
        u8 uuid_size;
        TagUUID uuid;
    };

    Common::Input::DriverResult WaitUntilNfcIs(NFCStatus status);

    Common::Input::DriverResult IsTagInRange(TagFoundData& data, std::size_t timeout_limit = 1);

    Common::Input::DriverResult GetAmiiboData(std::vector<u8>& data);

    Common::Input::DriverResult WriteAmiiboData(const TagUUID& tag_uuid, std::span<const u8> data);

    Common::Input::DriverResult GetMifareData(const MifareUUID& tag_uuid,
                                              std::span<const MifareReadChunk> read_request,
                                              std::span<MifareReadData> out_data);

    Common::Input::DriverResult WriteMifareData(const MifareUUID& tag_uuid,
                                                std::span<const MifareWriteChunk> write_request);

    Common::Input::DriverResult SendStartPollingRequest(MCUCommandResponse& output,
                                                        bool is_second_attempt = false);

    Common::Input::DriverResult SendStopPollingRequest(MCUCommandResponse& output);

    Common::Input::DriverResult SendNextPackageRequest(MCUCommandResponse& output, u8 packet_id);

    Common::Input::DriverResult SendReadAmiiboRequest(MCUCommandResponse& output,
                                                      NFCPages ntag_pages);

    Common::Input::DriverResult SendWriteAmiiboRequest(MCUCommandResponse& output,
                                                       const TagUUID& tag_uuid);

    Common::Input::DriverResult SendWriteDataAmiiboRequest(MCUCommandResponse& output, u8 block_id,
                                                           bool is_last_packet,
                                                           std::span<const u8> data);

    Common::Input::DriverResult SendReadDataMifareRequest(MCUCommandResponse& output, u8 block_id,
                                                          bool is_last_packet,
                                                          std::span<const u8> data);

    std::vector<u8> SerializeWritePackage(const NFCWritePackage& package) const;

    std::vector<u8> SerializeMifareReadPackage(const MifareReadPackage& package) const;

    std::vector<u8> SerializeMifareWritePackage(const MifareWritePackage& package) const;

    NFCWritePackage MakeAmiiboWritePackage(const TagUUID& tag_uuid, std::span<const u8> data) const;

    NFCDataChunk MakeAmiiboChunk(u8 page, u8 size, std::span<const u8> data) const;

    MifareReadPackage MakeMifareReadPackage(const MifareUUID& tag_uuid,
                                            std::span<const MifareReadChunk> read_request) const;

    MifareWritePackage MakeMifareWritePackage(const MifareUUID& tag_uuid,
                                              std::span<const MifareWriteChunk> read_request) const;

    NFCReadBlockCommand GetReadBlockCommand(NFCPages pages) const;

    TagUUID GetTagUUID(std::span<const u8> data) const;

    bool is_enabled{};
    bool is_polling{};
    std::size_t update_counter{};
};

} // namespace InputCommon::Joycon
