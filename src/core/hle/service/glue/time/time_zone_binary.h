// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string>
#include <string_view>

#include "core/hle/service/psc/time/common.h"

namespace Core {
class System;
}

namespace Service::Glue::Time {

void ResetTimeZoneBinary();
Result MountTimeZoneBinary(Core::System& system);
void GetTimeZoneBinaryListPath(std::string& out_path);
void GetTimeZoneBinaryVersionPath(std::string& out_path);
void GetTimeZoneZonePath(std::string& out_path, const Service::PSC::Time::LocationName& name);
bool IsTimeZoneBinaryValid(const Service::PSC::Time::LocationName& name);
u32 GetTimeZoneCount();
Result GetTimeZoneVersion(Service::PSC::Time::RuleVersion& out_rule_version);
Result GetTimeZoneRule(std::span<const u8>& out_rule, size_t& out_rule_size,
                       const Service::PSC::Time::LocationName& name);
Result GetTimeZoneLocationList(u32& out_count,
                               std::span<Service::PSC::Time::LocationName> out_names,
                               size_t max_names, u32 index);

} // namespace Service::Glue::Time
