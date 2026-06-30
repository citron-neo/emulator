// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <numeric>
#include <vector>

#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/aoc/addon_content_manager.h"
#include "core/hle/service/aoc/contents_service_manager.h"
#include "core/hle/service/aoc/purchase_event_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/loader/loader.h"

// [UNITY-FIX] undef Win32 macros shadowing ServiceContext methods.
#undef CreateEvent
#undef CreateMutex
#undef CreateSemaphore

namespace Service::AOC {

static bool CheckAOCTitleIDMatchesBase(u64 title_id, u64 base) {
    return FileSys::GetBaseTitleID(title_id) == base;
}

static std::vector<u64> AccumulateAOCTitleIDs(Core::System& system) {
    std::vector<u64> add_on_content;
    const auto& rcu = system.GetContentProvider();
    const auto list =
        rcu.ListEntriesFilter(FileSys::TitleType::AOC, FileSys::ContentRecordType::Data);
    std::transform(list.begin(), list.end(), std::back_inserter(add_on_content),
                   [](const FileSys::ContentProviderEntry& rce) { return rce.title_id; });
    add_on_content.erase(
        std::remove_if(
            add_on_content.begin(), add_on_content.end(),
            [&rcu](u64 tid) {
                return rcu.GetEntry(tid, FileSys::ContentRecordType::Data)->GetStatus() !=
                       Loader::ResultStatus::Success;
            }),
        add_on_content.end());

    LOG_WARNING(Service_AOC, "Accumulated {} AOC title IDs", add_on_content.size());
    for (const auto& tid : add_on_content) {
        LOG_WARNING(Service_AOC, "Found AOC: {:016X}", tid);
    }
    return add_on_content;
}

static std::vector<u64> GetAOCTitleIDsForBase(const std::vector<u64>& add_on_content, u64 base) {
    std::vector<u64> out;
    for (const auto title_id : add_on_content) {
        if (CheckAOCTitleIDMatchesBase(title_id, base)) {
            out.push_back(title_id);
        }
    }

    std::sort(out.begin(), out.end());
    return out;
}

static u32 GetGuestAOCIndex(std::size_t ordinal) {
    return static_cast<u32>(ordinal + 1);
}

IAddOnContentManager::IAddOnContentManager(Core::System& system_)
    : ServiceFramework{system_, "aoc:u"}, add_on_content{AccumulateAOCTitleIDs(system)},
      service_context{system_, "aoc:u"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAddOnContentManager::CountAddOnContentByApplicationId>, "CountAddOnContentByApplicationId"},
        {1, D<&IAddOnContentManager::ListAddOnContentByApplicationId>, "ListAddOnContentByApplicationId"},
        {2, D<&IAddOnContentManager::CountAddOnContent>, "CountAddOnContent"},
        {3, D<&IAddOnContentManager::ListAddOnContent>, "ListAddOnContent"},
        {4, nullptr, "GetAddOnContentBaseIdByApplicationId"},
        {5, D<&IAddOnContentManager::GetAddOnContentBaseId>, "GetAddOnContentBaseId"},
        {6, nullptr, "PrepareAddOnContentByApplicationId"},
        {7, D<&IAddOnContentManager::PrepareAddOnContent>, "PrepareAddOnContent"},
        {8, D<&IAddOnContentManager::GetAddOnContentListChangedEvent>, "GetAddOnContentListChangedEvent"},
        {9, nullptr, "GetAddOnContentLostErrorCode"},
        {10, D<&IAddOnContentManager::GetAddOnContentListChangedEventWithProcessId>, "GetAddOnContentListChangedEventWithProcessId"},
        {11, D<&IAddOnContentManager::NotifyMountAddOnContent>, "NotifyMountAddOnContent"},
        {12, D<&IAddOnContentManager::NotifyUnmountAddOnContent>, "NotifyUnmountAddOnContent"},
        {13, nullptr, "IsAddOnContentMountedForDebug"},
        {50, D<&IAddOnContentManager::CheckAddOnContentMountStatus>, "CheckAddOnContentMountStatus"},
        {100, D<&IAddOnContentManager::CreateEcPurchasedEventManager>, "CreateEcPurchasedEventManager"},
        {101, D<&IAddOnContentManager::CreatePermanentEcPurchasedEventManager>, "CreatePermanentEcPurchasedEventManager"},
        {110, D<&IAddOnContentManager::CreateContentsServiceManager>, "CreateContentsServiceManager"},
        {200, nullptr, "SetRequiredAddOnContentsOnContentsAvailabilityTransition"},
        {300, nullptr, "SetupHostAddOnContent"},
        {301, nullptr, "GetRegisteredAddOnContentPath"},
        {302, nullptr, "UpdateCachedList"},
    };
    // clang-format on

    RegisterHandlers(functions);

    aoc_change_event = service_context.CreateEvent("GetAddOnContentListChanged:Event");
}

