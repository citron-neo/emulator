// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"
#include "hid_core/resources/npad/npad_types.h"

namespace Service::HID {
struct NpadSharedMemoryEntry;

struct AppletResourceHolder;
class NpadAbstractedPadHolder;

struct ColorProperties {
    ColorAttribute attribute;
    Core::HID::NpadControllerColor color;
    INSERT_PADDING_BYTES(0x4);
};

/// Handles Npad request from HID interfaces
class NpadAbstractPropertiesHandler final {
public:
    explicit NpadAbstractPropertiesHandler();
    ~NpadAbstractPropertiesHandler();

    void SetAbstractPadHolder(NpadAbstractedPadHolder* holder);
    void SetAppletResource(AppletResourceHolder* applet_resource);
    void SetNpadId(Core::HID::NpadIdType npad_id);

    Core::HID::NpadIdType GetNpadId() const;

    Result IncrementRefCounter();
    Result DecrementRefCounter();

    Result ActivateNpadUnknown0x88(u64 aruid);

    void UpdateDeviceType();
    void UpdateDeviceColor();
    void UpdateFooterAttributes();
    void UpdateAllDeviceProperties();

    Core::HID::NpadInterfaceType GetFullkeyInterfaceType();
    Core::HID::NpadInterfaceType GetInterfaceType();

    Core::HID::NpadStyleSet GetStyleSet(u64 aruid);
    std::size_t GetAbstractedPadsWithStyleTag(std::span<IAbstractedPad*> list,
                                              Core::HID::NpadStyleTag style);
    std::size_t GetAbstractedPads(std::span<IAbstractedPad*> list);

    AppletFooterUiType GetAppletFooterUiType();

    AppletDetailedUiType GetAppletDetailedUiType();

    void UpdateDeviceProperties(u64 aruid, NpadSharedMemoryEntry& internal_state);

    Core::HID::NpadInterfaceType GetNpadInterfaceType();

    Result GetNpadFullKeyGripColor(Core::HID::NpadColor& main_color,
                                   Core::HID::NpadColor& sub_color) const;

    void GetNpadLeftRightInterfaceType(Core::HID::NpadInterfaceType& param_2,
                                       Core::HID::NpadInterfaceType& param_3) const;

private:
    AppletResourceHolder* applet_resource_holder{nullptr};
    NpadAbstractedPadHolder* abstract_pad_holder{nullptr};
    Core::HID::NpadIdType npad_id_type{Core::HID::NpadIdType::Invalid};
    s32 ref_counter{};
    Core::HID::DeviceIndex device_type{};
    AppletDetailedUiType applet_ui_type{};
    AppletFooterUiAttributes applet_ui_attributes{};
    bool is_vertical{};
    bool is_horizontal{};
    bool use_plus{};
    bool use_minus{};
    bool has_directional_buttons{};
    ColorProperties fullkey_color{};
    ColorProperties left_color{};
    ColorProperties right_color{};
};
} // namespace Service::HID
