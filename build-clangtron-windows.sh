#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
# =============================================================================
# build-clangtron-windows.sh — PGO + LTO + PLO cross-compilation build script
#
# Builds Citron for Windows (x86_64-w64-mingw32) from Linux using a
# multi-stage optimization pipeline:
#
#   Stage 1  (generate):   PGO-instrumented PE (FE or IR PGO)
#   Stage 1b (csgenerate): [IR only] CS-instrumented PE; needs stage1 profile
#   Stage 2  (use):        PGO+LTO PE; auto-merges CS profiles if present
#   Stage 2b (build-elf):  Native Linux ELF with BBAddrMap for BOLT/Propeller
#   Stage 3 (choose one):
#     bolt       Instruments ELF, extracts hot order, relinks PE with /order:@
#     propeller  perf LBR on ELF → BB+function layout → relinks PE with /order:@
#
# PGO MODES (--pgo-type):
#
#   fe  Frontend PGO (-fprofile-instr-generate / -fprofile-instr-use).
#       Counters before optimization passes. More tolerant of flag changes.
#       CS-IRPGO not available with fe.
#
#   ir  LLVM IR PGO (-fprofile-generate / -fprofile-use). [DEFAULT]
#       Counters after early IR passes; better inlining decisions (~2-5% faster).
#       CRITICAL: --lto and optimization flags MUST be identical across
#       generate, csgenerate, and use. Only the bolt/propeller relink may differ.
#
#   CS-IRPGO (ir + csgenerate stage):
#       Second instrumentation pass on an already-PGO-optimized binary.
#       Captures per-call-site counts for better inlining of hot/cold paths.
#       Requires two Windows profiling sessions (stage1, then csgenerate).
#       The use stage auto-detects pgo-profiles/cs/ and merges both profiles.
#
#       CRITICAL: csgenerate must use default.profdata (stage1 only), never
#       merged.profdata. Using merged.profdata causes the new CS counters to
#       key against a doubly-CS-influenced IR, producing hash mismatches at
#       the use stage. The script enforces this and errors if only
#       merged.profdata is present.
#
# PROFILE RUNTIME (llvm-mingw path, all PGO modes):
#   All modes use libclang_rt.profile.a. On llvm-mingw PEs this must include
#   POSIX stubs (mmap, flock, etc.) missing from MinGW. ensure_profile_runtime_mingw()
#   verifies and rebuilds it if needed. -u,__llvm_profile_write_file and
#   -u,__llvm_profile_runtime prevent lld from stripping the runtime entry points.
#   (clang-cl equivalent: /INCLUDE: — see LINKER FORCE-KEEP FLAGS below.)
#
# LTO + PGO LINKER FLAGS (use stage):
#   -fprofile-use must appear on the linker line too (CMAKE_EXE_LINKER_FLAGS_RELEASE),
#   otherwise LTO's cross-TU inlining runs without profile guidance.
#
# BOLT:
#   BOLT is ELF-only. A native Linux ELF is built (build-elf), profiled, and its
#   hot function order is fed to lld's /order:@ when relinking the PE.
#   Agreement rate ~38-64% (many ELF hot functions are inlined away by full LTO).
#
# PROPELLER:
#   perf LBR on the Linux ELF → generate_propeller_profiles produces:
#     propeller_cc.prof      BB layout (ELF-only, not applied to PE)
#     propeller_symorder.txt hot function order (→ /order:@ for PE relink)
#   Basic-block layout for PE is blocked pending COFF BBAddrMap support in LLVM.
#   Track: https://github.com/llvm/llvm-project/pull/187268
#
# TOOLCHAIN:
#   llvm-mingw — self-contained Clang/LLD/libc++/compiler-rt for Windows targets.
#   Cached in CPM_SOURCE_CACHE (default: ~/.cache/cpm). Host LLVM (clang-21,
#   llvm-profdata, llvm-bolt) handles PGO merging, BOLT, and the Linux ELF.
#
# =============================================================================
# CLANG-CL PATH (--compiler clang-cl)
# =============================================================================
#   Native Windows build using VS's clang-cl + lld-link (MSVC ABI, COFF/PDB).
#   Linux-only stages (build-elf, bolt, propeller) are NOT available.
#   To use BOLT/Propeller with clang-cl profiles, run this path's use stage
#   first, then feed pgo-profiles/ into the llvm-mingw bolt/propeller stages.
#
#   HOST REQUIREMENTS (native Windows, MSYS2 CLANG64 shell):
#     - Visual Studio 2022 + "C++ Clang tools for Windows" (provides clang-cl.exe,
#       lld-link.exe, llvm-profdata.exe under VC/Tools/Llvm/x64/bin) + Win11 SDK
#     - MSYS2 CLANG64: nasm, yasm, glslang, ninja, sccache, jom
#     - Native Strawberry Perl + Python 3.12 (OpenSSL/FFmpeg need real Win32 tools)
#     - aqtinstall in the native Python (for CMake's Qt download step)
#     Run setup --compiler clang-cl once per machine.
#
#   COMPILE VS LINK FLAGS:
#     CMake invokes lld-link.exe directly (not via clang-cl) for the final link.
#     /clang:-prefixed tokens are a clang-cl driver escape hatch; lld-link treats
#     them as input file paths and fails. So PGO flags (/clang:-fprofile-*) go
#     only in compile flags (CITRON_CLANGCL_PGO_COMPILE_FLAGS), never in linker
#     flags. The LTO flag is also omitted from CMAKE_EXE_LINKER_FLAGS — lld-link
#     auto-detects bitcode .obj files. stage_clangcl() keeps pgo_flags and
#     pgo_link_flags as separate variables throughout for this reason.
#
#   PROFRAW NAMING:
#     Binaries bake in a relative filename pattern (no directory prefix):
#       generate:   citron-generate-<pgo-mode>-%p.profraw
#       csgenerate: citron-csgenerate-<pgo-mode>-%p.profraw
#     %p is per-process, so repeated runs never collide. LLVM_PROFILE_FILE
#     overrides this if set.
#
#     GOTCHA: in the generated build-clang-cl.cmd heredoc, %p must be written
#     as %%p — cmd.exe pairs bare % tokens as env-var references, silently
#     mangling the filename. The script builds _batch-suffixed copies of flags
#     strings with %p→%%p just before the heredoc write.
#
#   LINKER FORCE-KEEP FLAGS:
#     /OPT:REF can strip the CS profiling runtime's static initializer, causing
#     the binary to run cleanly but never write a .profraw. -fcs-profile-generate
#     does not get the automatic reference that -fprofile-generate does.
#     generate and csgenerate both pass /INCLUDE:__llvm_profile_runtime
#     /INCLUDE:__llvm_profile_write_file via pgo_link_flags to prevent this.
#     (llvm-mingw equivalent: -Wl,-u,__llvm_profile_write_file,...)
#     The use stage needs neither — no instrumentation runtime is linked.
#
#   SENTINEL: build/.citron-clangcl-gen-config records --lto and --pgo-type
#     used by generate. csgenerate/use error out if invoked with different values.
#
#   OUTPUT:
#     build/clang-cl/generate/   Stage 1 instrumented PE
#     build/clang-cl/csgenerate/ Stage 1b CS-instrumented PE
#     build/clang-cl/use/        Final PGO+LTO PE
#     (CMake work dirs: build/clang-cl/.work/<stage>/)
#
#   EXAMPLE (MSYS2 CLANG64):
#     ./build-clangtron-windows.sh setup      --compiler clang-cl
#     ./build-clangtron-windows.sh generate   --compiler clang-cl --pgo-type ir --lto full
#     # Run build/clang-cl/generate/citron.exe, copy .profraw → build/pgo-profiles/
#     ./build-clangtron-windows.sh use        --compiler clang-cl --pgo-type ir --lto full
#     # Optional CS pass:
#     ./build-clangtron-windows.sh csgenerate --compiler clang-cl --pgo-type ir --lto full
#     # Run csgenerate/citron.exe, copy .profraw → pgo-profiles/cs/, then re-run use
#     # Final binary: build/clang-cl/use/citron.exe
#
# USAGE:
#   ./build-clangtron-windows.sh [stage] [options]
#
#   Stages:
#     setup       Install dependencies (run once per machine)
#     generate    Stage 1:  PGO-instrumented PE
#     csgenerate  Stage 1b: [IR only] CS-instrumented PE; needs default.profdata
#     use         Stage 2:  PGO+LTO PE; auto-merges pgo-profiles/cs/ if present
#     build-elf   Stage 2b: Linux ELF with -fbasic-block-address-map for BOLT/Propeller
#                           (built on-demand; use --pgo none for a baseline ELF)
#     bolt        Stage 3A: BOLT function-order optimization (ELF-proxy → PE)
#     propeller   Stage 3B: Propeller BB+function layout (perf LBR → PE)
#     clean       Remove build directory
#
#     NOTE: build-elf/bolt/propeller require --compiler llvm-mingw (the default).
#
#   Options:
#     --source DIR             Citron source tree (default: cwd)
#     --build DIR              Build directory (default: ./build)
#     --compiler llvm-mingw|clang-cl  Toolchain (default: llvm-mingw)
#     --jobs N                 Parallel jobs (default: nproc)
#     --lto thin|full|none     LTO mode (default: full); MUST match across stages 1-2
#     --lite-lto               Alias for --lto thin
#     --no-lto                 Alias for --lto none
#     --pgo-type ir|fe|none    PGO mode (default: ir); MUST match across stages 1-2
#                              ir   = IR PGO; required for CS-IRPGO; LTO/flags must match
#                              fe   = Frontend PGO; more flag-change tolerant; no CS-IRPGO
#                              none = No PGO; use → build/use-nopgo/, LTO still applies
#     --release                Release build (default)
#     --relwithdebinfo         RelWithDebInfo (adds -g, keeps O3/LTO/PGO)
#     --unity                  Unity builds (~30-90% faster compile, no runtime effect)
#     --tracy                  Enable Tracy profiler instrumentation (default off)
#     --tracy-alloc             Enable Tracy heap allocator tracking (default off, requires --tracy)
#     --clang-version N        Host Clang version (default: 21)
#     --llvm-mingw-version VER llvm-mingw release tag (default: 20260224)
#
#   LTO notes:
#     full  Best performance; ~38-44% BOLT/Propeller agreement (aggressive inlining)
#     thin  Faster builds; slightly higher agreement rates
#     none  Not recommended; use build-elf (always disables LTO for BBAddrMap)
#
# REQUIREMENTS (installed by setup):
#   clang/clang++ 21+, lld, llvm-profdata, llvm-bolt, perf, llvm-mingw, cmake, ninja
#
# EXAMPLE — IR PGO + Propeller (recommended):
#   ./build-clangtron-windows.sh setup
#   ./build-clangtron-windows.sh generate   --pgo-type ir --lto full
#   # Copy build/generate/bin/ to Windows, run citron.exe 15-30 min, copy .profraw → build/pgo-profiles/
#   ./build-clangtron-windows.sh use        --pgo-type ir --lto full
#   ./build-clangtron-windows.sh propeller  --pgo-type ir --lto full
#   # Final: build/propeller/bin/citron.exe
#
# EXAMPLE — CS-IRPGO (two profiling sessions, best quality):
#   ./build-clangtron-windows.sh setup
#   ./build-clangtron-windows.sh generate   --pgo-type ir --lto full
#   # Session 1: run generate/citron.exe, copy .profraw → pgo-profiles/
#   ./build-clangtron-windows.sh use        --pgo-type ir --lto full
#   ./build-clangtron-windows.sh csgenerate --pgo-type ir --lto full
#   # Session 2: run csgenerate/citron.exe, copy .profraw → pgo-profiles/cs/
#   ./build-clangtron-windows.sh use        --pgo-type ir --lto full   # merges CS profiles
#   ./build-clangtron-windows.sh propeller  --pgo-type ir --lto full
#   # Final: build/propeller/bin/citron.exe
#
# EXAMPLE — BOLT:
#   ./build-clangtron-windows.sh bolt --pgo-type ir --lto full
#   # Script pauses: run build/use-elf/bin/citron-bolt-instrumented on Linux, then Enter
#   # Final: build/bolt/bin/citron.exe
# =============================================================================

set -euo pipefail

# =============================================================================
# Configuration
# =============================================================================

CLANG_VERSION="${CLANG_VERSION:-21}"
COMPILER_MODE="${COMPILER_MODE:-llvm-mingw}"
VS_INSTALL_PATH="${VS_INSTALL_PATH:-}"


# llvm-mingw release tag — cross-compilation toolchain (Clang+libc++/compiler-rt)
# https://github.com/mstorsjo/llvm-mingw/releases
LLVM_MINGW_VERSION="${LLVM_MINGW_VERSION:-20260224}"

SOURCE_DIR="${SOURCE_DIR:-$(pwd)}"
BUILD_ROOT="${BUILD_ROOT:-$(pwd)/build}"
JOBS="${JOBS:-$(nproc)}"
LTO_MODE="${LTO_MODE:-full}"
PGO_MODE="${PGO_MODE:-ir}"          # ir|fe|none
UNITY_BUILD="${UNITY_BUILD:-OFF}"   # ENABLE_UNITY_BUILD
TRACY_BUILD="${TRACY_BUILD:-OFF}"   # CITRON_ENABLE_TRACY
TRACY_ALLOC_BUILD="${TRACY_ALLOC_BUILD:-OFF}"   # CITRON_ENABLE_TRACY_ALLOC
BUILD_TYPE="${BUILD_TYPE:-Release}" # Release|RelWithDebInfo
CPM_SOURCE_CACHE="${CPM_SOURCE_CACHE:-${HOME}/.cache/cpm}"
CPM_SOURCE_CACHE="${CPM_SOURCE_CACHE/#\~/$HOME}"
# MARCH_NATIVE="-march=native"  # non-portable, host-tuned build
MARCH_NATIVE="${MARCH_NATIVE:-}"

# =============================================================================
# Host OS detection
# =============================================================================

_HOST_OS="linux"
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) _HOST_OS="windows" ;;
    Darwin*)               _HOST_OS="macos" ;;
esac

# MSYS2 clang64 toolchain prefix — mirrors llvm-mingw layout.
# Override with MSYS2_PREFIX for ucrt64/clang32/etc.
MSYS2_PREFIX="${MSYS2_PREFIX:-/clang64}"

# =============================================================================
# Derived paths
# =============================================================================

BUILD_GENERATE="${BUILD_ROOT}/generate"
BUILD_CSGENERATE="${BUILD_ROOT}/cs-generate"
BUILD_USE="${BUILD_ROOT}/use"
BUILD_USE_ELF="${BUILD_ROOT}/use-elf"
BUILD_BOLT="${BUILD_ROOT}/bolt"
BUILD_PROPELLER="${BUILD_ROOT}/propeller"
PROFILE_DIR="${BUILD_ROOT}/pgo-profiles"
BOLT_PROFILE_DIR="${BUILD_ROOT}/bolt-profiles"
PROPELLER_PROFILE_DIR="${BUILD_ROOT}/propeller-profiles"

# Windows: clang64 IS the llvm-mingw equivalent. Linux: downloaded into CPM cache.
if [[ "${_HOST_OS}" == "windows" ]]; then
    LLVM_MINGW_DIR="${MSYS2_PREFIX}"
else
    LLVM_MINGW_DIR="${CPM_SOURCE_CACHE}/llvm-mingw"
fi

CLANG="clang-${CLANG_VERSION}"
CLANGPP="clang++-${CLANG_VERSION}"
LLVM_PROFDATA="llvm-profdata-${CLANG_VERSION}"
LLVM_BOLT="llvm-bolt-${CLANG_VERSION}"
MERGE_FDATA="merge-fdata-${CLANG_VERSION}"

# On MSYS2/Windows LLVM tools are unversioned; BOLT/Propeller are Linux-only.
if [[ "${_HOST_OS}" == "windows" ]]; then
    CLANG="clang"
    CLANGPP="clang++"
    LLVM_PROFDATA="llvm-profdata"
    LLVM_BOLT=""
    MERGE_FDATA=""
fi

MINGW_TRIPLE="x86_64-w64-mingw32"
MINGW_CLANG=""
MINGW_CLANGPP=""

SPIRV_HEADERS_INSTALL="${BUILD_ROOT}/spirv-headers-install"
VULKAN_HEADERS_INSTALL="${BUILD_ROOT}/vulkan-headers-install"

# =============================================================================
# Helpers
# =============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET} $*"; }
success() { echo -e "${GREEN}[OK]${RESET} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET} $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; exit 1; }
header()  { echo -e "\n${BOLD}${GREEN}=================================================================${RESET}"; \
            echo -e "${BOLD}${GREEN}  $*${RESET}"; \
            echo -e "${BOLD}${GREEN}=================================================================${RESET}"; }

# =============================================================================
# Validate CPM_SOURCE_CACHE
# =============================================================================
if [[ "${CPM_SOURCE_CACHE}" == *" "* ]]; then
    error "CPM_SOURCE_CACHE ('${CPM_SOURCE_CACHE}') contains spaces.\n" \
          "       CPM and some build tools do not support paths with spaces.\n" \
          "       Please set CPM_SOURCE_CACHE to a path without spaces, e.g.:\n" \
          "       export CPM_SOURCE_CACHE=\"/tmp/cpm-cache\""
fi

# download_with_retry URL OUTPUT_FILE [MAX_RETRIES]
# wget with exponential back-off (5s→10s→20s…). Returns 1 after all attempts.
download_with_retry() {
    local url="$1"
    local output="$2"
    local max_retries="${3:-3}"
    local attempt=1
    local delay=5

    while [[ "${attempt}" -le "${max_retries}" ]]; do
        if [[ "${attempt}" -gt 1 ]]; then
            warn "Download retry ${attempt}/${max_retries} (waiting ${delay}s): $(basename "${output}")"
            sleep "${delay}"
            delay=$(( delay * 2 ))
        fi
        if wget -q --show-progress --timeout=60 --tries=1 \
                -O "${output}" "${url}" 2>&1; then
            return 0
        fi
        rm -f "${output}"
        attempt=$(( attempt + 1 ))
    done
    return 1
}

# _sudo — on Windows/MSYS2 runs directly (no sudo); on Linux delegates to sudo.
_sudo() {
    if [[ "${_HOST_OS}" == "windows" ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

# require_llvm_mingw — ensure llvm-mingw is present and set MINGW_CLANG/MINGW_CLANGPP.
# Linux: downloads via ensure_llvm_mingw if missing, then prepends bin/ to PATH.
# Windows: resolves from MSYS2 clang64 (PATH already configured by shell).
require_llvm_mingw() {
    if [[ "${_HOST_OS}" == "windows" ]]; then
        export CC=clang
        export CXX=clang++
        if command -v clang &>/dev/null; then
            MINGW_CLANG="$(cygpath -m "$(command -v clang)")"
            MINGW_CLANGPP="$(cygpath -m "$(command -v clang++)")"
            info "MSYS2: using clang from PATH: ${MINGW_CLANG}"
        else
            error "MSYS2 clang not found at ${LLVM_MINGW_DIR}/bin/ or in PATH.\n" \
                  "  Run: ./build-clangtron-windows.sh setup"
        fi
        return 0
    fi
    # Linux: download if needed, then activate.
    ensure_llvm_mingw
    setup_llvm_mingw_path
}

check_tool() {
    if ! command -v "$1" &>/dev/null; then
        error "Required tool not found: $1\n       Run: ./build-clangtron-windows.sh setup"
    fi
}

resolve_bolt_binaries() {
    if command -v "llvm-bolt-${CLANG_VERSION}" &>/dev/null; then
        LLVM_BOLT="llvm-bolt-${CLANG_VERSION}"
        MERGE_FDATA="merge-fdata-${CLANG_VERSION}"
    elif command -v llvm-bolt &>/dev/null; then
        LLVM_BOLT="llvm-bolt"
        MERGE_FDATA="merge-fdata"
    else
        LLVM_BOLT=""
        MERGE_FDATA=""
    fi
}

lto_cmake_flag() {
    case "$LTO_MODE" in
        full|thin) echo "ON" ;;
        none)      echo "OFF" ;;
    esac
}

lto_clang_flag() {
    case "$LTO_MODE" in
        full) echo "-flto" ;;
        thin) echo "-flto=thin" ;;
        none) echo "" ;;
    esac
}


# =============================================================================
# llvm-mingw toolchain
#
# Pre-built Clang+LLD+libc++/compiler-rt for Windows targets. No GCC runtime
# workarounds needed (no __once_callable TLS issues, no --whole-archive libstdc++).
# https://github.com/mstorsjo/llvm-mingw/releases
# =============================================================================
ensure_llvm_mingw() {
    local tarball="llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz"
    local url="https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/${tarball}"
    local sentinel="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang"

    if [[ -x "${sentinel}" ]]; then
        info "llvm-mingw already present: ${LLVM_MINGW_DIR}"
        MINGW_CLANG="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang"
        MINGW_CLANGPP="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang++"
        return 0
    fi

    mkdir -p "${CPM_SOURCE_CACHE}"
    info "Downloading llvm-mingw ${LLVM_MINGW_VERSION}..."
    info "  URL: ${url}"
    wget --quiet --show-progress -O "${CPM_SOURCE_CACHE}/${tarball}" "${url}" \
        || error "Failed to download llvm-mingw — check network or LLVM_MINGW_VERSION"

    info "Extracting llvm-mingw..."
    tar -xf "${CPM_SOURCE_CACHE}/${tarball}" -C "${CPM_SOURCE_CACHE}"
    rm -f "${CPM_SOURCE_CACHE}/${tarball}"

    # Find extracted dir (name includes version and platform), move to stable path
    local extract_dir
    extract_dir="$(find "${CPM_SOURCE_CACHE}" -maxdepth 1 -type d -name "llvm-mingw-${LLVM_MINGW_VERSION}*" | head -1)"
    [[ -n "${extract_dir}" ]] || error "Could not find extracted llvm-mingw directory"

    if [[ "${extract_dir}" != "${LLVM_MINGW_DIR}" ]]; then
        rm -rf "${LLVM_MINGW_DIR}"
        mv "${extract_dir}" "${LLVM_MINGW_DIR}"
    fi
    [[ -x "${sentinel}" ]] || error "llvm-mingw extraction failed — ${sentinel} not found"

    MINGW_CLANG="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang"
    MINGW_CLANGPP="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang++"
    success "llvm-mingw ${LLVM_MINGW_VERSION} installed: ${LLVM_MINGW_DIR}"

    local clang_ver
    clang_ver=$("${MINGW_CLANG}" --version 2>&1 | head -1 || true)
    info "  ${clang_ver}"
}

# Prepend llvm-mingw/bin to PATH; also ensure ~/.local/bin for aqt.
setup_llvm_mingw_path() {
    export PATH="${LLVM_MINGW_DIR}/bin:${PATH}"
    if [[ -d "${HOME}/.local/bin" && ":${PATH}:" != *":${HOME}/.local/bin:"* ]]; then
        export PATH="${HOME}/.local/bin:${PATH}"
    fi
    MINGW_CLANG="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang"
    MINGW_CLANGPP="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang++"
}

ensure_aqt() {
    if command -v aqt &>/dev/null || "${HOME}/.local/bin/aqt" --version &>/dev/null 2>&1; then
        return 0
    fi

    local python_mm=""
    python_mm="$(python3 -c 'import sys; print(f"{sys.version_info[0]}.{sys.version_info[1]}")' 2>/dev/null || true)"

    if [[ "${_HOST_OS}" == "windows" && "${python_mm}" == "3.11" ]]; then
        error "aqt auto-install disabled on MSYS2 CLANG64 Python 3.11.\n" \
              "       pip may try to build backports.zstd from source (fails: 'unknown file type .s').\n" \
              "       Fix: pacman -Syu && restart shell, or install aqt via pacman / newer Python."
    fi

    python3 -m pip install aqtinstall --break-system-packages --quiet
}

# =============================================================================
# build_bolt_from_source — BOLT is not in the LLVM apt repo for noble; build from source.
# =============================================================================
build_bolt_from_source() {
    header "Building BOLT ${CLANG_VERSION} from Source"

    local bolt_src="/tmp/llvm-bolt-${CLANG_VERSION}-src"
    local bolt_build="/tmp/llvm-bolt-${CLANG_VERSION}-build"
    local bolt_tag=""
    local install_dir="/usr/local/bin"

    # Single ls-remote call to find the latest point-release tag, then pick highest with sort -V.
    local found_tag=""
    local _all_tags
    _all_tags="$(git ls-remote --tags https://github.com/llvm/llvm-project.git \
        "refs/tags/llvmorg-${CLANG_VERSION}.*" 2>/dev/null || true)"
    found_tag="$(printf '%s\n' "${_all_tags}" \
        | grep -o "llvmorg-${CLANG_VERSION}\.[0-9][0-9.]*" \
        | sort -V | tail -1 || true)"

    if [[ -z "${found_tag}" ]]; then
        error "Could not find any LLVM ${CLANG_VERSION} release tag on GitHub.\n" \
              "       Check that CLANG_VERSION=${CLANG_VERSION} matches an actual LLVM release."
    fi
    bolt_tag="${found_tag}"
    info "Using LLVM tag: ${bolt_tag}"

    if [[ ! -d "${bolt_src}/.git" ]]; then
        info "Cloning LLVM source (sparse, shallow)..."
        git clone \
            --depth=1 \
            --branch "${bolt_tag}" \
            --filter=blob:none \
            --sparse \
            https://github.com/llvm/llvm-project.git \
            "${bolt_src}" || error "Failed to clone llvm-project at tag ${bolt_tag}"
        pushd "${bolt_src}" > /dev/null
        git sparse-checkout set llvm bolt cmake third-party
        popd > /dev/null
    else
        info "Cached clone found at ${bolt_src}, skipping re-clone."
    fi

    info "Configuring BOLT build..."
    cmake \
        -S "${bolt_src}/llvm" \
        -B "${bolt_build}" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
        -DLLVM_ENABLE_PROJECTS="bolt" \
        -DLLVM_TARGETS_TO_BUILD="X86" \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DCMAKE_C_COMPILER="clang-${CLANG_VERSION}" \
        -DCMAKE_CXX_COMPILER="clang++-${CLANG_VERSION}" \
        || error "BOLT cmake configure failed"

    info "Building llvm-bolt, merge-fdata, and BOLT runtime (approx 15-20 min)..."
    cmake --build "${bolt_build}" --target llvm-bolt merge-fdata bolt_rt -j "${JOBS}" \
        || error "BOLT build failed"

    _sudo cp "${bolt_build}/bin/llvm-bolt"   "${install_dir}/llvm-bolt-${CLANG_VERSION}"
    _sudo cp "${bolt_build}/bin/merge-fdata" "${install_dir}/merge-fdata-${CLANG_VERSION}"
    _sudo chmod +x "${install_dir}/llvm-bolt-${CLANG_VERSION}"
    _sudo chmod +x "${install_dir}/merge-fdata-${CLANG_VERSION}"
    _sudo cp "${bolt_build}/lib/libbolt_rt_instr.a"  /usr/local/lib/libbolt_rt_instr.a
    _sudo cp "${bolt_build}/lib/libbolt_rt_hugify.a" /usr/local/lib/libbolt_rt_hugify.a 2>/dev/null || true

    command -v "llvm-bolt-${CLANG_VERSION}" &>/dev/null \
        || error "Installation failed — llvm-bolt-${CLANG_VERSION} not found in PATH"

    success "llvm-bolt-${CLANG_VERSION} installed"
    success "merge-fdata-${CLANG_VERSION} installed"
}

# =============================================================================
# Stage: setup
# =============================================================================
stage_setup_clangcl() {
    header "Setting Up Native clang-cl Build Environment"
    [[ "${_HOST_OS}" == "windows" ]] ||
        error "--compiler clang-cl setup requires a native Windows/MSYS2 host."
    command -v pacman &>/dev/null ||
        error "pacman not found. Launch the MSYS2 CLANG64 terminal and re-run setup."

    info "Installing MSYS2 shell and assembler prerequisites..."
    pacman -S --needed --noconfirm base-devel git curl wget \
        mingw-w64-clang-x86_64-nasm mingw-w64-clang-x86_64-yasm \
        mingw-w64-clang-x86_64-glslang mingw-w64-clang-x86_64-ninja \
        mingw-w64-clang-x86_64-sccache mingw-w64-clang-x86_64-jom \
        pkgconf mingw-w64-clang-x86_64-pkgconf \
        2>/dev/null || error "Failed to install required MSYS2 packages."

    # Locate Python 3.12 — pre-installed on CI runners, installed via winget on dev machines.
    local setup_python=""
    for setup_python_candidate in \
            /c/hostedtoolcache/windows/Python/3.12.*/x64/python.exe \
            /c/Python312/python.exe \
            /c/Users/*/AppData/Local/Programs/Python/Python312/python.exe; do
        if [[ -x "${setup_python_candidate}" ]]; then
            setup_python="${setup_python_candidate}"
            break
        fi
    done

    local _need_winget=0
    [[ -x /c/Strawberry/perl/bin/perl.exe ]]         || _need_winget=1
    [[ -n "${setup_python}" ]]                        || _need_winget=1
    [[ -x "/c/Program Files/CMake/bin/cmake.exe" ]]  || _need_winget=1
    [[ -x "/c/Program Files/Git/cmd/git.exe" ]]      || _need_winget=1

    if [[ "${_need_winget}" -eq 1 ]]; then
        local winget
        winget="$(command -v winget.exe 2>/dev/null || true)"
        if [[ -z "${winget}" && -n "${LOCALAPPDATA:-}" ]]; then
            local winget_candidate
            winget_candidate="$(cygpath -au "${LOCALAPPDATA}")/Microsoft/WindowsApps/winget.exe"
            [[ -x "${winget_candidate}" ]] && winget="${winget_candidate}"
        fi
        [[ -n "${winget}" ]] ||
            error "winget.exe not found. Install Microsoft App Installer, then re-run setup."

        winget_install() {
            local id="$1"
            info "Ensuring ${id} is installed..."
            "${winget}" install --id "${id}" --exact --silent \
                --accept-package-agreements --accept-source-agreements \
                || warn "winget could not install ${id}; it may already be installed."
        }

        [[ -x /c/Strawberry/perl/bin/perl.exe ]]        || winget_install StrawberryPerl.StrawberryPerl
        [[ -n "${setup_python}" ]]                       || winget_install Python.Python.3.12
        [[ -x "/c/Program Files/CMake/bin/cmake.exe" ]] || winget_install Kitware.CMake
        [[ -x "/c/Program Files/Git/cmd/git.exe" ]]     || winget_install Git.Git

        # Re-probe Python after winget install.
        if [[ -z "${setup_python}" ]]; then
            for setup_python_candidate in /c/Python312/python.exe \
                    /c/Users/*/AppData/Local/Programs/Python/Python312/python.exe; do
                if [[ -x "${setup_python_candidate}" ]]; then
                    setup_python="${setup_python_candidate}"
                    break
                fi
            done
        fi
    else
        info "All Windows prerequisites already present — skipping winget."
    fi

    # Install aqtinstall into native Windows Python so cmake's find_program locates aqt.exe.
    local _pip_python=""
    for _pip_candidate in \
            /c/hostedtoolcache/windows/Python/3.12.*/x64/python.exe \
            /c/Python312/python.exe \
            "${setup_python}"; do
        if [[ -x "${_pip_candidate}" ]]; then
            _pip_python="${_pip_candidate}"
            break
        fi
    done
    if [[ -n "${_pip_python}" ]]; then
        info "Installing aqtinstall into native Windows Python ($(basename "$(dirname "${_pip_python}")")/python)..."
        "${_pip_python}" -m pip install aqtinstall --quiet \
            || warn "aqtinstall install failed — Qt download during cmake configure may fail"
    else
        warn "No native Windows Python found — aqtinstall not installed. Qt may fail to download."
    fi
    local vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
    [[ -x "${vswhere}" ]] ||
        error "Visual Studio Installer/vswhere missing. Install Visual Studio 2022 first."
    local vs_install
    vs_install="$("${vswhere}" -latest -products '*' \
        -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang \
        -property installationPath | tr -d '\r')"
    [[ -n "${vs_install}" ]] ||
        error "Visual Studio clang-cl component missing. Add Desktop development with C++, C++ Clang tools for Windows, and Windows 11 SDK."

    local vs_unix ok=1
    vs_unix="$(cygpath -au "${vs_install}")"
    for tool in \
        "${vs_unix}/VC/Tools/Llvm/x64/bin/clang-cl.exe" \
        "${vs_unix}/VC/Tools/Llvm/x64/bin/lld-link.exe" \
        "${vs_unix}/VC/Tools/Llvm/x64/bin/llvm-profdata.exe" \
        "/c/Strawberry/perl/bin/perl.exe" "${setup_python}" \
        "/c/Program Files/CMake/bin/cmake.exe" "/c/Program Files/Git/cmd/git.exe" \
        "/clang64/bin/nasm.exe" "/clang64/bin/ninja.exe" \
        "/clang64/bin/sccache.exe" "/clang64/bin/jom.exe"; do
        if [[ -x "${tool}" ]]; then
            success "  ${tool}"
        else
            warn "  NOT FOUND: ${tool}"
            ok=0
        fi
    done
    [[ "${ok}" -eq 1 ]] ||
        error "clang-cl setup incomplete. Restart MSYS2 after installs, then rerun setup."
    success "Native clang-cl build environment ready."
}