IAddOnContentManager::~IAddOnContentManager() {
    service_context.CloseEvent(aoc_change_event);
}

Result IAddOnContentManager::CountAddOnContent(Out<u32> out_count, ClientProcessId process_id) {
    // LOG_DEBUG(Service_AOC, "called. process_id={}", process_id.pid);
    LOG_DEBUG(Service_AOC, "CountAddOnContent called. process_id={}", process_id.pid);
    const auto raw_program_id = system.GetApplicationProcessProgramID();
    const auto current = FileSys::GetBaseTitleID(raw_program_id);

    const auto& disabled = Settings::values.disabled_addons[current];
    const auto dlc_disabled = std::find(disabled.begin(), disabled.end(), "DLC") != disabled.end();
    const auto matching_aocs = GetAOCTitleIDsForBase(add_on_content, current);
    if (dlc_disabled) {
        *out_count = 0;
        LOG_DEBUG(Service_AOC,
                  "CountAddOnContent: raw_program_id={:016X}, base_id={:016X}, "
                  "accumulated={}, matched={}, dlc_disabled=true",
                  raw_program_id, current, add_on_content.size(), matching_aocs.size());
        R_SUCCEED();
    }

    *out_count = static_cast<u32>(matching_aocs.size());
    LOG_DEBUG(Service_AOC,
              "CountAddOnContent: raw_program_id={:016X}, base_id={:016X}, accumulated={}, "
              "matched={}, dlc_disabled=false",
              raw_program_id, current, add_on_content.size(), matching_aocs.size());
    R_SUCCEED();
}

Result IAddOnContentManager::ListAddOnContent(Out<u32> out_count,
                                              OutBuffer<BufferAttr_HipcMapAlias> out_addons,
                                              u32 offset, u32 count, ClientProcessId process_id) {
    LOG_WARNING(Service_AOC, "called with offset={}, count={}, process_id={}", offset, count,
                process_id.pid);

    const auto current = FileSys::GetBaseTitleID(system.GetApplicationProcessProgramID());

    std::vector<u32> out;
    const auto matching_aocs = GetAOCTitleIDsForBase(add_on_content, current);
    const auto& disabled = Settings::values.disabled_addons[current];
    if (std::find(disabled.begin(), disabled.end(), "DLC") == disabled.end()) {
        LOG_WARNING(Service_AOC, "Filtering AOCs for base title ID: {:016X}", current);
        for (std::size_t i = 0; i < matching_aocs.size(); ++i) {
            const auto guest_index = GetGuestAOCIndex(i);
            LOG_WARNING(Service_AOC,
                        "Match! AOC {:016X} belongs to current app. Adding guest_index={}.",
                        matching_aocs[i], guest_index);
            out.push_back(guest_index);
        }
    } else {
        LOG_WARNING(Service_AOC, "DLCs are disabled for this title {:016X}", current);
    }

    // TODO(DarkLordZach): Find the correct error code.
    R_UNLESS(out.size() >= offset, ResultUnknown);

    const auto buffer_count = out_addons.size() / sizeof(u32);
    *out_count = static_cast<u32>(std::min({out.size() - offset, static_cast<size_t>(count),
                                            buffer_count}));
    LOG_WARNING(Service_AOC,
                "ListAddOnContent result: base_id={:016X}, matched_total={}, offset={}, "
                "requested_count={}, returned_count={}, buffer_bytes={}",
                current, out.size(), offset, count, *out_count, out_addons.size());

    for (u32 i = 0; i < *out_count; ++i) {
        const auto addon_index = out[offset + i];
        const auto physical_title_id = matching_aocs[addon_index - 1];

        LOG_WARNING(Service_AOC,
                    "ListAddOnContent output[{}]: addon_index={}, physical_title_id={:016X}",
                    i, addon_index, physical_title_id);
    }

    std::rotate(out.begin(), out.begin() + offset, out.end());

    std::memcpy(out_addons.data(), out.data(), *out_count * sizeof(u32));

    R_SUCCEED();
}

