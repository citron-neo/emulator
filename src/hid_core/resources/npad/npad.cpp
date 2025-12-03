// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>

#include "common/assert.h"
#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/service/kernel_helpers.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_result.h"
#include "hid_core/hid_util.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/npad/npad.h"
#include "hid_core/resources/npad/npad_vibration.h"
#include "hid_core/resources/shared_memory_format.h"

namespace Service::HID {

NPad::NPad(Core::HID::HIDCore& hid_core_, KernelHelpers::ServiceContext& service_context_)
    : hid_core{hid_core_}, service_context{service_context_}, npad_resource{service_context} {
    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; ++aruid_index) {
        for (std::size_t i = 0; i < controller_data[aruid_index].size(); ++i) {
            auto& controller = controller_data[aruid_index][i];
            controller.device = hid_core.GetEmulatedControllerByIndex(i);
            Core::HID::ControllerUpdateCallback engine_callback{
                .on_change =
                    [this, i](Core::HID::ControllerTriggerType type) { ControllerUpdate(type, i); },
                .is_npad_service = true,
            };
            controller.callback_key = controller.device->SetCallback(engine_callback);
        }
    }
    for (std::size_t i = 0; i < abstracted_pads.size(); ++i) {
        abstracted_pads[i] = AbstractPad{};
        abstracted_pads[i].SetNpadId(IndexToNpadIdType(i));
    }
}

NPad::~NPad() {
    // Iterate over ALL aruids and ALL controllers to delete every callback
    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; ++aruid_index) {
        for (std::size_t i = 0; i < controller_data[aruid_index].size(); ++i) {
            auto& controller = controller_data[aruid_index][i];
            if (controller.device && controller.callback_key != -1) {
                controller.device->DeleteCallback(controller.callback_key);
                controller.callback_key = -1; // Prevent double deletion
            }
        }
    }
}

Result NPad::Activate() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadResourceOverflow;
    }

    if (ref_counter == 0) {
        std::scoped_lock lock{mutex};

        // TODO: Activate handlers and AbstractedPad
    }

    ref_counter++;
    return ResultSuccess;
}

Result NPad::Activate(u64 aruid) {
    std::scoped_lock lock{mutex};
    std::scoped_lock shared_lock{*applet_resource_holder.shared_mutex};

    auto* data = applet_resource_holder.applet_resource->GetAruidData(aruid);
    const auto aruid_index = applet_resource_holder.applet_resource->GetIndexFromAruid(aruid);

    if (data == nullptr || !data->flag.is_assigned) {
        return ResultSuccess;
    }

    for (std::size_t i = 0; i < controller_data[aruid_index].size(); ++i) {
        auto& controller = controller_data[aruid_index][i];
        controller.shared_memory = &data->shared_memory_format->npad.npad_entry[i].internal_state;
    }

    // Prefill controller buffers
    for (auto& controller : controller_data[aruid_index]) {
        auto* npad = controller.shared_memory;
        npad->fullkey_color = {
            .attribute = ColorAttribute::NoController,
            .fullkey = {},
        };
        npad->joycon_color = {
            .attribute = ColorAttribute::NoController,
            .left = {},
            .right = {},
        };
        // HW seems to initialize the first 19 entries
        for (std::size_t i = 0; i < 19; ++i) {
            WriteEmptyEntry(npad);
        }

        controller.is_active = true;
    }

    return ResultSuccess;
}

Result NPad::ActivateNpadResource() {
    return npad_resource.Activate();
}

Result NPad::ActivateNpadResource(u64 aruid) {
    return npad_resource.Activate(aruid);
}

void NPad::FreeAppletResourceId(u64 aruid) {
    return npad_resource.FreeAppletResourceId(aruid);
}

void NPad::ControllerUpdate(Core::HID::ControllerTriggerType type, std::size_t controller_idx) {
    if (type == Core::HID::ControllerTriggerType::All) {
        ControllerUpdate(Core::HID::ControllerTriggerType::Connected, controller_idx);
        ControllerUpdate(Core::HID::ControllerTriggerType::Battery, controller_idx);
        return;
    }

    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        if (controller_idx >= controller_data[aruid_index].size()) {
            return;
        }

        auto* data = applet_resource_holder.applet_resource->GetAruidDataByIndex(aruid_index);

        if (data == nullptr || !data->flag.is_assigned) {
            continue;
        }

        auto& controller = controller_data[aruid_index][controller_idx];
        const auto is_connected = controller.device->IsConnected();
        const auto npad_type = controller.device->GetNpadStyleIndex();
        const auto npad_id = controller.device->GetNpadIdType();
        switch (type) {
        case Core::HID::ControllerTriggerType::Connected:
        case Core::HID::ControllerTriggerType::Disconnected:
            if (is_connected == controller.is_connected) {
                return;
            }
            UpdateControllerAt(data->aruid, npad_type, npad_id, is_connected);
            break;
        case Core::HID::ControllerTriggerType::Battery: {
            if (!controller.device->IsConnected()) {
                return;
            }
            auto* shared_memory = controller.shared_memory;
            const auto& battery_level = controller.device->GetBattery();
            shared_memory->battery_level_dual = battery_level.dual.battery_level;
            shared_memory->battery_level_left = battery_level.left.battery_level;
            shared_memory->battery_level_right = battery_level.right.battery_level;
            break;
        }
        default:
            break;
        }
    }
}

