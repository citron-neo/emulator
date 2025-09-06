// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <span>
#include <unordered_map>

#include "common/common_types.h"
#include "video_core/buffer_cache/buffer_cache_base.h"
#include "video_core/buffer_cache/memory_tracker_base.h"
#include "video_core/renderer_opengl/gl_device.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_staging_buffer_pool.h"

namespace OpenGL {

class BufferCacheRuntime;

class Buffer : public VideoCommon::BufferBase {
public:
    explicit Buffer(BufferCacheRuntime&, DAddr cpu_addr, u64 size_bytes);
    explicit Buffer(BufferCacheRuntime&, VideoCommon::NullBufferParams);

    void ImmediateUpload(size_t offset, std::span<const u8> data) noexcept;

    void ImmediateDownload(size_t offset, std::span<u8> data) noexcept;

    void MakeResident(GLenum access) noexcept;

    void MarkUsage(u64 offset, u64 size) {}

    [[nodiscard]] GLuint View(u32 offset, u32 size, VideoCore::Surface::PixelFormat format);

    [[nodiscard]] GLuint64EXT HostGpuAddr() const noexcept {
        return address;
    }

    [[nodiscard]] GLuint Handle() const noexcept {
        return buffer.handle;
    }

private:
    struct BufferView {
        u32 offset;
        u32 size;
        VideoCore::Surface::PixelFormat format;
        OGLTexture texture;
    };

    GLuint64EXT address = 0;
    OGLBuffer buffer;
    GLenum current_residency_access = GL_NONE;
    std::vector<BufferView> views;
};

class BufferCacheRuntime {
    friend Buffer;

public:
    static constexpr u8 INVALID_BINDING = std::numeric_limits<u8>::max();

    explicit BufferCacheRuntime(const Device& device_, StagingBufferPool& staging_buffer_pool_);

    [[nodiscard]] StagingBufferMap UploadStagingBuffer(size_t size);

    [[nodiscard]] StagingBufferMap DownloadStagingBuffer(size_t size, bool deferred = false);

    void FreeDeferredStagingBuffer(StagingBufferMap& buffer);

    bool CanReorderUpload(const Buffer&, std::span<const VideoCommon::BufferCopy>) {
        return false;
    }

    void CopyBuffer(GLuint dst_buffer, GLuint src_buffer,
                    std::span<const VideoCommon::BufferCopy> copies, bool barrier);

    void CopyBuffer(GLuint dst_buffer, Buffer& src_buffer,
                    std::span<const VideoCommon::BufferCopy> copies, bool barrier);

    void CopyBuffer(Buffer& dst_buffer, GLuint src_buffer,
                    std::span<const VideoCommon::BufferCopy> copies, bool barrier,
                    bool can_reorder_upload = false);

    void CopyBuffer(Buffer& dst_buffer, Buffer& src_buffer,
                    std::span<const VideoCommon::BufferCopy> copies, bool);

    void PreCopyBarrier();
    void PostCopyBarrier();
    void Finish();

    void TickFrame(Common::SlotVector<Buffer>&) noexcept {}

    void ClearBuffer(Buffer& dest_buffer, u32 offset, size_t size, u32 value);

    void BindIndexBuffer(Buffer& buffer, u32 offset, u32 size);

    void BindVertexBuffer(u32 index, Buffer& buffer, u32 offset, u32 size, u32 stride);

    void BindVertexBuffers(VideoCommon::HostBindings<Buffer>& bindings);

    void BindUniformBuffer(size_t stage, u32 binding_index, Buffer& buffer, u32 offset, u32 size);

    void BindComputeUniformBuffer(u32 binding_index, Buffer& buffer, u32 offset, u32 size);

    void BindStorageBuffer(size_t stage, u32 binding_index, Buffer& buffer, u32 offset, u32 size,
                           bool is_written);

    void BindComputeStorageBuffer(u32 binding_index, Buffer& buffer, u32 offset, u32 size,
                                  bool is_written);

    void BindTransformFeedbackBuffer(u32 index, Buffer& buffer, u32 offset, u32 size);

    void BindTransformFeedbackBuffers(VideoCommon::HostBindings<Buffer>& bindings);

    void BindTextureBuffer(Buffer& buffer, u32 offset, u32 size,
                           VideoCore::Surface::PixelFormat format);

    void BindImageBuffer(Buffer& buffer, u32 offset, u32 size,
                         VideoCore::Surface::PixelFormat format);

    void BindTransformFeedbackObject(GPUVAddr tfb_object_addr);
    GLuint GetTransformFeedbackObject(GPUVAddr tfb_object_addr);

    u64 GetDeviceMemoryUsage() const;

    void BindFastUniformBuffer(size_t stage, u32 binding_index, u32 size) {
        const GLuint handle = fast_uniforms[stage][binding_index].handle;
        const GLsizeiptr gl_size = static_cast<GLsizeiptr>(size);
        if (use_assembly_shaders) {
            glBindBufferRangeNV(PABO_LUT[stage], binding_index, handle, 0, gl_size);
        } else {
            const GLuint base_binding = graphics_base_uniform_bindings[stage];
            const GLuint binding = base_binding + binding_index;
            glBindBufferRange(GL_UNIFORM_BUFFER, binding, handle, 0, gl_size);
        }
    }

