// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core::Memory {
class Memory;
}

namespace Kernel {
class KTransferMemory;
}

namespace Service::AM {

class LibraryAppletStorage {
public:
    virtual ~LibraryAppletStorage();
    virtual Result Read(s64 offset, void* buffer, size_t size) = 0;
    virtual Result Write(s64 offset, const void* buffer, size_t size) = 0;
    virtual s64 GetSize() = 0;
    virtual Kernel::KTransferMemory* GetHandle() = 0;

    std::vector<u8> GetData();
};

std::shared_ptr<LibraryAppletStorage> CreateStorage(std::vector<u8>&& data);
std::shared_ptr<LibraryAppletStorage> CreateTransferMemoryStorage(Core::Memory::Memory& memory,
                                                                  Kernel::KTransferMemory* trmem,
                                                                  bool is_writable, s64 size);
std::shared_ptr<LibraryAppletStorage> CreateHandleStorage(Core::Memory::Memory& memory,
                                                          Kernel::KTransferMemory* trmem, s64 size);

} // namespace Service::AM