void NPad::InitNewlyAddedController(u64 aruid, Core::HID::NpadIdType npad_id) {
    auto& controller = GetControllerFromNpadIdType(aruid, npad_id);
    if (!npad_resource.IsControllerSupported(aruid, controller.device->GetNpadStyleIndex())) {
        return;
    }
    LOG_DEBUG(Service_HID, "Npad connected {}", npad_id);
    const auto controller_type = controller.device->GetNpadStyleIndex();
    const auto& body_colors = controller.device->GetColors();
    const auto& battery_level = controller.device->GetBattery();
    auto* shared_memory = controller.shared_memory;
    if (controller_type == Core::HID::NpadStyleIndex::None) {
        npad_resource.SignalStyleSetUpdateEvent(aruid, npad_id);
        return;
    }

    // Reset memory values
    shared_memory->style_tag.raw = Core::HID::NpadStyleSet::None;
    shared_memory->device_type.raw = 0;
    shared_memory->system_properties.raw = 0;
    shared_memory->joycon_color.attribute = ColorAttribute::NoController;
    shared_memory->joycon_color.attribute = ColorAttribute::NoController;
    shared_memory->fullkey_color = {};
    shared_memory->joycon_color.left = {};
    shared_memory->joycon_color.right = {};
    shared_memory->battery_level_dual = {};
    shared_memory->battery_level_left = {};
    shared_memory->battery_level_right = {};

    switch (controller_type) {
    case Core::HID::NpadStyleIndex::None:
        ASSERT(false);
        break;
    case Core::HID::NpadStyleIndex::Fullkey:
        shared_memory->fullkey_color.attribute = ColorAttribute::Ok;
        shared_memory->fullkey_color.fullkey = body_colors.fullkey;
        shared_memory->battery_level_dual = battery_level.dual.battery_level;
        shared_memory->style_tag.fullkey.Assign(1);
        shared_memory->device_type.fullkey.Assign(1);
        shared_memory->system_properties.is_vertical.Assign(1);
        shared_memory->system_properties.use_plus.Assign(1);
        shared_memory->system_properties.use_minus.Assign(1);
        shared_memory->system_properties.is_charging_joy_dual.Assign(
            battery_level.dual.is_charging);
        shared_memory->applet_footer_type = AppletFooterUiType::SwitchProController;
        shared_memory->sixaxis_fullkey_properties.is_newly_assigned.Assign(1);
        break;
    case Core::HID::NpadStyleIndex::Handheld:
        shared_memory->fullkey_color.attribute = ColorAttribute::Ok;
        shared_memory->joycon_color.attribute = ColorAttribute::Ok;
        shared_memory->fullkey_color.fullkey = body_colors.fullkey;
        shared_memory->joycon_color.left = body_colors.left;
        shared_memory->joycon_color.right = body_colors.right;
        shared_memory->style_tag.handheld.Assign(1);
        shared_memory->device_type.handheld_left.Assign(1);
        shared_memory->device_type.handheld_right.Assign(1);
        shared_memory->system_properties.is_vertical.Assign(1);
        shared_memory->system_properties.use_plus.Assign(1);
        shared_memory->system_properties.use_minus.Assign(1);
        shared_memory->system_properties.use_directional_buttons.Assign(1);
        shared_memory->system_properties.is_charging_joy_dual.Assign(
            battery_level.left.is_charging);
        shared_memory->system_properties.is_charging_joy_left.Assign(
            battery_level.left.is_charging);
        shared_memory->system_properties.is_charging_joy_right.Assign(
            battery_level.right.is_charging);
        shared_memory->assignment_mode = NpadJoyAssignmentMode::Dual;
        shared_memory->applet_footer_type = AppletFooterUiType::HandheldJoyConLeftJoyConRight;
        shared_memory->sixaxis_handheld_properties.is_newly_assigned.Assign(1);
        break;
    case Core::HID::NpadStyleIndex::JoyconDual:
        shared_memory->fullkey_color.attribute = ColorAttribute::Ok;
        shared_memory->joycon_color.attribute = ColorAttribute::Ok;
        shared_memory->style_tag.joycon_dual.Assign(1);
        if (controller.is_dual_left_connected) {
            shared_memory->joycon_color.left = body_colors.left;
            shared_memory->battery_level_left = battery_level.left.battery_level;
            shared_memory->device_type.joycon_left.Assign(1);
            shared_memory->system_properties.use_minus.Assign(1);
            shared_memory->system_properties.is_charging_joy_left.Assign(
                battery_level.left.is_charging);
            shared_memory->sixaxis_dual_left_properties.is_newly_assigned.Assign(1);
        }
        if (controller.is_dual_right_connected) {
            shared_memory->joycon_color.right = body_colors.right;
            shared_memory->battery_level_right = battery_level.right.battery_level;
            shared_memory->device_type.joycon_right.Assign(1);
            shared_memory->system_properties.use_plus.Assign(1);
            shared_memory->system_properties.is_charging_joy_right.Assign(
                battery_level.right.is_charging);
            shared_memory->sixaxis_dual_right_properties.is_newly_assigned.Assign(1);
        }
        shared_memory->system_properties.use_directional_buttons.Assign(1);
        shared_memory->system_properties.is_vertical.Assign(1);
        shared_memory->assignment_mode = NpadJoyAssignmentMode::Dual;

        if (controller.is_dual_left_connected && controller.is_dual_right_connected) {
            shared_memory->applet_footer_type = AppletFooterUiType::JoyDual;
            shared_memory->fullkey_color.fullkey = body_colors.left;
            shared_memory->battery_level_dual = battery_level.left.battery_level;
            shared_memory->system_properties.is_charging_joy_dual.Assign(
                battery_level.left.is_charging);
        } else if (controller.is_dual_left_connected) {
            shared_memory->applet_footer_type = AppletFooterUiType::JoyDualLeftOnly;
            shared_memory->fullkey_color.fullkey = body_colors.left;
            shared_memory->battery_level_dual = battery_level.left.battery_level;
            shared_memory->system_properties.is_charging_joy_dual.Assign(
                battery_level.left.is_charging);
        } else {
            shared_memory->applet_footer_type = AppletFooterUiType::JoyDualRightOnly;
            shared_memory->fullkey_color.fullkey = body_colors.right;
            shared_memory->battery_level_dual = battery_level.right.battery_level;
            shared_memory->system_properties.is_charging_joy_dual.Assign(
                battery_level.right.is_charging);
        }
        break;
    case Core::HID::NpadStyleIndex::JoyconLeft:
        shared_memory->fullkey_color.attribute = ColorAttribute::Ok;
        shared_memory->fullkey_color.fullkey = body_colors.left;
        shared_memory->joycon_color.attribute = ColorAttribute::Ok;
        shared_memory->joycon_color.left = body_colors.left;
        shared_memory->battery_level_dual = battery_level.left.battery_level;
        shared_memory->style_tag.joycon_left.Assign(1);
        shared_memory->device_type.joycon_left.Assign(1);
        shared_memory->system_properties.is_horizontal.Assign(1);
        shared_memory->system_properties.use_minus.Assign(1);
        shared_memory->system_properties.is_charging_joy_left.Assign(
            battery_level.left.is_charging);
        shared_memory->applet_footer_type = AppletFooterUiType::JoyLeftHorizontal;
        shared_memory->sixaxis_left_properties.is_newly_assigned.Assign(1);
        break;
    case Core::HID::NpadStyleIndex::JoyconRight:
        shared_memory->fullkey_color.attribute = ColorAttribute::Ok;
        shared_memory->fullkey_color.fullkey = body_colors.right;
        shared_memory->joycon_color.attribute = ColorAttribute::Ok;
        shared_memory->joycon_color.right = body_colors.right;
        shared_memory->battery_level_right = battery_level.right.battery_level;
        shared_memory->style_tag.joycon_right.Assign(1);
        shared_memory->device_type.joycon_right.Assign(1);
        shared_memory->system_properties.is_horizontal.Assign(1);
        shared_memory->system_properties.use_plus.Assign(1);
        shared_memory->system_properties.is_charging_joy_right.Assign(
            battery_level.right.is_charging);
        shared_memory->applet_footer_type = AppletFooterUiType::JoyRightHorizontal;
        shared_memory->sixaxis_right_properties.is_newly_assigned.Assign(1);
        break;
    case Core::HID::NpadStyleIndex::GameCube:
        shared_memory->style_tag.gamecube.Assign(1);
        shared_memory->device_type.fullkey.Assign(1);
        shared_memory->system_properties.is_vertical.Assign(1);
        shared_memory->system_properties.use_plus.Assign(1);
        break;
    case Core::HID::NpadStyleIndex::Pokeball:
        shared_memory->style_tag.palma.Assign(1);
        shared_memory->device_type.palma.Assign(1);
        shared_memory->sixaxis_fullkey_properties.is_newly_assigned.Assign(1);
        break;
    case Core::HID::NpadStyleIndex::NES:
        shared_memory->style_tag.lark.Assign(1);
        shared_memory->device_type.fullkey.Assign(1);
        break;
    case Core::HID::NpadStyleIndex::SNES:
        shared_memory->style_tag.lucia.Assign(1);
        shared_memory->device_type.fullkey.Assign(1);
        shared_memory->applet_footer_type = AppletFooterUiType::Lucia;
        break;
    case Core::HID::NpadStyleIndex::N64:
        shared_memory->style_tag.lagoon.Assign(1);
        shared_memory->device_type.fullkey.Assign(1);
        shared_memory->applet_footer_type = AppletFooterUiType::Lagon;
        break;
    case Core::HID::NpadStyleIndex::SegaGenesis:
        shared_memory->style_tag.lager.Assign(1);
        shared_memory->device_type.fullkey.Assign(1);
        break;
    default:
        break;
    }

    controller.is_connected = true;
    controller.device->Connect();
    controller.device->SetLedPattern();
    if (controller_type == Core::HID::NpadStyleIndex::JoyconDual) {
        if (controller.is_dual_left_connected) {
            controller.device->SetPollingMode(Core::HID::EmulatedDeviceIndex::LeftIndex,
                                              Common::Input::PollingMode::Active);
        }
        if (controller.is_dual_right_connected) {
            controller.device->SetPollingMode(Core::HID::EmulatedDeviceIndex::RightIndex,
                                              Common::Input::PollingMode::Active);
        }
    } else {
        controller.device->SetPollingMode(Core::HID::EmulatedDeviceIndex::AllDevices,
                                          Common::Input::PollingMode::Active);
    }

    npad_resource.SignalStyleSetUpdateEvent(aruid, npad_id);
    WriteEmptyEntry(controller.shared_memory);
    hid_core.SetLastActiveController(npad_id);
    abstracted_pads[NpadIdTypeToIndex(npad_id)].Update();
}

