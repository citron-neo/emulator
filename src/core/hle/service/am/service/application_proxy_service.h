// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service {

namespace AM {

struct Applet;
class IApplicationProxy;
class WindowSystem;

class IApplicationProxyService final : public ServiceFramework<IApplicationProxyService> {
public:
    explicit IApplicationProxyService(Core::System& system_, WindowSystem& window_system);
    ~IApplicationProxyService() override;

private:
    Result OpenApplicationProxy(Out<SharedPointer<IApplicationProxy>> out_application_proxy,
                                ClientProcessId pid, InCopyHandle<Kernel::KProcess> process_handle);

private:
    std::shared_ptr<Applet> GetAppletFromProcessId(ProcessId pid);

    WindowSystem& m_window_system;
};

} // namespace AM
} // namespace Service
