// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <vector>

#include "common/uuid.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Glue {

// This is nn::notification::AlarmSettingId
using AlarmSettingId = u16;
static_assert(sizeof(AlarmSettingId) == 0x2, "AlarmSettingId is an invalid size");

using ApplicationParameter = std::array<u8, 0x400>;
static_assert(sizeof(ApplicationParameter) == 0x400, "ApplicationParameter is an invalid size");

struct DailyAlarmSetting {
    s8 hour;
    s8 minute;
};
static_assert(sizeof(DailyAlarmSetting) == 0x2, "DailyAlarmSetting is an invalid size");

struct WeeklyScheduleAlarmSetting {
    INSERT_PADDING_BYTES_NOINIT(0xA);
    std::array<DailyAlarmSetting, 0x7> day_of_week;
};
static_assert(sizeof(WeeklyScheduleAlarmSetting) == 0x18,
              "WeeklyScheduleAlarmSetting is an invalid size");

// This is nn::notification::AlarmSetting
struct AlarmSetting {
    AlarmSettingId alarm_setting_id;
    u8 kind;
    u8 muted;
    INSERT_PADDING_BYTES_NOINIT(0x4);
    Common::UUID account_id;
    u64 application_id;
    INSERT_PADDING_BYTES_NOINIT(0x8);
    WeeklyScheduleAlarmSetting schedule;
};
static_assert(sizeof(AlarmSetting) == 0x40, "AlarmSetting is an invalid size");

enum class NotificationChannel : u8 {
    Unknown0 = 0,
};

struct NotificationPresentationSetting {
    INSERT_PADDING_BYTES_NOINIT(0x10);
};
static_assert(sizeof(NotificationPresentationSetting) == 0x10,
              "NotificationPresentationSetting is an invalid size");

class NotificationServiceImpl {
public:
    Result RegisterAlarmSetting(AlarmSettingId* out_alarm_setting_id,
                                const AlarmSetting& alarm_setting,
                                std::span<const u8> application_parameter);
    Result UpdateAlarmSetting(const AlarmSetting& alarm_setting,
                              std::span<const u8> application_parameter);
    Result ListAlarmSettings(s32* out_count, std::span<AlarmSetting> out_alarms);
    Result LoadApplicationParameter(u32* out_size, std::span<u8> out_application_parameter,
                                    AlarmSettingId alarm_setting_id);
    Result DeleteAlarmSetting(AlarmSettingId alarm_setting_id);
    Result Initialize(u64 aruid);

private:
    std::vector<AlarmSetting>::iterator GetAlarmFromId(AlarmSettingId alarm_setting_id);
    std::vector<AlarmSetting> alarms{};
    AlarmSettingId last_alarm_setting_id{};
};

class INotificationServicesForApplication final
    : public ServiceFramework<INotificationServicesForApplication> {
public:
    explicit INotificationServicesForApplication(Core::System& system_);
    ~INotificationServicesForApplication() override;

private:
    Result RegisterAlarmSetting(Out<AlarmSettingId> out_alarm_setting_id,
                                InLargeData<AlarmSetting, BufferAttr_HipcMapAlias> alarm_setting,
                                InBuffer<BufferAttr_HipcMapAlias> application_parameter);
    Result UpdateAlarmSetting(InLargeData<AlarmSetting, BufferAttr_HipcMapAlias> alarm_setting,
                              InBuffer<BufferAttr_HipcMapAlias> application_parameter);
    Result ListAlarmSettings(Out<s32> out_count,
                             OutArray<AlarmSetting, BufferAttr_HipcMapAlias> out_alarms);
    Result LoadApplicationParameter(Out<u32> out_size,
                                    OutBuffer<BufferAttr_HipcMapAlias> out_application_parameter,
                                    AlarmSettingId alarm_setting_id);
    Result DeleteAlarmSetting(AlarmSettingId alarm_setting_id);
    Result Initialize(ClientAppletResourceUserId aruid);

    NotificationServiceImpl impl;
};

class INotificationSystemEventAccessor;

class INotificationServices final : public ServiceFramework<INotificationServices> {
public:
    explicit INotificationServices(Core::System& system_);
    ~INotificationServices() override;

private:
    Result RegisterAlarmSetting(Out<AlarmSettingId> out_alarm_setting_id,
                                InLargeData<AlarmSetting, BufferAttr_HipcMapAlias> alarm_setting,
                                InBuffer<BufferAttr_HipcMapAlias> application_parameter);
    Result UpdateAlarmSetting(InLargeData<AlarmSetting, BufferAttr_HipcMapAlias> alarm_setting,
                              InBuffer<BufferAttr_HipcMapAlias> application_parameter);
    Result ListAlarmSettings(Out<s32> out_count,
                             OutArray<AlarmSetting, BufferAttr_HipcMapAlias> out_alarms);
    Result LoadApplicationParameter(Out<u32> out_size,
                                    OutBuffer<BufferAttr_HipcMapAlias> out_application_parameter,
                                    AlarmSettingId alarm_setting_id);
    Result DeleteAlarmSetting(AlarmSettingId alarm_setting_id);
    Result Initialize(ClientAppletResourceUserId aruid);
    Result OpenNotificationSystemEventAccessor(Out<SharedPointer<INotificationSystemEventAccessor>>
                                                   out_notification_system_event_accessor);
    Result GetNotificationPresentationSetting(
        Out<NotificationPresentationSetting> out_notification_presentation_setting,
        NotificationChannel notification_channel);

    NotificationServiceImpl impl;
};
} // namespace Service::Glue
