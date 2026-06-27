// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <atomic>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"
#include "common/fs/file.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include <ranges>
#include "common/stb.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/constants.h"
#include "core/core.h"
#include "core/hle/api_version.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_event.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/service/acc/acc.h"
#include "core/hle/service/acc/acc_aa.h"
#include "core/hle/service/acc/acc_e.h"
#include "core/hle/service/acc/acc_e_u1.h"
#include "core/hle/service/acc/acc_e_u2.h"
#include "core/hle/service/acc/acc_su.h"
#include "core/hle/service/acc/acc_u0.h"
#include "core/hle/service/acc/acc_u1.h"
#include "core/hle/service/acc/async_context.h"
#include "core/hle/service/acc/dauth_0.h"
#include "core/hle/service/acc/errors.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/server_manager.h"
#include "core/loader/loader.h"

namespace Service::Account {

// Thumbnails are hard coded to be at least this size
constexpr std::size_t THUMBNAIL_SIZE = 0x24000;

// Forward declaration — full definition appears later in this file after the async interfaces.
class EnsureTokenIdCacheAsyncInterface;

static std::filesystem::path GetImagePath(const Common::UUID& uuid) {
    return Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) /
           fmt::format("system/save/8000000000000010/su/avators/{}.jpg", uuid.FormattedString());
}

static void JPGToMemory(void* context, void* data, int len) {
    std::vector<u8>* jpg_image = static_cast<std::vector<u8>*>(context);
    unsigned char* jpg = static_cast<unsigned char*>(data);
    jpg_image->insert(jpg_image->end(), jpg, jpg + len);
}

static void SanitizeJPEGImageSize(std::vector<u8>& image) {
    constexpr std::size_t max_jpeg_image_size = 0x20000;
    constexpr int profile_dimensions = 256;
    int original_width, original_height, color_channels;

    const auto plain_image =
        stbi_load_from_memory(image.data(), static_cast<int>(image.size()), &original_width,
                              &original_height, &color_channels, STBI_rgb);

    // Resize image to match 256*256
    if (original_width != profile_dimensions || original_height != profile_dimensions) {
        // Use vector instead of array to avoid overflowing the stack
        std::vector<u8> out_image(profile_dimensions * profile_dimensions * STBI_rgb);
        stbir_resize_uint8_srgb(plain_image, original_width, original_height, 0, out_image.data(),
                                profile_dimensions, profile_dimensions, 0, STBI_rgb, 0,
                                STBIR_FILTER_BOX);
        image.clear();
        if (!stbi_write_jpg_to_func(JPGToMemory, &image, profile_dimensions, profile_dimensions,
                                    STBI_rgb, out_image.data(), 0)) {
            LOG_ERROR(Service_ACC, "Failed to resize the user provided image.");
        }
    }

    image.resize(std::min(image.size(), max_jpeg_image_size));
}

namespace {

// Bedrock treats license kind 0 as "no license" and shows "Something went wrong".
// Ryujinx stubs NSO as Subscribed (2), not Personal (1).
constexpr u32 NETWORK_SERVICE_LICENSE_KIND_SUBSCRIBED = 2;
constexpr u32 NETWORK_SERVICE_LICENSE_KIND_BEDROCK = NETWORK_SERVICE_LICENSE_KIND_SUBSCRIBED;
constexpr u64 STUB_NETWORK_SERVICE_ACCOUNT_ID = 0x000000000000CAFEULL;
constexpr s64 NETWORK_SERVICE_LICENSE_FAR_FUTURE_EXPIRATION = 0x7FFFFFFFFFFFFFFFLL;

std::string Base64UrlEncode(std::string_view input) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < input.size(); i += 3) {
        const u32 block = (static_cast<u8>(input[i]) << 16) |
                          ((i + 1 < input.size() ? static_cast<u8>(input[i + 1]) : 0) << 8) |
                          (i + 2 < input.size() ? static_cast<u8>(input[i + 2]) : 0);
        out.push_back(table[(block >> 18) & 0x3F]);
        out.push_back(table[(block >> 12) & 0x3F]);
        out.push_back(i + 1 < input.size() ? table[(block >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < input.size() ? table[block & 0x3F] : '=');
    }

    for (char& c : out) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        }
    }
    while (!out.empty() && out.back() == '=') {
        out.pop_back();
    }
    return out;
}

std::filesystem::path GetAccountSaveBasePath() {
    return Common::FS::GetCitronPath(Common::FS::CitronPath::NANDDir) /
           "system/save/8000000000000010";
}

std::filesystem::path GetBaasAccountCachePath(const Common::UUID& uuid) {
    return GetAccountSaveBasePath() / "su/baas" / fmt::format("{}.dat", uuid.FormattedString());
}

std::filesystem::path GetSuIdTokenCachePath(const Common::UUID& uuid) {
    return GetAccountSaveBasePath() / "su/cache" / fmt::format("{}.dat", uuid.FormattedString());
}

std::filesystem::path GetNasUserResourceCachePath(const Common::UUID& uuid) {
    return GetAccountSaveBasePath() / "su/nas" / fmt::format("{}.dat", uuid.FormattedString());
}

std::filesystem::path GetNetworkServiceLicenseCachePath(const Common::UUID& uuid) {
    return GetAccountSaveBasePath() / "su/license" /
           fmt::format("{}.dat", uuid.FormattedString());
}

bool WriteBinaryFile(const std::filesystem::path& path, std::span<const u8> data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        LOG_WARNING(Service_ACC, "Failed to create directory {}: {}", path.parent_path().string(),
                    ec.message());
        return false;
    }

    Common::FS::IOFile file(path, Common::FS::FileAccessMode::Write, Common::FS::FileType::BinaryFile);
    if (!file.IsOpen()) {
        LOG_WARNING(Service_ACC, "Failed to open {}", path.string());
        return false;
    }

    if (file.Write(data) != data.size()) {
        LOG_WARNING(Service_ACC, "Failed to write {}", path.string());
        return false;
    }

    return true;
}

constexpr std::size_t NAS_USER_BASE_SIZE = 0x68;
constexpr u64 STUB_NINTENDO_ACCOUNT_ID = 0x0123456789ABCDEFULL;

u64 GetStubNintendoAccountId(const Common::UUID& account_id) {
    const u64 derived = account_id.Hash();
    return derived != 0 ? derived : STUB_NINTENDO_ACCOUNT_ID;
}

std::string GetStubDeviceIdHex(const Common::UUID& user_id) {
    const u64 derived = user_id.IsInvalid() ? 0x0123456789ABCDEFULL : user_id.Hash();
    const u64 device_account = GetStubNintendoAccountId(user_id);
    return fmt::format("{:016x}{:016x}", derived, device_account);
}

std::vector<u8> GenerateStubIdToken(const Common::UUID& user_id) {
    // Bedrock validates Nintendo id_token JWT fields (iss/exp/aud/sub/di/bs:did). sub must match user.
    constexpr std::string_view header{R"({"alg":"none","typ":"JWT"})"};
    const std::string subject =
        user_id.IsInvalid() ? "00000000000000000000000000000001" : user_id.RawString();
    const std::string device_id = GetStubDeviceIdHex(user_id);
    const std::string payload = fmt::format(
        R"({{"sub":"{}","aud":"ed9e2f05d286f7b8","di":"{}","sn":"XAW10000000000","bs:did":"{}","iss":"https://e0d67c509fb203858ebcbcf2fe3f88c2aa.baas.nintendo.com","typ":"id_token","exp":2000000000,"iat":1700000000}})",
        subject, device_id, device_id);
    const std::string token =
        fmt::format("{}.{}.", Base64UrlEncode(header), Base64UrlEncode(payload));
    return std::vector<u8>(token.begin(), token.end());
}

void PopulateNasUserBaseForApplication(std::vector<u8>& out, u64 nintendo_account_id) {
    out.assign(NAS_USER_BASE_SIZE, 0);
    std::memcpy(out.data(), &nintendo_account_id, sizeof(nintendo_account_id));
    out[0x08] = 1; // is_nintendo_account_linked
    out[0x09] = 1; // is_network_service_account_registered
    if (out.size() >= 0x0C + sizeof(u32)) {
        const u32 license_kind = NETWORK_SERVICE_LICENSE_KIND_BEDROCK;
        std::memcpy(out.data() + 0x0C, &license_kind, sizeof(license_kind));
    }
}

