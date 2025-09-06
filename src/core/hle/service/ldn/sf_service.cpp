// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/ldn/sf_service.h"

namespace Service::LDN {

ISfService::ISfService(Core::System& system_) : ServiceFramework{system_, "ISfService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
            {0, nullptr, "Initialize"},
            {256, nullptr, "AttachNetworkInterfaceStateChangeEvent"},
            {264, nullptr, "GetNetworkInterfaceLastError"},
            {272, nullptr, "GetRole"},
            {280, nullptr, "GetAdvertiseData"},
            {288, nullptr, "GetGroupInfo"},
            {296, nullptr, "GetGroupInfo2"},
            {304, nullptr, "GetGroupOwner"},
            {312, nullptr, "GetIpConfig"},
            {320, nullptr, "GetLinkLevel"},
            {512, nullptr, "Scan"},
            {768, nullptr, "CreateGroup"},
            {776, nullptr, "DestroyGroup"},
            {784, nullptr, "SetAdvertiseData"},
            {1536, nullptr, "SendToOtherGroup"},
            {1544, nullptr, "RecvFromOtherGroup"},
            {1552, nullptr, "AddAcceptableGroupId"},
            {1560, nullptr, "ClearAcceptableGroupId"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISfService::~ISfService() = default;

} // namespace Service::LDN
