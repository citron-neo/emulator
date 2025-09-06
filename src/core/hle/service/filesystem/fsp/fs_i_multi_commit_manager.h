// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/file_sys/vfs/vfs.h"
#include "core/hle/service/service.h"

namespace Service::FileSystem {

class IMultiCommitManager final : public ServiceFramework<IMultiCommitManager> {
public:
    explicit IMultiCommitManager(Core::System& system_);
    ~IMultiCommitManager() override;

private:
    Result Add(std::shared_ptr<IFileSystem> filesystem);
    Result Commit();

    FileSys::VirtualFile backend;
};

} // namespace Service::FileSystem