Result IAddOnContentManager::CountAddOnContentByApplicationId(Out<u32> out_count,
                                                              u64 application_id) {
    LOG_DEBUG(Service_AOC, "called. application_id={:016X}", application_id);

    const auto current = FileSys::GetBaseTitleID(application_id);

    const auto& disabled = Settings::values.disabled_addons[current];
    const auto dlc_disabled = std::find(disabled.begin(), disabled.end(), "DLC") != disabled.end();
    const auto matching_aocs = GetAOCTitleIDsForBase(add_on_content, current);
    if (dlc_disabled) {
        *out_count = 0;
        LOG_DEBUG(Service_AOC,
                  "CountAddOnContentByApplicationId: application_id={:016X}, base_id={:016X}, "
                  "accumulated={}, matched={}, dlc_disabled=true",
                  application_id, current, add_on_content.size(), matching_aocs.size());
        R_SUCCEED();
    }

    *out_count = static_cast<u32>(matching_aocs.size());
    LOG_DEBUG(Service_AOC,
              "CountAddOnContentByApplicationId: application_id={:016X}, base_id={:016X}, "
              "accumulated={}, matched={}, dlc_disabled=false",
              application_id, current, add_on_content.size(), matching_aocs.size());

    R_SUCCEED();
}

Result IAddOnContentManager::ListAddOnContentByApplicationId(
    Out<u32> out_count, OutBuffer<BufferAttr_HipcMapAlias> out_addons, u32 offset, u32 count,
    u64 application_id) {
    LOG_WARNING(Service_AOC, "called with offset={}, count={}, application_id={:016X}", offset,
                count, application_id);

    const auto current = FileSys::GetBaseTitleID(application_id);

    std::vector<u32> out;
    const auto matching_aocs = GetAOCTitleIDsForBase(add_on_content, current);
    const auto& disabled = Settings::values.disabled_addons[current];
    if (std::find(disabled.begin(), disabled.end(), "DLC") == disabled.end()) {
        LOG_WARNING(Service_AOC, "Filtering AOCs for base title ID: {:016X}", current);
        for (std::size_t i = 0; i < matching_aocs.size(); ++i) {
            const auto guest_index = GetGuestAOCIndex(i);
            LOG_WARNING(Service_AOC,
                        "Match! AOC {:016X} belongs to current app. Adding guest_index={}.",
                        matching_aocs[i], guest_index);
            out.push_back(guest_index);
        }
    } else {
        LOG_WARNING(Service_AOC, "DLCs are disabled for this title {:016X}", current);
    }

    // TODO(DarkLordZach): Find the correct error code.
    R_UNLESS(out.size() >= offset, ResultUnknown);

    const auto buffer_count = out_addons.size() / sizeof(u32);
    *out_count = static_cast<u32>(std::min({out.size() - offset, static_cast<size_t>(count),
                                            buffer_count}));
    LOG_WARNING(Service_AOC,
                "ListAddOnContentByApplicationId result: base_id={:016X}, matched_total={}, "
                "offset={}, requested_count={}, returned_count={}, buffer_bytes={}",
                current, out.size(), offset, count, *out_count, out_addons.size());

    for (u32 i = 0; i < *out_count; ++i) {
        const auto addon_index = out[offset + i];
        const auto physical_title_id = matching_aocs[addon_index - 1];

        LOG_WARNING(Service_AOC,
                    "ListAddOnContentByApplicationId output[{}]: addon_index={}, "
                    "physical_title_id={:016X}",
                    i, addon_index, physical_title_id);
    }

    std::rotate(out.begin(), out.begin() + offset, out.end());

    std::memcpy(out_addons.data(), out.data(), *out_count * sizeof(u32));

    R_SUCCEED();
}