void NPad::WriteEmptyEntry(NpadInternalState* npad) {
    NPadGenericState dummy_pad_state{};
    NpadGcTriggerState dummy_gc_state{};
    dummy_pad_state.sampling_number = npad->fullkey_lifo.ReadCurrentEntry().sampling_number + 1;
    npad->fullkey_lifo.WriteNextEntry(dummy_pad_state);
    dummy_pad_state.sampling_number = npad->handheld_lifo.ReadCurrentEntry().sampling_number + 1;
    npad->handheld_lifo.WriteNextEntry(dummy_pad_state);
    dummy_pad_state.sampling_number = npad->joy_dual_lifo.ReadCurrentEntry().sampling_number + 1;
    npad->joy_dual_lifo.WriteNextEntry(dummy_pad_state);
    dummy_pad_state.sampling_number = npad->joy_left_lifo.ReadCurrentEntry().sampling_number + 1;
    npad->joy_left_lifo.WriteNextEntry(dummy_pad_state);
    dummy_pad_state.sampling_number = npad->joy_right_lifo.ReadCurrentEntry().sampling_number + 1;
    npad->joy_right_lifo.WriteNextEntry(dummy_pad_state);
    dummy_pad_state.sampling_number = npad->palma_lifo.ReadCurrentEntry().sampling_number + 1;
    npad->palma_lifo.WriteNextEntry(dummy_pad_state);
    dummy_pad_state.sampling_number = npad->system_ext_lifo.ReadCurrentEntry().sampling_number + 1;
    npad->system_ext_lifo.WriteNextEntry(dummy_pad_state);
    dummy_gc_state.sampling_number = npad->gc_trigger_lifo.ReadCurrentEntry().sampling_number + 1;
    npad->gc_trigger_lifo.WriteNextEntry(dummy_gc_state);
}

void NPad::RequestPadStateUpdate(u64 aruid, Core::HID::NpadIdType npad_id) {
    std::scoped_lock lock{*applet_resource_holder.shared_mutex};
    auto& controller = GetControllerFromNpadIdType(aruid, npad_id);
    const auto controller_type = controller.device->GetNpadStyleIndex();

    if (!controller.device->IsConnected() && controller.is_connected) {
        DisconnectNpad(aruid, npad_id);
        return;
    }
    if (!controller.device->IsConnected()) {
        return;
    }
    if (controller.device->IsConnected() && !controller.is_connected) {
        InitNewlyAddedController(aruid, npad_id);
    }

    // This function is unique to citron for the turbo buttons and motion to work properly
    controller.device->StatusUpdate();

    auto& pad_entry = controller.npad_pad_state;
    auto& trigger_entry = controller.npad_trigger_state;
    const auto button_state = controller.device->GetNpadButtons();
    const auto stick_state = controller.device->GetSticks();

    using btn = Core::HID::NpadButton;
    pad_entry.npad_buttons.raw = btn::None;
    if (controller_type != Core::HID::NpadStyleIndex::JoyconLeft) {
        constexpr btn right_button_mask = btn::A | btn::B | btn::X | btn::Y | btn::StickR | btn::R |
                                          btn::ZR | btn::Plus | btn::StickRLeft | btn::StickRUp |
                                          btn::StickRRight | btn::StickRDown;
        pad_entry.npad_buttons.raw = button_state.raw & right_button_mask;
        pad_entry.r_stick = stick_state.right;
    }

    if (controller_type != Core::HID::NpadStyleIndex::JoyconRight) {
        constexpr btn left_button_mask =
            btn::Left | btn::Up | btn::Right | btn::Down | btn::StickL | btn::L | btn::ZL |
            btn::Minus | btn::StickLLeft | btn::StickLUp | btn::StickLRight | btn::StickLDown;
        pad_entry.npad_buttons.raw |= button_state.raw & left_button_mask;
        pad_entry.l_stick = stick_state.left;
    }

    if (controller_type == Core::HID::NpadStyleIndex::JoyconLeft ||
        controller_type == Core::HID::NpadStyleIndex::JoyconDual) {
        pad_entry.npad_buttons.left_sl.Assign(button_state.left_sl);
        pad_entry.npad_buttons.left_sr.Assign(button_state.left_sr);
    }

    if (controller_type == Core::HID::NpadStyleIndex::JoyconRight ||
        controller_type == Core::HID::NpadStyleIndex::JoyconDual) {
        pad_entry.npad_buttons.right_sl.Assign(button_state.right_sl);
        pad_entry.npad_buttons.right_sr.Assign(button_state.right_sr);
    }

    if (controller_type == Core::HID::NpadStyleIndex::GameCube) {
        const auto& trigger_state = controller.device->GetTriggers();
        trigger_entry.l_analog = trigger_state.left;
        trigger_entry.r_analog = trigger_state.right;
        pad_entry.npad_buttons.zl.Assign(false);
        pad_entry.npad_buttons.zr.Assign(button_state.r);
        pad_entry.npad_buttons.l.Assign(button_state.zl);
        pad_entry.npad_buttons.r.Assign(button_state.zr);
    }

    if (pad_entry.npad_buttons.raw != Core::HID::NpadButton::None) {
        hid_core.SetLastActiveController(npad_id);
    }
}

