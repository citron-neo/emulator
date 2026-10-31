// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>
#include "common/common_types.h"

namespace Common {
// CPU source-copy / cache-selection boundary only. No GPU waits or escaped pointers.
class FrameMappingDomain {
    struct State {
        std::mutex mutex;
        u64 generation{};
        size_t pending{};
        bool failed{}, closed{};
    };
public:
    struct Stamp {
        std::shared_ptr<State> state;
        u64 generation{}, address{}, size{};
    };
    class Mutation {
    public:
        explicit Mutation(std::shared_ptr<State> state_) : state{std::move(state_)} {
            std::scoped_lock lock{state->mutex};
            ++state->generation;
            ++state->pending;
        }
        Mutation(const Mutation&) = delete;
        Mutation& operator=(const Mutation&) = delete;
        ~Mutation() {
            std::scoped_lock lock{state->mutex};
            state->failed |= !completed;
            --state->pending;
        }
        void Complete() { completed = true; }
    private:
        std::shared_ptr<State> state;
        bool completed{};
    };
    FrameMappingDomain() = default;
    FrameMappingDomain(const FrameMappingDomain&) = delete;
    FrameMappingDomain& operator=(const FrameMappingDomain&) = delete;
    ~FrameMappingDomain() { Close(); }
    Mutation BeginMutation() { return Mutation{state}; }
    void Close() {
        std::scoped_lock lock{state->mutex};
        state->closed = true;
    }
    std::optional<Stamp> Capture(u64 address, u64 size) const {
        std::unique_lock lock{state->mutex, std::try_to_lock};
        if (!lock.owns_lock() || state->closed || state->failed || state->pending || !size)
            return {};
        return Stamp{state, state->generation, address, size};
    }
    template <typename F>
    bool WithUse(const Stamp& stamp, u64 address, u64 size, F&& operation) const {
        std::unique_lock lock{state->mutex, std::try_to_lock};
        if (!lock.owns_lock() || stamp.state != state || state->closed || state->failed ||
            state->pending || stamp.generation != state->generation || !size ||
            address < stamp.address || address - stamp.address > stamp.size ||
            size > stamp.size - (address - stamp.address)) return false;
        return operation();
    }
private:
    std::shared_ptr<State> state{std::make_shared<State>()};
};
} // namespace Common
