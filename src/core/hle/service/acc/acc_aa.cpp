// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/acc/acc_aa.h"
#include "core/hle/service/acc/async_context.h"
#include "core/hle/service/ipc_helpers.h"
#include "common/logging.h"
#include <cstring>

namespace Service::Account {

namespace {

class CompletedBaasCacheAsync final : public IAsyncContext {
public:
    explicit CompletedBaasCacheAsync(Core::System& system_) : IAsyncContext{system_} {
        MarkComplete();
    }

protected:
    bool IsComplete() const override {
        return true;
    }

    void Cancel() override {}

    Result GetResult() const override {
        return ResultSuccess;
    }
};

} // Anonymous namespace

ACC_AA::ACC_AA(Core::System& system_) : ServiceFramework{system_, "acc:aa"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ACC_AA::EnsureCacheAsync, "EnsureCacheAsync"},
        {1, &ACC_AA::LoadCache, "LoadCache"},
        {2, &ACC_AA::GetDeviceAccountId, "GetDeviceAccountId"},
        {50, nullptr, "RegisterNotificationTokenAsync"},   // 1.0.0 - 6.2.0
        {51, nullptr, "UnregisterNotificationTokenAsync"}, // 1.0.0 - 6.2.0
    };
    // clang-format on
    RegisterHandlers(functions);
}

ACC_AA::~ACC_AA() = default;

void ACC_AA::EnsureCacheAsync(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "acc:aa EnsureCacheAsync called");

    auto async = std::make_shared<CompletedBaasCacheAsync>(system);

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface(async);
}

void ACC_AA::LoadCache(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "acc:aa LoadCache called");

    // Baas device-account cache: network service account id + linked/registered flags.
    std::vector<u8> cache(0x40, 0);
    const u64 network_service_account_id = 0x000000000000CAFEULL;
    std::memcpy(cache.data(), &network_service_account_id, sizeof(network_service_account_id));
    cache[0x08] = 1;
    cache[0x09] = 1;
    const u32 license_kind = 2;
    std::memcpy(cache.data() + 0x0C, &license_kind, sizeof(license_kind));

    if (ctx.CanWriteBuffer(0)) {
        ctx.WriteBuffer(cache, 0);
    }

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ACC_AA::GetDeviceAccountId(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "acc:aa GetDeviceAccountId called");

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.PushRaw<u64>(0x0123456789ABCDEFULL);
}

} // namespace Service::Account
