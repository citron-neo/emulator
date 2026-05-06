#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
# =============================================================================
# build-citron-linux.sh — Local Linux build script with PGO + LTO + BOLT
#
# Lives in the citron repo root.  Assumes the repo has already been cloned
# (with or without submodules — CPM handles all library dependencies and
# FFmpeg source at cmake configure time; no submodule init is required).
#
# PGO FLAG MANAGEMENT:
#   This script manages all PGO and LTO compiler/linker flags directly, using
#   CITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON to suppress PGO.cmake's built-in
#   flag injection.
#
#   Why: PGO.cmake's default Clang path uses -fprofile-instr-generate (FE PGO
#   only), has no path control, and never passes -fprofile-use to the linker
#   command line.  Without -fprofile-use on the linker, the LTO backend runs
#   without profile guidance even when full LTO is active — negating most of
#   the combined PGO+LTO benefit.
#
#   This script passes explicit flags to CMAKE_C_FLAGS_<BT>,
#   CMAKE_CXX_FLAGS_<BT>, and CMAKE_EXE_LINKER_FLAGS_<BT> so:
#     - IR PGO uses -fprofile-generate / -fprofile-use (not -fprofile-instr-*)
#     - Profraw files land in a controlled directory with %p PID expansion
#     - The LTO linker backend receives -fprofile-use for LTCG with profile data
#     - Profile runtime symbols are force-kept on the linker command line
#
# LTO + PGO LINKER FLAGS (use/csgenerate/bolt/propeller stages):
#   For full LTO (-flto), the -fprofile-use flag must appear on
#   CMAKE_EXE_LINKER_FLAGS as well as the compile flags, so the LTO backend
#   applies the profile during link-time code generation.
#
# CS-IRPGO (csgenerate stage):
#   IR PGO only.  Layers context-sensitive counters on top of a stage1-optimized
#   binary.  Provides per-call-site counter data vs per-function-definition.
#   CRITICAL INVARIANT: csgenerate always uses default.profdata (stage1 only)
#   as -fprofile-use input, never merged.profdata.  If merged.profdata (which
#   already contains CS records) were used, the inlining decisions would shift
#   relative to the plain stage1 baseline, restructuring the IR that the new CS
#   counters are keyed to — yielding profile hash mismatches at the use stage.
#
# STAGES:
#   setup       Detect your distro and install required build tools (run once).
#   generate    Stage 1: PGO instrumented build.
#   csgenerate  Stage 1b: [IR PGO only] CS-IRPGO instrumented build.
#   merge       Merge collected .profraw files into .profdata.
#   summary     Show profile statistics.
#   use         Stage 2: PGO + LTO optimized build (or baseline if --pgo none).
#   bolt        Stage 3A: BOLT binary layout optimization.
#   propeller   Stage 3B: Propeller BB+function layout (requires perf + LBR).
#   clean       Remove build directories (profile data preserved).
#
# USAGE:
#   ./build-citron-linux.sh <stage> [options]
#
# OPTIONS:
#   --build DIR           Build root directory (default: ./build)
#   --jobs N              Parallel compile jobs (default: nproc)
#   --pgo ir|fe|none      PGO instrumentation mode (default: ir)
#                           ir   = LLVM IR PGO (-fprofile-generate/-fprofile-use)
#                                  LTO mode MUST match between generate,
#                                  csgenerate, and use stages.
#                           fe   = Frontend PGO (-fprofile-instr-generate/-use)
#                                  CS-IRPGO not available with fe.
#                           none = No PGO. Baseline build.
#   --lto thin|full|none  LTO mode (default: full)
#   --lite-lto            Alias for --lto thin
#   --no-lto              Alias for --lto none
#   --arch x86_64|v3|aarch64|auto
#                         Optimization target (default: auto)
#                           v3 = x86-64-v3 (AVX2, BMI, FMA — Haswell+)
#   --unity               Enable unity (jumbo) builds (~30-90% faster compile)
#   --relwithdebinfo      Include debug symbols alongside optimizations
#   --clang-version N     Clang version to use (default: 21)
#   --help, -h            Show this message
#
# TYPICAL WORKFLOWS:
#
#   Baseline (no PGO):
#     ./build-citron-linux.sh setup
#     ./build-citron-linux.sh use --pgo none --lto full
#     # Binary: ./build/use-nopgo/bin/citron
#
#   IR PGO (recommended):
#     ./build-citron-linux.sh setup
#     ./build-citron-linux.sh generate --pgo ir --lto full
#     # Run ./build/generate/bin/citron, play 2-3 games for 5-10 min, exit cleanly.
#     ./build-citron-linux.sh use --pgo ir --lto full
#     # Binary: ./build/use/bin/citron
#
#   CS-IRPGO (two profiling sessions, better inlining data):
#     ./build-citron-linux.sh generate --pgo ir --lto full
#     # Run ./build/generate/bin/citron, play games, exit cleanly.
#     ./build-citron-linux.sh use --pgo ir --lto full
#     # (produces default.profdata — also a usable binary)
#     ./build-citron-linux.sh csgenerate --pgo ir --lto full
#     # Run ./build/cs-generate/bin/citron, play the same games, exit cleanly.
#     # cs-default-*.profraw lands next to the binary — copy to build/pgo-profiles/cs/
#     ./build-citron-linux.sh use --pgo ir --lto full
#     # (auto-merges CS layer → merged.profdata, rebuilds)
#     # Binary: ./build/use/bin/citron
#
#   IR PGO + BOLT:
#     (after completing 'use' above)
#     ./build-citron-linux.sh bolt --pgo ir --lto full
#     # BOLT pauses — run instrumented binary, exit, press Enter.
#     # Binary: ./build/bolt/citron
#
#   IR PGO + Propeller (requires perf + LBR-capable CPU):
#     (after completing 'use' above)
#     ./build-citron-linux.sh propeller --pgo ir --lto full
#     # Propeller pauses — run perf command shown, exit, press Enter.
#     # Binary: ./build/propeller/bin/citron
#
# DEPENDENCIES MANAGED BY CMAKE (not this script):
#   All C++ library dependencies are fetched from source by CPM at cmake
#   configure time.  No system library packages are required.
#   Qt is downloaded via aqt by CMakeModules/qt_download.cmake.
#   FFmpeg source is downloaded via CPM; built by externals/ffmpeg/CMakeLists.txt.
#   OpenSSL is built from source by CMakeModules/openssl_build.cmake.
#
# REQUIRED HOST TOOLS (installed by the setup stage):
#   clang / clang++ / lld / llvm-profdata  compiler toolchain
#   cmake + ninja                           build system
#   git                                     CPM source fetches
#   nasm                                    FFmpeg assembly optimisations
#   perl                                    OpenSSL Configure script
#   python3 + aqtinstall                    Qt binary download (invoked by cmake)
#   autoconf + automake + make              FFmpeg autotools build
#   glslang (glslc)                         Vulkan shader compilation
#   perf                                    propeller stage only
# =============================================================================

set -euo pipefail

# =============================================================================
# Locate repo root (script lives in repo root)
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[[ -f "${SCRIPT_DIR}/CMakeLists.txt" ]] \
    || { echo "[ERROR] CMakeLists.txt not found next to this script." >&2; exit 1; }

# =============================================================================
# Configuration defaults
# =============================================================================

CLANG_VERSION="${CLANG_VERSION:-21}"
BUILD_ROOT="${BUILD_ROOT:-${SCRIPT_DIR}/build}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
LTO_MODE="${LTO_MODE:-full}"
PGO_MODE="${PGO_MODE:-ir}"
UNITY_BUILD="${UNITY_BUILD:-OFF}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
CPM_SOURCE_CACHE="${CPM_SOURCE_CACHE:-${HOME}/.cache/cpm}"
CPM_SOURCE_CACHE="${CPM_SOURCE_CACHE/#\~/$HOME}"
_ARCH_ARG="${_ARCH_ARG:-auto}"

# =============================================================================
# Derived paths (updated when --build is passed)
# =============================================================================

_set_derived_paths() {
    BUILD_GENERATE="${BUILD_ROOT}/generate"
    BUILD_CSGENERATE="${BUILD_ROOT}/cs-generate"
    BUILD_USE="${BUILD_ROOT}/use"
    BUILD_BOLT="${BUILD_ROOT}/bolt"
    BUILD_PROPELLER="${BUILD_ROOT}/propeller"
    PROFILE_DIR="${BUILD_ROOT}/pgo-profiles"
    BOLT_PROFILE_DIR="${BUILD_ROOT}/bolt-profiles"
    PROPELLER_PROFILE_DIR="${BUILD_ROOT}/propeller-profiles"
}
_set_derived_paths

