// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <span>

#include "common/common_types.h"

namespace AudioCore {

template <typename BufferType>
class AudioBufferList {
public:
    static constexpr size_t BufferCount = 32;

    AudioBufferList() = default;
    ~AudioBufferList() = default;

    void clear() {
        count = 0;
        head_index = 0;
        tail_index = 0;
    }

    void push_back(const BufferType& buffer) {
        if (count >= BufferCount) {
            return;
        }

        buffers[tail_index] = buffer;
        tail_index = (tail_index + 1) % BufferCount;
        count++;
    }

    void pop_front() {
        if (count == 0) {
            return;
        }

        head_index = (head_index + 1) % BufferCount;
        count--;
    }

    BufferType& front() {
        return buffers[head_index];
    }

    const BufferType& front() const {
        return buffers[head_index];
    }

    BufferType& back() {
        size_t index = (tail_index + BufferCount - 1) % BufferCount;
        return buffers[index];
    }

    const BufferType& back() const {
        size_t index = (tail_index + BufferCount - 1) % BufferCount;
        return buffers[index];
    }

    size_t size() const {
        return count;
    }

    bool empty() const {
        return count == 0;
    }

    bool full() const {
        return count >= BufferCount;
    }

    bool contains(const BufferType* buffer_ptr) const {
        for (size_t i = 0; i < count; i++) {
            size_t index = (head_index + i) % BufferCount;
            if (&buffers[index] == buffer_ptr) {
                return true;
            }
        }
        return false;
    }

private:
    std::array<BufferType, BufferCount> buffers{};
    size_t count{0};
    size_t head_index{0};
    size_t tail_index{0};
};

} // namespace AudioCore
