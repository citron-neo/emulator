// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"

namespace Service::VI {

class Display;

class Layer {
public:
    constexpr Layer() = default;

    void Initialize(u64 id, u64 owner_aruid, Display* display, s32 consumer_binder_id,
                    s32 producer_binder_id) {
        m_id = id;
        m_owner_aruid = owner_aruid;
        m_display = display;
        m_consumer_binder_id = consumer_binder_id;
        m_producer_binder_id = producer_binder_id;
        m_is_initialized = true;
    }

    void Finalize() {
        m_id = {};
        m_owner_aruid = {};
        m_display = {};
        m_consumer_binder_id = {};
        m_producer_binder_id = {};
        m_is_initialized = {};
    }

    void Open() {
        m_is_open = true;
    }

    void Close() {
        m_is_open = false;
    }

    u64 GetId() const {
        return m_id;
    }

    u64 GetOwnerAruid() const {
        return m_owner_aruid;
    }

    Display* GetDisplay() const {
        return m_display;
    }

    s32 GetConsumerBinderId() const {
        return m_consumer_binder_id;
    }

    s32 GetProducerBinderId() const {
        return m_producer_binder_id;
    }

    bool IsInitialized() const {
        return m_is_initialized;
    }

    bool IsOpen() const {
        return m_is_open;
    }

private:
    u64 m_id{};
    u64 m_owner_aruid{};
    Display* m_display{};
    s32 m_consumer_binder_id{};
    s32 m_producer_binder_id{};
    bool m_is_initialized{};
    bool m_is_open{};
};

} // namespace Service::VI