_set_clang_tools() {
    CLANG="clang-${CLANG_VERSION}"
    CLANGPP="clang++-${CLANG_VERSION}"
    LLVM_PROFDATA="llvm-profdata-${CLANG_VERSION}"
    LLVM_BOLT="llvm-bolt-${CLANG_VERSION}"
    MERGE_FDATA="merge-fdata-${CLANG_VERSION}"
    LLD="lld-${CLANG_VERSION}"
}
_set_clang_tools

# =============================================================================
# Colour helpers
# =============================================================================

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET} $*"; }
success() { echo -e "${GREEN}[OK]${RESET} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET} $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; exit 1; }
header()  {
    echo -e "\n${BOLD}${GREEN}=================================================================${RESET}"
    echo -e "${BOLD}${GREEN}  $*${RESET}"
    echo -e "${BOLD}${GREEN}=================================================================${RESET}"
}

# =============================================================================
# Architecture flags
# =============================================================================

resolve_arch_flags() {
    local host_arch; host_arch="$(uname -m)"
    case "${_ARCH_ARG}" in
        v3)
            [[ "${host_arch}" == "x86_64" ]] \
                || error "--arch v3 requires an x86_64 host (this machine is ${host_arch})"
            ARCH_FLAGS="-march=x86-64-v3 -mtune=generic" ;;
        x86_64)  ARCH_FLAGS="-march=x86-64 -mtune=generic" ;;
        aarch64) ARCH_FLAGS="-march=armv8-a -mtune=generic" ;;
        auto|*)
            case "${host_arch}" in
                x86_64)  ARCH_FLAGS="-march=x86-64 -mtune=generic" ;;
                aarch64) ARCH_FLAGS="-march=armv8-a -mtune=generic" ;;
                *)       ARCH_FLAGS="" ;;
            esac ;;
    esac
    info "Architecture flags: ${ARCH_FLAGS:-(none)}"
}

# =============================================================================
# LTO helpers
# =============================================================================

lto_cmake_flag() { case "${LTO_MODE}" in full|thin) echo "ON" ;; *) echo "OFF" ;; esac; }
lto_clang_flag() { case "${LTO_MODE}" in full) echo "-flto" ;; thin) echo "-flto=thin" ;; *) echo "" ;; esac; }

# =============================================================================
# PGO flag helpers
# =============================================================================

pgo_gen_compile_flag() {
    # $1 = output directory for profraw files
    local output_dir="$1"
    [[ "${PGO_MODE}" == "ir" ]] \
        && echo "-fprofile-generate=${output_dir}/default-%p.profraw" \
        || echo "-fprofile-instr-generate=${output_dir}/default-%p.profraw"
}

pgo_use_compile_flag() {
    # $1 = path to .profdata file
    local profdata="$1"
    [[ "${PGO_MODE}" == "ir" ]] \
        && echo "-fprofile-use=${profdata}" \
        || echo "-fprofile-instr-use=${profdata} -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
}

# build_compile_flags: assemble CMAKE_C/CXX_FLAGS_<BT> for a stage.
# Args: stage (generate|csgenerate|use|nopgo) [extra]
#   generate:   extra = profile output directory
#   csgenerate: extra = "stage1_profdata:cs_output_dir"
#   use:        extra = path to .profdata
#   nopgo:      extra = (unused)
build_compile_flags() {
    local stage="$1"; local extra="${2:-}"
    local debug_flag=""; [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
    local lto_flag; lto_flag="$(lto_clang_flag)"
    local base="-O3 -DNDEBUG ${debug_flag} -USuccess -UNone ${ARCH_FLAGS:-} -Wno-error -w"

    case "${stage}" in
        generate)
            echo "${base} $(pgo_gen_compile_flag "${extra}")${lto_flag:+ ${lto_flag}}"
            ;;
        csgenerate)
            # Use stage1 profile to guide inlining; layer CS counters on top.
            local pd="${extra%%:*}"; local cs_dir="${extra##*:}"
            echo "${base} $(pgo_use_compile_flag "${pd}") -fcs-profile-generate=${cs_dir}/cs-default-%p.profraw${lto_flag:+ ${lto_flag}}"
            ;;
        use)
            # LTO flag before pgo_use so profile flag reaches the LTO backend.
            echo "${base} ${lto_flag:+${lto_flag} }$(pgo_use_compile_flag "${extra}")"
            ;;
        nopgo)
            echo "${base}${lto_flag:+ ${lto_flag}}"
            ;;
    esac
}

# build_linker_flags: assemble CMAKE_EXE_LINKER_FLAGS_<BT> for a stage.
#
# Critical for IR PGO + LTO:
#   -fprofile-use MUST appear on the linker command line when LTO is active.
#   Without it the LTO backend's link-time code generation pass runs without
#   profile guidance — negating inlining and hot-path layout decisions that
#   require profile data to be meaningful at LTCG time.
#
#   For the generate/csgenerate stages, we also force-keep profile runtime
#   entry points that the linker might otherwise dead-strip:
#     -u __llvm_profile_write_file  pulls InstrProfilingFile.o (write logic)
#     -u __llvm_profile_runtime     pulls InstrProfilingRuntime.o whose
#                                   constructor registers the flush-on-exit hook
build_linker_flags() {
    local stage="$1"; local extra="${2:-}"
    local debug_flag=""; [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
    local lto_flag; lto_flag="$(lto_clang_flag)"
    local base="-fuse-ld=${LLD}"

    case "${stage}" in
        generate|csgenerate)
            # Include the full compile flags so the profiling runtime is linked
            # correctly, then add the force-keep symbols.
            echo "${base} $(build_compile_flags "${stage}" "${extra}") -Wl,-u,__llvm_profile_write_file,-u,__llvm_profile_runtime"
            ;;
        use)
            # Pass pgo_use flag to linker so LTO backend gets profile guidance.
            echo "${base} ${debug_flag:+${debug_flag} }${lto_flag:+${lto_flag} }$(pgo_use_compile_flag "${extra}")"
            ;;
        nopgo)
            echo "${base}${lto_flag:+ ${lto_flag}}"
            ;;
    esac
}

# =============================================================================
# Distro / package manager detection
# =============================================================================

detect_pkg_manager() {
    if   command -v apt-get &>/dev/null; then echo "apt"
    elif command -v pacman  &>/dev/null; then echo "pacman"
    elif command -v dnf     &>/dev/null; then echo "dnf"
    elif command -v yum     &>/dev/null; then echo "yum"
    elif command -v zypper  &>/dev/null; then echo "zypper"
    elif command -v emerge  &>/dev/null; then echo "emerge"
    else echo "unknown"
    fi
}

# =============================================================================
# Stage: setup
# =============================================================================

stage_setup() {
    header "Setting Up Build Environment"
    local pkg_mgr; pkg_mgr="$(detect_pkg_manager)"
    info "Package manager: ${pkg_mgr}"

    case "${pkg_mgr}" in
        apt)    _setup_apt    ;;
        pacman) _setup_pacman ;;
        dnf)    _setup_dnf    ;;
        yum)    _setup_yum    ;;
        zypper) _setup_zypper ;;
        emerge) _setup_emerge ;;
        *)
            warn "Unrecognised package manager. Install manually:"
            warn "  clang (${CLANG_VERSION}+), lld, llvm-profdata, cmake, ninja, git,"
            warn "  nasm, perl, python3, python3-pip, autoconf, automake, make, glslang-tools"
            ;;
    esac

    _install_llvm_clang
    _install_aqt
    _check_bolt
    _verify_tools
}

_setup_apt() {
    info "Updating apt package lists..."
    sudo apt-get update -qq
    info "Installing core build tools via apt..."
    sudo apt-get install -y \
        build-essential cmake ninja-build git pkg-config \
        python3 python3-pip curl wget xz-utils \
        nasm yasm perl \
        autoconf automake make \
        glslang-tools \
        lsb-release software-properties-common gnupg \
        libelf-dev libssl-dev libzstd-dev \
        linux-tools-common linux-tools-generic 2>/dev/null || true
    sudo apt-get install -y "linux-tools-$(uname -r)" 2>/dev/null || true
}

