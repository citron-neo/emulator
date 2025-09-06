// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/language.h"
#include "core/hle/service/ns/ns_results.h"
#include "core/hle/service/ns/read_only_application_control_data_interface.h"
#include "core/hle/service/set/settings_server.h"

namespace Service::NS {

IReadOnlyApplicationControlDataInterface::IReadOnlyApplicationControlDataInterface(
    Core::System& system_)
    : ServiceFramework{system_, "IReadOnlyApplicationControlDataInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IReadOnlyApplicationControlDataInterface::GetApplicationControlData>, "GetApplicationControlData"},
        {1, D<&IReadOnlyApplicationControlDataInterface::GetApplicationDesiredLanguage>, "GetApplicationDesiredLanguage"},
        {2, D<&IReadOnlyApplicationControlDataInterface::ConvertApplicationLanguageToLanguageCode>, "ConvertApplicationLanguageToLanguageCode"},
        {3, nullptr, "ConvertLanguageCodeToApplicationLanguage"},
        {4, nullptr, "SelectApplicationDesiredLanguage"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IReadOnlyApplicationControlDataInterface::~IReadOnlyApplicationControlDataInterface() = default;

Result IReadOnlyApplicationControlDataInterface::GetApplicationControlData(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, Out<u32> out_actual_size,
    ApplicationControlSource application_control_source, u64 application_id) {
    LOG_INFO(Service_NS, "called with control_source={}, application_id={:016X}",
             application_control_source, application_id);

    const FileSys::PatchManager pm{application_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto control = pm.GetControlMetadata();
    const auto size = out_buffer.size();

    const auto icon_size = control.second ? control.second->GetSize() : 0;
    const auto total_size = sizeof(FileSys::RawNACP) + icon_size;

    if (size < total_size) {
        LOG_ERROR(Service_NS, "output buffer is too small! (actual={:016X}, expected_min=0x4000)",
                  size);
        R_THROW(ResultUnknown);
    }

    if (control.first != nullptr) {
        const auto bytes = control.first->GetRawBytes();
        std::memcpy(out_buffer.data(), bytes.data(), bytes.size());
    } else {
        LOG_WARNING(Service_NS, "missing NACP data for application_id={:016X}, defaulting to zero",
                    application_id);
        std::memset(out_buffer.data(), 0, sizeof(FileSys::RawNACP));
    }

    if (control.second != nullptr) {
        control.second->Read(out_buffer.data() + sizeof(FileSys::RawNACP), icon_size);
    } else {
        LOG_WARNING(Service_NS, "missing icon data for application_id={:016X}", application_id);
    }

    *out_actual_size = static_cast<u32>(total_size);
    R_SUCCEED();
}

Result IReadOnlyApplicationControlDataInterface::GetApplicationDesiredLanguage(
    Out<ApplicationLanguage> out_desired_language, u32 supported_languages) {
    LOG_INFO(Service_NS, "called with supported_languages={:08X}", supported_languages);

    // Get language code from settings
    const auto language_code =
        Set::GetLanguageCodeFromIndex(static_cast<s32>(Settings::values.language_index.GetValue()));

    // Convert to application language, get priority list
    const auto application_language = ConvertToApplicationLanguage(language_code);
    if (application_language == std::nullopt) {
        LOG_ERROR(Service_NS, "Could not convert application language! language_code={}",
                  language_code);
        R_THROW(Service::NS::ResultApplicationLanguageNotFound);
    }
    const auto priority_list = GetApplicationLanguagePriorityList(*application_language);
    if (!priority_list) {
        LOG_ERROR(Service_NS,
                  "Could not find application language priorities! application_language={}",
                  *application_language);
        R_THROW(Service::NS::ResultApplicationLanguageNotFound);
    }

    // Try to find a valid language.
    for (const auto lang : *priority_list) {
        const auto supported_flag = GetSupportedLanguageFlag(lang);
        if (supported_languages == 0 || (supported_languages & supported_flag) == supported_flag) {
            *out_desired_language = lang;
            R_SUCCEED();
        }
    }

    LOG_ERROR(Service_NS, "Could not find a valid language! supported_languages={:08X}",
              supported_languages);
    R_THROW(Service::NS::ResultApplicationLanguageNotFound);
}

Result IReadOnlyApplicationControlDataInterface::ConvertApplicationLanguageToLanguageCode(
    Out<u64> out_language_code, ApplicationLanguage application_language) {
    const auto language_code = ConvertToLanguageCode(application_language);
    if (language_code == std::nullopt) {
        LOG_ERROR(Service_NS, "Language not found! application_language={}", application_language);
        R_THROW(Service::NS::ResultApplicationLanguageNotFound);
    }

    *out_language_code = static_cast<u64>(*language_code);
    R_SUCCEED();
}

} // namespace Service::NS
