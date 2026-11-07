// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <bitset>
#include <limits>
#include <optional>
#include <vector>

#include "common/common_types.h"

namespace Tegra::Engines {

enum class EngineTypes : u32 {
    KeplerCompute,
    Maxwell3D,
    Fermi2D,
    MaxwellDMA,
    KeplerMemory,
};

class EngineInterface {
public:
    virtual ~EngineInterface() = default;

    /// Write the value to the register identified by method.
    virtual void CallMethod(u32 method, u32 method_argument, bool is_last_call) = 0;

    /// Write multiple values to the register identified by method.
    virtual void CallMultiMethod(u32 method, const u32* base_start, u32 amount,
                                 u32 methods_pending) = 0;

    void ConsumeSink() {
        if (method_sink.empty()) {
            return;
        }
        ConsumeSinkImpl();
    }

    std::bitset<std::numeric_limits<u16>::max()> execution_mask{};
    std::vector<std::pair<u32, u32>> method_sink{};
    bool current_dirty{};
    GPUVAddr current_dma_segment;

protected:
    /// GPU-thread-only snapshot of inert pending register writes. Never consumes the sink or
    /// calls an engine method. Validate the complete queue before constructing the projection.
    template <std::size_t N>
    [[nodiscard]] std::optional<std::array<u32, N>> ProjectPendingRegisters(
        const std::array<u32, N>& committed) const {
        for (const auto& [method, value] : method_sink) {
            if (method >= N || method >= execution_mask.size() || execution_mask[method]) {
                return std::nullopt;
            }
        }
        auto projected = committed;
        for (const auto& [method, value] : method_sink) {
            projected[method] = value;
        }
        return projected;
    }

    virtual void ConsumeSinkImpl() {
        for (auto [method, value] : method_sink) {
            CallMethod(method, value, true);
        }
        method_sink.clear();
    }
};

} // namespace Tegra::Engines
