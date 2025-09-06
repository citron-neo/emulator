// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"

namespace Service::HID {
struct NpadSharedMemoryEntry;

struct AppletResourceHolder;
class NpadAbstractedPadHolder;
class NpadAbstractPropertiesHandler;

/// Handles Npad request from HID interfaces
class NpadAbstractButtonHandler final {
public:
    explicit NpadAbstractButtonHandler();
    ~NpadAbstractButtonHandler();

    void SetAbstractPadHolder(NpadAbstractedPadHolder* holder);
    void SetAppletResource(AppletResourceHolder* applet_resource);
    void SetPropertiesHandler(NpadAbstractPropertiesHandler* handler);

    Result IncrementRefCounter();
    Result DecrementRefCounter();

    Result UpdateAllButtonWithHomeProtection(u64 aruid);

    void UpdateAllButtonLifo();
    void UpdateCoreBatteryState();
    void UpdateButtonState(u64 aruid);

    Result SetHomeProtection(bool is_enabled, u64 aruid);
    bool IsButtonPressedOnConsoleMode();
    void EnableCenterClamp();

    void UpdateButtonLifo(NpadSharedMemoryEntry& shared_memory, u64 aruid);

    void UpdateNpadFullkeyLifo(Core::HID::NpadStyleTag style_tag, int index, u64 aruid,
                               NpadSharedMemoryEntry& shared_memory);
    void UpdateHandheldLifo(Core::HID::NpadStyleTag style_tag, int index, u64 aruid,
                            NpadSharedMemoryEntry& shared_memory);
    void UpdateJoyconDualLifo(Core::HID::NpadStyleTag style_tag, int index, u64 aruid,
                              NpadSharedMemoryEntry& shared_memory);
    void UpdateJoyconLeftLifo(Core::HID::NpadStyleTag style_tag, int index, u64 aruid,
                              NpadSharedMemoryEntry& shared_memory);
    void UpdateJoyconRightLifo(Core::HID::NpadStyleTag style_tag, int index, u64 aruid,
                               NpadSharedMemoryEntry& shared_memory);
    void UpdateSystemExtLifo(Core::HID::NpadStyleTag style_tag, int index, u64 aruid,
                             NpadSharedMemoryEntry& shared_memory);
    void UpdatePalmaLifo(Core::HID::NpadStyleTag style_tag, int index, u64 aruid,
                         NpadSharedMemoryEntry& shared_memory);

private:
    struct GcTrigger {
        float left;
        float right;
    };

    AppletResourceHolder* applet_resource_holder{nullptr};
    NpadAbstractedPadHolder* abstract_pad_holder{nullptr};
    NpadAbstractPropertiesHandler* properties_handler{nullptr};

    s32 ref_counter{};

    bool is_button_pressed_on_console_mode{};

    u64 gc_sampling_number{};
    GcTrigger gc_trigger_state{};
};

} // namespace Service::HID
