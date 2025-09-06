// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "common/common_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nfc/mifare_types.h"
#include "core/hle/service/nfc/nfc_types.h"
#include "core/hle/service/nfp/nfp_types.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Core {
class System;
} // namespace Core

namespace Core::HID {
class EmulatedController;
enum class ControllerTriggerType;
enum class NpadIdType : u32;
} // namespace Core::HID

namespace Service::NFC {
class NfcDevice {
public:
    NfcDevice(Core::HID::NpadIdType npad_id_, Core::System& system_,
              KernelHelpers::ServiceContext& service_context_,
              Kernel::KEvent* availability_change_event_);
    ~NfcDevice();

    void Initialize();
    void Finalize();

    Result StartDetection(NfcProtocol allowed_protocol);
    Result StopDetection();

    Result GetTagInfo(TagInfo& tag_info) const;

    Result ReadMifare(std::span<const MifareReadBlockParameter> parameters,
                      std::span<MifareReadBlockData> read_block_data) const;

    Result WriteMifare(std::span<const MifareWriteBlockParameter> parameters);

    Result SendCommandByPassThrough(const s64& timeout, std::span<const u8> command_data,
                                    std::span<u8> out_data);

    Result Mount(NFP::ModelType model_type, NFP::MountTarget mount_target);
    Result Unmount();

    Result Flush();
    Result FlushDebug();
    Result FlushWithBreak(NFP::BreakType break_type);
    Result Restore();

    Result GetCommonInfo(NFP::CommonInfo& common_info) const;
    Result GetModelInfo(NFP::ModelInfo& model_info) const;
    Result GetRegisterInfo(NFP::RegisterInfo& register_info) const;
    Result GetRegisterInfoPrivate(NFP::RegisterInfoPrivate& register_info) const;
    Result GetAdminInfo(NFP::AdminInfo& admin_info) const;

    Result DeleteRegisterInfo();
    Result SetRegisterInfoPrivate(const NFP::RegisterInfoPrivate& register_info);
    Result Format();

    Result OpenApplicationArea(u32 access_id);
    Result GetApplicationAreaId(u32& application_area_id) const;
    Result GetApplicationArea(std::span<u8> data) const;
    Result SetApplicationArea(std::span<const u8> data);
    Result CreateApplicationArea(u32 access_id, std::span<const u8> data);
    Result RecreateApplicationArea(u32 access_id, std::span<const u8> data);
    Result DeleteApplicationArea();
    Result ExistsApplicationArea(bool& has_application_area) const;

    Result GetAll(NFP::NfpData& data) const;
    Result SetAll(const NFP::NfpData& data);
    Result BreakTag(NFP::BreakType break_type);
    Result HasBackup(const UniqueSerialNumber& uid, std::size_t uuid_size) const;
    Result HasBackup(const NFP::TagUuid& tag_uid) const;
    Result ReadBackupData(const UniqueSerialNumber& uid, std::size_t uuid_size,
                          std::span<u8> data) const;
    Result ReadBackupData(const NFP::TagUuid& tag_uid, std::span<u8> data) const;
    Result WriteBackupData(const UniqueSerialNumber& uid, std::size_t uuid_size,
                           std::span<const u8> data);
    Result WriteBackupData(const NFP::TagUuid& tag_uid, std::span<const u8> data);
    Result WriteNtf(std::span<const u8> data);

    u64 GetHandle() const;
    DeviceState GetCurrentState() const;
    Result GetNpadId(Core::HID::NpadIdType& out_npad_id) const;

    Kernel::KReadableEvent& GetActivateEvent() const;
    Kernel::KReadableEvent& GetDeactivateEvent() const;

private:
    void NpadUpdate(Core::HID::ControllerTriggerType type);
    bool LoadNfcTag(u8 protocol, u8 tag_type, u8 uuid_length, UniqueSerialNumber uuid);
    bool LoadAmiiboData();
    void CloseNfcTag();

    NFP::AmiiboName GetAmiiboName(const NFP::AmiiboSettings& settings) const;
    void SetAmiiboName(NFP::AmiiboSettings& settings, const NFP::AmiiboName& amiibo_name) const;
    NFP::AmiiboDate GetAmiiboDate(s64 posix_time) const;
    s64 GetCurrentPosixTime() const;
    u64 RemoveVersionByte(u64 application_id) const;
    void UpdateSettingsCrc();
    void UpdateRegisterInfoCrc();

    void BuildAmiiboWithoutKeys(NFP::NTAG215File& stubbed_tag_data,
                                const NFP::EncryptedNTAG215File& encrypted_file) const;

    bool is_controller_set{};
    int callback_key;
    const Core::HID::NpadIdType npad_id;
    Core::System& system;
    Core::HID::EmulatedController* npad_device = nullptr;
    KernelHelpers::ServiceContext& service_context;
    Kernel::KEvent* activate_event = nullptr;
    Kernel::KEvent* deactivate_event = nullptr;
    Kernel::KEvent* availability_change_event = nullptr;

    bool is_initialized{};
    NfcProtocol allowed_protocols{};
    DeviceState device_state{DeviceState::Unavailable};

    // NFP data
    bool is_data_moddified{};
    bool is_app_area_open{};
    bool is_plain_amiibo{};
    bool is_write_protected{};
    NFP::MountTarget mount_target{NFP::MountTarget::None};

    TagInfo real_tag_info{};
    NFP::NTAG215File tag_data{};
    NFP::EncryptedNTAG215File encrypted_tag_data{};
};

} // namespace Service::NFC