void WriteAccountServiceCachesOnDisk(const Common::UUID& user_id, u64 title_id) {
    // su/baas: CheckAvailability / GetAccountId read NetworkServiceAccountId from here.
    std::vector<u8> baas_data(0x40, 0);
    const u64 network_service_account_id = STUB_NETWORK_SERVICE_ACCOUNT_ID;
    std::memcpy(baas_data.data(), &network_service_account_id, sizeof(network_service_account_id));
    baas_data[0x08] = 1; // is_nintendo_account_linked
    baas_data[0x09] = 1; // is_network_service_account_registered
    const u32 license_kind = NETWORK_SERVICE_LICENSE_KIND_BEDROCK;
    std::memcpy(baas_data.data() + 0x0C, &license_kind, sizeof(license_kind));

    const auto baas_path = GetBaasAccountCachePath(user_id);
    if (WriteBinaryFile(baas_path, baas_data)) {
        LOG_INFO(Service_ACC, "Wrote baas account cache to {}", baas_path.string());
    }

    // su/cache: LoadIdTokenCache reads the JWT id token from here (not su/baas).
    const auto token = GenerateStubIdToken(user_id);
    const auto cache_path = GetSuIdTokenCachePath(user_id);
    if (WriteBinaryFile(cache_path, token)) {
        LOG_INFO(Service_ACC, "Wrote id token cache to {}", cache_path.string());
    }

    const u64 nintendo_account_id = GetStubNintendoAccountId(user_id);
    std::vector<u8> nas_user_base;
    PopulateNasUserBaseForApplication(nas_user_base, nintendo_account_id);
    const auto nas_path = GetNasUserResourceCachePath(user_id);
    if (WriteBinaryFile(nas_path, nas_user_base)) {
        LOG_INFO(Service_ACC, "Wrote NAS user resource cache to {}", nas_path.string());
    }

    std::vector<u8> license_cache(sizeof(u32) + sizeof(s64), 0);
    std::memcpy(license_cache.data(), &license_kind, sizeof(license_kind));
    const s64 expiration = NETWORK_SERVICE_LICENSE_FAR_FUTURE_EXPIRATION;
    std::memcpy(license_cache.data() + sizeof(u32), &expiration, sizeof(expiration));
    const auto license_path = GetNetworkServiceLicenseCachePath(user_id);
    if (WriteBinaryFile(license_path, license_cache)) {
        LOG_INFO(Service_ACC, "Wrote network service license cache to {}", license_path.string());
    }

    if (title_id != 0) {
        WriteDauthApplicationAuthCacheOnDisk(title_id);
    }
}

struct BaasStubSessionState {
    bool id_token_cache_ready{};
    u32 network_service_license_kind{};
};

std::mutex baas_stub_session_mutex;
std::unordered_map<Common::UUID, BaasStubSessionState> baas_stub_session_cache;

BaasStubSessionState& GetOrCreateBaasStubSessionState(const Common::UUID& user_id) {
    return baas_stub_session_cache[user_id];
}

void PopulateBaasStubSessionCache(Core::System& system, const Common::UUID& user_id) {
    std::scoped_lock lock{baas_stub_session_mutex};
    auto& state = GetOrCreateBaasStubSessionState(user_id);
    state.id_token_cache_ready = true;
    state.network_service_license_kind = NETWORK_SERVICE_LICENSE_KIND_BEDROCK;
    WriteAccountServiceCachesOnDisk(user_id, system.GetApplicationProcessProgramID());
}

void EnsureDefaultAvatarExists(const Common::UUID& uuid) {
    const auto image_path = GetImagePath(uuid);
    if (std::filesystem::exists(image_path)) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(image_path.parent_path(), ec);
    if (ec) {
        LOG_WARNING(Service_ACC, "Failed to create avatar directory for {}: {}", image_path.string(),
                    ec.message());
        return;
    }

    Common::FS::IOFile image(image_path, Common::FS::FileAccessMode::Write,
                             Common::FS::FileType::BinaryFile);
    if (!image.IsOpen()) {
        LOG_WARNING(Service_ACC, "Failed to create default avatar at {}", image_path.string());
        return;
    }

    if (image.Write(Core::Constants::ACCOUNT_BACKUP_JPEG) !=
        Core::Constants::ACCOUNT_BACKUP_JPEG.size()) {
        LOG_WARNING(Service_ACC, "Failed to write default avatar at {}", image_path.string());
        return;
    }

    LOG_INFO(Service_ACC, "Created default avatar at {}", image_path.string());
}

