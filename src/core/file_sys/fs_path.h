// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/alignment.h"
#include "common/common_funcs.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/fs_memory_management.h"
#include "core/file_sys/fs_path_utility.h"
#include "core/file_sys/fs_string_util.h"
#include "core/hle/result.h"

namespace FileSys {
class DirectoryPathParser;

class Path {
    CITRON_NON_COPYABLE(Path);
    CITRON_NON_MOVEABLE(Path);

private:
    static constexpr const char* EmptyPath = "";
    static constexpr size_t WriteBufferAlignmentLength = 8;

private:
    friend class DirectoryPathParser;

public:
    class WriteBuffer {
        CITRON_NON_COPYABLE(WriteBuffer);

    private:
        char* m_buffer;
        size_t m_length_and_is_normalized;

    public:
        constexpr WriteBuffer() : m_buffer(nullptr), m_length_and_is_normalized(0) {}

        constexpr ~WriteBuffer() {
            if (m_buffer != nullptr) {
                Deallocate(m_buffer, this->GetLength());
                this->ResetBuffer();
            }
        }

        constexpr WriteBuffer(WriteBuffer&& rhs)
            : m_buffer(rhs.m_buffer), m_length_and_is_normalized(rhs.m_length_and_is_normalized) {
            rhs.ResetBuffer();
        }

        constexpr WriteBuffer& operator=(WriteBuffer&& rhs) {
            if (m_buffer != nullptr) {
                Deallocate(m_buffer, this->GetLength());
            }

            m_buffer = rhs.m_buffer;
            m_length_and_is_normalized = rhs.m_length_and_is_normalized;

            rhs.ResetBuffer();

            return *this;
        }

        constexpr void ResetBuffer() {
            m_buffer = nullptr;
            this->SetLength(0);
        }

        constexpr char* Get() const {
            return m_buffer;
        }

        constexpr size_t GetLength() const {
            return m_length_and_is_normalized >> 1;
        }

        constexpr bool IsNormalized() const {
            return static_cast<bool>(m_length_and_is_normalized & 1);
        }

        constexpr void SetNormalized() {
            m_length_and_is_normalized |= static_cast<size_t>(1);
        }

        constexpr void SetNotNormalized() {
            m_length_and_is_normalized &= ~static_cast<size_t>(1);
        }

    private:
        constexpr WriteBuffer(char* buffer, size_t length)
            : m_buffer(buffer), m_length_and_is_normalized(0) {
            this->SetLength(length);
        }

    public:
        static WriteBuffer Make(size_t length) {
            if (void* alloc = Allocate(length); alloc != nullptr) {
                return WriteBuffer(static_cast<char*>(alloc), length);
            } else {
                return WriteBuffer();
            }
        }

    private:
        constexpr void SetLength(size_t size) {
            m_length_and_is_normalized = (m_length_and_is_normalized & 1) | (size << 1);
        }
    };

private:
    const char* m_str;
    WriteBuffer m_write_buffer;

public:
    constexpr Path() : m_str(EmptyPath), m_write_buffer() {}

    constexpr Path(const char* s) : m_str(s), m_write_buffer() {
        m_write_buffer.SetNormalized();
    }

    constexpr ~Path() = default;

    constexpr Result SetShallowBuffer(const char* buffer) {
        // Check pre-conditions
        ASSERT(m_write_buffer.GetLength() == 0);

        // Check the buffer is valid
        R_UNLESS(buffer != nullptr, ResultNullptrArgument);

        // Set buffer
        this->SetReadOnlyBuffer(buffer);

        // Note that we're normalized
        this->SetNormalized();

        R_SUCCEED();
    }

    constexpr const char* GetString() const {
        // Check pre-conditions
        ASSERT(this->IsNormalized());

        return m_str;
    }

    constexpr size_t GetLength() const {
        if (std::is_constant_evaluated()) {
            return Strlen(this->GetString());
        } else {
            return std::strlen(this->GetString());
        }
    }

    constexpr bool IsEmpty() const {
        return *m_str == '\x00';
    }

    constexpr bool IsMatchHead(const char* p, size_t len) const {
        return Strncmp(this->GetString(), p, len) == 0;
    }

