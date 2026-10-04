// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "common/fs/file.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/service/acc/dauth_0.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"

// [UNITY-FIX] undef Win32 macros shadowing ServiceContext methods.
#undef CreateEvent
#undef CreateMutex
#undef CreateSemaphore

namespace Service::Account {

namespace {

constexpr std::string_view STUB_APPLICATION_AUTH_TOKEN{
    "eyJhbGciOiJub25lIiwidHlwIjoiSldUIn0."
    "eyJzdWIiOiIwMTAwZDcxMDA0Njk0MDAwIiwiYXVkIjoiYWF1dGgiLCJleHAiOjIwMDAwMDAwMDAsImlhdCI6"
    "MTcwMDAwMDAwMH0."
    ""};

std::filesystem::path GetDauthApplicationAuthCachePath(u64 title_id) {
    return Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) /
           fmt::format("system/save/8000000000000010/su/dauth/application/{:016X}.dat", title_id);
}

bool WriteBinaryFile(const std::filesystem::path& path, std::span<const u8> data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        LOG_WARNING(Service_ACC, "dauth: failed to create directory {}: {}",
                    path.parent_path().string(), ec.message());
        return false;
    }

    Common::FS::IOFile file(path, Common::FS::FileAccessMode::Write, Common::FS::FileType::BinaryFile);
    if (!file.IsOpen()) {
        LOG_WARNING(Service_ACC, "dauth: failed to open {}", path.string());
        return false;
    }

    if (file.Write(data) != data.size()) {
        LOG_WARNING(Service_ACC, "dauth: failed to write {}", path.string());
        return false;
    }

    return true;
}

std::vector<u8> ReadApplicationAuthTokenFromDisk(u64 title_id) {
    const auto path = GetDauthApplicationAuthCachePath(title_id);
    Common::FS::IOFile file(path, Common::FS::FileAccessMode::Read, Common::FS::FileType::BinaryFile);
    if (!file.IsOpen()) {
        return std::vector<u8>(STUB_APPLICATION_AUTH_TOKEN.begin(), STUB_APPLICATION_AUTH_TOKEN.end());
    }

    std::vector<u8> data(static_cast<std::size_t>(file.GetSize()));
    if (file.Read(data) != data.size() || data.empty()) {
        return std::vector<u8>(STUB_APPLICATION_AUTH_TOKEN.begin(), STUB_APPLICATION_AUTH_TOKEN.end());
    }

    return data;
}

class DauthAsyncResult final : public ServiceFramework<DauthAsyncResult> {
public:
    explicit DauthAsyncResult(Core::System& system_)
        : ServiceFramework{system_, "IAsyncResult"},
          service_context{system_, "Dauth:IAsyncResult"} {
        static const FunctionInfo functions[] = {
            {0, &DauthAsyncResult::GetResult, "GetResult"},
            {1, &DauthAsyncResult::Cancel, "Cancel"},
            {2, &DauthAsyncResult::IsAvailable, "IsAvailable"},
            {3, &DauthAsyncResult::GetSystemEvent, "GetSystemEvent"},
        };
        RegisterHandlers(functions);

        completion_event = service_context.CreateEvent("Dauth:IAsyncResult:CompletionEvent");
        completion_event->Signal();
    }

    ~DauthAsyncResult() override {
        service_context.CloseEvent(completion_event);
    }

private:
    void GetResult(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "dauth: IAsyncResult::GetResult called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Cancel(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "dauth: IAsyncResult::Cancel called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void IsAvailable(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "dauth: IAsyncResult::IsAvailable called, available=true");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u8>(1);
    }

    void GetSystemEvent(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "dauth: IAsyncResult::GetSystemEvent called");
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(completion_event->GetReadableEvent());
    }

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* completion_event{};
};

void PushDauthAsyncResult(Core::System& system, HLERequestContext& ctx) {
    auto async = std::make_shared<DauthAsyncResult>(system);

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface(async);
}

} // Anonymous namespace

