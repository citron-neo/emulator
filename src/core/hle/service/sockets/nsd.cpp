// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/sockets/nsd.h"

#include "common/string_util.h"

namespace Service::Sockets {

[[maybe_unused]] constexpr Result ResultNotImplemented{ErrorModule::NSD, 1}; // Generic "not implemented"
[[maybe_unused]] constexpr Result ResultNsdNotInitialized{ErrorModule::NSD, 2}; // Example, if needed
constexpr Result ResultPermissionDenied{ErrorModule::NSD, 3}; // For nsd:a specific calls
constexpr Result ResultOverflow{ErrorModule::NSD, 6};

// This is nn::oe::ServerEnvironmentType
enum class ServerEnvironmentType : u8 {
    Dd,
    Lp,
    Sd,
    Sp,
    Dp,
};

// This is nn::nsd::EnvironmentIdentifier
struct EnvironmentIdentifier {
    std::array<u8, 8> identifier;
};
static_assert(sizeof(EnvironmentIdentifier) == 0x8);

// nn::nsd::Url (0x100-byte struct)
struct Url {
    std::array<u8, 0x100> value;
};
static_assert(sizeof(Url) == 0x100);

// nn::nsd::SettingName (0x100-byte struct)
struct SettingName {
    std::array<u8, 0x100> value;
};
static_assert(sizeof(SettingName) == 0x100);

// nn::nsd::DeviceId (0x10-byte struct)
struct DeviceId {
    std::array<u8, 0x10> value;
};
static_assert(sizeof(DeviceId) == 0x10);

// nn::nsd::Fqdn (0x100-byte struct, containing a string)
// Using FqdnStruct to avoid conflict with potential using alias for std::array<char, 0x100>
struct FqdnStruct {
    std::array<char, 0x100> value;
};
static_assert(sizeof(FqdnStruct) == 0x100);

// nn::nsd::NasServiceSetting (0x108-byte struct)
struct NasServiceSetting {
    std::array<u8, 0x108> value;
};
static_assert(sizeof(NasServiceSetting) == 0x108);

// nn::nsd::NasServiceName (0x10-byte struct)
struct NasServiceName {
    std::array<u8, 0x10> value;
};
static_assert(sizeof(NasServiceName) == 0x10);

// nn::nsd::SaveData (0x12BF0-byte struct)
struct SaveData {
    std::array<u8, 0x12BF0> value;
};
static_assert(sizeof(SaveData) == 0x12BF0);

// nn::nsd::detail::TestParameter (0x80-byte struct)
struct TestParameter {
    std::array<u8, 0x80> value;
};
static_assert(sizeof(TestParameter) == 0x80);

// nn::nsd::DeleteMode (enum)
enum class DeleteMode : u32 { // Assuming u32, adjust if different
    Mode0 = 0,
    Mode1 = 1,
};

// nn::nsd::ImportMode (enum)
enum class ImportMode : u32 { // Assuming u32, adjust if different
    Mode0 = 0,
    Mode1 = 1,
};

NSD::NSD(Core::System& system_, const char* name) : ServiceFramework{system_, name} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {5, &NSD::GetSettingUrl, "GetSettingUrl"},
        {10, &NSD::GetSettingName, "GetSettingName"},
        {11, &NSD::GetEnvironmentIdentifier, "GetEnvironmentIdentifier"},
        {12, &NSD::GetDeviceId, "GetDeviceId"},
        {13, &NSD::DeleteSettings, "DeleteSettings"},
        {14, &NSD::ImportSettings, "ImportSettings"},
        {15, &NSD::SetChangeEnvironmentIdentifierDisabled, "SetChangeEnvironmentIdentifierDisabled"},
        {20, &NSD::Resolve, "Resolve"},
        {21, &NSD::ResolveEx, "ResolveEx"},
        {30, &NSD::GetNasServiceSetting, "GetNasServiceSetting"},
        {31, &NSD::GetNasServiceSettingEx, "GetNasServiceSettingEx"},
        {40, &NSD::GetNasRequestFqdn, "GetNasRequestFqdn"},
        {41, &NSD::GetNasRequestFqdnEx, "GetNasRequestFqdnEx"},
        {42, &NSD::GetNasApiFqdn, "GetNasApiFqdn"},
        {43, &NSD::GetNasApiFqdnEx, "GetNasApiFqdnEx"},
        {50, &NSD::GetCurrentSetting, "GetCurrentSetting"},
        {51, &NSD::WriteTestParameter, "WriteTestParameter"},
        {52, &NSD::ReadTestParameter, "ReadTestParameter"},
        {60, &NSD::ReadSaveDataFromFsForTest, "ReadSaveDataFromFsForTest"},
        {61, &NSD::WriteSaveDataToFsForTest, "WriteSaveDataToFsForTest"},
        {62, &NSD::DeleteSaveDataOfFsForTest, "DeleteSaveDataOfFsForTest"},
        {63, &NSD::IsChangeEnvironmentIdentifierDisabled, "IsChangeEnvironmentIdentifierDisabled"},
        {64, &NSD::SetWithoutDomainExchangeFqdns, "SetWithoutDomainExchangeFqdns"},
        {100, &NSD::GetApplicationServerEnvironmentType, "GetApplicationServerEnvironmentType"},
        {101, &NSD::SetApplicationServerEnvironmentType, "SetApplicationServerEnvironmentType"},
        {102, &NSD::DeleteApplicationServerEnvironmentType, "DeleteApplicationServerEnvironmentType"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

static std::string ResolveImpl(const std::string& fqdn_in) {
    // The real implementation makes various substitutions.
    // For now we just return the string as-is, which is good enough when not
    // connecting to real Nintendo servers.
    LOG_WARNING(Service, "(STUBBED) called, fqdn_in={}", fqdn_in);
    return fqdn_in;
}

static Result ResolveCommon(const std::string& fqdn_in, std::array<char, 0x100>& fqdn_out) {
    const auto res = ResolveImpl(fqdn_in);
    if (res.size() >= fqdn_out.size()) {
        return ResultOverflow;
    }
    std::memcpy(fqdn_out.data(), res.c_str(), res.size() + 1);
    return ResultSuccess;
}

void NSD::Resolve(HLERequestContext& ctx) {
    const std::string fqdn_in = Common::StringFromBuffer(ctx.ReadBuffer(0));

    std::array<char, 0x100> fqdn_out{};
    const Result res = ResolveCommon(fqdn_in, fqdn_out);

    ctx.WriteBuffer(fqdn_out);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(res);
}

void NSD::ResolveEx(HLERequestContext& ctx) {
    const std::string fqdn_in = Common::StringFromBuffer(ctx.ReadBuffer(0));

    std::array<char, 0x100> fqdn_out;
    const Result res = ResolveCommon(fqdn_in, fqdn_out);

    if (res.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
        return;
    }

    ctx.WriteBuffer(fqdn_out);
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push(ResultSuccess);
}

void NSD::GetEnvironmentIdentifier(HLERequestContext& ctx) {
    constexpr EnvironmentIdentifier lp1 = {
        .identifier = {'l', 'p', '1', '\0', '\0', '\0', '\0', '\0'}};
    ctx.WriteBuffer(lp1);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::GetApplicationServerEnvironmentType(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(static_cast<u32>(ServerEnvironmentType::Lp));
}

void NSD::GetSettingUrl(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) GetSettingUrl called");

    Url url{}; // Zero-initialize
    ctx.WriteBuffer(url); // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::GetSettingName(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) GetSettingName called");

    SettingName setting_name{}; // Zero-initialize
    ctx.WriteBuffer(setting_name); // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::GetDeviceId(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) GetDeviceId called");

    DeviceId device_id{}; // Zero-initialize
    // TODO: Fill with some dummy data if needed, e.g., from settings
    // For now, just zeroed.
    ctx.WriteBuffer(device_id); // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::DeleteSettings(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const auto delete_mode = rp.PopEnum<DeleteMode>();
    [[maybe_unused]] const auto setting_name_buffer = ctx.ReadBuffer(0); // Type-0x5 input buffer for SettingName

    LOG_WARNING(Service, "(STUBBED) DeleteSettings called, mode={}", static_cast<u32>(delete_mode));
    // Real impl would use setting_name_buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess); // Or ResultNotImplemented
}

void NSD::ImportSettings(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const auto import_mode = rp.PopEnum<ImportMode>();
    [[maybe_unused]] const auto setting_name_buffer = ctx.ReadBuffer(0); // Type-0x5 input buffer for SettingName
    [[maybe_unused]] const auto save_data_buffer = ctx.ReadBuffer(1);    // Type-0x5 input buffer for SaveData

    LOG_WARNING(Service, "(STUBBED) ImportSettings called, mode={}", static_cast<u32>(import_mode));
    // Real impl would use setting_name_buffer and save_data_buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess); // Or ResultNotImplemented
}

void NSD::SetChangeEnvironmentIdentifierDisabled(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const bool disabled = rp.Pop<bool>();
    LOG_WARNING(Service, "(STUBBED) SetChangeEnvironmentIdentifierDisabled called, disabled={}", disabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::GetNasServiceSetting(HLERequestContext& ctx) {
    [[maybe_unused]] const auto service_name_buffer = ctx.ReadBuffer(0); // Type-0x9 input buffer for NasServiceName

    LOG_WARNING(Service, "(STUBBED) GetNasServiceSetting called");

    NasServiceSetting service_setting{}; // Zero-initialize
    ctx.WriteBuffer(service_setting);   // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::GetNasServiceSettingEx(HLERequestContext& ctx) {
    [[maybe_unused]] const auto service_name_buffer = ctx.ReadBuffer(0); // Type-0x9 input buffer for NasServiceName

    LOG_WARNING(Service, "(STUBBED) GetNasServiceSettingEx called");

    NasServiceSetting service_setting{}; // Zero-initialize
    ctx.WriteBuffer(service_setting);   // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);         // Outer Result
    rb.Push(ResultSuccess); // nn::nsd::InnerResult
}

void NSD::GetNasRequestFqdn(HLERequestContext& ctx) {
    [[maybe_unused]] const auto service_name_buffer = ctx.ReadBuffer(0); // Type-0x9 input buffer for NasServiceName

    LOG_WARNING(Service, "(STUBBED) GetNasRequestFqdn called");

    FqdnStruct fqdn{};                       // Zero-initialize
    ctx.WriteBuffer(fqdn); // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::GetNasRequestFqdnEx(HLERequestContext& ctx) {
    [[maybe_unused]] const auto service_name_buffer = ctx.ReadBuffer(0); // Type-0x9 input buffer for NasServiceName

    LOG_WARNING(Service, "(STUBBED) GetNasRequestFqdnEx called");

    FqdnStruct fqdn{};                       // Zero-initialize
    ctx.WriteBuffer(fqdn); // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);         // Outer Result
    rb.Push(ResultSuccess); // nn::nsd::InnerResult
}

void NSD::GetNasApiFqdn(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) GetNasApiFqdn called");

    FqdnStruct fqdn{};                       // Zero-initialize
    ctx.WriteBuffer(fqdn); // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::GetNasApiFqdnEx(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) GetNasApiFqdnEx called");

    FqdnStruct fqdn{};                       // Zero-initialize
    ctx.WriteBuffer(fqdn); // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);         // Outer Result
    rb.Push(ResultSuccess); // nn::nsd::InnerResult
}

bool NSD::IsNsdA() const {
    // A simple way to check if this instance is nsd:a
    // This relies on the name passed during construction.
    return strncmp(service_name.c_str(), "nsd:a", 5) == 0;
}

void NSD::GetCurrentSetting(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) GetCurrentSetting called");
    if (!IsNsdA()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultPermissionDenied);
        return;
    }

    SaveData save_data{};                 // Zero-initialize
    ctx.WriteBuffer(save_data); // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::WriteTestParameter(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) WriteTestParameter called");
    if (!IsNsdA()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultPermissionDenied);
        return;
    }
    [[maybe_unused]] const auto test_param_buffer = ctx.ReadBuffer(0); // Type-0x5 input buffer for TestParameter

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::ReadTestParameter(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) ReadTestParameter called");
    if (!IsNsdA()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultPermissionDenied);
        return;
    }
    TestParameter test_param{};           // Zero-initialize
    ctx.WriteBuffer(test_param); // Type-0x6 output buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::ReadSaveDataFromFsForTest(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) ReadSaveDataFromFsForTest called");
    if (!IsNsdA()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultPermissionDenied);
        return;
    }
    SaveData save_data{};                 // Zero-initialize
    ctx.WriteBuffer(save_data); // Type-0x16 output buffer

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::WriteSaveDataToFsForTest(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) WriteSaveDataToFsForTest called");
    if (!IsNsdA()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultPermissionDenied);
        return;
    }
    [[maybe_unused]] const auto save_data_buffer = ctx.ReadBuffer(0); // Type-0x15 input buffer for SaveData

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::DeleteSaveDataOfFsForTest(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) DeleteSaveDataOfFsForTest called");
    if (!IsNsdA()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultPermissionDenied);
        return;
    }

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::IsChangeEnvironmentIdentifierDisabled(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) IsChangeEnvironmentIdentifierDisabled called");
    IPC::ResponseBuilder rb{ctx, 3}; // bool is 1 byte, Result is 2 bytes, total 3. Or is bool promoted? Assume u32 for IPC.
    rb.Push(ResultSuccess);
    rb.Push<u8>(false); // Return false (not disabled) by default
}

void NSD::SetWithoutDomainExchangeFqdns(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) SetWithoutDomainExchangeFqdns called");
    // Takes type-0x5 input buffer containing an array of Fqdn. Max 2.
    // Requires nsd!test_mode == 1
    [[maybe_unused]] const auto fqdn_array_buffer = ctx.ReadBuffer(0);

    // For now, assume it's not test mode or doesn't matter for stub
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::SetApplicationServerEnvironmentType(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const auto env_type = rp.PopEnum<ServerEnvironmentType>();
    LOG_WARNING(Service, "(STUBBED) SetApplicationServerEnvironmentType called, type={}", static_cast<u8>(env_type));

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::DeleteApplicationServerEnvironmentType(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) DeleteApplicationServerEnvironmentType called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

NSD::~NSD() = default;

} // namespace Service::Sockets