stage_setup() {
    header "Setting Up Build Environment"
    if [[ "${COMPILER_MODE}" == "clang-cl" ]]; then
        stage_setup_clangcl
        return
    fi

    # ── MSYS2/Windows path ────────────────────────────────────────────────────
    if [[ "${_HOST_OS}" == "windows" ]]; then
        info "Detected MSYS2/Windows host (MSYS2_PREFIX=${MSYS2_PREFIX})."
        if ! command -v pacman &>/dev/null; then
            error "pacman not found. Windows setup requires MSYS2 (clang64 environment).\n" \
                  "  Launch the 'MSYS2 CLANG64' terminal from the Start Menu and re-run."
        fi
        info "Installing toolchain and build tools via pacman..."
        pacman -S --needed --noconfirm \
            base-devel git curl wget pkgconf \
            mingw-w64-clang-x86_64-pkgconf \
            mingw-w64-clang-x86_64-python-pip \
            mingw-w64-clang-x86_64-python-psutil \
            mingw-w64-clang-x86_64-toolchain \
            mingw-w64-clang-x86_64-cmake \
            mingw-w64-clang-x86_64-ninja \
            mingw-w64-clang-x86_64-python \
            mingw-w64-clang-x86_64-boost \
            mingw-w64-clang-x86_64-SDL2 \
            mingw-w64-clang-x86_64-nasm \
            mingw-w64-clang-x86_64-yasm \
            mingw-w64-clang-x86_64-glslang \
            2>/dev/null || warn "Some pacman packages failed — check output above."

        info "MSYS2: llvm-mingw is the system clang64 environment."
        info "       LLVM_MINGW_DIR → ${LLVM_MINGW_DIR}"

        # Activate toolchain so shared setup steps below have MINGW_CLANG set.
        require_llvm_mingw

        mkdir -p "${BUILD_ROOT}"
        compile_comsupp_stubs
        setup_case_fixup_headers

        # Verify
        echo ""
        info "Verifying MSYS2 installation..."
        local _ok=1
        for _tool in clang "clang++" lld cmake ninja llvm-profdata; do
            if command -v "${_tool}" &>/dev/null; then
                success "  ${_tool} -> $(command -v "${_tool}")"
            else
                warn   "  ${_tool} -> NOT FOUND"
                _ok=0
            fi
        done
        [[ ${_ok} -eq 1 ]] && success "All required tools available." \
                           || warn    "Some tools missing — check output above."

        echo ""
        warn "ELF build, BOLT, and Propeller stages require a Linux host."
        echo ""
        info "Setup complete. Clone citron source if needed:"
        echo "  git clone --recursive https://github.com/citron-neo/emulator.git"
        echo ""
        info "Then run: ./build-clangtron-windows.sh generate"
        return 0
    fi
    # ── Linux path ───────────────────────────────────────────────────────────

    info "Updating package lists..."
    _sudo apt-get update -qq

    info "Installing core build tools..."
    _sudo apt-get install -y \
        build-essential cmake ninja-build git pkg-config \
        python3 python3-pip curl wget xz-utils \
        lsb-release software-properties-common gnupg

    ensure_aqt

    # Host LLVM: used for profdata merging, BOLT, and the Linux ELF build.
    info "Installing host LLVM ${CLANG_VERSION}..."
    if ! command -v "clang-${CLANG_VERSION}" &>/dev/null; then
        wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
        chmod +x /tmp/llvm.sh
        _sudo /tmp/llvm.sh "${CLANG_VERSION}"
    else
        info "clang-${CLANG_VERSION} already installed, skipping."
    fi


    _sudo apt-get install -y \
        "clang-${CLANG_VERSION}" \
        "clang++-${CLANG_VERSION}" \
        "lld-${CLANG_VERSION}" \
        "llvm-${CLANG_VERSION}" \
        "llvm-${CLANG_VERSION}-dev" \
        "libclang-rt-${CLANG_VERSION}-dev" \
        || warn "Some LLVM packages failed to install."

    # BOLT is not in the LLVM apt repo for noble. Building it from source
    # takes ~15-20 min, and only the bolt stage actually needs it —
    # generate/csgenerate/use/build-elf/propeller never touch BOLT. Don't
    # force that cost on every setup run — stage_bolt() builds it on
    # demand, the first time it's actually needed.
    if command -v "llvm-bolt-${CLANG_VERSION}" &>/dev/null; then
        info "llvm-bolt-${CLANG_VERSION} already installed."
    else
        info "llvm-bolt-${CLANG_VERSION} not found — will be built from source on first use of the bolt stage (~15-20 min, one-time)."
    fi

    info "Setting up llvm-mingw cross-compilation toolchain..."
    mkdir -p "${BUILD_ROOT}"
    ensure_llvm_mingw

    info "Installing citron build dependencies..."
    _sudo apt-get install -y \
        nasm yasm glslang-tools

    # Idempotent (sentinel-guarded) — fast no-ops on re-run.
    mkdir -p "${BUILD_ROOT}"
    compile_comsupp_stubs
    setup_case_fixup_headers

    # ── Verify ────────────────────────────────────────────────────────────────
    echo ""
    info "Verifying installation..."
    local ok=1

    for tool in "clang-${CLANG_VERSION}" "clang++-${CLANG_VERSION}" \
                "lld-${CLANG_VERSION}" "llvm-profdata-${CLANG_VERSION}" \
                cmake ninja; do
        if command -v "$tool" &>/dev/null; then
            success "  $tool -> $(command -v "$tool")"
        else
            warn "  $tool -> NOT FOUND"
            ok=0
        fi
    done

    local mingw_clang="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang"
    if [[ -x "${mingw_clang}" ]]; then
        local ver
        ver=$("${mingw_clang}" --version 2>&1 | head -1 || true)
        success "  ${MINGW_TRIPLE}-clang -> ${mingw_clang}"
        success "    (${ver})"
    else
        warn "  ${MINGW_TRIPLE}-clang -> NOT FOUND (${mingw_clang})"
        ok=0
    fi

    if command -v "llvm-bolt-${CLANG_VERSION}" &>/dev/null; then
        success "  llvm-bolt-${CLANG_VERSION} -> $(command -v "llvm-bolt-${CLANG_VERSION}")"
    else
        warn "  llvm-bolt-${CLANG_VERSION} -> NOT FOUND (generate/use stages still work)"
    fi

    [[ $ok -eq 1 ]] && success "All required tools available." \
                    || warn "Some tools missing — check output above."

    echo ""
    info "Setup complete. Clone citron source if needed:"
    echo "  git clone --recursive https://github.com/citron-neo/emulator.git"
    echo ""
    info "Then run: ./build-clangtron-windows.sh generate"
}

# =============================================================================
# ensure_profile_runtime_mingw — verify libclang_rt.profile.a for the MinGW target;
# rebuild from LLVM sources if missing or incomplete.
# =============================================================================
ensure_profile_runtime_mingw() {
    [[ -x "${MINGW_CLANG}" ]] || error "MINGW_CLANG not set — call ensure_llvm_mingw first"

    local resource_dir
    resource_dir=$("${MINGW_CLANG}" --print-resource-dir 2>/dev/null || true)
    if [[ -z "${resource_dir}" ]]; then
        warn "Could not determine llvm-mingw resource dir — skipping profile runtime check"
        return 0
    fi

    # clang MinGW driver resolves profile runtime by ToolChain.getTriple().str()
    # which is "x86_64-w64-mingw32", not "x86_64-w64-windows-gnu".
    local target_triple="${MINGW_TRIPLE}"
    local runtime_dir="${resource_dir}/lib/${target_triple}"
    local runtime_lib="${runtime_dir}/libclang_rt.profile.a"

    # Also accept the legacy "windows" layout (libclang_rt.profile-x86_64.a).
    local windows_dir="${resource_dir}/lib/windows"
    local windows_lib="${windows_dir}/libclang_rt.profile-x86_64.a"

_profile_rt_valid() {
        local lib="$1"
        [[ -f "${lib}" ]] || return 1
        local nm_tool="llvm-nm-${CLANG_VERSION}"
        command -v "${nm_tool}" >/dev/null 2>&1 || nm_tool="llvm-nm"
        command -v "${nm_tool}" >/dev/null 2>&1 || nm_tool="nm"
        local nm_out
        nm_out=$("${nm_tool}" --defined-only "${lib}" 2>/dev/null || true)

        # llvm-mingw 20260224 x86_64 can expose __llvm_profile_raw_version while
        # missing the Windows mmap/flock helpers — check both.
        echo "${nm_out}" | grep -q '__llvm_profile_raw_version' || return 1

        if [[ "${lib}" == *profile-x86_64.a || "${lib}" == *x86_64-w64-mingw32/libclang_rt.profile.a ]]; then
            local required=(
                'lprofProfileDumped$'
                '__llvm_profile_mmap$'
                '__llvm_profile_flock$'
                '__llvm_profile_munmap$'
                '__llvm_profile_madvise$'
            )
            local sym
            for sym in "${required[@]}"; do
                echo "${nm_out}" | grep -Eq "[[:xdigit:]]+[[:space:]]+[TDBR][[:space:]]+${sym}" || return 1
            done
        fi

        return 0
    }

    if _profile_rt_valid "${runtime_lib}"; then
        info "Profile runtime OK: ${runtime_lib}"
        export PROFILE_RUNTIME_LIB="${runtime_lib}"
        return 0
    fi
    if _profile_rt_valid "${windows_lib}"; then
        info "Profile runtime OK (windows layout): ${windows_lib}"
        mkdir -p "${runtime_dir}"
        if cp -f "${windows_lib}" "${runtime_lib}" 2>/dev/null; then
            info "Installed MinGW-layout profile runtime from existing windows-layout archive"
            export PROFILE_RUNTIME_LIB="${runtime_lib}"
        else
            warn "Could not copy profile runtime into ${runtime_dir}; using windows-layout archive directly"
            export PROFILE_RUNTIME_LIB="${windows_lib}"
        fi
        return 0
    fi

    [[ -f "${runtime_lib}" ]] \
        && warn "Profile runtime exists but missing required symbols — rebuilding." \
        || warn "Profile runtime not found at ${runtime_lib} — building from source."

    # Fallback: build from LLVM compiler-rt sources
    local clang_version
    clang_version=$("${MINGW_CLANG}" --version 2>&1 | grep -oP '\d+\.\d+\.\d+' | head -1)
    [[ -n "${clang_version}" ]] \
        || { warn "Cannot determine Clang version — skipping profile runtime build"; return 0; }

    local llvm_tag="llvmorg-${clang_version}"
    local build_dir="${BUILD_ROOT}/compiler-rt-profile"
    local src_dir="${build_dir}/src"
    local inc_dir="${build_dir}/include"
    local obj_dir="${build_dir}/obj"
    mkdir -p "${src_dir}" "${inc_dir}" "${obj_dir}"
    info "Building profile runtime from ${llvm_tag}..."

    local raw_base="https://raw.githubusercontent.com/llvm/llvm-project/${llvm_tag}"
    # InstrProfilingRuntime: .cpp since LLVM 16, .c before.
    local profile_c_srcs=(
        InstrProfiling.c InstrProfilingBuffer.c InstrProfilingFile.c
        InstrProfilingMerge.c InstrProfilingMergeFile.c InstrProfilingNameVar.c
        InstrProfilingPlatformWindows.c InstrProfilingUtil.c
        InstrProfilingValue.c InstrProfilingVersionVar.c InstrProfilingWriter.c
    )

    # Probe for InstrProfilingRuntime — .cpp since LLVM 16, .c before that.
    # LLVM 16+ always uses .cpp; since we require Clang >= 19 we can skip the .c
    # fallback entirely.  We still do a two-step probe but avoid relying on network
    # HEAD requests (which can be blocked or return unreliable results in CI), and
    # we purge any stale zero-byte files from previous failed attempts.
    local runtime_src=""
    local major_ver
    major_ver=$(echo "${clang_version}" | cut -d. -f1)
    if (( major_ver >= 16 )); then
        local stale="${src_dir}/InstrProfilingRuntime.c"
        [[ -f "${stale}" ]] && rm -f "${stale}"
        runtime_src="InstrProfilingRuntime.cpp"
    else
        # Legacy: probe for .c or .cpp
        for ext in c cpp; do
            local candidate="InstrProfilingRuntime.${ext}"
            if [[ -s "${src_dir}/${candidate}" ]]; then
                runtime_src="${candidate}"; break
            fi
        done
        [[ -n "${runtime_src}" ]] || runtime_src="InstrProfilingRuntime.c"
    fi
    [[ -n "${runtime_src}" ]] \
        || { warn "Cannot determine InstrProfilingRuntime source for ${llvm_tag}"; return 1; }
    profile_c_srcs+=("${runtime_src}")

    # Remove zero-byte/partial files from previous failed attempts.
    find "${src_dir}" "${inc_dir}" -maxdepth 1 -type f -empty -delete 2>/dev/null || true

    _populate_profile_sources_from_git() {
        local git_src="${build_dir}/llvm-project-src-${llvm_tag}"
        local repo_profile="${git_src}/compiler-rt/lib/profile"
        local repo_include="${git_src}/compiler-rt/include/profile"

        if [[ ! -d "${git_src}/.git" ]]; then
            command -v git >/dev/null 2>&1 \
                || { warn "git not available for LLVM source fallback"; return 1; }

            info "  Falling back to sparse llvm-project checkout..."
            git clone \
                --depth=1 \
                --branch "${llvm_tag}" \
                --filter=blob:none \
                --sparse \
                https://github.com/llvm/llvm-project.git \
                "${git_src}" \
                || return 1

            pushd "${git_src}" >/dev/null
            git sparse-checkout set compiler-rt/lib/profile compiler-rt/include/profile \
                || { popd >/dev/null; return 1; }
            popd >/dev/null
        elif [[ ! -d "${repo_profile}" || ! -d "${repo_include}" ]]; then
            pushd "${git_src}" >/dev/null
            git sparse-checkout set compiler-rt/lib/profile compiler-rt/include/profile \
                || { popd >/dev/null; return 1; }
            popd >/dev/null
        fi

        [[ -d "${repo_profile}" ]] || return 1
        [[ -d "${repo_include}" ]] || return 1

        for f in "${profile_c_srcs[@]}"; do
            cp -f "${repo_profile}/${f}" "${src_dir}/${f}" || return 1
        done
        for f in InstrProfiling.h InstrProfilingInternal.h InstrProfilingPort.h \
                  InstrProfilingUtil.h WindowsMMap.h; do
            cp -f "${repo_profile}/${f}" "${src_dir}/${f}" || return 1
        done
        cp -f "${repo_include}/InstrProfData.inc" "${inc_dir}/InstrProfData.inc" \
            || return 1
        return 0
    }

    # curl_retry: download $1 → $2 with exponential backoff.
    # GitHub's raw content CDN returns HTTP 429 (Too Many Requests) when multiple
    # files are fetched in rapid succession from the same IP.  We retry up to 4
    # times (delays: 0 s, 2 s, 8 s, 32 s) before giving up.
    curl_retry() {
        local url="$1" dest="$2" fatal="${3:-1}"
        local delay=0 attempt
        for attempt in 1 2 3 4; do
            [[ ${delay} -gt 0 ]] && { info "  (rate-limited, retrying in ${delay}s…)"; sleep "${delay}"; }
            if curl -fsSL --retry 0 -o "${dest}" "${url}" 2>/dev/null; then
                return 0
            fi
            delay=$(( delay == 0 ? 2 : delay * 4 ))
        done
        rm -f "${dest}" 2>/dev/null || true
        [[ "${fatal}" == 1 ]] \
            && { warn "Failed to download $(basename "${url}")"; return 1; } \
            || return 1
    }

    for f in "${profile_c_srcs[@]}"; do
        [[ -f "${src_dir}/${f}" ]] && continue
        info "  Downloading ${f}..."
        curl_retry "${raw_base}/compiler-rt/lib/profile/${f}" "${src_dir}/${f}" 1 \
            || { warn "Raw source fetch failed; using sparse llvm-project checkout."; _populate_profile_sources_from_git || return 1; break; }
    done
    for f in InstrProfiling.h InstrProfilingInternal.h InstrProfilingPort.h \
              InstrProfilingUtil.h WindowsMMap.h; do
        [[ -f "${src_dir}/${f}" ]] && continue
        curl_retry "${raw_base}/compiler-rt/lib/profile/${f}" "${src_dir}/${f}" 0 || true
    done
    [[ -f "${inc_dir}/InstrProfData.inc" ]] || \
        curl_retry "${raw_base}/compiler-rt/include/profile/InstrProfData.inc" \
            "${inc_dir}/InstrProfData.inc" 1 \
        || { warn "Raw include fetch failed; using sparse llvm-project checkout."; _populate_profile_sources_from_git || return 1; }

    mkdir -p "${inc_dir}/sys"
    [[ -f "${inc_dir}/sys/utsname.h" ]] || cat > "${inc_dir}/sys/utsname.h" <<'EOF'
#pragma once
struct utsname { char sysname[256]; char nodename[256]; char release[256];
                 char version[256]; char machine[256]; };
static inline int uname(struct utsname *buf) { (void)buf; return -1; }
EOF

    local stubs_file="${src_dir}/InstrProfilingWindowsStubs.c"
    cat > "${stubs_file}" <<'STUBS_EOF'
#include <windows.h>
#include <errno.h>
#include <io.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

static int profile_dumped_flag = 0;

unsigned lprofProfileDumped(void) {
    return (unsigned)profile_dumped_flag;
}

void lprofSetProfileDumped(int value) {
    profile_dumped_flag = value;
}

void* __llvm_profile_mmap(void* start, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)prot;
    (void)flags;

    HANDLE file = (HANDLE)_get_osfhandle(fd);
    if (file == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return (void*)-1;
    }

    DWORD protect = PAGE_READONLY;
    if (prot & 0x2) {
        protect = PAGE_READWRITE;
    }

    ULARGE_INTEGER map_size;
    map_size.QuadPart = (unsigned long long)offset + (unsigned long long)length;

    HANDLE mapping = CreateFileMappingW(file, NULL, protect, map_size.HighPart, map_size.LowPart, NULL);
    if (!mapping) {
        errno = EINVAL;
        return (void*)-1;
    }

    DWORD access = FILE_MAP_READ;
    if (prot & 0x2) {
        access |= FILE_MAP_WRITE;
    }

    ULARGE_INTEGER view_offset;
    view_offset.QuadPart = (unsigned long long)offset;
    void* view = MapViewOfFileEx(mapping, access, view_offset.HighPart, view_offset.LowPart, length, start);
    CloseHandle(mapping);

    if (!view) {
        errno = EINVAL;
        return (void*)-1;
    }

    return view;
}

void __llvm_profile_munmap(void* addr, size_t length) {
    (void)length;
    if (addr && addr != (void*)-1) {
        UnmapViewOfFile(addr);
    }
}

int __llvm_profile_madvise(void* addr, size_t length, int advice) {
    (void)addr;
    (void)length;
    (void)advice;
    return 0;
}

int __llvm_profile_flock(int fd, int operation) {
    HANDLE file = (HANDLE)_get_osfhandle(fd);
    if (file == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    OVERLAPPED ov = {0};
    DWORD flags = 0;

    if (operation & 0x8) {
        if (!UnlockFileEx(file, 0, MAXDWORD, MAXDWORD, &ov)) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }

    if (operation & 0x4) {
        flags |= LOCKFILE_FAIL_IMMEDIATELY;
    }
    if (operation & 0x2) {
        flags |= LOCKFILE_EXCLUSIVE_LOCK;
    }

    if (!LockFileEx(file, flags, 0, MAXDWORD, MAXDWORD, &ov)) {
        errno = EWOULDBLOCK;
        return -1;
    }
    return 0;
}
STUBS_EOF

    local cflags=(
        "-I${src_dir}" "-I${inc_dir}" "-O2"
        "-fno-stack-protector" "-fno-exceptions"
        "-D_WIN32" "-D__MINGW32__"
        "-UCOMPILER_RT_HAS_FCNTL_LCK" "-UCOMPILER_RT_HAS_UNAME"
        "-DCOMPILER_RT_HAS_ATOMICS=1"
        "-fvisibility=default"
    )

    local objs=()
    for src in "${profile_c_srcs[@]}"; do
        local obj="${obj_dir}/${src%.c}.o"
        info "  Compiling ${src}..."
        "${MINGW_CLANG}" "${cflags[@]}" -c "${src_dir}/${src}" -o "${obj}" \
            || { warn "Failed to compile ${src}"; rm -f "${obj}"; return 1; }
        objs+=("${obj}")
    done

    local stubs_obj="${obj_dir}/InstrProfilingWindowsStubs.o"
    "${MINGW_CLANG}" "${cflags[@]}" -c "${stubs_file}" -o "${stubs_obj}" \
        || { warn "Failed to compile stubs"; return 1; }
    objs+=("${stubs_obj}")

    local ar="${LLVM_MINGW_DIR}/bin/llvm-ar"
    [[ -x "${ar}" ]] || ar="llvm-ar-${CLANG_VERSION}"
    command -v "${ar}" >/dev/null 2>&1 || ar="ar"

    local tmp_lib="${obj_dir}/libclang_rt.profile.a"
    mkdir -p "${runtime_dir}"
    "${ar}" rcs "${tmp_lib}" "${objs[@]}" \
        && cp "${tmp_lib}" "${runtime_lib}" \
        || { warn "Failed to create profile runtime"; return 1; }

    # Also install to the windows-layout directory so older clang versions find it
    mkdir -p "${windows_dir}"
    cp "${tmp_lib}" "${windows_dir}/libclang_rt.profile-x86_64.a" 2>/dev/null || true

    export PROFILE_RUNTIME_LIB="${runtime_lib}"
    success "Profile runtime built: ${runtime_lib}"
}





# =============================================================================
# comsupp stub — _com_util::ConvertStringToBSTR (MSVC comsuppw.lib); MinGW doesn't ship it.
# =============================================================================
compile_comsupp_stubs() {
    local stub_src="${BUILD_ROOT}/comsupp_stubs.cpp"
    local stub_obj="${BUILD_ROOT}/comsupp_stubs.o"

    [[ -x "${MINGW_CLANGPP}" ]] || error "MINGW_CLANGPP not set — call ensure_llvm_mingw first"

    if [[ -f "${stub_obj}" ]]; then
        info "comsupp_stubs.o already compiled: ${stub_obj}"
    else
        info "Compiling _com_util::ConvertStringToBSTR stub..."

        cat > "${stub_src}" << 'COMSUPP_CPP_EOF'
// Stub for _com_util::ConvertStringToBSTR (MSVC comsuppw.lib).
// performance_overlay.cpp uses it for WMI BSTR strings.
// Uses LocalAlloc (no oleaut32 dep at compile time; SysFreeString uses
// LocalFree internally so BSTRs are safe to free with SysFreeString).
#include <windows.h>
namespace _com_util {
    BSTR __stdcall ConvertStringToBSTR(const char* pSrc) {
        if (!pSrc) return nullptr;
        int nWide = MultiByteToWideChar(CP_ACP, 0, pSrc, -1, nullptr, 0);
        if (nWide <= 0) nWide = 1;
        UINT byteLen = (UINT)(nWide - 1) * sizeof(WCHAR);
        BYTE* raw = (BYTE*)LocalAlloc(LMEM_FIXED, sizeof(UINT) + nWide * sizeof(WCHAR));
        if (!raw) return nullptr;
        *((UINT*)raw) = byteLen;
        WCHAR* bstr = (WCHAR*)(raw + sizeof(UINT));
        if (nWide > 1)
            MultiByteToWideChar(CP_ACP, 0, pSrc, -1, bstr, nWide);
        else
            bstr[0] = L'\0';
        return bstr;
    }
}
COMSUPP_CPP_EOF

        # llvm-mingw wrapper sets --target, --sysroot, -stdlib=libc++ automatically
        "${MINGW_CLANGPP}" -O2 -c "${stub_src}" -o "${stub_obj}" \
            || error "Failed to compile comsupp_stubs.o"

        success "comsupp_stubs.o compiled: ${stub_obj}"
    fi

    # CMAKE_CXX_STANDARD_LIBRARIES embeds the path as a raw linker flag string; CMake splits
    # on spaces, so a path with spaces (e.g. username "Gaming PC") breaks the link. If
    # BUILD_ROOT has spaces, copy the .o to /tmp (always space-free on MSYS2).
    if [[ "${stub_obj}" == *' '* ]]; then
        local _safe_obj="/tmp/citron-comsupp_stubs.o"
        cp "${stub_obj}" "${_safe_obj}" \
            || error "Failed to copy comsupp_stubs.o to space-free path ${_safe_obj}"
        if [[ "${_HOST_OS}" == "windows" ]]; then
            _COMSUPP_TC_PATH="$(cygpath -am "${_safe_obj}")"
        else
            _COMSUPP_TC_PATH="$(realpath -m "${_safe_obj}")"
        fi
        info "comsupp_stubs.o staged to space-free path: ${_COMSUPP_TC_PATH}"
    else
        if [[ "${_HOST_OS}" == "windows" ]]; then
            _COMSUPP_TC_PATH="$(cygpath -am "${stub_obj}")"
        else
            _COMSUPP_TC_PATH="$(realpath -m "${stub_obj}")"
        fi
    fi
}

# =============================================================================
# Windows header case-fixup directory
# =============================================================================
setup_case_fixup_headers() {
    local fixup_dir="${BUILD_ROOT}/mingw-case-fixups"
    info "Creating Windows header case-fixup directory..."
    mkdir -p "${fixup_dir}"

    local -a pairs=(
        "Windows.h:windows.h"       "Winsock2.h:winsock2.h"
        "Ws2tcpip.h:ws2tcpip.h"     "Winerror.h:winerror.h"
        "Winnt.h:winnt.h"           "Windef.h:windef.h"
        "Winbase.h:winbase.h"       "Wingdi.h:wingdi.h"
        "Winuser.h:winuser.h"       "Objbase.h:objbase.h"
        "Ole2.h:ole2.h"             "Shlobj.h:shlobj.h"
        "Shellapi.h:shellapi.h"     "Commctrl.h:commctrl.h"
        "Psapi.h:psapi.h"           "Tlhelp32.h:tlhelp32.h"
        "Dbghelp.h:dbghelp.h"       "Mmsystem.h:mmsystem.h"
        "Iphlpapi.h:iphlpapi.h"
        "WbemIdl.h:wbemidl.h"       "WbemCli.h:wbemcli.h"
        "WbemDisp.h:wbemdisp.h"     "WbemProv.h:wbemprov.h"
        "WbemTran.h:wbemtran.h"     "ObjBase.h:objbase.h"
        "ObjIdl.h:objidl.h"         "PropIdl.h:propidl.h"
        "ComDef.h:comdef.h"         "ComDefSP.h:comdefsp.h"
        "ComUtil.h:comutil.h"
    )

    # Search llvm-mingw sysroot first; on MSYS2 clang64 headers are in ${LLVM_MINGW_DIR}/include.
    local mingw_inc="${LLVM_MINGW_DIR}/${MINGW_TRIPLE}/include"
    if [[ "${_HOST_OS}" == "windows" ]] && [[ ! -d "${mingw_inc}" ]]; then
        mingw_inc="${LLVM_MINGW_DIR}/include"
    fi
    local sys_mingw_inc="/usr/${MINGW_TRIPLE}/include"

    local created=0
    for pair in "${pairs[@]}"; do
        local upper="${pair%%:*}" lower="${pair##*:}"
        if [[ -f "${mingw_inc}/${lower}" ]] || [[ -f "${sys_mingw_inc}/${lower}" ]]; then
            printf '#include <%s>\n' "${lower}" > "${fixup_dir}/${upper}"
            (( created++ )) || true
        fi
    done

    success "Case fixup headers: ${created} wrappers in ${fixup_dir}"
}

# =============================================================================
# normalize_profraw_dirs — flatten default-<pid>.profraw/ directories into
# standalone .profraw files so later steps can glob "*.profraw" directly.
# =============================================================================
normalize_profraw_dirs() {
    local base_dir="$1"
    [[ -d "${base_dir}" ]] || return 0

    local entry
    while IFS= read -r -d '' entry; do
        [[ -d "${entry}" ]] || continue
        local dir_name="${entry##*/}"
        local prefix="${dir_name%.profraw}"
        local idx=0
        local file
        while IFS= read -r -d '' file; do
            [[ -f "${file}" ]] || continue
            local target_suffix=""
            [[ "${idx}" -gt 0 ]] && target_suffix="-${idx}"
            local target="${base_dir}/${prefix}${target_suffix}.profraw"
            while [[ -e "${target}" ]]; do
                idx=$((idx + 1))
                target_suffix="-${idx}"
                target="${base_dir}/${prefix}${target_suffix}.profraw"
            done
            mv "${file}" "${target}"
            idx=$((idx + 1))
        done < <(find "${entry}" -maxdepth 1 -type f -name '*.profraw' -print0)
        rm -rf "${entry}"
        info "Flattened profraw directory: ${dir_name}"
    done < <(find "${base_dir}" -maxdepth 1 -type d -name '*.profraw' -print0)
}

