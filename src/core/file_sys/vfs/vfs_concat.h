// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <compare>
#include <map>
#include <memory>
#include "core/file_sys/vfs/vfs.h"

namespace FileSys {

// Class that wraps multiple vfs files and concatenates them, making reads seamless. Currently
// read-only.
class ConcatenatedVfsFile : public VfsFile {
private:
    struct ConcatenationEntry {
        u64 offset;
        VirtualFile file;

        auto operator<=>(const ConcatenationEntry& other) const {
            return this->offset <=> other.offset;
        }
    };
    using ConcatenationMap = std::vector<ConcatenationEntry>;

    explicit ConcatenatedVfsFile(std::string&& name,
                                 std::vector<ConcatenationEntry>&& concatenation_map);
    bool VerifyContinuity() const;

public:
    ~ConcatenatedVfsFile() override;

    /// Wrapper function to allow for more efficient handling of files.size() == 0, 1 cases.
    static VirtualFile MakeConcatenatedFile(std::string&& name, std::vector<VirtualFile>&& files);

    /// Convenience function that turns a map of offsets to files into a concatenated file, filling
    /// gaps with a given filler byte.
    static VirtualFile MakeConcatenatedFile(u8 filler_byte, std::string&& name,
                                            std::vector<std::pair<u64, VirtualFile>>&& files);

    std::string GetName() const override;
    std::size_t GetSize() const override;
    bool Resize(std::size_t new_size) override;
    VirtualDir GetContainingDirectory() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    std::size_t Read(u8* data, std::size_t length, std::size_t offset) const override;
    std::size_t Write(const u8* data, std::size_t length, std::size_t offset) override;
    bool Rename(std::string_view new_name) override;

private:
    ConcatenationMap concatenation_map;
    std::string name;
};

} // namespace FileSys
