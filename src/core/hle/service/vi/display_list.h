// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstring>

#include "core/hle/service/vi/display.h"

namespace Service::VI {

class DisplayList {
public:
    constexpr DisplayList() = default;

    bool CreateDisplay(const DisplayName& name) {
        Display* const display = this->GetFreeDisplay();
        if (!display) {
            return false;
        }

        display->Initialize(m_next_id++, name);
        return true;
    }

    bool DestroyDisplay(u64 display_id) {
        Display* display = this->GetDisplayById(display_id);
        if (!display) {
            return false;
        }

        display->Finalize();
        return true;
    }

    Display* GetDisplayByName(const DisplayName& name) {
        for (auto& display : m_displays) {
            if (display.IsInitialized() &&
                std::strncmp(name.data(), display.GetDisplayName().data(), sizeof(DisplayName)) ==
                    0) {
                return &display;
            }
        }

        return nullptr;
    }

    Display* GetDisplayById(u64 display_id) {
        for (auto& display : m_displays) {
            if (display.IsInitialized() && display.GetId() == display_id) {
                return &display;
            }
        }

        return nullptr;
    }

    template <typename F>
    void ForEachDisplay(F&& cb) {
        for (auto& display : m_displays) {
            if (display.IsInitialized()) {
                cb(display);
            }
        }
    }

private:
    Display* GetFreeDisplay() {
        for (auto& display : m_displays) {
            if (!display.IsInitialized()) {
                return &display;
            }
        }

        return nullptr;
    }

private:
    std::array<Display, 8> m_displays{};
    u64 m_next_id{};
};

} // namespace Service::VI