void NPad::OnUpdate(const Core::Timing::CoreTiming& core_timing) {
    if (ref_counter == 0) {
        return;
    }

    std::scoped_lock lock{*applet_resource_holder.shared_mutex};
    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; ++aruid_index) {
        const auto* data = applet_resource_holder.applet_resource->GetAruidDataByIndex(aruid_index);

        if (data == nullptr || !data->flag.is_assigned) {
            continue;
        }

        bool is_set{};
        const auto aruid = data->aruid;
        npad_resource.IsSupportedNpadStyleSet(is_set, aruid);
        // Wait until style is defined
        if (!is_set) {
            continue;
        }

        for (std::size_t i = 0; i < controller_data[aruid_index].size(); ++i) {
            auto& controller = controller_data[aruid_index][i];
            controller.shared_memory =
                &data->shared_memory_format->npad.npad_entry[i].internal_state;
            auto* npad = controller.shared_memory;

            const auto& controller_type = controller.device->GetNpadStyleIndex();

            if (controller_type == Core::HID::NpadStyleIndex::None ||
                !controller.device->IsConnected()) {
                continue;
            }

            if (!data->flag.enable_pad_input) {
                continue;
            }

            if (!controller.is_active) {
                continue;
            }

            RequestPadStateUpdate(aruid, controller.device->GetNpadIdType());
            auto& pad_state = controller.npad_pad_state;
            auto& libnx_state = controller.npad_libnx_state;
            auto& trigger_state = controller.npad_trigger_state;

            // LibNX exclusively uses this section, so we always update it since LibNX doesn't
            // activate any controllers.
            libnx_state.connection_status.raw = 0;
            libnx_state.connection_status.is_connected.Assign(1);
            switch (controller_type) {
            case Core::HID::NpadStyleIndex::None:
                ASSERT(false);
                break;
            case Core::HID::NpadStyleIndex::Fullkey:
            case Core::HID::NpadStyleIndex::NES:
            case Core::HID::NpadStyleIndex::SNES:
            case Core::HID::NpadStyleIndex::N64:
            case Core::HID::NpadStyleIndex::SegaGenesis:
                pad_state.connection_status.raw = 0;
                pad_state.connection_status.is_connected.Assign(1);
                pad_state.connection_status.is_wired.Assign(1);

                libnx_state.connection_status.is_wired.Assign(1);
                pad_state.sampling_number =
                    npad->fullkey_lifo.ReadCurrentEntry().state.sampling_number + 1;
                npad->fullkey_lifo.WriteNextEntry(pad_state);
                break;
            case Core::HID::NpadStyleIndex::Handheld:
                pad_state.connection_status.raw = 0;
                pad_state.connection_status.is_connected.Assign(1);
                pad_state.connection_status.is_wired.Assign(1);
                pad_state.connection_status.is_left_connected.Assign(1);
                pad_state.connection_status.is_right_connected.Assign(1);
                pad_state.connection_status.is_left_wired.Assign(1);
                pad_state.connection_status.is_right_wired.Assign(1);

                libnx_state.connection_status.is_wired.Assign(1);
                libnx_state.connection_status.is_left_connected.Assign(1);
                libnx_state.connection_status.is_right_connected.Assign(1);
                libnx_state.connection_status.is_left_wired.Assign(1);
                libnx_state.connection_status.is_right_wired.Assign(1);
                pad_state.sampling_number =
                    npad->handheld_lifo.ReadCurrentEntry().state.sampling_number + 1;
                npad->handheld_lifo.WriteNextEntry(pad_state);
                break;
            case Core::HID::NpadStyleIndex::JoyconDual:
                pad_state.connection_status.raw = 0;
                pad_state.connection_status.is_connected.Assign(1);
                if (controller.is_dual_left_connected) {
                    pad_state.connection_status.is_left_connected.Assign(1);
                    libnx_state.connection_status.is_left_connected.Assign(1);
                }
                if (controller.is_dual_right_connected) {
                    pad_state.connection_status.is_right_connected.Assign(1);
                    libnx_state.connection_status.is_right_connected.Assign(1);
                }

                pad_state.sampling_number =
                    npad->joy_dual_lifo.ReadCurrentEntry().state.sampling_number + 1;
                npad->joy_dual_lifo.WriteNextEntry(pad_state);
                break;
            case Core::HID::NpadStyleIndex::JoyconLeft:
                pad_state.connection_status.raw = 0;
                pad_state.connection_status.is_connected.Assign(1);
                pad_state.connection_status.is_left_connected.Assign(1);

                libnx_state.connection_status.is_left_connected.Assign(1);
                pad_state.sampling_number =
                    npad->joy_left_lifo.ReadCurrentEntry().state.sampling_number + 1;
                npad->joy_left_lifo.WriteNextEntry(pad_state);
                break;
            case Core::HID::NpadStyleIndex::JoyconRight:
                pad_state.connection_status.raw = 0;
                pad_state.connection_status.is_connected.Assign(1);
                pad_state.connection_status.is_right_connected.Assign(1);

                libnx_state.connection_status.is_right_connected.Assign(1);
                pad_state.sampling_number =
                    npad->joy_right_lifo.ReadCurrentEntry().state.sampling_number + 1;
                npad->joy_right_lifo.WriteNextEntry(pad_state);
                break;
            case Core::HID::NpadStyleIndex::GameCube:
                pad_state.connection_status.raw = 0;
                pad_state.connection_status.is_connected.Assign(1);
                pad_state.connection_status.is_wired.Assign(1);

                libnx_state.connection_status.is_wired.Assign(1);
                pad_state.sampling_number =
                    npad->fullkey_lifo.ReadCurrentEntry().state.sampling_number + 1;
                trigger_state.sampling_number =
                    npad->gc_trigger_lifo.ReadCurrentEntry().state.sampling_number + 1;
                npad->fullkey_lifo.WriteNextEntry(pad_state);
                npad->gc_trigger_lifo.WriteNextEntry(trigger_state);
                break;
            case Core::HID::NpadStyleIndex::Pokeball:
                pad_state.connection_status.raw = 0;
                pad_state.connection_status.is_connected.Assign(1);
                pad_state.sampling_number =
                    npad->palma_lifo.ReadCurrentEntry().state.sampling_number + 1;
                npad->palma_lifo.WriteNextEntry(pad_state);
                break;
            default:
                break;
            }

            libnx_state.npad_buttons.raw = pad_state.npad_buttons.raw;
            libnx_state.l_stick = pad_state.l_stick;
            libnx_state.r_stick = pad_state.r_stick;
            libnx_state.sampling_number =
                npad->system_ext_lifo.ReadCurrentEntry().state.sampling_number + 1;
            npad->system_ext_lifo.WriteNextEntry(libnx_state);

            press_state |= static_cast<u64>(pad_state.npad_buttons.raw);
        }
    }
}

Result NPad::SetSupportedNpadStyleSet(u64 aruid, Core::HID::NpadStyleSet supported_style_set) {
    std::scoped_lock lock{mutex};
    hid_core.SetSupportedStyleTag({supported_style_set});
    const Result result = npad_resource.SetSupportedNpadStyleSet(aruid, supported_style_set);
    if (result.IsSuccess()) {
        OnUpdate({});
    }
    return result;
}

Result NPad::GetSupportedNpadStyleSet(u64 aruid,
                                      Core::HID::NpadStyleSet& out_supported_style_set) const {
    std::scoped_lock lock{mutex};
    const Result result = npad_resource.GetSupportedNpadStyleSet(out_supported_style_set, aruid);

    if (result == ResultUndefinedStyleset) {
        out_supported_style_set = Core::HID::NpadStyleSet::None;
        return ResultSuccess;
    }

    return result;
}

Result NPad::GetMaskedSupportedNpadStyleSet(
    u64 aruid, Core::HID::NpadStyleSet& out_supported_style_set) const {
    std::scoped_lock lock{mutex};
    const Result result =
        npad_resource.GetMaskedSupportedNpadStyleSet(out_supported_style_set, aruid);

    if (result == ResultUndefinedStyleset) {
        out_supported_style_set = Core::HID::NpadStyleSet::None;
        return ResultSuccess;
    }

    return result;
}

Result NPad::SetSupportedNpadIdType(u64 aruid,
                                    std::span<const Core::HID::NpadIdType> supported_npad_list) {
    std::scoped_lock lock{mutex};
    if (supported_npad_list.size() > MaxSupportedNpadIdTypes) {
        return ResultInvalidArraySize;
    }

    Result result = npad_resource.SetSupportedNpadIdType(aruid, supported_npad_list);

    if (result.IsSuccess()) {
        OnUpdate({});
    }

    return result;
}

Result NPad::SetNpadJoyHoldType(u64 aruid, NpadJoyHoldType hold_type) {
    std::scoped_lock lock{mutex};
    return npad_resource.SetNpadJoyHoldType(aruid, hold_type);
}

