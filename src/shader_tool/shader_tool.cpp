// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"
#include "shader_recompiler/backend/bindings.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/attribute.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/object_pool.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/program_header.h"
#include "shader_recompiler/runtime_info.h"
#include "shader_recompiler/shader_info.h"
#include "shader_recompiler/stage.h"

namespace {

using Shader::Environment;
using Shader::HostTranslateInfo;
using Shader::InputTopology;
using Shader::ProgramHeader;
using Shader::Profile;
using Shader::ReplaceConstant;
using Shader::RuntimeInfo;
using Shader::Stage;
using Shader::TexturePixelFormat;
using Shader::TextureType;

class ToolEnvironment final : public Environment {
public:
    ToolEnvironment(std::vector<u64> code_words, u32 program_base, u32 code_offset_in_program,
                    Stage stage_, std::optional<ProgramHeader> sph_in,
                    std::array<u32, 3> workgroup_size_, u32 local_memory_size_,
                    u32 shared_memory_size_, u32 texture_bound_,
                    std::unordered_map<u64, u32> cbuf_values_)
        : code{std::move(code_words)},
          cbuf_values{std::move(cbuf_values_)},
          workgroup_size_value{workgroup_size_},
          local_memory{local_memory_size_},
          shared_memory{shared_memory_size_},
          texture_bound_value{texture_bound_},
          code_lowest{program_base + code_offset_in_program} {
        start_address = program_base;
        stage = stage_;
        if (sph_in) {
            sph = *sph_in;
        }
        is_proprietary_driver = (texture_bound_value == 2);
    }

    u64 ReadInstruction(u32 address) override {
        if (code.empty() || address < code_lowest) {
            return 0;
        }
        const u32 byte_offset = address - code_lowest;
        const u32 index = byte_offset / static_cast<u32>(sizeof(u64));
        if (index >= code.size()) {
            return 0;
        }
        return code[index];
    }

    u32 ReadCbufValue(u32 cbuf_index, u32 cbuf_offset) override {
        const u64 key = (static_cast<u64>(cbuf_index) << 32) | cbuf_offset;
        const auto it = cbuf_values.find(key);
        return it == cbuf_values.end() ? 0u : it->second;
    }

    u32 ReadCbufSize(u32 cbuf_index) override {
        // 64 KiB is the conventional NVN constant buffer ceiling. Reporting it
        // unconditionally keeps the constant-propagation pass willing to fold
        // user-supplied cbuf overrides into the SPIR-V output.
        return cbuf_index < 18 ? 65536u : 0u;
    }

    TextureType ReadTextureType(u32 raw_handle) override {
        const auto it = texture_type_overrides.find(raw_handle);
        return it == texture_type_overrides.end() ? TextureType::Color2D : it->second;
    }

    TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return TexturePixelFormat::A8B8G8R8_UNORM;
    }

    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }

    u32 ReadViewportTransformState() override {
        return 1u;
    }

    u32 TextureBoundBuffer() const override {
        return texture_bound_value;
    }

    u32 LocalMemorySize() const override {
        return local_memory;
    }

    u32 SharedMemorySize() const override {
        return shared_memory;
    }

    std::array<u32, 3> WorkgroupSize() const override {
        return workgroup_size_value;
    }

    bool HasHLEMacroState() const override {
        return false;
    }

    std::optional<ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }

    void Dump(u64, u64) override {}

    std::unordered_map<u32, TextureType> texture_type_overrides;

private:
    std::vector<u64> code;
    std::unordered_map<u64, u32> cbuf_values;
    std::array<u32, 3> workgroup_size_value{};
    u32 local_memory{};
    u32 shared_memory{};
    u32 texture_bound_value{};
    u32 code_lowest{};
};

