// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <limits>
#include <optional>
#include <span>

#include "common/common_types.h"
#include "common/scratch_buffer.h"

namespace Tegra::Engines::Upload {

// Packet boundaries need not coincide with the transfer described by EXEC.
class DataAccumulator {
public:
    void Begin(size_t size) {
        buffer.resize_destructive(size);
        written = 0;
    }

    size_t Size() const {
        return buffer.size();
    }

    // The returned view is consumed synchronously, before the caller's input or
    // this accumulator is reused. A complete single packet needs no extra copy.
    std::optional<std::span<const u8>> Append(std::span<const u8> bytes) {
        if (written == Size() || bytes.empty()) {
            return std::nullopt;
        }
        if (written == 0 && bytes.size() >= Size()) {
            written = Size();
            return bytes.first(Size());
        }
        const size_t count = std::min(bytes.size(), Size() - written);
        std::copy_n(bytes.data(), count, buffer.data() + written);
        written += count;
        if (written != Size()) {
            return std::nullopt;
        }
        return std::span<const u8>{buffer.data(), Size()};
    }

private:
    Common::ScratchBuffer<u8> buffer;
    size_t written{};
};

struct SourceRange {
    GPUVAddr upload_address;
    GPUVAddr exec_address;
    size_t copy_size;
    bool contiguous;

    bool Overlaps(GPUVAddr address, size_t size) const {
        if (copy_size == 0 || size == 0) {
            return false;
        }
        return exec_address <= address ? address - exec_address < copy_size
                                       : exec_address - address < size;
    }

    std::optional<GPUVAddr> IndirectAddress(GPUVAddr address, size_t size) const {
        if (!contiguous || exec_address != address || copy_size < size) {
            return std::nullopt;
        }
        return upload_address;
    }
};

// DispatchIndirect can reference the upload's guest source only when both its
// source and destination form a contiguous range for the completed transfer.
class SourceTracker {
public:
    void Begin(GPUVAddr destination, size_t size, bool contiguous_destination) {
        range = {0, destination, size, contiguous_destination};
        written = 0;
    }

    std::optional<SourceRange> Append(GPUVAddr source, size_t size) {
        if (written == range.copy_size || size == 0) {
            return std::nullopt;
        }
        if (written == 0) {
            range.upload_address = source;
        }
        const size_t count = std::min(size, range.copy_size - written);
        if (source == 0 || count - 1 > std::numeric_limits<GPUVAddr>::max() - source ||
            written > std::numeric_limits<GPUVAddr>::max() - range.upload_address ||
            source != range.upload_address + written) {
            range.contiguous = false;
        }
        written += count;
        if (written == range.copy_size) {
            return range;
        }
        return std::nullopt;
    }

private:
    SourceRange range{};
    size_t written{};
};

} // namespace Tegra::Engines::Upload