Result NPad::GetNpadJoyHoldType(u64 aruid, NpadJoyHoldType& out_hold_type) const {
    std::scoped_lock lock{mutex};
    return npad_resource.GetNpadJoyHoldType(out_hold_type, aruid);
}

Result NPad::SetNpadHandheldActivationMode(u64 aruid, NpadHandheldActivationMode mode) {
    std::scoped_lock lock{mutex};
    Result result = npad_resource.SetNpadHandheldActivationMode(aruid, mode);
    if (result.IsSuccess()) {
        OnUpdate({});
    }
    return result;
}

Result NPad::GetNpadHandheldActivationMode(u64 aruid, NpadHandheldActivationMode& out_mode) const {
    std::scoped_lock lock{mutex};
    return npad_resource.GetNpadHandheldActivationMode(out_mode, aruid);
}

bool NPad::SetNpadMode(u64 aruid, Core::HID::NpadIdType& new_npad_id, Core::HID::NpadIdType npad_id,
                       NpadJoyDeviceType npad_device_type, NpadJoyAssignmentMode assignment_mode) {
    if (!IsNpadIdValid(npad_id)) {
        LOG_ERROR(Service_HID, "Invalid NpadIdType npad_id:{}", npad_id);
        return false;
    }

    auto& controller = GetControllerFromNpadIdType(aruid, npad_id);
    if (controller.shared_memory->assignment_mode != assignment_mode) {
        controller.shared_memory->assignment_mode = assignment_mode;
    }

    if (!controller.device->IsConnected()) {
        return false;
    }

    if (assignment_mode == NpadJoyAssignmentMode::Dual) {
        if (controller.device->GetNpadStyleIndex() == Core::HID::NpadStyleIndex::JoyconLeft) {
            DisconnectNpad(aruid, npad_id);
            controller.is_dual_left_connected = true;
            controller.is_dual_right_connected = false;
            UpdateControllerAt(aruid, Core::HID::NpadStyleIndex::JoyconDual, npad_id, true);
            return false;
        }
        if (controller.device->GetNpadStyleIndex() == Core::HID::NpadStyleIndex::JoyconRight) {
            DisconnectNpad(aruid, npad_id);
            controller.is_dual_left_connected = false;
            controller.is_dual_right_connected = true;
            UpdateControllerAt(aruid, Core::HID::NpadStyleIndex::JoyconDual, npad_id, true);
            return false;
        }
        return false;
    }

    // This is for NpadJoyAssignmentMode::Single

    // Only JoyconDual get affected by this function
    if (controller.device->GetNpadStyleIndex() != Core::HID::NpadStyleIndex::JoyconDual) {
        return false;
    }

    if (controller.is_dual_left_connected && !controller.is_dual_right_connected) {
        DisconnectNpad(aruid, npad_id);
        UpdateControllerAt(aruid, Core::HID::NpadStyleIndex::JoyconLeft, npad_id, true);
        return false;
    }
    if (!controller.is_dual_left_connected && controller.is_dual_right_connected) {
        DisconnectNpad(aruid, npad_id);
        UpdateControllerAt(aruid, Core::HID::NpadStyleIndex::JoyconRight, npad_id, true);
        return false;
    }

    // We have two controllers connected to the same npad_id we need to split them
    new_npad_id = hid_core.GetFirstDisconnectedNpadId();
    auto& controller_2 = GetControllerFromNpadIdType(aruid, new_npad_id);
    DisconnectNpad(aruid, npad_id);
    if (npad_device_type == NpadJoyDeviceType::Left) {
        UpdateControllerAt(aruid, Core::HID::NpadStyleIndex::JoyconLeft, npad_id, true);
        controller_2.is_dual_left_connected = false;
        controller_2.is_dual_right_connected = true;
        UpdateControllerAt(aruid, Core::HID::NpadStyleIndex::JoyconDual, new_npad_id, true);
    } else {
        UpdateControllerAt(aruid, Core::HID::NpadStyleIndex::JoyconRight, npad_id, true);
        controller_2.is_dual_left_connected = true;
        controller_2.is_dual_right_connected = false;
        UpdateControllerAt(aruid, Core::HID::NpadStyleIndex::JoyconDual, new_npad_id, true);
    }
    return true;
}

Result NPad::AcquireNpadStyleSetUpdateEventHandle(u64 aruid, Kernel::KReadableEvent** out_event,
                                                  Core::HID::NpadIdType npad_id) {
    std::scoped_lock lock{mutex};
    return npad_resource.AcquireNpadStyleSetUpdateEventHandle(aruid, out_event, npad_id);
}

void NPad::AddNewControllerAt(u64 aruid, Core::HID::NpadStyleIndex controller,
                              Core::HID::NpadIdType npad_id) {
    UpdateControllerAt(aruid, controller, npad_id, true);
}

void NPad::UpdateControllerAt(u64 aruid, Core::HID::NpadStyleIndex type,
                              Core::HID::NpadIdType npad_id, bool connected) {
    auto& controller = GetControllerFromNpadIdType(aruid, npad_id);
    if (!connected) {
        DisconnectNpad(aruid, npad_id);
        return;
    }

    controller.device->SetNpadStyleIndex(type);
    InitNewlyAddedController(aruid, npad_id);
}

Result NPad::DisconnectNpad(u64 aruid, Core::HID::NpadIdType npad_id) {
    if (!IsNpadIdValid(npad_id)) {
        LOG_ERROR(Service_HID, "Invalid NpadIdType npad_id:{}", npad_id);
        return ResultInvalidNpadId;
    }

    LOG_DEBUG(Service_HID, "Npad disconnected {}", npad_id);
    auto& controller = GetControllerFromNpadIdType(aruid, npad_id);

    auto* shared_memory = controller.shared_memory;
    // Don't reset shared_memory->assignment_mode this value is persistent
    shared_memory->style_tag.raw = Core::HID::NpadStyleSet::None; // Zero out
    shared_memory->device_type.raw = 0;
    shared_memory->system_properties.raw = 0;
    shared_memory->button_properties.raw = 0;
    shared_memory->sixaxis_fullkey_properties.raw = 0;
    shared_memory->sixaxis_handheld_properties.raw = 0;
    shared_memory->sixaxis_dual_left_properties.raw = 0;
    shared_memory->sixaxis_dual_right_properties.raw = 0;
    shared_memory->sixaxis_left_properties.raw = 0;
    shared_memory->sixaxis_right_properties.raw = 0;
    shared_memory->battery_level_dual = Core::HID::NpadBatteryLevel::Empty;
    shared_memory->battery_level_left = Core::HID::NpadBatteryLevel::Empty;
    shared_memory->battery_level_right = Core::HID::NpadBatteryLevel::Empty;
    shared_memory->fullkey_color = {
        .attribute = ColorAttribute::NoController,
        .fullkey = {},
    };
    shared_memory->joycon_color = {
        .attribute = ColorAttribute::NoController,
        .left = {},
        .right = {},
    };
    shared_memory->applet_footer_type = AppletFooterUiType::None;

    controller.is_dual_left_connected = true;
    controller.is_dual_right_connected = true;
    controller.is_connected = false;
    controller.device->Disconnect();
    npad_resource.SignalStyleSetUpdateEvent(aruid, npad_id);
    WriteEmptyEntry(shared_memory);
    return ResultSuccess;
}

