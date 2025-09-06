// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/vi/layer.h"

namespace Service::VI {

class LayerList {
public:
    constexpr LayerList() = default;

    Layer* CreateLayer(u64 owner_aruid, Display* display, s32 consumer_binder_id,
                       s32 producer_binder_id) {
        Layer* const layer = GetFreeLayer();
        if (!layer) {
            return nullptr;
        }

        layer->Initialize(++m_next_id, owner_aruid, display, consumer_binder_id,
                          producer_binder_id);
        return layer;
    }

    bool DestroyLayer(u64 layer_id) {
        Layer* const layer = GetLayerById(layer_id);
        if (!layer) {
            return false;
        }

        layer->Finalize();
        return true;
    }

    Layer* GetLayerById(u64 layer_id) {
        for (auto& layer : m_layers) {
            if (layer.IsInitialized() && layer.GetId() == layer_id) {
                return &layer;
            }
        }

        return nullptr;
    }

    template <typename F>
    void ForEachLayer(F&& cb) {
        for (auto& layer : m_layers) {
            if (layer.IsInitialized()) {
                cb(layer);
            }
        }
    }

private:
    Layer* GetFreeLayer() {
        for (auto& layer : m_layers) {
            if (!layer.IsInitialized()) {
                return &layer;
            }
        }

        return nullptr;
    }

private:
    std::array<Layer, 8> m_layers{};
    u64 m_next_id{};
};

} // namespace Service::VI