    Result Initialize(const Path& rhs) {
        // Check the other path is normalized
        const bool normalized = rhs.IsNormalized();
        R_UNLESS(normalized, ResultNotNormalized);

        // Allocate buffer for our path
        const auto len = rhs.GetLength();
        R_TRY(this->Preallocate(len + 1));

        // Copy the path
        const size_t copied = Strlcpy<char>(m_write_buffer.Get(), rhs.GetString(), len + 1);
        R_UNLESS(copied == len, ResultUnexpectedInPathA);

        // Set normalized
        this->SetNormalized();
        R_SUCCEED();
    }

    Result Initialize(const char* path, size_t len) {
        // Check the path is valid
        R_UNLESS(path != nullptr, ResultNullptrArgument);

        // Initialize
        R_TRY(this->InitializeImpl(path, len));

        // Set not normalized
        this->SetNotNormalized();

        R_SUCCEED();
    }

    Result Initialize(const char* path) {
        // Check the path is valid
        R_UNLESS(path != nullptr, ResultNullptrArgument);

        R_RETURN(this->Initialize(path, std::strlen(path)));
    }

    Result InitializeWithReplaceBackslash(const char* path) {
        // Check the path is valid
        R_UNLESS(path != nullptr, ResultNullptrArgument);

        // Initialize
        R_TRY(this->InitializeImpl(path, std::strlen(path)));

        // Replace slashes as desired
        if (const auto write_buffer_length = m_write_buffer.GetLength(); write_buffer_length > 1) {
            Replace(m_write_buffer.Get(), write_buffer_length - 1, '\\', '/');
        }

        // Set not normalized
        this->SetNotNormalized();

        R_SUCCEED();
    }

    Result InitializeWithReplaceForwardSlashes(const char* path) {
        // Check the path is valid
        R_UNLESS(path != nullptr, ResultNullptrArgument);

        // Initialize
        R_TRY(this->InitializeImpl(path, std::strlen(path)));

        // Replace slashes as desired
        if (m_write_buffer.GetLength() > 1) {
            if (auto* p = m_write_buffer.Get(); p[0] == '/' && p[1] == '/') {
                p[0] = '\\';
                p[1] = '\\';
            }
        }

        // Set not normalized
        this->SetNotNormalized();

        R_SUCCEED();
    }

    Result InitializeWithNormalization(const char* path, size_t size) {
        // Check the path is valid
        R_UNLESS(path != nullptr, ResultNullptrArgument);

        // Initialize
        R_TRY(this->InitializeImpl(path, size));

        // Set not normalized
        this->SetNotNormalized();

        // Perform normalization
        PathFlags path_flags;
        if (IsPathRelative(m_str)) {
            path_flags.AllowRelativePath();
        } else if (IsWindowsPath(m_str, true)) {
            path_flags.AllowWindowsPath();
        } else {
            /* NOTE: In this case, Nintendo checks is normalized, then sets is normalized, then
             * returns success. */
            /* This seems like a bug. */
            size_t dummy;
            bool normalized;
            R_TRY(PathFormatter::IsNormalized(std::addressof(normalized), std::addressof(dummy),
                                              m_str));

            this->SetNormalized();
            R_SUCCEED();
        }

        // Normalize
        R_TRY(this->Normalize(path_flags));

        this->SetNormalized();
        R_SUCCEED();
    }

    Result InitializeWithNormalization(const char* path) {
        // Check the path is valid
        R_UNLESS(path != nullptr, ResultNullptrArgument);

        R_RETURN(this->InitializeWithNormalization(path, std::strlen(path)));
    }

    Result InitializeAsEmpty() {
        // Clear our buffer
        this->ClearBuffer();

        // Set normalized
        this->SetNormalized();

        R_SUCCEED();
    }