# =============================================================================

# =============================================================================
# detect_ffmpeg_version — read FFmpeg version from the submodule RELEASE file
# and set FFMPEG_VERSION + per-library soname vars (FFMPEG_AVCODEC_VER, etc.).
# Soname major numbers don't follow the package version; they're in a lookup
# table keyed by FFmpeg major. Add one entry per new major release.
# =============================================================================
detect_ffmpeg_version() {
    local release_file="${SOURCE_DIR}/externals/ffmpeg/ffmpeg/RELEASE"

    if [[ -f "${release_file}" ]]; then
        FFMPEG_VERSION="$(tr -d '[:space:]' < "${release_file}")"
    else
        # No submodule — fall back to CPM-pinned version (n8.0)
        FFMPEG_VERSION="8.0"
        info "[detect_ffmpeg_version] RELEASE file not found — using pinned version ${FFMPEG_VERSION}"
    fi

    [[ -n "${FFMPEG_VERSION}" ]] || error "[detect_ffmpeg_version] RELEASE file is empty: ${release_file}"

    local _major
    _major="$(echo "${FFMPEG_VERSION}" | cut -d. -f1)"

    # Soname lookup — update when a new FFmpeg major is released.
    case "${_major}" in
        8)
            FFMPEG_AVCODEC_VER=62
            FFMPEG_AVFORMAT_VER=62
            FFMPEG_AVFILTER_VER=11
            FFMPEG_AVUTIL_VER=60
            FFMPEG_SWSCALE_VER=9
            FFMPEG_SWRESAMPLE_VER=6
            ;;
        7)
            FFMPEG_AVCODEC_VER=61
            FFMPEG_AVFORMAT_VER=61
            FFMPEG_AVFILTER_VER=10
            FFMPEG_AVUTIL_VER=59
            FFMPEG_SWSCALE_VER=8
            FFMPEG_SWRESAMPLE_VER=5
            ;;
        6)
            FFMPEG_AVCODEC_VER=60
            FFMPEG_AVFORMAT_VER=60
            FFMPEG_AVFILTER_VER=9
            FFMPEG_AVUTIL_VER=58
            FFMPEG_SWSCALE_VER=7
            FFMPEG_SWRESAMPLE_VER=4
            ;;
        5)
            FFMPEG_AVCODEC_VER=59
            FFMPEG_AVFORMAT_VER=59
            FFMPEG_AVFILTER_VER=8
            FFMPEG_AVUTIL_VER=57
            FFMPEG_SWSCALE_VER=6
            FFMPEG_SWRESAMPLE_VER=4
            ;;
        *)
            error "[detect_ffmpeg_version] Unknown FFmpeg major version '${_major}' (from ${FFMPEG_VERSION}).
  Add a soname entry for this major version in detect_ffmpeg_version()."
            ;;
    esac

    info "[ffmpeg] Detected FFmpeg ${FFMPEG_VERSION} (avcodec-${FFMPEG_AVCODEC_VER}, avutil-${FFMPEG_AVUTIL_VER}, swscale-${FFMPEG_SWSCALE_VER})"
}



# =============================================================================
# Runtime DLL deployment
# =============================================================================
deploy_runtime_dlls() {
    local bin_dir="$1"
    # This step is now redundant as CMake (CopyMinGWDeps.cmake) handles 
    # synchronized, recursive DLL and plugin deployment during the build.
    success "All runtime DLLs deployed to ${bin_dir} (synchronized via CMake)"
}

print_profiling_instructions() {
    local binary="$1"
    local bin_dir="${binary%/*}"
    local unity_flag=""
    [[ "${UNITY_BUILD}" == "ON" ]] && unity_flag=" --unity"

    echo ""
    echo -e "${YELLOW}================================================================${RESET}"
    echo -e "${YELLOW}  NEXT STEP: Collect Profile Data on Windows (Session 1)${RESET}"
    echo -e "${YELLOW}================================================================${RESET}"
    echo ""
    echo -e "  ${BOLD}Instrumented binary :${RESET} ${binary}"
    echo -e "  ${BOLD}Profile output dir  :${RESET} ${PROFILE_DIR}/"
    echo ""
    echo "  1. Copy the entire bin/ folder to your Windows machine:"
    echo "       ${bin_dir}/"
    echo ""
    echo "  2. Run citron.exe directly (do NOT run from a terminal — the profraw"
    echo "     is written next to citron.exe on a clean exit, not to the terminal"
    echo "     working directory)."
    echo ""
    echo "  3. Play games / navigate menus for 15-30 minutes of representative"
    echo "     gameplay. Exit cleanly via File > Exit or Ctrl+Q (do NOT kill"
    echo "     the process — the profraw is only written on clean exit)."
    echo ""
    echo "  4. After exiting, look next to citron.exe for:"
    echo "       default-<pid>.profraw"
    echo ""
    echo -e "     ${BOLD}NOTE (IR PGO):${RESET} For IR PGO (-fprofile-generate), Clang writes a"
    echo "     DIRECTORY named  default-<pid>.profraw/  containing numbered chunk"
    echo "     files inside it — NOT a single flat file. Copy the entire directory."
    echo "     Copy it (and any others from the same run) here:"
    echo "       ${PROFILE_DIR}/"
    echo ""
    echo "  5. Build the optimized binary:"
    echo "       ./build-clangtron-windows.sh use --pgo-type ${PGO_MODE} --lto ${LTO_MODE}${unity_flag}"
    echo "     (auto-normalizes profraw directories, merges → default.profdata,"
    echo "      then builds citron.exe with -fprofile-use applied to compile + LTO link)"
    echo ""
    if [[ "${PGO_MODE}" == "ir" ]]; then
        echo "  Optional: add a CS-IRPGO layer (second Windows session, higher quality):"
        echo "       ./build-clangtron-windows.sh csgenerate --pgo-type ir --lto ${LTO_MODE}${unity_flag}"
        echo "     Run that binary on Windows → copy cs-default-*.profraw (or folder) to"
        echo "     ${PROFILE_DIR}/cs/ → re-run use."
        echo ""
    fi
    echo -e "${YELLOW}================================================================${RESET}"
    echo ""
}

# =============================================================================
# write_toolchain_file — CMake toolchain for llvm-mingw cross-compilation.
# llvm-mingw wrappers set --target, --sysroot, -stdlib=libc++, -rtlib=compiler-rt,
# -fuse-ld=lld automatically; no extra cross flags needed.
# =============================================================================
write_toolchain_file() {
    local path="$1"
    mkdir -p "$(dirname "$path")"

    local CMAKE_BUILD_ROOT="${BUILD_ROOT}"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        CMAKE_BUILD_ROOT="$(cygpath -m "${BUILD_ROOT}")"

    # MSYS2/Windows: native build — CMAKE_SYSTEM_NAME auto-detected.
    # Case-insensitive filesystem makes the case-fixup include dir unnecessary.
        cat > "$path" <<MSYS2_TC_EOF
# CMake toolchain: native Windows x64 with MSYS2 clang64
# Generated by build-clangtron-windows.sh — do not edit manually
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER   "${MINGW_CLANG}")
set(CMAKE_CXX_COMPILER "${MINGW_CLANGPP}")
set(CMAKE_RC_COMPILER  "windres.exe")
set(CMAKE_C_FLAGS_INIT   "-D__INTRINSIC_DEFINED___cpuidex -D__USE_MINGW_STAT64 -Wno-unknown-pragmas")
set(CMAKE_CXX_FLAGS_INIT "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -D__INTRINSIC_DEFINED___cpuidex -D__USE_MINGW_STAT64 -U__GLIBCXX__ -Wno-unknown-pragmas")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-fuse-ld=lld -Wl,--allow-multiple-definition")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld -Wl,--allow-multiple-definition")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld -Wl,--allow-multiple-definition")
set(CMAKE_CXX_STANDARD_LIBRARIES "${_COMSUPP_TC_PATH} -loleaut32")
set(CMAKE_AUTORCC_OPTIONS "--compress-algo;zlib")
MSYS2_TC_EOF
        return
    fi

    cat > "$path" <<EOF
# CMake toolchain: cross-compile Windows x86_64 with llvm-mingw
# Generated by build-clangtron-windows.sh — do not edit manually

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   "${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang++")
set(CMAKE_RC_COMPILER  "${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-windres")

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_DIR}/${MINGW_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# -D__INTRINSIC_DEFINED___cpuidex: prevents MinGW intrin-impl.h from defining __cpuidex
# with external linkage, eliminating duplicate-symbol errors from SDL2 and others.
set(CMAKE_C_FLAGS_INIT   "-D__INTRINSIC_DEFINED___cpuidex -D__USE_MINGW_STAT64 -isystem \"${CMAKE_BUILD_ROOT}/mingw-case-fixups\" -Wno-unknown-pragmas")
set(CMAKE_CXX_FLAGS_INIT "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -D__INTRINSIC_DEFINED___cpuidex -D__USE_MINGW_STAT64 -U__GLIBCXX__ -isystem \"${CMAKE_BUILD_ROOT}/mingw-case-fixups\" -Wno-unknown-pragmas")

# --allow-multiple-definition: residual __cpuidex duplicates from libSDL2.a
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-Wl,--allow-multiple-definition")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,--allow-multiple-definition")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-Wl,--allow-multiple-definition")

# comsupp_stubs.o: _com_util::ConvertStringToBSTR stub (not in MinGW)
# -loleaut32: COM/OLE Automation (SysAllocString etc.) for WMI code
set(CMAKE_CXX_STANDARD_LIBRARIES "${_COMSUPP_TC_PATH} -loleaut32")

# Use zlib resource compression — aqt's llvm_mingw Qt6Core lacks zstd support
set(CMAKE_AUTORCC_OPTIONS "--compress-algo;zlib")
EOF
}

# =============================================================================
# build_common_cmake_args — populate _CMAKE_ARGS with flags shared by all stages.
# Callers append stage-specific flags, then pass the array to cmake.
# Using an array avoids word-splitting on paths with spaces.
# =============================================================================
build_common_cmake_args() {
    local lto_flag; lto_flag="$(lto_cmake_flag)"
    local toolchain_file="${BUILD_ROOT}/mingw-clang-toolchain.cmake"
    write_toolchain_file "$toolchain_file"

    local CMAKE_BUILD_ROOT="${BUILD_ROOT}"
    local CMAKE_SOURCE_DIR="${SOURCE_DIR}"
    local CMAKE_SPIRV_HEADERS_INSTALL="${SPIRV_HEADERS_INSTALL}"
    local CMAKE_VULKAN_HEADERS_INSTALL="${VULKAN_HEADERS_INSTALL}"
    local CMAKE_TOOLCHAIN_FILE_PATH="${toolchain_file}"

    if [[ "${_HOST_OS}" == "windows" ]]; then
        CMAKE_BUILD_ROOT="$(cygpath -m "${BUILD_ROOT}")"
        CMAKE_SOURCE_DIR="$(cygpath -m "${SOURCE_DIR}")"
        CMAKE_SPIRV_HEADERS_INSTALL="$(cygpath -m "${SPIRV_HEADERS_INSTALL}")"
        CMAKE_VULKAN_HEADERS_INSTALL="$(cygpath -m "${VULKAN_HEADERS_INSTALL}")"
        CMAKE_TOOLCHAIN_FILE_PATH="$(cygpath -m "${toolchain_file}")"
    fi

    local CMAKE_CPM_CACHE="${CPM_SOURCE_CACHE}"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        CMAKE_CPM_CACHE="$(cygpath -m "${CMAKE_CPM_CACHE}")"
    fi

    # Populate the global array — one element per cmake arg (spaces in paths handled correctly).
    _CMAKE_ARGS=(
        "-G" "Ninja"
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE_PATH}"
        "-DCMAKE_DISABLE_FIND_PACKAGE_LLVM=ON"
        "-DCITRON_ENABLE_LTO=${lto_flag}"
        "-DBUILD_TESTING=OFF"
        "-DCITRON_TESTS=OFF"
        "-DCITRON_USE_BUNDLED_FFMPEG=ON"
        "-DCITRON_CLANGTRON=ON"
        "-DCITRON_USE_EXTERNAL_SDL2=ON"
        "-DCITRON_USE_EXTERNAL_VULKAN_HEADERS=ON"
        "-DCITRON_USE_EXTERNAL_VULKAN_UTILITY_LIBRARIES=ON"
        "-DSPIRV-Headers_DIR=${CMAKE_SPIRV_HEADERS_INSTALL}/share/cmake/SPIRV-Headers"
        "-DVulkanHeaders_DIR=${CMAKE_VULKAN_HEADERS_INSTALL}/share/cmake/VulkanHeaders"
        "-DCMAKE_PREFIX_PATH=${CMAKE_VULKAN_HEADERS_INSTALL};${CMAKE_SPIRV_HEADERS_INSTALL}"
        "-DVulkanMemoryAllocator_FOUND=TRUE"
        "-Ddynarmic_FOUND=TRUE"
        "-Dxbyak_FOUND=TRUE"
        "-Dcubeb_FOUND=TRUE"
        "-DENABLE_LIBUSB=ON"
        "-DCITRON_USE_PRECOMPILED_HEADERS=OFF"
        "-DCITRON_USE_CPM=ON"
        "-DCITRON_CHECK_SUBMODULES=OFF"
        "-DCPM_SOURCE_CACHE=${CMAKE_CPM_CACHE}"
        "-Wno-dev"
    )
    [[ -n "${GLSLC_PATH:-}" ]] && _CMAKE_ARGS+=(
        "-DVulkan_GLSLC_EXECUTABLE=${GLSLC_PATH}"
        "-DVulkan_GLSLANG_VALIDATOR_EXECUTABLE=${GLSLC_PATH}"
    )
    [[ -n "${CITRON_BUILD_TYPE:-}" ]] && _CMAKE_ARGS+=("-DCITRON_BUILD_TYPE=${CITRON_BUILD_TYPE}")
    [[ "${UNITY_BUILD}" == "ON" ]] && _CMAKE_ARGS+=("-DENABLE_UNITY_BUILD=ON")
    [[ -n "${MARCH_NATIVE:-}" ]] && _CMAKE_ARGS+=(
        "-DCMAKE_C_FLAGS=${MARCH_NATIVE}"
        "-DCMAKE_CXX_FLAGS=${MARCH_NATIVE}"
    )
    _CMAKE_ARGS+=("-DCITRON_ENABLE_TRACY=${TRACY_BUILD}")
    _CMAKE_ARGS+=("-DCITRON_ENABLE_TRACY_ALLOC=${TRACY_ALLOC_BUILD}")
    # Always return 0 — a trailing [[ ]] that evaluates false would trigger set -e in caller.
    :
}

