// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/vi/vi_types.h"

namespace Service::VI {

class Display {
public:
    constexpr Display() = default;

    void Initialize(u64 id, const DisplayName& display_name) {
        m_id = id;
        m_display_name = display_name;
        m_is_initialized = true;
    }

    void Finalize() {
        m_id = {};
        m_display_name = {};
        m_is_initialized = {};
    }

    u64 GetId() const {
        return m_id;
    }

    const DisplayName& GetDisplayName() const {
        return m_display_name;
    }

    bool IsInitialized() const {
        return m_is_initialized;
    }

private:
    u64 m_id{};
    DisplayName m_display_name{};
    bool m_is_initialized{};
};

} // namespace Service::VI