_setup_pacman() {
    info "Installing build tools via pacman..."
    sudo pacman -Syu --needed --noconfirm \
        base-devel cmake ninja git \
        python python-pip curl wget \
        nasm yasm perl \
        autoconf automake make \
        glslang clang lld llvm \
        perf 2>/dev/null || true
    # Arch ships unversioned tools — symlink to versioned names
    for tool in clang clang++ lld llvm-profdata llvm-bolt merge-fdata; do
        local versioned="/usr/local/bin/${tool}-${CLANG_VERSION}"
        if command -v "${tool}" &>/dev/null && [[ ! -e "${versioned}" ]]; then
            sudo ln -sf "$(command -v "${tool}")" "${versioned}" 2>/dev/null || true
        fi
    done
    success "Pacman packages installed."
}

_setup_dnf() {
    info "Installing build tools via dnf..."
    sudo dnf install -y \
        gcc gcc-c++ cmake ninja-build git pkg-config \
        python3 python3-pip curl wget xz \
        nasm yasm perl \
        autoconf automake make \
        glslang clang lld \
        elfutils-libelf-devel openssl-devel \
        perf 2>/dev/null || true
    sudo dnf install -y "clang${CLANG_VERSION}" "llvm${CLANG_VERSION}" 2>/dev/null \
        || warn "Versioned LLVM ${CLANG_VERSION} not in repos — using default clang."
}

_setup_yum() {
    info "Installing build tools via yum..."
    sudo yum install -y epel-release 2>/dev/null || true
    sudo yum install -y \
        gcc gcc-c++ cmake ninja-build git pkg-config \
        python3 python3-pip curl wget xz \
        nasm yasm perl \
        autoconf automake make \
        clang lld \
        elfutils-libelf-devel openssl-devel \
        perf 2>/dev/null || true
    warn "yum/CentOS: LLVM ${CLANG_VERSION} may not be in repos. Check SCL or llvm.org."
}

_setup_zypper() {
    info "Installing build tools via zypper..."
    sudo zypper install -y --no-recommends \
        gcc gcc-c++ cmake ninja git pkg-config \
        python3 python3-pip curl wget xz \
        nasm yasm perl \
        autoconf automake make \
        glslang clang lld llvm \
        libelf-devel libopenssl-devel \
        perf 2>/dev/null || true
}

_setup_emerge() {
    info "Installing build tools via emerge..."
    sudo emerge --ask=n \
        dev-build/cmake dev-build/ninja dev-vcs/git \
        dev-lang/python dev-lang/perl \
        dev-lang/nasm dev-lang/yasm \
        sys-devel/clang sys-devel/lld \
        dev-build/autoconf dev-build/automake \
        media-libs/glslang \
        dev-libs/elfutils dev-libs/openssl 2>/dev/null || true
}

_install_llvm_clang() {
    if command -v "${CLANG}" &>/dev/null; then
        success "${CLANG} already available: $(command -v "${CLANG}")"
        return 0
    fi
    local pkg_mgr; pkg_mgr="$(detect_pkg_manager)"
    if [[ "${pkg_mgr}" != "apt" ]]; then
        warn "${CLANG} not found. Install LLVM ${CLANG_VERSION} manually for your distro."
        return 0
    fi
    info "Installing LLVM/Clang ${CLANG_VERSION} from apt.llvm.org..."
    wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
    chmod +x /tmp/llvm.sh
    sudo /tmp/llvm.sh "${CLANG_VERSION}" all \
        || sudo /tmp/llvm.sh "${CLANG_VERSION}" \
        || error "LLVM ${CLANG_VERSION} install failed — see https://apt.llvm.org"
    sudo apt-get install -y \
        "clang-${CLANG_VERSION}" "clang++-${CLANG_VERSION}" \
        "lld-${CLANG_VERSION}" "llvm-${CLANG_VERSION}" \
        "llvm-${CLANG_VERSION}-dev" "libclang-rt-${CLANG_VERSION}-dev" \
        2>/dev/null || warn "Some LLVM ${CLANG_VERSION} packages unavailable."
    success "${CLANG} installed."
}

_install_aqt() {
    # aqt is used by CMakeModules/qt_download.cmake to fetch Qt binaries at
    # cmake configure time.  It must be installed before the first cmake run.
    if command -v aqt &>/dev/null || python3 -m aqt --version &>/dev/null 2>&1; then
        success "aqt already available"
        return 0
    fi
    info "Installing aqtinstall (Qt binary downloader — invoked by cmake)..."
    python3 -m pip install aqtinstall --break-system-packages --quiet \
        || python3 -m pip install aqtinstall --user --quiet \
        || error "aqtinstall install failed — run: pip3 install aqtinstall"
    success "aqtinstall installed."
}

_check_bolt() {
    if command -v "${LLVM_BOLT}" &>/dev/null; then
        success "${LLVM_BOLT} available"
        return 0
    fi
    if command -v llvm-bolt &>/dev/null; then
        sudo ln -sf "$(command -v llvm-bolt)"   "/usr/local/bin/${LLVM_BOLT}" 2>/dev/null || true
        sudo ln -sf "$(command -v merge-fdata)" "/usr/local/bin/${MERGE_FDATA}" 2>/dev/null || true
        return 0
    fi
    warn "${LLVM_BOLT} not found. The bolt stage will build it from source (~15-20 min)."
}

_verify_tools() {
    echo ""
    info "Verifying installation..."
    local ok=1
    for tool in "${CLANG}" "${CLANGPP}" "${LLD}" "${LLVM_PROFDATA}" \
                cmake ninja git nasm perl python3; do
        if command -v "${tool}" &>/dev/null; then
            success "  ${tool} -> $(command -v "${tool}")"
        else
            warn   "  ${tool} -> NOT FOUND"; ok=0
        fi
    done
    if command -v aqt &>/dev/null || python3 -m aqt --version &>/dev/null 2>&1; then
        success "  aqt -> available"
    else
        warn   "  aqt -> NOT FOUND (Qt download by cmake will fail)"; ok=0
    fi
    [[ ${ok} -eq 1 ]] && success "All required tools available." \
                       || warn   "Some tools missing — check output above."
    echo ""
    info "Setup complete. Typical next steps:"
    echo "  ./build-citron-linux.sh use --pgo none --lto full   # quick baseline"
    echo "  ./build-citron-linux.sh generate --pgo ir --lto full  # start IR PGO"
}

# =============================================================================
# Source patches (run against the repo this script lives in)
# =============================================================================

apply_source_patches() {
    info "Applying source compatibility patches..."

    # Boost.Asio API: io_service → io_context (Boost 1.74+)
    find "${SCRIPT_DIR}/src" -type f \( -name '*.cpp' -o -name '*.h' \) \
        -exec sed -i \
            -e 's/\bboost::asio::io_service\b/boost::asio::io_context/g' \
            -e 's/\bboost::asio::io_service::strand\b/boost::asio::strand<boost::asio::io_context::executor_type>/g' \
            {} + 2>/dev/null || true

    # Boost.Process v1 (async_pipe moved in Boost 1.86+)
    find "${SCRIPT_DIR}/src" -type f \( -name '*.cpp' -o -name '*.h' \) \
        -exec sed -i \
            -e 's|#include *<boost/process/async_pipe\.hpp>|#include <boost/process/v1/async_pipe.hpp>|g' \
            -e 's/\bboost::process::async_pipe\b/boost::process::v1::async_pipe/g' \
            {} + 2>/dev/null || true

    # sse2neon reference (AArch64 only — breaks x86_64 video_core builds)
    [[ -f "${SCRIPT_DIR}/src/video_core/CMakeLists.txt" ]] && \
        sed -i '/sse2neon/d' "${SCRIPT_DIR}/src/video_core/CMakeLists.txt" \
        2>/dev/null || true

    # xbyak cmake_minimum_required floor (some forks still carry 2.8)
    [[ -f "${SCRIPT_DIR}/externals/xbyak/CMakeLists.txt" ]] && \
        sed -i 's/cmake_minimum_required(VERSION 2\.8)/cmake_minimum_required(VERSION 3.5)/' \
            "${SCRIPT_DIR}/externals/xbyak/CMakeLists.txt" 2>/dev/null || true

    success "Source patches applied."
}

# =============================================================================
# Profile helpers
# =============================================================================

