// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/filesystem/fsp/save_data_transfer_prohibiter.h"

namespace Service::FileSystem {

ISaveDataTransferProhibiter::ISaveDataTransferProhibiter(Core::System& system_)
    : ServiceFramework{system_, "ISaveDataTransferProhibiter"} {}

ISaveDataTransferProhibiter::~ISaveDataTransferProhibiter() = default;

} // namespace Service::FileSystem
