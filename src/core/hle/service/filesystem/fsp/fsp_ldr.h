// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/file_sys/fssrv/fssrv_sf_path.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::FileSystem {

class IFileSystem;

class FSP_LDR final : public ServiceFramework<FSP_LDR> {
public:
    explicit FSP_LDR(Core::System& system_);
    ~FSP_LDR() override;

private:
    Result OpenCodeFileSystem(OutInterface<IFileSystem> out_interface, u64 program_id,
                              const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result IsArchivedProgram(u64 program_id, Out<bool> out_is_archived);
    Result SetCurrentProcess(ClientProcessId process_id);
};

} // namespace Service::FileSystem
