// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/btm/btm_debug.h"

namespace Service::BTM {

IBtmDebug::IBtmDebug(Core::System& system_) : ServiceFramework{system_, "btm:dbg"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "AcquireDiscoveryEvent"},
        {1, nullptr, "StartDiscovery"},
        {2, nullptr, "CancelDiscovery"},
        {3, nullptr, "GetDeviceProperty"},
        {4, nullptr, "CreateBond"},
        {5, nullptr, "CancelBond"},
        {6, nullptr, "SetTsiMode"},
        {7, nullptr, "GeneralTest"},
        {8, nullptr, "HidConnect"},
        {9, nullptr, "GeneralGet"},
        {10, nullptr, "GetGattClientDisconnectionReason"},
        {11, nullptr, "GetBleConnectionParameter"},
        {12, nullptr, "GetBleConnectionParameterRequest"},
        {13, nullptr, "Unknown13"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IBtmDebug::~IBtmDebug() = default;

} // namespace Service::BTM