void PushLoadIdTokenCacheResponse(HLERequestContext& ctx, const Common::UUID& user_id = {}) {
    std::vector<u8> token = GenerateStubIdToken(user_id);
    if (!user_id.IsInvalid()) {
        const auto cache_path = GetSuIdTokenCachePath(user_id);
        Common::FS::IOFile cache_file(cache_path, Common::FS::FileAccessMode::Read,
                                      Common::FS::FileType::BinaryFile);
        if (cache_file.IsOpen()) {
            token.resize(static_cast<std::size_t>(cache_file.GetSize()));
            if (cache_file.Read(token) != token.size()) {
                token = GenerateStubIdToken(user_id);
            }
        }
    }

    const u32 token_size = static_cast<u32>(token.size());
    if (ctx.CanWriteBuffer(0)) {
        ctx.WriteBuffer(token, 0);
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(token_size);
}

void PushAuthorizationCodeResponse(HLERequestContext& ctx) {
    constexpr std::string_view stub_code{"citron_stub_authorization_code"};
    const u32 code_size = static_cast<u32>(stub_code.size());

    if (ctx.CanWriteBuffer(0)) {
        std::vector<u8> buffer(ctx.GetWriteBufferSize(0), 0);
        if (code_size <= buffer.size()) {
            std::memcpy(buffer.data(), stub_code.data(), stub_code.size());
        }
        ctx.WriteBuffer(buffer, 0);
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(code_size);
}

void PushNasAuthorizationStateResponse(HLERequestContext& ctx, bool authorized) {
    if (ctx.CanWriteBuffer(0)) {
        std::vector<u8> state(ctx.GetWriteBufferSize(0), 0);
        if (state.size() >= sizeof(u32)) {
            const u32 value = authorized ? 1U : 0U;
            std::memcpy(state.data(), &value, sizeof(value));
        } else if (!state.empty()) {
            state[0] = authorized ? 1 : 0;
        }
        ctx.WriteBuffer(state, 0);
    }

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void WriteNetworkServiceLicenseCacheBuffer(HLERequestContext& ctx) {
    if (!ctx.CanWriteBuffer(0)) {
        return;
    }

    std::vector<u8> cache(ctx.GetWriteBufferSize(0), 0);
    if (cache.size() >= sizeof(u32)) {
        const u32 license_kind = NETWORK_SERVICE_LICENSE_KIND_BEDROCK;
        std::memcpy(cache.data(), &license_kind, sizeof(license_kind));
    }
    if (cache.size() >= sizeof(u32) + sizeof(s64)) {
        const s64 expiration = NETWORK_SERVICE_LICENSE_FAR_FUTURE_EXPIRATION;
        std::memcpy(cache.data() + sizeof(u32), &expiration, sizeof(expiration));
    }
    ctx.WriteBuffer(cache, 0);
}

void WriteNasUserResourceCacheBuffers(HLERequestContext& ctx, u64 nintendo_account_id) {
    std::vector<u8> nas_user_base;
    PopulateNasUserBaseForApplication(nas_user_base, nintendo_account_id);
    ctx.WriteBuffer(nas_user_base, 0);

    if (ctx.CanWriteBuffer(1)) {
        std::vector<u8> unknown_out_buffer(ctx.GetWriteBufferSize(1));
        ctx.WriteBuffer(unknown_out_buffer, 1);
    }
}

class CompletedIAsyncContextInterface final : public IAsyncContext {
public:
    explicit CompletedIAsyncContextInterface(Core::System& system_) : IAsyncContext{system_} {
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

void PushCompletedIAsyncContextResponse(Core::System& system, HLERequestContext& ctx) {
    auto async = std::make_shared<CompletedIAsyncContextInterface>(system);

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface(async);
}

} // Anonymous namespace

class IManagerForSystemService final : public ServiceFramework<IManagerForSystemService> {
public:
    explicit IManagerForSystemService(Core::System& system_, Common::UUID uuid)
        : ServiceFramework{system_, "IManagerForSystemService"}, account_id{uuid} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IManagerForSystemService::CheckAvailability>, "CheckAvailability"},
            {1, D<&IManagerForSystemService::GetAccountId>, "GetAccountId"},
            {2, &IManagerForSystemService::EnsureIdTokenCacheAsync, "EnsureIdTokenCacheAsync"},
            {3, &IManagerForSystemService::LoadIdTokenCacheDeprecated, "LoadIdTokenCacheDeprecated"},
            {4, &IManagerForSystemService::LoadIdTokenCache, "LoadIdTokenCache"},
            {100, nullptr, "SetSystemProgramIdentification"},
            {101, nullptr, "RefreshNotificationTokenAsync"}, // 7.0.0+
            {110, nullptr, "GetServiceEntryRequirementCache"}, // 4.0.0+
            {111, nullptr, "InvalidateServiceEntryRequirementCache"}, // 4.0.0+
            {112, nullptr, "InvalidateTokenCache"}, // 4.0.0 - 6.2.0
            {113, D<&IManagerForSystemService::GetServiceEntryRequirementCacheForOnlinePlay>,
             "GetServiceEntryRequirementCacheForOnlinePlay"}, // 6.1.0+
            {120, nullptr, "GetNintendoAccountId"},
            {121, nullptr, "CalculateNintendoAccountAuthenticationFingerprint"}, // 9.0.0+
            {130, &IManagerForSystemService::GetNintendoAccountUserResourceCache,
             "GetNintendoAccountUserResourceCache"},
            {131, &IManagerForSystemService::RefreshNintendoAccountUserResourceCacheAsync,
             "RefreshNintendoAccountUserResourceCacheAsync"},
            {132, &IManagerForSystemService::RefreshNintendoAccountUserResourceCacheAsyncIfSecondsElapsed,
             "RefreshNintendoAccountUserResourceCacheAsyncIfSecondsElapsed"},
            {133, nullptr, "GetNintendoAccountVerificationUrlCache"}, // 9.0.0+
            {134, nullptr, "RefreshNintendoAccountVerificationUrlCache"}, // 9.0.0+
            {135, nullptr, "RefreshNintendoAccountVerificationUrlCacheAsyncIfSecondsElapsed"}, // 9.0.0+
            {140, &IManagerForSystemService::GetNetworkServiceLicenseCache,
             "GetNetworkServiceLicenseCache"}, // 5.0.0+
            {141, &IManagerForSystemService::RefreshNetworkServiceLicenseCacheAsync,
             "RefreshNetworkServiceLicenseCacheAsync"}, // 5.0.0+
            {142, &IManagerForSystemService::RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed,
             "RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed"}, // 5.0.0+
            {150, nullptr, "CreateAuthorizationRequest"},
            {160, D<&IManagerForSystemService::RequiresUpdateNetworkServiceAccountIdTokenCache>,
             "RequiresUpdateNetworkServiceAccountIdTokenCache"},
            {161, nullptr, "RequireReauthenticationOfNetworkServiceAccount"},
            {143, D<&IManagerForSystemService::GetNetworkServiceLicenseCacheEx>, "GetNetworkServiceLicenseCacheEx"}, // 15.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result CheckAvailability(Out<bool> out_available) {
        LOG_DEBUG(Service_ACC, "called");
        *out_available = true;
        R_SUCCEED();
    }

    Result GetAccountId(Out<u64> out_account_id) {
        LOG_INFO(Service_ACC, "IManagerForSystemService::GetAccountId called");
        *out_account_id = STUB_NETWORK_SERVICE_ACCOUNT_ID;
        R_SUCCEED();
    }

    // Defined out-of-class below, after EnsureTokenIdCacheAsyncInterface is fully defined.
    void EnsureIdTokenCacheAsync(HLERequestContext& ctx);

    void LoadIdTokenCacheDeprecated(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called");
        PushLoadIdTokenCacheResponse(ctx, account_id);
    }

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called");
        PushLoadIdTokenCacheResponse(ctx, account_id);
    }

    Result GetNetworkServiceLicenseCacheEx(Out<u32> out_license, Out<s64> out_expiration) {
        LOG_INFO(Service_ACC, "called");

        *out_license = NETWORK_SERVICE_LICENSE_KIND_BEDROCK;
        *out_expiration = NETWORK_SERVICE_LICENSE_FAR_FUTURE_EXPIRATION;

        R_SUCCEED();
    }

    Result GetServiceEntryRequirementCacheForOnlinePlay(Out<u32> out_requirement) {
        LOG_INFO(Service_ACC, "called");
        // Non-zero indicates the title may proceed with online/local play gating.
        *out_requirement = 1;
        R_SUCCEED();
    }

    void GetNetworkServiceLicenseCache(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IManagerForSystemService::GetNetworkServiceLicenseCache called");
        WriteNetworkServiceLicenseCacheBuffer(ctx);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetNintendoAccountUserResourceCache(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called");

        const u64 nintendo_account_id = GetStubNintendoAccountId(account_id);
        WriteNasUserResourceCacheBuffers(ctx, nintendo_account_id);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(nintendo_account_id);
    }

    void RefreshNintendoAccountUserResourceCacheAsync(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called");
        PushCompletedIAsyncContextResponse(system, ctx);
    }

    void RefreshNintendoAccountUserResourceCacheAsyncIfSecondsElapsed(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called");

        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto seconds = rp.Pop<u32>();

        auto async = std::make_shared<CompletedIAsyncContextInterface>(system);

        IPC::ResponseBuilder rb{ctx, 3, 0, 1};
        rb.Push(ResultSuccess);
        rb.Push<u8>(1);
        rb.PushIpcInterface(async);
    }

    void RefreshNetworkServiceLicenseCacheAsync(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called");
        PopulateBaasStubSessionCache(system,account_id);
        PushCompletedIAsyncContextResponse(system, ctx);
    }

    void RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called");

        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto seconds = rp.Pop<u32>();

        PopulateBaasStubSessionCache(system,account_id);

        auto async = std::make_shared<CompletedIAsyncContextInterface>(system);

        IPC::ResponseBuilder rb{ctx, 3, 0, 1};
        rb.Push(ResultSuccess);
        rb.Push<u8>(1);
        rb.PushIpcInterface(async);
    }

    Result RequiresUpdateNetworkServiceAccountIdTokenCache(Out<u8> out_requires_update) {
        LOG_INFO(Service_ACC, "called");
        *out_requires_update = 0;
        R_SUCCEED();
    }

    Common::UUID account_id;
};

// 3.0.0+
class IFloatingRegistrationRequest final : public ServiceFramework<IFloatingRegistrationRequest> {
public:
    explicit IFloatingRegistrationRequest(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IFloatingRegistrationRequest"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetSessionId"},
            {12, nullptr, "GetAccountId"},
            {13, nullptr, "GetLinkedNintendoAccountId"},
            {14, nullptr, "GetNickname"},
            {15, nullptr, "GetProfileImage"},
            {21, nullptr, "LoadIdTokenCache"},
            {100, nullptr, "RegisterUser"}, // [1.0.0-3.0.2] RegisterAsync
            {101, nullptr, "RegisterUserWithUid"}, // [1.0.0-3.0.2] RegisterWithUidAsync
            {102, nullptr, "RegisterNetworkServiceAccountAsync"}, // 4.0.0+
            {103, nullptr, "RegisterNetworkServiceAccountWithUidAsync"}, // 4.0.0+
            {110, nullptr, "SetSystemProgramIdentification"},
            {111, nullptr, "EnsureIdTokenCacheAsync"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IAdministrator final : public ServiceFramework<IAdministrator> {
public:
    explicit IAdministrator(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAdministrator"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "CheckAvailability"},
            {1, nullptr, "GetAccountId"},
            {2, nullptr, "EnsureIdTokenCacheAsync"},
            {3, nullptr, "LoadIdTokenCache"},
            {100, nullptr, "SetSystemProgramIdentification"},
            {101, nullptr, "RefreshNotificationTokenAsync"}, // 7.0.0+
            {110, nullptr, "GetServiceEntryRequirementCache"}, // 4.0.0+
            {111, nullptr, "InvalidateServiceEntryRequirementCache"}, // 4.0.0+
            {112, nullptr, "InvalidateTokenCache"}, // 4.0.0 - 6.2.0
            {113, nullptr, "GetServiceEntryRequirementCacheForOnlinePlay"}, // 6.1.0+
            {120, nullptr, "GetNintendoAccountId"},
            {121, nullptr, "CalculateNintendoAccountAuthenticationFingerprint"}, // 9.0.0+
            {130, nullptr, "GetNintendoAccountUserResourceCache"},
            {131, nullptr, "RefreshNintendoAccountUserResourceCacheAsync"},
            {132, nullptr, "RefreshNintendoAccountUserResourceCacheAsyncIfSecondsElapsed"},
            {133, nullptr, "GetNintendoAccountVerificationUrlCache"}, // 9.0.0+
            {134, nullptr, "RefreshNintendoAccountVerificationUrlCacheAsync"}, // 9.0.0+
            {135, nullptr, "RefreshNintendoAccountVerificationUrlCacheAsyncIfSecondsElapsed"}, // 9.0.0+
            {140, nullptr, "GetNetworkServiceLicenseCache"}, // 5.0.0+
            {141, nullptr, "RefreshNetworkServiceLicenseCacheAsync"}, // 5.0.0+
            {142, nullptr, "RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed"}, // 5.0.0+
            {143, nullptr, "GetNetworkServiceLicenseCacheEx"},
            {150, nullptr, "CreateAuthorizationRequest"},
            {160, nullptr, "RequiresUpdateNetworkServiceAccountIdTokenCache"},
            {161, nullptr, "RequireReauthenticationOfNetworkServiceAccount"},
            {200, nullptr, "IsRegistered"},
            {201, nullptr, "RegisterAsync"},
            {202, nullptr, "UnregisterAsync"},
            {203, nullptr, "DeleteRegistrationInfoLocally"},
            {220, nullptr, "SynchronizeProfileAsync"},
            {221, nullptr, "UploadProfileAsync"},
            {222, nullptr, "SynchronizaProfileAsyncIfSecondsElapsed"},
            {250, nullptr, "IsLinkedWithNintendoAccount"},
            {251, nullptr, "CreateProcedureToLinkWithNintendoAccount"},
            {252, nullptr, "ResumeProcedureToLinkWithNintendoAccount"},
            {255, nullptr, "CreateProcedureToUpdateLinkageStateOfNintendoAccount"},
            {256, nullptr, "ResumeProcedureToUpdateLinkageStateOfNintendoAccount"},
            {260, nullptr, "CreateProcedureToLinkNnidWithNintendoAccount"}, // 3.0.0+
            {261, nullptr, "ResumeProcedureToLinkNnidWithNintendoAccount"}, // 3.0.0+
            {280, nullptr, "ProxyProcedureToAcquireApplicationAuthorizationForNintendoAccount"},
            {290, nullptr, "GetRequestForNintendoAccountUserResourceView"}, // 8.0.0+
            {300, nullptr, "TryRecoverNintendoAccountUserStateAsync"}, // 6.0.0+
            {400, nullptr, "IsServiceEntryRequirementCacheRefreshRequiredForOnlinePlay"}, // 6.1.0+
            {401, nullptr, "RefreshServiceEntryRequirementCacheForOnlinePlayAsync"}, // 6.1.0+
            {900, nullptr, "GetAuthenticationInfoForWin"}, // 9.0.0+
            {901, nullptr, "ImportAsyncForWin"}, // 9.0.0+
            {997, nullptr, "DebugUnlinkNintendoAccountAsync"},
            {998, nullptr, "DebugSetAvailabilityErrorDetail"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IAuthorizationRequest final : public ServiceFramework<IAuthorizationRequest> {
public:
    explicit IAuthorizationRequest(Core::System& system_, Common::UUID user_id_)
        : ServiceFramework{system_, "IAuthorizationRequest"}, user_id{user_id_},
          session_id{Common::UUID::MakeRandom()} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IAuthorizationRequest::GetSessionId, "GetSessionId"},
            {10, &IAuthorizationRequest::InvokeWithoutInteractionAsync, "InvokeWithoutInteractionAsync"},
            {19, &IAuthorizationRequest::IsAuthorized, "IsAuthorized"},
            {20, &IAuthorizationRequest::GetAuthorizationCode, "GetAuthorizationCode"},
            {21, &IAuthorizationRequest::GetIdToken, "GetIdToken"},
            {22, &IAuthorizationRequest::GetState, "GetState"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetSessionId(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAuthorizationRequest::GetSessionId called");

        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw(session_id);
    }

    void InvokeWithoutInteractionAsync(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAuthorizationRequest::InvokeWithoutInteractionAsync called");
        authorized = true;
        PushCompletedIAsyncContextResponse(system, ctx);
    }

    void IsAuthorized(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAuthorizationRequest::IsAuthorized called, authorized={}",
                 authorized);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u8>(authorized ? 1 : 0);
    }

    void GetAuthorizationCode(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAuthorizationRequest::GetAuthorizationCode called");
        PushAuthorizationCodeResponse(ctx);
    }

    void GetIdToken(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAuthorizationRequest::GetIdToken called");
        PopulateBaasStubSessionCache(system, user_id);
        PushLoadIdTokenCacheResponse(ctx, user_id);
    }

    void GetState(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAuthorizationRequest::GetState called, authorized={}", authorized);
        PushNasAuthorizationStateResponse(ctx, authorized);
    }

    Common::UUID user_id;
    Common::UUID session_id;
    bool authorized{};
};

class IOAuthProcedure final : public ServiceFramework<IOAuthProcedure> {
public:
    explicit IOAuthProcedure(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedure"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "PrepareAsync"},
            {1, nullptr, "GetRequest"},
            {2, nullptr, "ApplyResponse"},
            {3, nullptr, "ApplyResponseAsync"},
            {10, nullptr, "Suspend"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

// 3.0.0+
class IOAuthProcedureForExternalNsa final : public ServiceFramework<IOAuthProcedureForExternalNsa> {
public:
    explicit IOAuthProcedureForExternalNsa(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForExternalNsa"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "PrepareAsync"},
            {1, nullptr, "GetRequest"},
            {2, nullptr, "ApplyResponse"},
            {3, nullptr, "ApplyResponseAsync"},
            {10, nullptr, "Suspend"},
            {100, nullptr, "GetAccountId"},
            {101, nullptr, "GetLinkedNintendoAccountId"},
            {102, nullptr, "GetNickname"},
            {103, nullptr, "GetProfileImage"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IOAuthProcedureForNintendoAccountLinkage final
    : public ServiceFramework<IOAuthProcedureForNintendoAccountLinkage> {
public:
    explicit IOAuthProcedureForNintendoAccountLinkage(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForNintendoAccountLinkage"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "PrepareAsync"},
            {1, nullptr, "GetRequest"},
            {2, nullptr, "ApplyResponse"},
            {3, nullptr, "ApplyResponseAsync"},
            {10, nullptr, "Suspend"},
            {100, nullptr, "GetRequestWithTheme"},
            {101, nullptr, "IsNetworkServiceAccountReplaced"},
            {199, nullptr, "GetUrlForIntroductionOfExtraMembership"}, // 2.0.0 - 5.1.0
            {200, nullptr, "ApplyAsyncWithAuthorizedToken"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class INotifier final : public ServiceFramework<INotifier> {
public:
    explicit INotifier(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "INotifier"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetSystemEvent"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IProfileCommon : public ServiceFramework<IProfileCommon> {
public:
    explicit IProfileCommon(Core::System& system_, const char* name, bool editor_commands,
                            Common::UUID user_id_, ProfileManager& profile_manager_)
        : ServiceFramework{system_, name}, profile_manager{profile_manager_}, user_id{user_id_} {
        static const FunctionInfo functions[] = {
            {0, &IProfileCommon::Get, "Get"},
            {1, &IProfileCommon::GetBase, "GetBase"},
            {10, &IProfileCommon::GetImageSize, "GetImageSize"},
            {11, &IProfileCommon::LoadImage, "LoadImage"},
            {20, &IProfileCommon::Unknown20, "Unknown20"},
            {21, &IProfileCommon::Unknown21, "Unknown21"},
            {30, &IProfileCommon::Unknown30, "Unknown30"},
        };

        RegisterHandlers(functions);

        if (editor_commands) {
            static const FunctionInfo editor_functions[] = {
                {100, &IProfileCommon::Store, "Store"},
                {101, &IProfileCommon::StoreWithImage, "StoreWithImage"},
                {110, &IProfileCommon::Unknown110, "Unknown110"},
            };

            RegisterHandlers(editor_functions);
        }
    }

protected:
    void Get(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());
        ProfileBase profile_base{};
        UserData data{};
        if (profile_manager.GetProfileBaseAndData(user_id, profile_base, data)) {
            ctx.WriteBuffer(data);
            IPC::ResponseBuilder rb{ctx, 16};
            rb.Push(ResultSuccess);
            rb.PushRaw(profile_base);
        } else {
            LOG_ERROR(Service_ACC, "Failed to get profile base and data for user=0x{}",
                      user_id.RawString());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown); // TODO(ogniK): Get actual error code
        }
    }

    void GetBase(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());
        ProfileBase profile_base{};
        if (profile_manager.GetProfileBase(user_id, profile_base)) {
            IPC::ResponseBuilder rb{ctx, 16};
            rb.Push(ResultSuccess);
            rb.PushRaw(profile_base);
        } else {
            LOG_ERROR(Service_ACC, "Failed to get profile base for user=0x{}", user_id.RawString());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown); // TODO(ogniK): Get actual error code
        }
    }

    void LoadImage(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);

        const Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Read,
                                       Common::FS::FileType::BinaryFile);
        if (!image.IsOpen()) {
            LOG_WARNING(Service_ACC,
                        "Failed to load user provided image! Falling back to built-in backup...");
            ctx.WriteBuffer(Core::Constants::ACCOUNT_BACKUP_JPEG);
            rb.Push(static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
            return;
        }

        std::vector<u8> buffer(image.GetSize());

        if (image.Read(buffer) != buffer.size()) {
            LOG_ERROR(Service_ACC, "Failed to read all the bytes in the user provided image.");
        }

        SanitizeJPEGImageSize(buffer);

        ctx.WriteBuffer(buffer);
        rb.Push(static_cast<u32>(buffer.size()));
    }

    void GetImageSize(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);

        const Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Read,
                                       Common::FS::FileType::BinaryFile);

        if (!image.IsOpen()) {
            LOG_WARNING(Service_ACC,
                        "Failed to load user provided image! Falling back to built-in backup...");
            rb.Push(static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
            return;
        }

        std::vector<u8> buffer(image.GetSize());

        if (image.Read(buffer) != buffer.size()) {
            LOG_ERROR(Service_ACC, "Failed to read all the bytes in the user provided image.");
        }

        SanitizeJPEGImageSize(buffer);
        rb.Push(static_cast<u32>(buffer.size()));
    }

    void Unknown20(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown21(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown30(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown110(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Store(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto base = rp.PopRaw<ProfileBase>();

        const auto user_data = ctx.ReadBuffer();

        LOG_DEBUG(Service_ACC, "called, username='{}', timestamp={:016X}, uuid=0x{}",
                  Common::StringFromFixedZeroTerminatedBuffer(
                      reinterpret_cast<const char*>(base.username.data()), base.username.size()),
                  base.timestamp, base.user_uuid.RawString());

        if (user_data.size() < sizeof(UserData)) {
            LOG_ERROR(Service_ACC, "UserData buffer too small!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultInvalidArrayLength);
            return;
        }

        UserData data;
        std::memcpy(&data, user_data.data(), sizeof(UserData));

        if (!profile_manager.SetProfileBaseAndData(user_id, base, data)) {
            LOG_ERROR(Service_ACC, "Failed to update user data and base!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultAccountUpdateFailed);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void StoreWithImage(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto base = rp.PopRaw<ProfileBase>();

        const auto image_data = ctx.ReadBufferA(0);
        const auto user_data = ctx.ReadBufferX(0);

        LOG_INFO(Service_ACC, "called, username='{}', timestamp={:016X}, uuid=0x{}",
                 Common::StringFromFixedZeroTerminatedBuffer(
                     reinterpret_cast<const char*>(base.username.data()), base.username.size()),
                 base.timestamp, base.user_uuid.RawString());

        if (user_data.size() < sizeof(UserData)) {
            LOG_ERROR(Service_ACC, "UserData buffer too small!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultInvalidArrayLength);
            return;
        }

        UserData data;
        std::memcpy(&data, user_data.data(), sizeof(UserData));

        Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Write,
                                 Common::FS::FileType::BinaryFile);

        if (!image.IsOpen() || !image.SetSize(image_data.size()) ||
            image.Write(image_data) != image_data.size() ||
            !profile_manager.SetProfileBaseAndData(user_id, base, data)) {
            LOG_ERROR(Service_ACC, "Failed to update profile data, base, and image!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultAccountUpdateFailed);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    ProfileManager& profile_manager;
    Common::UUID user_id{}; ///< The user id this profile refers to.
};

class IProfile final : public IProfileCommon {
public:
    explicit IProfile(Core::System& system_, Common::UUID user_id_,
                      ProfileManager& profile_manager_)
        : IProfileCommon{system_, "IProfile", false, user_id_, profile_manager_} {}
};

class IProfileEditor final : public IProfileCommon {
public:
    explicit IProfileEditor(Core::System& system_, Common::UUID user_id_,
                            ProfileManager& profile_manager_)
        : IProfileCommon{system_, "IProfileEditor", true, user_id_, profile_manager_} {}
};

class ISessionObject final : public ServiceFramework<ISessionObject> {
public:
    explicit ISessionObject(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "ISessionObject"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {999, nullptr, "Dummy"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IGuestLoginRequest final : public ServiceFramework<IGuestLoginRequest> {
public:
    explicit IGuestLoginRequest(Core::System& system_, Common::UUID user_id_,
                                ProfileManager& profile_manager_)
        : ServiceFramework{system_, "IGuestLoginRequest"}, user_id{user_id_},
          profile_manager{profile_manager_}, session_id{Common::UUID::MakeRandom()} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IGuestLoginRequest::GetSessionId, "GetSessionId"},
            {11, nullptr, "Unknown"}, // 1.0.0 - 2.3.0 (the name is blank on Switchbrew)
            {12, &IGuestLoginRequest::GetAccountId, "GetAccountId"},
            {13, &IGuestLoginRequest::GetLinkedNintendoAccountId, "GetLinkedNintendoAccountId"},
            {14, &IGuestLoginRequest::GetNickname, "GetNickname"},
            {15, &IGuestLoginRequest::GetProfileImage, "GetProfileImage"},
            {21, &IGuestLoginRequest::LoadIdTokenCache, "LoadIdTokenCache"}, // 3.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetSessionId(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IGuestLoginRequest::GetSessionId called");
        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw(session_id);
    }

    void GetAccountId(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IGuestLoginRequest::GetAccountId called");
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(STUB_NETWORK_SERVICE_ACCOUNT_ID);
    }

    void GetLinkedNintendoAccountId(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IGuestLoginRequest::GetLinkedNintendoAccountId called");
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(GetStubNintendoAccountId(user_id));
    }

    void GetNickname(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IGuestLoginRequest::GetNickname called");

        ProfileBase profile_base{};
        if (profile_manager.GetProfileBase(user_id, profile_base)) {
            ctx.WriteBuffer(profile_base.username);
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetProfileImage(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IGuestLoginRequest::GetProfileImage called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);

        EnsureDefaultAvatarExists(user_id);
        const Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Read,
                                       Common::FS::FileType::BinaryFile);
        if (!image.IsOpen()) {
            ctx.WriteBuffer(Core::Constants::ACCOUNT_BACKUP_JPEG);
            rb.Push(static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
            return;
        }

        std::vector<u8> buffer(static_cast<std::size_t>(image.GetSize()));
        if (image.Read(buffer) != buffer.size()) {
            buffer = std::vector<u8>(Core::Constants::ACCOUNT_BACKUP_JPEG.begin(),
                                     Core::Constants::ACCOUNT_BACKUP_JPEG.end());
        } else {
            SanitizeJPEGImageSize(buffer);
        }

        ctx.WriteBuffer(buffer);
        rb.Push(static_cast<u32>(buffer.size()));
    }

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IGuestLoginRequest::LoadIdTokenCache called");
        PopulateBaasStubSessionCache(system,user_id);
        PushLoadIdTokenCacheResponse(ctx, user_id);
    }

    Common::UUID user_id;
    ProfileManager& profile_manager;
    Common::UUID session_id;
};

class EnsureTokenIdCacheAsyncInterface final : public IAsyncContext {
public:
    explicit EnsureTokenIdCacheAsyncInterface(Core::System& system_, const Common::UUID& user_id_)
        : IAsyncContext{system_}, user_id{user_id_} {}
    ~EnsureTokenIdCacheAsyncInterface() = default;

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "EnsureTokenIdCacheAsyncInterface::LoadIdTokenCache called");
        PopulateBaasStubSessionCache(system, user_id);
        PushLoadIdTokenCacheResponse(ctx, user_id);
    }
    bool IsComplete() const override {
        return is_complete.load();
    }

    void Cancel() override {}

    Result GetResult() const override {
        return ResultSuccess;
    }

    Common::UUID user_id;
};

class AuthenticateApplicationAsyncInterface final : public IAsyncContext {
public:
    explicit AuthenticateApplicationAsyncInterface(Core::System& system_) : IAsyncContext{system_} {
        MarkComplete();
    }
    ~AuthenticateApplicationAsyncInterface() = default;

protected:
    bool IsComplete() const override {
        return true;
    }

    void Cancel() override {}

    Result GetResult() const override {
        return ResultSuccess;
    }
};

class CheckNetworkServiceAvailabilityAsyncInterface final : public IAsyncContext {
public:
    explicit CheckNetworkServiceAvailabilityAsyncInterface(Core::System& system_)
        : IAsyncContext{system_} {
        MarkComplete();
    }
    ~CheckNetworkServiceAvailabilityAsyncInterface() = default;

protected:
    bool IsComplete() const override {
        return true;
    }

    void Cancel() override {}

    Result GetResult() const override {
        return ResultSuccess;
    }
};

class EnsureSignedDeviceIdentifierCacheAsyncInterface final : public IAsyncContext {
public:
    explicit EnsureSignedDeviceIdentifierCacheAsyncInterface(Core::System& system_)
        : IAsyncContext{system_} {
        MarkComplete();
    }
    ~EnsureSignedDeviceIdentifierCacheAsyncInterface() = default;

protected:
    bool IsComplete() const override {
        return true;
    }

    void Cancel() override {}

    Result GetResult() const override {
        return ResultSuccess;
    }
};

class AuthenticateServiceAsyncInterface final : public IAsyncContext {
public:
    explicit AuthenticateServiceAsyncInterface(Core::System& system_) : IAsyncContext{system_} {
        MarkComplete();
    }
    ~AuthenticateServiceAsyncInterface() = default;

protected:
    bool IsComplete() const override {
        return true;
    }

    void Cancel() override {}

    Result GetResult() const override {
        return ResultSuccess;
    }
};

class SynchronizeNetworkServiceAccountsSnapshotAsyncInterface final : public IAsyncContext {
public:
    explicit SynchronizeNetworkServiceAccountsSnapshotAsyncInterface(Core::System& system_)
        : IAsyncContext{system_} {
        MarkComplete();
    }
    ~SynchronizeNetworkServiceAccountsSnapshotAsyncInterface() = default;

protected:
    bool IsComplete() const override {
        return true;
    }

    void Cancel() override {}

    Result GetResult() const override {
        return ResultSuccess;
    }
};

class IAsyncNetworkServiceLicenseKindContext final
    : public ServiceFramework<IAsyncNetworkServiceLicenseKindContext> {
public:
    explicit IAsyncNetworkServiceLicenseKindContext(Core::System& system_)
        : ServiceFramework{system_, "IAsyncNetworkServiceLicenseKindContext"},
          service_context{system_, "IAsyncNetworkServiceLicenseKindContext"} {
        static const FunctionInfo functions[] = {
            {0, &IAsyncNetworkServiceLicenseKindContext::GetSystemEvent, "GetSystemEvent"},
            {1, &IAsyncNetworkServiceLicenseKindContext::Cancel, "Cancel"},
            {2, &IAsyncNetworkServiceLicenseKindContext::HasDone, "HasDone"},
            {3, &IAsyncNetworkServiceLicenseKindContext::GetResult, "GetResult"},
            {100, &IAsyncNetworkServiceLicenseKindContext::GetNetworkServiceLicenseKind,
             "GetNetworkServiceLicenseKind"},
        };
        RegisterHandlers(functions);

        completion_event =
            service_context.CreateEvent("IAsyncNetworkServiceLicenseKindContext:CompletionEvent");
    }

    ~IAsyncNetworkServiceLicenseKindContext() override {
        service_context.CloseEvent(completion_event);
    }

    void SignalCompletion() {
        is_complete.store(true);
        completion_event->Signal();
    }

private:
    void GetSystemEvent(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAsyncNetworkServiceLicenseKindContext::GetSystemEvent called");

        if (is_complete.load()) {
            completion_event->Signal();
        }

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(completion_event->GetReadableEvent());
    }

    void Cancel(HLERequestContext& ctx) {
        is_complete.store(true);
        completion_event->Signal();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void HasDone(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAsyncNetworkServiceLicenseKindContext::HasDone called, done={}",
                 is_complete.load());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(is_complete.load());
    }

    void GetResult(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAsyncNetworkServiceLicenseKindContext::GetResult called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetNetworkServiceLicenseKind(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(NETWORK_SERVICE_LICENSE_KIND_BEDROCK);
    }

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* completion_event{};
    std::atomic<bool> is_complete{false};
};

class IAsyncContextForLoginForOnlinePlay final
    : public ServiceFramework<IAsyncContextForLoginForOnlinePlay> {
public:
    explicit IAsyncContextForLoginForOnlinePlay(Core::System& system_,
                                                const Common::UUID& user_id_)
        : ServiceFramework{system_, "IAsyncContextForLoginForOnlinePlay"},
          service_context{system_, "IAsyncContextForLoginForOnlinePlay"}, user_id{user_id_} {
        static const FunctionInfo functions[] = {
            {0, &IAsyncContextForLoginForOnlinePlay::GetSystemEvent, "GetSystemEvent"},
            {1, &IAsyncContextForLoginForOnlinePlay::Cancel, "Cancel"},
            {2, &IAsyncContextForLoginForOnlinePlay::HasDone, "HasDone"},
            {3, &IAsyncContextForLoginForOnlinePlay::GetResult, "GetResult"},
            {21, &IAsyncContextForLoginForOnlinePlay::LoadIdTokenCache, "LoadIdTokenCache"},
            {100, &IAsyncContextForLoginForOnlinePlay::GetNetworkServiceLicenseInfoForOnlinePlay,
             "GetNetworkServiceLicenseInfoForOnlinePlay"},
        };
        RegisterHandlers(functions);

        completion_event =
            service_context.CreateEvent("IAsyncContextForLoginForOnlinePlay:CompletionEvent");
    }

    ~IAsyncContextForLoginForOnlinePlay() override {
        service_context.CloseEvent(completion_event);
    }

    void SignalCompletion() {
        is_complete.store(true);
        completion_event->Signal();
    }

private:
    void GetSystemEvent(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAsyncContextForLoginForOnlinePlay::GetSystemEvent called");

        if (is_complete.load()) {
            completion_event->Signal();
        }

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(completion_event->GetReadableEvent());
    }

    void Cancel(HLERequestContext& ctx) {
        is_complete.store(true);
        completion_event->Signal();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void HasDone(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAsyncContextForLoginForOnlinePlay::HasDone called, done={}",
                 is_complete.load());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(is_complete.load());
    }

    void GetResult(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAsyncContextForLoginForOnlinePlay::GetResult called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IAsyncContextForLoginForOnlinePlay::LoadIdTokenCache called");
        PopulateBaasStubSessionCache(system,user_id);
        PushLoadIdTokenCacheResponse(ctx, user_id);
    }

    void GetNetworkServiceLicenseInfoForOnlinePlay(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC,
                 "IAsyncContextForLoginForOnlinePlay::GetNetworkServiceLicenseInfoForOnlinePlay "
                 "called");
        IPC::ResponseBuilder rb{ctx, 5};
        rb.Push(ResultSuccess);
        rb.Push<u32>(NETWORK_SERVICE_LICENSE_KIND_BEDROCK);
        rb.PushRaw<s64>(NETWORK_SERVICE_LICENSE_FAR_FUTURE_EXPIRATION);
    }

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* completion_event{};
    std::atomic<bool> is_complete{false};
    Common::UUID user_id;
};

// cmd 2 EnsureIdTokenCacheAsync returns IAsyncContext; cmd 170 returns
// IAsyncContextForLoginForOnlinePlay (Switchbrew).
void PushEnsureIdTokenCacheAsyncResponse(Core::System& system, HLERequestContext& ctx,
                                         const Common::UUID& user_id) {
    auto async = std::make_shared<EnsureTokenIdCacheAsyncInterface>(system, user_id);

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface(async);
    async->SignalCompletion();
}

// Out-of-class definition — needs IAsyncContextForLoginForOnlinePlay to be fully defined first.
void IManagerForSystemService::EnsureIdTokenCacheAsync(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "called");

    PopulateBaasStubSessionCache(system,account_id);
    EnsureDefaultAvatarExists(account_id);
    PushEnsureIdTokenCacheAsyncResponse(system, ctx, account_id);
}

class IManagerForApplication final : public ServiceFramework<IManagerForApplication> {
public:
    explicit IManagerForApplication(Core::System& system_,
                                    const std::shared_ptr<ProfileManager>& profile_manager_)
        : ServiceFramework{system_, "IManagerForApplication"}, profile_manager{profile_manager_} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IManagerForApplication::CheckAvailability, "CheckAvailability"},
            {1, &IManagerForApplication::GetAccountId, "GetAccountId"},
            {2, &IManagerForApplication::EnsureIdTokenCacheAsync, "EnsureIdTokenCacheAsync"},
            {3, &IManagerForApplication::LoadIdTokenCacheDeprecated, "LoadIdTokenCacheDeprecated"},
            {4, &IManagerForApplication::LoadIdTokenCache, "LoadIdTokenCache"},
            {130, &IManagerForApplication::GetNintendoAccountUserResourceCacheForApplication, "GetNintendoAccountUserResourceCacheForApplication"},
            {136, &IManagerForApplication::GetNintendoAccountUserResourceCacheForApplication, "GetNintendoAccountUserResourceCache"}, // 19.0.0+
            {140, &IManagerForApplication::GetNetworkServiceLicenseCache, "GetNetworkServiceLicenseCache"}, // 5.0.0+
            {141, &IManagerForApplication::RefreshNetworkServiceLicenseCacheAsync,
             "RefreshNetworkServiceLicenseCacheAsync"}, // 5.0.0+
            {142, &IManagerForApplication::RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed,
             "RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed"}, // 5.0.0+
            {143, D<&IManagerForApplication::GetNetworkServiceLicenseCacheEx>, "GetNetworkServiceLicenseCacheEx"}, // 15.0.0+
            {150, &IManagerForApplication::CreateAuthorizationRequest, "CreateAuthorizationRequest"},
            {160, &IManagerForApplication::StoreOpenContext, "StoreOpenContext"},
            {170, &IManagerForApplication::EnsureIdTokenCacheForOnlinePlayOrLicenseKindAsync,
             "EnsureIdTokenCacheForOnlinePlayOrLicenseKindAsync"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CheckAvailability(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u8>(1);
    }

    void GetAccountId(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(STUB_NETWORK_SERVICE_ACCOUNT_ID);
    }

    void EnsureIdTokenCacheAsync(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IManagerForApplication::EnsureIdTokenCacheAsync called");

        const auto user_id = profile_manager->GetLastOpenedUser();
        if (!user_id.IsInvalid()) {
            profile_manager->OpenUser(user_id);
            profile_manager->StoreOpenedUsers();
        }
        PopulateBaasStubSessionCache(system,user_id);
        EnsureDefaultAvatarExists(user_id);
        PushEnsureIdTokenCacheAsyncResponse(system, ctx, user_id);
    }

    void LoadIdTokenCacheDeprecated(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IManagerForApplication::LoadIdTokenCacheDeprecated called");
        const auto user_id = profile_manager->GetLastOpenedUser();
        PopulateBaasStubSessionCache(system,user_id);
        PushLoadIdTokenCacheResponse(ctx, user_id);
    }

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IManagerForApplication::LoadIdTokenCache called");
        const auto user_id = profile_manager->GetLastOpenedUser();
        PopulateBaasStubSessionCache(system,user_id);
        PushLoadIdTokenCacheResponse(ctx, user_id);
    }

    void GetNintendoAccountUserResourceCacheForApplication(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called");

        const auto user_id = profile_manager->GetLastOpenedUser();
        const u64 nintendo_account_id = GetStubNintendoAccountId(user_id);
        WriteNasUserResourceCacheBuffers(ctx, nintendo_account_id);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(nintendo_account_id);
    }

    void CreateAuthorizationRequest(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IManagerForApplication::CreateAuthorizationRequest called");

        const auto user_id = profile_manager->GetLastOpenedUser();
        PopulateBaasStubSessionCache(system, user_id);

        auto request = std::make_shared<IAuthorizationRequest>(system, user_id);

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface(request);
    }

    void StoreOpenContext(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        profile_manager->StoreOpenedUsers();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetNetworkServiceLicenseCache(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IManagerForApplication::GetNetworkServiceLicenseCache called");
        WriteNetworkServiceLicenseCacheBuffer(ctx);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void RefreshNetworkServiceLicenseCacheAsync(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IManagerForApplication::RefreshNetworkServiceLicenseCacheAsync called");

        const auto user_id = profile_manager->GetLastOpenedUser();
        PopulateBaasStubSessionCache(system,user_id);
        PushCompletedIAsyncContextResponse(system, ctx);
    }

    void RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC,
                 "IManagerForApplication::RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed "
                 "called");

        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto seconds = rp.Pop<u32>();

        const auto user_id = profile_manager->GetLastOpenedUser();
        PopulateBaasStubSessionCache(system,user_id);

        auto async = std::make_shared<CompletedIAsyncContextInterface>(system);

        IPC::ResponseBuilder rb{ctx, 3, 0, 1};
        rb.Push(ResultSuccess);
        rb.Push<u8>(1);
        rb.PushIpcInterface(async);
    }

    Result GetNetworkServiceLicenseCacheEx(Out<u32> out_license, Out<s64> out_expiration) {
        LOG_INFO(Service_ACC, "IManagerForApplication::GetNetworkServiceLicenseCacheEx called");

        *out_license = NETWORK_SERVICE_LICENSE_KIND_BEDROCK;
        *out_expiration = NETWORK_SERVICE_LICENSE_FAR_FUTURE_EXPIRATION;

        R_SUCCEED();
    }

    void LoadNetworkServiceLicenseKindAsync(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "LoadNetworkServiceLicenseKindAsync called");

        const auto user_id = profile_manager->GetLastOpenedUser();
        PopulateBaasStubSessionCache(system,user_id);

        auto async = std::make_shared<IAsyncNetworkServiceLicenseKindContext>(system);

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface(async);
        async->SignalCompletion();
    }

    void EnsureIdTokenCacheForOnlinePlayAsync(HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "IManagerForApplication::EnsureIdTokenCacheForOnlinePlayAsync (cmd 170) called");

        const auto user_id = profile_manager->GetLastOpenedUser();
        PopulateBaasStubSessionCache(system,user_id);
        EnsureDefaultAvatarExists(user_id);

        auto async = std::make_shared<IAsyncContextForLoginForOnlinePlay>(system, user_id);

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface(async);
        async->SignalCompletion();
    }

    void EnsureIdTokenCacheForOnlinePlayOrLicenseKindAsync(HLERequestContext& ctx) {
        if (HLE::ApiVersion::HOS_VERSION_MAJOR >= 19) {
            EnsureIdTokenCacheForOnlinePlayAsync(ctx);
            return;
        }

        LoadNetworkServiceLicenseKindAsync(ctx);
    }

    std::shared_ptr<ProfileManager> profile_manager;
};

// 8.0.0+
class IOAuthProcedureForUserRegistration final
    : public ServiceFramework<IOAuthProcedureForUserRegistration> {
public:
    explicit IOAuthProcedureForUserRegistration(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForUserRegistration"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "PrepareAsync"},
            {1, nullptr, "GetRequest"},
            {2, nullptr, "ApplyResponse"},
            {3, nullptr, "ApplyResponseAsync"},
            {10, nullptr, "Suspend"},
            {100, nullptr, "GetAccountId"},
            {101, nullptr, "GetLinkedNintendoAccountId"},
            {102, nullptr, "GetNickname"},
            {103, nullptr, "GetProfileImage"},
            {110, nullptr, "RegisterUserAsync"},
            {111, nullptr, "GetUid"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class DAUTH_O final : public ServiceFramework<DAUTH_O> {
public:
    explicit DAUTH_O(Core::System& system_, Common::UUID) : ServiceFramework{system_, "dauth:o"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "EnsureAuthenticationTokenCacheAsync"},
            {1, nullptr, "LoadAuthenticationTokenCache"},
            {2, nullptr, "InvalidateAuthenticationTokenCache"},
            {3, nullptr, "IsDeviceAuthenticationTokenCacheAvailable"},
            {10, nullptr, "EnsureEdgeTokenCacheAsync"},
            {11, nullptr, "LoadEdgeTokenCache"},
            {12, nullptr, "InvalidateEdgeTokenCache"},
            {13, nullptr, "IsEdgeTokenCacheAvailable"},
            {20, nullptr, "EnsureApplicationAuthenticationCacheAsync"},
            {21, nullptr, "LoadApplicationAuthenticationTokenCache"},
            {22, nullptr, "LoadApplicationNetworkServiceClientConfigCache"},
            {23, nullptr, "IsApplicationAuthenticationCacheAvailable"},
            {24, nullptr, "InvalidateApplicationAuthenticationCache"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

// 6.0.0+
class IAsyncResult final : public ServiceFramework<IAsyncResult> {
public:
    explicit IAsyncResult(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAsyncResult"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetResult"},
            {1, nullptr, "Cancel"},
            {2, nullptr, "IsAvailable"},
            {3, nullptr, "GetSystemEvent"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

void Module::Interface::GetUserCount(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(static_cast<u32>(profile_manager->GetUserCount()));
}

void Module::Interface::GetUserExistence(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(profile_manager->UserExists(user_id));
}

void Module::Interface::ListAllUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    ctx.WriteBuffer(profile_manager->GetAllUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ListOpenUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    ctx.WriteBuffer(profile_manager->GetOpenUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetLastOpenedUser(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw<Common::UUID>(profile_manager->GetLastOpenedUser());
}

void Module::Interface::GetProfile(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IProfile>(system, user_id, *profile_manager);
}

void Module::Interface::GetProfileDigest(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());

    // Return a dummy digest for now
    std::array<u8, 0x20> digest{};
    std::fill(digest.begin(), digest.end(), static_cast<u8>(0));

    ctx.WriteBuffer(digest);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::IsUserRegistrationRequestPermitted(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(profile_manager->CanSystemRegisterUser());
}

void Module::Interface::InitializeApplicationInfo(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase());
}

void Module::Interface::InitializeApplicationInfoRestricted(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(Partial implementation) called");

    // TODO(ogniK): We require checking if the user actually owns the title and what not. As of
    // currently, we assume the user owns the title. InitializeApplicationInfoBase SHOULD be called
    // first then we do extra checks if the game is a digital copy.

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase());
}

Result Module::Interface::InitializeApplicationInfoBase() {
    if (application_info) {
        LOG_ERROR(Service_ACC, "Application already initialized");
        return Account::ResultApplicationInfoAlreadyInitialized;
    }

    // TODO(ogniK): This should be changed to reflect the target process for when we have multiple
    // processes emulated. As we don't actually have pid support we should assume we're just using
    // our own process
    Glue::ApplicationLaunchProperty launch_property{};
    const auto result = system.GetARPManager().GetLaunchProperty(
        &launch_property, system.GetApplicationProcessProgramID());

    if (result != ResultSuccess) {
        LOG_ERROR(Service_ACC, "Failed to get launch property");
        return Account::ResultInvalidApplication;
    }

    application_info.launch_property = launch_property;
    application_info.application_type = ApplicationType::Digital;
    switch (launch_property.base_game_storage_id) {
    case FileSys::StorageId::GameCard:
        application_info.application_type = ApplicationType::GameCard;
        break;
    case FileSys::StorageId::Host:
    case FileSys::StorageId::NandUser:
    case FileSys::StorageId::SdCard:
    case FileSys::StorageId::None: // Citron specific, differs from hardware
        application_info.application_type = ApplicationType::Digital;
        break;
    default:
        LOG_ERROR(Service_ACC, "Invalid game storage ID! storage_id={}",
                  launch_property.base_game_storage_id);
        return Account::ResultInvalidApplication;
    }

    LOG_INFO(Service_ACC, "ApplicationInfo initialized for title_id={:016X}",
             application_info.launch_property.title_id);

    return ResultSuccess;
}

void Module::Interface::GetBaasAccountManagerForApplication(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IManagerForApplication>(system, profile_manager);
}

void Module::Interface::AuthenticateApplicationAsync(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<AuthenticateApplicationAsyncInterface>(system);
}

void Module::Interface::CheckNetworkServiceAvailabilityAsync(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<CheckNetworkServiceAvailabilityAsyncInterface>(system);
}

void Module::Interface::IsUserAccountSwitchLocked(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    FileSys::NACP nacp;
    const auto res = system.GetAppLoader().ReadControlData(nacp);

    bool is_locked = false;

    if (res != Loader::ResultStatus::Success) {
        const FileSys::PatchManager pm{system.GetApplicationProcessProgramID(),
                                       system.GetFileSystemController(),
                                       system.GetContentProvider()};
        const auto nacp_unique = pm.GetControlMetadata().first;

        if (nacp_unique != nullptr) {
            is_locked = nacp_unique->GetUserAccountSwitchLock();
        } else {
            LOG_ERROR(Service_ACC, "nacp_unique is null!");
        }
    } else {
        is_locked = nacp.GetUserAccountSwitchLock();
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_locked);
}

void Module::Interface::InitializeApplicationInfoV2(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase());
}

void Module::Interface::BeginUserRegistration(HLERequestContext& ctx) {
    const auto user_id = Common::UUID::MakeRandom();
    profile_manager->CreateNewUser(user_id, "citron");

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw(user_id);
}

void Module::Interface::CompleteUserRegistration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    profile_manager->WriteUserSaveFile();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetProfileEditor(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_DEBUG(Service_ACC, "called, user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IProfileEditor>(system, user_id, *profile_manager);
}

void Module::Interface::ListQualifiedUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    // All users should be qualified. We don't actually have parental control or anything to do with
    // nintendo online currently. We're just going to assume the user running the game has access to
    // the game regardless of parental control settings.
    ctx.WriteBuffer(profile_manager->GetAllUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ListOpenContextStoredUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    ctx.WriteBuffer(profile_manager->GetStoredOpenedUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::StoreSaveDataThumbnailApplication(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, uuid=0x{}", uuid.RawString());

    // TODO(ogniK): Check if application ID is zero on acc initialize. As we don't have a reliable
    // way of confirming things like the TID, we're going to assume a non zero value for the time
    // being.
    constexpr u64 tid{1};
    StoreSaveDataThumbnail(ctx, uuid, tid);
}

void Module::Interface::ClearSaveDataThumbnail(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::CreateGuestLoginRequest(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "CreateGuestLoginRequest called");

    const auto user_id = profile_manager->GetLastOpenedUser();
    if (!user_id.IsInvalid()) {
        profile_manager->OpenUser(user_id);
        profile_manager->StoreOpenedUsers();
    }
    PopulateBaasStubSessionCache(system,user_id);

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IGuestLoginRequest>(system, user_id, *profile_manager);
}

void Module::Interface::LoadOpenContext(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "LoadOpenContext called");

    for (const auto& uuid : profile_manager->GetStoredOpenedUsers()) {
        if (!uuid.IsInvalid()) {
            profile_manager->OpenUser(uuid);
        }
    }

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::DebugActivateOpenContextRetention(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    // Create a dummy UUID for the session object
    const Common::UUID dummy_uuid{};

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<ISessionObject>(system, dummy_uuid);
}

void Module::Interface::GetBaasAccountManagerForSystemService(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IManagerForSystemService>(system, uuid);
}

void Module::Interface::StoreSaveDataThumbnailSystem(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();
    const auto tid = rp.Pop<u64_le>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, uuid=0x{}, tid={:016X}", uuid.RawString(), tid);
    StoreSaveDataThumbnail(ctx, uuid, tid);
}

void Module::Interface::GetPinCodeLength(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0);
}

void Module::Interface::StoreSaveDataThumbnail(HLERequestContext& ctx, const Common::UUID& uuid,
                                               const u64 tid) {
    IPC::ResponseBuilder rb{ctx, 2};

    if (tid == 0) {
        LOG_ERROR(Service_ACC, "TitleID is not valid!");
        rb.Push(Account::ResultInvalidApplication);
        return;
    }

    if (uuid.IsInvalid()) {
        LOG_ERROR(Service_ACC, "User ID is not valid!");
        rb.Push(Account::ResultInvalidUserId);
        return;
    }
    const auto thumbnail_size = ctx.GetReadBufferSize();
    if (thumbnail_size != THUMBNAIL_SIZE) {
        LOG_ERROR(Service_ACC, "Buffer size is empty! size={:X} expecting {:X}", thumbnail_size,
                  THUMBNAIL_SIZE);
        rb.Push(Account::ResultInvalidArrayLength);
        return;
    }

    // TODO(ogniK): Construct save data thumbnail
    rb.Push(ResultSuccess);
}

void Module::Interface::TrySelectUserWithoutInteraction(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    // A u8 is passed into this function which we can safely ignore. It's to determine if we have
    // access to use the network or not by the looks of it
    IPC::ResponseBuilder rb{ctx, 6};
    if (profile_manager->GetUserCount() != 1) {
        rb.Push(ResultSuccess);
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    const auto user_list = profile_manager->GetAllUsers();
    if (std::ranges::all_of(user_list, [](const auto& user) { return user.IsInvalid(); })) {
        rb.Push(ResultUnknown); // TODO(ogniK): Find the correct error code
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    // Select the first user we have
    rb.Push(ResultSuccess);
    rb.PushRaw(profile_manager->GetUser(0)->uuid);
}

void Module::Interface::GetUserRegistrationNotifier(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INotifier>(system, dummy_uuid);
}

void Module::Interface::GetUserStateChangeNotifier(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INotifier>(system, dummy_uuid);
}

void Module::Interface::GetBaasUserAvailabilityChangeNotifier(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INotifier>(system, dummy_uuid);
}

void Module::Interface::GetProfileUpdateNotifier(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INotifier>(system, dummy_uuid);
}

void Module::Interface::GetProfileSyncNotifier(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INotifier>(system, dummy_uuid);
}

void Module::Interface::LoadSaveDataThumbnail(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();
    const auto tid = rp.Pop<u64_le>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, uuid=0x{}, tid={:016X}", uuid.RawString(), tid);

    // Return empty buffer for now
    std::vector<u8> thumbnail(THUMBNAIL_SIZE);
    ctx.WriteBuffer(thumbnail);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetSaveDataThumbnailExistence(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();
    const auto tid = rp.Pop<u64_le>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, uuid=0x{}, tid={:016X}", uuid.RawString(), tid);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(false); // Thumbnail does not exist
}

void Module::Interface::ListOpenUsersInApplication(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    ctx.WriteBuffer(profile_manager->GetOpenUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ActivateOpenContextRetention(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<ISessionObject>(system, dummy_uuid);
}

void Module::Interface::EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync(
    HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<EnsureSignedDeviceIdentifierCacheAsyncInterface>(system);
}

void Module::Interface::LoadSignedDeviceIdentifierCacheForNintendoAccount(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    // Return dummy data
    std::array<u8, 0x40> device_identifier{};
    ctx.WriteBuffer(device_identifier);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetUserLastOpenedApplication(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<u64>(0); // No application opened
}

void Module::Interface::ActivateOpenContextHolder(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::CancelUserRegistration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::DeleteUser(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    profile_manager->RemoveUser(user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::SetUserPosition(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto position = rp.Pop<u32>();
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, position={}, uuid=0x{}", position,
                uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::CompleteUserRegistrationForcibly(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    profile_manager->WriteUserSaveFile();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::CreateFloatingRegistrationRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IFloatingRegistrationRequest>(system, dummy_uuid);
}

void Module::Interface::CreateProcedureToRegisterUserWithNintendoAccount(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IOAuthProcedureForUserRegistration>(system, dummy_uuid);
}

void Module::Interface::ResumeProcedureToRegisterUserWithNintendoAccount(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IOAuthProcedureForUserRegistration>(system, dummy_uuid);
}

void Module::Interface::CreateProcedureToCreateUserWithNintendoAccount(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IOAuthProcedureForUserRegistration>(system, dummy_uuid);
}

void Module::Interface::ResumeProcedureToCreateUserWithNintendoAccount(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IOAuthProcedureForUserRegistration>(system, dummy_uuid);
}

void Module::Interface::ResumeProcedureToCreateUserWithNintendoAccountAfterApplyResponse(
    HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IOAuthProcedureForUserRegistration>(system, dummy_uuid);
}

void Module::Interface::AuthenticateServiceAsync(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<AuthenticateServiceAsyncInterface>(system);
}

void Module::Interface::GetBaasAccountAdministrator(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IAdministrator>(system, uuid);
}

void Module::Interface::SynchronizeNetworkServiceAccountsSnapshotAsync(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<SynchronizeNetworkServiceAccountsSnapshotAsyncInterface>(system);
}

void Module::Interface::ProxyProcedureForGuestLoginWithNintendoAccount(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IOAuthProcedureForExternalNsa>(system, dummy_uuid);
}

void Module::Interface::ProxyProcedureForFloatingRegistrationWithNintendoAccount(
    HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IOAuthProcedureForExternalNsa>(system, dummy_uuid);
}

void Module::Interface::ProxyProcedureForDeviceMigrationAuthenticatingOperatingUser(
    HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IOAuthProcedure>(system, dummy_uuid);
}

void Module::Interface::ProxyProcedureForDeviceMigrationDownload(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IOAuthProcedure>(system, dummy_uuid);
}

void Module::Interface::SuspendBackgroundDaemon(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    const Common::UUID dummy_uuid{};
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<ISessionObject>(system, dummy_uuid);
}

void Module::Interface::CreateDeviceMigrationUserExportRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::UploadNasCredential(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::CreateDeviceMigrationUserImportRequest(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::DeleteUserMigrationInfo(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::SetUserUnqualifiedForDebug(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::UnsetUserUnqualifiedForDebug(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ListUsersUnqualifiedForDebug(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    ctx.WriteBuffer(std::vector<Common::UUID>{});
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::RefreshFirmwareSettingsForDebug(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::DebugInvalidateTokenCacheForUser(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::DebugSetUserStateClose(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::DebugSetUserStateOpen(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

Module::Interface::Interface(std::shared_ptr<Module> module_,
                             std::shared_ptr<ProfileManager> profile_manager_,
                             Core::System& system_, const char* name)
    : ServiceFramework{system_, name}, module{std::move(module_)},
      profile_manager{std::move(profile_manager_)} {}

Module::Interface::~Interface() = default;

void LoopProcess(Core::System& system) {
    auto module = std::make_shared<Module>();
    auto profile_manager = std::make_shared<ProfileManager>();
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("acc:aa", std::make_shared<ACC_AA>(system));
    server_manager->RegisterNamedService("acc:e",
                                         std::make_shared<ACC_E>(module, profile_manager, system));
    server_manager->RegisterNamedService(
        "acc:e:u1", std::make_shared<ACC_E_U1>(module, profile_manager, system));
    server_manager->RegisterNamedService(
        "acc:e:u2", std::make_shared<ACC_E_U2>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:su",
                                         std::make_shared<ACC_SU>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:u0",
                                         std::make_shared<ACC_U0>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:u1",
                                         std::make_shared<ACC_U1>(module, profile_manager, system));
    server_manager->RegisterNamedService("dauth:0", std::make_shared<DAUTH_0>(system));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Account
