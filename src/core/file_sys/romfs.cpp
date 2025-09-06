// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/file_sys/fsmitm_romfsbuild.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_cached.h"
#include "core/file_sys/vfs/vfs_concat.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/file_sys/vfs/vfs_vector.h"

namespace FileSys {
namespace {
constexpr u32 ROMFS_ENTRY_EMPTY = 0xFFFFFFFF;

struct TableLocation {
    u64_le offset;
    u64_le size;
};
static_assert(sizeof(TableLocation) == 0x10, "TableLocation has incorrect size.");

struct RomFSHeader {
    u64_le header_size;
    TableLocation directory_hash;
    TableLocation directory_meta;
    TableLocation file_hash;
    TableLocation file_meta;
    u64_le data_offset;
};
static_assert(sizeof(RomFSHeader) == 0x50, "RomFSHeader has incorrect size.");

struct DirectoryEntry {
    u32_le parent;
    u32_le sibling;
    u32_le child_dir;
    u32_le child_file;
    u32_le hash;
    u32_le name_length;
};
static_assert(sizeof(DirectoryEntry) == 0x18, "DirectoryEntry has incorrect size.");

struct FileEntry {
    u32_le parent;
    u32_le sibling;
    u64_le offset;
    u64_le size;
    u32_le hash;
    u32_le name_length;
};
static_assert(sizeof(FileEntry) == 0x20, "FileEntry has incorrect size.");

struct RomFSTraversalContext {
    RomFSHeader header;
    VirtualFile file;
    std::vector<u8> directory_meta;
    std::vector<u8> file_meta;
};

template <typename EntryType, auto Member>
std::pair<EntryType, std::string> GetEntry(const RomFSTraversalContext& ctx, size_t offset) {
    const size_t entry_end = offset + sizeof(EntryType);
    const std::vector<u8>& vec = ctx.*Member;
    const size_t size = vec.size();
    const u8* data = vec.data();
    EntryType entry{};

    if (entry_end > size) {
        return {};
    }
    std::memcpy(&entry, data + offset, sizeof(EntryType));

    const size_t name_length = std::min(entry_end + entry.name_length, size) - entry_end;
    std::string name(reinterpret_cast<const char*>(data + entry_end), name_length);

    return {entry, std::move(name)};
}

std::pair<DirectoryEntry, std::string> GetDirectoryEntry(const RomFSTraversalContext& ctx,
                                                         size_t directory_offset) {
    return GetEntry<DirectoryEntry, &RomFSTraversalContext::directory_meta>(ctx, directory_offset);
}

std::pair<FileEntry, std::string> GetFileEntry(const RomFSTraversalContext& ctx,
                                               size_t file_offset) {
    return GetEntry<FileEntry, &RomFSTraversalContext::file_meta>(ctx, file_offset);
}

void ProcessFile(const RomFSTraversalContext& ctx, u32 this_file_offset,
                 std::shared_ptr<VectorVfsDirectory>& parent) {
    while (this_file_offset != ROMFS_ENTRY_EMPTY) {
        auto entry = GetFileEntry(ctx, this_file_offset);

        parent->AddFile(std::make_shared<OffsetVfsFile>(ctx.file, entry.first.size,
                                                        entry.first.offset + ctx.header.data_offset,
                                                        std::move(entry.second)));

        this_file_offset = entry.first.sibling;
    }
}

void ProcessDirectory(const RomFSTraversalContext& ctx, u32 this_dir_offset,
                      std::shared_ptr<VectorVfsDirectory>& parent) {
    while (this_dir_offset != ROMFS_ENTRY_EMPTY) {
        auto entry = GetDirectoryEntry(ctx, this_dir_offset);
        auto current = std::make_shared<VectorVfsDirectory>(
            std::vector<VirtualFile>{}, std::vector<VirtualDir>{}, entry.second);

        if (entry.first.child_file != ROMFS_ENTRY_EMPTY) {
            ProcessFile(ctx, entry.first.child_file, current);
        }

        if (entry.first.child_dir != ROMFS_ENTRY_EMPTY) {
            ProcessDirectory(ctx, entry.first.child_dir, current);
        }

        parent->AddDirectory(current);
        this_dir_offset = entry.first.sibling;
    }
}
} // Anonymous namespace

VirtualDir ExtractRomFS(VirtualFile file) {
    auto root_container = std::make_shared<VectorVfsDirectory>();
    if (!file) {
        return root_container;
    }

    RomFSTraversalContext ctx{};

    if (file->ReadObject(&ctx.header) != sizeof(RomFSHeader)) {
        return nullptr;
    }

    if (ctx.header.header_size != sizeof(RomFSHeader)) {
        return nullptr;
    }

    ctx.file = file;
    ctx.directory_meta =
        file->ReadBytes(ctx.header.directory_meta.size, ctx.header.directory_meta.offset);
    ctx.file_meta = file->ReadBytes(ctx.header.file_meta.size, ctx.header.file_meta.offset);

    ProcessDirectory(ctx, 0, root_container);

    if (auto root = root_container->GetSubdirectory(""); root) {
        return root;
    }

    ASSERT(false);
    return nullptr;
}

VirtualFile CreateRomFS(VirtualDir dir, VirtualDir ext) {
    if (dir == nullptr)
        return nullptr;

    RomFSBuildContext ctx{dir, ext};
    return ConcatenatedVfsFile::MakeConcatenatedFile(0, dir->GetName(), ctx.Build());
}

} // namespace FileSys
