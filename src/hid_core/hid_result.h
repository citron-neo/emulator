// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/result.h"

namespace Service::HID {

constexpr Result PalmaResultSuccess{ErrorModule::HID, 0};

constexpr Result ResultTouchNotInitialized{ErrorModule::HID, 41};
constexpr Result ResultTouchOverflow{ErrorModule::HID, 42};

constexpr Result NpadInvalidHandle{ErrorModule::HID, 100};
constexpr Result NpadDeviceIndexOutOfRange{ErrorModule::HID, 107};

constexpr Result ResultVibrationNotInitialized{ErrorModule::HID, 121};
constexpr Result ResultVibrationInvalidStyleIndex{ErrorModule::HID, 122};
constexpr Result ResultVibrationInvalidNpadId{ErrorModule::HID, 123};
constexpr Result ResultVibrationDeviceIndexOutOfRange{ErrorModule::HID, 124};
constexpr Result ResultVibrationStrengthOutOfRange{ErrorModule::HID, 126};
constexpr Result ResultVibrationArraySizeMismatch{ErrorModule::HID, 131};

constexpr Result InvalidSixAxisFusionRange{ErrorModule::HID, 423};

constexpr Result ResultNfcIsNotReady{ErrorModule::HID, 461};
constexpr Result ResultNfcXcdHandleIsNotInitialized{ErrorModule::HID, 464};
constexpr Result ResultIrSensorIsNotReady{ErrorModule::HID, 501};

constexpr Result ResultGestureOverflow{ErrorModule::HID, 522};
constexpr Result ResultGestureNotInitialized{ErrorModule::HID, 523};

constexpr Result ResultMcuIsNotReady{ErrorModule::HID, 541};

constexpr Result NpadIsDualJoycon{ErrorModule::HID, 601};
constexpr Result NpadIsSameType{ErrorModule::HID, 602};
constexpr Result ResultNpadIsNotProController{ErrorModule::HID, 604};

constexpr Result ResultInvalidNpadId{ErrorModule::HID, 709};
constexpr Result ResultNpadNotConnected{ErrorModule::HID, 710};
constexpr Result ResultNpadHandlerOverflow{ErrorModule::HID, 711};
constexpr Result ResultNpadHandlerNotInitialized{ErrorModule::HID, 712};
constexpr Result ResultInvalidArraySize{ErrorModule::HID, 715};
constexpr Result ResultUndefinedStyleset{ErrorModule::HID, 716};
constexpr Result ResultMultipleStyleSetSelected{ErrorModule::HID, 717};

constexpr Result ResultAppletResourceOverflow{ErrorModule::HID, 1041};
constexpr Result ResultAppletResourceNotInitialized{ErrorModule::HID, 1042};
constexpr Result ResultSharedMemoryNotInitialized{ErrorModule::HID, 1043};
constexpr Result ResultAruidNoAvailableEntries{ErrorModule::HID, 1044};
constexpr Result ResultAruidAlreadyRegistered{ErrorModule::HID, 1046};
constexpr Result ResultAruidNotRegistered{ErrorModule::HID, 1047};

constexpr Result ResultNpadResourceOverflow{ErrorModule::HID, 2001};
constexpr Result ResultNpadResourceNotInitialized{ErrorModule::HID, 2002};

constexpr Result InvalidPalmaHandle{ErrorModule::HID, 3302};

} // namespace Service::HID

namespace Service::IRS {

constexpr Result InvalidProcessorState{ErrorModule::Irsensor, 78};
constexpr Result InvalidIrCameraHandle{ErrorModule::Irsensor, 204};

} // namespace Service::IRS