Result NPad::IsFirmwareUpdateAvailableForSixAxisSensor(
    u64 aruid, const Core::HID::SixAxisSensorHandle& sixaxis_handle,
    bool& is_firmware_available) const {
    const auto is_valid = IsSixaxisHandleValid(sixaxis_handle);
    if (is_valid.IsError()) {
        LOG_ERROR(Service_HID, "Invalid handle, error_code={}", is_valid.raw);
        return is_valid;
    }

    const auto& sixaxis_properties = GetSixaxisProperties(aruid, sixaxis_handle);
    is_firmware_available = sixaxis_properties.is_firmware_update_available != 0;
    return ResultSuccess;
}

Result NPad::ResetIsSixAxisSensorDeviceNewlyAssigned(
    u64 aruid, const Core::HID::SixAxisSensorHandle& sixaxis_handle) {
    const auto is_valid = IsSixaxisHandleValid(sixaxis_handle);
    if (is_valid.IsError()) {
        LOG_ERROR(Service_HID, "Invalid handle, error_code={}", is_valid.raw);
        return is_valid;
    }

    auto& sixaxis_properties = GetSixaxisProperties(aruid, sixaxis_handle);
    sixaxis_properties.is_newly_assigned.Assign(0);

    return ResultSuccess;
}

Result NPad::MergeSingleJoyAsDualJoy(u64 aruid, Core::HID::NpadIdType npad_id_1,
                                     Core::HID::NpadIdType npad_id_2) {
    if (!IsNpadIdValid(npad_id_1) || !IsNpadIdValid(npad_id_2)) {
        LOG_ERROR(Service_HID, "Invalid NpadIdType npad_id_1:{}, npad_id_2:{}", npad_id_1,
                  npad_id_2);
        return ResultInvalidNpadId;
    }
    auto& controller_1 = GetControllerFromNpadIdType(aruid, npad_id_1);
    auto& controller_2 = GetControllerFromNpadIdType(aruid, npad_id_2);
    auto controller_style_1 = controller_1.device->GetNpadStyleIndex();
    auto controller_style_2 = controller_2.device->GetNpadStyleIndex();

    // Simplify this code by converting dualjoycon with only a side connected to single joycons
    if (controller_style_1 == Core::HID::NpadStyleIndex::JoyconDual) {
        if (controller_1.is_dual_left_connected && !controller_1.is_dual_right_connected) {
            controller_style_1 = Core::HID::NpadStyleIndex::JoyconLeft;
        }
        if (!controller_1.is_dual_left_connected && controller_1.is_dual_right_connected) {
            controller_style_1 = Core::HID::NpadStyleIndex::JoyconRight;
        }
    }
    if (controller_style_2 == Core::HID::NpadStyleIndex::JoyconDual) {
        if (controller_2.is_dual_left_connected && !controller_2.is_dual_right_connected) {
            controller_style_2 = Core::HID::NpadStyleIndex::JoyconLeft;
        }
        if (!controller_2.is_dual_left_connected && controller_2.is_dual_right_connected) {
            controller_style_2 = Core::HID::NpadStyleIndex::JoyconRight;
        }
    }

    // Invalid merge errors
    if (controller_style_1 == Core::HID::NpadStyleIndex::JoyconDual ||
        controller_style_2 == Core::HID::NpadStyleIndex::JoyconDual) {
        return NpadIsDualJoycon;
    }
    if (controller_style_1 == Core::HID::NpadStyleIndex::JoyconLeft &&
        controller_style_2 == Core::HID::NpadStyleIndex::JoyconLeft) {
        return NpadIsSameType;
    }
    if (controller_style_1 == Core::HID::NpadStyleIndex::JoyconRight &&
        controller_style_2 == Core::HID::NpadStyleIndex::JoyconRight) {
        return NpadIsSameType;
    }

    // These exceptions are handled as if they where dual joycon
    if (controller_style_1 != Core::HID::NpadStyleIndex::JoyconLeft &&
        controller_style_1 != Core::HID::NpadStyleIndex::JoyconRight) {
        return NpadIsDualJoycon;
    }
    if (controller_style_2 != Core::HID::NpadStyleIndex::JoyconLeft &&
        controller_style_2 != Core::HID::NpadStyleIndex::JoyconRight) {
        return NpadIsDualJoycon;
    }

    // Disconnect the joycons and connect them as dual joycon at the first index.
    DisconnectNpad(aruid, npad_id_1);
    DisconnectNpad(aruid, npad_id_2);
    controller_1.is_dual_left_connected = true;
    controller_1.is_dual_right_connected = true;
    AddNewControllerAt(aruid, Core::HID::NpadStyleIndex::JoyconDual, npad_id_1);
    return ResultSuccess;
}

Result NPad::StartLrAssignmentMode(u64 aruid) {
    std::scoped_lock lock{mutex};
    bool is_enabled{};
    Result result = npad_resource.GetLrAssignmentMode(is_enabled, aruid);
    if (result.IsSuccess() && is_enabled == false) {
        result = npad_resource.SetLrAssignmentMode(aruid, true);
    }
    return result;
}

Result NPad::StopLrAssignmentMode(u64 aruid) {
    std::scoped_lock lock{mutex};
    bool is_enabled{};
    Result result = npad_resource.GetLrAssignmentMode(is_enabled, aruid);
    if (result.IsSuccess() && is_enabled == true) {
        result = npad_resource.SetLrAssignmentMode(aruid, false);
    }
    return result;
}

Result NPad::SwapNpadAssignment(u64 aruid, Core::HID::NpadIdType npad_id_1,
                                Core::HID::NpadIdType npad_id_2) {
    if (!IsNpadIdValid(npad_id_1) || !IsNpadIdValid(npad_id_2)) {
        LOG_ERROR(Service_HID, "Invalid NpadIdType npad_id_1:{}, npad_id_2:{}", npad_id_1,
                  npad_id_2);
        return ResultInvalidNpadId;
    }
    if (npad_id_1 == Core::HID::NpadIdType::Handheld ||
        npad_id_2 == Core::HID::NpadIdType::Handheld || npad_id_1 == Core::HID::NpadIdType::Other ||
        npad_id_2 == Core::HID::NpadIdType::Other) {
        return ResultSuccess;
    }
    const auto& controller_1 = GetControllerFromNpadIdType(aruid, npad_id_1).device;
    const auto& controller_2 = GetControllerFromNpadIdType(aruid, npad_id_2).device;
    const auto type_index_1 = controller_1->GetNpadStyleIndex();
    const auto type_index_2 = controller_2->GetNpadStyleIndex();
    const auto is_connected_1 = controller_1->IsConnected();
    const auto is_connected_2 = controller_2->IsConnected();

    if (!npad_resource.IsControllerSupported(aruid, type_index_1) && is_connected_1) {
        return ResultNpadNotConnected;
    }
    if (!npad_resource.IsControllerSupported(aruid, type_index_2) && is_connected_2) {
        return ResultNpadNotConnected;
    }

    UpdateControllerAt(aruid, type_index_2, npad_id_1, is_connected_2);
    UpdateControllerAt(aruid, type_index_1, npad_id_2, is_connected_1);

    return ResultSuccess;
}

Result NPad::IsUnintendedHomeButtonInputProtectionEnabled(bool& out_is_enabled, u64 aruid,
                                                          Core::HID::NpadIdType npad_id) const {
    std::scoped_lock lock{mutex};
    return npad_resource.GetHomeProtectionEnabled(out_is_enabled, aruid, npad_id);
}

Result NPad::EnableUnintendedHomeButtonInputProtection(u64 aruid, Core::HID::NpadIdType npad_id,
                                                       bool is_enabled) {
    std::scoped_lock lock{mutex};
    return npad_resource.SetHomeProtectionEnabled(aruid, npad_id, is_enabled);
}