# =============================================================================
# Stage 1: generate
# =============================================================================
stage_generate() {
    header "Stage 1: PGO Instrumented Build"

    check_tool "${CLANG}"; check_tool "${CLANGPP}"
    check_tool "ninja";    check_tool "cmake"
    [[ -d "$SOURCE_DIR" ]] \
        || error "Source directory not found: ${SOURCE_DIR}\nClone citron first or use --source."

    require_llvm_mingw
    mkdir -p "${BUILD_GENERATE}" "${PROFILE_DIR}"

    local lto_generate_flag=""
    local generate_lto_cmake="OFF"
    case "${LTO_MODE}" in
        full)
            lto_generate_flag="-flto"
            generate_lto_cmake="ON"
            info "Generate stage: Full LTO enabled."
            ;;
        thin)
            lto_generate_flag="-flto=thin"
            generate_lto_cmake="ON"
            info "Generate stage: ThinLTO enabled."
            ;;
        none)
            info "Generate stage: LTO disabled."
            ;;
    esac

    # IR PGO: counters at LLVM IR level (-fprofile-generate).
    # FE PGO: counters at AST level (-fprofile-instr-generate).
    # Both write default-%p.profraw (%p=PID) next to the binary.
    # -O3 here must match the use stage — IR PGO hashes are post-optimization,
    # so a level mismatch causes widespread hash mismatches and discarded profile data.
    local pgo_gen_flag
    if [[ "${PGO_MODE}" == "ir" ]]; then
        pgo_gen_flag="-fprofile-generate=default-%p.profraw"
    else
        pgo_gen_flag="-fprofile-instr-generate=default-%p.profraw"
    fi
    local debug_flag=""
    # -gcodeview: see stage_use for why plain -g isn't enough on this MinGW target.
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g -gcodeview"
    local c_flags="-O3 -DNDEBUG ${debug_flag} ${pgo_gen_flag}${lto_generate_flag:+ ${lto_generate_flag}}"
    local cxx_flags="${c_flags}"

    # Force-keep profile runtime symbols — lld dead-strips them without this.
    local linker_debug_flag=""
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && linker_debug_flag="-Wl,--pdb= -Wl,--threads=1"
    local extra_link_flags="-Wl,-u,__llvm_profile_write_file,-u,__llvm_profile_runtime"

    # Qt via aqt
    # Qt via aqt - use global cache
    local qt_base_dir="${CPM_SOURCE_CACHE}/qt-bin"
    local qt_install_dir="${qt_base_dir}/6.9.3/llvm-mingw_64"
    local qt_host_dir="${BUILD_GENERATE}/externals/qt-host/6.9.3/gcc_64"
    if [[ "${_HOST_OS}" != "windows" ]]; then
        qt_host_dir="${CPM_SOURCE_CACHE}/qt-bin-host/6.9.3/gcc_64"
    fi
    local qt6_cmake_dir="${qt_install_dir}/lib/cmake/Qt6"

    ensure_aqt
    local aqt_bin
    aqt_bin="$(command -v aqt 2>/dev/null || echo "${HOME}/.local/bin/aqt")"

    if [[ ! -f "${qt_install_dir}/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
        info "Downloading Qt 6.9.3 Windows/MinGW target (base + multimedia) via aqt..."
        mkdir -p "${qt_base_dir}"
        "${aqt_bin}" install-qt windows desktop 6.9.3 win64_llvm_mingw \
            --outputdir "${qt_base_dir}" \
            --modules qtmultimedia qtimageformats \
            || error "Qt download failed."
    fi

    # Qt host tools only needed on Linux for cross-compilation.
    # On Windows, target Qt IS host Qt — do NOT pass QT_HOST_PATH (triggers cross-compile mode).
    # Linux Qt package has Unix symlinks Windows can't create without Developer Mode.
    if [[ "${_HOST_OS}" != "windows" ]]; then
        if [[ ! -f "${qt_host_dir}/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
            local _host_outdir="${CPM_SOURCE_CACHE}/qt-bin-host"
            mkdir -p "${_host_outdir}"
            "${aqt_bin}" install-qt linux desktop 6.9.3 linux_gcc_64 \
                --outputdir "${_host_outdir}" \
                --modules qtsvg qtmultimedia \
                || warn "aqt Qt 6.9.3 linux download failed"
        fi
    else
        info "Windows native build: skipping Linux host Qt (not needed)."
        qt_host_dir=""
    fi

    info "Qt6 cmake dir: ${qt6_cmake_dir}"

    ensure_profile_runtime_mingw
    compile_comsupp_stubs
    setup_case_fixup_headers

    GLSLC_PATH="$(command -v glslc 2>/dev/null || true)"
    if [[ -n "${GLSLC_PATH}" ]]; then
        info "Found glslc: ${GLSLC_PATH}"
    else
        GLSLC_PATH="$(command -v glslangValidator 2>/dev/null || true)"
        [[ -n "${GLSLC_PATH}" ]] \
            && info "Using glslangValidator: ${GLSLC_PATH}" \
            || warn "No Vulkan shader compiler found — install glslang-tools"
    fi

    # Pre-build FFmpeg before cmake configure so download_bundled_external() finds
    # it already present and skips the yuzu-mirror download (lacks FFmpeg >= 7.x).
    detect_ffmpeg_version

    info "Configuring CMake (instrumented build)..."
    cd "${BUILD_GENERATE}"
    rm -f CMakeCache.txt; rm -rf CMakeFiles
    [[ -d "src/citron/citron_autogen" ]] && rm -rf src/citron/citron_autogen

    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
    build_common_cmake_args
    [[ -n "${qt6_cmake_dir}" ]] && _CMAKE_ARGS+=("-DQt6_DIR=${qt6_cmake_dir}")
    [[ -n "${qt_host_dir}"   ]] && _CMAKE_ARGS+=("-DQT_HOST_PATH=${qt_host_dir}")
    _CMAKE_ARGS+=(
        "-DCITRON_ENABLE_PGO_GENERATE=ON"
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON"
        "-DCITRON_ENABLE_LTO=${generate_lto_cmake}"
        "-DCMAKE_C_FLAGS_${bt_upper}=${c_flags}"
        "-DCMAKE_CXX_FLAGS_${bt_upper}=${cxx_flags}"
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${c_flags} ${PROFILE_RUNTIME_LIB:+${PROFILE_RUNTIME_LIB}} ${extra_link_flags} ${linker_debug_flag}"
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}"
    )
    cmake "${SOURCE_DIR}" "${_CMAKE_ARGS[@]}" \
        || error "CMake configure failed"
    info "Building instrumented citron (${BUILD_TYPE})..."
    cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}"

    success "Instrumented build complete: ${BUILD_GENERATE}/bin/citron.exe"

    # Verify profile runtime symbols are present — if stripped, binary runs but writes no .profraw.
    local citron_exe="${BUILD_GENERATE}/bin/citron.exe"
    local nm_tool
    nm_tool="$(command -v "llvm-nm-${CLANG_VERSION}" 2>/dev/null                || command -v llvm-nm 2>/dev/null                || command -v nm 2>/dev/null || true)"

    if [[ -n "${nm_tool}" && -f "${citron_exe}" ]]; then
        local nm_out
        nm_out=$("${nm_tool}" --defined-only "${citron_exe}" 2>/dev/null || true)

        local has_raw_version has_runtime has_write_file
        has_raw_version=$(echo "${nm_out}" | grep -c '__llvm_profile_raw_version' || true)
        has_runtime=$(echo     "${nm_out}" | grep -c '__llvm_profile_runtime'     || true)
        has_write_file=$(echo  "${nm_out}" | grep -c '__llvm_profile_write_file'  || true)
        if [[ "${has_raw_version}" -gt 0 && "${has_runtime}" -gt 0 && "${has_write_file}" -gt 0 ]]; then
            success "Instrumentation check: OK"
            success "  __llvm_profile_raw_version  ✓"
            success "  __llvm_profile_runtime      ✓"
            success "  __llvm_profile_write_file   ✓"
        else
            echo ""
            error_no_exit() { echo -e "${RED}[ERROR]${RESET} $*" >&2; }
            warn "════════════════════════════════════════════════════════════════"
            warn "  INSTRUMENTATION CHECK FAILED — binary will NOT produce profraw"
            warn "════════════════════════════════════════════════════════════════"
            warn "  __llvm_profile_raw_version  $([ "${has_raw_version}" -gt 0 ] && echo ✓ || echo ✗)"
            warn "  __llvm_profile_runtime      $([ "${has_runtime}"     -gt 0 ] && echo ✓ || echo ✗)"
            warn "  __llvm_profile_write_file   $([ "${has_write_file}"  -gt 0 ] && echo ✓ || echo '✗  ← flush function stripped by linker')"
            warn ""
            warn "  Likely causes:"
            warn "    1. Profile runtime not linked: PROFILE_RUNTIME_LIB=${PROFILE_RUNTIME_LIB:-<unset>}"
            warn "    2. PGO generate flag not passed to the linker (PGO_MODE=${PGO_MODE})"
            warn "    3. citron cmake config overrode CMAKE_EXE_LINKER_FLAGS_${bt_upper}"
            warn ""
            warn "  The binary will still run, but no .profraw will be written."
            warn "  Re-run the generate stage or check the cmake flags above."
            warn "════════════════════════════════════════════════════════════════"
            echo ""
        fi
    else
        warn "Instrumentation check skipped: nm tool or citron.exe not found"
    fi

    deploy_runtime_dlls \
        "${BUILD_GENERATE}/bin" \
        "${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64" \
        "${BUILD_GENERATE}"

    # Write sentinel so stage_use/stage_csgenerate can verify LTO+PGO match.
    printf "LTO=%s\nPGO=%s\n" "${LTO_MODE}" "${PGO_MODE}" \
        > "${BUILD_ROOT}/.citron-gen-config"

    print_profiling_instructions "${BUILD_GENERATE}/bin/citron.exe"
}

# =============================================================================
# Stage 1b: csgenerate — Context-Sensitive IR PGO instrumented build.
#
# Layers CS instrumentation on a binary already optimized with stage1 IR PGO.
# Collects per-call-site counts for better inlining of shared hot/cold paths.
#
# Requirements:
#   - --pgo-type ir (CS-IRPGO is IR-only)
#   - --lto/--pgo-type must match the prior generate run (sentinel-enforced)
#   - default.profdata must exist (from 'use' or manual merge of stage1 profraw)
#     merged.profdata is NOT accepted — see CRITICAL INVARIANT in header
#
# CS binary writes cs-default-<pid>.profraw next to itself on exit.
# Copy to pgo-profiles/cs/, then re-run 'use' which auto-merges them.
# =============================================================================
stage_csgenerate() {
    header "Stage 1b: CS-IRPGO Instrumented Build"

    # CS-IRPGO is only valid with IR PGO — it layers a CS pass on IR counters.
    if [[ "${PGO_MODE}" != "ir" ]]; then
        error "csgenerate requires --pgo-type ir.\n" \
              "       Context-Sensitive PGO is not available for frontend PGO (fe).\n" \
              "       Re-run with: ./build-clangtron-windows.sh csgenerate --pgo-type ir --lto ${LTO_MODE}"
    fi

    check_tool "${CLANG}"; check_tool "${CLANGPP}"
    check_tool "ninja";    check_tool "cmake"
    [[ -d "$SOURCE_DIR" ]] \
        || error "Source directory not found: ${SOURCE_DIR}\nClone citron first or use --source."

    require_llvm_mingw

    # ── Sentinel check: LTO and PGO must match the prior generate run ─────────
    local _gen_cfg="${BUILD_ROOT}/.citron-gen-config"
    if [[ -f "${_gen_cfg}" ]]; then
        local _gen_lto _gen_pgo
        _gen_lto=$(awk -F= '/^LTO=/{print $2; exit}' "${_gen_cfg}" 2>/dev/null || true)
        _gen_pgo=$(awk -F= '/^PGO=/{print $2; exit}' "${_gen_cfg}" 2>/dev/null || true)
        if [[ -n "${_gen_lto}" && "${_gen_lto}" != "${LTO_MODE}" ]]; then
            error "LTO mismatch: generate used LTO=${_gen_lto}, csgenerate has LTO=${LTO_MODE}.\n"\
                  "       IR PGO profiles are tied to the IR produced at generate time.\n"\
                  "       Re-run csgenerate with --lto ${_gen_lto}."
        fi
        if [[ -n "${_gen_pgo}" && "${_gen_pgo}" != "${PGO_MODE}" ]]; then
            error "PGO mode mismatch: generate used PGO=${_gen_pgo}, csgenerate has PGO=${PGO_MODE}.\n"\
                  "       Re-run csgenerate with --pgo-type ${_gen_pgo}."
        fi
    else
        # The sentinel is written by stage_generate and records the LTO+PGO mode
        # that produced the IR which the stage1 profdata is keyed to.  Without it
        # we cannot verify that csgenerate is building on a compatible baseline —
        # a mismatch silently produces a CS binary whose counters are keyed to a
        # different IR shape, making the resulting profraw unloadable in stage_use.
        # bench.sh copies the sentinel from the IR config dir before invoking
        # csgenerate; if it is still absent something went wrong in that copy step.
        error "Generate sentinel not found at ${_gen_cfg}.\n" \
              "       This file is written by stage_generate and records the LTO+PGO\n" \
              "       mode used to produce the stage1 profdata.  Without it, csgenerate\n" \
              "       cannot verify IR compatibility and may produce an unusable CS binary.\n" \
              "       If running via bench.sh, re-run build-generate for the matching IR\n" \
              "       config first.  If running build-clangtron-windows.sh directly, run generate\n" \
              "       before csgenerate, or manually create the sentinel:\n" \
              "         printf 'LTO=${LTO_MODE}\\nPGO=ir\\n' > ${_gen_cfg}"
    fi

    # CRITICAL: use ONLY default.profdata (plain stage1), never merged.profdata.
    # merged.profdata has CS records keyed to the previous csgenerate IR; feeding
    # those through -fprofile-use changes inlining relative to the stage1 baseline,
    # causing hash mismatches at the use stage. See header for full explanation.
    local stage1_pd="${PROFILE_DIR}/default.profdata"

    if [[ ! -f "${stage1_pd}" ]]; then
        # Try building default.profdata from profraw files.
        # normalize_profraw_dirs first: IR PGO writes profraw directories, not flat files.
        normalize_profraw_dirs "${PROFILE_DIR}"
        local profraw_count
        profraw_count=$(find "${PROFILE_DIR}" -maxdepth 1 -name "*.profraw" 2>/dev/null | wc -l)
        if [[ "${profraw_count}" -gt 0 ]]; then
            info "Merging ${profraw_count} stage1 .profraw file(s) → default.profdata..."
            "${LLVM_PROFDATA}" merge --sparse \
                --output="${stage1_pd}" "${PROFILE_DIR}"/*.profraw
            success "Stage1 profdata merged: ${stage1_pd}"
        else
            local merged_pd="${PROFILE_DIR}/merged.profdata"
            if [[ -f "${merged_pd}" ]]; then
                error "default.profdata not found, but merged.profdata exists.\n" \
                      "       merged.profdata contains CS records from a previous cycle and\n" \
                      "       cannot be used as the stage1 base for csgenerate (see script header).\n" \
                      "       To rebuild default.profdata:\n" \
                      "         1. Copy the original stage1 default-<pid>.profraw files to\n" \
                      "            ${PROFILE_DIR}/\n" \
                      "         2. Re-run: ./build-clangtron-windows.sh use --pgo-type ir --lto ${LTO_MODE}\n" \
                      "            (this produces default.profdata from the stage1 profraw)"
            else
                error "No stage1 profdata or profraw found in ${PROFILE_DIR}/\n" \
                      "       Run generate, collect default-<pid>.profraw on Windows,\n" \
                      "       copy to ${PROFILE_DIR}/, then run:\n" \
                      "         ./build-clangtron-windows.sh use --pgo-type ir --lto ${LTO_MODE}\n" \
                      "       (produces default.profdata), then re-run csgenerate."
            fi
        fi
    fi
    info "Stage1 profdata (plain IR, no CS): ${stage1_pd}"

    mkdir -p "${BUILD_CSGENERATE}" "${PROFILE_DIR}/cs"

    local lto_generate_flag=""
    local generate_lto_cmake="OFF"
    case "${LTO_MODE}" in
        full) lto_generate_flag="-flto";       generate_lto_cmake="ON"
              info "csgenerate: Full LTO" ;;
        thin) lto_generate_flag="-flto=thin";  generate_lto_cmake="ON"
              info "csgenerate: ThinLTO" ;;
        none) info "csgenerate: LTO disabled" ;;
    esac

    # CS compile flags: -fprofile-use applies stage1 optimizations, -fcs-profile-generate
    # layers CS counters on the optimized IR. cs-default-%p.profraw uses %p (PID) so
    # parallel runs don't collide; relative path writes next to .exe on Windows.
    local stage1_pd_compiler="${stage1_pd}"
    [[ "${_HOST_OS}" == "windows" ]] && stage1_pd_compiler="$(cygpath -m "${stage1_pd}")"
    local cs_gen_flag="-fcs-profile-generate=cs-default-%p.profraw"
    local pgo_use_flag="-fprofile-use=\"${stage1_pd_compiler}\""
    local debug_flag=""
    # -gcodeview: see stage_use for why plain -g isn't enough on this MinGW target.
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g -gcodeview"
    local c_flags="-O3 -DNDEBUG ${debug_flag} ${pgo_use_flag} ${cs_gen_flag}${lto_generate_flag:+ ${lto_generate_flag}}"
    local cxx_flags="${c_flags}"
    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')

    # Force-keep profile runtime; same rationale as stage_generate.
    # --pdb= / --threads=1: RelWithDebInfo only — PDB auto-naming + LLD COFF
    # deadlock prevention (PDB merge threads vs LTO backend threads).
    local linker_debug_flag=""
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && linker_debug_flag="-Wl,--pdb= -Wl,--threads=1"
    local extra_link_flags="-Wl,-u,__llvm_profile_write_file,-u,__llvm_profile_runtime"

    ensure_profile_runtime_mingw
    local qt_install_dir="${CPM_SOURCE_CACHE}/qt-bin/6.9.3/llvm-mingw_64"
    local qt_host_dir="${CPM_SOURCE_CACHE}/qt-bin-host/6.9.3/gcc_64"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        qt_host_dir=""
    fi
    local qt6_cmake_dir="${qt_install_dir}/lib/cmake/Qt6"

    GLSLC_PATH="$(command -v glslc 2>/dev/null || true)"
    [[ -z "${GLSLC_PATH}" ]] && GLSLC_PATH="$(command -v glslangValidator 2>/dev/null || true)"

    # Pre-build FFmpeg (fast no-op if already built via sentinel check).
    detect_ffmpeg_version

    info "Configuring CMake (CS-IRPGO instrumented build)..."
    cd "${BUILD_CSGENERATE}"
    rm -f CMakeCache.txt; rm -rf CMakeFiles
    [[ -d "src/citron/citron_autogen" ]] && rm -rf src/citron/citron_autogen

    # shellcheck disable=SC2034  # _CMAKE_ARGS used via array expansion below
    build_common_cmake_args
    [[ -n "${qt6_cmake_dir}" ]] && _CMAKE_ARGS+=("-DQt6_DIR=${qt6_cmake_dir}")
    [[ -n "${qt_host_dir}"   ]] && _CMAKE_ARGS+=("-DQT_HOST_PATH=${qt_host_dir}")
    _CMAKE_ARGS+=(
        "-DCITRON_ENABLE_PGO_GENERATE=ON"
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON"
        "-DCITRON_ENABLE_LTO=${generate_lto_cmake}"
        "-DCMAKE_C_FLAGS_${bt_upper}=${c_flags}"
        "-DCMAKE_CXX_FLAGS_${bt_upper}=${cxx_flags}"
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${c_flags} ${PROFILE_RUNTIME_LIB:+${PROFILE_RUNTIME_LIB}} ${extra_link_flags} ${linker_debug_flag}"
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}"
    )
    cmake "${SOURCE_DIR}" "${_CMAKE_ARGS[@]}" \
        || error "CMake configure failed"

    info "Building CS-IRPGO instrumented citron (${BUILD_TYPE})..."
    cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}"

    success "CS-IRPGO instrumented build complete: ${BUILD_CSGENERATE}/bin/citron.exe"

    # ── Verify CS instrumentation symbols are present ─────────────────────────
    # The CS binary must have the same profile runtime symbols as a standard
    # generate binary. If any are missing lld dead-stripped them and the binary
    # will run but produce no .profraw.
    local citron_exe="${BUILD_CSGENERATE}/bin/citron.exe"
    local nm_tool
    nm_tool="$(command -v "llvm-nm-${CLANG_VERSION}" 2>/dev/null \
               || command -v llvm-nm 2>/dev/null \
               || command -v nm 2>/dev/null || true)"

    if [[ -n "${nm_tool}" && -f "${citron_exe}" ]]; then
        local nm_out
        nm_out=$("${nm_tool}" --defined-only "${citron_exe}" 2>/dev/null || true)
        local has_raw_version has_runtime has_write_file
        has_raw_version=$(echo "${nm_out}" | grep -c '__llvm_profile_raw_version' || true)
        has_runtime=$(echo     "${nm_out}" | grep -c '__llvm_profile_runtime'     || true)
        has_write_file=$(echo  "${nm_out}" | grep -c '__llvm_profile_write_file'  || true)

        if [[ "${has_raw_version}" -gt 0 && "${has_runtime}" -gt 0 && "${has_write_file}" -gt 0 ]]; then
            success "CS instrumentation check: OK"
        else
            warn "════════════════════════════════════════════════════════════════"
            warn "  CS INSTRUMENTATION CHECK FAILED — binary will NOT produce profraw"
            warn "════════════════════════════════════════════════════════════════"
            warn "  __llvm_profile_raw_version  $([ "${has_raw_version}" -gt 0 ] && echo ✓ || echo ✗)"
            warn "  __llvm_profile_runtime      $([ "${has_runtime}"     -gt 0 ] && echo ✓ || echo ✗)"
            warn "  __llvm_profile_write_file   $([ "${has_write_file}"  -gt 0 ] && echo ✓ || echo '✗  ← stripped by linker')"
            warn "  The binary will run but produce no cs-default-*.profraw."
            warn "════════════════════════════════════════════════════════════════"
        fi
    fi

    deploy_runtime_dlls \
        "${BUILD_CSGENERATE}/bin" \
        "${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64" \
        "${BUILD_CSGENERATE}"

    local unity_flag=""
    [[ "${UNITY_BUILD}" == "ON" ]] && unity_flag=" --unity"

    echo ""
    echo -e "${YELLOW}================================================================${RESET}"
    echo -e "${YELLOW}  NEXT STEP: Collect CS Profile Data on Windows (Session 2)${RESET}"
    echo -e "${YELLOW}================================================================${RESET}"
    echo ""
    echo -e "  ${BOLD}CS binary    :${RESET} ${citron_exe}"
    echo -e "  ${BOLD}CS profdata  :${RESET} ${stage1_pd}  (stage1 base, correct)"
    echo -e "  ${BOLD}CS output dir:${RESET} ${PROFILE_DIR}/cs/"
    echo ""
    echo "  1. Copy the entire bin/ folder to your Windows machine:"
    echo "       ${BUILD_CSGENERATE}/bin/"
    echo ""
    echo "  2. Run citron.exe directly (do NOT run from a terminal — the profraw"
    echo "     is written next to citron.exe on a clean exit, not to the terminal"
    echo "     working directory)."
    echo ""
    echo "  3. Play through the same games / scenarios as session 1."
    echo "     Aim for 15-30 minutes of representative gameplay."
    echo "     Exit cleanly via File > Exit or Ctrl+Q (do NOT kill the process)."
    echo ""
    echo "  4. After exiting, look next to citron.exe for:"
    echo "       cs-default-<pid>.profraw"
    echo ""
    echo -e "     ${BOLD}NOTE (IR PGO):${RESET} For IR PGO (-fcs-profile-generate), Clang writes a"
    echo "     DIRECTORY named  cs-default-<pid>.profraw/  containing numbered"
    echo "     chunk files inside — NOT a single flat file. Copy the entire"
    echo "     directory. Copy it (and any others from the same run) here:"
    echo "       ${PROFILE_DIR}/cs/"
    echo ""
    echo "  5. Re-run use to merge stage1 + CS and rebuild the PE:"
    echo "       ./build-clangtron-windows.sh use --pgo-type ir --lto ${LTO_MODE}${unity_flag}"
    echo ""
    echo "     The use stage will:"
    echo "       a) Normalize and merge cs-default-*.profraw → cs-only.profdata"
    echo "       b) Merge default.profdata + cs-only.profdata → merged.profdata"
    echo "       c) Rebuild citron.exe with -fprofile-use=merged.profdata"
    echo "          (applied to both compile and LTO link steps)"
    echo ""
    echo -e "${YELLOW}================================================================${RESET}"
    echo ""
}

# =============================================================================
# Stage 2: use
# =============================================================================
stage_use() {
    # --pgo-type none: plain Release build (no PGO, LTO controlled by --lto).
    # Outputs to build/use-nopgo/ so it never collides with a real PGO use build.
    if [[ "${PGO_MODE}" == "none" ]]; then
        header "Stage 2: Release Build (no PGO, LTO=${LTO_MODE})"

        check_tool "${CLANG}"; check_tool "${CLANGPP}"
        check_tool "ninja";    check_tool "cmake"
        [[ -d "$SOURCE_DIR" ]] \
            || error "Source directory not found: ${SOURCE_DIR}\nClone citron first or use --source."

        require_llvm_mingw

        local nopgo_dir="${BUILD_ROOT}/use-nopgo"
        mkdir -p "${nopgo_dir}"
        compile_comsupp_stubs
        setup_case_fixup_headers

        # ── Qt path detection ─────────────────────────────────────────────────
        # Search order: (1) generate's cached Qt (correct llvm-mingw variant),
        # (2) a prior nopgo run, (3) aqt download into nopgo's own externals.
        # Using find avoids hardcoding the Qt version and works after source upgrades.
        _nopgo_find_qt_target() {
            local root="$1"
            # Search both root/externals/qt (local build) and root/ (global cache)
            local search_paths=("${root}/externals/qt" "${root}")
            local hit=""
            for spath in "${search_paths[@]}"; do
                [[ -d "${spath}" ]] || continue
                # Prefer llvm-mingw_64 variant
                hit=$(find "${spath}" -maxdepth 6 \
                    -name "Qt6Config.cmake" -path "*/llvm-mingw_64/*" 2>/dev/null | head -1)
                [[ -z "${hit}" ]] && \
                    hit=$(find "${spath}" -maxdepth 6 \
                        -name "Qt6Config.cmake" 2>/dev/null | head -1)
                [[ -n "${hit}" ]] && break
            done
            [[ -n "${hit}" ]] && dirname "${hit}" || true
        }
        _nopgo_find_qt_host() {
            local root="$1"
            local search_paths=("${root}/externals/qt-host" "${root}/externals/qt" "${root}")
            local hit=""
            for spath in "${search_paths[@]}"; do
                [[ -d "${spath}" ]] || continue
                hit=$(find "${spath}" -maxdepth 6 \
                    -name "Qt6Config.cmake" -path "*/gcc_64/*" 2>/dev/null | head -1)
                [[ -n "${hit}" ]] && break
            done
            # QT_HOST_PATH must be the install root (.../gcc_64), not the cmake subdir.
            # Walk up 3 levels from .../gcc_64/lib/cmake/Qt6/Qt6Config.cmake → .../gcc_64
            [[ -n "${hit}" ]] && dirname "$(dirname "$(dirname "$(dirname "${hit}")")")" || true
        }

        local qt6_cmake_dir="" qt_host_dir=""
        qt6_cmake_dir="$(_nopgo_find_qt_target "${BUILD_GENERATE}" 2>/dev/null || true)"
        [[ -z "${qt6_cmake_dir}" ]] && \
            qt6_cmake_dir="$(_nopgo_find_qt_target "${nopgo_dir}" 2>/dev/null || true)"

        qt_host_dir="$(_nopgo_find_qt_host "${BUILD_GENERATE}" 2>/dev/null || true)"
        [[ -z "${qt_host_dir}" ]] && \
            qt_host_dir="$(_nopgo_find_qt_host "${nopgo_dir}" 2>/dev/null || true)"

        # Check global cache for host Qt
        if [[ -z "${qt_host_dir}" ]]; then
            local _global_host_base="${CPM_SOURCE_CACHE}/qt-bin-host"
            if [[ -d "${_global_host_base}" ]]; then
                qt_host_dir="$(_nopgo_find_qt_host "${_global_host_base}" 2>/dev/null || true)"
            fi
        fi

        # If neither cache has Qt, download via aqt (avoids CMakeLists.txt pulling wrong MinGW variant).
        local _nopgo_qt_base="${CPM_SOURCE_CACHE}/qt-bin"

        if [[ -z "${qt6_cmake_dir}" ]]; then
            # Re-check global path if CMake dir wasn't found in build logs
            local _global_target="${_nopgo_qt_base}/6.9.3/llvm-mingw_64/lib/cmake/Qt6"
            if [[ -f "${_global_target}/Qt6Config.cmake" ]]; then
                qt6_cmake_dir="${_global_target}"
            fi
        fi

        if [[ -z "${qt6_cmake_dir}" ]]; then
            warn "No cached Qt found in generate or prior nopgo build."
            warn "Downloading Qt (base + multimedia) via aqt into ${_nopgo_qt_base} ..."
            ensure_aqt
            local _aqt; _aqt="$(command -v aqt 2>/dev/null || echo "${HOME}/.local/bin/aqt")"
            mkdir -p "${_nopgo_qt_base}"
            "${_aqt}" install-qt windows desktop 6.9.3 win64_llvm_mingw \
                --outputdir "${_nopgo_qt_base}" \
                --modules qtmultimedia qtimageformats \
                || error "Qt download failed.\n" \
                         "       Run generate first to cache Qt, then re-run:\n" \
                         "         ./build-clangtron-windows.sh use --pgo none --lto ${LTO_MODE}"
            qt6_cmake_dir="$(_nopgo_find_qt_target "${nopgo_dir}")"
            [[ -z "${qt6_cmake_dir}" ]] && qt6_cmake_dir="$(_nopgo_find_qt_target "${_nopgo_qt_base}")"
            [[ -z "${qt6_cmake_dir}" ]] && \
                error "Qt downloaded but Qt6Config.cmake not found — check aqt output above."
        fi



        if [[ -z "${qt_host_dir}" ]]; then
            if [[ "${_HOST_OS}" == "windows" ]]; then
                # Native build uses target Qt tools automatically (rcc, uic, moc)
                # Setting QT_HOST_PATH breaks native builds by triggering cross-compilation mode
                qt_host_dir=""
            else
                ensure_aqt
                local _aqt; _aqt="$(command -v aqt 2>/dev/null || echo "${HOME}/.local/bin/aqt")"
                # Extract Qt version from the target cmake dir path
                # (.../qt/<ver>/<variant>/lib/cmake/Qt6 → 3 dirname calls → version dir → basename)
                local _qt_variant_dir
                _qt_variant_dir="$(dirname "$(dirname "$(dirname "${qt6_cmake_dir}")")")"
                local _qt_ver
                _qt_ver="$(basename "$(dirname "${_qt_variant_dir}")")"
                mkdir -p "${nopgo_dir}/externals/qt-host"
                "${_aqt}" install-qt linux desktop "${_qt_ver}" linux_gcc_64 \
                    --outputdir "${nopgo_dir}/externals/qt-host" \
                    || warn "Qt host tools download failed — build may still succeed without it."
                qt_host_dir="$(_nopgo_find_qt_host "${nopgo_dir}" || true)"
            fi
        fi

        info "Qt target cmake dir: ${qt6_cmake_dir}"
        [[ -n "${qt_host_dir}" ]] && info "Qt host dir:         ${qt_host_dir}"

        local debug_flag=""
        # -gcodeview: on x86_64-w64-mingw32 clang defaults to DWARF; without
        # -gcodeview lld-link has nothing CodeView-formatted and emits no .pdb.
        [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g -gcodeview"
        local linker_debug_flag=""
        # --pdb=: MinGW lld driver's flag (not -DEBUG, which isn't in its table).
        # Empty value auto-names PDB after each output binary.
        # --threads=1: prevents LLD COFF deadlock between PDB merge and LTO backend
        # threads (only affects RelWithDebInfo, which is slower anyway).
        [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && linker_debug_flag="-Wl,--pdb= -Wl,--threads=1"
        local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
        local lto_flag; lto_flag="$(lto_clang_flag)"

        # Pre-build FFmpeg for this build directory
        detect_ffmpeg_version

        info "Configuring CMake (no-PGO Windows PE, LTO=${LTO_MODE})..."
        cd "${nopgo_dir}"
        rm -f CMakeCache.txt; rm -rf CMakeFiles

        # shellcheck disable=SC2034  # _CMAKE_ARGS used via array expansion below
        build_common_cmake_args
        _CMAKE_ARGS+=(
            "-DCITRON_ENABLE_PGO_USE=OFF"
            "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON"
            "-DCMAKE_C_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_flag}"
            "-DCMAKE_CXX_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_flag}"
            "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${linker_debug_flag}"
        )
        [[ -n "${qt6_cmake_dir}" ]] && _CMAKE_ARGS+=("-DQt6_DIR=${qt6_cmake_dir}")
        [[ -n "${qt_host_dir}"   ]] && _CMAKE_ARGS+=("-DQT_HOST_PATH=${qt_host_dir}")
        cmake "${SOURCE_DIR}" "${_CMAKE_ARGS[@]}" \
            || error "CMake configure failed"
        info "Building citron.exe (no PGO, ${BUILD_TYPE})..."
        cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}" \
            || error "cmake --build failed"

        success "No-PGO Windows PE: ${nopgo_dir}/bin/citron.exe"

        # Derive the Qt install root from qt6_cmake_dir for DLL deployment
        local _nopgo_qt_root
        _nopgo_qt_root="$(dirname "$(dirname "$(dirname "${qt6_cmake_dir}")")")"

        deploy_runtime_dlls \
            "${nopgo_dir}/bin" \
            "${_nopgo_qt_root}" \
            "${nopgo_dir}"

        echo ""
        success "════════════════════════════════════════════════════════════════"
        success "  Stage use (--pgo-type none) complete"
        success "  Binary: ${nopgo_dir}/bin/citron.exe"
        success "  PGO:    none"
        local lto_label; lto_label="$(lto_clang_flag)"
        success "  LTO:    ${LTO_MODE}${lto_label:+ (${lto_label})}"
        success "════════════════════════════════════════════════════════════════"
        return 0
    fi

    header "Stage 2: PGO + LTO Optimized Build"

    check_tool "${CLANG}"; check_tool "${CLANGPP}"
    check_tool "ninja";    check_tool "cmake"

    require_llvm_mingw
    compile_comsupp_stubs
    setup_case_fixup_headers

    # ── Sentinel check: verify generate/use LTO and PGO modes match ─────────
    # PGO_MODE=="none" is exempt: a --pgo none use build consumes no
    # profraw/profdata from generate at all, so there is nothing that could
    # actually be mismatched -- see the identical exemption in stage_clangcl().
    local _gen_cfg="${BUILD_ROOT}/.citron-gen-config"
    if [[ "${PGO_MODE}" != "none" && -f "${_gen_cfg}" ]]; then
        local _gen_lto _gen_pgo
        _gen_lto=$(awk -F= '/^LTO=/{print $2; exit}' "${_gen_cfg}" 2>/dev/null || true)
        _gen_pgo=$(awk -F= '/^PGO=/{print $2; exit}' "${_gen_cfg}" 2>/dev/null || true)
        if [[ -n "${_gen_lto}" && "${_gen_lto}" != "${LTO_MODE}" ]]; then
            error "LTO mismatch: generate used LTO=${_gen_lto}, use has LTO=${LTO_MODE}.\n"\
                  "       IR PGO profiles are tied to the IR produced at generate time.\n"\
                  "       Re-run generate with --lto ${LTO_MODE}, or use with --lto ${_gen_lto}."
        fi
        if [[ -n "${_gen_pgo}" && "${_gen_pgo}" != "${PGO_MODE}" ]]; then
            error "PGO mode mismatch: generate used PGO=${_gen_pgo}, use has PGO=${PGO_MODE}.\n"\
                  "       Profile data from ${_gen_pgo} PGO cannot feed ${PGO_MODE} use.\n"\
                  "       Re-run generate with --pgo-type ${PGO_MODE}."
        fi
    fi

    # Prefer merged.profdata (stage1 + CS context-sensitive) if present.
    local merged_pd="${PROFILE_DIR}/merged.profdata"
    local stage1_pd="${PROFILE_DIR}/default.profdata"
    local profdata

    # Guard: if merged.profdata already exists but unmerged CS profraw has
    # arrived since it was written, the file is stale — it contains only the
    # stage1 profile.  A re-run that skips CS merging would silently produce a
    # binary that looks like a full CS-IRPGO build but is missing the CS layer.
    # Detect this and remove the stale file so the merge block below runs.
    if [[ -f "${merged_pd}" ]]; then
        local _cs_dir_check="${PROFILE_DIR}/cs"
        normalize_profraw_dirs "${_cs_dir_check}" 2>/dev/null || true
        local _cs_pending
        _cs_pending=$(find "${_cs_dir_check}" -maxdepth 1 -name "*.profraw" 2>/dev/null | wc -l)
        if [[ "${_cs_pending}" -gt 0 ]]; then
            warn "merged.profdata exists but ${_cs_pending} unmerged CS profraw file(s) found."
            warn "The existing merged.profdata was built without the CS layer."
            warn "Removing stale merged.profdata and re-merging with CS data..."
            rm -f "${merged_pd}"
        fi
    fi

    if [[ -f "${merged_pd}" ]]; then
        profdata="${merged_pd}"
        info "Using CS-IRPGO merged profile: ${profdata}"
    elif [[ -f "${stage1_pd}" ]]; then
        profdata="${stage1_pd}"
        info "Using stage1 profile: ${profdata}"
    else
        normalize_profraw_dirs "${PROFILE_DIR}"
        local profraw_count
        profraw_count=$(find "${PROFILE_DIR}" -maxdepth 1 -name "*.profraw" 2>/dev/null | wc -l)
        if [[ "${profraw_count}" -gt 0 ]]; then
            info "Merging ${profraw_count} .profraw file(s) into default.profdata..."
            "${LLVM_PROFDATA}" merge --sparse \
                --output="${stage1_pd}" "${PROFILE_DIR}"/*.profraw
            success "Profile data merged: ${stage1_pd}"
            profdata="${stage1_pd}"
        else
            error "No profile data found.\n" \
                  "       Run generate, collect .profraw on Windows,\n" \
                  "       copy to ${PROFILE_DIR}/, then re-run."
        fi
    fi

    # Auto-merge CS profraw if present and merged.profdata not yet written.
    # Step 1: CS profraw → cs-only.profdata
    # Step 2: default.profdata + cs-only.profdata → merged.profdata
    local cs_dir="${PROFILE_DIR}/cs"
    if [[ ! -f "${merged_pd}" && -d "${cs_dir}" ]]; then
        normalize_profraw_dirs "${cs_dir}"
        local cs_count
        cs_count=$(find "${cs_dir}" -name "*.profraw" 2>/dev/null | wc -l)
        if [[ "${cs_count}" -gt 0 ]]; then
            info "CS profraw detected (${cs_count} files) — merging with stage1..."
            local cs_tmp="${PROFILE_DIR}/cs-only.profdata"
            "${LLVM_PROFDATA}" merge --sparse \
                --output="${cs_tmp}" "${cs_dir}"/*.profraw
            "${LLVM_PROFDATA}" merge --sparse \
                --output="${merged_pd}" "${profdata}" "${cs_tmp}"
            rm -f "${cs_tmp}"
            success "CS-IRPGO merged profile: ${merged_pd}"
            profdata="${merged_pd}"
        fi
    fi

    local lto_flag; lto_flag="$(lto_clang_flag)"
    local profdata_compiler="${profdata}"
    [[ "${_HOST_OS}" == "windows" ]] && profdata_compiler="$(cygpath -m "${profdata}")"
    local pgo_flag
    if [[ "${PGO_MODE}" == "ir" ]]; then
        pgo_flag="-fprofile-use=\"${profdata_compiler}\""
    else
        pgo_flag="-fprofile-instr-use=\"${profdata_compiler}\" -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
    fi
    local lto_pgo_flag="${lto_flag:+${lto_flag} }${pgo_flag}"

    # ── 2a. Cross-compiled Windows PE ────────────────────────────────────────
    # Pre-build FFmpeg for this build directory
    detect_ffmpeg_version

    info "Configuring CMake (PGO+LTO Windows PE)..."
    mkdir -p "${BUILD_USE}"; cd "${BUILD_USE}"
    rm -f CMakeCache.txt; rm -rf CMakeFiles

    # Reuse generate's Qt to avoid re-downloading the wrong variant.
    local qt_install_dir="${CPM_SOURCE_CACHE}/qt-bin/6.9.3/llvm-mingw_64"
    local qt_host_dir="${CPM_SOURCE_CACHE}/qt-bin-host/6.9.3/gcc_64"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        qt_host_dir=""
    fi
    local qt6_cmake_dir="${qt_install_dir}/lib/cmake/Qt6"

    local debug_flag=""
    # -gcodeview: clang defaults to DWARF on x86_64-w64-mingw32; without it lld emits no .pdb.
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g -gcodeview"
    local linker_debug_flag=""
    # --pdb=: MinGW lld driver's flag (not -DEBUG). --threads=1: prevents LLD COFF
    # deadlock between PDB merge threads and LTO backend threads (RelWithDebInfo only).
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && linker_debug_flag="-Wl,--pdb= -Wl,--threads=1"
    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
    build_common_cmake_args
    _CMAKE_ARGS+=(
        "-DCITRON_ENABLE_PGO_USE=ON"
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON"
        "-DCMAKE_C_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}"
        "-DCMAKE_CXX_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}"
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag} ${linker_debug_flag}"
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}"
    )
    [[ -n "${qt6_cmake_dir}" ]] && _CMAKE_ARGS+=("-DQt6_DIR=${qt6_cmake_dir}")
    [[ -n "${qt_host_dir}"   ]] && _CMAKE_ARGS+=("-DQT_HOST_PATH=${qt_host_dir}")
    cmake "${SOURCE_DIR}" "${_CMAKE_ARGS[@]}" \
        || error "CMake configure failed"
    info "Building PGO+LTO citron.exe (${BUILD_TYPE})..."
    cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}"

    success "PGO+LTO Windows PE: ${BUILD_USE}/bin/citron.exe"

    deploy_runtime_dlls \
        "${BUILD_USE}/bin" \
        "${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64" \
        "${BUILD_USE}"

    local _pgo_label
    if [[ "${profdata}" == "${merged_pd}" ]]; then
        _pgo_label="CS-IRPGO (merged: stage1 IR + CS layer)"
    else
        _pgo_label="IR PGO (stage1 only)"
    fi

    local unity_flag=""
    [[ "${UNITY_BUILD}" == "ON" ]] && unity_flag=" --unity"

    echo ""
    echo -e "${GREEN}================================================================${RESET}"
    echo -e "${GREEN}  Stage use complete${RESET}"
    echo -e "${GREEN}================================================================${RESET}"
    echo ""
    echo -e "  ${BOLD}Binary  :${RESET} ${BUILD_USE}/bin/citron.exe"
    echo -e "  ${BOLD}PGO     :${RESET} ${_pgo_label}"
    echo -e "  ${BOLD}Profile :${RESET} ${profdata}"
    echo -e "  ${BOLD}LTO     :${RESET} ${LTO_MODE}$(lto_clang_flag | grep -q . && echo " ($(lto_clang_flag))" || true)"
    echo ""
    echo "  Next steps (choose one):"
    echo ""
    echo "  A) Run Propeller (recommended — perf LBR function+BB layout):"
    echo "       ./build-clangtron-windows.sh propeller --pgo-type ${PGO_MODE} --lto ${LTO_MODE}${unity_flag}"
    echo ""
    echo "  B) Run BOLT (ELF-proxy function ordering):"
    echo "       ./build-clangtron-windows.sh bolt --pgo-type ${PGO_MODE} --lto ${LTO_MODE}${unity_flag}"
    echo ""
    if [[ "${profdata}" != "${merged_pd}" ]] && [[ "${PGO_MODE}" == "ir" ]]; then
        echo "  C) Add CS-IRPGO layer (second Windows session, better profile quality):"
        echo "       ./build-clangtron-windows.sh csgenerate --pgo-type ir --lto ${LTO_MODE}${unity_flag}"
        echo "       # then collect cs-default-*.profraw (or folder) → pgo-profiles/cs/"
        echo "       ./build-clangtron-windows.sh use --pgo-type ir --lto ${LTO_MODE}${unity_flag}"
        echo ""
    fi
    echo -e "${GREEN}================================================================${RESET}"
    echo ""
}

# =============================================================================
# Helper: build the native Linux ELF (for BOLT/Propeller profiling)
# =============================================================================
stage_build_elf() {
    if [[ "${_HOST_OS}" == "windows" ]]; then
        error "build-elf requires a Linux host (ELF target). Not supported on Windows/MSYS2.\n" \
              "  BBAddrMap support for Windows PE/COFF is being developed — track at:\n" \
              "  https://discourse.llvm.org/t/rfc-extend-bbaddrmap-support-to-coff-windows/90232"
    fi
    # --pgo none: baseline ELF (no PGO, just -fbasic-block-address-map for BOLT/Propeller).
    # Outputs to build/use-nopgo-elf/ so it never collides with the PGO ELF.
    local _elf_nopgo=0
    if [[ "${PGO_MODE}" == "none" ]]; then
        _elf_nopgo=1
        BUILD_USE_ELF="${BUILD_ROOT}/use-nopgo-elf"
        header "Stage 2b: Baseline Linux ELF (no PGO, BBAddrMap)"
        info "Output: ${BUILD_USE_ELF}/bin/citron"
    fi

    # Resolve profdata — use merged (CS+stage1) if present, else stage1.
    # Works for both IR and FE PGO modes; the elf_pgo_flag below uses it.
    local merged_pd="${PROFILE_DIR}/merged.profdata"
    local stage1_pd="${PROFILE_DIR}/default.profdata"
    local profdata=""
    if [[ "${_elf_nopgo}" -eq 0 ]]; then
    if [[ -f "${merged_pd}" ]]; then
        profdata="${merged_pd}"
        info "ELF build: using CS-IRPGO merged profile"
    elif [[ -f "${stage1_pd}" ]]; then
        profdata="${stage1_pd}"
        info "ELF build: using stage1 profile"
    else
        # Try merging profraw on the fly
        normalize_profraw_dirs "${PROFILE_DIR}"
        local profraw_count
        profraw_count=$(find "${PROFILE_DIR}" -maxdepth 1 -name "*.profraw" 2>/dev/null | wc -l)
        if [[ "${profraw_count}" -gt 0 ]]; then
            info "ELF build: merging ${profraw_count} profraw files..."
            "${LLVM_PROFDATA}" merge --sparse \
                --output="${stage1_pd}" "${PROFILE_DIR}"/*.profraw
            profdata="${stage1_pd}"
        else
            error "No profile data found for ELF build.\n"\
                  "       Run the use stage first so profdata exists in ${PROFILE_DIR}/"
        fi
    fi
    # Auto-merge CS profraw if it arrived after stage1 was merged
    local cs_dir="${PROFILE_DIR}/cs"
    if [[ ! -f "${merged_pd}" && -d "${cs_dir}" ]]; then
        normalize_profraw_dirs "${cs_dir}"
        local cs_count
        cs_count=$(find "${cs_dir}" -name "*.profraw" 2>/dev/null | wc -l)
        if [[ "${cs_count}" -gt 0 ]]; then
            info "ELF build: merging ${cs_count} CS profraw files with stage1..."
            local cs_tmp="${PROFILE_DIR}/cs-only.profdata"
            "${LLVM_PROFDATA}" merge --sparse \
                --output="${cs_tmp}" "${cs_dir}"/*.profraw
            "${LLVM_PROFDATA}" merge --sparse \
                --output="${merged_pd}" "${profdata}" "${cs_tmp}"
            rm -f "${cs_tmp}"
            success "CS-IRPGO merged profile written: ${merged_pd}"
            profdata="${merged_pd}"
        fi
    fi
    fi # end if [[ _elf_nopgo -eq 0 ]]


    info "Configuring CMake (native Linux ELF)..."
    mkdir -p "${BUILD_USE_ELF}"

    cd "${BUILD_USE_ELF}"
    rm -f CMakeCache.txt; rm -rf CMakeFiles

    # ── Qt for native ELF build ───────────────────────────────────────────────
    # DownloadExternals.cmake has two issues with aqt 3.x:
    #   1. Uses wrong module names ('qt_base') — aqt errors but cmake prints "Downloaded Qt" anyway.
    #   2. Sets Qt6_DIR to install root instead of .../lib/cmake/Qt6, causing find_package fallback
    #      to system Qt 6.4.2 which lacks Qt6GuiPrivate.
    # Fix: pre-download via aqt with correct syntax. QT_HOST_PATH deliberately not set
    # (would trigger cross-compile mode, making cmake ignore Qt6_DIR).
    local elf_qt_dir="${BUILD_USE_ELF}/externals/qt/6.9.3/linux"
    local elf_qt_cmake_dir="${elf_qt_dir}/lib/cmake/Qt6"

    # Remove stale symlink to qt-host (lacks GuiPrivate).
    if [[ -L "${elf_qt_dir}" ]]; then
        info "ELF build: removing qt-host symlink (lacks Qt6GuiPrivate)"
        rm -f "${elf_qt_dir}"
    fi

    # Verify required Qt cmake configs are present; wipe and re-download if any are missing.
    local _elf_qt_ok=1
    for _qtmod in Qt6 Qt6Network Qt6Widgets Qt6Gui Qt6DBus Qt6Svg Qt6OpenGL; do
        if [[ ! -f "${elf_qt_dir}/lib/cmake/${_qtmod}/${_qtmod}Config.cmake" ]]; then
            warn "ELF build: missing Qt cmake config: ${_qtmod}Config.cmake"
            _elf_qt_ok=0
        fi
    done

    if [[ "${_elf_qt_ok}" -eq 0 || ! -f "${elf_qt_cmake_dir}/Qt6Config.cmake" ]]; then
        info "ELF build: (re-)downloading Qt 6.9.3 linux via aqt..."
        python3 -m pip install aqtinstall --break-system-packages --quiet 2>/dev/null || true
        local aqt_base_dir="${BUILD_USE_ELF}/externals/qt"
        rm -rf "${aqt_base_dir}/6.9.3"
        mkdir -p "${aqt_base_dir}"
        python3 -m aqt install-qt             --outputdir "${aqt_base_dir}"             linux desktop 6.9.3 linux_gcc_64             || warn "aqt Qt 6.9.3 base download failed"
        # Rename arch dir to 'linux' (expected by DownloadExternals.cmake)
        for _arch in gcc_64 linux_gcc_64; do
            if [[ -d "${aqt_base_dir}/6.9.3/${_arch}" && "${_arch}" != "linux" ]]; then
                rm -rf "${aqt_base_dir}/6.9.3/linux"
                mv "${aqt_base_dir}/6.9.3/${_arch}" "${aqt_base_dir}/6.9.3/linux"
                success "ELF build: Qt 6.9.3 linux downloaded (arch was ${_arch})"
                break
            fi
        done
        python3 -m aqt install-qt             --outputdir "${aqt_base_dir}"             linux desktop 6.9.3 linux_gcc_64             --modules qtsvg 2>/dev/null             || warn "aqt qtsvg module install failed (may already be present)"
        if [[ ! -f "${elf_qt_cmake_dir}/Qt6Config.cmake" ]]; then
            warn "ELF build: Qt6Config.cmake still missing after aqt download — check aqt output"
        fi
    else
        info "ELF build: Qt 6.9.3 already present at ${elf_qt_dir}"
    fi

    # Qt6Multimedia: aqt can't install for linux desktop (GStreamer dep).
    # Inject a stub so find_package doesn't abort; CITRON_USE_QT_MULTIMEDIA=OFF prevents linking.
    local multimedia_cmake_dir="${elf_qt_dir}/lib/cmake/Qt6Multimedia"
    if [[ ! -f "${multimedia_cmake_dir}/Qt6MultimediaConfig.cmake" ]]; then
        info "ELF build: injecting Qt6Multimedia stub (aqt linux cannot install multimedia)"
        mkdir -p "${multimedia_cmake_dir}"
        cat > "${multimedia_cmake_dir}/Qt6MultimediaConfig.cmake" << 'QTMEOF'
# Stub — aqt cannot install qtmultimedia for linux desktop (GStreamer dependency).
# Satisfies find_package(Qt6 REQUIRED COMPONENTS Multimedia); never linked because
# CITRON_USE_QT_MULTIMEDIA=OFF is set in the cmake invocation.
set(Qt6Multimedia_FOUND TRUE)
set(Qt6Multimedia_VERSION "6.9.3")
if(NOT TARGET Qt6::Multimedia)
    add_library(Qt6::Multimedia INTERFACE IMPORTED GLOBAL)
endif()
QTMEOF
        cat > "${multimedia_cmake_dir}/Qt6MultimediaConfigVersion.cmake" << 'QTMEOF'
set(PACKAGE_VERSION "6.9.3")
if(PACKAGE_FIND_VERSION VERSION_GREATER "6.9.3")
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
    if(PACKAGE_FIND_VERSION STREQUAL "6.9.3")
        set(PACKAGE_VERSION_EXACT TRUE)
    endif()
endif()
QTMEOF
    fi

    # Qt6GuiPrivate: aqt base ships private headers but no cmake config for them.
    # Inject a stub that points cmake at the actual headers in the aqt download.
    local guiprivate_cmake_dir="${elf_qt_dir}/lib/cmake/Qt6GuiPrivate"
    if [[ ! -f "${guiprivate_cmake_dir}/Qt6GuiPrivateConfig.cmake" ]]; then
        info "ELF build: injecting Qt6GuiPrivate stub (aqt base has headers, no cmake config)"
        mkdir -p "${guiprivate_cmake_dir}"
        cat > "${guiprivate_cmake_dir}/Qt6GuiPrivateConfig.cmake" << 'QTGPEOF'
# Auto-generated — aqt linux Qt has private headers but no cmake config for them.
# CMAKE_CURRENT_LIST_DIR = <qt>/lib/cmake/Qt6GuiPrivate/
# Private headers live at <qt>/include/QtGui/6.9.3/
set(Qt6GuiPrivate_FOUND TRUE)
set(Qt6GuiPrivate_VERSION "6.9.3")
get_filename_component(_qt6_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET Qt6::GuiPrivate)
    add_library(Qt6::GuiPrivate INTERFACE IMPORTED GLOBAL)
    target_include_directories(Qt6::GuiPrivate INTERFACE
        "${_qt6_prefix}/include/QtGui/6.9.3"
        "${_qt6_prefix}/include/QtGui/6.9.3/QtGui"
    )
    if(TARGET Qt6::Gui)
        target_link_libraries(Qt6::GuiPrivate INTERFACE Qt6::Gui)
    endif()
endif()
QTGPEOF
        cat > "${guiprivate_cmake_dir}/Qt6GuiPrivateConfigVersion.cmake" << 'QTGPEOF'
set(PACKAGE_VERSION "6.9.3")
if(PACKAGE_FIND_VERSION VERSION_GREATER "6.9.3")
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
    if(PACKAGE_FIND_VERSION STREQUAL "6.9.3")
        set(PACKAGE_VERSION_EXACT TRUE)
    endif()
endif()
QTGPEOF
    fi

    # CMAKE_PREFIX_PATH includes Qt + Vulkan/SPIRV header installs from the generate stage.
    local elf_cmake_prefix="${elf_qt_dir};${VULKAN_HEADERS_INSTALL};${SPIRV_HEADERS_INSTALL}"
    info "ELF build: CMAKE_PREFIX_PATH → ${elf_cmake_prefix}"

    # LTO intentionally disabled: with -flto lld's ThinLTO backend doesn't propagate
    # -fbasic-block-address-map, so the .llvm_bb_addr_map section never appears.
    # Without LTO every TU compiles to native code directly and the section is always present.
    local elf_lto_flag
    case "${LTO_MODE}" in
        full) elf_lto_flag="-flto"      ;;
        thin) elf_lto_flag="-flto=thin" ;;
        none) elf_lto_flag=""           ;;
    esac
    local elf_pgo_flag
    if [[ "${_elf_nopgo}" -eq 1 ]]; then
        elf_pgo_flag=""
    elif [[ "${PGO_MODE}" == "ir" ]]; then
        elf_pgo_flag="-fprofile-use=\"${profdata}\""
    else
        elf_pgo_flag="-fprofile-instr-use=\"${profdata}\" -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
    fi
    local debug_flag=""
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
    local elf_compile_flags="-O3 -DNDEBUG ${debug_flag} -D_stat64=stat ${elf_pgo_flag} -fbasic-block-address-map -Wno-error=backend-plugin"
    local elf_linker_flags="-fuse-ld=lld-${CLANG_VERSION} -Wl,--emit-relocs"

    # Wipe object cache if compile flags changed (ninja reuses .o files baked at compile time).
    local _elf_flags_hash _elf_flags_stored=""
    _elf_flags_hash=$(printf '%s' "${elf_compile_flags}" | md5sum | cut -d' ' -f1)
    local _elf_flags_sentinel="${BUILD_USE_ELF}/.elf_flags_hash"
    [[ -f "${_elf_flags_sentinel}" ]] && _elf_flags_stored=$(cat "${_elf_flags_sentinel}" 2>/dev/null || true)

    if [[ "${_elf_flags_hash}" != "${_elf_flags_stored}" ]]; then
        info "ELF compile flags changed — wiping object cache (preserving externals/)..."
        find "${BUILD_USE_ELF}" -mindepth 1 -maxdepth 1 \
            ! -name "externals" -exec rm -rf {} + 2>/dev/null || true
        mkdir -p "${BUILD_USE_ELF}"
    elif [[ -f "${BUILD_USE_ELF}/bin/citron" ]]; then
        success "ELF already built and flags unchanged — skipping rebuild."
        return 0
    else
        info "ELF compile flags unchanged — incremental build"
    fi

    # Patch DownloadExternals.cmake to include all required Qt6 components.
    # The file only requests Core/Gui/Widgets by default; Network, Svg, DBus,
    # and OpenGL are also needed for the native ELF build.
    local _dle=""
    for _f in         "${SOURCE_DIR}/cmake/DownloadExternals.cmake"         "${SOURCE_DIR}/CMakeModules/DownloadExternals.cmake"         "${SOURCE_DIR}/externals/DownloadExternals.cmake"; do
        if [[ -f "${_f}" ]]; then _dle="${_f}"; break; fi
    done
    # Also search one level up (top-level cmake/ dir)
    if [[ -z "${_dle}" ]]; then
        _dle="$(find "${SOURCE_DIR}" -maxdepth 3 -name "DownloadExternals.cmake" 2>/dev/null | head -1)"
    fi
    if [[ -n "${_dle}" ]]; then
        info "ELF build: patching Qt6 COMPONENTS in ${_dle}..."
        python3 - "${_dle}" << 'DLPYEOF'
import sys, re, pathlib
path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding='utf-8', errors='replace')

needed = ['Network', 'Svg', 'DBus', 'OpenGL', 'OpenGLWidgets']

def patch_qt6_find(src):
    # Find find_package(Qt6 ... COMPONENTS ...) blocks (possibly multiline)
    # and ensure all needed components are present
    pattern = re.compile(
        r'(find_package\s*\(\s*Qt6[^)]*?COMPONENTS\s+)((?:[A-Za-z0-9_]+\s+)*[A-Za-z0-9_]+)(\s*(?:REQUIRED)?[^)]*\))',
        re.DOTALL
    )
    def add_components(m):
        prefix = m.group(1)
        comps_str = m.group(2)
        suffix = m.group(3)
        existing = set(comps_str.split())
        to_add = [c for c in needed if c not in existing]
        if to_add:
            print("  Adding Qt6 components: " + ' '.join(to_add))
            return prefix + comps_str + ' ' + ' '.join(to_add) + suffix
        return m.group(0)
    patched = pattern.sub(add_components, src)
    return patched

patched = patch_qt6_find(text)
if patched != text:
    path.write_text(patched, encoding='utf-8')
    print("  Patched " + str(path))
else:
    print("  No find_package(Qt6 COMPONENTS ...) found to patch — may need manual inspection")
DLPYEOF
    else
        warn "ELF build: DownloadExternals.cmake not found — Qt6::Network may be missing"
        warn "  Searched under ${SOURCE_DIR}/cmake/, CMakeModules/, externals/"
    fi

    # Also patch src/citron/CMakeLists.txt directly — DownloadExternals has a
    # fast-path when Qt6_DIR is cached that skips Network/DBus/OpenGL entirely.
    # Injecting find_package(Qt6 OPTIONAL_COMPONENTS ...) after Qt6_DIR is set
    # ensures those targets are imported. Guarded by CITRON_ELF_BUILD so the
    # PE build is unaffected.
    local _citron_cmake="${SOURCE_DIR}/src/citron/CMakeLists.txt"
    if [[ -f "${_citron_cmake}" ]]; then
        if ! grep -q "CITRON_ELF_QT_NETWORK_PATCH" "${_citron_cmake}"; then
            info "ELF build: patching src/citron/CMakeLists.txt to import Qt6::Network et al..."
            # Write patcher script to a file — avoids heredoc/backslash/newline issues
            local _cpatcher="${BUILD_ROOT}/_citron_cmake_patcher.py"
            cat > "${_cpatcher}" << 'CTPYEOF'
import sys, pathlib
path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding='utf-8', errors='replace')
inject = (
    "\n"
    "# CITRON_ELF_QT_NETWORK_PATCH -- injected by build-clangtron-windows.sh\n"
    "# DownloadExternals fast-path omits Network/DBus/OpenGL from find_package.\n"
    "if(CITRON_ELF_BUILD)\n"
    "    find_package(Qt6 OPTIONAL_COMPONENTS Network DBus OpenGL OpenGLWidgets)\n"
    "endif()\n"
)
anchor = "set(CMAKE_INCLUDE_CURRENT_DIR ON)"
if anchor in text:
    idx = text.index(anchor) + len(anchor)
    while idx < len(text) and text[idx] in ('\r', '\n'):
        idx += 1
    text = text[:idx] + inject + text[idx:]
    print("  Patched " + str(path))
else:
    text = inject + text
    print("  Patched (fallback) " + str(path))
path.write_text(text, encoding='utf-8')
CTPYEOF
            python3 "${_cpatcher}" "${_citron_cmake}"
        else
            info "ELF build: src/citron/CMakeLists.txt already patched"
        fi
    else
        warn "ELF build: src/citron/CMakeLists.txt not found at ${_citron_cmake}"
    fi
    # externals/ffmpeg/CMakeLists.txt contains add_custom_command blocks with
    # no OUTPUT or TARGET — a CMake error on Linux (the PE build takes a
    # different code path and never hits these). Delete the broken blocks.
    local _ffmpeg_cmake="${SOURCE_DIR}/externals/ffmpeg/CMakeLists.txt"
    if [[ -f "${_ffmpeg_cmake}" ]]; then
        info "ELF build: patching externals/ffmpeg/CMakeLists.txt (add_custom_command fix)..."
        # Revert any previous stamp-based patch so delete-based patcher works cleanly
        if grep -q "_ffmpeg_cmake_patch_0.stamp" "${_ffmpeg_cmake}" 2>/dev/null; then
            info "ELF build: reverting old stamp-based ffmpeg patch via git..."
            git -C "${SOURCE_DIR}" checkout -- externals/ffmpeg/CMakeLists.txt 2>/dev/null \
                || warn "ELF build: git restore failed — delete patcher will try anyway"
        fi
        # Write patcher using base64 — avoids ALL heredoc/backslash escaping issues.
        # Deletion strategy: remove add_custom_command blocks with no OUTPUT and no TARGET.
        # (Adding a dummy stamp OUTPUT fails in cmake foreach loops: same rule generated N times.)
        local _patcher="${BUILD_ROOT}/_ffmpeg_cmake_patcher.py"
        echo 'aW1wb3J0IHN5cywgcGF0aGxpYgoKcGF0aCA9IHBhdGhsaWIuUGF0aChzeXMuYXJndlsxXSkKdGV4dCA9IHBhdGgucmVhZF90ZXh0KGVuY29kaW5nPSd1dGYtOCcsIGVycm9ycz0ncmVwbGFjZScpCgpwcmludCgiICBmZm1wZWcgcGF0Y2hlcjogZmlsZSBsZW5ndGggPSAiICsgc3RyKGxlbih0ZXh0KSkgKyAiIGNoYXJzIikKCkFDQyA9ICdhZGRfY3VzdG9tX2NvbW1hbmQnCk9VVFBVVF9LVyA9ICdPVVRQVVQnClRBUkdFVF9LVyA9ICdUQVJHRVQnCgpkZWYgZmluZF9ibG9ja3Moc3JjKToKICAgIHNwYW5zID0gW10KICAgIGkgPSAwCiAgICB3aGlsZSBUcnVlOgogICAgICAgIGlkeCA9IHNyYy5maW5kKEFDQywgaSkKICAgICAgICBpZiBpZHggPT0gLTE6CiAgICAgICAgICAgIGJyZWFrCiAgICAgICAgaiA9IGlkeCArIGxlbihBQ0MpCiAgICAgICAgd2hpbGUgaiA8IGxlbihzcmMpIGFuZCBzcmNbal0gaW4gKCcgJywgJ1x0JywgJ1xyJywgJ1xuJyk6CiAgICAgICAgICAgIGogKz0gMQogICAgICAgIGlmIGogPj0gbGVuKHNyYykgb3Igc3JjW2pdICE9ICcoJzoKICAgICAgICAgICAgaSA9IGlkeCArIDEKICAgICAgICAgICAgY29udGludWUKICAgICAgICBkZXB0aCA9IDAKICAgICAgICBrID0gagogICAgICAgIHdoaWxlIGsgPCBsZW4oc3JjKToKICAgICAgICAgICAgaWYgc3JjW2tdID09ICcoJzoKICAgICAgICAgICAgICAgIGRlcHRoICs9IDEKICAgICAgICAgICAgZWxpZiBzcmNba10gPT0gJyknOgogICAgICAgICAgICAgICAgZGVwdGggLT0gMQogICAgICAgICAgICAgICAgaWYgZGVwdGggPT0gMDoKICAgICAgICAgICAgICAgICAgICBicmVhawogICAgICAgICAgICBrICs9IDEKICAgICAgICBzcGFucy5hcHBlbmQoKGlkeCwgayArIDEpKQogICAgICAgIGkgPSBrICsgMQogICAgcmV0dXJuIHNwYW5zCgpkZWYgc3RyaXBfY29tbWVudHMocyk6CiAgICBvdXQgPSBbXQogICAgZm9yIGxpbmUgaW4gcy5zcGxpdGxpbmVzKCk6CiAgICAgICAgaWR4ID0gbGluZS5maW5kKCcjJykKICAgICAgICBvdXQuYXBwZW5kKGxpbmVbOmlkeF0gaWYgaWR4ICE9IC0xIGVsc2UgbGluZSkKICAgIHJldHVybiAnXG4nLmpvaW4ob3V0KQoKc3BhbnMgPSBmaW5kX2Jsb2Nrcyh0ZXh0KQpwcmludCgiICBmZm1wZWcgcGF0Y2hlcjogZm91bmQgIiArIHN0cihsZW4oc3BhbnMpKSArICIgYWRkX2N1c3RvbV9jb21tYW5kIGJsb2NrcyhzKSIpCgpicm9rZW4gPSBbXQpmb3IgKHMsIGUpIGluIHNwYW5zOgogICAgYm9keSA9IHN0cmlwX2NvbW1lbnRzKHRleHRbczplXSkudXBwZXIoKQogICAgaGFzX291dHB1dCA9IE9VVFBVVF9LVyBpbiBib2R5CiAgICBoYXNfdGFyZ2V0ID0gVEFSR0VUX0tXIGluIGJvZHkKICAgIHByaW50KCIgIGJsb2NrIGF0IGNoYXIgIiArIHN0cihzKSArICI6IGhhc19vdXRwdXQ9IiArIHN0cihoYXNfb3V0cHV0KSArICIgaGFzX3RhcmdldD0iICsgc3RyKGhhc190YXJnZXQpKQogICAgaWYgbm90IGhhc19vdXRwdXQgYW5kIG5vdCBoYXNfdGFyZ2V0OgogICAgICAgIGJyb2tlbi5hcHBlbmQoKHMsIGUpKQoKaWYgbm90IGJyb2tlbjoKICAgIHByaW50KCIgIGZmbXBlZyBwYXRjaGVyOiBubyBicm9rZW4gYmxvY2tzIGZvdW5kIikKZWxzZToKICAgIHJlc3VsdCA9IGxpc3QodGV4dCkKICAgIGZvciAocywgZSkgaW4gcmV2ZXJzZWQoYnJva2VuKToKICAgICAgICBlZSA9IGUKICAgICAgICB3aGlsZSBlZSA8IGxlbih0ZXh0KSBhbmQgdGV4dFtlZV0gaW4gKCdccicsICdcbicpOgogICAgICAgICAgICBlZSArPSAxCiAgICAgICAgZGVsIHJlc3VsdFtzOmVlXQogICAgdGV4dCA9ICcnLmpvaW4ocmVzdWx0KQogICAgcGF0aC53cml0ZV90ZXh0KHRleHQsIGVuY29kaW5nPSd1dGYtOCcpCiAgICBwcmludCgiICBmZm1wZWcgcGF0Y2hlcjogcmVtb3ZlZCAiICsgc3RyKGxlbihicm9rZW4pKSArICIgYnJva2VuIGJsb2NrKHMpIGZyb20gIiArIHN0cihwYXRoKSkK' | base64 -d > "${_patcher}"
        python3 "${_patcher}" "${_ffmpeg_cmake}"
    else
        warn "ELF build: ${_ffmpeg_cmake} not found, skipping add_custom_command patch"
    fi

    # ── Patch FFmpeg CMakeLists.txt: remove --disable-postproc (removed in FFmpeg 8.x) ──
    # FFmpeg 8.x dropped the postproc library entirely. Any configure invocation that
    # passes --disable-postproc will abort with "Unknown option".
    local _ffmpeg_cfg_cmake="${SOURCE_DIR}/externals/ffmpeg/CMakeLists.txt"
    if [[ ! -f "${_ffmpeg_cfg_cmake}" ]]; then
        _ffmpeg_cfg_cmake="$(find "${SOURCE_DIR}/externals" -maxdepth 4 -name CMakeLists.txt             -exec grep -l "disable-postproc" {} + 2>/dev/null | head -1)"
    fi
    if [[ -n "${_ffmpeg_cfg_cmake}" ]] && grep -q "disable-postproc" "${_ffmpeg_cfg_cmake}" 2>/dev/null; then
        info "ELF build: removing --disable-postproc from FFmpeg cmake configure args..."
        sed -i 's/--disable-postproc[[:space:]]*//g' "${_ffmpeg_cfg_cmake}"
        info "  Patched ${_ffmpeg_cfg_cmake}"
    fi

    # ── Patch dynarmic emit_x64_vector.cpp: cvt256() was removed in Xbyak 6.x ──
    # dynarmic's emit_x64_vector.cpp calls tmp0.cvt256() to cast Xmm→Ymm.
    # The bundled xbyak in dynarmic no longer has this method.
    # Replacement: Xbyak::Ymm(reg.getIdx()) — identical semantics.
    local _ev_cpp="${SOURCE_DIR}/externals/dynarmic/src/dynarmic/backend/x64/emit_x64_vector.cpp"
    if [[ -f "${_ev_cpp}" ]] && grep -q "cvt256" "${_ev_cpp}" 2>/dev/null; then
        info "ELF build: patching dynarmic emit_x64_vector.cpp (cvt256 → Ymm(getIdx()))..."
        python3 - "${_ev_cpp}" << 'XBYAK_PATCH_EOF'
import sys, re, pathlib
p = pathlib.Path(sys.argv[1])
text = p.read_text(encoding='utf-8', errors='replace')
# Replace every occurrence of <reg>.cvt256() with Xbyak::Ymm(<reg>.getIdx())
patched = re.sub(r'(\w+)\.cvt256\(\)', lambda m: f'Xbyak::Ymm({m.group(1)}.getIdx())', text)
if patched != text:
    p.write_text(patched, encoding='utf-8')
    print("  Patched " + str(p) + " (" + str(text.count('.cvt256()')) + " replacement(s))")
else:
    print("  No cvt256() found — already patched or not present")
XBYAK_PATCH_EOF
    elif [[ -f "${_ev_cpp}" ]]; then
        info "ELF build: emit_x64_vector.cpp has no cvt256() — already patched"
    else
        warn "ELF build: emit_x64_vector.cpp not found at ${_ev_cpp}"
    fi


    # Run cmake; if it fails, re-run with --trace-expand to pinpoint silent
    # FATAL_ERROR messages that produce "Configuring incomplete" with no text.
    local _elf_cmake_args=(
        "${SOURCE_DIR}"
        -G Ninja
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
        -DCMAKE_C_COMPILER="${CLANG}"
        -DCMAKE_CXX_COMPILER="${CLANGPP}"
        "-DCMAKE_EXE_LINKER_FLAGS=${elf_linker_flags}"
        "-DCITRON_ENABLE_LTO=OFF"
        $([ "${_elf_nopgo}" -eq 1 ] && echo "-DCITRON_ENABLE_PGO_USE=OFF" || echo "-DCITRON_ENABLE_PGO_USE=ON")
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON"
        "-DCMAKE_C_FLAGS_${bt_upper}=${elf_compile_flags}"
        "-DCMAKE_CXX_FLAGS_${bt_upper}=${elf_compile_flags}"
        $([ "${_elf_nopgo}" -eq 0 ] && echo "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}")
        "-DCITRON_TESTS=OFF"
        "-DCITRON_USE_BUNDLED_FFMPEG=ON"
        "-DCITRON_USE_EXTERNAL_SDL2=ON"
        "-DCITRON_USE_EXTERNAL_VULKAN_HEADERS=ON"
        "-DCITRON_USE_EXTERNAL_VULKAN_UTILITY_LIBRARIES=ON"
        "-DCITRON_USE_QT_MULTIMEDIA=OFF"
        "-DCITRON_ELF_BUILD=ON"
        "-DFFmpeg_COMPONENTS=avfilter;swscale;avcodec;avutil"
        "-DQT_PROMOTE_TO_GLOBAL_TARGETS=TRUE"
        "-DCMAKE_PREFIX_PATH=${elf_cmake_prefix}"
        "-DQt6_DIR=${elf_qt_cmake_dir}"
        "-DSPIRV-Headers_DIR=${SPIRV_HEADERS_INSTALL}/share/cmake/SPIRV-Headers"
        "-DVulkanHeaders_DIR=${VULKAN_HEADERS_INSTALL}/share/cmake/VulkanHeaders"
        "-DVulkanMemoryAllocator_FOUND=TRUE"
        -Wno-dev
        ${UNITY_BUILD:+"-DENABLE_UNITY_BUILD=${UNITY_BUILD}"}
    )
    set +e
    cmake "${_elf_cmake_args[@]}" 2>&1
    local _cmake_exit=$?
    set -e
    if [[ ${_cmake_exit} -ne 0 ]]; then
        warn "ELF cmake configure failed — re-running with --trace-expand to find silent FATAL_ERROR..."
        echo ""
        # Wipe cache so trace run starts fresh
        rm -f CMakeCache.txt; rm -rf CMakeFiles
        local _trace_log="${BUILD_USE_ELF}/cmake-trace.log"
        set +e
        cmake "${_elf_cmake_args[@]}" --trace-expand 2>&1 | tee "${_trace_log}"
        set -e
        echo ""
        warn "Trace saved to: ${_trace_log}"
        warn "CMake errors found in trace:"
        echo "────────────────────────────────────────────────────────"
        # Show CMake Error lines WITH the following 5 lines (the actual error message)
        grep -n -A 5 "CMake Error at\|CMake Error:" \
            "${_trace_log}" | head -80 || true
        echo "---"
        grep -n "FATAL_ERROR\|SEND_ERROR\|Generate step failed\|Configuring incomplete" \
            "${_trace_log}" | grep -v "cmake_minimum_required\|option(" | head -20 || true
        echo ""
        warn "Last 30 non-Qt-deploy trace lines:"
        grep -v "QT_DEPLOY_TARGET\|Qt6CoreMacros\|QtPublicTarget\|QtPublicCMake\|file(GENERATE\|STATIC_LIBRARY\|EXECUTABLE\|SHARED_LIBRARY" \
            "${_trace_log}" | tail -30
        echo "────────────────────────────────────────────────────────"
        error "ELF cmake configure failed — see trace above to identify the fatal error source"
    fi

    info "Building native Linux ELF (${BUILD_TYPE})..."
    cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}"
    # Record the compile flags hash so the next run can detect changes
    printf '%s' "${_elf_flags_hash}" > "${_elf_flags_sentinel}"
    success "Native ELF: ${BUILD_USE_ELF}/bin/citron"
    echo ""
    if [[ "${_elf_nopgo}" -eq 1 ]]; then
        info "Baseline ELF built (no PGO). Use with bolt or propeller:"
        info "  ./build-clangtron-windows.sh bolt     --pgo none"
        info "  ./build-clangtron-windows.sh propeller --pgo none"
    else
        info "ELF built. Choose your next optimization stage:"
        info ""
        info "  Option A — BOLT (function-level reordering via ELF instrumentation):"
        info "    ./build-clangtron-windows.sh bolt"
        info "    (bolt pauses mid-stage — run the instrumented ELF, exit, press Enter)"
        info ""
        info "  Option B — Propeller (BB + function layout via perf LBR):"
        info "    ./build-clangtron-windows.sh propeller"
        info "    (propeller pauses mid-stage — run the perf command shown, exit, press Enter)"
    fi
}
stage_bolt() {
    if [[ "${_HOST_OS}" == "windows" ]]; then
        error "BOLT requires a Linux host (operates on ELF binaries only). Not supported on Windows/MSYS2."
    fi
    resolve_bolt_binaries
    header "Stage 3: BOLT Binary Layout Optimization"

    # BOLT is not in the LLVM apt repo for noble. It's only needed here, in
    # the bolt stage itself, so build it from source on demand instead of
    # forcing every `setup` run to pay the ~15-20 min cost.
    if [[ -z "${LLVM_BOLT}" ]]; then
        info "llvm-bolt-${CLANG_VERSION} not found — building from source (~15-20 min)..."
        build_bolt_from_source
        resolve_bolt_binaries
    fi

    check_tool "${LLVM_BOLT}"; check_tool "${MERGE_FDATA}"
    require_llvm_mingw

    # Build ELF if not present or if compile flags changed
    stage_build_elf

    local elf_binary="${BUILD_USE_ELF}/bin/citron"
    [[ -f "$elf_binary" ]] \
        || error "ELF binary not found: ${elf_binary}"

    mkdir -p "${BOLT_PROFILE_DIR}" "${BUILD_BOLT}"

    local instrumented="${BUILD_BOLT}/citron-bolt-instrumented"
    local fdata_pattern="${BOLT_PROFILE_DIR}/citron-%p.fdata"
    local merged_fdata="${BOLT_PROFILE_DIR}/citron-merged.fdata"
    local optimized_elf="${BUILD_BOLT}/citron-bolt-optimized"

    # Ensure the BOLT runtime library is present before instrumenting.
    if [[ ! -f /usr/local/lib/libbolt_rt_instr.a ]]; then
        local _bolt_build="/tmp/llvm-bolt-${CLANG_VERSION}-build"
        if [[ -f "${_bolt_build}/lib/libbolt_rt_instr.a" ]]; then
            info "Installing BOLT runtime from existing build..."
            _sudo cp "${_bolt_build}/lib/libbolt_rt_instr.a"  /usr/local/lib/libbolt_rt_instr.a
            _sudo cp "${_bolt_build}/lib/libbolt_rt_hugify.a" /usr/local/lib/libbolt_rt_hugify.a 2>/dev/null || true
        elif [[ -d "${_bolt_build}" ]]; then
            info "Building BOLT runtime from existing build tree..."
            cmake --build "${_bolt_build}" --target bolt_rt -j "${JOBS}" \
                || error "BOLT runtime build failed"
            _sudo cp "${_bolt_build}/lib/libbolt_rt_instr.a"  /usr/local/lib/libbolt_rt_instr.a
            _sudo cp "${_bolt_build}/lib/libbolt_rt_hugify.a" /usr/local/lib/libbolt_rt_hugify.a 2>/dev/null || true
        else
            info "No cached BOLT build found — building from source (this takes ~15 min)..."
            build_bolt_from_source
        fi
    fi

    info "Instrumenting ELF with BOLT..."
    "${LLVM_BOLT}" "${elf_binary}" \
        --instrument \
        --instrumentation-file="${fdata_pattern}" \
        --instrumentation-file-append-pid \
        -o "${instrumented}"
    success "Instrumented: ${instrumented}"

    echo ""
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    echo -e "${YELLOW}  Run BOLT-Instrumented Binary (native Linux ELF)${RESET}"
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    echo ""
    echo "    ${instrumented}"
    echo ""
    echo "  Play for 15-30 min. Exit cleanly. fdata files go to:"
    echo "    ${BOLT_PROFILE_DIR}/"
    echo ""
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    read -rp "  Press Enter once you have exited the instrumented binary... "
    echo ""

    # ── 3b. Merge .fdata ─────────────────────────────────────────────────────
    local fdata_count
    fdata_count=$(find "${BOLT_PROFILE_DIR}" -name "*.fdata" 2>/dev/null | wc -l)
    [[ "$fdata_count" -gt 0 ]] || error "No .fdata files in ${BOLT_PROFILE_DIR}"
    info "Merging ${fdata_count} .fdata file(s)..."
    "${MERGE_FDATA}" "${BOLT_PROFILE_DIR}"/*.fdata -o "${merged_fdata}"
    success "Merged: ${merged_fdata}"

    # ── 3c. Optimize ELF ─────────────────────────────────────────────────────
    info "Optimizing ELF with BOLT..."
    local bolt_log
    bolt_log="$(mktemp /tmp/citron-bolt-opt.XXXXXX.log)"
    "${LLVM_BOLT}" "${elf_binary}" \
        -p "${merged_fdata}" \
        --reorder-blocks=ext-tsp \
        --reorder-functions=cdsort \
        --split-functions \
        --split-all-cold \
        --split-eh \
        --dyno-stats \
        -o "${optimized_elf}" 2>&1 | tee "${bolt_log}"
    # tee exits 0 even if BOLT fails — check the output file was actually produced
    [[ -f "${optimized_elf}" ]] || error "BOLT optimization failed (see ${bolt_log})"
    success "BOLT-optimized ELF: ${optimized_elf}"

    # Preserve the BOLT-optimized ELF in a permanent location so it
    # survives subsequent bolt re-runs that wipe BUILD_BOLT.
    local elf_output="${BUILD_ROOT}/citron-bolt-optimized"
    cp "${optimized_elf}" "${elf_output}"
    success "ELF preserved: ${elf_output}"

    # ── Extract BOLT function order for PE linker ─────────────────────────────
    # BOLT can't rewrite PE/COFF directly. Instead, extract the hot function order
    # from the optimized ELF (symbols sorted by address in .text) and pass it to
    # lld via /order:@ when relinking the PE.
    local order_file="${BUILD_ROOT}/bolt-function-order.txt"
    info "Extracting BOLT function order from optimized ELF..."

    local nm_tool
    nm_tool="$(command -v "llvm-nm-${CLANG_VERSION}" 2>/dev/null \
               || command -v llvm-nm 2>/dev/null \
               || command -v nm 2>/dev/null || true)"
    [[ -n "${nm_tool}" ]] || error "No nm tool found — cannot extract BOLT function order"

    python3 - "${optimized_elf}" "${order_file}" "${nm_tool}" "${bolt_log:-}" << 'BOLT_ORDER_EOF'
import sys, subprocess, re

elf_path   = sys.argv[1]
order_path = sys.argv[2]
nm_tool    = sys.argv[3]
bolt_log   = sys.argv[4] if len(sys.argv) > 4 else ""

# ── 1. Parse __hot_start / __hot_end from the saved BOLT log ─────────────
# BOLT always prints these during optimisation:
#   BOLT-INFO: setting __hot_start to 0x...
#   BOLT-INFO: setting __hot_end   to 0x...
# This is far more reliable than post-hoc symbol table parsing (nm
# silently drops SHN_ABS symbols in PIE binaries; readelf regex can
# be fragile across LLVM versions).
hot_start = None
hot_end   = None

if bolt_log:
    try:
        hs_re = re.compile(r'BOLT-INFO: setting __hot_start to (0x[0-9a-fA-F]+)')
        he_re = re.compile(r'BOLT-INFO: setting __hot_end to (0x[0-9a-fA-F]+)')
        with open(bolt_log) as fh:
            for line in fh:
                if hot_start is None:
                    m = hs_re.search(line)
                    if m:
                        hot_start = int(m.group(1), 16)
                if hot_end is None:
                    m = he_re.search(line)
                    if m:
                        hot_end = int(m.group(1), 16)
                if hot_start is not None and hot_end is not None:
                    break
    except OSError:
        pass  # log file gone — fall through to fallback

# ── 2. Collect text symbols via nm --numeric-sort ─────────────────────────
nm_result = subprocess.run(
    [nm_tool, "--defined-only", "--numeric-sort", "--format=posix", elf_path],
    capture_output=True, text=True
)
if nm_result.returncode != 0:
    print("  nm failed: " + nm_result.stderr[:200])
    sys.exit(1)

# Strip BOLT internals, cold clones, LTO-local hashes, and SDL internals:
#   __BOLT_*      -- BOLT instrumentation/padding artifacts
#   *.cold[.N]    -- cold halves of split functions (placed after __hot_end)
#   __COLD_*      -- BOLT cold-region labels
#   .llvm.<hash>  -- ThinLTO-internalized copies (hash differs per build)
#   SDL_*_REAL    -- Linux SDL2 internal dispatch symbols absent in Windows SDL2.dll
skip = re.compile(
    r'^__BOLT_'
    r'|\.cold(?:\.\d+)?$'
    r'|^__COLD_'
    r'|\.llvm\.\d+$'
    r'|^SDL_\w+_REAL$'
)

text_syms = []   # list of (addr, name)
for line in nm_result.stdout.splitlines():
    parts = line.split()
    if len(parts) < 3:
        continue
    name, typ, val_str = parts[0], parts[1], parts[2]
    if typ not in ('T', 't'):
        continue
    if skip.search(name):
        continue
    try:
        addr = int(val_str, 16)
    except ValueError:
        continue
    text_syms.append((addr, name))

# ── 3. Filter to hot segment ──────────────────────────────────────────────
if hot_start is not None and hot_end is not None and hot_start < hot_end:
    hot_kb = (hot_end - hot_start) // 1024
    print(f"  Hot segment : 0x{hot_start:08x} - 0x{hot_end:08x}  ({hot_kb:,} KiB)")
    hot_syms  = [(a, n) for a, n in text_syms if hot_start <= a < hot_end]
    cold_syms = len(text_syms) - len(hot_syms)
    print(f"  Hot symbols : {len(hot_syms):,} of {len(text_syms):,} total text symbols")
    print(f"  Cold/other  : {cold_syms:,} excluded (inlined by ThinLTO in PE -> LNK4037 noise)")
    symbols = [n for _, n in hot_syms]
else:
    print("  WARNING: could not determine hot segment boundaries from BOLT log.")
    print(f"  Falling back to all {len(text_syms):,} text symbols -- expect more LNK4037 warnings.")
    symbols = [n for _, n in text_syms]

if not symbols:
    print("  WARNING: no text symbols extracted -- /order file will be empty")
    sys.exit(0)

with open(order_path, 'w') as f:
    f.write('\n'.join(symbols) + '\n')

print(f"  Wrote {len(symbols)} symbols to {order_path}")
BOLT_ORDER_EOF

    local order_count=0
    [[ -f "${order_file}" ]] && order_count=$(wc -l < "${order_file}")
    if [[ "${order_count}" -gt 0 ]]; then
        success "BOLT order file: ${order_file} (${order_count} functions)"
    else
        warn "BOLT order file is empty — PE relink will proceed without function ordering"
        order_file=""
    fi

    # ── Re-link Windows PE with BOLT function order ────────────────────────────
    info "Re-linking final Windows PE (PGO + LTO + BOLT function order)..."
    rm -rf "${BUILD_BOLT}"
    mkdir -p "${BUILD_BOLT}"; cd "${BUILD_BOLT}"

    local debug_flag=""
    # -gcodeview: clang defaults to DWARF on x86_64-w64-mingw32; without it no .pdb is emitted.
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g -gcodeview"
    local linker_debug_flag=""
    # --pdb= / --threads=1: same rationale as stage_use — MinGW lld driver syntax,
    # prevents LLD COFF deadlock between PDB merge and LTO backend threads.
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && linker_debug_flag="-Wl,--pdb= -Wl,--threads=1"
    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
    local lto_flag; lto_flag="$(lto_clang_flag)"
    local _bolt_merged="${PROFILE_DIR}/merged.profdata"
    local profdata
    [[ -f "${_bolt_merged}" ]] && profdata="${_bolt_merged}" || profdata="${PROFILE_DIR}/default.profdata"
    local pgo_flag
    if [[ "${PGO_MODE}" == "ir" ]]; then
        pgo_flag="-fprofile-use=\"${profdata}\""
    else
        pgo_flag="-fprofile-instr-use=\"${profdata}\" -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
    fi
    local lto_pgo_flag="${lto_flag:+${lto_flag} }${pgo_flag}"

    # /order:@<file>: hot function placement in .text. /ignore:4037 suppresses
    # "missing symbol" warnings for entries absent from a given binary (inlined by LTO, etc.).
    # -Xlink=: passthrough to lld-link — MinGW driver doesn't recognize order/ignore directly.
    local order_linker_flag=""
    if [[ -n "${order_file}" ]]; then
        order_linker_flag="-Wl,-Xlink=/order:@${order_file} -Wl,-Xlink=/ignore:4037"
    fi

    local qt_install_dir="${CPM_SOURCE_CACHE}/qt-bin/6.9.3/llvm-mingw_64"
    local qt_host_dir="${CPM_SOURCE_CACHE}/qt-bin-host/6.9.3/gcc_64"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        qt_host_dir=""
    fi
    local qt6_cmake_dir="${qt_install_dir}/lib/cmake/Qt6"

    # Pre-build FFmpeg for this build directory
    detect_ffmpeg_version

    # shellcheck disable=SC2034  # _CMAKE_ARGS used via array expansion below
    build_common_cmake_args
    _CMAKE_ARGS+=(
        "-DCITRON_ENABLE_PGO_USE=ON"
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON"
        "-DCMAKE_C_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}"
        "-DCMAKE_CXX_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}"
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}${order_linker_flag:+ ${order_linker_flag}} ${linker_debug_flag}"
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}"
    )
    [[ -n "${qt6_cmake_dir}" ]] && _CMAKE_ARGS+=("-DQt6_DIR=${qt6_cmake_dir}")
    [[ -n "${qt_host_dir}"   ]] && _CMAKE_ARGS+=("-DQT_HOST_PATH=${qt_host_dir}")
    cmake "${SOURCE_DIR}" "${_CMAKE_ARGS[@]}" \
        || error "CMake configure failed"

    info "Building final optimized Windows PE (PGO + LTO + BOLT function order, ${BUILD_TYPE})..."
    cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}"

    deploy_runtime_dlls \
        "${BUILD_BOLT}/bin" \
        "${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64" \
        "${BUILD_BOLT}"

    # Cross-reference order file against citron.exe symbol table to report agreement rate.
    if [[ -n "${order_file}" && -f "${BUILD_BOLT}/bin/citron.exe" ]]; then
        local elf_lto_used="${LTO_MODE}"
        python3 - "${order_file}" "${BUILD_BOLT}/bin/citron.exe" "${nm_tool}" "${LTO_MODE}" "${elf_lto_used}" << 'BOLT_SUMMARY_EOF'
import sys, subprocess, re

order_path = sys.argv[1]
exe_path   = sys.argv[2]
nm_tool    = sys.argv[3]
lto_mode     = sys.argv[4] if len(sys.argv) > 4 else "unknown"
elf_lto_mode = sys.argv[5] if len(sys.argv) > 5 else "thin"

# Resolve actual LTO used in the bolt PE re-link:
#   full  → -flto   (whole-program LTO; most inlining → most "missing" hot symbols)
#   thin  → -flto=thin
#   none  → no LTO
lto_label = {
    "full":    "Full LTO (-flto)",
    "thin":    "ThinLTO (-flto=thin)",
    "none":    "No LTO",
}.get(lto_mode, f"unknown ({lto_mode})")
elf_lto_label = {
    "full": "Full LTO (-flto)",
    "thin": "ThinLTO (-flto=thin)",
    "none": "No LTO",
}.get(elf_lto_mode, f"unknown ({elf_lto_mode})")

with open(order_path) as f:
    hot_syms = set(l.strip() for l in f if l.strip())

result = subprocess.run(
    [nm_tool, "--defined-only", "--format=posix", exe_path],
    capture_output=True, text=True
)

pe_syms = set()
for line in result.stdout.splitlines():
    parts = line.split()
    if len(parts) >= 2 and parts[1] in ('T', 't'):
        pe_syms.add(parts[0])

matched   = hot_syms & pe_syms
missed    = hot_syms - pe_syms
total_hot = len(hot_syms)
pct       = 100.0 * len(matched) / total_hot if total_hot else 0.0

W  = "[1;37m"   # bold white
G  = "[1;32m"   # bold green
Y  = "[1;33m"   # bold yellow
C  = "[1;36m"   # bold cyan
R  = "[0m"      # reset
BAR_W = 40

filled = round(BAR_W * len(matched) / total_hot) if total_hot else 0
bar    = "█" * filled + "░" * (BAR_W - filled)

absent_reason = "Inlined by LTO (absent)" if lto_mode == "none" else f"Inlined by {lto_label.split()[0]} (absent)"

# Build each content string at exactly IW visible chars before adding ANSI
# codes, so ║ delimiters always align regardless of color escape widths.
IW = 60

def pad(s, w=IW):
    return s[:w].ljust(w)

pe_lto_str  = f"  PE  LTO (bolt re-link) : {lto_label}"
elf_lto_str = f"  ELF LTO (BOLT source)  : {elf_lto_label}"
hot_str     = f"  Hot functions in order file  : {total_hot:>7,}"
match_str   = f"  Successfully reordered       : {len(matched):>7,}  ({pct:5.1f}%)"
miss_str    = f"  {absent_reason:<30}: {len(missed):>7,}  ({100-pct:5.1f}%)"
bar_str     = f"  [{bar}] {pct:.1f}%"
bar_pad     = " " * max(0, IW - len(bar_str))

print()
print(f"{C}  ╔════════════════════════════════════════════════════════════╗{R}")
print(f"{C}  ║{R}{pad(chr(32)*8 + "BOLT Function Reorder — citron.exe Summary")}{C}║{R}")
print(f"{C}  ╠════════════════════════════════════════════════════════════╣{R}")
print(f"{C}  ║{R}{W}{pad(pe_lto_str)}{R}{C}║{R}")
print(f"{C}  ║{R}{W}{pad(elf_lto_str)}{R}{C}║{R}")
print(f"{C}  ╠════════════════════════════════════════════════════════════╣{R}")
print(f"{C}  ║{R}{W}{pad(hot_str)}{R}{C}║{R}")
print(f"{C}  ║{R}{G}{pad(match_str)}{R}{C}║{R}")
print(f"{C}  ║{R}{Y}{pad(miss_str)}{R}{C}║{R}")
print(f"{C}  ║{R}{pad("")}{C}║{R}")
print(f"{C}  ║{R}  [{G}{bar}{R}] {G}{pct:.1f}%{R}{bar_pad}{C}║{R}")
print(f"{C}  ╚════════════════════════════════════════════════════════════╝{R}")
print()
BOLT_SUMMARY_EOF
    fi

    success "════════════════════════════════════════════════════════════════"
    success "  Final binary: ${BUILD_BOLT}/bin/citron.exe"
    local _bolt_pgo_label
    if [[ -f "${PROFILE_DIR}/merged.profdata" && "${profdata}" == "${PROFILE_DIR}/merged.profdata" ]]; then
        _bolt_pgo_label="CS-IRPGO (-fprofile-use=merged.profdata)"
    elif [[ "${PGO_MODE}" == "ir" ]]; then
        _bolt_pgo_label="IR PGO (-fprofile-use)"
    else
        _bolt_pgo_label="${PGO_MODE} (-fprofile-instr-use)"
    fi
    success "  Optimizations: PGO (${_bolt_pgo_label}) + LTO + BOLT (function reordering)"
    success "════════════════════════════════════════════════════════════════"
}

# =============================================================================
# ensure_create_llvm_prof — build generate_propeller_profiles from
# google/llvm-propeller (autofdo moved there 2025Q1). Self-contained cmake
# with FetchContent; understands BBAddrMap v3 (Clang 19+).
# Interface: --cc_profile / --ld_profile
# Rebuilt automatically on Clang version change.
# =============================================================================
ensure_create_llvm_prof() {
    local src_dir="/tmp/propeller-src"
    local build_dir="/tmp/propeller-build"
    local install_bin="/usr/local/bin/create_llvm_prof"
    local ver_sentinel="/usr/local/bin/.create_llvm_prof_llvm_ver"
    local clang_ver
    clang_ver=$("${CLANG}" --version 2>&1 | head -1 || echo "unknown")

    if command -v create_llvm_prof &>/dev/null; then
        local stored_ver=""
        [[ -f "${ver_sentinel}" ]] && stored_ver=$(cat "${ver_sentinel}" 2>/dev/null || true)
        if [[ "${clang_ver}" == "${stored_ver}" ]]; then
            info "create_llvm_prof already installed and up-to-date: $(command -v create_llvm_prof)"
            return 0
        else
            warn "create_llvm_prof version mismatch — rebuilding."
            _sudo rm -f "${install_bin}" "${ver_sentinel}"
            rm -rf "${build_dir}" "${src_dir}"
        fi
    fi

    info "Building create_llvm_prof from google/llvm-propeller..."

    # Dependencies per google/llvm-propeller README
    local _missing=()
    dpkg -s libelf-dev  &>/dev/null 2>&1 || _missing+=(libelf-dev)
    dpkg -s libssl-dev  &>/dev/null 2>&1 || _missing+=(libssl-dev)
    dpkg -s libzstd-dev &>/dev/null 2>&1 || _missing+=(libzstd-dev)
    if [[ ${#_missing[@]} -gt 0 ]]; then
        info "Installing: ${_missing[*]}"
        _sudo apt-get install -y "${_missing[@]}"             || error "Failed to install dependencies"
    fi

    if [[ ! -d "${src_dir}/.git" ]]; then
        info "Cloning google/llvm-propeller..."
        git clone             --depth=1             https://github.com/google/llvm-propeller.git             "${src_dir}"             || error "Failed to clone google/llvm-propeller"
        success "llvm-propeller cloned"
    else
        info "Cached llvm-propeller clone found at ${src_dir}"
    fi

    info "Configuring llvm-propeller cmake..."
    rm -rf "${build_dir}"
    CC="${CLANG}" CXX="${CLANGPP}"     cmake -S "${src_dir}" -B "${build_dir}"         -G Ninja         -DCMAKE_BUILD_TYPE=${BUILD_TYPE}         || error "llvm-propeller cmake configure failed"

    info "Building generate_propeller_profiles (~15-30 min)..."
    cmake --build "${build_dir}" --target generate_propeller_profiles -j "${JOBS}"         || error "llvm-propeller build failed"

    local built_bin="${build_dir}/propeller/generate_propeller_profiles"
    [[ -f "${built_bin}" ]]         || error "Built binary not found at ${built_bin}"

    _sudo cp "${built_bin}" "${install_bin}"
    _sudo chmod +x "${install_bin}"
    printf '%s' "${clang_ver}" | _sudo tee "${ver_sentinel}" > /dev/null

    command -v create_llvm_prof &>/dev/null         || error "create_llvm_prof installation failed"
    success "create_llvm_prof installed: ${install_bin}"
}

# =============================================================================
# stage_propeller — Propeller BB+function layout optimization via perf LBR.
#
# Collects branch-stack profiles from the native Linux ELF, then generates:
#   propeller_cc.prof       — BB layout list (-fbasic-block-sections=list=)
#   propeller_symorder.txt  — hot function order (/order:@ for PE)
# The Windows PE is rebuilt with PGO+LTO plus both Propeller profiles.
#
# Hardware: AMD Zen 4 (BRBS, kernel 6.1+) or Intel 13th gen (LBR) — both use
# perf -b. Profile collection uses the same ELF as the BOLT stage.
#
# Note: -fbasic-block-sections=list= for PE/COFF is compiler-level BB layout;
# COFF section granularity is coarser than ELF but still provides i-cache gains.
# The function-order benefit from propeller_symorder.txt is identical to BOLT's.
# =============================================================================
stage_propeller() {
    if [[ "${_HOST_OS}" == "windows" ]]; then
        error "Propeller requires a Linux host (perf LBR + ELF target). Not supported on Windows/MSYS2."
    fi
    header "Stage: Propeller Basic-Block Profile Optimization"

    check_tool "${CLANG}"; check_tool "${CLANGPP}"
    check_tool "ninja";    check_tool "cmake"
    check_tool "perf"

    ensure_create_llvm_prof
    require_llvm_mingw

    # Build ELF if not present or if compile flags changed
    stage_build_elf

    local elf_binary="${BUILD_USE_ELF}/bin/citron"
    [[ -f "${elf_binary}" ]] \
        || error "ELF binary not found: ${elf_binary}"

    # Verify the ELF was built with -fbasic-block-address-map
    # by checking for the .llvm_bb_addr_map section it emits
    if ! "${LLVM_MINGW_DIR}/bin/llvm-readelf" --sections "${elf_binary}" \
            2>/dev/null | grep -q '\.llvm_bb_addr_map'; then
        # Fallback: use system readelf
        if ! readelf --sections "${elf_binary}" 2>/dev/null | grep -q '\.llvm_bb_addr_map'; then
            warn "ELF does not contain a .llvm_bb_addr_map section."
            warn "The ELF may have been built with an older version of this script."
            warn "Re-run './build-clangtron-windows.sh build-elf' to rebuild the ELF with BB labels."
            warn "Propeller will still produce a function-order profile but no BB layout."
        fi
    else
        success "ELF has .llvm_bb_addr_map section — BB-level profiling available"
    fi

    mkdir -p "${PROPELLER_PROFILE_DIR}" "${BUILD_PROPELLER}/bin"

    local perf_data="${PROPELLER_PROFILE_DIR}/perf.data"
    local cc_profile="${PROPELLER_PROFILE_DIR}/propeller_cc.prof"
    local symorder="${PROPELLER_PROFILE_DIR}/propeller_symorder.txt"

    # ── 1. Profile collection ─────────────────────────────────────────────────
    # If perf.data already exists, verify its build ID matches the current ELF.
    # A mismatch means the ELF was rebuilt since the profile was collected — the
    # old perf.data is useless and must be discarded before re-collecting.
    if [[ -f "${perf_data}" ]]; then
        local _elf_buildid _perf_buildids
        _elf_buildid=$(readelf -n "${elf_binary}" 2>/dev/null             | grep -oP '(?<=Build ID: )[0-9a-f]+' | head -1 || true)
        _perf_buildids=$(perf buildid-list -i "${perf_data}" 2>/dev/null             | awk '{print $1}' || true)
        if [[ -n "${_elf_buildid}" ]] &&            ! grep -qF "${_elf_buildid}" <<< "${_perf_buildids}"; then
            warn "perf.data build ID does not match the current ELF."
            warn "  ELF build ID:  ${_elf_buildid}"
            warn "  perf.data has: $(head -1 <<< "${_perf_buildids}") (first entry)"
            warn "The ELF was rebuilt since the profile was collected."
            info "Deleting stale perf.data — re-collection required."
            rm -f "${perf_data}"
        else
            info "Found existing perf.data: ${perf_data}"
            info "Build ID verified — skipping collection."
        fi
    fi
    if [[ ! -f "${perf_data}" ]]; then
        # ── Hardware / kernel capability checks ─────────────────────────────
        # 1. perf_event_paranoid: branch stacks require <= 1
        local paranoid
        paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "unknown")
        if [[ "${paranoid}" != "unknown" ]] && [[ "${paranoid}" -gt 1 ]]; then
            warn "perf_event_paranoid=${paranoid} — branch stack sampling requires <= 1"
            info "Fixing automatically with: _sudo sysctl kernel.perf_event_paranoid=1"
            _sudo sysctl -w kernel.perf_event_paranoid=1 \
                || error "Could not set perf_event_paranoid=1 — run manually:\n       _sudo sysctl kernel.perf_event_paranoid=1"
            success "perf_event_paranoid set to 1"
            info "To make permanent: echo 'kernel.perf_event_paranoid=1' | _sudo tee -a /etc/sysctl.conf"
        else
            success "perf_event_paranoid=${paranoid} (OK)"
        fi

        # 2. Kernel version: AMD BRBS requires 6.1+, Intel LBR works on any modern kernel
        local kernel_ver kernel_maj kernel_min
        kernel_ver=$(uname -r)
        kernel_maj=$(echo "${kernel_ver}" | cut -d. -f1)
        kernel_min=$(echo "${kernel_ver}" | cut -d. -f2 | cut -d- -f1)
        if [[ "${kernel_maj}" -lt 6 ]] || { [[ "${kernel_maj}" -eq 6 ]] && [[ "${kernel_min}" -lt 1 ]]; }; then
            warn "Kernel ${kernel_ver} is older than 6.1 — AMD BRBS branch stack support"
            warn "requires kernel 6.1+. Intel LBR still works on older kernels."
            warn "If perf fails below, upgrade your kernel and retry."
        else
            success "Kernel ${kernel_ver} >= 6.1 (branch stack support OK)"
        fi

        # 3. Verify perf can actually record branch stacks on this hardware.
        #    A 0.1-second test capture confirms the hardware/driver supports -b.
        info "Testing perf branch-stack capability on this hardware..."
        if ! perf record -b -e cycles:u -o /tmp/citron-perf-captest.data \
                -- sleep 0.1 >/dev/null 2>&1; then
            error "perf -b (branch stack recording) is not supported on this hardware/kernel.\n" \
                  "       Propeller requires branch stacks for BB-level profile data.\n" \
                  "       AMD: ensure kernel >= 6.1 and amd_iommu=off is not set.\n" \
                  "       Intel: ensure MSR access is not blocked (no nolbr boot flag)."
        fi
        rm -f /tmp/citron-perf-captest.data
        success "perf branch-stack recording works on this hardware"

        echo ""
        echo -e "${YELLOW}╔══════════════════════════════════════════════════════════════════╗${RESET}"
        echo -e "${YELLOW}║         Propeller — Branch Profile Collection                    ║${RESET}"
        echo -e "${YELLOW}╠══════════════════════════════════════════════════════════════════╣${RESET}"
        echo ""
        echo -e "${CYAN}  Run the following commands to collect a branch-stack profile:${RESET}"
        echo ""
        echo "    cd ${elf_binary%/*}"
        echo "    perf record -b -e cycles:u \\"
        echo "        -o ${perf_data} \\"
        echo "        -- ${elf_binary}"
        echo ""
        echo "  Play games / navigate menus for 15-30 minutes."
        echo "  Exit citron cleanly (File > Exit or Ctrl+Q)."
        echo "  perf writes ${perf_data} on exit."
        echo ""
        echo -e "${CYAN}  If citron fails to display (no GUI available):${RESET}"
        echo "    Run from a desktop session, or set DISPLAY=:0 before the command."
        echo ""
        echo -e "${YELLOW}╚══════════════════════════════════════════════════════════════════╝${RESET}"
        echo ""
        read -rp "  Press Enter once perf has finished and perf.data is written... "
        echo ""

        [[ -f "${perf_data}" ]] \
            || error "perf.data not found at ${perf_data}\n" \
                     "       Run the perf command above, then re-run this stage."
    fi

    # ── 2. Convert perf.data to Propeller profiles ────────────────────────────
    # --cc_profile = BB layout, --ld_profile = function order
    info "Converting perf branch data to Propeller profiles..."
    info "  Binary:    ${elf_binary}"
    info "  Input:     ${perf_data}"
    info "  CC prof:   ${cc_profile}"
    info "  LD prof:   ${symorder}"
    echo ""

    set +e
    create_llvm_prof \
        --binary="${elf_binary}" \
        --profile="${perf_data}" \
        --cc_profile="${cc_profile}" \
        --ld_profile="${symorder}" \
        2>&1
    local clp_exit=$?
    set -e

    if [[ ${clp_exit} -ne 0 ]]; then
        warn "generate_propeller_profiles exited ${clp_exit}."
        warn "Common causes:"
        warn "  - perf.data was collected without -b (branch stacks required)"
        warn "  - Binary mismatch: perf.data collected on a different build"
        warn "  - ELF has no .llvm_bb_addr_map: re-run build-elf and re-collect"
        error "Propeller profile conversion failed"
    fi

    if [[ ! -f "${cc_profile}" ]] && [[ ! -f "${symorder}" ]]; then
        error "create_llvm_prof produced no output files — check perf.data validity"
    fi

    local have_bb=0; local have_sym=0
    [[ -f "${cc_profile}" ]] && have_bb=1 \
        && success "CC profile (BB layout):   ${cc_profile} ($(wc -l < "${cc_profile}") entries)"
    [[ -f "${symorder}" ]] && have_sym=1 \
        && success "Symbol order (fn layout): ${symorder} ($(wc -l < "${symorder}") functions)"

    if [[ ${have_bb} -eq 0 ]]; then
        warn "No CC profile produced — BB-level layout unavailable."
        warn "Function ordering via symorder will still be applied if present."
    fi

    # ── 3. Rebuild Windows PE with Propeller profiles ─────────────────────────
    info "Rebuilding Windows PE with Propeller profiles (PGO + LTO + Propeller)..."
    rm -rf "${BUILD_PROPELLER}"
    mkdir -p "${BUILD_PROPELLER}"; cd "${BUILD_PROPELLER}"

    local debug_flag=""
    # -gcodeview: clang defaults to DWARF on x86_64-w64-mingw32; without it no .pdb is emitted.
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g -gcodeview"
    local linker_debug_flag=""
    # --pdb= / --threads=1: same rationale as stage_use.
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && linker_debug_flag="-Wl,--pdb= -Wl,--threads=1"
    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
    local lto_flag; lto_flag="$(lto_clang_flag)"
    local _prop_merged="${PROFILE_DIR}/merged.profdata"
    local profdata
    [[ -f "${_prop_merged}" ]] && profdata="${_prop_merged}" || profdata="${PROFILE_DIR}/default.profdata"
    local pgo_flag
    if [[ "${PGO_MODE}" == "ir" ]]; then
        pgo_flag="-fprofile-use=\"${profdata}\""
    else
        pgo_flag="-fprofile-instr-use=\"${profdata}\" -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
    fi
    local lto_pgo_flag="${lto_flag:+${lto_flag} }${pgo_flag}"

    # -fbasic-block-sections=list=<cc_profile>: compiler splits listed BBs into
    # separate COFF sections; lld orders them per symorder.
    # /order:@ + /ignore:4037: function placement via -Xlink= passthrough (MinGW
    # lld driver doesn't recognize order/ignore directly; -Xlink passes to lld-link).
    local propeller_linker_flag=""
    if [[ ${have_sym} -eq 1 ]]; then
        propeller_linker_flag="-Wl,-Xlink=/order:@${symorder} -Wl,-Xlink=/ignore:4037"
    fi

    local qt_install_dir="${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64"
    local qt_host_dir="${BUILD_GENERATE}/externals/qt-host/6.9.3/gcc_64"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        qt_host_dir=""
    fi
    local qt6_cmake_dir="${qt_install_dir}/lib/cmake/Qt6"

    detect_ffmpeg_version

    # shellcheck disable=SC2034  # _CMAKE_ARGS used via array expansion below
    build_common_cmake_args
    _CMAKE_ARGS+=(
        "-DCITRON_ENABLE_PGO_USE=ON"
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON"
        "-DCMAKE_C_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}"
        "-DCMAKE_CXX_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}"
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}${propeller_linker_flag:+ ${propeller_linker_flag}} ${linker_debug_flag}"
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}"
    )
    [[ -n "${qt6_cmake_dir}" ]] && _CMAKE_ARGS+=("-DQt6_DIR=${qt6_cmake_dir}")
    [[ -n "${qt_host_dir}"   ]] && _CMAKE_ARGS+=("-DQT_HOST_PATH=${qt_host_dir}")
    cmake "${SOURCE_DIR}" "${_CMAKE_ARGS[@]}" \
        || error "CMake configure failed"
    info "Building Propeller-optimized Windows PE (${BUILD_TYPE})..."
    cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}"

    deploy_runtime_dlls \
        "${BUILD_PROPELLER}/bin" \
        "${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64" \
        "${BUILD_PROPELLER}"

    # ── Agreement metric: how many symorder functions survived into the PE ──────
    local nm_tool
    if command -v "llvm-nm-${CLANG_VERSION}" &>/dev/null; then
        nm_tool="llvm-nm-${CLANG_VERSION}"
    elif command -v llvm-nm &>/dev/null; then
        nm_tool="llvm-nm"
    else
        nm_tool=""
    fi

    if [[ -n "${nm_tool}" && -f "${symorder}" && -f "${BUILD_PROPELLER}/bin/citron.exe" ]]; then
        python3 - "${symorder}" "${BUILD_PROPELLER}/bin/citron.exe" "${nm_tool}"             "${LTO_MODE}" << 'PROPELLER_SUMMARY_EOF'
import sys, subprocess, re

symorder_path = sys.argv[1]
exe_path      = sys.argv[2]
nm_tool       = sys.argv[3]
lto_mode      = sys.argv[4] if len(sys.argv) > 4 else "full"

lto_label = {
    "full": "Full LTO (-flto)",
    "thin": "ThinLTO (-flto=thin)",
    "none": "No LTO",
}.get(lto_mode, f"unknown ({lto_mode})")

with open(symorder_path) as f:
    # Each line is a mangled function name
    hot_syms = set(l.strip() for l in f if l.strip())

result = subprocess.run(
    [nm_tool, "--defined-only", "--format=posix", exe_path],
    capture_output=True, text=True
)

pe_syms = set()
for line in result.stdout.splitlines():
    parts = line.split()
    if len(parts) >= 2 and parts[1] in ("T", "t"):
        pe_syms.add(parts[0])

matched   = hot_syms & pe_syms
missed    = hot_syms - pe_syms
total_hot = len(hot_syms)
pct       = 100.0 * len(matched) / total_hot if total_hot else 0.0

W  = "[1;37m"
G  = "[1;32m"
Y  = "[1;33m"
C  = "[1;36m"
R  = "[0m"
BAR_W = 40
IW    = 60

filled = round(BAR_W * len(matched) / total_hot) if total_hot else 0
bar    = "█" * filled + "░" * (BAR_W - filled)

def pad(s, w=IW):
    return s[:w].ljust(w)

lto_str   = f"  PE LTO (propeller rebuild) : {lto_label}"
hot_str   = f"  Hot functions in symorder  : {total_hot:>7,}"
match_str = f"  Reordered in PE            : {len(matched):>7,}  ({pct:5.1f}%)"
miss_str  = f"  Inlined/absent by LTO      : {len(missed):>7,}  ({100-pct:5.1f}%)"
bar_str   = f"  [{bar}] {pct:.1f}%"
bar_pad   = " " * max(0, IW - len(bar_str))

print()
print(f"{C}  ╔════════════════════════════════════════════════════════════╗{R}")
print(f"{C}  ║{R}{pad('        Propeller Function Reorder — citron.exe Summary')}{C}║{R}")
print(f"{C}  ╠════════════════════════════════════════════════════════════╣{R}")
print(f"{C}  ║{R}{W}{pad(lto_str)}{R}{C}║{R}")
print(f"{C}  ╠════════════════════════════════════════════════════════════╣{R}")
print(f"{C}  ║{R}{W}{pad(hot_str)}{R}{C}║{R}")
print(f"{C}  ║{R}{G}{pad(match_str)}{R}{C}║{R}")
print(f"{C}  ║{R}{Y}{pad(miss_str)}{R}{C}║{R}")
print(f"{C}  ║{R}{pad('')}{C}║{R}")
print(f"{C}  ║{R}  [{G}{bar}{R}] {G}{pct:.1f}%{R}{bar_pad}{C}║{R}")
print(f"{C}  ╚════════════════════════════════════════════════════════════╝{R}")
print()
PROPELLER_SUMMARY_EOF
    fi

    echo ""
    success "════════════════════════════════════════════════════════════════"
    success "  Stage propeller complete"
    success "  Final binary: ${BUILD_PROPELLER}/bin/citron.exe"
    success "  Optimizations applied:"
    [[ ${have_sym} -eq 1 ]] && success "    Function order:   /order:@ (Propeller LD profile — ${symorder##*/})"
    local _prop_pgo_label
    if [[ -f "${PROFILE_DIR}/merged.profdata" && "${profdata}" == "${PROFILE_DIR}/merged.profdata" ]]; then
        _prop_pgo_label="CS-IRPGO (-fprofile-use=merged.profdata)"
    elif [[ "${PGO_MODE}" == "ir" ]]; then
        _prop_pgo_label="IR PGO (-fprofile-use)"
    else
        _prop_pgo_label="${PGO_MODE} (-fprofile-instr-use)"
    fi
    success "    PGO:              ${_prop_pgo_label}"
    success "    LTO:              $(lto_clang_flag || echo none)"
    success "════════════════════════════════════════════════════════════════"
}


