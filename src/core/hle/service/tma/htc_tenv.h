// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Service::TMA {

class IService final : public ServiceFramework<IService> {
public:
    explicit IService(Core::System& system_);
    ~IService() override;

private:
    Result GetVariable(OutBuffer<BufferAttr_HipcMapAlias> out_value,
                      InBuffer<BufferAttr_HipcMapAlias> name);
    Result GetVariableLength(Out<u32> out_length,
                           InBuffer<BufferAttr_HipcMapAlias> name);
    Result WaitUntilVariableAvailable(InBuffer<BufferAttr_HipcMapAlias> name);

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* variable_available_event{};
};

class IServiceManager final : public ServiceFramework<IServiceManager> {
public:
    explicit IServiceManager(Core::System& system_);
    ~IServiceManager() override;

private:
    Result GetServiceInterface(OutInterface<IService> out_service);
};

} // namespace Service::TMA