void PrintUsage() {
    std::fprintf(stderr,
        "Usage: shader_tool <shader.bin> [-o out.spv] [options]\n"
        "\n"
        "Translates a Maxwell/Turing shader binary to SPIR-V using the citron recompiler.\n"
        "By default the input is treated as a graphics shader: the first 0x50 bytes are\n"
        "parsed as the Shader Program Header and the remainder as instructions.\n"
        "Ryujinx GpuAccessor .data dump files are auto-detected (magic 0x12345678) and\n"
        "their 0x30-byte container header is stripped automatically.\n"
        "\n"
        "Options:\n"
        "  -o, --output <path>      Write SPIR-V to file (default: stdout, binary).\n"
        "      --base <hex>         Program base address (default: 0).\n"
        "      --compute            Treat input as a compute shader (no SPH).\n"
        "      --workgroup X,Y,Z    Compute workgroup size (default: 1,1,1).\n"
        "      --shared-mem <N>     Compute shared memory size in bytes.\n"
        "      --local-mem <N>      Local memory size in bytes (compute only;\n"
        "                           graphics derives this from the SPH).\n"
        "      --texture-bound <N>  Index of the bindless texture cbuf (default: 1;\n"
        "                           NVN proprietary driver typically uses 2).\n"
        "      --cbuf B:O=V         Override cbuf B at byte offset O (hex) with the u32\n"
        "                           value V (hex). Repeatable. Bank is decimal.\n"
        "      --cbuf0-file <path>  Load cbuf 0 contents from a raw little-endian binary;\n"
        "                           each 4-byte word is exposed at its natural offset.\n"
        "      --no-fp64            Force lowering of fp64 to fp32 during translation.\n"
        "  -h, --help               Show this help.\n"
        "\n"
        "NVN compatibility caveats:\n"
        "  * Texture and uniform-buffer bindings in the emitted SPIR-V follow citron's\n"
        "    runtime allocator, not the NVN ABI. A binding remap pass is the next step.\n"
        "  * Constant buffer 0 reads are inlined whenever values are supplied via\n"
        "    --cbuf or --cbuf0-file. Unset offsets resolve to 0.\n"
        "  * No emulator support-buffer code is injected: rescaling, render-area, and\n"
        "    debug verification passes are off in this tool.\n");
}

bool ParseHexU32(std::string_view sv, u32& out) {
    auto first = sv.data();
    auto last = sv.data() + sv.size();
    int base = 16;
    if (sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) {
        first += 2;
    }
    auto [ptr, ec] = std::from_chars(first, last, out, base);
    return ec == std::errc{} && ptr == last;
}

bool ParseDecU32(std::string_view sv, u32& out) {
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out, 10);
    return ec == std::errc{} && ptr == sv.data() + sv.size();
}

bool ReadFile(const std::string& path, std::vector<char>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size < 0) {
        return false;
    }
    file.seekg(0);
    out.resize(static_cast<size_t>(size));
    if (size > 0) {
        file.read(out.data(), size);
    }
    return static_cast<bool>(file);
}

Stage StageFromSphType(u32 sph_type) {
    switch (sph_type) {
    case 1: return Stage::VertexB;
    case 2: return Stage::TessellationControl;
    case 3: return Stage::TessellationEval;
    case 4: return Stage::Geometry;
    case 5: return Stage::Fragment;
    default: return Stage::VertexB;
    }
}

Profile DefaultToolProfile() {
    Profile p{};
    p.supported_spirv = 0x00010300;
    p.unified_descriptor_binding = true;
    p.support_descriptor_aliasing = true;
    p.support_int8 = true;
    p.support_int16 = true;
    p.support_int64 = true;
    p.support_vertex_instance_id = false;
    p.support_float_controls = true;
    p.support_separate_denorm_behavior = true;
    p.support_separate_rounding_mode = true;
    p.support_fp16_denorm_preserve = true;
    p.support_fp32_denorm_preserve = true;
    p.support_fp16_signed_zero_nan_preserve = true;
    p.support_fp32_signed_zero_nan_preserve = true;
    p.support_fp64_signed_zero_nan_preserve = true;
    p.support_explicit_workgroup_layout = true;
    p.support_vote = true;
    p.support_viewport_index_layer_non_geometry = true;
    p.support_typeless_image_loads = true;
    p.support_demote_to_helper_invocation = true;
    p.support_int64_atomics = true;
    p.support_derivative_control = true;
    p.support_native_ndc = true;
    p.support_scaled_attributes = true;
    p.support_multi_viewport = true;
    p.support_geometry_streams = true;
    p.min_ssbo_alignment = 16;
    p.max_user_clip_distances = 8;
    return p;
}

