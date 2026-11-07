// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <optional>
#include "common/terminal_ingress.h"

namespace VideoCore {
class RasterizerInterface;
}

namespace Tegra {
// Detached terminal binding lifetime. Retained GMMUs never call a retired GPU.
// Target publication is cold and one-shot; it does not grant native work admission.
class RasterizerBindingState {
    Common::TerminalIngress ingress;
    std::atomic<VideoCore::RasterizerInterface*> target{};

public:
    RasterizerBindingState() { ingress.Open(); }
    bool SetTarget(VideoCore::RasterizerInterface* value) noexcept {
        if (!value) return false;
        VideoCore::RasterizerInterface* expected{};
        return target.compare_exchange_strong(expected, value, std::memory_order_release);
    }
    VideoCore::RasterizerInterface* Target() const noexcept {
        return target.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::optional<Common::TerminalIngress::Entry>
    TryBind(VideoCore::RasterizerInterface* value) noexcept {
        auto entry = ingress.TryEnter();
        if (!entry || !value || value != Target()) return std::nullopt;
        return entry;
    }
    void Close() noexcept { ingress.Close(); }
    void Join() noexcept { ingress.Join(); }
};
} // namespace Tegra