    Result AppendChild(const char* child) {
        // Check the path is valid
        R_UNLESS(child != nullptr, ResultNullptrArgument);

        // Basic checks. If we have a path and the child is empty, we have nothing to do
        const char* c = child;
        if (m_str[0]) {
            // Skip an early separator
            if (*c == '/') {
                ++c;
            }

            R_SUCCEED_IF(*c == '\x00');
        }

        // If we don't have a string, we can just initialize
        auto cur_len = std::strlen(m_str);
        if (cur_len == 0) {
            R_RETURN(this->Initialize(child));
        }

        // Remove a trailing separator
        if (m_str[cur_len - 1] == '/' || m_str[cur_len - 1] == '\\') {
            --cur_len;
        }

        // Get the child path's length
        auto child_len = std::strlen(c);

        // Reset our write buffer
        WriteBuffer old_write_buffer;
        if (m_write_buffer.Get() != nullptr) {
            old_write_buffer = std::move(m_write_buffer);
            this->ClearBuffer();
        }

        // Pre-allocate the new buffer
        R_TRY(this->Preallocate(cur_len + 1 + child_len + 1));

        // Get our write buffer
        auto* dst = m_write_buffer.Get();
        if (old_write_buffer.Get() != nullptr && cur_len > 0) {
            Strlcpy<char>(dst, old_write_buffer.Get(), cur_len + 1);
        }

        // Add separator
        dst[cur_len] = '/';

        // Copy the child path
        const size_t copied = Strlcpy<char>(dst + cur_len + 1, c, child_len + 1);
        R_UNLESS(copied == child_len, ResultUnexpectedInPathA);

        R_SUCCEED();
    }

    Result AppendChild(const Path& rhs) {
        R_RETURN(this->AppendChild(rhs.GetString()));
    }

    Result Combine(const Path& parent, const Path& child) {
        // Get the lengths
        const auto p_len = parent.GetLength();
        const auto c_len = child.GetLength();

        // Allocate our buffer
        R_TRY(this->Preallocate(p_len + c_len + 1));

        // Initialize as parent
        R_TRY(this->Initialize(parent));

        // If we're empty, we can just initialize as child
        if (this->IsEmpty()) {
            R_TRY(this->Initialize(child));
        } else {
            // Otherwise, we should append the child
            R_TRY(this->AppendChild(child));
        }

        R_SUCCEED();
    }

    Result RemoveChild() {
        // If we don't have a write-buffer, ensure that we have one
        if (m_write_buffer.Get() == nullptr) {
            if (const auto len = std::strlen(m_str); len > 0) {
                R_TRY(this->Preallocate(len));
                Strlcpy<char>(m_write_buffer.Get(), m_str, len + 1);
            }
        }

        // Check that it's possible for us to remove a child
        auto* p = m_write_buffer.Get();
        s32 len = static_cast<s32>(std::strlen(p));
        R_UNLESS(len != 1 || (p[0] != '/' && p[0] != '.'), ResultNotImplemented);

        // Handle a trailing separator
        if (len > 0 && (p[len - 1] == '\\' || p[len - 1] == '/')) {
            --len;
        }

        // Remove the child path segment
        while ((--len) >= 0 && p[len]) {
            if (p[len] == '/' || p[len] == '\\') {
                if (len > 0) {
                    p[len] = 0;
                } else {
                    p[1] = 0;
                    len = 1;
                }
                break;
            }
        }

        // Check that length remains > 0
        R_UNLESS(len > 0, ResultNotImplemented);

        R_SUCCEED();
    }

    Result Normalize(const PathFlags& flags) {
        // If we're already normalized, nothing to do
        R_SUCCEED_IF(this->IsNormalized());

        // Check if we're normalized
        bool normalized;
        size_t dummy;
        R_TRY(PathFormatter::IsNormalized(std::addressof(normalized), std::addressof(dummy), m_str,
                                          flags));

        // If we're not normalized, normalize
        if (!normalized) {
            // Determine necessary buffer length
            auto len = m_write_buffer.GetLength();
            if (flags.IsRelativePathAllowed() && IsPathRelative(m_str)) {
                len += 2;
            }
            if (flags.IsWindowsPathAllowed() && IsWindowsPath(m_str, true)) {
                len += 1;
            }

            // Allocate a new buffer
            const size_t size = Common::AlignUp(len, WriteBufferAlignmentLength);
            auto buf = WriteBuffer::Make(size);
            R_UNLESS(buf.Get() != nullptr, ResultAllocationMemoryFailedMakeUnique);

            // Normalize into it
            R_TRY(PathFormatter::Normalize(buf.Get(), size, m_write_buffer.Get(),
                                           m_write_buffer.GetLength(), flags));

            // Set the normalized buffer as our buffer
            this->SetModifiableBuffer(std::move(buf));
        }

        // Set normalized
        this->SetNormalized();
        R_SUCCEED();
    }

private:
    void ClearBuffer() {
        m_write_buffer.ResetBuffer();
        m_str = EmptyPath;
    }