RuntimeInfo MakeRuntimeInfoForStage(Stage stage) {
    RuntimeInfo info{};
    if (stage == Stage::Fragment) {
        // Fragment shaders read inputs that the recompiler only emits when the
        // previous stage is recorded as having stored them. With no real prior
        // stage available offline, claim every generic + position component is
        // available so any read the shader performs gets a corresponding input
        // declaration in the emitted SPIR-V.
        for (size_t i = static_cast<size_t>(Shader::IR::Attribute::PositionX);
             i <= static_cast<size_t>(Shader::IR::Attribute::Generic31W); ++i) {
            info.previous_stage_stores.mask.set(i);
        }
        // Default FragmentOutputType is Float for all 8 RTs (enum value 0), which
        // matches what most fragment shaders expect; nothing more to do here.
        info.input_topology = InputTopology::Triangles;
    }
    return info;
}

HostTranslateInfo DefaultHostInfo(bool support_fp64) {
    HostTranslateInfo h{};
    h.support_float64 = support_fp64;
    h.support_float16 = true;
    h.support_int64 = true;
    h.needs_demote_reorder = false;
    h.support_snorm_render_buffer = true;
    h.support_viewport_index_layer = true;
    h.min_ssbo_alignment = 16;
    h.support_geometry_shader_passthrough = false;
    h.support_conditional_barrier = true;
    return h;
}

bool ParseWorkgroup(std::string_view sv, std::array<u32, 3>& out) {
    std::string copy{sv};
    for (auto& c : copy) {
        if (c == ',') c = ' ';
    }
    std::stringstream ss(copy);
    ss >> out[0] >> out[1] >> out[2];
    return static_cast<bool>(ss);
}

} // namespace

