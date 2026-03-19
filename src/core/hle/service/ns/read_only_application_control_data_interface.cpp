// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <vector>
#include "common/settings.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/ns/language.h"
#include "core/hle/service/ns/ns_results.h"
#include "core/hle/service/ns/read_only_application_control_data_interface.h"
#include "core/hle/service/set/settings_server.h"

// Suppress warnings from stb headers - use compiler-specific pragmas
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // conversion, possible loss of data
#pragma warning(disable : 4456) // declaration hides previous local declaration
#pragma warning(disable : 4457) // declaration hides function parameter
#pragma warning(disable : 4701) // potentially uninitialized local variable
#pragma warning(disable : 4703) // potentially uninitialized local pointer variable
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "common/stb.h"
#ifdef _MSC_VER
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Service::NS {

namespace {

void JPGToMemory(void* context, void* data, int size) {
    auto* buffer = static_cast<std::vector<u8>*>(context);
    const auto* char_data = static_cast<const u8*>(data);
    buffer->insert(buffer->end(), char_data, char_data + size);
}

void SanitizeJPEGImageSize(std::vector<u8>& image) {
    constexpr std::size_t max_jpeg_image_size = 0x20000;
    constexpr int profile_dimensions = 174; // for grid view thingy
    int original_width, original_height, color_channels;

    auto* plain_image =
        stbi_load_from_memory(image.data(), static_cast<int>(image.size()), &original_width,
                              &original_height, &color_channels, STBI_rgb);

    if (plain_image == nullptr) {
        LOG_ERROR(Service_NS, "Failed to load JPEG for sanitization.");
        return;
    }

    if (original_width != profile_dimensions || original_height != profile_dimensions) {
        std::vector<u8> out_image(profile_dimensions * profile_dimensions * STBI_rgb);
        stbir_resize_uint8_srgb(plain_image, original_width, original_height, 0, out_image.data(),
                                profile_dimensions, profile_dimensions, 0, STBI_rgb, 0,
                                STBIR_FILTER_BOX);
        image.clear();
        if (!stbi_write_jpg_to_func(JPGToMemory, &image, profile_dimensions, profile_dimensions,
                                    STBI_rgb, out_image.data(), 90)) {
            LOG_ERROR(Service_NS, "Failed to resize the user provided image.");
        }
    }

    stbi_image_free(plain_image);

    if (image.size() > max_jpeg_image_size) {
        image.resize(max_jpeg_image_size);
    }
}

} // namespace

// IAsyncValue implementation for ListApplicationTitle
// https://switchbrew.org/wiki/NS_services#ListApplicationTitle
class IAsyncValueForListApplicationTitle final
    : public ServiceFramework<IAsyncValueForListApplicationTitle> {
public:
    explicit IAsyncValueForListApplicationTitle(Core::System& system_, s32 offset, s32 size)
        : ServiceFramework{system_, "IAsyncValue"}, service_context{system_, "IAsyncValue"},
          data_offset{offset}, data_size{size} {
        static const FunctionInfo functions[] = {
            {0, &IAsyncValueForListApplicationTitle::GetSize, "GetSize"},
            {1, &IAsyncValueForListApplicationTitle::Get, "Get"},
            {2, &IAsyncValueForListApplicationTitle::Cancel, "Cancel"},
            {3, &IAsyncValueForListApplicationTitle::GetErrorContext, "GetErrorContext"},
        };
        RegisterHandlers(functions);

        completion_event = service_context.CreateEvent("IAsyncValue:Completion");
        completion_event->GetReadableEvent().Signal();
    }

    ~IAsyncValueForListApplicationTitle() override {
        service_context.CloseEvent(completion_event);
    }

    Kernel::KReadableEvent& ReadableEvent() const {
        return completion_event->GetReadableEvent();
    }

private:
    void GetSize(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NS, "called");
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<s64>(data_size);
    }

    void Get(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NS, "called");
        std::vector<u8> buffer(sizeof(s32));
        std::memcpy(buffer.data(), &data_offset, sizeof(s32));
        ctx.WriteBuffer(buffer);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Cancel(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NS, "called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetErrorContext(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NS, "called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* completion_event{};
    s32 data_offset;
    s32 data_size;
};

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
        {5, D<&IReadOnlyApplicationControlDataInterface::GetApplicationControlData2>, "GetApplicationControlData"},
        {13, &IReadOnlyApplicationControlDataInterface::ListApplicationTitle, "ListApplicationTitle"},
        {19, D<&IReadOnlyApplicationControlDataInterface::GetApplicationControlData3>, "GetApplicationControlData"},
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

Result IReadOnlyApplicationControlDataInterface::GetApplicationControlData2(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, Out<u64> out_total_size,
    ApplicationControlSource application_control_source, u8 flag1, u8 flag2, u64 application_id) {
    LOG_INFO(Service_NS,
             "called with control_source={}, flags=({:02X},{:02X}), application_id={:016X}",
             application_control_source, flag1, flag2, application_id);

    const FileSys::PatchManager pm{application_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto control = pm.GetControlMetadata();
    const auto size = out_buffer.size();

    const auto nacp_size = sizeof(FileSys::RawNACP);

    if (size < nacp_size) {
        LOG_ERROR(Service_NS, "output buffer is too small! (actual={:016X}, expected_min={:08X})",
                  size, nacp_size);
        R_THROW(ResultUnknown);
    }

    if (control.first != nullptr) {
        const auto bytes = control.first->GetRawBytes();
        const auto copy_len =
            (std::min)(static_cast<size_t>(bytes.size()), static_cast<size_t>(nacp_size));
        std::memcpy(out_buffer.data(), bytes.data(), copy_len);
        if (copy_len < nacp_size) {
            std::memset(out_buffer.data() + copy_len, 0, nacp_size - copy_len);
        }
    } else {
        LOG_WARNING(Service_NS, "missing NACP data for application_id={:016X}", application_id);
        std::memset(out_buffer.data(), 0, nacp_size);
    }

    const auto icon_area_size = size - nacp_size;
    std::vector<u8> final_icon_data;

    if (control.second != nullptr) {
        size_t full_size = control.second->GetSize();
        if (full_size > 0) {
            final_icon_data.resize(full_size);
            control.second->Read(final_icon_data.data(), full_size);

            if (flag1 == 1) {
                SanitizeJPEGImageSize(final_icon_data);
            }
        }
    }

    size_t available_icon_bytes = final_icon_data.size();

    if (icon_area_size > 0) {
        const size_t to_copy = (std::min)(available_icon_bytes, icon_area_size);

        if (to_copy > 0) {
            std::memcpy(out_buffer.data() + nacp_size, final_icon_data.data(), to_copy);
        }

        if (to_copy < icon_area_size) {
            std::memset(out_buffer.data() + nacp_size + to_copy, 0, icon_area_size - to_copy);
        }
    }

    const u32 total_available = static_cast<u32>(nacp_size + available_icon_bytes);

    *out_total_size = (static_cast<u64>(total_available) << 32) | static_cast<u64>(flag1);
    R_SUCCEED();
}

void IReadOnlyApplicationControlDataInterface::ListApplicationTitle(HLERequestContext& ctx) {
    const auto app_ids_buffer = ctx.ReadBuffer();
    const size_t app_count = app_ids_buffer.size() / sizeof(u64);

    std::vector<u64> application_ids(app_count);
    if (app_count > 0) {
        std::memcpy(application_ids.data(), app_ids_buffer.data(), app_count * sizeof(u64));
    }

    auto t_mem_obj = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(ctx.GetCopyHandle(0));
    auto* t_mem = t_mem_obj.GetPointerUnsafe();

    constexpr size_t title_entry_size = sizeof(FileSys::LanguageEntry);
    const size_t total_data_size = app_count * title_entry_size;

    constexpr s32 data_offset = 0;

    if (t_mem != nullptr && app_count > 0) {
        auto& memory = system.ApplicationMemory();
        const auto t_mem_address = t_mem->GetSourceAddress();

        for (size_t i = 0; i < app_count; ++i) {
            const u64 app_id = application_ids[i];
            const FileSys::PatchManager pm{app_id, system.GetFileSystemController(),
                                           system.GetContentProvider()};
            const auto control = pm.GetControlMetadata();

            FileSys::LanguageEntry entry{};
            if (control.first != nullptr) {
                entry = control.first->GetLanguageEntry();
            }

            const size_t offset = i * title_entry_size;
            memory.WriteBlock(t_mem_address + offset, &entry, title_entry_size);
        }
    }

    auto async_value = std::make_shared<IAsyncValueForListApplicationTitle>(
        system, data_offset, static_cast<s32>(total_data_size));

    IPC::ResponseBuilder rb{ctx, 2, 1, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(async_value->ReadableEvent());
    rb.PushIpcInterface(std::move(async_value));
}

Result IReadOnlyApplicationControlDataInterface::GetApplicationControlData3(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, Out<u32> out_flags_a, Out<u32> out_flags_b,
    Out<u32> out_actual_size, ApplicationControlSource application_control_source, u8 flag1,
    u8 flag2, u64 application_id) {
    LOG_INFO(Service_NS,
             "called with control_source={}, flags=({:02X},{:02X}), application_id={:016X}",
             application_control_source, flag1, flag2, application_id);

    const FileSys::PatchManager pm{application_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto control = pm.GetControlMetadata();
    const auto size = out_buffer.size();

    const auto nacp_size = sizeof(FileSys::RawNACP);

    if (size < nacp_size) {
        LOG_ERROR(Service_NS, "output buffer is too small! (actual={:016X}, expected_min={:08X})",
                  size, nacp_size);
        R_THROW(ResultUnknown);
    }

    if (control.first != nullptr) {
        const auto bytes = control.first->GetRawBytes();
        const auto copy_len =
            (std::min)(static_cast<size_t>(bytes.size()), static_cast<size_t>(nacp_size));
        std::memcpy(out_buffer.data(), bytes.data(), copy_len);
        if (copy_len < nacp_size) {
            std::memset(out_buffer.data() + copy_len, 0, nacp_size - copy_len);
        }
    } else {
        LOG_WARNING(Service_NS, "missing NACP data for application_id={:016X}, defaulting to zero",
                    application_id);
        std::memset(out_buffer.data(), 0, nacp_size);
    }

    const auto icon_area_size = size - nacp_size;
    std::vector<u8> final_icon_data;

    if (control.second != nullptr) {
        size_t full_size = control.second->GetSize();
        if (full_size > 0) {
            final_icon_data.resize(full_size);
            control.second->Read(final_icon_data.data(), full_size);
            // TODO: Implement SanitizeJPEGImageSize(final_icon_data) if flag1 == 1
        }
    }

    size_t available_icon_bytes = final_icon_data.size();

    if (icon_area_size > 0) {
        const size_t to_copy = (std::min)(available_icon_bytes, icon_area_size);
        if (to_copy > 0) {
            std::memcpy(out_buffer.data() + nacp_size, final_icon_data.data(), to_copy);
        }
        if (to_copy < icon_area_size) {
            std::memset(out_buffer.data() + nacp_size + to_copy, 0, icon_area_size - to_copy);
        }
    } else {
        std::memset(out_buffer.data() + nacp_size, 0, icon_area_size);
    }

    const u32 actual_total_size = static_cast<u32>(nacp_size + available_icon_bytes);

    // Out 1: always 0x10001 (likely presents flags: Bit0=Icon, Bit16=NACP)
    // Out 2: reflects flag1 application (0 if flag1=0, 0x10001 if flag1=1)
    // Out 3: The actual size of data
    *out_flags_a = 0x10001;
    *out_flags_b = (flag1 == 1) ? 0x10001 : 0;
    *out_actual_size = actual_total_size;

    R_SUCCEED();
}

} // namespace Service::NS
