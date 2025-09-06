// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <span>
#include <string_view>
#include "common/alignment.h"
#include "common/assert.h"
#include "core/file_sys/fsmitm_romfsbuild.h"
#include "core/file_sys/ips_layer.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_vector.h"

namespace FileSys {

constexpr u64 FS_MAX_PATH = 0x301;

constexpr u32 ROMFS_ENTRY_EMPTY = 0xFFFFFFFF;
constexpr u32 ROMFS_FILEPARTITION_OFS = 0x200;

// Types for building a RomFS.
struct RomFSHeader {
    u64 header_size;
    u64 dir_hash_table_ofs;
    u64 dir_hash_table_size;
    u64 dir_table_ofs;
    u64 dir_table_size;
    u64 file_hash_table_ofs;
    u64 file_hash_table_size;
    u64 file_table_ofs;
    u64 file_table_size;
    u64 file_partition_ofs;
};
static_assert(sizeof(RomFSHeader) == 0x50, "RomFSHeader has incorrect size.");

struct RomFSDirectoryEntry {
    u32 parent;
    u32 sibling;
    u32 child;
    u32 file;
    u32 hash;
    u32 name_size;
};
static_assert(sizeof(RomFSDirectoryEntry) == 0x18, "RomFSDirectoryEntry has incorrect size.");

struct RomFSFileEntry {
    u32 parent;
    u32 sibling;
    u64 offset;
    u64 size;
    u32 hash;
    u32 name_size;
};
static_assert(sizeof(RomFSFileEntry) == 0x20, "RomFSFileEntry has incorrect size.");

struct RomFSBuildFileContext;

struct RomFSBuildDirectoryContext {
    std::string path;
    u32 cur_path_ofs = 0;
    u32 path_len = 0;
    u32 entry_offset = 0;
    std::shared_ptr<RomFSBuildDirectoryContext> parent;
    std::shared_ptr<RomFSBuildDirectoryContext> child;
    std::shared_ptr<RomFSBuildDirectoryContext> sibling;
    std::shared_ptr<RomFSBuildFileContext> file;
};

struct RomFSBuildFileContext {
    std::string path;
    u32 cur_path_ofs = 0;
    u32 path_len = 0;
    u32 entry_offset = 0;
    u64 offset = 0;
    u64 size = 0;
    std::shared_ptr<RomFSBuildDirectoryContext> parent;
    std::shared_ptr<RomFSBuildFileContext> sibling;
    VirtualFile source;
};

static u32 romfs_calc_path_hash(u32 parent, std::string_view path, u32 start,
                                std::size_t path_len) {
    u32 hash = parent ^ 123456789;
    for (u32 i = 0; i < path_len; i++) {
        hash = (hash >> 5) | (hash << 27);
        hash ^= path[start + i];
    }

    return hash;
}

static u64 romfs_get_hash_table_count(u64 num_entries) {
    if (num_entries < 3) {
        return 3;
    }

    if (num_entries < 19) {
        return num_entries | 1;
    }

    u64 count = num_entries;
    while (count % 2 == 0 || count % 3 == 0 || count % 5 == 0 || count % 7 == 0 ||
           count % 11 == 0 || count % 13 == 0 || count % 17 == 0) {
        count++;
    }
    return count;
}

void RomFSBuildContext::VisitDirectory(VirtualDir romfs_dir, VirtualDir ext_dir,
                                       std::shared_ptr<RomFSBuildDirectoryContext> parent) {
    for (auto& child_romfs_file : romfs_dir->GetFiles()) {
        const auto name = child_romfs_file->GetName();
        const auto child = std::make_shared<RomFSBuildFileContext>();
        // Set child's path.
        child->cur_path_ofs = parent->path_len + 1;
        child->path_len = child->cur_path_ofs + static_cast<u32>(name.size());
        child->path = parent->path + "/" + name;

        if (ext_dir != nullptr && ext_dir->GetFile(name + ".stub") != nullptr) {
            continue;
        }

        // Sanity check on path_len
        ASSERT(child->path_len < FS_MAX_PATH);

        child->source = std::move(child_romfs_file);

        if (ext_dir != nullptr) {
            if (const auto ips = ext_dir->GetFile(name + ".ips")) {
                if (auto patched = PatchIPS(child->source, ips)) {
                    child->source = std::move(patched);
                }
            }
        }

        child->size = child->source->GetSize();

        AddFile(parent, std::move(child));
    }

    for (auto& child_romfs_dir : romfs_dir->GetSubdirectories()) {
        const auto name = child_romfs_dir->GetName();
        const auto child = std::make_shared<RomFSBuildDirectoryContext>();
        // Set child's path.
        child->cur_path_ofs = parent->path_len + 1;
        child->path_len = child->cur_path_ofs + static_cast<u32>(name.size());
        child->path = parent->path + "/" + name;

        if (ext_dir != nullptr && ext_dir->GetFile(name + ".stub") != nullptr) {
            continue;
        }

        // Sanity check on path_len
        ASSERT(child->path_len < FS_MAX_PATH);

        if (!AddDirectory(parent, child)) {
            continue;
        }

        auto child_ext_dir = ext_dir != nullptr ? ext_dir->GetSubdirectory(name) : nullptr;
        this->VisitDirectory(child_romfs_dir, child_ext_dir, child);
    }
}

bool RomFSBuildContext::AddDirectory(std::shared_ptr<RomFSBuildDirectoryContext> parent_dir_ctx,
                                     std::shared_ptr<RomFSBuildDirectoryContext> dir_ctx) {
    // Add a new directory.
    num_dirs++;
    dir_table_size +=
        sizeof(RomFSDirectoryEntry) + Common::AlignUp(dir_ctx->path_len - dir_ctx->cur_path_ofs, 4);
    dir_ctx->parent = std::move(parent_dir_ctx);
    directories.emplace_back(std::move(dir_ctx));

    return true;
}

bool RomFSBuildContext::AddFile(std::shared_ptr<RomFSBuildDirectoryContext> parent_dir_ctx,
                                std::shared_ptr<RomFSBuildFileContext> file_ctx) {
    // Add a new file.
    num_files++;
    file_table_size +=
        sizeof(RomFSFileEntry) + Common::AlignUp(file_ctx->path_len - file_ctx->cur_path_ofs, 4);
    file_ctx->parent = std::move(parent_dir_ctx);
    files.emplace_back(std::move(file_ctx));

    return true;
}

RomFSBuildContext::RomFSBuildContext(VirtualDir base_, VirtualDir ext_)
    : base(std::move(base_)), ext(std::move(ext_)) {
    root = std::make_shared<RomFSBuildDirectoryContext>();
    root->path = "\0";
    directories.emplace_back(root);
    num_dirs = 1;
    dir_table_size = 0x18;

    VisitDirectory(base, ext, root);
}

RomFSBuildContext::~RomFSBuildContext() = default;

std::vector<std::pair<u64, VirtualFile>> RomFSBuildContext::Build() {
    const u64 dir_hash_table_entry_count = romfs_get_hash_table_count(num_dirs);
    const u64 file_hash_table_entry_count = romfs_get_hash_table_count(num_files);
    dir_hash_table_size = 4 * dir_hash_table_entry_count;
    file_hash_table_size = 4 * file_hash_table_entry_count;

    // Assign metadata pointers.
    RomFSHeader header{};

    std::vector<u8> metadata(file_hash_table_size + file_table_size + dir_hash_table_size +
                             dir_table_size);
    u32* const dir_hash_table_pointer = reinterpret_cast<u32*>(metadata.data());
    u8* const dir_table_pointer = metadata.data() + dir_hash_table_size;
    u32* const file_hash_table_pointer =
        reinterpret_cast<u32*>(metadata.data() + dir_hash_table_size + dir_table_size);
    u8* const file_table_pointer =
        metadata.data() + dir_hash_table_size + dir_table_size + file_hash_table_size;

    std::span<u32> dir_hash_table(dir_hash_table_pointer, dir_hash_table_entry_count);
    std::span<u32> file_hash_table(file_hash_table_pointer, file_hash_table_entry_count);
    std::span<u8> dir_table(dir_table_pointer, dir_table_size);
    std::span<u8> file_table(file_table_pointer, file_table_size);

    // Initialize hash tables.
    std::memset(dir_hash_table.data(), 0xFF, dir_hash_table.size_bytes());
    std::memset(file_hash_table.data(), 0xFF, file_hash_table.size_bytes());

    // Sort tables by name.
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a->path < b->path; });
    std::sort(directories.begin(), directories.end(),
              [](const auto& a, const auto& b) { return a->path < b->path; });

    // Determine file offsets.
    u32 entry_offset = 0;
    std::shared_ptr<RomFSBuildFileContext> prev_file = nullptr;
    for (const auto& cur_file : files) {
        file_partition_size = Common::AlignUp(file_partition_size, 16);
        cur_file->offset = file_partition_size;
        file_partition_size += cur_file->size;
        cur_file->entry_offset = entry_offset;
        entry_offset +=
            static_cast<u32>(sizeof(RomFSFileEntry) +
                             Common::AlignUp(cur_file->path_len - cur_file->cur_path_ofs, 4));
        prev_file = cur_file;
    }
    // Assign deferred parent/sibling ownership.
    for (auto it = files.rbegin(); it != files.rend(); ++it) {
        auto& cur_file = *it;
        cur_file->sibling = cur_file->parent->file;
        cur_file->parent->file = cur_file;
    }

    // Determine directory offsets.
    entry_offset = 0;
    for (const auto& cur_dir : directories) {
        cur_dir->entry_offset = entry_offset;
        entry_offset +=
            static_cast<u32>(sizeof(RomFSDirectoryEntry) +
                             Common::AlignUp(cur_dir->path_len - cur_dir->cur_path_ofs, 4));
    }
    // Assign deferred parent/sibling ownership.
    for (auto it = directories.rbegin(); (*it) != root; ++it) {
        auto& cur_dir = *it;
        cur_dir->sibling = cur_dir->parent->child;
        cur_dir->parent->child = cur_dir;
    }

    // Create output map.
    std::vector<std::pair<u64, VirtualFile>> out;
    out.reserve(num_files + 2);

    // Set header fields.
    header.header_size = sizeof(RomFSHeader);
    header.file_hash_table_size = file_hash_table_size;
    header.file_table_size = file_table_size;
    header.dir_hash_table_size = dir_hash_table_size;
    header.dir_table_size = dir_table_size;
    header.file_partition_ofs = ROMFS_FILEPARTITION_OFS;
    header.dir_hash_table_ofs = Common::AlignUp(header.file_partition_ofs + file_partition_size, 4);
    header.dir_table_ofs = header.dir_hash_table_ofs + header.dir_hash_table_size;
    header.file_hash_table_ofs = header.dir_table_ofs + header.dir_table_size;
    header.file_table_ofs = header.file_hash_table_ofs + header.file_hash_table_size;

    std::vector<u8> header_data(sizeof(RomFSHeader));
    std::memcpy(header_data.data(), &header, header_data.size());
    out.emplace_back(0, std::make_shared<VectorVfsFile>(std::move(header_data)));

    // Populate file tables.
    for (const auto& cur_file : files) {
        RomFSFileEntry cur_entry{};

        cur_entry.parent = cur_file->parent->entry_offset;
        cur_entry.sibling =
            cur_file->sibling == nullptr ? ROMFS_ENTRY_EMPTY : cur_file->sibling->entry_offset;
        cur_entry.offset = cur_file->offset;
        cur_entry.size = cur_file->size;

        const auto name_size = cur_file->path_len - cur_file->cur_path_ofs;
        const auto hash = romfs_calc_path_hash(cur_file->parent->entry_offset, cur_file->path,
                                               cur_file->cur_path_ofs, name_size);
        cur_entry.hash = file_hash_table[hash % file_hash_table_entry_count];
        file_hash_table[hash % file_hash_table_entry_count] = cur_file->entry_offset;

        cur_entry.name_size = name_size;

        out.emplace_back(cur_file->offset + ROMFS_FILEPARTITION_OFS, std::move(cur_file->source));
        std::memcpy(file_table.data() + cur_file->entry_offset, &cur_entry, sizeof(RomFSFileEntry));
        std::memset(file_table.data() + cur_file->entry_offset + sizeof(RomFSFileEntry), 0,
                    Common::AlignUp(cur_entry.name_size, 4));
        std::memcpy(file_table.data() + cur_file->entry_offset + sizeof(RomFSFileEntry),
                    cur_file->path.data() + cur_file->cur_path_ofs, name_size);
    }

    // Populate dir tables.
    for (const auto& cur_dir : directories) {
        RomFSDirectoryEntry cur_entry{};

        cur_entry.parent = cur_dir == root ? 0 : cur_dir->parent->entry_offset;
        cur_entry.sibling =
            cur_dir->sibling == nullptr ? ROMFS_ENTRY_EMPTY : cur_dir->sibling->entry_offset;
        cur_entry.child =
            cur_dir->child == nullptr ? ROMFS_ENTRY_EMPTY : cur_dir->child->entry_offset;
        cur_entry.file = cur_dir->file == nullptr ? ROMFS_ENTRY_EMPTY : cur_dir->file->entry_offset;

        const auto name_size = cur_dir->path_len - cur_dir->cur_path_ofs;
        const auto hash = romfs_calc_path_hash(cur_dir == root ? 0 : cur_dir->parent->entry_offset,
                                               cur_dir->path, cur_dir->cur_path_ofs, name_size);
        cur_entry.hash = dir_hash_table[hash % dir_hash_table_entry_count];
        dir_hash_table[hash % dir_hash_table_entry_count] = cur_dir->entry_offset;

        cur_entry.name_size = name_size;

        std::memcpy(dir_table.data() + cur_dir->entry_offset, &cur_entry,
                    sizeof(RomFSDirectoryEntry));
        std::memset(dir_table.data() + cur_dir->entry_offset + sizeof(RomFSDirectoryEntry), 0,
                    Common::AlignUp(cur_entry.name_size, 4));
        std::memcpy(dir_table.data() + cur_dir->entry_offset + sizeof(RomFSDirectoryEntry),
                    cur_dir->path.data() + cur_dir->cur_path_ofs, name_size);
    }

    // Write metadata.
    out.emplace_back(header.dir_hash_table_ofs,
                     std::make_shared<VectorVfsFile>(std::move(metadata)));

    // Sort the output.
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    return out;
}

} // namespace FileSys
