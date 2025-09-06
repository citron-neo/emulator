// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"

namespace OpenGL::ShaderContext {
struct ShaderPools {
    void ReleaseContents() {
        flow_block.ReleaseContents();
        block.ReleaseContents();
        inst.ReleaseContents();
    }

    Shader::ObjectPool<Shader::IR::Inst> inst{8192};
    Shader::ObjectPool<Shader::IR::Block> block{32};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_block{32};
};

struct Context {
    explicit Context(Core::Frontend::EmuWindow& emu_window)
        : gl_context{emu_window.CreateSharedContext()}, scoped{*gl_context} {}

    std::unique_ptr<Core::Frontend::GraphicsContext> gl_context;
    Core::Frontend::GraphicsContext::Scoped scoped;
    ShaderPools pools;
};

} // namespace OpenGL::ShaderContext