Result IAddOnContentManager::GetAddOnContentBaseId(Out<u64> out_title_id,
                                                   ClientProcessId process_id) {
    // LOG_DEBUG(Service_AOC, "called. process_id={}", process_id.pid);
    LOG_WARNING(Service_AOC, "GetAddOnContentBaseId called. process_id={}", process_id.pid);
    const auto title_id = system.GetApplicationProcessProgramID();
    const FileSys::PatchManager pm{title_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};

    const auto res = pm.GetControlMetadata();
    if (res.first == nullptr) {
        *out_title_id = FileSys::GetAOCBaseTitleID(title_id);
        LOG_WARNING(Service_AOC,
            "GetAddOnContentBaseId fallback: program_id={:016X}, aoc_base={:016X}",
            title_id, *out_title_id);
        R_SUCCEED();
    }

    *out_title_id = res.first->GetDLCBaseTitleId();
    LOG_WARNING(Service_AOC,
            "GetAddOnContentBaseId metadata: program_id={:016X}, aoc_base={:016X}",
            title_id, *out_title_id);

    R_SUCCEED();
}

Result IAddOnContentManager::PrepareAddOnContent(s32 addon_index, ClientProcessId process_id) {
    const auto raw_program_id = system.GetApplicationProcessProgramID();
    const auto program_id = FileSys::GetBaseTitleID(raw_program_id);
    const auto aoc_base_id = FileSys::GetAOCBaseTitleID(program_id);
    const auto matching_aocs = GetAOCTitleIDsForBase(add_on_content, program_id);
    u64 physical_title_id = 0;
    if (addon_index > 0 && static_cast<std::size_t>(addon_index) <= matching_aocs.size()) {
        physical_title_id = matching_aocs[static_cast<std::size_t>(addon_index) - 1];
    }

    LOG_DEBUG(Service_AOC,
              "PrepareAddOnContent: raw_program_id={:016X}, base_id={:016X}, "
              "aoc_base={:016X}, accumulated={}, matched={}, addon_index={}, "
              "physical_title_id={:016X}, process_id={}",
              raw_program_id, program_id, aoc_base_id, add_on_content.size(),
              matching_aocs.size(), addon_index, physical_title_id, process_id.pid);

    R_SUCCEED();
}

Result IAddOnContentManager::GetAddOnContentListChangedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_AOC, "(STUBBED) called");

    *out_event = &aoc_change_event->GetReadableEvent();

    R_SUCCEED();
}

Result IAddOnContentManager::GetAddOnContentListChangedEventWithProcessId(
    OutCopyHandle<Kernel::KReadableEvent> out_event, ClientProcessId process_id) {
    LOG_WARNING(Service_AOC, "(STUBBED) called");

    *out_event = &aoc_change_event->GetReadableEvent();

    R_SUCCEED();
}

Result IAddOnContentManager::NotifyMountAddOnContent() {
    LOG_WARNING(Service_AOC, "(STUBBED) called");

    R_SUCCEED();
}

Result IAddOnContentManager::NotifyUnmountAddOnContent() {
    LOG_WARNING(Service_AOC, "(STUBBED) called");

    R_SUCCEED();
}

Result IAddOnContentManager::CheckAddOnContentMountStatus() {
    LOG_WARNING(Service_AOC, "(STUBBED) called");

    R_SUCCEED();
}

Result IAddOnContentManager::CreateEcPurchasedEventManager(
    OutInterface<IPurchaseEventManager> out_interface) {
    LOG_WARNING(Service_AOC, "(STUBBED) called");

    *out_interface = std::make_shared<IPurchaseEventManager>(system);

    R_SUCCEED();
}

Result IAddOnContentManager::CreatePermanentEcPurchasedEventManager(
    OutInterface<IPurchaseEventManager> out_interface) {
    LOG_WARNING(Service_AOC, "(STUBBED) called");

    *out_interface = std::make_shared<IPurchaseEventManager>(system);

    R_SUCCEED();
}

Result IAddOnContentManager::CreateContentsServiceManager(
    OutInterface<IContentsServiceManager> out_interface) {
    LOG_WARNING(Service_AOC, "(STUBBED) called");

    *out_interface = std::make_shared<IContentsServiceManager>(system);

    R_SUCCEED();
}

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    server_manager->RegisterNamedService("aoc:u", std::make_shared<IAddOnContentManager>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::AOC