void NPad::SetNpadAnalogStickUseCenterClamp(u64 aruid, bool is_enabled) {
    std::scoped_lock lock{mutex};
    npad_resource.SetNpadAnalogStickUseCenterClamp(aruid, is_enabled);
}

void NPad::ClearAllConnectedControllers() {
    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        for (auto& controller : controller_data[aruid_index]) {
            if (controller.device->IsConnected() &&
                controller.device->GetNpadStyleIndex() != Core::HID::NpadStyleIndex::None) {
                controller.device->Disconnect();
                controller.device->SetNpadStyleIndex(Core::HID::NpadStyleIndex::None);
            }
        }
    }
}

void NPad::DisconnectAllConnectedControllers() {
    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        for (auto& controller : controller_data[aruid_index]) {
            controller.device->Disconnect();
        }
    }
}

void NPad::ConnectAllDisconnectedControllers() {
    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        for (auto& controller : controller_data[aruid_index]) {
            if (controller.device->GetNpadStyleIndex() != Core::HID::NpadStyleIndex::None &&
                !controller.device->IsConnected()) {
                controller.device->Connect();
            }
        }
    }
}

void NPad::ClearAllControllers() {
    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        for (auto& controller : controller_data[aruid_index]) {
            controller.device->Disconnect();
            controller.device->SetNpadStyleIndex(Core::HID::NpadStyleIndex::None);
        }
    }
}

Core::HID::NpadButton NPad::GetAndResetPressState() {
    return static_cast<Core::HID::NpadButton>(press_state.exchange(0));
}

Result NPad::ApplyNpadSystemCommonPolicy(u64 aruid) {
    std::scoped_lock lock{mutex};
    const Result result = npad_resource.ApplyNpadSystemCommonPolicy(aruid, false);
    if (result.IsSuccess()) {
        OnUpdate({});
    }
    return result;
}

Result NPad::ApplyNpadSystemCommonPolicyFull(u64 aruid) {
    std::scoped_lock lock{mutex};
    const Result result = npad_resource.ApplyNpadSystemCommonPolicy(aruid, true);
    if (result.IsSuccess()) {
        OnUpdate({});
    }
    return result;
}

Result NPad::ClearNpadSystemCommonPolicy(u64 aruid) {
    std::scoped_lock lock{mutex};
    const Result result = npad_resource.ClearNpadSystemCommonPolicy(aruid);
    if (result.IsSuccess()) {
        OnUpdate({});
    }
    return result;
}

void NPad::SetRevision(u64 aruid, NpadRevision revision) {
    npad_resource.SetNpadRevision(aruid, revision);
}

NpadRevision NPad::GetRevision(u64 aruid) {
    return npad_resource.GetNpadRevision(aruid);
}

Result NPad::RegisterAppletResourceUserId(u64 aruid) {
    return npad_resource.RegisterAppletResourceUserId(aruid);
}

void NPad::UnregisterAppletResourceUserId(u64 aruid) {
    // TODO: Remove this once abstract pad is emulated properly
    const auto aruid_index = npad_resource.GetIndexFromAruid(aruid);
    for (auto& controller : controller_data[aruid_index]) {
        controller.is_active = false;
        controller.is_connected = false;
        controller.shared_memory = nullptr;
    }

    npad_resource.UnregisterAppletResourceUserId(aruid);
}

void NPad::SetNpadExternals(std::shared_ptr<AppletResource> resource,
                            std::recursive_mutex* shared_mutex,
                            std::shared_ptr<HandheldConfig> handheld_config,
                            Kernel::KEvent* input_event, std::mutex* input_mutex,
                            std::shared_ptr<Service::Set::ISystemSettingsServer> settings) {
    applet_resource_holder.applet_resource = resource;
    applet_resource_holder.shared_mutex = shared_mutex;
    applet_resource_holder.shared_npad_resource = &npad_resource;
    applet_resource_holder.handheld_config = handheld_config;
    applet_resource_holder.input_event = input_event;
    applet_resource_holder.input_mutex = input_mutex;

    vibration_handler.SetSettingsService(settings);

    for (auto& abstract_pad : abstracted_pads) {
        abstract_pad.SetExternals(&applet_resource_holder, nullptr, nullptr, nullptr, nullptr,
                                  &vibration_handler, &hid_core);
    }
}

NPad::NpadControllerData& NPad::GetControllerFromHandle(
    u64 aruid, const Core::HID::SixAxisSensorHandle& device_handle) {
    const auto npad_id = static_cast<Core::HID::NpadIdType>(device_handle.npad_id);
    return GetControllerFromNpadIdType(aruid, npad_id);
}

const NPad::NpadControllerData& NPad::GetControllerFromHandle(
    u64 aruid, const Core::HID::SixAxisSensorHandle& device_handle) const {
    const auto npad_id = static_cast<Core::HID::NpadIdType>(device_handle.npad_id);
    return GetControllerFromNpadIdType(aruid, npad_id);
}

NPad::NpadControllerData& NPad::GetControllerFromNpadIdType(u64 aruid,
                                                            Core::HID::NpadIdType npad_id) {
    if (!IsNpadIdValid(npad_id)) {
        LOG_ERROR(Service_HID, "Invalid NpadIdType npad_id:{}", npad_id);
        npad_id = Core::HID::NpadIdType::Player1;
    }
    const auto npad_index = NpadIdTypeToIndex(npad_id);
    const auto aruid_index = applet_resource_holder.applet_resource->GetIndexFromAruid(aruid);
    return controller_data[aruid_index][npad_index];
}

const NPad::NpadControllerData& NPad::GetControllerFromNpadIdType(
    u64 aruid, Core::HID::NpadIdType npad_id) const {
    if (!IsNpadIdValid(npad_id)) {
        LOG_ERROR(Service_HID, "Invalid NpadIdType npad_id:{}", npad_id);
        npad_id = Core::HID::NpadIdType::Player1;
    }
    const auto npad_index = NpadIdTypeToIndex(npad_id);
    const auto aruid_index = applet_resource_holder.applet_resource->GetIndexFromAruid(aruid);
    return controller_data[aruid_index][npad_index];
}

Core::HID::SixAxisSensorProperties& NPad::GetSixaxisProperties(
    u64 aruid, const Core::HID::SixAxisSensorHandle& sixaxis_handle) {
    auto& controller = GetControllerFromHandle(aruid, sixaxis_handle);
    switch (sixaxis_handle.npad_type) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::Pokeball:
        return controller.shared_memory->sixaxis_fullkey_properties;
    case Core::HID::NpadStyleIndex::Handheld:
        return controller.shared_memory->sixaxis_handheld_properties;
    case Core::HID::NpadStyleIndex::JoyconDual:
        if (sixaxis_handle.device_index == Core::HID::DeviceIndex::Left) {
            return controller.shared_memory->sixaxis_dual_left_properties;
        }
        return controller.shared_memory->sixaxis_dual_right_properties;
    case Core::HID::NpadStyleIndex::JoyconLeft:
        return controller.shared_memory->sixaxis_left_properties;
    case Core::HID::NpadStyleIndex::JoyconRight:
        return controller.shared_memory->sixaxis_right_properties;
    default:
        return controller.shared_memory->sixaxis_fullkey_properties;
    }
}