void WriteDauthApplicationAuthCacheOnDisk(u64 title_id) {
    const std::vector<u8> token(STUB_APPLICATION_AUTH_TOKEN.begin(), STUB_APPLICATION_AUTH_TOKEN.end());
    const auto path = GetDauthApplicationAuthCachePath(title_id);
    if (WriteBinaryFile(path, token)) {
        LOG_INFO(Service_ACC, "dauth: wrote application auth cache to {}", path.string());
    }
}

DAUTH_0::DAUTH_0(Core::System& system_) : ServiceFramework{system_, "dauth:0"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &DAUTH_0::EnsureAuthenticationTokenCacheAsync, "EnsureAuthenticationTokenCacheAsync"},
        {1, &DAUTH_0::LoadAuthenticationTokenCache, "LoadAuthenticationTokenCache"},
        {2, nullptr, "InvalidateAuthenticationTokenCache"},
        {3, &DAUTH_0::IsDeviceAuthenticationTokenCacheAvailable, "IsDeviceAuthenticationTokenCacheAvailable"},
        {10, nullptr, "EnsureEdgeTokenCacheAsync"},
        {11, nullptr, "LoadEdgeTokenCache"},
        {12, nullptr, "InvalidateEdgeTokenCache"},
        {13, nullptr, "IsEdgeTokenCacheAvailable"},
        {20, &DAUTH_0::EnsureApplicationAuthenticationCacheAsync, "EnsureApplicationAuthenticationCacheAsync"},
        {21, &DAUTH_0::LoadApplicationAuthenticationTokenCache, "LoadApplicationAuthenticationTokenCache"},
        {22, nullptr, "LoadApplicationNetworkServiceClientConfigCache"},
        {23, &DAUTH_0::IsApplicationAuthenticationCacheAvailable, "IsApplicationAuthenticationCacheAvailable"},
        {24, nullptr, "InvalidateApplicationAuthenticationCache"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

DAUTH_0::~DAUTH_0() = default;

void DAUTH_0::EnsureAuthenticationTokenCacheAsync(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "dauth:0 EnsureAuthenticationTokenCacheAsync called");
    PushDauthAsyncResult(system, ctx);
}

void DAUTH_0::LoadAuthenticationTokenCache(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "dauth:0 LoadAuthenticationTokenCache called");

    constexpr std::string_view stub_device_token{"stub-device-auth-token"};
    const std::vector<u8> token(stub_device_token.begin(), stub_device_token.end());
    if (ctx.CanWriteBuffer(0)) {
        ctx.WriteBuffer(token, 0);
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(static_cast<u32>(token.size()));
}

void DAUTH_0::IsDeviceAuthenticationTokenCacheAvailable(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "dauth:0 IsDeviceAuthenticationTokenCacheAvailable called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(1);
}

void DAUTH_0::EnsureApplicationAuthenticationCacheAsync(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "dauth:0 EnsureApplicationAuthenticationCacheAsync called");
    WriteDauthApplicationAuthCacheOnDisk(system.GetApplicationProcessProgramID());
    PushDauthAsyncResult(system, ctx);
}

void DAUTH_0::LoadApplicationAuthenticationTokenCache(HLERequestContext& ctx) {
    const u64 title_id = system.GetApplicationProcessProgramID();
    LOG_INFO(Service_ACC, "dauth:0 LoadApplicationAuthenticationTokenCache called, title_id={:016X}",
             title_id);

    auto token = ReadApplicationAuthTokenFromDisk(title_id);
    if (ctx.CanWriteBuffer(0)) {
        ctx.WriteBuffer(token, 0);
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(static_cast<u32>(token.size()));
}

void DAUTH_0::IsApplicationAuthenticationCacheAvailable(HLERequestContext& ctx) {
    const u64 title_id = system.GetApplicationProcessProgramID();
    const auto path = GetDauthApplicationAuthCachePath(title_id);
    const bool available = std::filesystem::exists(path);

    LOG_INFO(Service_ACC,
             "dauth:0 IsApplicationAuthenticationCacheAvailable called, available={}",
             available);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(available ? 1 : 0);
}

} // namespace Service::Account
