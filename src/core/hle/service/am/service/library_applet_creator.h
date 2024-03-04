// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/am/am_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;
class ILibraryAppletAccessor;
class IStorage;
class WindowSystem;

class ILibraryAppletCreator final : public ServiceFramework<ILibraryAppletCreator> {
public:
    explicit ILibraryAppletCreator(Core::System& system_, std::shared_ptr<Applet> applet,
                                   WindowSystem& window_system);
    ~ILibraryAppletCreator() override;

private:
    Result CreateLibraryApplet(
        Out<SharedPointer<ILibraryAppletAccessor>> out_library_applet_accessor, AppletId applet_id,
        LibraryAppletMode library_applet_mode);
    Result CreateStorage(Out<SharedPointer<IStorage>> out_storage, s64 size);
    Result CreateTransferMemoryStorage(
        Out<SharedPointer<IStorage>> out_storage, bool is_writable, s64 size,
        InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle);
    Result CreateHandleStorage(Out<SharedPointer<IStorage>> out_storage, s64 size,
                               InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle);

    WindowSystem& m_window_system;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