const Core::HID::SixAxisSensorProperties& NPad::GetSixaxisProperties(
    u64 aruid, const Core::HID::SixAxisSensorHandle& sixaxis_handle) const {
    const auto& controller = GetControllerFromHandle(aruid, sixaxis_handle);
    switch (sixaxis_handle.npad_type) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::Pokeball:
        return controller.shared_memory->sixaxis_fullkey_properties;
    case Core::HID::NpadStyleIndex::Handheld:
        return controller.shared_memory->sixaxis_handheld_properties;
    case Core::HID::NpadStyleIndex::JoyconDual:
        if (sixaxis_handle.device_index == Core::HID::DeviceIndex::Left) {
            return controller.shared_memory->sixaxis_dual_left_properties;
        }
        return controller.shared_memory->sixaxis_dual_right_properties;
    case Core::HID::NpadStyleIndex::JoyconLeft:
        return controller.shared_memory->sixaxis_left_properties;
    case Core::HID::NpadStyleIndex::JoyconRight:
        return controller.shared_memory->sixaxis_right_properties;
    default:
        return controller.shared_memory->sixaxis_fullkey_properties;
    }
}

AppletDetailedUiType NPad::GetAppletDetailedUiType(Core::HID::NpadIdType npad_id) {
    const auto aruid = applet_resource_holder.applet_resource->GetActiveAruid();
    const auto& shared_memory = GetControllerFromNpadIdType(aruid, npad_id).shared_memory;

    return {
        .ui_variant = 0,
        .footer = shared_memory->applet_footer_type,
    };
}

Result NPad::SetNpadCaptureButtonAssignment(u64 aruid, Core::HID::NpadStyleSet npad_style_set,
                                            Core::HID::NpadButton button_assignment) {
    std::scoped_lock lock{mutex};
    return npad_resource.SetNpadCaptureButtonAssignment(aruid, npad_style_set, button_assignment);
}

Result NPad::ClearNpadCaptureButtonAssignment(u64 aruid) {
    std::scoped_lock lock{mutex};
    return npad_resource.ClearNpadCaptureButtonAssignment(aruid);
}

std::size_t NPad::GetNpadCaptureButtonAssignment(std::span<Core::HID::NpadButton> out_list,
                                                 u64 aruid) const {
    std::scoped_lock lock{mutex};
    return npad_resource.GetNpadCaptureButtonAssignment(out_list, aruid);
}

Result NPad::SetNpadSystemExtStateEnabled(u64 aruid, bool is_enabled) {
    std::scoped_lock lock{mutex};
    const auto result = npad_resource.SetNpadSystemExtStateEnabled(aruid, is_enabled);

    if (result.IsSuccess()) {
        std::scoped_lock shared_lock{*applet_resource_holder.shared_mutex};
        // TODO: abstracted_pad->EnableAppletToGetInput(aruid);
    }

    return result;
}

Result NPad::AssigningSingleOnSlSrPress(u64 aruid, bool is_enabled) {
    std::scoped_lock lock{mutex};
    bool is_currently_enabled{};
    Result result = npad_resource.IsAssigningSingleOnSlSrPressEnabled(is_currently_enabled, aruid);
    if (result.IsSuccess() && is_enabled != is_currently_enabled) {
        result = npad_resource.SetAssigningSingleOnSlSrPress(aruid, is_enabled);
    }
    return result;
}

Result NPad::GetLastActiveNpad(Core::HID::NpadIdType& out_npad_id) const {
    std::scoped_lock lock{mutex};
    out_npad_id = hid_core.GetLastActiveController();
    return ResultSuccess;
}

NpadVibration* NPad::GetVibrationHandler() {
    return &vibration_handler;
}

std::vector<NpadVibrationBase*> NPad::GetAllVibrationDevices() {
    std::vector<NpadVibrationBase*> vibration_devices;

    for (auto& abstract_pad : abstracted_pads) {
        auto* left_device = abstract_pad.GetVibrationDevice(Core::HID::DeviceIndex::Left);
        auto* right_device = abstract_pad.GetVibrationDevice(Core::HID::DeviceIndex::Right);
        auto* n64_device = abstract_pad.GetGCVibrationDevice();
        auto* gc_device = abstract_pad.GetGCVibrationDevice();

        if (left_device != nullptr) {
            vibration_devices.emplace_back(left_device);
        }
        if (right_device != nullptr) {
            vibration_devices.emplace_back(right_device);
        }
        if (n64_device != nullptr) {
            vibration_devices.emplace_back(n64_device);
        }
        if (gc_device != nullptr) {
            vibration_devices.emplace_back(gc_device);
        }
    }

    return vibration_devices;
}

NpadVibrationBase* NPad::GetVibrationDevice(const Core::HID::VibrationDeviceHandle& handle) {
    if (IsVibrationHandleValid(handle).IsError()) {
        return nullptr;
    }

    const auto npad_index = NpadIdTypeToIndex(static_cast<Core::HID::NpadIdType>(handle.npad_id));
    const auto style_inde = static_cast<Core::HID::NpadStyleIndex>(handle.npad_type);
    if (style_inde == Core::HID::NpadStyleIndex::GameCube) {
        return abstracted_pads[npad_index].GetGCVibrationDevice();
    }
    if (style_inde == Core::HID::NpadStyleIndex::N64) {
        return abstracted_pads[npad_index].GetN64VibrationDevice();
    }
    return abstracted_pads[npad_index].GetVibrationDevice(handle.device_index);
}

NpadN64VibrationDevice* NPad::GetN64VibrationDevice(
    const Core::HID::VibrationDeviceHandle& handle) {
    if (IsVibrationHandleValid(handle).IsError()) {
        return nullptr;
    }

    const auto npad_index = NpadIdTypeToIndex(static_cast<Core::HID::NpadIdType>(handle.npad_id));
    const auto style_inde = static_cast<Core::HID::NpadStyleIndex>(handle.npad_type);
    if (style_inde != Core::HID::NpadStyleIndex::N64) {
        return nullptr;
    }
    return abstracted_pads[npad_index].GetN64VibrationDevice();
}

NpadVibrationDevice* NPad::GetNSVibrationDevice(const Core::HID::VibrationDeviceHandle& handle) {
    if (IsVibrationHandleValid(handle).IsError()) {
        return nullptr;
    }

    const auto npad_index = NpadIdTypeToIndex(static_cast<Core::HID::NpadIdType>(handle.npad_id));
    const auto style_inde = static_cast<Core::HID::NpadStyleIndex>(handle.npad_type);
    if (style_inde == Core::HID::NpadStyleIndex::GameCube ||
        style_inde == Core::HID::NpadStyleIndex::N64) {
        return nullptr;
    }

    return abstracted_pads[npad_index].GetVibrationDevice(handle.device_index);
}

NpadGcVibrationDevice* NPad::GetGcVibrationDevice(const Core::HID::VibrationDeviceHandle& handle) {
    if (IsVibrationHandleValid(handle).IsError()) {
        return nullptr;
    }

    const auto npad_index = NpadIdTypeToIndex(static_cast<Core::HID::NpadIdType>(handle.npad_id));
    const auto style_inde = static_cast<Core::HID::NpadStyleIndex>(handle.npad_type);
    if (style_inde != Core::HID::NpadStyleIndex::GameCube) {
        return nullptr;
    }
    return abstracted_pads[npad_index].GetGCVibrationDevice();
}

void NPad::UpdateHandheldAbstractState() {
    std::scoped_lock lock{mutex};
    abstracted_pads[NpadIdTypeToIndex(Core::HID::NpadIdType::Handheld)].Update();
}

void NPad::EnableAppletToGetInput(u64 aruid) {
    std::scoped_lock lock{mutex};
    std::scoped_lock shared_lock{*applet_resource_holder.shared_mutex};

    for (auto& abstract_pad : abstracted_pads) {
        abstract_pad.EnableAppletToGetInput(aruid);
    }
}

} // namespace Service::HID
