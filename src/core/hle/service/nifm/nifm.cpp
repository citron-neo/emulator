// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nifm/nifm.h"
#include "core/hle/service/server_manager.h"
#include "network/network.h"

namespace {

// Avoids name conflict with Windows' CreateEvent macro.
[[nodiscard]] Kernel::KEvent* CreateKEvent(Service::KernelHelpers::ServiceContext& service_context,
                                           std::string&& name) {
    return service_context.CreateEvent(std::move(name));
}

} // Anonymous namespace

#include "core/internal_network/network.h"
#include "core/internal_network/network_interface.h"

namespace Service::NIFM {

// This is nn::nifm::RequestState
// Reference: https://switchbrew.org/wiki/Network_Interface_services#RequestState
enum class RequestState : u32 {
    Invalid = 0,
    Free = 1, ///< NotSubmitted/Free state
    OnHold = 2,
    Accepted = 3,
    Blocking = 4,
};

// This is nn::nifm::NetworkInterfaceType
enum class NetworkInterfaceType : u32 {
    Invalid = 0,
    WiFi_Ieee80211 = 1,
    Ethernet = 2,
};

enum class InternetConnectionStatus : u8 {
    ConnectingUnknown1,
    ConnectingUnknown2,
    ConnectingUnknown3,
    ConnectingUnknown4,
    Connected,
};

// This is nn::nifm::NetworkProfileType
enum class NetworkProfileType : u32 {
    User,
    SsidList,
    Temporary,
};

// This is nn::nifm::ConnectionConfirmationOption
// Reference: https://switchbrew.org/wiki/Network_Interface_services#ConnectionConfirmationOption
enum class ConnectionConfirmationOption : u32 {
    Invalid = 0,
    Prohibited = 1,
    NotRequired = 2,
    Preferred = 3,
    Required = 4,
    Forced = 5,
};

// This is nn::nifm::IpAddressSetting
struct IpAddressSetting {
    bool is_automatic{};
    Network::IPv4Address ip_address{};
    Network::IPv4Address subnet_mask{};
    Network::IPv4Address default_gateway{};
};
static_assert(sizeof(IpAddressSetting) == 0xD, "IpAddressSetting has incorrect size.");

// This is nn::nifm::DnsSetting
struct DnsSetting {
    bool is_automatic{};
    Network::IPv4Address primary_dns{};
    Network::IPv4Address secondary_dns{};
};
static_assert(sizeof(DnsSetting) == 0x9, "DnsSetting has incorrect size.");

// This is nn::nifm::AuthenticationSetting
struct AuthenticationSetting {
    bool is_enabled{};
    std::array<char, 0x20> user{};
    std::array<char, 0x20> password{};
};
static_assert(sizeof(AuthenticationSetting) == 0x41, "AuthenticationSetting has incorrect size.");

// This is nn::nifm::ProxySetting
struct ProxySetting {
    bool is_enabled{};
    INSERT_PADDING_BYTES(1);
    u16 port{};
    std::array<char, 0x64> proxy_server{};
    AuthenticationSetting authentication{};
    INSERT_PADDING_BYTES(1);
};
static_assert(sizeof(ProxySetting) == 0xAA, "ProxySetting has incorrect size.");

// This is nn::nifm::IpSettingData
struct IpSettingData {
    IpAddressSetting ip_address_setting{};
    DnsSetting dns_setting{};
    ProxySetting proxy_setting{};
    u16 mtu{};
};
static_assert(sizeof(IpSettingData) == 0xC2, "IpSettingData has incorrect size.");

struct SfWirelessSettingData {
    u8 ssid_length{};
    std::array<char, 0x20> ssid{};
    u8 unknown_1{};
    u8 unknown_2{};
    u8 unknown_3{};
    std::array<char, 0x41> passphrase{};
};
static_assert(sizeof(SfWirelessSettingData) == 0x65, "SfWirelessSettingData has incorrect size.");

struct NifmWirelessSettingData {
    u8 ssid_length{};
    std::array<char, 0x21> ssid{};
    u8 unknown_1{};
    INSERT_PADDING_BYTES(1);
    u32 unknown_2{};
    u32 unknown_3{};
    std::array<char, 0x41> passphrase{};
    INSERT_PADDING_BYTES(3);
};
static_assert(sizeof(NifmWirelessSettingData) == 0x70,
              "NifmWirelessSettingData has incorrect size.");

#pragma pack(push, 1)
// This is nn::nifm::detail::sf::NetworkProfileData
struct SfNetworkProfileData {
    IpSettingData ip_setting_data{};
    u128 uuid{};
    std::array<char, 0x40> network_name{};
    u8 unknown_1{};
    u8 unknown_2{};
    u8 unknown_3{};
    u8 unknown_4{};
    SfWirelessSettingData wireless_setting_data{};
    INSERT_PADDING_BYTES(1);
};
static_assert(sizeof(SfNetworkProfileData) == 0x17C, "SfNetworkProfileData has incorrect size.");

// This is nn::nifm::NetworkProfileData
struct NifmNetworkProfileData {
    u128 uuid{};
    std::array<char, 0x40> network_name{};
    NetworkProfileType network_profile_type{};
    NetworkInterfaceType network_interface_type{};
    bool is_auto_connect{};
    bool is_large_capacity{};
    INSERT_PADDING_BYTES(2);
    NifmWirelessSettingData wireless_setting_data{};
    IpSettingData ip_setting_data{};
};
static_assert(sizeof(NifmNetworkProfileData) == 0x18E,
              "NifmNetworkProfileData has incorrect size.");
#pragma pack(pop)

constexpr Result ResultPendingConnection{ErrorModule::NIFM, 111};
constexpr Result ResultNetworkCommunicationDisabled{ErrorModule::NIFM, 1111};

class IScanRequest final : public ServiceFramework<IScanRequest> {
public:
    explicit IScanRequest(Core::System& system_) : ServiceFramework{system_, "IScanRequest"},
                                                  service_context{system_, "IScanRequest"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IScanRequest::Submit, "Submit"},
            {1, &IScanRequest::IsProcessing, "IsProcessing"},
            {2, &IScanRequest::GetResult, "GetResult"},
            {3, &IScanRequest::GetSystemEventReadableHandle, "GetSystemEventReadableHandle"},
            {4, &IScanRequest::SetChannels, "SetChannels"},
        };
        // clang-format on

