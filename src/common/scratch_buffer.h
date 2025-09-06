// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <iterator>

#include "common/make_unique_for_overwrite.h"

namespace Common {

/**
 * ScratchBuffer class
 * This class creates a default initialized heap allocated buffer for cases such as intermediate
 * buffers being copied into entirely, where value initializing members during allocation or resize
 * is redundant.
 */
template <typename T>
class ScratchBuffer {
public:
    using element_type = T;
    using value_type = T;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::contiguous_iterator_tag;

    ScratchBuffer() = default;

    explicit ScratchBuffer(size_type initial_capacity)
        : last_requested_size{initial_capacity}, buffer_capacity{initial_capacity},
          buffer{Common::make_unique_for_overwrite<T[]>(initial_capacity)} {}

    ~ScratchBuffer() = default;
    ScratchBuffer(const ScratchBuffer&) = delete;
    ScratchBuffer& operator=(const ScratchBuffer&) = delete;

    ScratchBuffer(ScratchBuffer&& other) noexcept {
        swap(other);
        other.last_requested_size = 0;
        other.buffer_capacity = 0;
        other.buffer.reset();
    }

    ScratchBuffer& operator=(ScratchBuffer&& other) noexcept {
        swap(other);
        other.last_requested_size = 0;
        other.buffer_capacity = 0;
        other.buffer.reset();
        return *this;
    }

    /// This will only grow the buffer's capacity if size is greater than the current capacity.
    /// The previously held data will remain intact.
    void resize(size_type size) {
        if (size > buffer_capacity) {
            auto new_buffer = Common::make_unique_for_overwrite<T[]>(size);
            std::move(buffer.get(), buffer.get() + buffer_capacity, new_buffer.get());
            buffer = std::move(new_buffer);
            buffer_capacity = size;
        }
        last_requested_size = size;
    }

    /// This will only grow the buffer's capacity if size is greater than the current capacity.
    /// The previously held data will be destroyed if a reallocation occurs.
    void resize_destructive(size_type size) {
        if (size > buffer_capacity) {
            buffer_capacity = size;
            buffer = Common::make_unique_for_overwrite<T[]>(buffer_capacity);
        }
        last_requested_size = size;
    }

    [[nodiscard]] pointer data() noexcept {
        return buffer.get();
    }

    [[nodiscard]] const_pointer data() const noexcept {
        return buffer.get();
    }

    [[nodiscard]] iterator begin() noexcept {
        return data();
    }

    [[nodiscard]] const_iterator begin() const noexcept {
        return data();
    }

    [[nodiscard]] iterator end() noexcept {
        return data() + last_requested_size;
    }

    [[nodiscard]] const_iterator end() const noexcept {
        return data() + last_requested_size;
    }

    [[nodiscard]] reference operator[](size_type i) {
        return buffer[i];
    }

    [[nodiscard]] const_reference operator[](size_type i) const {
        return buffer[i];
    }

    [[nodiscard]] size_type size() const noexcept {
        return last_requested_size;
    }

    [[nodiscard]] size_type capacity() const noexcept {
        return buffer_capacity;
    }

    void swap(ScratchBuffer& other) noexcept {
        std::swap(last_requested_size, other.last_requested_size);
        std::swap(buffer_capacity, other.buffer_capacity);
        std::swap(buffer, other.buffer);
    }

private:
    size_type last_requested_size{};
    size_type buffer_capacity{};
    std::unique_ptr<T[]> buffer{};
};

} // namespace Common