stage_clean() {
    header "Cleaning Build Directories"
    read -rp "This will delete ${BUILD_ROOT}. Are you sure? [y/N] " confirm
    [[ "$confirm" =~ ^[Yy]$ ]] || { info "Aborted."; exit 0; }
    rm -rf "${BUILD_ROOT}"
    success "Build directories removed."
}

print_clangcl_stage_guidance() {
    local stage_name="$1"
    local binary="$2"
    local config="$3"
    local profile_win
    profile_win="$(cygpath -am "${PROFILE_DIR}")"
    local args="--compiler clang-cl --pgo ${PGO_MODE} --lto ${LTO_MODE} --build \"${BUILD_ROOT}\""
    [[ "${UNITY_BUILD}" == "ON" ]] && args="${args} --unity"
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && args="${args} --relwithdebinfo"

    echo ""
    if [[ "${stage_name}" == "use" ]]; then
        echo -e "${GREEN}================================================================${RESET}"
        echo -e "${GREEN}  Stage use complete (native Windows clang-cl)${RESET}"
        echo -e "${GREEN}================================================================${RESET}"
        echo ""
        echo -e "  ${BOLD}Binary  :${RESET} ${binary}"
        echo -e "  ${BOLD}Config  :${RESET} ${config}"
        echo -e "  ${BOLD}PGO     :${RESET} ${PGO_MODE}"
        echo -e "  ${BOLD}LTO     :${RESET} ${LTO_MODE}"
        echo -e "  ${BOLD}Artifacts:${RESET} ${binary%/*}/"
        echo ""
        echo "  Validate runtime deps, startup, gameplay, and clean shutdown."
        echo -e "${GREEN}================================================================${RESET}"
        echo ""
        return
    fi

    local session profile_pattern next_title default_pattern
    if [[ "${stage_name}" == "generate" ]]; then
        session="Session 1"
        profile_pattern="citron-generate-%p.profraw"
        default_pattern="citron-generate-${PGO_MODE}-%p.profraw"
        next_title="Build optimized binary"
    else
        session="Session 2 (context-sensitive IR)"
        profile_pattern="citron-csgenerate-%p.profraw"
        default_pattern="citron-csgenerate-${PGO_MODE}-%p.profraw"
        next_title="Merge stage1 + CS profiles and rebuild"
    fi

    echo -e "${YELLOW}================================================================${RESET}"
    echo -e "${YELLOW}  NEXT STEP: Collect Profile Data (${session})${RESET}"
    echo -e "${YELLOW}================================================================${RESET}"
    echo ""
    echo -e "  ${BOLD}Instrumented binary:${RESET} ${binary}"
    echo -e "  ${BOLD}Profile output dir :${RESET} ${profile_win}/"
    echo ""
    echo "  1. In PowerShell, set the profile destination and launch Citron:"
    echo "       \$env:LLVM_PROFILE_FILE='${profile_win}/${profile_pattern}'"
    echo "       & '${binary}'"
    echo ""
    echo "     (Setting LLVM_PROFILE_FILE is optional but recommended -- it"
    echo "      lets you choose where files land. If you forget it, this"
    echo "      build writes '${default_pattern}' next to citron.exe instead,"
    echo "      with a unique %p-per-process name that won't collide with the"
    echo "      other stage or with previous runs.)"
    echo ""
    echo "  2. Exercise games and menus for 15-30 minutes."
    echo "     Exit cleanly via File > Exit or Ctrl+Q; do not kill the process."
    echo ""
    echo "  3. Confirm ${profile_pattern} files exist under:"
    echo "       ${profile_win}/"
    echo ""
    echo "  4. ${next_title}:"
    if [[ "${stage_name}" == "generate" && "${PGO_MODE}" == "ir" ]]; then
        echo "       # Optional CS-IRPGO pass:"
        echo "       ./build-clangtron-windows.sh csgenerate ${args}"
        echo "       # Or build directly from stage1 profiles:"
    fi
    echo "       ./build-clangtron-windows.sh use ${args}"
    echo ""
    echo -e "${YELLOW}================================================================${RESET}"
    echo ""
}

