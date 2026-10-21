// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cinttypes>
#include <cstring>
#include <exception>
#include <vector>

#include "common/common_funcs.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "common/lz4_compression.h"
#include "common/settings.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/kernel/code_set.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/loader/nso.h"
#include "core/memory.h"

#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#endif

namespace Loader {
namespace {
struct MODHeader {
    u32_le magic;
    u32_le dynamic_offset;
    u32_le bss_start_offset;
    u32_le bss_end_offset;
    u32_le eh_frame_hdr_start_offset;
    u32_le eh_frame_hdr_end_offset;
    u32_le module_offset; // Offset to runtime-generated module object. typically equal to .bss base
};
static_assert(sizeof(MODHeader) == 0x1c, "MODHeader has incorrect size.");

std::vector<u8> DecompressSegment(const std::vector<u8>& compressed_data,
                                  const NSOSegmentHeader& header) {
    std::vector<u8> uncompressed_data =
        Common::Compression::DecompressDataLZ4(compressed_data, header.size);

    assert(uncompressed_data.size() == header.size && "{} != {}" &&
           (void(header.size), void(uncompressed_data.size()), true));

    return uncompressed_data;
}

constexpr u32 PageAlignSizeNSO(u32 size) {
    return static_cast<u32>((size + Core::Memory::CITRON_PAGEMASK) & ~Core::Memory::CITRON_PAGEMASK);
}
} // Anonymous namespace

bool NSOHeader::IsSegmentCompressed(size_t segment_num) const {
    assert(segment_num < 3 && "Invalid segment {}" && (void(segment_num), true));
    return ((flags >> segment_num) & 1) != 0;
}

AppLoader_NSO::AppLoader_NSO(FileSys::VirtualFile file_) : AppLoader(std::move(file_)) {}

FileType AppLoader_NSO::IdentifyType(const FileSys::VirtualFile& in_file) {
    u32 magic = 0;
    if (in_file->ReadObject(&magic) != sizeof(magic)) {
        return FileType::Error;
    }

    if (Common::MakeMagic('N', 'S', 'O', '0') != magic) {
        return FileType::Error;
    }

    return FileType::NSO;
}

std::optional<VAddr> AppLoader_NSO::LoadModule(Kernel::KProcess& process, Core::System& system,
                                               const FileSys::VfsFile& nso_file, VAddr load_base,
                                               bool should_pass_arguments, bool load_into_process,
                                               std::optional<FileSys::PatchManager> pm,
                                               std::vector<Core::NCE::Patcher>* patches,
                                               s32 patch_index) {
    if (nso_file.GetSize() < sizeof(NSOHeader)) {
        return std::nullopt;
    }

    NSOHeader nso_header{};
    if (sizeof(NSOHeader) != nso_file.ReadObject(&nso_header)) {
        return std::nullopt;
    }

    if (nso_header.magic != Common::MakeMagic('N', 'S', 'O', '0')) {
        return std::nullopt;
    }

    // Allocate some space at the beginning if we are patching in PreText mode.
    const size_t module_start = [&]() -> size_t {
#ifdef HAS_NCE
        if (patches && load_into_process) {
            auto* patch = &patches->operator[](patch_index);
            if (patch->GetPatchMode() == Core::NCE::PatchMode::PreText) {
                return patch->GetSectionSize();
            }
        }
#endif
        return 0;
    }();

    // Build program image
    Kernel::CodeSet codeset;
    Kernel::PhysicalMemory program_image;
    for (std::size_t i = 0; i < nso_header.segments.size(); ++i) {
        std::vector<u8> data = nso_file.ReadBytes(nso_header.segments_compressed_size[i],
                                                  nso_header.segments[i].offset);
        if (nso_header.IsSegmentCompressed(i)) {
            data = DecompressSegment(data, nso_header.segments[i]);
        }
        program_image.resize(module_start + nso_header.segments[i].location +
                             static_cast<u32>(data.size()));
        std::memcpy(program_image.data() + module_start + nso_header.segments[i].location,
                    data.data(), data.size());
        codeset.segments[i].addr = module_start + nso_header.segments[i].location;
        codeset.segments[i].offset = module_start + nso_header.segments[i].location;
        codeset.segments[i].size = nso_header.segments[i].size;
    }

    if (should_pass_arguments && !Settings::values.program_args.GetValue().empty()) {
        const auto arg_data{Settings::values.program_args.GetValue()};

        codeset.DataSegment().size += NSO_ARGUMENT_DATA_ALLOCATION_SIZE;
        NSOArgumentHeader args_header{
            NSO_ARGUMENT_DATA_ALLOCATION_SIZE, static_cast<u32_le>(arg_data.size()), {}};
        const auto end_offset = program_image.size();
        program_image.resize(static_cast<u32>(program_image.size()) +
                             NSO_ARGUMENT_DATA_ALLOCATION_SIZE);
        std::memcpy(program_image.data() + end_offset, &args_header, sizeof(NSOArgumentHeader));
        std::memcpy(program_image.data() + end_offset + sizeof(NSOArgumentHeader), arg_data.data(),
                    arg_data.size());
    }

    codeset.DataSegment().size += nso_header.segments[2].bss_size;
    u32 image_size{
        PageAlignSizeNSO(static_cast<u32>(program_image.size()) + nso_header.segments[2].bss_size)};
    program_image.resize(image_size);

    for (std::size_t i = 0; i < nso_header.segments.size(); ++i) {
        codeset.segments[i].size = PageAlignSizeNSO(codeset.segments[i].size);
    }

    // Apply patches if necessary
    const auto name = nso_file.GetName();
    const bool has_nso_patch = pm && pm->HasNSOPatch(nso_header.build_id, name);
    const bool should_patch_nso =
        has_nso_patch || (load_into_process && Settings::values.dump_nso);
    if (pm && should_patch_nso) {
        std::span<u8> patchable_section(program_image.data() + module_start,
                                        program_image.size() - module_start);
        std::vector<u8> pi_header(sizeof(NSOHeader) + patchable_section.size());
        std::memcpy(pi_header.data(), &nso_header, sizeof(NSOHeader));
        std::memcpy(pi_header.data() + sizeof(NSOHeader), patchable_section.data(),
                    patchable_section.size());

        pi_header = pm->PatchNSO(pi_header, name);

        if (pi_header.size() < sizeof(NSOHeader)) {
            LOG_ERROR(Loader, "Patched NSO is too small: module={}, patched_size={:#x}", name,
                      pi_header.size());
            return std::nullopt;
        }

        const auto patched_size = pi_header.size() - sizeof(NSOHeader);
        if (patched_size != patchable_section.size()) {
            LOG_ERROR(Loader,
                      "Patched NSO size mismatch: module={}, original_size={:#x}, "
                      "patched_size={:#x}",
                      name, patchable_section.size(), patched_size);
            return std::nullopt;
        }

        std::copy(pi_header.begin() + sizeof(NSOHeader), pi_header.end(), patchable_section.data());
    }

#ifdef HAS_NCE
    // If we are computing the process code layout and using nce backend, patch.
    const auto& code = codeset.CodeSegment();
    auto* patch = patches ? &patches->operator[](patch_index) : nullptr;
    if (patch && !load_into_process) {
        // Patch SVCs and MRS calls in the guest code
        while (!patch->PatchText(program_image, code)) {
            patch = &patches->emplace_back();
        }
    } else if (patch) {
        // Relocate code patch and copy to the program_image.
        const auto build_id = Common::HexToString(nso_header.build_id);
        LOG_DEBUG(Loader,
                  "NCE relocating NSO module: name={}, build_id={}, patch_index={}, mode={}, "
                  "load_base={:#x}, image_size={:#x}, code_offset={:#x}, code_size={:#x}, "
                  "patch_section_size={:#x}",
                  name, build_id, patch_index, patch->GetPatchMode(), load_base,
                  program_image.size(), code.offset, code.size, patch->GetSectionSize());

        bool copied_patch_section;
        try {
            copied_patch_section =
                patch->RelocateAndCopy(load_base, code, program_image, &process.GetPostHandlers());
        } catch (const std::exception& ex) {
            LOG_CRITICAL(Loader,
                         "NCE failed while relocating NSO module: name={}, build_id={}, "
                         "patch_index={}, mode={}, load_base={:#x}, image_size={:#x}, "
                         "code_offset={:#x}, code_size={:#x}, patch_section_size={:#x}, "
                         "exception={}",
                         name, build_id, patch_index, patch->GetPatchMode(), load_base,
                         program_image.size(), code.offset, code.size, patch->GetSectionSize(),
                         ex.what());
            throw;
        }

        LOG_DEBUG(Loader,
                  "NCE relocated NSO module: name={}, build_id={}, patch_index={}, "
                  "copied_patch_section={}, final_image_size={:#x}",
                  name, build_id, patch_index, copied_patch_section, program_image.size());

        if (copied_patch_section) {
            // Update patch section.
            auto& patch_segment = codeset.PatchSegment();
            patch_segment.addr =
                patch->GetPatchMode() == Core::NCE::PatchMode::PreText ? 0 : image_size;
            patch_segment.size = static_cast<u32>(patch->GetSectionSize());
        }

        // Refresh image_size to take account the patch section if it was added by RelocateAndCopy
        image_size = static_cast<u32>(program_image.size());
    }
#endif

    // If we aren't actually loading (i.e. just computing the process code layout), we are done
    if (!load_into_process) {
        return load_base + image_size;
    }

    // The application build ID belongs to main, not the last loaded SDK module.
    // Keep it available independently of whether a patch manager was supplied.
    if (name == "main") {
        system.SetApplicationProcessBuildID(nso_header.build_id);
    }

    // Apply cheats if they exist and the program has a valid title ID
    if (pm) {
        const auto cheats = pm->CreateCheatList(nso_header.build_id);
        if (!cheats.empty()) {
            system.RegisterCheatList(cheats, nso_header.build_id, load_base, image_size);
        }
    }

    // Load codeset for current process
    codeset.memory = std::move(program_image);
    process.LoadModule(std::move(codeset), load_base);

    return load_base + image_size;
}

AppLoader_NSO::LoadResult AppLoader_NSO::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    modules.clear();

    // Load module
    const VAddr base_address = GetInteger(process.GetEntryPoint());
    if (!LoadModule(process, system, *file, base_address, true, true)) {
        return {ResultStatus::ErrorLoadingNSO, {}};
    }

    modules.insert_or_assign(base_address, file->GetName());
    LOG_DEBUG(Loader, "loaded module {} @ 0x{:X}", file->GetName(), base_address);

    is_loaded = true;
    return {ResultStatus::Success, LoadParameters{Kernel::KThread::DefaultThreadPriority,
                                                  Core::Memory::DEFAULT_STACK_SIZE}};
}

ResultStatus AppLoader_NSO::ReadNSOModules(Modules& out_modules) {
    out_modules = this->modules;
    return ResultStatus::Success;
}

} // namespace Loader