normalize_profraw_dirs() {
    # IR PGO on Linux writes a directory named default-<pid>.profraw/ containing
    # numbered chunk files rather than a flat .profraw file.  Flatten them so
    # llvm-profdata can glob *.profraw directly.
    local base_dir="$1"
    [[ -d "${base_dir}" ]] || return 0
    local entry
    while IFS= read -r -d '' entry; do
        [[ -d "${entry}" ]] || continue
        local dir_name="${entry##*/}"
        local prefix="${dir_name%.profraw}"
        local idx=0 file
        while IFS= read -r -d '' file; do
            [[ -f "${file}" ]] || continue
            local target="${base_dir}/${prefix}${idx:+-${idx}}.profraw"
            while [[ -e "${target}" ]]; do
                idx=$((idx + 1))
                target="${base_dir}/${prefix}-${idx}.profraw"
            done
            mv "${file}" "${target}"; idx=$((idx + 1))
        done < <(find "${entry}" -maxdepth 1 -type f -name '*.profraw' -print0)
        rm -rf "${entry}"
        info "Flattened profraw directory: ${dir_name}"
    done < <(find "${base_dir}" -maxdepth 1 -type d -name '*.profraw' -print0)
}

_merge_profraw_to_profdata() {
    local profile_dir="$1"; local output_file="$2"
    normalize_profraw_dirs "${profile_dir}"
    local count; count="$(find "${profile_dir}" -maxdepth 1 -name "*.profraw" 2>/dev/null | wc -l)"
    [[ "${count}" -gt 0 ]] || { info "No .profraw files in ${profile_dir}."; return 1; }
    info "Merging ${count} .profraw file(s) → ${output_file##*/}..."
    local cmd="${LLVM_PROFDATA}"
    command -v "${cmd}" &>/dev/null || cmd="llvm-profdata"
    command -v "${cmd}" &>/dev/null || error "llvm-profdata not found. Run setup first."
    "${cmd}" merge --sparse --output="${output_file}" "${profile_dir}"/*.profraw
    success "Merged: ${output_file}"
}

resolve_use_profdata() {
    # Determine which profdata to hand to the use stage.
    # Preference order: merged.profdata (stage1+CS) > default.profdata (stage1) > merge from profraw.
    local merged="${PROFILE_DIR}/merged.profdata"
    local stage1="${PROFILE_DIR}/default.profdata"

    # Invalidate merged if there are newer unmerged CS profraw files
    if [[ -f "${merged}" && -d "${PROFILE_DIR}/cs" ]]; then
        normalize_profraw_dirs "${PROFILE_DIR}/cs" 2>/dev/null || true
        local cs_pending
        cs_pending="$(find "${PROFILE_DIR}/cs" -maxdepth 1 -name "*.profraw" 2>/dev/null | wc -l)"
        if [[ "${cs_pending}" -gt 0 ]]; then
            warn "merged.profdata stale — ${cs_pending} unmerged CS file(s) pending. Rebuilding..."
            rm -f "${merged}"
        fi
    fi

    # Auto-merge CS profraw if present and merged doesn't exist yet
    if [[ ! -f "${merged}" && -d "${PROFILE_DIR}/cs" ]]; then
        normalize_profraw_dirs "${PROFILE_DIR}/cs" 2>/dev/null || true
        local cs_count
        cs_count="$(find "${PROFILE_DIR}/cs" -maxdepth 1 -name "*.profraw" 2>/dev/null | wc -l)"
        if [[ "${cs_count}" -gt 0 ]]; then
            [[ -f "${stage1}" ]] \
                || error "default.profdata missing — run generate + use first before using CS profiles."
            info "CS profraw detected (${cs_count} files) — merging with stage1..."
            local cs_tmp="${PROFILE_DIR}/cs-only.profdata"
            local cmd="${LLVM_PROFDATA}"
            command -v "${cmd}" &>/dev/null || cmd="llvm-profdata"
            "${cmd}" merge --sparse --output="${cs_tmp}" "${PROFILE_DIR}/cs"/*.profraw
            "${cmd}" merge --sparse --output="${merged}" "${stage1}" "${cs_tmp}"
            rm -f "${cs_tmp}"
            success "CS-IRPGO merged profile: ${merged}"
        fi
    fi

    if   [[ -f "${merged}" ]]; then echo "${merged}"; return 0
    elif [[ -f "${stage1}" ]]; then echo "${stage1}"; return 0
    fi

    # Last resort: try merging from profraw
    _merge_profraw_to_profdata "${PROFILE_DIR}" "${stage1}" \
        || error "No profile data found in ${PROFILE_DIR}.\nRun generate, collect profraw by playing games, then re-run use."
    echo "${stage1}"
}

# =============================================================================
# Common cmake arguments (compile/linker flags are per-stage, not here)
# =============================================================================

common_cmake_args() {
    echo \
        "-G" "Ninja" \
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" \
        "-DCMAKE_C_COMPILER=${CLANG}" \
        "-DCMAKE_CXX_COMPILER=${CLANGPP}" \
        "-DCITRON_USE_CPM=ON" \
        "-DCPM_SOURCE_CACHE=${CPM_SOURCE_CACHE}" \
        "-DCITRON_USE_BUNDLED_VCPKG=OFF" \
        "-DCITRON_USE_BUNDLED_QT=ON" \
        "-DUSE_SYSTEM_QT=OFF" \
        "-DENABLE_QT6=ON" \
        "-DCITRON_USE_BUNDLED_FFMPEG=ON" \
        "-DCITRON_TESTS=OFF" \
        "-DCITRON_CHECK_SUBMODULES=OFF" \
        "-DCITRON_USE_LLVM_DEMANGLE=OFF" \
        "-DCITRON_USE_QT_MULTIMEDIA=ON" \
        "-DCITRON_USE_QT_WEB_ENGINE=OFF" \
        "-DENABLE_QT_TRANSLATION=ON" \
        "-DUSE_DISCORD_PRESENCE=ON" \
        "-DENABLE_WEB_SERVICE=ON" \
        "-DENABLE_OPENSSL=ON" \
        "-DBUNDLE_SPEEX=ON" \
        "-DCITRON_USE_FASTER_LD=OFF" \
        "-DCITRON_USE_EXTERNAL_Vulkan_HEADERS=ON" \
        "-DCITRON_USE_EXTERNAL_VULKAN_UTILITY_LIBRARIES=ON" \
        "-DCITRON_USE_AUTO_UPDATER=ON" \
        "-DCITRON_BUILD_TYPE=Release" \
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" \
        "-Wno-dev"

    [[ "${UNITY_BUILD}" == "ON" ]] && echo "-DENABLE_UNITY_BUILD=ON"
}

print_profiling_instructions() {
    local binary="$1"
    echo ""
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    echo -e "${YELLOW}  Training guide for best PGO results${RESET}"
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    echo ""
    echo -e "  ${BOLD}Instrumented binary:${RESET} ${binary}"
    echo -e "  ${BOLD}Profile output:${RESET}      ${PROFILE_DIR}/"
    echo ""
    echo "  1. Run the instrumented binary and play 2-3 games for 5-10 min each."
    echo "     Navigate menus and the game list (profiles UI code too)."
    echo "     Exit cleanly via File > Exit or Ctrl+Q."
    if [[ "${PGO_MODE}" == "ir" ]]; then
        echo ""
        echo "     NOTE (IR PGO): LLVM writes a directory named default-<pid>.profraw/"
        echo "     containing numbered chunks. This is normal — the script flattens"
        echo "     them automatically before merging."
    fi
    echo ""
    echo "  2. Build the optimized binary:"
    echo "     ./build-citron-linux.sh use --pgo ${PGO_MODE} --lto ${LTO_MODE}"
    echo ""
    if [[ "${PGO_MODE}" == "ir" ]]; then
        echo "  Optional: add CS-IRPGO (second profiling session, better inlining data):"
        echo "     ./build-citron-linux.sh use --pgo ir --lto ${LTO_MODE}"
        echo "     ./build-citron-linux.sh csgenerate --pgo ir --lto ${LTO_MODE}"
        echo "     # Run csgenerate binary, exit cleanly, copy cs-default-*.profraw"
        echo "     # to build/pgo-profiles/cs/"
        echo "     ./build-citron-linux.sh use --pgo ir --lto ${LTO_MODE}"
        echo ""
    fi
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    echo ""
}

# =============================================================================
# Sentinel helpers
# =============================================================================

write_gen_sentinel() {
    printf "LTO=%s\nPGO=%s\n" "${LTO_MODE}" "${PGO_MODE}" \
        > "${BUILD_ROOT}/.citron-gen-config"
}

check_stage_compatibility() {
    local gen_cfg="${BUILD_ROOT}/.citron-gen-config"
    [[ -f "${gen_cfg}" ]] || return 0
    local gen_lto gen_pgo
    gen_lto="$(grep -oP '(?<=LTO=)\S+' "${gen_cfg}" 2>/dev/null || true)"
    gen_pgo="$(grep -oP '(?<=PGO=)\S+' "${gen_cfg}" 2>/dev/null || true)"
    if [[ -n "${gen_lto}" && "${gen_lto}" != "${LTO_MODE}" ]]; then
        error "LTO mismatch: generate used LTO=${gen_lto}, this stage has LTO=${LTO_MODE}.\n" \
              "       For IR PGO, LTO must match across all stages. Use --lto ${gen_lto}"
    fi
    if [[ -n "${gen_pgo}" && "${gen_pgo}" != "${PGO_MODE}" ]]; then
        error "PGO mode mismatch: generate used PGO=${gen_pgo}. Use --pgo ${gen_pgo}"
    fi
}

# =============================================================================
# Internal: configure + build in a given directory
# =============================================================================

_build_with_flags() {
    local build_dir="$1"; local stage="$2"
    shift 2; local extra_args=("$@")
    local bt_upper; bt_upper="$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')"

    # nopgo is a convenience shorthand — fill in the extra_args here
    if [[ "${stage}" == "nopgo" && ${#extra_args[@]} -eq 0 ]]; then
        extra_args=(
            "-DCITRON_ENABLE_PGO_GENERATE=OFF"
            "-DCITRON_ENABLE_PGO_USE=OFF"
            "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON"
            "-DCITRON_ENABLE_LTO=$(lto_cmake_flag)"
            "-DCMAKE_C_FLAGS_${bt_upper}=$(build_compile_flags nopgo)"
            "-DCMAKE_CXX_FLAGS_${bt_upper}=$(build_compile_flags nopgo)"
            "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=$(build_linker_flags nopgo)"
        )
    fi

    mkdir -p "${build_dir}"; cd "${build_dir}"
    rm -f CMakeCache.txt; rm -rf CMakeFiles

    # shellcheck disable=SC2046
    cmake "${SCRIPT_DIR}" \
        $(common_cmake_args) \
        "${extra_args[@]}"

    info "Building citron (${BUILD_TYPE}, ${JOBS} jobs)..."
    cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}"
}

# =============================================================================
# Stage: generate
# =============================================================================

stage_generate() {
    header "Stage 1: PGO Instrumented Build (${PGO_MODE} PGO, LTO=${LTO_MODE})"
    command -v "${CLANG}" &>/dev/null || error "${CLANG} not found. Run setup first."
    command -v cmake       &>/dev/null || error "cmake not found."
    command -v ninja       &>/dev/null || error "ninja not found."

    apply_source_patches
    mkdir -p "${BUILD_GENERATE}" "${PROFILE_DIR}"

    # Remove stale profraw from a previous generate run so they don't
    # contaminate a fresh profdata merge.
    local stale
    stale="$(find "${PROFILE_DIR}" -maxdepth 1 \
        \( -name "*.profraw" -o \( -type d -name "*.profraw" \) \) \
        2>/dev/null | wc -l)"
    if [[ "${stale}" -gt 0 ]]; then
        info "Removing ${stale} stale profraw entries from previous generate run..."
        find "${PROFILE_DIR}" -maxdepth 1 -name "*.profraw" -delete
        find "${PROFILE_DIR}" -maxdepth 1 -type d -name "*.profraw" \
            -exec rm -rf {} + 2>/dev/null || true
    fi

    local bt_upper; bt_upper="$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')"
    local compile_flags; compile_flags="$(build_compile_flags generate "${PROFILE_DIR}")"
    local linker_flags;  linker_flags="$(build_linker_flags  generate "${PROFILE_DIR}")"

    info "Compile flags: ${compile_flags}"
    info "Linker flags:  ${linker_flags}"

    _build_with_flags "${BUILD_GENERATE}" generate \
        "-DCITRON_ENABLE_PGO_GENERATE=ON" \
        "-DCITRON_ENABLE_PGO_USE=OFF" \
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
        "-DCITRON_ENABLE_LTO=$(lto_cmake_flag)" \
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}" \
        "-DCMAKE_C_FLAGS_${bt_upper}=${compile_flags}" \
        "-DCMAKE_CXX_FLAGS_${bt_upper}=${compile_flags}" \
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${linker_flags}"

    write_gen_sentinel
    success "Instrumented build: ${BUILD_GENERATE}/bin/citron"
    print_profiling_instructions "${BUILD_GENERATE}/bin/citron"
}

# =============================================================================
# Stage: csgenerate
#
# CRITICAL INVARIANT:
#   Uses default.profdata (stage1 only) as -fprofile-use input.
#   NEVER uses merged.profdata.  If merged.profdata (which contains CS records
#   from a prior CS cycle) were used, the inlining decisions at this stage
#   would diverge from the plain stage1 baseline that the use-stage binary is
#   built from — causing CS counter hash mismatches at the use stage and
#   producing a worse binary than plain IR PGO.
# =============================================================================

stage_csgenerate() {
    header "Stage 1b: CS-IRPGO Instrumented Build"
    [[ "${PGO_MODE}" == "ir" ]] \
        || error "csgenerate requires --pgo ir. CS-IRPGO is not available for FE PGO."
    command -v "${CLANG}" &>/dev/null || error "${CLANG} not found. Run setup first."

    local gen_cfg="${BUILD_ROOT}/.citron-gen-config"
    [[ -f "${gen_cfg}" ]] \
        || error "Generate sentinel not found at ${BUILD_ROOT}/.citron-gen-config.\nRun the generate stage first."
    check_stage_compatibility

    # Locate stage1 profdata — MUST be default.profdata, never merged.profdata
    local stage1_pd="${PROFILE_DIR}/default.profdata"
    if [[ ! -f "${stage1_pd}" ]]; then
        normalize_profraw_dirs "${PROFILE_DIR}"
        local count
        count="$(find "${PROFILE_DIR}" -maxdepth 1 -name "*.profraw" 2>/dev/null | wc -l)"
        if [[ "${count}" -gt 0 ]]; then
            _merge_profraw_to_profdata "${PROFILE_DIR}" "${stage1_pd}"
        elif [[ -f "${PROFILE_DIR}/merged.profdata" ]]; then
            error "default.profdata missing but merged.profdata exists.\n" \
                  "       merged.profdata contains CS records and CANNOT be used as the\n" \
                  "       csgenerate baseline — see script header for explanation.\n" \
                  "       Fix: re-run 'use --pgo ir --lto ${LTO_MODE}' to produce a\n" \
                  "       fresh default.profdata, then re-run csgenerate."
        else
            error "No stage1 profdata or profraw found in ${PROFILE_DIR}/.\n" \
                  "       Run generate, collect profraw by playing games, then:\n" \
                  "         ./build-citron-linux.sh use --pgo ir --lto ${LTO_MODE}\n" \
                  "       (produces default.profdata), then re-run csgenerate."
        fi
    fi
    info "Stage1 profdata (plain IR, no CS): ${stage1_pd}"

    local cs_dir="${PROFILE_DIR}/cs"
    mkdir -p "${BUILD_CSGENERATE}" "${cs_dir}"

    local bt_upper; bt_upper="$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')"
    local compile_flags; compile_flags="$(build_compile_flags csgenerate "${stage1_pd}:${cs_dir}")"
    local linker_flags;  linker_flags="$(build_linker_flags  csgenerate "${stage1_pd}:${cs_dir}")"

    info "Compile flags: ${compile_flags}"
    info "Linker flags:  ${linker_flags}"

    apply_source_patches

    _build_with_flags "${BUILD_CSGENERATE}" csgenerate \
        "-DCITRON_ENABLE_PGO_GENERATE=ON" \
        "-DCITRON_ENABLE_PGO_USE=OFF" \
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
        "-DCITRON_ENABLE_LTO=$(lto_cmake_flag)" \
        "-DCITRON_PGO_PROFILE_DIR=${cs_dir}" \
        "-DCMAKE_C_FLAGS_${bt_upper}=${compile_flags}" \
        "-DCMAKE_CXX_FLAGS_${bt_upper}=${compile_flags}" \
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${linker_flags}"

    success "CS-IRPGO instrumented build: ${BUILD_CSGENERATE}/bin/citron"
    echo ""
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    echo -e "${YELLOW}  NEXT STEP: Collect CS Profile Data (Session 2)${RESET}"
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    echo ""
    echo "  Run: ${BUILD_CSGENERATE}/bin/citron"
    echo "  Play the same games as session 1 for 15-30 min. Exit cleanly."
    echo "  cs-default-<pid>.profraw (or directory) lands next to the binary."
    echo "  Copy it to: ${cs_dir}/"
    echo ""
    echo "  Then rebuild — the use stage merges stage1 + CS automatically:"
    echo "    ./build-citron-linux.sh use --pgo ir --lto ${LTO_MODE}"
    echo ""
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    echo ""
}

# =============================================================================
# Stage: merge
# =============================================================================

stage_merge() {
    header "Merging PGO Profiles"
    [[ -d "${PROFILE_DIR}" ]] || error "Profile directory not found: ${PROFILE_DIR}"
    _merge_profraw_to_profdata "${PROFILE_DIR}" "${PROFILE_DIR}/default.profdata"
    find "${PROFILE_DIR}" -maxdepth 1 -name "*.profraw" -delete
    info "Run 'summary' to check coverage, or 'use' to build the optimized binary."
}

# =============================================================================
# Stage: summary
# =============================================================================

stage_summary() {
    header "Profile Summary"
    [[ -d "${PROFILE_DIR}" ]] || { warn "Profile directory not found: ${PROFILE_DIR}"; return 0; }
    local cmd="${LLVM_PROFDATA}"
    command -v "${cmd}" &>/dev/null || cmd="llvm-profdata"

    for pd in "${PROFILE_DIR}/merged.profdata" "${PROFILE_DIR}/default.profdata"; do
        if [[ -f "${pd}" ]]; then
            local sz; sz="$(du -h "${pd}" | cut -f1)"
            success "Profile: ${pd} (${sz})"
            command -v "${cmd}" &>/dev/null && \
                "${cmd}" show --counts --all-functions "${pd}" 2>/dev/null | tail -6 || true
        fi
    done

    local pr; pr="$(find "${PROFILE_DIR}" -maxdepth 1 -name "*.profraw" 2>/dev/null | wc -l)"
    [[ "${pr}" -gt 0 ]] && warn "${pr} unmerged .profraw file(s). Run: merge"

    local cs; cs="$(find "${PROFILE_DIR}/cs" -name "*.profraw" 2>/dev/null | wc -l)"
    [[ "${cs}" -gt 0 ]] && \
        info "${cs} CS profraw file(s) in pgo-profiles/cs/ (auto-merged on next 'use')"

    [[ ! -f "${PROFILE_DIR}/default.profdata" && "${pr}" -eq 0 ]] && \
        warn "No profile data found in ${PROFILE_DIR}"
}

# =============================================================================
# Stage: use
# =============================================================================

stage_use() {
    if [[ "${PGO_MODE}" == "none" ]]; then
        header "Build: Baseline Release (no PGO, LTO=${LTO_MODE})"
        apply_source_patches
        _build_with_flags "${BUILD_ROOT}/use-nopgo" nopgo
        success "Baseline build: ${BUILD_ROOT}/use-nopgo/bin/citron"
        return 0
    fi

    header "Stage 2: PGO + LTO Optimized Build"
    command -v "${CLANG}" &>/dev/null || error "${CLANG} not found. Run setup first."
    check_stage_compatibility

    local profdata; profdata="$(resolve_use_profdata)"
    [[ "${profdata}" == "${PROFILE_DIR}/merged.profdata" ]] \
        && info "Using CS-IRPGO merged profile: ${profdata}" \
        || info "Using stage1 profile: ${profdata}"

    apply_source_patches

    local bt_upper; bt_upper="$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')"
    local compile_flags; compile_flags="$(build_compile_flags use "${profdata}")"
    local linker_flags;  linker_flags="$(build_linker_flags  use "${profdata}")"

    info "Compile flags: ${compile_flags}"
    info "Linker flags:  ${linker_flags}"
    info "  ↳ -fprofile-use on linker enables LTO backend LTCG with profile guidance"

    _build_with_flags "${BUILD_USE}" use \
        "-DCITRON_ENABLE_PGO_USE=ON" \
        "-DCITRON_ENABLE_PGO_GENERATE=OFF" \
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
        "-DCITRON_ENABLE_LTO=$(lto_cmake_flag)" \
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}" \
        "-DCMAKE_C_FLAGS_${bt_upper}=${compile_flags}" \
        "-DCMAKE_CXX_FLAGS_${bt_upper}=${compile_flags}" \
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${linker_flags}"

    local pgo_label
    [[ "${profdata}" == "${PROFILE_DIR}/merged.profdata" ]] \
        && pgo_label="CS-IRPGO (stage1 IR + CS layer)" \
        || pgo_label="IR PGO (stage1 only)"

    echo ""
    success "════════════════════════════════════════════════════════════════"
    success "  Stage use complete"
    success "  Binary:  ${BUILD_USE}/bin/citron"
    success "  PGO:     ${pgo_label}"
    success "  LTO:     ${LTO_MODE}${LTO_MODE:+ ($(lto_clang_flag))}"
    success "════════════════════════════════════════════════════════════════"
    echo ""
    info "Next steps (optional):"
    echo "  BOLT:      ./build-citron-linux.sh bolt      --pgo ${PGO_MODE} --lto ${LTO_MODE}"
    echo "  Propeller: ./build-citron-linux.sh propeller --pgo ${PGO_MODE} --lto ${LTO_MODE}"
    if [[ "${profdata}" != "${PROFILE_DIR}/merged.profdata" && "${PGO_MODE}" == "ir" ]]; then
        echo ""
        echo "  CS-IRPGO (add a second profiling layer):"
        echo "    ./build-citron-linux.sh csgenerate --pgo ir --lto ${LTO_MODE}"
    fi
    echo ""
}

# =============================================================================
# BOLT: build from source if needed
# =============================================================================

_ensure_bolt() {
    if command -v "${LLVM_BOLT}" &>/dev/null && command -v "${MERGE_FDATA}" &>/dev/null; then
        success "${LLVM_BOLT} available"; return 0
    fi
    if command -v llvm-bolt &>/dev/null; then
        sudo ln -sf "$(command -v llvm-bolt)"   "/usr/local/bin/${LLVM_BOLT}" 2>/dev/null || true
        sudo ln -sf "$(command -v merge-fdata)" "/usr/local/bin/${MERGE_FDATA}" 2>/dev/null || true
        return 0
    fi

    header "Building LLVM BOLT ${CLANG_VERSION} from Source (~15-20 min)"
    local bolt_src="/tmp/llvm-bolt-${CLANG_VERSION}-src"
    local bolt_build="/tmp/llvm-bolt-${CLANG_VERSION}-build"

    local found_tag=""
    for minor in 0.0 1.0 1.1 1.2 1.3 1.4 1.5 1.6 1.7 1.8 1.9; do
        local candidate="llvmorg-${CLANG_VERSION}.${minor}"
        if git ls-remote --tags https://github.com/llvm/llvm-project.git \
                "${candidate}" 2>/dev/null | grep -q "${candidate}"; then
            found_tag="${candidate}"
        fi
    done
    [[ -n "${found_tag}" ]] || error "No LLVM ${CLANG_VERSION} release tag found on GitHub."
    info "Using LLVM tag: ${found_tag}"

    if [[ ! -d "${bolt_src}/.git" ]]; then
        git clone --depth=1 --branch "${found_tag}" \
            --filter=blob:none --sparse \
            https://github.com/llvm/llvm-project.git "${bolt_src}"
        pushd "${bolt_src}" > /dev/null
        git sparse-checkout set llvm bolt cmake third-party
        popd > /dev/null
    fi

    cmake -S "${bolt_src}/llvm" -B "${bolt_build}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DLLVM_ENABLE_PROJECTS="bolt" \
        -DLLVM_TARGETS_TO_BUILD="X86;AArch64" \
        -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DCMAKE_C_COMPILER="${CLANG}" -DCMAKE_CXX_COMPILER="${CLANGPP}"

    cmake --build "${bolt_build}" --target llvm-bolt merge-fdata bolt_rt -j "${JOBS}"

    sudo cp "${bolt_build}/bin/llvm-bolt"   "/usr/local/bin/${LLVM_BOLT}"
    sudo cp "${bolt_build}/bin/merge-fdata" "/usr/local/bin/${MERGE_FDATA}"
    sudo chmod +x "/usr/local/bin/${LLVM_BOLT}" "/usr/local/bin/${MERGE_FDATA}"
    sudo cp "${bolt_build}/lib/libbolt_rt_instr.a"  /usr/local/lib/ 2>/dev/null || true
    sudo cp "${bolt_build}/lib/libbolt_rt_hugify.a" /usr/local/lib/ 2>/dev/null || true
    success "${LLVM_BOLT} installed"
}

# =============================================================================
# Stage: bolt
# =============================================================================

stage_bolt() {
    header "Stage 3A: BOLT Binary Layout Optimization"
    _ensure_bolt

    # BOLT requires the ELF to have been linked with --emit-relocs.
    # We build a dedicated relocs-enabled binary rather than relying on the
    # use-stage binary (which typically is not built with --emit-relocs).
    local relocs_dir="${BUILD_ROOT}/use-relocs"
    if [[ ! -f "${relocs_dir}/bin/citron" ]]; then
        info "Building use-stage binary with --emit-relocs for BOLT..."
        apply_source_patches
        local bt_upper; bt_upper="$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')"

        if [[ "${PGO_MODE}" != "none" ]]; then
            check_stage_compatibility
            local profdata; profdata="$(resolve_use_profdata)"
            local compile_flags; compile_flags="$(build_compile_flags use "${profdata}")"
            # Append --emit-relocs to the use-stage linker flags
            local linker_flags; linker_flags="$(build_linker_flags use "${profdata}") -Wl,--emit-relocs"
            _build_with_flags "${relocs_dir}" use \
                "-DCITRON_ENABLE_PGO_USE=ON" \
                "-DCITRON_ENABLE_PGO_GENERATE=OFF" \
                "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
                "-DCITRON_ENABLE_LTO=$(lto_cmake_flag)" \
                "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}" \
                "-DCMAKE_C_FLAGS_${bt_upper}=${compile_flags}" \
                "-DCMAKE_CXX_FLAGS_${bt_upper}=${compile_flags}" \
                "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${linker_flags}"
        else
            local compile_flags; compile_flags="$(build_compile_flags nopgo)"
            local linker_flags; linker_flags="$(build_linker_flags nopgo) -Wl,--emit-relocs"
            _build_with_flags "${relocs_dir}" use \
                "-DCITRON_ENABLE_PGO_USE=OFF" \
                "-DCITRON_ENABLE_PGO_GENERATE=OFF" \
                "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
                "-DCITRON_ENABLE_LTO=$(lto_cmake_flag)" \
                "-DCMAKE_C_FLAGS_${bt_upper}=${compile_flags}" \
                "-DCMAKE_CXX_FLAGS_${bt_upper}=${compile_flags}" \
                "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${linker_flags}"
        fi
        success "use-with-relocs build: ${relocs_dir}/bin/citron"
    fi

    local elf_binary="${relocs_dir}/bin/citron"
    mkdir -p "${BOLT_PROFILE_DIR}" "${BUILD_BOLT}"
    local instrumented="${BUILD_BOLT}/citron-bolt-instrumented"
    local fdata_pattern="${BOLT_PROFILE_DIR}/citron-%p.fdata"
    local merged_fdata="${BOLT_PROFILE_DIR}/citron-merged.fdata"

    info "Instrumenting ELF with BOLT..."
    "${LLVM_BOLT}" "${elf_binary}" \
        --instrument \
        --instrumentation-file="${fdata_pattern}" \
        --instrumentation-file-append-pid \
        -o "${instrumented}"
    success "Instrumented: ${instrumented}"

    echo ""
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    echo -e "${YELLOW}  Run the BOLT-instrumented binary${RESET}"
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    echo ""
    echo "    ${instrumented}"
    echo ""
    echo "  Play for 15-30 min. Exit cleanly."
    echo "  fdata files will appear in: ${BOLT_PROFILE_DIR}/"
    echo ""
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${RESET}"
    read -rp "  Press Enter once you have exited the instrumented binary... "
    echo ""

    local fdata_count
    fdata_count="$(find "${BOLT_PROFILE_DIR}" -name "*.fdata" 2>/dev/null | wc -l)"
    [[ "${fdata_count}" -gt 0 ]] \
        || error "No .fdata files found in ${BOLT_PROFILE_DIR}. Run the instrumented binary first."
    info "Merging ${fdata_count} .fdata file(s)..."
    "${MERGE_FDATA}" "${BOLT_PROFILE_DIR}"/*.fdata -o "${merged_fdata}"
    success "Merged: ${merged_fdata}"

    info "Optimizing ELF with BOLT..."
    "${LLVM_BOLT}" "${elf_binary}" \
        -p "${merged_fdata}" \
        --reorder-blocks=ext-tsp \
        --reorder-functions=cdsort \
        --split-functions \
        --split-all-cold \
        --split-eh \
        --dyno-stats \
        -o "${BUILD_BOLT}/citron" \
        || error "BOLT optimization failed."

    echo ""
    success "════════════════════════════════════════════════════════════════"
    success "  Stage bolt complete"
    success "  Binary: ${BUILD_BOLT}/citron"
    success "  Optimizations: PGO + LTO + BOLT basic-block reordering"
    success "════════════════════════════════════════════════════════════════"
}

# =============================================================================
# Propeller helpers
# =============================================================================

_ensure_create_llvm_prof() {
    local ver_sentinel="/usr/local/bin/.create_llvm_prof_ver"
    local clang_ver; clang_ver="$("${CLANG}" --version 2>&1 | head -1 || echo unknown)"

    if command -v create_llvm_prof &>/dev/null; then
        local stored=""; [[ -f "${ver_sentinel}" ]] && stored="$(cat "${ver_sentinel}")"
        if [[ "${clang_ver}" == "${stored}" ]]; then
            success "create_llvm_prof available"; return 0
        fi
        warn "create_llvm_prof version mismatch — rebuilding."
        sudo rm -f /usr/local/bin/create_llvm_prof "${ver_sentinel}"
    fi

    header "Building create_llvm_prof (google/llvm-propeller)"
    local pkg_mgr; pkg_mgr="$(detect_pkg_manager)"
    case "${pkg_mgr}" in
        apt)    sudo apt-get install -y libelf-dev libssl-dev libzstd-dev 2>/dev/null || true ;;
        pacman) sudo pacman -S --needed --noconfirm elfutils openssl zstd 2>/dev/null || true ;;
        dnf)    sudo dnf install -y elfutils-libelf-devel openssl-devel libzstd-devel 2>/dev/null || true ;;
        zypper) sudo zypper install -y libelf-devel libopenssl-devel libzstd-devel 2>/dev/null || true ;;
    esac

    local src="/tmp/propeller-src" bld="/tmp/propeller-build"
    [[ -d "${src}/.git" ]] \
        || git clone --depth=1 https://github.com/google/llvm-propeller.git "${src}"
    rm -rf "${bld}"
    CC="${CLANG}" CXX="${CLANGPP}" \
        cmake -S "${src}" -B "${bld}" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        || error "llvm-propeller cmake configure failed."
    cmake --build "${bld}" --target generate_propeller_profiles -j "${JOBS}" \
        || error "llvm-propeller build failed."
    sudo cp "${bld}/propeller/generate_propeller_profiles" /usr/local/bin/create_llvm_prof
    sudo chmod +x /usr/local/bin/create_llvm_prof
    printf '%s' "${clang_ver}" | sudo tee "${ver_sentinel}" > /dev/null
    success "create_llvm_prof installed."
}

# =============================================================================
# Stage: propeller
# =============================================================================

stage_propeller() {
    header "Stage 3B: Propeller BB+Function Layout Optimization"
    command -v perf &>/dev/null \
        || error "perf not found. Install linux-tools-$(uname -r) and re-run."
    _ensure_create_llvm_prof

    # Build a binary with -fbasic-block-address-map for perf profiling.
    # LTO is intentionally OFF: the ThinLTO backend in lld does not propagate
    # -fbasic-block-address-map through the link step, so .llvm_bb_addr_map
    # sections would be absent in the final binary. PGO data alone provides
    # sufficient representative coverage for the Propeller conversion step.
    local bb_dir="${BUILD_ROOT}/use-bb"
    if [[ ! -f "${bb_dir}/bin/citron" ]]; then
        info "Building BB-annotated binary (-fbasic-block-address-map, no LTO)..."
        apply_source_patches
        local bt_upper; bt_upper="$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')"
        local debug_flag=""; [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
        local pgo_part="" pgo_enable="OFF"
        if [[ "${PGO_MODE}" != "none" ]]; then
            check_stage_compatibility
            local profdata; profdata="$(resolve_use_profdata)"
            pgo_part="$(pgo_use_compile_flag "${profdata}")"
            pgo_enable="ON"
        fi
        local bb_cflags="-O3 -DNDEBUG ${debug_flag} -USuccess -UNone ${ARCH_FLAGS:-} -Wno-error -w ${pgo_part} -fbasic-block-address-map"
        local bb_linker="-fuse-ld=${LLD} -Wl,--emit-relocs"
        _build_with_flags "${bb_dir}" use \
            "-DCITRON_ENABLE_PGO_USE=${pgo_enable}" \
            "-DCITRON_ENABLE_PGO_GENERATE=OFF" \
            "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
            "-DCITRON_ENABLE_LTO=OFF" \
            "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}" \
            "-DCMAKE_C_FLAGS_${bt_upper}=${bb_cflags}" \
            "-DCMAKE_CXX_FLAGS_${bt_upper}=${bb_cflags}" \
            "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${bb_linker}"
        success "BB-annotated binary: ${bb_dir}/bin/citron"
    fi

    local elf_binary="${bb_dir}/bin/citron"
    mkdir -p "${PROPELLER_PROFILE_DIR}" "${BUILD_PROPELLER}/bin"
    local perf_data="${PROPELLER_PROFILE_DIR}/perf.data"
    local cc_profile="${PROPELLER_PROFILE_DIR}/propeller_cc.prof"
    local symorder="${PROPELLER_PROFILE_DIR}/propeller_symorder.txt"

    # Ensure perf can record branch stacks
    local paranoid; paranoid="$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 1)"
    if [[ "${paranoid}" -gt 1 ]]; then
        warn "perf_event_paranoid=${paranoid} — branch stacks require <= 1. Setting now..."
        sudo sysctl -w kernel.perf_event_paranoid=1 \
            || error "Could not set perf_event_paranoid. Run: sudo sysctl kernel.perf_event_paranoid=1"
    fi

    if [[ ! -f "${perf_data}" ]]; then
        echo ""
        echo -e "${YELLOW}═══════════════════════════════════════════════════════════════${RESET}"
        echo -e "${YELLOW}  Propeller — Branch Profile Collection${RESET}"
        echo -e "${YELLOW}═══════════════════════════════════════════════════════════════${RESET}"
        echo ""
        echo "  Run the following to collect a branch-stack profile:"
        echo ""
        echo "    perf record -b -e cycles:u \\"
        echo "        -o ${perf_data} \\"
        echo "        -- ${elf_binary}"
        echo ""
        echo "  Play for 15-30 minutes. Exit citron cleanly."
        echo ""
        echo -e "${YELLOW}═══════════════════════════════════════════════════════════════${RESET}"
        read -rp "  Press Enter once perf has finished and perf.data is written... "
        echo ""
        [[ -f "${perf_data}" ]] \
            || error "perf.data not found at ${perf_data}. Run the perf command above first."
    fi

    info "Converting perf branch data to Propeller profiles..."
    create_llvm_prof \
        --binary="${elf_binary}" \
        --profile="${perf_data}" \
        --cc_profile="${cc_profile}" \
        --ld_profile="${symorder}" \
        2>&1 || error "generate_propeller_profiles failed — check perf.data and ELF compatibility."

    [[ -f "${cc_profile}" ]] && success "CC profile: ${cc_profile} ($(wc -l < "${cc_profile}") entries)"
    [[ -f "${symorder}"   ]] && success "LD profile: ${symorder} ($(wc -l < "${symorder}") functions)"

    # Final rebuild: PGO + LTO + Propeller
    info "Rebuilding optimized binary with Propeller profiles (PGO + LTO + Propeller)..."
    apply_source_patches
    local bt_upper; bt_upper="$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')"
    local debug_flag=""; [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
    local lto_flag; lto_flag="$(lto_clang_flag)"
    local pgo_part="" pgo_enable="OFF"
    if [[ "${PGO_MODE}" != "none" ]]; then
        check_stage_compatibility
        local profdata; profdata="$(resolve_use_profdata)"
        pgo_part="$(pgo_use_compile_flag "${profdata}")"
        pgo_enable="ON"
    fi

    local prop_cflags="-O3 -DNDEBUG ${debug_flag} -USuccess -UNone ${ARCH_FLAGS:-} -Wno-error -w ${lto_flag:+${lto_flag} }${pgo_part}"
    [[ -f "${cc_profile}" ]] && prop_cflags="${prop_cflags} -fbasic-block-sections=list=${cc_profile}"

    # -fprofile-use on the linker so LTO backend gets profile guidance
    local prop_linker="-fuse-ld=${LLD} ${debug_flag:+${debug_flag} }${lto_flag:+${lto_flag} }${pgo_part}"
    [[ -f "${symorder}" ]] && prop_linker="${prop_linker} -Wl,--symbol-ordering-file=${symorder}"

    _build_with_flags "${BUILD_PROPELLER}" use \
        "-DCITRON_ENABLE_PGO_USE=${pgo_enable}" \
        "-DCITRON_ENABLE_PGO_GENERATE=OFF" \
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
        "-DCITRON_ENABLE_LTO=$(lto_cmake_flag)" \
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}" \
        "-DCMAKE_C_FLAGS_${bt_upper}=${prop_cflags}" \
        "-DCMAKE_CXX_FLAGS_${bt_upper}=${prop_cflags}" \
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${prop_linker}"

    echo ""
    success "════════════════════════════════════════════════════════════════"
    success "  Stage propeller complete"
    success "  Binary: ${BUILD_PROPELLER}/bin/citron"
    success "  Optimizations: PGO + LTO + Propeller BB+function layout"
    success "════════════════════════════════════════════════════════════════"
}

# =============================================================================
# Stage: clean
# =============================================================================

stage_clean() {
    header "Cleaning Build Directories"
    local backup="/tmp/citron-pgo-backup-$$"
    [[ -d "${PROFILE_DIR}" ]] && { info "Backing up profile data..."; cp -r "${PROFILE_DIR}" "${backup}"; }
    [[ -d "${BUILD_ROOT}" ]]  && { info "Removing ${BUILD_ROOT}..."; rm -rf "${BUILD_ROOT}"; }
    if [[ -d "${backup}" ]]; then
        mkdir -p "${PROFILE_DIR}"
        cp -r "${backup}/." "${PROFILE_DIR}/"
        rm -rf "${backup}"
        success "Profile data preserved: ${PROFILE_DIR}"
    fi
    success "Clean complete."
}

# =============================================================================
# Argument parsing
# =============================================================================

STAGE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        setup|generate|csgenerate|merge|summary|use|bolt|propeller|clean)
            STAGE="$1"; shift ;;
        --build)
            BUILD_ROOT="$2"; _set_derived_paths; shift 2 ;;
        --jobs|-j)
            JOBS="$2"; shift 2 ;;
        --pgo-type|--pgo)
            case "$2" in
                ir|fe|none) PGO_MODE="$2"; shift 2 ;;
                *) error "--pgo requires: ir, fe, or none" ;;
            esac ;;
        --lto)
            case "$2" in
                thin|full|none) LTO_MODE="$2"; shift 2 ;;
                *) error "--lto requires: thin, full, or none" ;;
            esac ;;
        --lite-lto)       LTO_MODE="thin"; shift ;;
        --no-lto)         LTO_MODE="none"; shift ;;
        --arch)
            case "$2" in
                x86_64|v3|aarch64|auto) _ARCH_ARG="$2"; shift 2 ;;
                *) error "--arch requires: x86_64, v3, aarch64, or auto" ;;
            esac ;;
        --unity)          UNITY_BUILD="ON";  shift ;;
        --no-unity)       UNITY_BUILD="OFF"; shift ;;
        --relwithdebinfo) BUILD_TYPE="RelWithDebInfo"; shift ;;
        --clang-version)
            CLANG_VERSION="$2"; _set_clang_tools; shift 2 ;;
        --help|-h)
            sed -n '/^# build-citron-linux/,/^# ===/p' "$0" | head -130
            exit 0 ;;
        *) error "Unknown argument: $1\nRun with --help for usage." ;;
    esac
done

[[ -n "${STAGE}" ]] \
    || error "No stage specified.\nUsage: $0 <stage> [options]\nStages: setup generate csgenerate merge summary use bolt propeller clean"

resolve_arch_flags

case "${STAGE}" in
    setup)       stage_setup      ;;
    generate)    stage_generate   ;;
    csgenerate)  stage_csgenerate ;;
    merge)       stage_merge      ;;
    summary)     stage_summary    ;;
    use)         stage_use        ;;
    bolt)        stage_bolt       ;;
    propeller)   stage_propeller  ;;
    clean)       stage_clean      ;;
esac