stage_clangcl() {
    [[ "${_HOST_OS}" == "windows" ]] ||
        error "clang-cl requires a native Windows host."
    [[ "${STAGE}" == "generate" || "${STAGE}" == "csgenerate" || "${STAGE}" == "use" ]] ||
        error "clang-cl supports generate, csgenerate, and use stages."
    [[ "${STAGE}" != "csgenerate" || "${PGO_MODE}" == "ir" ]] ||
        error "clang-cl csgenerate requires --pgo ir."

    # ── Sentinel: record generate config; verify it matches on csgenerate/use ─
    # Mirrors the llvm-mingw sentinel logic so LTO+PGO mismatches are caught
    # before a long build wastes time. The write for STAGE=="generate" happens
    # later, only after the build succeeds, so a failed/invalid generate run
    # never leaves stale sentinel state for csgenerate/use to read.
    #
    # PGO_MODE=="none" is exempt entirely: a --pgo none build (use only --
    # csgenerate always requires --pgo ir, enforced above) consumes no
    # profraw/profdata from generate at all, so there is nothing that could
    # actually be mismatched. It's an independent LTO-only build that should
    # succeed regardless of what PGO mode (or LTO mode) a prior generate run
    # used.
    local _gen_cfg="${BUILD_ROOT}/.citron-clangcl-gen-config"
    if [[ "${STAGE}" != "generate" && "${PGO_MODE}" != "none" && -f "${_gen_cfg}" ]]; then
        local _gen_lto _gen_pgo
        _gen_lto=$(awk -F= '/^LTO=/{print $2; exit}' "${_gen_cfg}" 2>/dev/null || true)
        _gen_pgo=$(awk -F= '/^PGO=/{print $2; exit}' "${_gen_cfg}" 2>/dev/null || true)
        if [[ -n "${_gen_lto}" && "${_gen_lto}" != "${LTO_MODE}" ]]; then
            error "LTO mismatch: generate used LTO=${_gen_lto}, ${STAGE} has LTO=${LTO_MODE}.\n" \
                  "       IR PGO profiles are tied to the IR produced at generate time.\n" \
                  "       Re-run ${STAGE} with --lto ${_gen_lto}."
        fi
        if [[ -n "${_gen_pgo}" && "${_gen_pgo}" != "${PGO_MODE}" ]]; then
            error "PGO mismatch: generate used PGO=${_gen_pgo}, ${STAGE} has PGO=${PGO_MODE}.\n" \
                  "       Re-run ${STAGE} with --pgo ${_gen_pgo}."
        fi
    fi

    local vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
    if [[ -z "${VS_INSTALL_PATH}" ]]; then
        [[ -x "${vswhere}" ]] || error "vswhere.exe not found; install Visual Studio Installer or set VS_INSTALL_PATH."
        VS_INSTALL_PATH="$("${vswhere}" -latest -products '*' \
            -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang \
            -property installationPath | tr -d '\r')"
    fi
    [[ -n "${VS_INSTALL_PATH}" ]] || error "No Visual Studio installation with clang-cl found."

    local vs_root vsdev clang_cl llvm_profdata native_perl native_python
    vs_root="$(cygpath -au "${VS_INSTALL_PATH}")"
    vsdev="${vs_root}/Common7/Tools/VsDevCmd.bat"
    clang_cl="${vs_root}/VC/Tools/Llvm/x64/bin/clang-cl.exe"
    llvm_profdata="${vs_root}/VC/Tools/Llvm/x64/bin/llvm-profdata.exe"
    [[ -f "${vsdev}" ]] || error "VsDevCmd.bat missing under ${VS_INSTALL_PATH}."
    [[ -f "${clang_cl}" ]] || error "clang-cl.exe missing under ${VS_INSTALL_PATH}."
    [[ -f "${llvm_profdata}" ]] || error "llvm-profdata.exe missing beside clang-cl.exe."
    local perl_candidate
    native_perl=""
    for perl_candidate in \
        "${PERL_EXECUTABLE:-}" \
        "/c/Strawberry/perl/bin/perl.exe" \
        "/c/Perl64/bin/perl.exe"; do
        [[ -n "${perl_candidate}" && -x "${perl_candidate}" ]] || continue
        if "${perl_candidate}" -e 'exit(($^O eq "MSWin32") ? 0 : 1)'; then
            native_perl="${perl_candidate}"
            break
        fi
    done
    [[ -n "${native_perl}" ]] ||
        error "Native Win32 Perl required for clang-cl OpenSSL. Install Strawberry Perl or set PERL_EXECUTABLE. MSYS/Cygwin Perl is incompatible with VC-WIN64A."
    local python_candidate
    native_python=""
    for python_candidate in \
        "${PYTHON_EXECUTABLE:-}" \
        /c/Python312/python.exe \
        /c/hostedtoolcache/windows/Python/3.12.*/x64/python.exe \
        /c/Users/*/AppData/Local/Programs/Python/Python312/python.exe; do
        [[ -x "${python_candidate}" ]] || continue
        if "${python_candidate}" -c 'import sys; raise SystemExit(sys.platform != "win32")'; then
            native_python="${python_candidate}"
            break
        fi
    done
    [[ -n "${native_python}" ]] ||
        error "Native Windows Python 3.12 required. Run setup --compiler clang-cl or set PYTHON_EXECUTABLE."

    local sccache="/clang64/bin/sccache.exe"
    local ninja="/clang64/bin/ninja.exe"
    [[ -x "${ninja}" ]] ||
        error "ninja.exe missing. Run setup --compiler clang-cl."
    local sccache_cmake_args="  -DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER= ^"
    local sccache_start_cmd="rem sccache unavailable" sccache_stats_cmd="rem sccache unavailable"
    if [[ -x "${sccache}" ]]; then
        local sccache_win_tmp
        sccache_win_tmp="$(cygpath -am "${sccache}")"
        sccache_cmake_args="  -DCMAKE_C_COMPILER_LAUNCHER=\"${sccache_win_tmp}\" -DCMAKE_CXX_COMPILER_LAUNCHER=\"${sccache_win_tmp}\" ^"
        sccache_start_cmd="set \"SCCACHE_IGNORE_SERVER_IO_ERROR=1\"
\"${sccache_win_tmp}\" --start-server >NUL 2>&1"
        sccache_stats_cmd="\"${sccache_win_tmp}\" --show-stats"
    else
        warn "sccache.exe missing; clang-cl build will run without compiler cache."
    fi

    local config="${BUILD_TYPE}" stage_name flags="" pgo_flags="" pgo_link_flags="" pgo_flags_dash="" config_compile_flags config_link_flags
    case "${config}" in
        Release)
            config_compile_flags="/O2 /DNDEBUG /clang:-fno-strict-aliasing"
            config_link_flags="/OPT:REF /OPT:ICF /force:multiple"
            ;;
        RelWithDebInfo)
            config_compile_flags="/O2 /Z7 /DNDEBUG /clang:-fno-strict-aliasing"
            config_link_flags="/DEBUG /OPT:REF /OPT:ICF /force:multiple"
            ;;
        Debug)
            config_compile_flags="/Od /Z7"
            config_link_flags="/DEBUG /force:multiple"
            ;;
        *) error "Unsupported clang-cl build type: ${config}" ;;
    esac
    stage_name="${STAGE}"
    local package_dir="${BUILD_ROOT}/clang-cl/${stage_name}"
    local build_dir="${BUILD_ROOT}/clang-cl/.work/${stage_name}"
    mkdir -p "${build_dir}" "${PROFILE_DIR}" "${PROFILE_DIR}/cs"

    # Default profraw filenames baked into the binary (relative path — writes next to citron.exe).
    # %p = PID, so repeated runs and different stages never overwrite each other.
    local default_profraw_name_generate="citron-generate-${PGO_MODE}-%p.profraw"
    local default_profraw_name_csgenerate="citron-csgenerate-${PGO_MODE}-%p.profraw"

    # Fold a content hash of the profdata into compile flags so sccache/ninja see
    # a cache miss whenever the profile changes (path alone doesn't change on re-merge).
    _pgo_profdata_hash() {
        local pd="$1"
        if command -v sha256sum >/dev/null 2>&1; then
            sha256sum "${pd}" | cut -c1-16
        else
            cksum "${pd}" | tr -d ' \r\n'
        fi
    }

    # pgo_flags_dash: same as pgo_flags but in bare dash syntax for OpenSSL/FFmpeg
    # configure scripts (they don't go through clang-cl's CL-style arg parser).
    # /INCLUDE: linker flags are omitted — those only matter for the final citron.exe link.
    case "${PGO_MODE}:${STAGE}" in
        none:use) ;;
        none:*) error "--pgo none supports only clang-cl use stage." ;;
        fe:generate)
            # /INCLUDE:__llvm_profile_runtime and /INCLUDE:__llvm_profile_write_file
            # force-keep the LLVM profiling runtime's static initializer and
            # flush routine at link time. Without them, /OPT:REF (dead-code
            # elimination, enabled below for Release/RelWithDebInfo) can strip
            # these symbols when the instrumented counter code that references
            # them isn't directly reachable from main() -- the binary still
            # links and runs, it just silently never writes any .profraw.
            # See the identical -Wl,-u,__llvm_profile_write_file,-u,__llvm_profile_runtime
            # workaround in stage_generate()/stage_csgenerate() for the
            # llvm-mingw path; /INCLUDE: is the lld-link/link.exe (COFF)
            # equivalent of that GNU-ld -u flag.
            # =<pattern> bakes the default output filename into the binary
            # itself, so it writes next to citron.exe with a unique name even
            # when LLVM_PROFILE_FILE isn't set -- LLVM_PROFILE_FILE still
            # overrides this at runtime if the user does set it.
            # pgo_flags/pgo_link_flags (NOT flags): scoped to citron's own
            # executables only via CITRON_CLANGCL_PGO_COMPILE_FLAGS/
            # _LINK_FLAGS in the root CMakeLists.txt -- see the comment there
            # for why this must not be applied through the global
            # CMAKE_*_FLAGS_${config} variables.
            #
            # BUG FIX: pgo_flags (compile) and pgo_link_flags (link) are
            # DELIBERATELY DIFFERENT, not the same string applied twice. For
            # this MSVC-ABI toolchain, CMake's Ninja Multi-Config generator
            # invokes lld-link.exe DIRECTLY for the final .exe link (via
            # `cmake -E vs_link_exe`) -- it does NOT go through clang-cl.exe.
            # /clang:-prefixed tokens are a clang-cl DRIVER escape hatch with
            # no meaning to lld-link.exe itself; lld-link, like any COFF
            # linker, treats an unrecognized token as an input file path and
            # fails with "could not open '/clang:...'" when that "file"
            # doesn't exist. /INCLUDE: IS genuine native lld-link/link.exe
            # syntax, so it's the only thing that belongs in pgo_link_flags.
            # -fprofile-instr-generate does not need to be repeated at link
            # time -- the instrumentation is fully determined per-TU at
            # compile time.
            pgo_flags="/clang:-fprofile-instr-generate=${default_profraw_name_generate}"
            pgo_link_flags="/INCLUDE:__llvm_profile_runtime /INCLUDE:__llvm_profile_write_file"
            pgo_flags_dash="-fprofile-instr-generate=${default_profraw_name_generate}"
            ;;
        ir:generate)
            # See fe:generate above for why pgo_flags and pgo_link_flags
            # carry different content, why the /INCLUDE: force-keep flags
            # only belong in pgo_link_flags, and why the baked-in =<pattern>
            # output name is required.
            pgo_flags="/clang:-fprofile-generate=${default_profraw_name_generate}"
            pgo_link_flags="/INCLUDE:__llvm_profile_runtime /INCLUDE:__llvm_profile_write_file"
            pgo_flags_dash="-fprofile-generate=${default_profraw_name_generate}"
            ;;
        fe:use|ir:use)
            # Profile priority: merged.profdata (stage1+CS, ir only) → clang-cl-<mode>.profdata (stage1) →
            # default.profdata (ir-only fallback input) → profraw merge.
            # If merged.profdata is stale (new CS profraw arrived), remove it so it gets rebuilt.
            #
            # BUG FIX: all derived filenames below are PGO_MODE-specific
            # (clang-cl-${PGO_MODE}... rather than a hardcoded clang-cl-ir...).
            # This case arm handles BOTH fe and ir (fe:use|ir:use combined) --
            # a hardcoded name meant an fe-mode session's merge output and an
            # ir-mode session's could collide on the exact same filename, so a
            # stale file from one mode could get silently reused by a run in
            # the other mode. The generate/use sentinel only catches a
            # mismatch within one generate-then-use cycle; it does nothing
            # about a leftover file from an earlier, different-mode session
            # still sitting in PROFILE_DIR. csgenerate is unaffected --
            # PGO_MODE is always "ir" there (enforced elsewhere), so its own
            # "clang-cl-ir.profdata" reference already matches what this
            # produces for ir mode.
            local merged_pd="${PROFILE_DIR}/clang-cl-${PGO_MODE}-merged.profdata"
            local stage1_pd="${PROFILE_DIR}/clang-cl-${PGO_MODE}.profdata"
            local stage1_pd_default="${PROFILE_DIR}/default.profdata"
            local profdata_use

            # BUG FIX: stage1_pd is the CANONICAL output path and must never be
            # reassigned -- every merge below writes to it, and the freshness
            # check below is only ever allowed to delete files this stage owns
            # (stage1_pd, merged_pd). default.profdata is a foreign file (the
            # llvm-mingw path's own merge output) that must only ever be READ
            # here, never written or deleted. stage1_pd_input tracks which
            # file to actually read as stage1 data -- it may point at the
            # fallback; stage1_pd itself never does.
            local stage1_pd_input="${stage1_pd}"
            # Fall back to default.profdata (as INPUT only) when
            # clang-cl-<mode>.profdata is absent. default.profdata is the
            # llvm-mingw path's own merge output name and was never
            # mode-specific there either -- only trust it as an ir-mode
            # fallback (matching its only historical use), not for fe.
            if [[ "${PGO_MODE}" == "ir" && ! -f "${stage1_pd}" && -f "${stage1_pd_default}" ]]; then
                stage1_pd_input="${stage1_pd_default}"
            fi

            # BUG FIX: raw profraw files are named citron-generate-<mode>-<pid>.profraw
            # (see default_profraw_name_generate above) specifically so a mode-aware
            # glob can select only this mode's raw data -- a bare *.profraw glob would
            # merge fe-mode and ir-mode raw profiles together into one (incompatible,
            # meaningless) profdata if both ever existed in PROFILE_DIR at once.
            local _raw_glob_pattern="citron-generate-${PGO_MODE}-*.profraw"

            # BUG FIX: freshness check now covers stage1_pd_input AND merged_pd
            # independently (previously gated entirely behind `-f stage1_pd`,
            # so if only merged_pd still existed -- e.g. stage1_pd had already
            # been consumed/removed by an earlier run -- newer raw data would
            # never invalidate it). Only ever removes files this stage owns
            # (stage1_pd, merged_pd); default.profdata is never deleted even
            # if it's the stale one -- we just stop trusting it as input and
            # fall through to rebuilding stage1_pd from raw data instead.
            normalize_profraw_dirs "${PROFILE_DIR}" 2>/dev/null || true
            local _derived_pd _newer_raw
            for _derived_pd in "${stage1_pd_input}" "${merged_pd}"; do
                [[ -f "${_derived_pd}" ]] || continue
                _newer_raw=$(find "${PROFILE_DIR}" -maxdepth 1 -name "${_raw_glob_pattern}" -newer "${_derived_pd}" 2>/dev/null | head -1)
                [[ -n "${_newer_raw}" ]] || continue
                warn "Newer raw profile data found (e.g. ${_newer_raw}) than" \
                     "${_derived_pd}."
                if [[ "${_derived_pd}" == "${stage1_pd_default}" ]]; then
                    warn "Not deleting ${stage1_pd_default} (owned by the llvm-mingw path)" \
                         "-- rebuilding ${stage1_pd} from raw data instead."
                    stage1_pd_input="${stage1_pd}"
                else
                    warn "Removing stale ${_derived_pd} and re-merging..."
                    rm -f "${_derived_pd}"
                fi
            done

            # BUG FIX: CS-IRPGO is exclusively an ir-mode concept (csgenerate
            # always requires --pgo ir, enforced elsewhere) -- fe-mode sessions
            # must never look at PROFILE_DIR/cs or attempt to build/consume a
            # clang-cl-fe-merged.profdata. Without this guard, an fe-mode use
            # run could pick up CS profraw left over from an earlier, unrelated
            # ir-mode csgenerate session and merge incompatible profile data
            # together.
            if [[ "${PGO_MODE}" == "ir" && -f "${merged_pd}" ]]; then
                local _cs_dir_check="${PROFILE_DIR}/cs"
                normalize_profraw_dirs "${_cs_dir_check}" 2>/dev/null || true
                local _cs_pending
                _cs_pending=$(find "${_cs_dir_check}" -maxdepth 1 -name "*.profraw" \
                              2>/dev/null | wc -l)
                if [[ "${_cs_pending}" -gt 0 ]]; then
                    warn "clang-cl-${PGO_MODE}-merged.profdata exists but ${_cs_pending} unmerged CS" \
                         "profraw file(s) found in ${_cs_dir_check}."
                    warn "Removing stale merged profdata and re-merging with CS data..."
                    rm -f "${merged_pd}"
                fi
            fi

            if [[ "${PGO_MODE}" == "ir" && -f "${merged_pd}" ]]; then
                profdata_use="${merged_pd}"
                info "Using CS-IRPGO merged profile: ${profdata_use}"
            elif [[ -f "${stage1_pd_input}" ]]; then
                # Check whether CS profraw exists and needs merging (ir only)
                local cs_count=0
                if [[ "${PGO_MODE}" == "ir" ]]; then
                    local cs_dir="${PROFILE_DIR}/cs"
                    normalize_profraw_dirs "${cs_dir}" 2>/dev/null || true
                    cs_count=$(find "${cs_dir}" -maxdepth 1 -name "*.profraw" \
                               2>/dev/null | wc -l)
                fi
                if [[ "${cs_count}" -gt 0 ]]; then
                    info "CS profraw detected (${cs_count} files) — merging with stage1..."
                    local cs_tmp="${PROFILE_DIR}/clang-cl-${PGO_MODE}-cs-only.profdata"
                    "${llvm_profdata}" merge -output="${cs_tmp}" "${cs_dir}"/*.profraw ||
                        error "llvm-profdata CS merge failed."
                    "${llvm_profdata}" merge -output="${merged_pd}" \
                        "${stage1_pd_input}" "${cs_tmp}" ||
                        error "llvm-profdata stage1+CS merge failed."
                    rm -f "${cs_tmp}"
                    profdata_use="${merged_pd}"
                    info "CS-IRPGO merged profile written: ${profdata_use}"
                else
                    profdata_use="${stage1_pd_input}"
                    info "Using stage1 profile (no CS data): ${profdata_use}"
                fi
            else
                # On-the-fly merge from raw files -- ALWAYS writes to the
                # canonical stage1_pd, never to stage1_pd_default.
                normalize_profraw_dirs "${PROFILE_DIR}" 2>/dev/null || true
                local raw=("${PROFILE_DIR}"/${_raw_glob_pattern})
                [[ -e "${raw[0]}" ]] || error \
                    "No .profraw files matching ${_raw_glob_pattern} in ${PROFILE_DIR};" \
                    "run instrumented workload first.\n" \
                    "       Collect citron-generate-${PGO_MODE}-<pid>.profraw from Windows, copy to ${PROFILE_DIR}/,\n" \
                    "       then re-run: ./build-clangtron-windows.sh use --compiler clang-cl --pgo ${PGO_MODE}"
                "${llvm_profdata}" merge -output="${stage1_pd}" "${raw[@]}" ||
                    error "llvm-profdata merge failed."
                profdata_use="${stage1_pd}"
                info "Merged ${#raw[@]} profraw file(s) → ${stage1_pd}"
            fi

            # /DCITRON_PGO_PROFDATA_HASH: forces sccache/ninja cache miss when profdata content changes.
            # pgo_link_flags empty — use stage activates no profiling runtime, nothing to force-keep.
            # -Wno-error=backend-plugin: hash-mismatch profile warnings promoted to errors by -Werror.
            # BUG FIX: _profdata_use_win is quoted below. It's a full path derived
            # from BUILD_ROOT (via cygpath), which CAN contain spaces (e.g. a
            # Windows username with a space). Left unquoted, CMake's
            # separate_arguments(UNIX_COMMAND ...) -- used both for citron's own
            # PGO flags and for OpenSSL's -- treats an unescaped space as a token
            # boundary, silently splitting "-fprofile-use=C:/Users/John Smith/..."
            # into two broken tokens instead of preserving it as one flag.
            local _pd_hash; _pd_hash="$(_pgo_profdata_hash "${profdata_use}")"
            local _profdata_use_win; _profdata_use_win="$(cygpath -am "${profdata_use}")"
            if [[ "${PGO_MODE}" == "fe" ]]; then
                pgo_flags="/clang:-fprofile-instr-use=\\\"${_profdata_use_win}\\\" /DCITRON_PGO_PROFDATA_HASH=${_pd_hash} /clang:-Wno-error=backend-plugin"
                pgo_flags_dash="-fprofile-instr-use=\\\"${_profdata_use_win}\\\" -DCITRON_PGO_PROFDATA_HASH=${_pd_hash} -Wno-error=backend-plugin"
            else
                pgo_flags="/clang:-fprofile-use=\\\"${_profdata_use_win}\\\" /DCITRON_PGO_PROFDATA_HASH=${_pd_hash} /clang:-Wno-error=backend-plugin"
                pgo_flags_dash="-fprofile-use=\\\"${_profdata_use_win}\\\" -DCITRON_PGO_PROFDATA_HASH=${_pd_hash} -Wno-error=backend-plugin"
            fi
            ;;
        ir:csgenerate)
            local stage1="${PROFILE_DIR}/clang-cl-ir.profdata"
            # CRITICAL: use ONLY the plain stage1 profdata (clang-cl-ir.profdata or
            # default.profdata), never merged.profdata — see header CRITICAL INVARIANT.
            # Priority: clang-cl-ir.profdata → default.profdata → raw profraw merge
            #   2. default.profdata      (stage1 from llvm-mingw use/csgenerate path)
            #   3. citron-generate-*.profraw  (canonical clang-cl generate output)
            #   4. *.profraw             (fallback when LLVM_PROFILE_FILE was not set)
            if [[ -f "${stage1}" ]]; then
                info "Using existing stage-1 profdata: ${stage1}"
            elif [[ -f "${PROFILE_DIR}/default.profdata" ]]; then
                info "Using default.profdata as stage-1 profdata."
                stage1="${PROFILE_DIR}/default.profdata"
            else
                # Explicit guard: merged.profdata without default.profdata means
                # the stage1 raw files have been discarded after a CS cycle.
                # Using merged.profdata here would violate the critical invariant.
                local merged_check="${PROFILE_DIR}/clang-cl-merged.profdata"
                if [[ ! -f "${merged_check}" ]]; then
                    merged_check="${PROFILE_DIR}/merged.profdata"
                fi
                if [[ -f "${merged_check}" ]]; then
                    error "default.profdata not found, but merged.profdata exists.\n" \
                          "       merged.profdata contains CS records from a previous cycle and\n" \
                          "       MUST NOT be used as the stage1 base for csgenerate.\n" \
                          "       To rebuild default.profdata:\n" \
                          "         1. Copy the original stage1 profraw files to ${PROFILE_DIR}/\n" \
                          "         2. Re-run: ./build-clangtron-windows.sh use --compiler clang-cl --pgo ir\n" \
                          "            (this produces clang-cl-ir.profdata from the stage1 profraw)"
                fi
                # Try to merge from raw files.
                normalize_profraw_dirs "${PROFILE_DIR}" 2>/dev/null || true
                local ir_raw=("${PROFILE_DIR}"/citron-generate-*.profraw)
                if [[ ! -e "${ir_raw[0]}" ]]; then
                    # Fallback: accept any *.profraw (e.g. default-<pid>.profraw
                    # written when LLVM_PROFILE_FILE was not explicitly set).
                    ir_raw=("${PROFILE_DIR}"/*.profraw)
                    [[ -e "${ir_raw[0]}" ]] || error \
                        "No stage-1 .profraw files in ${PROFILE_DIR}.\n" \
                        "       Run generate, then profile citron.exe with:\n" \
                        "         \$env:LLVM_PROFILE_FILE='${PROFILE_DIR}/citron-generate-%p.profraw'\n" \
                        "       Copy the resulting .profraw to ${PROFILE_DIR}/ and re-run csgenerate."
                fi
                info "Merging ${#ir_raw[@]} stage-1 .profraw file(s) → ${stage1##*/}..."
                "${llvm_profdata}" merge -output="${stage1}" "${ir_raw[@]}" ||
                    error "llvm-profdata stage-1 merge failed."
            fi
            # BUG FIX: -fcs-profile-generate is a distinct driver flag from
            # -fprofile-generate (it's only ever combined with -fprofile-use,
            # /INCLUDE: force-keep: -fcs-profile-generate doesn't auto-inject the runtime reference
            # that -fprofile-generate does. Without it, /OPT:REF strips the runtime silently.
            # /DCITRON_PGO_PROFDATA_HASH: busts cache if stage1 content changes between runs.
            # BUG FIX: _stage1_win is quoted below -- same reason as
            # fe:use|ir:use's _profdata_use_win quoting above (a full path
            # derived from BUILD_ROOT can contain spaces, which would
            # otherwise get split into two broken tokens by
            # separate_arguments(UNIX_COMMAND ...)).
            local _pd_hash; _pd_hash="$(_pgo_profdata_hash "${stage1}")"
            local _stage1_win; _stage1_win="$(cygpath -am "${stage1}")"
            # pgo_link_flags is plain lld-link syntax (no /clang:) — lld-link is invoked directly.
            # -Wno-error=backend-plugin: csgenerate also uses -fprofile-use and hits hash-mismatch
            # warnings that citron's -Werror would otherwise promote to hard build failures.
            pgo_flags="/clang:-fprofile-use=\"${_stage1_win}\" /DCITRON_PGO_PROFDATA_HASH=${_pd_hash} /clang:-fcs-profile-generate=${default_profraw_name_csgenerate} /clang:-Wno-error=backend-plugin"
            pgo_link_flags="/INCLUDE:__llvm_profile_runtime /INCLUDE:__llvm_profile_write_file"
            pgo_flags_dash="-fprofile-use=\"${_stage1_win}\" -DCITRON_PGO_PROFDATA_HASH=${_pd_hash} -fcs-profile-generate=${default_profraw_name_csgenerate} -Wno-error=backend-plugin"
            ;;
        *) error "Unsupported clang-cl PGO flow: ${PGO_MODE}:${STAGE}" ;;
    esac
    # LTO flags go in CMAKE_C/CXX_FLAGS (compile only), never CMAKE_EXE_LINKER_FLAGS.
    # lld-link auto-detects bitcode .obj files; no flag needed at link time.
    case "${LTO_MODE}" in
        none) ;;
        thin)
            flags="${flags} /clang:-flto=thin"
            config_link_flags="${config_link_flags} /opt:lldltojobs=all"
            ;;
        full)
            flags="${flags} /clang:-flto=full"
            config_link_flags="${config_link_flags} /opt:lldltopartitions=${JOBS:-16} /opt:lldltojobs=all"
            ;;
    esac

    local source_win build_win package_win build_copy_win package_copy_win batch_win cpm_win vsdev_win perl_win python_win
    local clang_cl_win clang_bin_win ninja_win clang64_bin_win
    source_win="$(cygpath -am "${SOURCE_DIR}")"
    build_win="$(cygpath -am "${build_dir}")"
    package_win="$(cygpath -am "${package_dir}")"
    build_copy_win="$(cygpath -aw "${build_dir}")"
    package_copy_win="$(cygpath -aw "${package_dir}")"
    batch_win="$(cygpath -aw "${build_dir}/build-clang-cl.cmd")"
    cpm_win="$(cygpath -am "${CPM_SOURCE_CACHE}")"
    vsdev_win="$(cygpath -am "${vsdev}")"
    perl_win="$(cygpath -am "${native_perl}")"
    python_win="$(cygpath -am "${native_python}")"
    clang_cl_win="$(cygpath -am "${clang_cl}")"
    clang_bin_win="$(cygpath -am "$(dirname "${clang_cl}")")"
    ninja_win="$(cygpath -am "${ninja}")"
    # Resolve the MSYS2 clang64/bin path dynamically (varies on CI runners).
    clang64_bin_win="$(cygpath -am /clang64/bin)"
    local msys2_usr_bin_win
    msys2_usr_bin_win="$(cygpath -am /usr/bin)"
    # Resolve native Python Scripts dir for aqt.exe.
    local python_scripts_win=""
    if [[ -n "${native_python}" && -d "$(dirname "${native_python}")/Scripts" ]]; then
        python_scripts_win="$(cygpath -am "$(dirname "${native_python}")/Scripts")"
    else
        local _py_scripts_candidate
        for _py_scripts_candidate in \
                /c/hostedtoolcache/windows/Python/3.12.*/x64 \
                /c/Python312; do
            if [[ -x "${_py_scripts_candidate}/python.exe" ]]; then
                python_scripts_win="$(cygpath -am "${_py_scripts_candidate}/Scripts")"
                break
            fi
        done
    fi

    # Qt/OpenSSL/FFmpeg cached under CPM_SOURCE_CACHE — shared across all clang-cl stages.
    local _qt_version="6.9.3"
    local _openssl_version="3.4.1"
    local _ffmpeg_tag="n8.0"

    # Qt: qt_download.cmake already writes to CPM_SOURCE_CACHE/qt-bin/.
    # Pre-check here and pass Qt6_DIR to cmake so the configure-time download
    # is skipped when the cache is warm.
    local _qt_cache_dir="${CPM_SOURCE_CACHE}/qt-bin/${_qt_version}/msvc2022_64"
    local _qt6_dir="${_qt_cache_dir}/lib/cmake/Qt6"
    local qt6_dir_win="" qt_target_path_win=""
    if [[ -f "${_qt6_dir}/Qt6Config.cmake" ]]; then
        info "Qt ${_qt_version} (msvc2022_64) found in CPM cache — skipping aqt download."
        qt6_dir_win="$(cygpath -am "${_qt6_dir}")"
        qt_target_path_win="$(cygpath -am "${_qt_cache_dir}")"
    else
        info "Qt ${_qt_version} (msvc2022_64) not in CPM cache — will download during cmake configure."
        if [[ -n "${native_python}" ]]; then
            "${native_python}" -m pip install aqtinstall --quiet 2>/dev/null || true
        fi
    fi

    # Cache key folds LTO mode, PGO mode, stage, and profdata hash into the path.
    # Each distinct (LTO, PGO, stage, profile) combination gets its own dir —
    # prevents execute_process-based builds (OpenSSL, FFmpeg) from silently reusing
    # a cache built with different flags.
    # OpenSSL and FFmpeg deliberately stay native COFF even when the application
    # uses LTO. lld-link can then resolve members from these lazy archives after
    # its LTO phase; LLVM-bitcode archive members cannot be loaded at that point.
    # Include the dependency object format in the key so old bitcode archives are
    # never reused.
    local _pgo_lto_cache_key="deps-coff_lto-${LTO_MODE}_pgo-${PGO_MODE}_${STAGE}"
    if [[ -n "${_pd_hash:-}" ]]; then
        _pgo_lto_cache_key="${_pgo_lto_cache_key}_${_pd_hash}"
    fi

    local _openssl_cache_dir="${CPM_SOURCE_CACHE}/citron-openssl-clangcl/${_openssl_version}-VC-WIN64A-${_pgo_lto_cache_key}"
    local openssl_cache_dir_win
    openssl_cache_dir_win="$(cygpath -am "${_openssl_cache_dir}")"
    mkdir -p "${_openssl_cache_dir}"

    local _ffmpeg_cache_dir="${CPM_SOURCE_CACHE}/citron-ffmpeg-clangcl/${_ffmpeg_tag}-${_pgo_lto_cache_key}"
    local ffmpeg_cache_dir_win
    ffmpeg_cache_dir_win="$(cygpath -am "${_ffmpeg_cache_dir}")"
    mkdir -p "${_ffmpeg_cache_dir}/build" "${_ffmpeg_cache_dir}/install"

    # Qt cmake args — pass pre-resolved paths when cache is warm.
    # Empty -DQt6_DIR= is a no-op when Qt is not yet cached.
    local qt_cmake_line
    if [[ -n "${qt6_dir_win}" ]]; then
        qt_cmake_line="  -DQt6_DIR=\"${qt6_dir_win}\" -DQT_TARGET_PATH=\"${qt_target_path_win}\" ^"
    else
        qt_cmake_line="  -DQt6_DIR= ^"
    fi

    # BUG FIX: %p in profraw patterns (e.g. -fprofile-generate=..-%p.profraw) must be
    # doubled to %%p in the .cmd heredoc. cmd.exe expands %...% pairs across the entire
    # logical line (including ^ continuations), pairing stray percent signs and silently
    # deleting everything between them. %%p collapses to literal %p after cmd.exe's
    # one-pass expansion, so clang still sees the correct PID placeholder.
    local flags_batch="${flags//%/%%}"
    local pgo_flags_batch="${pgo_flags//%/%%}"
    local pgo_link_flags_batch="${pgo_link_flags//%/%%}"
    local pgo_flags_dash_batch="${pgo_flags_dash//%/%%}"

    # NOTE: CITRON_ENABLE_LTO and CITRON_ENABLE_PGO_GENERATE/USE are deliberately left
    # OFF below, even though this build may genuinely be doing LTO/PGO via the raw
    # /clang:-flto=... and /clang:-fprofile-instr-... flags already baked into
    # CMAKE_C/CXX_FLAGS_${config} above. Do NOT "fix" these to reflect real state --
    # turning them ON activates CMake-native, MSVC-flavored codepaths that fire
    # whenever MSVC==TRUE (true for clang-cl) and stack incompatible flags on top of
    # the script's own Clang-native ones:
    #   - citron_configure_lto() (called from common/core/video_core/shader_recompiler/
    #     network/audio_core/hid_core/input_common/frontend_common/web_service) sets
    #     CMake's INTERPROCEDURAL_OPTIMIZATION property, which for an MSVC-ABI compiler
    #     means CMake injects /GL at compile time and expects /LTCG at link time --
    #     MSVC's own whole-program-optimization mechanism, not Clang's bitcode LTO.
    #   - the top-level `if (MSVC) ... add_link_options(/LTCG)` block (CMakeLists.txt)
    #     fires on the same OR condition.
    #   - citron_configure_pgo()'s MSVC branch adds /FASTGENPROFILE, /PGD:, and
    #     /USEPROFILE:PGD= link options -- MSVC's .pgd-based PGO, a completely
    #     different, incompatible system from Clang's .profraw/.profdata instr-PGO
    #     the script already sets up via -fprofile-instr-generate/-fprofile-instr-use.
    #   - PGO.cmake's `if (MSVC) ... add_compile_options(/GL)` block fires too, and is
    #     not gated by CITRON_PGO_FLAGS_MANAGED_BY_SCRIPT (that guard only wraps the
    #     GNU/Clang branch, not the MSVC one).
    # TracyClient's own LTO/PGO opt-out in dependencies.cmake does not depend on these
    # vars being accurate -- it unconditionally forces INTERPROCEDURAL_OPTIMIZATION
    # FALSE and appends -fno-lto/-fno-profile-* as target-level compile options on
    # TracyClient regardless, so Tracy stays correctly isolated either way.
    local _clangcl_lto_cmake="OFF"
    local _clangcl_pgo_generate="OFF"
    local _clangcl_pgo_use="OFF"

    cat > "${build_dir}/build-clang-cl.cmd" <<CLANGCL_CMD_EOF
