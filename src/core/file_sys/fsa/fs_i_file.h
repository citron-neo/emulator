// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/overflow.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/fs_file.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/fs_operate_range.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_types.h"
#include "core/hle/result.h"

namespace FileSys::Fsa {

class IFile {
public:
    explicit IFile(VirtualFile backend_) : backend(std::move(backend_)) {}
    virtual ~IFile() {}

    Result Read(size_t* out, s64 offset, void* buffer, size_t size, const ReadOption& option) {
        // Check that we have an output pointer
        R_UNLESS(out != nullptr, ResultNullptrArgument);

        // If we have nothing to read, just succeed
        if (size == 0) {
            *out = 0;
            R_SUCCEED();
        }

        // Check that the read is valid
        R_UNLESS(buffer != nullptr, ResultNullptrArgument);
        R_UNLESS(offset >= 0, ResultOutOfRange);
        R_UNLESS(Common::CanAddWithoutOverflow<s64>(offset, size), ResultOutOfRange);

        // Do the read
        R_RETURN(this->DoRead(out, offset, buffer, size, option));
    }

    Result Read(size_t* out, s64 offset, void* buffer, size_t size) {
        R_RETURN(this->Read(out, offset, buffer, size, ReadOption::None));
    }

    Result GetSize(s64* out) {
        R_UNLESS(out != nullptr, ResultNullptrArgument);
        R_RETURN(this->DoGetSize(out));
    }

    Result Flush() {
        R_RETURN(this->DoFlush());
    }

    Result Write(s64 offset, const void* buffer, size_t size, const WriteOption& option) {
        // Handle the zero-size case
        if (size == 0) {
            if (option.HasFlushFlag()) {
                R_TRY(this->Flush());
            }
            R_SUCCEED();
        }

        // Check the write is valid
        R_UNLESS(buffer != nullptr, ResultNullptrArgument);
        R_UNLESS(offset >= 0, ResultOutOfRange);
        R_UNLESS(Common::CanAddWithoutOverflow<s64>(offset, size), ResultOutOfRange);

        R_RETURN(this->DoWrite(offset, buffer, size, option));
    }

    Result SetSize(s64 size) {
        R_UNLESS(size >= 0, ResultOutOfRange);
        R_RETURN(this->DoSetSize(size));
    }

    Result OperateRange(void* dst, size_t dst_size, OperationId op_id, s64 offset, s64 size,
                        const void* src, size_t src_size) {
        R_RETURN(this->DoOperateRange(dst, dst_size, op_id, offset, size, src, src_size));
    }

    Result OperateRange(OperationId op_id, s64 offset, s64 size) {
        R_RETURN(this->DoOperateRange(nullptr, 0, op_id, offset, size, nullptr, 0));
    }

protected:
    Result DryRead(size_t* out, s64 offset, size_t size, const ReadOption& option,
                   OpenMode open_mode) {
        // Check that we can read
        R_UNLESS(static_cast<u32>(open_mode & OpenMode::Read) != 0, ResultReadNotPermitted);

        // Get the file size, and validate our offset
        s64 file_size = 0;
        R_TRY(this->DoGetSize(std::addressof(file_size)));
        R_UNLESS(offset <= file_size, ResultOutOfRange);

        *out = static_cast<size_t>(std::min(file_size - offset, static_cast<s64>(size)));
        R_SUCCEED();
    }

    Result DrySetSize(s64 size, OpenMode open_mode) {
        // Check that we can write
        R_UNLESS(static_cast<u32>(open_mode & OpenMode::Write) != 0, ResultWriteNotPermitted);
        R_SUCCEED();
    }

    Result DryWrite(bool* out_append, s64 offset, size_t size, const WriteOption& option,
                    OpenMode open_mode) {
        // Check that we can write
        R_UNLESS(static_cast<u32>(open_mode & OpenMode::Write) != 0, ResultWriteNotPermitted);

        // Get the file size
        s64 file_size = 0;
        R_TRY(this->DoGetSize(&file_size));

        // Determine if we need to append
        *out_append = false;
        if (file_size < offset + static_cast<s64>(size)) {
            R_UNLESS(static_cast<u32>(open_mode & OpenMode::AllowAppend) != 0,
                     ResultFileExtensionWithoutOpenModeAllowAppend);
            *out_append = true;
        }

        R_SUCCEED();
    }

private:
    Result DoRead(size_t* out, s64 offset, void* buffer, size_t size, const ReadOption& option) {
        const auto read_size = backend->Read(static_cast<u8*>(buffer), size, offset);
        *out = read_size;

        R_SUCCEED();
    }

    Result DoGetSize(s64* out) {
        *out = backend->GetSize();
        R_SUCCEED();
    }

    Result DoFlush() {
        // Exists for SDK compatibiltity -- No need to flush file.
        R_SUCCEED();
    }

    Result DoWrite(s64 offset, const void* buffer, size_t size, const WriteOption& option) {
        const std::size_t written = backend->Write(static_cast<const u8*>(buffer), size, offset);

        ASSERT_MSG(written == size,
                   "Could not write all bytes to file (requested={:016X}, actual={:016X}).", size,
                   written);

        R_SUCCEED();
    }

    Result DoSetSize(s64 size) {
        backend->Resize(size);
        R_SUCCEED();
    }

    Result DoOperateRange(void* dst, size_t dst_size, OperationId op_id, s64 offset, s64 size,
                          const void* src, size_t src_size) {
        R_THROW(ResultNotImplemented);
    }

    VirtualFile backend;
};

} // namespace FileSys::Fsa
