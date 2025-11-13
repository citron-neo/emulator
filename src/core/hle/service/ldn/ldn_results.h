// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/result.h"

namespace Service::LDN {

constexpr Result ResultAdvertiseDataTooLarge{ErrorModule::LDN, 10};
constexpr Result ResultAuthenticationFailed{ErrorModule::LDN, 20};
constexpr Result ResultDisabled{ErrorModule::LDN, 22};
constexpr Result ResultAirplaneModeEnabled{ErrorModule::LDN, 23};
constexpr Result ResultInvalidNodeCount{ErrorModule::LDN, 30};
constexpr Result ResultConnectionFailed{ErrorModule::LDN, 31};
constexpr Result ResultBadState{ErrorModule::LDN, 32};
constexpr Result ResultNoIpAddress{ErrorModule::LDN, 33};
constexpr Result ResultInvalidBufferCount{ErrorModule::LDN, 50};
constexpr Result ResultAccessPointConnectionFailed{ErrorModule::LDN, 65};
constexpr Result ResultAuthenticationTimeout{ErrorModule::LDN, 66};
constexpr Result ResultMaximumNodeCount{ErrorModule::LDN, 67};
constexpr Result ResultBadInput{ErrorModule::LDN, 96};
constexpr Result ResultLocalCommunicationIdNotFound{ErrorModule::LDN, 97};
constexpr Result ResultLocalCommunicationVersionTooLow{ErrorModule::LDN, 113};
constexpr Result ResultLocalCommunicationVersionTooHigh{ErrorModule::LDN, 114};

// Module 38 error codes - Unknown/undefined module
// These are stubbed to prevent crashes during multiplayer
// Error code format: 2038-XXXX where XXXX is the description
constexpr Result ResultModule38Error2618{ErrorModule::Module38, 2618}; // Reported during multiplayer
constexpr Result ResultModule38Generic{ErrorModule::Module38, 0}; // Generic module 38 error
constexpr Result ResultModule38NetworkError{ErrorModule::Module38, 100}; // Network-related
constexpr Result ResultModule38ConnectionFailed{ErrorModule::Module38, 200}; // Connection failure
constexpr Result ResultModule38Timeout{ErrorModule::Module38, 300}; // Operation timeout

// Module 56 error codes - Unknown/undefined module
// Error code format: 2056-XXXX where XXXX is the description
// Error code 2056-2306 (0x00120438) has been reported by Minecraft
constexpr Result ResultModule56Generic{ErrorModule::Module56, 0}; // Generic module 56 error
constexpr Result ResultModule56Error2306{ErrorModule::Module56, 2306}; // Reported by Minecraft
constexpr Result ResultModule56NetworkError{ErrorModule::Module56, 100}; // Network-related
constexpr Result ResultModule56ConnectionFailed{ErrorModule::Module56, 200}; // Connection failure
constexpr Result ResultModule56Timeout{ErrorModule::Module56, 300}; // Operation timeout

} // namespace Service::LDN