    void PushFastUniformBuffer(size_t stage, u32 binding_index, std::span<const u8> data) {
        if (use_assembly_shaders) {
            glProgramBufferParametersIuivNV(
                PABO_LUT[stage], binding_index, 0,
                static_cast<GLsizei>(data.size_bytes() / sizeof(GLuint)),
                reinterpret_cast<const GLuint*>(data.data()));
        } else {
            glNamedBufferSubData(fast_uniforms[stage][binding_index].handle, 0,
                                 static_cast<GLsizeiptr>(data.size_bytes()), data.data());
        }
    }

    std::span<u8> BindMappedUniformBuffer(size_t stage, u32 binding_index, u32 size) noexcept {
        const auto [mapped_span, offset] = stream_buffer->Request(static_cast<size_t>(size));
        const GLuint base_binding = graphics_base_uniform_bindings[stage];
        const GLuint binding = base_binding + binding_index;
        glBindBufferRange(GL_UNIFORM_BUFFER, binding, stream_buffer->Handle(),
                          static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size));
        return mapped_span;
    }

    [[nodiscard]] const GLvoid* IndexOffset() const noexcept {
        return reinterpret_cast<const GLvoid*>(static_cast<uintptr_t>(index_buffer_offset));
    }

    [[nodiscard]] bool HasFastBufferSubData() const noexcept {
        return has_fast_buffer_sub_data;
    }

    [[nodiscard]] bool SupportsNonZeroUniformOffset() const noexcept {
        return !use_assembly_shaders;
    }

    void SetBaseUniformBindings(const std::array<GLuint, 5>& bindings) {
        graphics_base_uniform_bindings = bindings;
    }

    void SetBaseStorageBindings(const std::array<GLuint, 5>& bindings) {
        graphics_base_storage_bindings = bindings;
    }

    void SetImagePointers(GLuint* texture_handles_, GLuint* image_handles_) {
        texture_handles = texture_handles_;
        image_handles = image_handles_;
    }

    void SetEnableStorageBuffers(bool use_storage_buffers_) {
        use_storage_buffers = use_storage_buffers_;
    }

    u64 GetDeviceLocalMemory() const {
        return device_access_memory;
    }

    bool CanReportMemoryUsage() const {
        return device.CanReportMemoryUsage();
    }

    u32 GetStorageBufferAlignment() const {
        return static_cast<u32>(device.GetShaderStorageBufferAlignment());
    }

private:
    static constexpr std::array PABO_LUT{
        GL_VERTEX_PROGRAM_PARAMETER_BUFFER_NV,          GL_TESS_CONTROL_PROGRAM_PARAMETER_BUFFER_NV,
        GL_TESS_EVALUATION_PROGRAM_PARAMETER_BUFFER_NV, GL_GEOMETRY_PROGRAM_PARAMETER_BUFFER_NV,
        GL_FRAGMENT_PROGRAM_PARAMETER_BUFFER_NV,
    };

    const Device& device;
    StagingBufferPool& staging_buffer_pool;

    bool has_fast_buffer_sub_data = false;
    bool use_assembly_shaders = false;
    bool has_unified_vertex_buffers = false;

    bool use_storage_buffers = false;

    u32 max_attributes = 0;

    std::array<GLuint, 5> graphics_base_uniform_bindings{};
    std::array<GLuint, 5> graphics_base_storage_bindings{};
    GLuint* texture_handles = nullptr;
    GLuint* image_handles = nullptr;

    std::optional<StreamBuffer> stream_buffer;

    std::array<std::array<OGLBuffer, VideoCommon::NUM_GRAPHICS_UNIFORM_BUFFERS>,
               VideoCommon::NUM_STAGES>
        fast_uniforms;
    std::array<std::array<OGLBuffer, VideoCommon::NUM_GRAPHICS_UNIFORM_BUFFERS>,
               VideoCommon::NUM_STAGES>
        copy_uniforms;
    std::array<OGLBuffer, VideoCommon::NUM_COMPUTE_UNIFORM_BUFFERS> copy_compute_uniforms;

    u32 index_buffer_offset = 0;

    u64 device_access_memory;
    std::unordered_map<GPUVAddr, OGLTransformFeedback> tfb_objects;
};

struct BufferCacheParams {
    using Runtime = OpenGL::BufferCacheRuntime;
    using Buffer = OpenGL::Buffer;
    using Async_Buffer = OpenGL::StagingBufferMap;
    using MemoryTracker = VideoCommon::MemoryTrackerBase<Tegra::MaxwellDeviceMemoryManager>;

    static constexpr bool IS_OPENGL = true;
    static constexpr bool HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS = true;
    static constexpr bool HAS_FULL_INDEX_AND_PRIMITIVE_SUPPORT = true;
    static constexpr bool NEEDS_BIND_UNIFORM_INDEX = true;
    static constexpr bool NEEDS_BIND_STORAGE_INDEX = true;
    static constexpr bool USE_MEMORY_MAPS = true;
    static constexpr bool SEPARATE_IMAGE_BUFFER_BINDINGS = true;

    // TODO: Investigate why OpenGL seems to perform worse with persistently mapped buffer uploads
    static constexpr bool USE_MEMORY_MAPS_FOR_UPLOADS = false;
};

using BufferCache = VideoCommon::BufferCache<BufferCacheParams>;

} // namespace OpenGL
