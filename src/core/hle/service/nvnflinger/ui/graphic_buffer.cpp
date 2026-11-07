// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvnflinger/ui/graphic_buffer.h"

namespace Service::android {

static NvGraphicBuffer GetBuffer(std::shared_ptr<NvGraphicBuffer>& buffer) {
    if (buffer) {
        return *buffer;
    } else {
        return {};
    }
}

GraphicBuffer::GraphicBuffer(u32 width_, u32 height_, PixelFormat format_, u32 usage_)
    : NvGraphicBuffer(width_, height_, format_, usage_) {}

GraphicBuffer::GraphicBuffer(std::shared_ptr<Service::Nvidia::Module> owner,
                             std::shared_ptr<NvGraphicBuffer> buffer)
    : NvGraphicBuffer(GetBuffer(buffer)), m_owner{std::move(owner)} {}

std::shared_ptr<GraphicBuffer> GraphicBuffer::TryCreate(
    const std::shared_ptr<Service::Nvidia::Module>& owner, std::shared_ptr<NvGraphicBuffer> buffer,
    const Common::ProducerDrain::Entry& dispatch) {
    // Allocate the public wrapper before any nvmap effect. A failed allocation
    // must not strand an already duplicated/pinned resource.
    auto result = std::shared_ptr<GraphicBuffer>(new GraphicBuffer(owner, std::move(buffer)));
    if (result->BufferId() > 0) {
        result->m_retirement = owner->PrepareGraphicBufferRetirement(result->BufferId(), dispatch);
        if (!result->m_retirement) return {};
    } else {
        result->m_owner.reset(); // Empty descriptors borrow no nvmap/provider.
    }
    return result;
}

GraphicBuffer::~GraphicBuffer() {
    if (m_retirement) {
        auto queue = m_owner->RetirementQueue();
        m_owner.reset(); // Record now supplies the retained Module anchor.
        queue->Retire(std::move(m_retirement));
    }
}

} // namespace Service::android