@echo off
setlocal
for %%V in (CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH CFLAGS CXXFLAGS CPPFLAGS INCLUDE LIB LIBPATH PKG_CONFIG_PATH PKG_CONFIG_LIBDIR) do set "%%V="
set "PATH=${clang_bin_win};C:\Program Files\CMake\bin;${clang64_bin_win};${msys2_usr_bin_win};%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;C:\Program Files\Git\cmd;C:\Python312;${python_scripts_win}"
call "${vsdev_win}" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
if not defined CPM_SOURCE_CACHE set "CPM_SOURCE_CACHE=${cpm_win}"
${sccache_start_cmd}
cmake -S "${source_win}" -B "${build_win}" -G "Ninja Multi-Config" ^
  -DCMAKE_MAKE_PROGRAM="${ninja_win}" ^
  -DCMAKE_C_COMPILER="${clang_cl_win}" -DCMAKE_CXX_COMPILER="${clang_cl_win}" ^
${sccache_cmake_args}
  -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT="$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>" ^
  -DCITRON_USE_CPM=ON -DCITRON_USE_BUNDLED_VCPKG=OFF -DCITRON_CHECK_SUBMODULES=OFF ^
  -DCPM_SOURCE_CACHE="${cpm_win}" ^
  -DCITRON_CLANGCL=ON -DCITRON_USE_BUNDLED_QT=ON -DCITRON_USE_BUNDLED_FFMPEG=ON ^
  -DBUILD_TESTING=OFF -DCITRON_TESTS=OFF -DCITRON_SHADER_TOOL=OFF ^
  -DCITRON_CRASH_DUMPS=OFF ^
  -DENABLE_UNITY_BUILD=${UNITY_BUILD} ^
  -DPython3_EXECUTABLE="${python_win}" ^
  -DPERL_EXECUTABLE="${perl_win}" ^
  -D_OPENSSL_NASM=${clang64_bin_win}/nasm.exe ^
  -DGLSLANGVALIDATOR=${clang64_bin_win}/glslangValidator.exe ^
  -DCLANGCL_OPENSSL_CACHE_DIR="${openssl_cache_dir_win}" ^
  -DCLANGCL_FFMPEG_CACHE_DIR="${ffmpeg_cache_dir_win}" ^
  -DCLANGCL_OPENSSL_EXTRA_CFLAGS="${pgo_flags_dash_batch}" ^
  -DCLANGCL_FFMPEG_EXTRA_CFLAGS="${pgo_flags_dash_batch}" ^
