// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/library_applet_storage.h"
#include "core/memory.h"

namespace Service::AM {

namespace {

Result ValidateOffset(s64 offset, size_t size, size_t data_size) {
    R_UNLESS(offset >= 0, AM::ResultInvalidOffset);

    const size_t begin = offset;
    const size_t end = begin + size;

    R_UNLESS(begin <= end && end <= data_size, AM::ResultInvalidOffset);
    R_SUCCEED();
}

class BufferLibraryAppletStorage final : public LibraryAppletStorage {
public:
    explicit BufferLibraryAppletStorage(std::vector<u8>&& data) : m_data(std::move(data)) {}
    ~BufferLibraryAppletStorage() = default;

    Result Read(s64 offset, void* buffer, size_t size) override {
        R_TRY(ValidateOffset(offset, size, m_data.size()));

        std::memcpy(buffer, m_data.data() + offset, size);

        R_SUCCEED();
    }

    Result Write(s64 offset, const void* buffer, size_t size) override {
        R_TRY(ValidateOffset(offset, size, m_data.size()));

        std::memcpy(m_data.data() + offset, buffer, size);

        R_SUCCEED();
    }

    s64 GetSize() override {
        return m_data.size();
    }

    Kernel::KTransferMemory* GetHandle() override {
        return nullptr;
    }

private:
    std::vector<u8> m_data;
};

class TransferMemoryLibraryAppletStorage : public LibraryAppletStorage {
public:
    explicit TransferMemoryLibraryAppletStorage(Core::Memory::Memory& memory,
                                                Kernel::KTransferMemory* trmem, bool is_writable,
                                                s64 size)
        : m_memory(memory), m_trmem(trmem), m_is_writable(is_writable), m_size(size) {
        m_trmem->Open();
    }

    ~TransferMemoryLibraryAppletStorage() {
        m_trmem->Close();
        m_trmem = nullptr;
    }

    Result Read(s64 offset, void* buffer, size_t size) override {
        R_TRY(ValidateOffset(offset, size, m_size));

        m_memory.ReadBlock(m_trmem->GetSourceAddress() + offset, buffer, size);

        R_SUCCEED();
    }

    Result Write(s64 offset, const void* buffer, size_t size) override {
        R_UNLESS(m_is_writable, ResultUnknown);
        R_TRY(ValidateOffset(offset, size, m_size));

        m_memory.WriteBlock(m_trmem->GetSourceAddress() + offset, buffer, size);

        R_SUCCEED();
    }

    s64 GetSize() override {
        return m_size;
    }

    Kernel::KTransferMemory* GetHandle() override {
        return nullptr;
    }

protected:
    Core::Memory::Memory& m_memory;
    Kernel::KTransferMemory* m_trmem;
    bool m_is_writable;
    s64 m_size;
};

class HandleLibraryAppletStorage : public TransferMemoryLibraryAppletStorage {
public:
    explicit HandleLibraryAppletStorage(Core::Memory::Memory& memory,
                                        Kernel::KTransferMemory* trmem, s64 size)
        : TransferMemoryLibraryAppletStorage(memory, trmem, true, size) {}
    ~HandleLibraryAppletStorage() = default;

    Kernel::KTransferMemory* GetHandle() override {
        return m_trmem;
    }
};

} // namespace

LibraryAppletStorage::~LibraryAppletStorage() = default;

std::vector<u8> LibraryAppletStorage::GetData() {
    std::vector<u8> data(this->GetSize());
    this->Read(0, data.data(), data.size());
    return data;
}

std::shared_ptr<LibraryAppletStorage> CreateStorage(std::vector<u8>&& data) {
    return std::make_shared<BufferLibraryAppletStorage>(std::move(data));
}

std::shared_ptr<LibraryAppletStorage> CreateTransferMemoryStorage(Core::Memory::Memory& memory,
                                                                  Kernel::KTransferMemory* trmem,
                                                                  bool is_writable, s64 size) {
    return std::make_shared<TransferMemoryLibraryAppletStorage>(memory, trmem, is_writable, size);
}

std::shared_ptr<LibraryAppletStorage> CreateHandleStorage(Core::Memory::Memory& memory,
                                                          Kernel::KTransferMemory* trmem,
                                                          s64 size) {
    return std::make_shared<HandleLibraryAppletStorage>(memory, trmem, size);
}

} // namespace Service::AM