        RegisterHandlers(functions);

        event = CreateKEvent(service_context, "IScanRequest:Event");
    }

    ~IScanRequest() override {
        service_context.CloseEvent(event);
    }

private:
    void Submit(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void IsProcessing(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u8>(0); // Not processing
    }

    void GetResult(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetSystemEventReadableHandle(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(event->GetReadableEvent());
    }

    void SetChannels(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* event;
};

class IRequest final : public ServiceFramework<IRequest> {
public:
    explicit IRequest(Core::System& system_)
        : ServiceFramework{system_, "IRequest"}, service_context{system_, "IRequest"} {
        static const FunctionInfo functions[] = {
            {0, &IRequest::GetRequestState, "GetRequestState"},
            {1, &IRequest::GetResult, "GetResult"},
            {2, &IRequest::GetSystemEventReadableHandles, "GetSystemEventReadableHandles"},
            {3, &IRequest::Cancel, "Cancel"},
            {4, &IRequest::Submit, "Submit"},
            {5, &IRequest::SetRequirement, "SetRequirement"},
            {6, &IRequest::SetRequirementPreset, "SetRequirementPreset"},
            {8, &IRequest::SetPriority, "SetPriority"},
            {9, &IRequest::SetNetworkProfileId, "SetNetworkProfileId"},
            {10, &IRequest::SetRejectable, "SetRejectable"},
            {11, &IRequest::SetConnectionConfirmationOption, "SetConnectionConfirmationOption"},
            {12, &IRequest::SetPersistent, "SetPersistent"},
            {13, &IRequest::SetInstant, "SetInstant"},
            {14, &IRequest::SetSustainable, "SetSustainable"},
            {15, &IRequest::SetRawPriority, "SetRawPriority"},
            {16, &IRequest::SetGreedy, "SetGreedy"},
            {17, &IRequest::SetSharable, "SetSharable"},
            {18, &IRequest::SetRequirementByRevision, "SetRequirementByRevision"},
            {19, &IRequest::GetRequirement, "GetRequirement"},
            {20, &IRequest::GetRevision, "GetRevision"},
            {21, &IRequest::GetAppletInfo, "GetAppletInfo"},
            {22, &IRequest::GetAdditionalInfo, "GetAdditionalInfo"},
            {23, &IRequest::SetKeptInSleep, "SetKeptInSleep"},
            {24, &IRequest::RegisterSocketDescriptor, "RegisterSocketDescriptor"},
            {25, &IRequest::UnregisterSocketDescriptor, "UnregisterSocketDescriptor"},
        };
        RegisterHandlers(functions);

        event1 = CreateKEvent(service_context, "IRequest:Event1");
        event2 = CreateKEvent(service_context, "IRequest:Event2");
        state = RequestState::Free;
    }

    ~IRequest() override {
        service_context.CloseEvent(event1);
        service_context.CloseEvent(event2);
    }

private:
    void Submit(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "(STUBBED) called");

        if (state == RequestState::Free) {
            UpdateState(RequestState::OnHold);
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetRequestState(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.PushEnum(state);
    }

    void SetRequirementPreset(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto param_1 = rp.Pop<u32>();

        LOG_WARNING(Service_NIFM, "(STUBBED) called, param_1={}", param_1);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetResult(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "(STUBBED) called");

        const auto result = [this] {
            const auto has_connection = Network::GetHostIPv4Address().has_value();
            switch (state) {
            case RequestState::Free:
                return has_connection ? ResultSuccess : ResultNetworkCommunicationDisabled;
            case RequestState::OnHold:
                if (has_connection) {
                    UpdateState(RequestState::Accepted);
                } else {
                    UpdateState(RequestState::Invalid);
                }
                return ResultPendingConnection;
            case RequestState::Accepted:
            default:
                return ResultSuccess;
            }
        }();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result);
    }

    void GetSystemEventReadableHandles(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 2};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(event1->GetReadableEvent(), event2->GetReadableEvent());
    }

    void Cancel(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetConnectionConfirmationOption(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto connection_option = rp.PopEnum<ConnectionConfirmationOption>();

        LOG_INFO(Service_NIFM, "called, connection_option={}", static_cast<u32>(connection_option));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetAppletInfo(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        std::vector<u8> out_buffer(ctx.GetWriteBufferSize());

        ctx.WriteBuffer(out_buffer);

        IPC::ResponseBuilder rb{ctx, 5};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
        rb.Push<u32>(0);
        rb.Push<u32>(0);
    }

    void UpdateState(RequestState new_state) {
        state = new_state;
        event1->Signal();
    }

    void SetRequirement(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetPriority(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetNetworkProfileId(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetRejectable(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetPersistent(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetInstant(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetSustainable(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetRawPriority(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetGreedy(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetSharable(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetRequirementByRevision(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetRequirement(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetRevision(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(1); // Arbitrary revision number
    }

    void GetAdditionalInfo(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0); // No additional info
    }

    void SetKeptInSleep(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void RegisterSocketDescriptor(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void UnregisterSocketDescriptor(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    KernelHelpers::ServiceContext service_context;

    RequestState state;

    Kernel::KEvent* event1;
    Kernel::KEvent* event2;
};

class INetworkProfile final : public ServiceFramework<INetworkProfile> {
public:
    explicit INetworkProfile(Core::System& system_) : ServiceFramework{system_, "INetworkProfile"} {
        static const FunctionInfo functions[] = {
            {0, nullptr, "Update"},
            {1, nullptr, "PersistOld"},
            {2, nullptr, "Persist"},
        };
        RegisterHandlers(functions);
    }
};

void IGeneralService::GetClientId(HLERequestContext& ctx) {
    static constexpr u32 client_id = 1;
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<u64>(client_id); // Client ID needs to be non zero otherwise it's considered invalid
}

void IGeneralService::CreateScanRequest(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NIFM, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IScanRequest>(system);
}

void IGeneralService::CreateRequest(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NIFM, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IRequest>(system);
}

void IGeneralService::GetCurrentNetworkProfile(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    const auto net_iface = Network::GetSelectedNetworkInterface();

    SfNetworkProfileData network_profile_data = [&net_iface] {
        if (!net_iface) {
            return SfNetworkProfileData{};
        }

        return SfNetworkProfileData{
            .ip_setting_data{
                .ip_address_setting{
                    .is_automatic{true},
                    .ip_address{Network::TranslateIPv4(net_iface->ip_address)},
                    .subnet_mask{Network::TranslateIPv4(net_iface->subnet_mask)},
                    .default_gateway{Network::TranslateIPv4(net_iface->gateway)},
                },
                .dns_setting{
                    .is_automatic{true},
                    .primary_dns{1, 1, 1, 1},
                    .secondary_dns{1, 0, 0, 1},
                },
                .proxy_setting{
                    .is_enabled{false},
                    .port{},
                    .proxy_server{},
                    .authentication{
                        .is_enabled{},
                        .user{},
                        .password{},
                    },
                },
                .mtu{1500},
            },
            .uuid{0xdeadbeef, 0xdeadbeef},
            .network_name{"citron Network"},
            .wireless_setting_data{
                .ssid_length{12},
                .ssid{"citron Network"},
                .passphrase{"citronpassword"},
            },
        };
    }();

    // When we're connected to a room, spoof the hosts IP address
    if (auto room_member = network.GetRoomMember().lock()) {
        if (room_member->IsConnected()) {
            network_profile_data.ip_setting_data.ip_address_setting.ip_address =
                room_member->GetFakeIpAddress();
        }
    }

    ctx.WriteBuffer(network_profile_data);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::RemoveNetworkProfile(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::GetCurrentIpAddress(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    auto ipv4 = Network::GetHostIPv4Address();
    if (!ipv4) {
        LOG_ERROR(Service_NIFM, "Couldn't get host IPv4 address, defaulting to 0.0.0.0");
        ipv4.emplace(Network::IPv4Address{0, 0, 0, 0});
    }

    // When we're connected to a room, spoof the hosts IP address
    if (auto room_member = network.GetRoomMember().lock()) {
        if (room_member->IsConnected()) {
            ipv4 = room_member->GetFakeIpAddress();
        }
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.PushRaw(*ipv4);
}

void IGeneralService::CreateTemporaryNetworkProfile(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NIFM, "called");

    ASSERT_MSG(ctx.GetReadBufferSize() == 0x17c, "SfNetworkProfileData is not the correct size");
    u128 uuid{};
    auto buffer = ctx.ReadBuffer();
    std::memcpy(&uuid, buffer.data() + 8, sizeof(u128));

    IPC::ResponseBuilder rb{ctx, 6, 0, 1};

    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INetworkProfile>(system);
    rb.PushRaw<u128>(uuid);
}

void IGeneralService::GetCurrentIpConfigInfo(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NIFM, "called");

    struct IpConfigInfo {
        IpAddressSetting ip_address_setting{};
        DnsSetting dns_setting{};
    };
    static_assert(sizeof(IpConfigInfo) == sizeof(IpAddressSetting) + sizeof(DnsSetting),
                  "IpConfigInfo has incorrect size.");

    const auto net_iface = Network::GetSelectedNetworkInterface();

    IpConfigInfo ip_config_info = [&net_iface] {
        if (!net_iface) {
            return IpConfigInfo{};
        }

        return IpConfigInfo{
            .ip_address_setting{
                .is_automatic{true},
                .ip_address{Network::TranslateIPv4(net_iface->ip_address)},
                .subnet_mask{Network::TranslateIPv4(net_iface->subnet_mask)},
                .default_gateway{Network::TranslateIPv4(net_iface->gateway)},
            },
            .dns_setting{
                .is_automatic{true},
                .primary_dns{1, 1, 1, 1},
                .secondary_dns{1, 0, 0, 1},
            },
        };
    }();

    // When we're connected to a room, spoof the hosts IP address
    if (auto room_member = network.GetRoomMember().lock()) {
        if (room_member->IsConnected()) {
            ip_config_info.ip_address_setting.ip_address = room_member->GetFakeIpAddress();
        }
    }

    IPC::ResponseBuilder rb{ctx, 2 + (sizeof(IpConfigInfo) + 3) / sizeof(u32)};
    rb.Push(ResultSuccess);
    rb.PushRaw<IpConfigInfo>(ip_config_info);
}

void IGeneralService::IsWirelessCommunicationEnabled(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(1);
}

void IGeneralService::GetInternetConnectionStatus(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    struct Output {
        u8 type{static_cast<u8>(NetworkInterfaceType::WiFi_Ieee80211)};
        u8 wifi_strength{3};
        InternetConnectionStatus state{InternetConnectionStatus::Connected};
    };
    static_assert(sizeof(Output) == 0x3, "Output has incorrect size.");

    constexpr Output out{};

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.PushRaw(out);
}

void IGeneralService::IsEthernetCommunicationEnabled(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    if (Network::GetHostIPv4Address().has_value()) {
        rb.Push<u8>(1);
    } else {
        rb.Push<u8>(0);
    }
}

void IGeneralService::IsAnyInternetRequestAccepted(HLERequestContext& ctx) {
    LOG_ERROR(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    if (Network::GetHostIPv4Address().has_value()) {
        rb.Push<u8>(1);
    } else {
        rb.Push<u8>(0);
    }
}

void IGeneralService::IsAnyForegroundRequestAccepted(HLERequestContext& ctx) {
    const bool is_accepted{};

    LOG_WARNING(Service_NIFM, "(STUBBED) called, is_accepted={}", is_accepted);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(is_accepted);
}

void IGeneralService::GetNetworkProfile(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::SetNetworkProfile(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::GetScanData(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::GetCurrentAccessPoint(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::Shutdown(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::GetAllowedChannels(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // No channels explicitly allowed (bitmap of allowed channels)
}

void IGeneralService::SetAcceptableNetworkTypeFlag(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::GetAcceptableNetworkTypeFlag(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(3); // Accept both WiFi and Ethernet
}

void IGeneralService::NotifyConnectionStateChanged(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::SetWowlDelayedWakeTime(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::SetWowlTcpKeepAliveTimeout(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::IsWiredConnectionAvailable(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called IsWiredConnectionAvailable [18.0.0+]");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(1); // Wired connection available
}

void IGeneralService::IsNetworkEmulationFeatureEnabled(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called IsNetworkEmulationFeatureEnabled [18.0.0+]");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(0); // Network emulation disabled
}

void IGeneralService::SelectActiveNetworkEmulationProfileIdForDebug(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called SelectActiveNetworkEmulationProfileIdForDebug [18.0.0+]");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::GetScanData2(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called GetScanData [19.0.0+]");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // No scan data
}

void IGeneralService::ResetActiveNetworkEmulationProfileId(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called ResetActiveNetworkEmulationProfileId [20.0.0+]");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::GetActiveNetworkEmulationProfileId(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called GetActiveNetworkEmulationProfileId [18.0.0+]");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // No active profile
}

void IGeneralService::IsRewriteFeatureEnabled(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called IsRewriteFeatureEnabled [18.0.0+]");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(0); // Rewrite feature disabled
}

void IGeneralService::CreateRewriteRule(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called CreateRewriteRule [18.0.0+]");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::DestroyRewriteRule(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called DestroyRewriteRule [18.0.0+]");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::IsActiveNetworkEmulationProfileIdSelected(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called IsActiveNetworkEmulationProfileIdSelected [20.0.0+]");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(0); // No profile selected
}

void IGeneralService::SelectDefaultNetworkEmulationProfileId(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called SelectDefaultNetworkEmulationProfileId [20.0.0+]");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IGeneralService::GetDefaultNetworkEmulationProfileId(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called GetDefaultNetworkEmulationProfileId [20.0.0+]");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Default profile ID
}

void IGeneralService::GetNetworkEmulationProfile(HLERequestContext& ctx) {
    LOG_WARNING(Service_NIFM, "(STUBBED) called GetNetworkEmulationProfile [20.0.0+]");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

IGeneralService::IGeneralService(Core::System& system_)
    : ServiceFramework{system_, "IGeneralService"}, network{system_.GetRoomNetwork()} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, &IGeneralService::GetClientId, "GetClientId"},
        {2, &IGeneralService::CreateScanRequest, "CreateScanRequest"},
        {4, &IGeneralService::CreateRequest, "CreateRequest"},
        {5, &IGeneralService::GetCurrentNetworkProfile, "GetCurrentNetworkProfile"},
        {6, nullptr, "EnumerateNetworkInterfaces"},
        {7, nullptr, "EnumerateNetworkProfiles"},
        {8, &IGeneralService::GetNetworkProfile, "GetNetworkProfile"},
        {9, &IGeneralService::SetNetworkProfile, "SetNetworkProfile"},
        {10, &IGeneralService::RemoveNetworkProfile, "RemoveNetworkProfile"},
        {11, nullptr, "GetScanDataOld"},
        {12, &IGeneralService::GetCurrentIpAddress, "GetCurrentIpAddress"},
        {13, nullptr, "GetCurrentAccessPointOld"},
        {14, &IGeneralService::CreateTemporaryNetworkProfile, "CreateTemporaryNetworkProfile"},
        {15, &IGeneralService::GetCurrentIpConfigInfo, "GetCurrentIpConfigInfo"},
        {16, nullptr, "SetWirelessCommunicationEnabled"},
        {17, &IGeneralService::IsWirelessCommunicationEnabled, "IsWirelessCommunicationEnabled"},
        {18, &IGeneralService::GetInternetConnectionStatus, "GetInternetConnectionStatus"},
        {19, nullptr, "SetEthernetCommunicationEnabled"},
        {20, &IGeneralService::IsEthernetCommunicationEnabled, "IsEthernetCommunicationEnabled"},
        {21, &IGeneralService::IsAnyInternetRequestAccepted, "IsAnyInternetRequestAccepted"},
        {22, &IGeneralService::IsAnyForegroundRequestAccepted, "IsAnyForegroundRequestAccepted"},
        {23, nullptr, "PutToSleep"},
        {24, nullptr, "WakeUp"},
        {25, nullptr, "GetSsidListVersion"},
        {26, nullptr, "SetExclusiveClient"},
        {27, nullptr, "GetDefaultIpSetting"},
        {28, nullptr, "SetDefaultIpSetting"},
        {29, nullptr, "SetWirelessCommunicationEnabledForTest"},
        {30, nullptr, "SetEthernetCommunicationEnabledForTest"},
        {31, nullptr, "GetTelemetorySystemEventReadableHandle"},
        {32, nullptr, "GetTelemetryInfo"},
        {33, nullptr, "ConfirmSystemAvailability"},
        {34, nullptr, "SetBackgroundRequestEnabled"},
        {35, &IGeneralService::GetScanData, "GetScanData"},
        {36, &IGeneralService::GetCurrentAccessPoint, "GetCurrentAccessPoint"},
        {37, &IGeneralService::Shutdown, "Shutdown"},
        {38, &IGeneralService::GetAllowedChannels, "GetAllowedChannels"},
        {39, nullptr, "NotifyApplicationSuspended"},
        {40, &IGeneralService::SetAcceptableNetworkTypeFlag, "SetAcceptableNetworkTypeFlag"},
        {41, &IGeneralService::GetAcceptableNetworkTypeFlag, "GetAcceptableNetworkTypeFlag"},
        {42, &IGeneralService::NotifyConnectionStateChanged, "NotifyConnectionStateChanged"},
        {43, &IGeneralService::SetWowlDelayedWakeTime, "SetWowlDelayedWakeTime"},
        {44, &IGeneralService::IsWiredConnectionAvailable, "IsWiredConnectionAvailable"},
        {45, &IGeneralService::IsNetworkEmulationFeatureEnabled, "IsNetworkEmulationFeatureEnabled"},
        {46, &IGeneralService::SelectActiveNetworkEmulationProfileIdForDebug, "SelectActiveNetworkEmulationProfileIdForDebug"},
        {47, &IGeneralService::GetScanData2, "GetScanData"},
        {48, &IGeneralService::ResetActiveNetworkEmulationProfileId, "ResetActiveNetworkEmulationProfileId"},
        {49, &IGeneralService::GetActiveNetworkEmulationProfileId, "GetActiveNetworkEmulationProfileId"},
        {50, &IGeneralService::IsRewriteFeatureEnabled, "IsRewriteFeatureEnabled"},
        {51, &IGeneralService::CreateRewriteRule, "CreateRewriteRule"},
        {52, &IGeneralService::DestroyRewriteRule, "DestroyRewriteRule"},
        {53, &IGeneralService::IsActiveNetworkEmulationProfileIdSelected, "IsActiveNetworkEmulationProfileIdSelected"},
        {54, &IGeneralService::SelectDefaultNetworkEmulationProfileId, "SelectDefaultNetworkEmulationProfileId"},
        {55, &IGeneralService::GetDefaultNetworkEmulationProfileId, "GetDefaultNetworkEmulationProfileId"},
        {56, &IGeneralService::GetNetworkEmulationProfile, "GetNetworkEmulationProfile"},
        {57, &IGeneralService::SetWowlTcpKeepAliveTimeout, "SetWowlTcpKeepAliveTimeout"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IGeneralService::~IGeneralService() = default;

class NetworkInterface final : public ServiceFramework<NetworkInterface> {
public:
    explicit NetworkInterface(const char* name, Core::System& system_)
        : ServiceFramework{system_, name} {
        static const FunctionInfo functions[] = {
            {4, &NetworkInterface::CreateGeneralServiceOld, "CreateGeneralServiceOld"},
            {5, &NetworkInterface::CreateGeneralService, "CreateGeneralService"},
        };
        RegisterHandlers(functions);
    }

private:
    void CreateGeneralServiceOld(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IGeneralService>(system);
    }

    void CreateGeneralService(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IGeneralService>(system);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("nifm:a",
                                         std::make_shared<NetworkInterface>("nifm:a", system));
    server_manager->RegisterNamedService("nifm:s",
                                         std::make_shared<NetworkInterface>("nifm:s", system));
    server_manager->RegisterNamedService("nifm:u",
                                         std::make_shared<NetworkInterface>("nifm:u", system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NIFM
