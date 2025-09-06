// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/nvnflinger/buffer_item_consumer.h"
#include "core/hle/service/nvnflinger/hwc_layer.h"

namespace Service::Nvnflinger {

struct Layer {
    explicit Layer(std::shared_ptr<android::BufferItemConsumer> buffer_item_consumer_,
                   s32 consumer_id_)
        : buffer_item_consumer(std::move(buffer_item_consumer_)), consumer_id(consumer_id_),
          blending(LayerBlending::None), visible(true) {}
    ~Layer() {
        buffer_item_consumer->Abandon();
    }

    std::shared_ptr<android::BufferItemConsumer> buffer_item_consumer;
    s32 consumer_id;
    LayerBlending blending;
    bool visible;
};

struct LayerStack {
    std::vector<std::shared_ptr<Layer>> layers;

    std::shared_ptr<Layer> FindLayer(s32 consumer_id) {
        for (auto& layer : layers) {
            if (layer->consumer_id == consumer_id) {
                return layer;
            }
        }

        return nullptr;
    }

    bool HasLayers() {
        return !layers.empty();
    }
};

struct Display {
    explicit Display(u64 id_) {
        id = id_;
    }

    u64 id;
    LayerStack stack;
};

} // namespace Service::Nvnflinger
