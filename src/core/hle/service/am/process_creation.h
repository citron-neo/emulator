// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>

#include "common/common_types.h"
#include "core/file_sys/vfs/vfs_types.h"

namespace Core {
class System;
}

namespace Loader {
class AppLoader;
enum class ResultStatus : u16;
} // namespace Loader

namespace Service {
class Process;
}

namespace Service::AM {

std::unique_ptr<Process> CreateProcess(Core::System& system, u64 program_id,
                                       u8 minimum_key_generation, u8 maximum_key_generation);
std::unique_ptr<Process> CreateApplicationProcess(std::vector<u8>& out_control,
                                                  std::unique_ptr<Loader::AppLoader>& out_loader,
                                                  Loader::ResultStatus& out_load_result,
                                                  Core::System& system, FileSys::VirtualFile file,
                                                  u64 program_id, u64 program_index);

} // namespace Service::AM