${qt_cmake_line}
  -DCITRON_ENABLE_LTO=${_clangcl_lto_cmake} ^
  -DCITRON_ENABLE_TRACY=${TRACY_BUILD} ^
  -DCITRON_ENABLE_TRACY_ALLOC=${TRACY_ALLOC_BUILD} ^
  -DCITRON_ENABLE_PGO_GENERATE=${_clangcl_pgo_generate} -DCITRON_ENABLE_PGO_USE=${_clangcl_pgo_use} ^
  -DCMAKE_C_FLAGS_${config^^}="${config_compile_flags} ${flags_batch}" -DCMAKE_CXX_FLAGS_${config^^}="${config_compile_flags} ${flags_batch}" ^
  -DCMAKE_EXE_LINKER_FLAGS_${config^^}:STRING="${config_link_flags}" ^
  -DCMAKE_SHARED_LINKER_FLAGS_${config^^}:STRING="${config_link_flags}" ^
  -DCMAKE_MODULE_LINKER_FLAGS_${config^^}:STRING="${config_link_flags}" ^
  -DCITRON_CLANGCL_PGO_COMPILE_FLAGS="${pgo_flags_batch}" -DCITRON_CLANGCL_PGO_LINK_FLAGS="${pgo_link_flags_batch}" ^
  -DCMAKE_RC_FLAGS="" -DCMAKE_RC_FLAGS_DEBUG="" -DCMAKE_RC_FLAGS_RELEASE="" -DCMAKE_RC_FLAGS_RELWITHDEBINFO=""
if errorlevel 1 exit /b %errorlevel%
cmake --build "${build_win}" --config ${config} --parallel ${JOBS} --target citron-room
if not %errorlevel%==0 exit /b 1
cmake --build "${build_win}" --config ${config} --parallel ${JOBS} --target citron-cmd
if not %errorlevel%==0 exit /b 1
cmake --build "${build_win}" --config ${config} --parallel ${JOBS} --target citron
if not %errorlevel%==0 exit /b 1
cmake --build "${build_win}" --config ${config} --parallel ${JOBS} --target citron-runtime
if not %errorlevel%==0 exit /b 1
${sccache_stats_cmd}
if not exist "${package_copy_win}" mkdir "${package_copy_win}"
for %%F in ("${package_copy_win}\\*") do if exist "%%F" del /F /Q "%%F"
for /D %%D in ("${package_copy_win}\\*") do if /I not "%%~nxD"=="user" if exist "%%D" rmdir /S /Q "%%D"
if not exist "${package_copy_win}\\user" mkdir "${package_copy_win}\\user"
copy /Y "${build_copy_win}\\bin\\${config}\\citron.exe" "${package_copy_win}\\citron.exe" >NUL
if errorlevel 1 exit /b %errorlevel%
copy /Y "${build_copy_win}\\bin\\${config}\\citron-cmd.exe" "${package_copy_win}\\citron-cmd.exe" >NUL
if errorlevel 1 exit /b %errorlevel%
copy /Y "${build_copy_win}\\bin\\${config}\\citron-room.exe" "${package_copy_win}\\citron-room.exe" >NUL
if errorlevel 1 exit /b %errorlevel%
if exist "${build_copy_win}\\bin\\${config}\\*.dll" (
  copy /Y "${build_copy_win}\\bin\\${config}\\*.dll" "${package_copy_win}\\" >NUL
  if errorlevel 1 exit /b 1
)
if /I "${config}"=="RelWithDebInfo" (
  for %%P in (citron.pdb citron-cmd.pdb citron-room.pdb) do (
    if not exist "${build_copy_win}\\bin\\${config}\\%%P" exit /b 1
    copy /Y "${build_copy_win}\\bin\\${config}\\%%P" "${package_copy_win}\\%%P" >NUL
  )
)
if exist "${build_copy_win}\\bin\\${config}\\qt.conf" (
  copy /Y "${build_copy_win}\\bin\\${config}\\qt.conf" "${package_copy_win}\\qt.conf" >NUL
  if errorlevel 1 exit /b 1
)
for %%D in (iconengines imageformats platforms styles tls) do if exist "${build_copy_win}\\bin\\${config}\\%%D" (
  xcopy /E /I /Y "${build_copy_win}\\bin\\${config}\\%%D" "${package_copy_win}\\%%D" >NUL
  if errorlevel 1 exit /b 1
)
exit /b 0
CLANGCL_CMD_EOF
    info "Visual Studio: ${VS_INSTALL_PATH}"
    info "Compiler: $("${clang_cl}" --version | head -n1)"
    MSYS2_ARG_CONV_EXCL='*' cmd.exe /D /C "${batch_win}" ||
        error "clang-cl ${stage_name} build failed."
    [[ -f "${package_dir}/citron.exe" ]] ||
        error "clang-cl build returned success but citron.exe is missing."
    if [[ "${STAGE}" == "generate" ]]; then
        mkdir -p "${BUILD_ROOT}"
        printf "LTO=%s\nPGO=%s\n" "${LTO_MODE}" "${PGO_MODE}" > "${_gen_cfg}"
    fi
    local binary_win
    binary_win="$(cygpath -am "${package_dir}/citron.exe")"
    success "clang-cl runtime package: ${package_dir}"
    print_clangcl_stage_guidance "${stage_name}" "${binary_win}" "${config}"
}

# =============================================================================
# Argument parsing
# =============================================================================

STAGE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        setup|generate|csgenerate|use|build-elf|bolt|propeller|clean)
            STAGE="$1"; shift ;;
        --source)
            SOURCE_DIR="$2"; shift 2 ;;
        --build)
            BUILD_ROOT="$2"
            BUILD_GENERATE="${BUILD_ROOT}/generate"
            BUILD_CSGENERATE="${BUILD_ROOT}/cs-generate"
            BUILD_USE="${BUILD_ROOT}/use"
            BUILD_USE_ELF="${BUILD_ROOT}/use-elf"
            BUILD_BOLT="${BUILD_ROOT}/bolt"
            BUILD_PROPELLER="${BUILD_ROOT}/propeller"
            PROFILE_DIR="${BUILD_ROOT}/pgo-profiles"
            BOLT_PROFILE_DIR="${BUILD_ROOT}/bolt-profiles"
            PROPELLER_PROFILE_DIR="${BUILD_ROOT}/propeller-profiles"
            LLVM_MINGW_DIR="${BUILD_ROOT}/llvm-mingw"
            shift 2 ;;
        --generate-dir)
            BUILD_GENERATE="$2"
            shift 2 ;;
        --jobs)
            JOBS="$2"; shift 2 ;;
        --lto)
            case "$2" in
                thin|full|none) LTO_MODE="$2"; shift 2 ;;
                *) echo "[ERROR] --lto requires: thin, full, or none"; exit 1 ;;
            esac ;;
        --lite-lto)
            LTO_MODE="thin"; shift ;;
        --no-lto)
            LTO_MODE="none"; shift ;;
        --pgo-type|--pgo)
            case "$2" in
                ir|fe|none) PGO_MODE="$2"; shift 2 ;;
                *) echo "[ERROR] --pgo-type requires: ir, fe, or none"; exit 1 ;;
            esac ;;
        --release)
            BUILD_TYPE="Release"; shift ;;
        --relwithdebinfo)
            BUILD_TYPE="RelWithDebInfo"; shift ;;
        --unity)
            UNITY_BUILD="ON"; shift ;;
        --no-unity)
            UNITY_BUILD="OFF"; shift ;;
        --tracy)
            TRACY_BUILD="ON"; shift ;;
        --no-tracy)
            TRACY_BUILD="OFF"; shift ;;
        --tracy-alloc)
            TRACY_ALLOC_BUILD="ON"; shift ;;
        --no-tracy-alloc)
            TRACY_ALLOC_BUILD="OFF"; shift ;;
        --clang-version)
            CLANG_VERSION="$2"
            CLANG="clang-${CLANG_VERSION}"
            CLANGPP="clang++-${CLANG_VERSION}"
            LLVM_PROFDATA="llvm-profdata-${CLANG_VERSION}"
            LLVM_BOLT="llvm-bolt-${CLANG_VERSION}"
            MERGE_FDATA="merge-fdata-${CLANG_VERSION}"
            shift 2 ;;
        --llvm-mingw-version)
            LLVM_MINGW_VERSION="$2"; shift 2 ;;
        --compiler)
            case "$2" in
                llvm-mingw|clang-cl) COMPILER_MODE="$2"; shift 2 ;;
                *) echo "[ERROR] --compiler requires: llvm-mingw or clang-cl"; exit 1 ;;
            esac ;;
        --help|-h)
            sed -n '/^# USAGE/,/^# ====/p' "$0"
            exit 0 ;;
        *)
            error "Unknown argument: $1\nRun with --help for usage." ;;
    esac
done

[[ -n "$STAGE" ]] || error "No stage specified. Run with --help for usage."

if [[ "${TRACY_ALLOC_BUILD}" == "ON" && "${TRACY_BUILD}" != "ON" ]]; then
    error "--tracy-alloc requires --tracy (it adds heap tracking on top of the base Tracy instrumentation and does nothing without it)."
fi

if [[ "${COMPILER_MODE}" == "clang-cl" ]]; then
    if [[ "${STAGE}" == "setup" ]]; then
        stage_setup
        exit 0
    fi
    stage_clangcl
    exit 0
fi

case "$STAGE" in
    setup)       stage_setup ;;
    generate)    stage_generate ;;
    csgenerate)  stage_csgenerate ;;
    use)         stage_use ;;
    build-elf)   stage_build_elf ;;
    bolt)        stage_bolt ;;
    propeller)   stage_propeller ;;
    clean)       stage_clean ;;
esac
