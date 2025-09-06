// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <glad/glad.h>

#include "common/common_types.h"
#include "common/literals.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

using namespace Common::Literals;

struct StagingBufferMap {
    ~StagingBufferMap();

    std::span<u8> mapped_span;
    size_t offset = 0;
    OGLSync* sync;
    GLuint buffer;
    size_t index;
};

struct StagingBuffers {
    explicit StagingBuffers(GLenum storage_flags_, GLenum map_flags_);
    ~StagingBuffers();

    StagingBufferMap RequestMap(size_t requested_size, bool insert_fence, bool deferred = false);

    void FreeDeferredStagingBuffer(size_t index);

    size_t RequestBuffer(size_t requested_size);

    std::optional<size_t> FindBuffer(size_t requested_size);

    struct StagingBufferAlloc {
        OGLSync sync;
        OGLBuffer buffer;
        u8* map;
        size_t size;
        size_t sync_index;
        bool deferred;
    };
    std::vector<StagingBufferAlloc> allocs;
    GLenum storage_flags;
    GLenum map_flags;
    size_t current_sync_index = 0;
};

class StreamBuffer {
    static constexpr size_t STREAM_BUFFER_SIZE = 64_MiB;
    static constexpr size_t NUM_SYNCS = 16;
    static constexpr size_t REGION_SIZE = STREAM_BUFFER_SIZE / NUM_SYNCS;
    static constexpr size_t MAX_ALIGNMENT = 256;
    static_assert(STREAM_BUFFER_SIZE % MAX_ALIGNMENT == 0);
    static_assert(STREAM_BUFFER_SIZE % NUM_SYNCS == 0);
    static_assert(REGION_SIZE % MAX_ALIGNMENT == 0);

public:
    explicit StreamBuffer();

    [[nodiscard]] std::pair<std::span<u8>, size_t> Request(size_t size) noexcept;

    [[nodiscard]] GLuint Handle() const noexcept {
        return buffer.handle;
    }

private:
    [[nodiscard]] static size_t Region(size_t offset) noexcept {
        return offset / REGION_SIZE;
    }

    size_t iterator = 0;
    size_t used_iterator = 0;
    size_t free_iterator = 0;
    u8* mapped_pointer = nullptr;
    OGLBuffer buffer;
    std::array<OGLSync, NUM_SYNCS> fences;
};

class StagingBufferPool {
public:
    StagingBufferPool() = default;
    ~StagingBufferPool() = default;

    StagingBufferMap RequestUploadBuffer(size_t size);
    StagingBufferMap RequestDownloadBuffer(size_t size, bool deferred = false);
    void FreeDeferredStagingBuffer(StagingBufferMap& buffer);

private:
    StagingBuffers upload_buffers{GL_MAP_WRITE_BIT, GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT};
    StagingBuffers download_buffers{GL_MAP_READ_BIT | GL_CLIENT_STORAGE_BIT, GL_MAP_READ_BIT};
};

} // namespace OpenGL