int main(int argc, char** argv) try {
    if (argc < 2) {
        PrintUsage();
        return EXIT_FAILURE;
    }

    std::string input_path;
    std::string output_path;
    u32 base_address = 0;
    bool is_compute = false;
    std::array<u32, 3> workgroup{1, 1, 1};
    u32 shared_mem = 0;
    u32 local_mem_override = 0;
    bool local_mem_was_set = false;
    u32 texture_bound = 1;
    bool support_fp64 = true;
    std::unordered_map<u64, u32> cbuf_overrides;
    std::string cbuf0_file;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        const auto NextArg = [&](std::string_view name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "shader_tool: missing value for %.*s\n",
                             static_cast<int>(name.size()), name.data());
                std::exit(EXIT_FAILURE);
            }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            PrintUsage();
            return EXIT_SUCCESS;
        } else if (arg == "-o" || arg == "--output") {
            output_path = NextArg(arg);
        } else if (arg == "--base") {
            if (!ParseHexU32(NextArg(arg), base_address)) {
                std::fprintf(stderr, "shader_tool: invalid --base value\n");
                return EXIT_FAILURE;
            }
        } else if (arg == "--compute") {
            is_compute = true;
        } else if (arg == "--workgroup") {
            if (!ParseWorkgroup(NextArg(arg), workgroup)) {
                std::fprintf(stderr, "shader_tool: --workgroup expects X,Y,Z (decimal)\n");
                return EXIT_FAILURE;
            }
        } else if (arg == "--shared-mem") {
            if (!ParseDecU32(NextArg(arg), shared_mem)) {
                std::fprintf(stderr, "shader_tool: invalid --shared-mem value\n");
                return EXIT_FAILURE;
            }
        } else if (arg == "--local-mem") {
            if (!ParseDecU32(NextArg(arg), local_mem_override)) {
                std::fprintf(stderr, "shader_tool: invalid --local-mem value\n");
                return EXIT_FAILURE;
            }
            local_mem_was_set = true;
        } else if (arg == "--texture-bound") {
            if (!ParseDecU32(NextArg(arg), texture_bound)) {
                std::fprintf(stderr, "shader_tool: invalid --texture-bound value\n");
                return EXIT_FAILURE;
            }
        } else if (arg == "--cbuf") {
            const std::string spec = NextArg(arg);
            const auto colon = spec.find(':');
            const auto eq = spec.find('=');
            if (colon == std::string::npos || eq == std::string::npos || colon > eq) {
                std::fprintf(stderr, "shader_tool: --cbuf expects bank:offset=value\n");
                return EXIT_FAILURE;
            }
            u32 bank = 0;
            u32 offset = 0;
            u32 value = 0;
            const std::string_view spec_view{spec};
            if (!ParseDecU32(spec_view.substr(0, colon), bank) ||
                !ParseHexU32(spec_view.substr(colon + 1, eq - colon - 1), offset) ||
                !ParseHexU32(spec_view.substr(eq + 1), value)) {
                std::fprintf(stderr, "shader_tool: invalid --cbuf spec '%s'\n", spec.c_str());
                return EXIT_FAILURE;
            }
            cbuf_overrides[(static_cast<u64>(bank) << 32) | offset] = value;
        } else if (arg == "--cbuf0-file") {
            cbuf0_file = NextArg(arg);
        } else if (arg == "--no-fp64") {
            support_fp64 = false;
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "shader_tool: unknown option '%.*s'\n",
                         static_cast<int>(arg.size()), arg.data());
            return EXIT_FAILURE;
        } else {
            if (!input_path.empty()) {
                std::fprintf(stderr, "shader_tool: multiple input files given\n");
                return EXIT_FAILURE;
            }
            input_path = arg;
        }
    }

    if (input_path.empty()) {
        PrintUsage();
        return EXIT_FAILURE;
    }

    std::vector<char> raw;
    if (!ReadFile(input_path, raw)) {
        std::fprintf(stderr, "shader_tool: failed to read '%s'\n", input_path.c_str());
        return EXIT_FAILURE;
    }

    // Ryujinx GpuAccessor "shader dump" .data files have a 0x30-byte container
    // header (magic 0x12345678, code size, hash, padding) before the SPH.
    // Strip it transparently so users can drop dump files directly into the tool.
    constexpr u32 kRyujinxDataMagic = 0x12345678u;
    constexpr size_t kRyujinxDataHeader = 0x30;
    if (raw.size() >= sizeof(u32)) {
        u32 magic = 0;
        std::memcpy(&magic, raw.data(), sizeof(u32));
        if (magic == kRyujinxDataMagic) {
            if (raw.size() < kRyujinxDataHeader) {
                std::fprintf(stderr,
                             "shader_tool: '%s' looks like a Ryujinx dump but is too small\n",
                             input_path.c_str());
                return EXIT_FAILURE;
            }
            raw.erase(raw.begin(), raw.begin() + kRyujinxDataHeader);
            std::fprintf(stderr, "shader_tool: detected Ryujinx .data dump, skipped 0x%zx-byte header\n",
                         kRyujinxDataHeader);
        }
    }

    if (raw.size() % sizeof(u64) != 0) {
        std::fprintf(stderr,
                     "shader_tool: input size %zu is not a multiple of 8 bytes (instruction width)\n",
                     raw.size());
        return EXIT_FAILURE;
    }

    if (!cbuf0_file.empty()) {
        std::vector<char> cbuf0_raw;
        if (!ReadFile(cbuf0_file, cbuf0_raw)) {
            std::fprintf(stderr, "shader_tool: failed to read cbuf0 file '%s'\n",
                         cbuf0_file.c_str());
            return EXIT_FAILURE;
        }
        for (size_t off = 0; off + sizeof(u32) <= cbuf0_raw.size(); off += sizeof(u32)) {
            u32 value;
            std::memcpy(&value, cbuf0_raw.data() + off, sizeof(u32));
            cbuf_overrides.try_emplace(static_cast<u64>(off), value);
        }
    }

    Stage stage = Stage::VertexB;
    std::optional<ProgramHeader> sph_opt;
    std::vector<u64> code_words;
    u32 code_offset_in_program = 0;
    u32 effective_local_mem = local_mem_override;

    if (is_compute) {
        code_words.resize(raw.size() / sizeof(u64));
        if (!code_words.empty()) {
            std::memcpy(code_words.data(), raw.data(), raw.size());
        }
        stage = Stage::Compute;
    } else {
        if (raw.size() < sizeof(ProgramHeader)) {
            std::fprintf(stderr,
                         "shader_tool: graphics shader smaller than SPH (%zu < %zu); pass --compute "
                         "if this is a compute kernel\n",
                         raw.size(), sizeof(ProgramHeader));
            return EXIT_FAILURE;
        }
        ProgramHeader sph{};
        std::memcpy(&sph, raw.data(), sizeof(ProgramHeader));
        sph_opt = sph;
        stage = StageFromSphType(sph.common0.shader_type);

        const size_t code_bytes = raw.size() - sizeof(ProgramHeader);
        code_words.resize(code_bytes / sizeof(u64));
        if (!code_words.empty()) {
            std::memcpy(code_words.data(), raw.data() + sizeof(ProgramHeader), code_bytes);
        }
        code_offset_in_program = static_cast<u32>(sizeof(ProgramHeader));
        if (!local_mem_was_set) {
            effective_local_mem = static_cast<u32>(sph.LocalMemorySize()) +
                                  sph.common3.shader_local_memory_crs_size;
        }
    }

    ToolEnvironment env{std::move(code_words),
                        base_address,
                        code_offset_in_program,
                        stage,
                        sph_opt,
                        workgroup,
                        effective_local_mem,
                        shared_mem,
                        texture_bound,
                        std::move(cbuf_overrides)};

    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool(16);
    Shader::ObjectPool<Shader::IR::Inst> inst_pool(8192);
    Shader::ObjectPool<Shader::IR::Block> block_pool(32);

    const HostTranslateInfo host_info = DefaultHostInfo(support_fp64);
    const Profile profile = DefaultToolProfile();

    const u32 cfg_start = base_address + code_offset_in_program;
    Shader::Maxwell::Flow::CFG cfg(env, flow_pool, cfg_start, /*exits_to_dispatcher=*/false);
    Shader::IR::Program program =
        Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host_info);

    Shader::Backend::Bindings binding;
    RuntimeInfo runtime_info = MakeRuntimeInfoForStage(stage);
    Shader::Maxwell::ConvertLegacyToGeneric(program, runtime_info);
    const std::vector<u32> spirv =
        Shader::Backend::SPIRV::EmitSPIRV(profile, runtime_info, program, binding);

    if (output_path.empty()) {
        std::fwrite(spirv.data(), sizeof(u32), spirv.size(), stdout);
    } else {
        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "shader_tool: failed to open '%s' for writing\n",
                         output_path.c_str());
            return EXIT_FAILURE;
        }
        out.write(reinterpret_cast<const char*>(spirv.data()),
                  static_cast<std::streamsize>(spirv.size() * sizeof(u32)));
    }
    return EXIT_SUCCESS;
} catch (const std::exception& e) {
    std::fprintf(stderr, "shader_tool: %s\n", e.what());
    return EXIT_FAILURE;
}