    void SetModifiableBuffer(WriteBuffer&& buffer) {
        // Check pre-conditions
        ASSERT(buffer.Get() != nullptr);
        ASSERT(buffer.GetLength() > 0);
        ASSERT(Common::IsAligned(buffer.GetLength(), WriteBufferAlignmentLength));

        // Get whether we're normalized
        if (m_write_buffer.IsNormalized()) {
            buffer.SetNormalized();
        } else {
            buffer.SetNotNormalized();
        }

        // Set write buffer
        m_write_buffer = std::move(buffer);
        m_str = m_write_buffer.Get();
    }

    constexpr void SetReadOnlyBuffer(const char* buffer) {
        m_str = buffer;
        m_write_buffer.ResetBuffer();
    }

    Result Preallocate(size_t length) {
        // Allocate additional space, if needed
        if (length > m_write_buffer.GetLength()) {
            // Allocate buffer
            const size_t size = Common::AlignUp(length, WriteBufferAlignmentLength);
            auto buf = WriteBuffer::Make(size);
            R_UNLESS(buf.Get() != nullptr, ResultAllocationMemoryFailedMakeUnique);

            // Set write buffer
            this->SetModifiableBuffer(std::move(buf));
        }

        R_SUCCEED();
    }

    Result InitializeImpl(const char* path, size_t size) {
        if (size > 0 && path[0]) {
            // Pre allocate a buffer for the path
            R_TRY(this->Preallocate(size + 1));

            // Copy the path
            const size_t copied = Strlcpy<char>(m_write_buffer.Get(), path, size + 1);
            R_UNLESS(copied >= size, ResultUnexpectedInPathA);
        } else {
            // We can just clear the buffer
            this->ClearBuffer();
        }

        R_SUCCEED();
    }

    constexpr char* GetWriteBuffer() {
        ASSERT(m_write_buffer.Get() != nullptr);
        return m_write_buffer.Get();
    }

    constexpr size_t GetWriteBufferLength() const {
        return m_write_buffer.GetLength();
    }

    constexpr bool IsNormalized() const {
        return m_write_buffer.IsNormalized();
    }

    constexpr void SetNormalized() {
        m_write_buffer.SetNormalized();
    }

    constexpr void SetNotNormalized() {
        m_write_buffer.SetNotNormalized();
    }

public:
    bool operator==(const FileSys::Path& rhs) const {
        return std::strcmp(this->GetString(), rhs.GetString()) == 0;
    }
    bool operator!=(const FileSys::Path& rhs) const {
        return !(*this == rhs);
    }
    bool operator==(const char* p) const {
        return std::strcmp(this->GetString(), p) == 0;
    }
    bool operator!=(const char* p) const {
        return !(*this == p);
    }
};

inline Result SetUpFixedPath(FileSys::Path* out, const char* s) {
    // Verify the path is normalized
    bool normalized;
    size_t dummy;
    R_TRY(PathNormalizer::IsNormalized(std::addressof(normalized), std::addressof(dummy), s));

    R_UNLESS(normalized, ResultInvalidPathFormat);

    // Set the fixed path
    R_RETURN(out->SetShallowBuffer(s));
}

constexpr inline bool IsWindowsDriveRootPath(const FileSys::Path& path) {
    const char* const str = path.GetString();
    return IsWindowsDrive(str) &&
           (str[2] == StringTraits::DirectorySeparator ||
            str[2] == StringTraits::AlternateDirectorySeparator) &&
           str[3] == StringTraits::NullTerminator;
}

} // namespace FileSys
