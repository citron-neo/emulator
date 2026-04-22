#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
# =============================================================================
# build-clangtron-windows.sh — PGO + LTO + PLO optimized cross-compilation build script
#
# Builds Citron (Nintendo Switch emulator) for Windows (x86_64-w64-mingw32)
# from a Linux host, using a multi-stage compiler optimization pipeline:
#
#   Stage 1 (generate):    Build with Clang PGO instrumentation (FE or IR).
#   Stage 1b (csgenerate): [IR PGO only] Build with context-sensitive IR
#                          instrumentation layered on stage1 profile data.
#                          Requires a second Windows profiling session.
#   Stage 2 (use):         Rebuild using collected profile(s) + LTO.
#                          Auto-merges CS profraw if pgo-profiles/cs/ is
#                          populated. Builds the Windows PE only.
#   Stage 2b (build-elf):  Build native Linux ELF with BBAddrMap sections
#                          for BOLT/Propeller profiling. Invoked on-demand
#                          by bolt/propeller if not already present.
#   Stage 3 (choose one):
#     bolt       BOLT ELF-proxy: instruments the Linux ELF, profiles it natively,
#                extracts hot function order, re-links the Windows PE with /order:@
#     propeller  Propeller: collects perf LBR data from the Linux ELF, generates
#                a BB+function layout profile, rebuilds the Windows PE with /order:@
#
# PGO MODES (--pgo-type):
#
#   fe — Frontend PGO (-fprofile-instr-generate / -fprofile-instr-use):
#     Counters are inserted before LLVM optimization passes, at the AST/frontend
#     level. More robust to flag changes between generate and use. CS-IRPGO is
#     not available with fe.
#
#   ir — LLVM IR PGO (-fprofile-generate / -fprofile-use):  [DEFAULT]
#     Counters are inserted at the LLVM IR level, after early optimization passes
#     (SRO, SROA, etc.). The profile reflects the code structure that the
#     optimizer actually sees, making inlining and branch decisions more accurate.
#     For an emulator with complex JIT/dispatch paths, IR PGO typically yields
#     2-5% better runtime performance than FE PGO.
#
#     CRITICAL: IR PGO profiles are tied to the LLVM IR produced at generate
#     time. The --lto value and optimization flags MUST be identical between
#     generate, csgenerate, and use. Only the propeller/bolt final relink
#     may use a different --lto value.
#
#   CS-IRPGO (ir + csgenerate stage):
#     Context-Sensitive IR PGO adds a second instrumentation pass on top of an
#     already-PGO-optimized binary. The CS layer captures per-call-site counter
#     data rather than per-function-definition data, giving the compiler separate
#     profiles for each inlined instance of a function. For an emulator where
#     the same JIT/memory/GPU functions are called from both inner loops and cold
#     init paths, this provides substantially better inlining decisions.
#
#     CS-IRPGO requires two Windows profiling sessions:
#       Session 1: run the generate binary (stage1, standard IR instrumentation)
#       Session 2: run the csgenerate binary (CS instrumentation built on top of
#                  the stage1 profile — the binary is already PGO-optimized at
#                  stage1 quality before the CS counters are applied)
#     The use stage auto-detects pgo-profiles/cs/ and merges both profiles.
#
#     CRITICAL INVARIANT — csgenerate must always use default.profdata (stage1
#     only) as the input to -fprofile-use, never merged.profdata (which contains
#     CS records from a prior CS cycle). If merged.profdata were used:
#       - The compiler applies stale CS data (keyed to the previous csgenerate
#         binary's IR) during the new csgenerate build
#       - Inlining decisions change relative to the plain stage1 baseline,
#         restructuring the IR the new CS counters are keyed to
#       - The new CS profraw then hash-mismatches during the use stage because
#         the use stage builds on the plain stage1 IR, not the doubly-CS-
#         influenced one
#     This invariant is enforced by the script: csgenerate always requires
#     default.profdata and refuses to run if only merged.profdata is present.
#
# PROFILE RUNTIME (applies to ALL PGO modes — FE, IR, and CS-IRPGO):
#   All three instrumentation modes write .profraw files using the same LLVM
#   InstrProfiling runtime library (libclang_rt.profile.a). On a Windows PE
#   cross-compiled with llvm-mingw, this runtime must be present and must include
#   POSIX stubs for symbols (mmap, flock, etc.) that the MinGW runtime does not
#   provide. The ensure_profile_runtime_mingw() function verifies and rebuilds
#   this library if needed. The -u,__llvm_profile_write_file and
#   -u,__llvm_profile_runtime linker flags prevent lld from dead-stripping the
#   runtime entry points. These mechanisms apply equally to FE, IR, and
#   CS-IRPGO generate/csgenerate binaries.
#
# LTO + PGO LINKER FLAGS (use stage):
#   For full LTO (-flto), Clang/LLD re-runs the optimization backend at link
#   time across all merged bitcode modules. The -fprofile-use=... flag must be
#   present on the LINKER command line (CMAKE_EXE_LINKER_FLAGS_RELEASE) as well
#   as the compile flags, so the LTO backend can apply the profile during link-
#   time code generation. Without this, cross-TU inlining and hot-path layout
#   decisions made during LTO run without profile guidance, negating the main
#   benefit of full LTO. The use stage sets CMAKE_EXE_LINKER_FLAGS_RELEASE to
#   include both the LTO flag and -fprofile-use.
#
# HOW BOLT WORKS HERE:
#   BOLT operates on ELF binaries natively — PE/COFF support does not exist.
#   Instead, a native Linux ELF is built alongside the PE, profiled under BOLT
#   instrumentation, and the resulting hot function order is fed to lld's /order:@
#   flag when re-linking the final Windows PE. Agreement rate ~38-64%: many ELF
#   hot functions are inlined away by full LTO in the PE.
#
# HOW PROPELLER WORKS HERE:
#   Propeller uses Linux perf with LBR (Last Branch Record) to collect a
#   branch-trace profile of the native Linux ELF, then runs
#   generate_propeller_profiles (google/llvm-propeller) to produce:
#     - propeller_cc.prof       Basic-block layout profile (ELF-only, not used for PE)
#     - propeller_symorder.txt  Hot function order (fed to lld /order:@ for PE)
#   The PE rebuild uses PGO + LTO + /order:@ function ordering. Basic-block
#   layout (the CC profile) cannot currently be applied to COFF/PE targets
#   because -fbasic-block-sections=list is ELF-only.
#
#   FUTURE: COFF/Windows BBAddrMap support is being added to LLVM. When merged
#   into llvm-mingw, the CC profile can be applied to the PE build as well,
#   recovering the intra-function BB layout benefit currently limited to ELF.
#   Track progress at:
#     PR:  https://github.com/llvm/llvm-project/pull/187268
#     RFC: https://discourse.llvm.org/t/rfc-extend-bbaddrmap-support-to-coff-windows/90232
#
# TOOLCHAIN:
#   Cross-compilation uses llvm-mingw — a self-contained Clang/LLD/libc++/
#   compiler-rt MinGW-w64 toolchain. The host LLVM install (clang-21,
#   llvm-profdata, llvm-bolt) is used for PGO merging, BOLT, and the Linux ELF.
#
# USAGE:
#   ./build-clangtron-windows.sh [stage] [options]
#
#   Stages:
#     setup       Install all dependencies (run once on a new machine)
#     generate    Stage 1:  Build PGO-instrumented Windows PE (FE or IR PGO)
#     csgenerate  Stage 1b: [IR PGO only] Build CS-instrumented Windows PE.
#                           Requires default.profdata from a prior generate run.
#                           Produces build/cs-generate/bin/citron.exe; CS profraw
#                           goes to pgo-profiles/cs/ after the Windows session.
#     use         Stage 2:  Build PGO+LTO Windows PE only.
#                           Auto-merges CS profraw in pgo-profiles/cs/ if present.
#     build-elf   Stage 2b: Build native Linux ELF with BBAddrMap sections
#                           (-fbasic-block-address-map) for BOLT/Propeller profiling.
#                           Built on-demand by bolt/propeller if not already present.
#                           Use --pgo none to build a baseline ELF without PGO:
#                             ./build-clangtron-windows.sh build-elf --pgo none
#     bolt        Stage 3A: BOLT function-order optimization (ELF-proxy → PE)
#     propeller   Stage 3B: Propeller BB+function layout (perf LBR → PE)
#     clean       Remove build directory
#
#   Options:
#     --source DIR             Path to citron source tree (default: cwd)
#     --build DIR              Path to build directory (default: ./build)
#     --jobs N                 Parallel jobs (default: nproc)
#     --lto thin|full|none     LTO mode (default: full)
#                              MUST match between generate, csgenerate, and use.
#                              Only propeller/bolt (final relink) may differ.
#     --lite-lto               Alias for --lto thin
#     --no-lto                 Alias for --lto none
#     --pgo-type ir|fe|none    PGO instrumentation mode (default: ir)
#                              ir   = LLVM IR PGO (-fprofile-generate / -fprofile-use).
#                                     Counters at the optimized-IR level. More accurate
#                                     for inlining decisions. Required for CS-IRPGO.
#                                     LTO and flag set MUST match across all stages.
#                              fe   = Frontend PGO (-fprofile-instr-generate / -use).
#                                     Counters before optimizations. More robust to
#                                     flag changes. CS-IRPGO not available with fe.
#                              none = No PGO. Baseline build (use and build-elf stages).
#                                     use:       outputs to build/use-nopgo/.
#                                     build-elf: outputs to build/use-nopgo-elf/.
#                                     LTO still applies for use; build-elf always
#                                     disables LTO (required for BBAddrMap sections).
#                                     Use --lto none for a fully unoptimized PE:
#                                       ./build-clangtron-windows.sh use --pgo none --lto none
#                              MUST match between generate, csgenerate, and use
#                              (except none, which skips profdata entirely).
#     --unity                  Enable unity builds (passes ENABLE_UNITY_BUILD=ON)
#                              ~30-90% faster compilation; no runtime effect.
#     --clang-version N        Host Clang version (default: 21)
#     --relwithdebinfo         Enable RelWithDebInfo build (Release with debug symbols).
#                              Injects -g into all build stages while keeping O3/LTO/PGO.
#     --llvm-mingw-version VER llvm-mingw release tag (default: 20260224)
#
#   LTO mode details:
#     full → Full LTO (-flto). Best runtime performance; most aggressive inlining
#            reduces BOLT/Propeller agreement rates (~38-44%).
#     thin → ThinLTO (-flto=thin). Faster builds, slightly higher agreement rates.
#     none → No LTO. Not recommended; significantly reduced performance.
#
#     Stage 1 (generate/csgenerate): instruments the Windows PE.
#       IR PGO: LTO mode affects which IR is instrumented — must match use.
#       FE PGO: LTO does not affect counter placement — more forgiving.
#     Stage 2 (use): builds PGO+LTO Windows PE only.
#     Stage 2b (build-elf): builds the native Linux ELF for BOLT/Propeller.
#       The Linux ELF always omits LTO to allow -fbasic-block-address-map
#       to emit BBAddrMap sections (LTO prevents this at the TU level).
#     Stage 3A (bolt):      re-links Windows PE with BOLT function order.
#     Stage 3B (propeller): rebuilds Windows PE with Propeller function order.
#       Both stage 3 variants may use a different --lto than stages 1-2.
#
# REQUIREMENTS (installed by the setup stage):
#   - clang/clang++ 21+      Host compiler (PGO merge, BOLT/Propeller ELF build)
#   - lld                    Linker (LTO)
#   - llvm-profdata          Merges .profraw -> .profdata
#   - llvm-bolt              Binary optimization tool (ELF only)
#   - perf                   Linux perf with LBR support (for Propeller)
#   - llvm-mingw             Self-contained Clang+libc++/compiler-rt MinGW toolchain
#   - cmake + ninja-build    Build system
#
# EXAMPLE FULL PIPELINE — IR PGO (recommended):
#   ./build-clangtron-windows.sh setup
#   ./build-clangtron-windows.sh generate --pgo-type ir --lto full
#   # Copy build/generate/bin/ to Windows, run citron.exe for 15-30 min.
#   # default-<pid>.profraw appears next to citron.exe on exit.
#   # Copy the .profraw file(s) to build/pgo-profiles/
#   ./build-clangtron-windows.sh use --pgo-type ir --lto full
#   # Then propeller or bolt (may use a different --lto for the relink):
#   ./build-clangtron-windows.sh propeller --pgo-type ir --lto full
#   # Final binary: build/propeller/bin/citron.exe
#
# EXAMPLE FULL PIPELINE — CS-IRPGO (two Windows sessions, best quality):
#   ./build-clangtron-windows.sh setup
#
#   # --- Session 1: Standard IR instrumentation ---
#   ./build-clangtron-windows.sh generate --pgo-type ir --lto full
#   # Copy build/generate/bin/ to Windows. Run citron.exe for 15-30 min.
#   # Copy default-<pid>.profraw back to build/pgo-profiles/
#
#   # Merge stage1 profraw → default.profdata (required before csgenerate).
#   # This also gives you a usable PGO+LTO binary without the CS layer.
#   ./build-clangtron-windows.sh use --pgo-type ir --lto full
#
#   # --- Session 2: Context-sensitive instrumentation ---
#   # csgenerate builds a NEW binary using ONLY default.profdata for -fprofile-use
#   # (never merged.profdata — see CRITICAL INVARIANT above) and adds CS counters.
#   ./build-clangtron-windows.sh csgenerate --pgo-type ir --lto full
#   # Copy build/cs-generate/bin/ to Windows. Run citron.exe for 15-30 min.
#   # cs-default-<pid>.profraw is written next to citron.exe on exit.
#   # Copy cs-default-*.profraw to build/pgo-profiles/cs/
#
#   # --- Final build: use auto-detects CS profiles and merges them ---
#   # With pgo-profiles/cs/ populated, use:
#   #   1. Merges cs-default-*.profraw → cs-only.profdata
#   #   2. Merges default.profdata + cs-only.profdata → merged.profdata
#   #   3. Rebuilds the PE with -fprofile-use=merged.profdata + LTO
#   #      (linker also gets -fprofile-use for LTO backend LTCG)
#   ./build-clangtron-windows.sh use --pgo-type ir --lto full
#
#   # Propeller or BOLT (final relink; --lto here may differ):
#   ./build-clangtron-windows.sh propeller --pgo-type ir --lto full
#   # Final binary: build/propeller/bin/citron.exe
#
# EXAMPLE FULL PIPELINE — Frontend PGO (simpler, flag-change tolerant):
#   ./build-clangtron-windows.sh setup
#   ./build-clangtron-windows.sh generate --pgo-type fe --lto full
#   # Copy build/generate/bin/ to Windows, collect default-*.profraw
#   # Copy profraw to build/pgo-profiles/
#   ./build-clangtron-windows.sh use --pgo-type fe --lto full
#   ./build-clangtron-windows.sh propeller --pgo-type fe --lto full
#   # Final binary: build/propeller/bin/citron.exe
#
#   Option A — BOLT ELF-proxy (function-level reordering):
#   ./build-clangtron-windows.sh bolt --pgo-type ir --lto full
#   # bolt pauses: run build/use-elf/bin/citron-bolt-instrumented on Linux
#   # Play for 15-30 minutes, exit cleanly, then press Enter
#   # Final binary: build/bolt/bin/citron.exe
# =============================================================================

set -euo pipefail

# =============================================================================
# Configuration
# =============================================================================

CLANG_VERSION="${CLANG_VERSION:-21}"


# llvm-mingw release tag — cross-compilation toolchain (Clang+libc++/compiler-rt)
# https://github.com/mstorsjo/llvm-mingw/releases
LLVM_MINGW_VERSION="${LLVM_MINGW_VERSION:-20260224}"

SOURCE_DIR="${SOURCE_DIR:-$(pwd)}"
BUILD_ROOT="${BUILD_ROOT:-$(pwd)/build}"
JOBS="${JOBS:-$(nproc)}"
LTO_MODE="${LTO_MODE:-full}"
PGO_MODE="${PGO_MODE:-ir}"     # ir = LLVM IR PGO (-fprofile-generate/-fprofile-use)
                               # fe = Frontend PGO (-fprofile-instr-generate/-fprofile-instr-use)
UNITY_BUILD="${UNITY_BUILD:-OFF}"   # ENABLE_UNITY_BUILD: batch TUs to speed up compilation
BUILD_TYPE="${BUILD_TYPE:-Release}" # Release, RelWithDebInfo

# =============================================================================
# Host OS detection
# =============================================================================

_HOST_OS="linux"
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) _HOST_OS="windows" ;;
    Darwin*)               _HOST_OS="macos" ;;
esac

# MSYS2 clang64 toolchain prefix.  On Windows/MSYS2 this replaces llvm-mingw:
# the clang64 directory layout mirrors llvm-mingw exactly —
#   bin/  x86_64-w64-mingw32-clang, -clang++, llvm-dlltool, -windres, llvm-ar …
#   x86_64-w64-mingw32/{include,lib,bin}/  (sysroot headers + runtime DLLs)
# Override with MSYS2_PREFIX env var when using ucrt64/clang32/etc.
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

# On MSYS2/Windows the clang64 prefix IS the llvm-mingw equivalent.
# On Linux we download a pre-built llvm-mingw release into the build root.
if [[ "${_HOST_OS}" == "windows" ]]; then
    LLVM_MINGW_DIR="${MSYS2_PREFIX}"
else
    LLVM_MINGW_DIR="${BUILD_ROOT}/llvm-mingw"
fi

CLANG="clang-${CLANG_VERSION}"
CLANGPP="clang++-${CLANG_VERSION}"
LLVM_PROFDATA="llvm-profdata-${CLANG_VERSION}"
LLVM_BOLT="llvm-bolt-${CLANG_VERSION}"
MERGE_FDATA="merge-fdata-${CLANG_VERSION}"

# On MSYS2/Windows, LLVM tools are unversioned; BOLT/Propeller are Linux-only.
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

# _sudo — portable sudo wrapper.
# On Windows/MSYS2, sudo is unavailable; run privileged commands directly.
# On Linux, delegate to the real sudo as usual.
_sudo() {
    if [[ "${_HOST_OS}" == "windows" ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

# require_llvm_mingw — validate and activate the MinGW cross/native toolchain.
#
# On Linux  : verifies the downloaded llvm-mingw toolchain is present
#             (downloading it if the sentinel is missing via ensure_llvm_mingw),
#             then prepends its bin/ to PATH and sets MINGW_CLANG/MINGW_CLANGPP.
#             Replaces the "ensure_llvm_mingw; setup_llvm_mingw_path" pair that
#             formerly appeared verbatim at the top of every build stage.
#
# On Windows: resolves MINGW_CLANG/MINGW_CLANGPP from the MSYS2 clang64
#             environment.  PATH is already configured by the MSYS2 shell.
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
# Downloads and extracts the llvm-mingw pre-built cross-compilation toolchain.
# Provides Clang+LLD+libc++/compiler-rt for Windows targets.
#
# Eliminates all GCC runtime workarounds:
#   - No std::__once_callable TLS/non-TLS ABI mismatch (libc++ doesn't use these)
#   - No dual GCC variant detection (posix vs win32 threading model)
#   - No --whole-archive libstdc++ hackery
#   - No --gcc-toolchain flag or manual GCC include/lib paths
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

    mkdir -p "${BUILD_ROOT}"
    info "Downloading llvm-mingw ${LLVM_MINGW_VERSION}..."
    info "  URL: ${url}"
    wget --quiet --show-progress -O "${BUILD_ROOT}/${tarball}" "${url}" \
        || error "Failed to download llvm-mingw — check network or LLVM_MINGW_VERSION"

    info "Extracting llvm-mingw..."
    tar -xf "${BUILD_ROOT}/${tarball}" -C "${BUILD_ROOT}"
    rm -f "${BUILD_ROOT}/${tarball}"

    # Find the extracted directory (name includes version and platform)
    local extract_dir
    extract_dir="$(find "${BUILD_ROOT}" -maxdepth 1 -type d -name "llvm-mingw-${LLVM_MINGW_VERSION}*" | head -1)"
    [[ -n "${extract_dir}" ]] || error "Could not find extracted llvm-mingw directory"

    # Move to a version-independent path for stable toolchain file references
    mv "${extract_dir}" "${LLVM_MINGW_DIR}"
    [[ -x "${sentinel}" ]] || error "llvm-mingw extraction failed — ${sentinel} not found"

    MINGW_CLANG="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang"
    MINGW_CLANGPP="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang++"
    success "llvm-mingw ${LLVM_MINGW_VERSION} installed: ${LLVM_MINGW_DIR}"

    local clang_ver
    clang_ver=$("${MINGW_CLANG}" --version 2>&1 | head -1 || true)
    info "  ${clang_ver}"
}

# Prepend llvm-mingw/bin to PATH so cmake and tools find the wrappers.
setup_llvm_mingw_path() {
    export PATH="${LLVM_MINGW_DIR}/bin:${PATH}"
    MINGW_CLANG="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang"
    MINGW_CLANGPP="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang++"
}

# =============================================================================
# Build BOLT from source (LLVM subproject)
# BOLT for current LLVM versions is not in the LLVM apt repo for noble
# (only older stable versions ship the full bolt package) — must build from source.
# =============================================================================
build_bolt_from_source() {
    header "Building BOLT ${CLANG_VERSION} from Source"

    local bolt_src="/tmp/llvm-bolt-${CLANG_VERSION}-src"
    local bolt_build="/tmp/llvm-bolt-${CLANG_VERSION}-build"
    local bolt_tag=""
    local install_dir="/usr/local/bin"

    # Probe candidate tags in order: X.0.0 → X.1.0 → X.1.1 → … → X.1.9
    # LLVM releases often skip X.0.0 and go straight to X.1.0 for final releases.
    # Point releases are frequent — LLVM 21 reached 21.1.7. We probe the full
    # range and take the latest confirmed tag so this stays correct automatically.
    local found_tag=""
    for _candidate in \
            "llvmorg-${CLANG_VERSION}.0.0" \
            "llvmorg-${CLANG_VERSION}.1.0" \
            "llvmorg-${CLANG_VERSION}.1.1" \
            "llvmorg-${CLANG_VERSION}.1.2" \
            "llvmorg-${CLANG_VERSION}.1.3" \
            "llvmorg-${CLANG_VERSION}.1.4" \
            "llvmorg-${CLANG_VERSION}.1.5" \
            "llvmorg-${CLANG_VERSION}.1.6" \
            "llvmorg-${CLANG_VERSION}.1.7" \
            "llvmorg-${CLANG_VERSION}.1.8" \
            "llvmorg-${CLANG_VERSION}.1.9"; do
        info "Checking for LLVM tag ${_candidate}..."
        if git ls-remote --tags https://github.com/llvm/llvm-project.git "${_candidate}" \
                2>/dev/null | grep -q "${_candidate}"; then
            found_tag="${_candidate}"
            # Keep probing — we want the latest point-release tag
        fi
    done

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
    # Install the BOLT instrumentation runtime library where llvm-bolt expects it
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
stage_setup() {
    header "Setting Up Build Environment"

    # ── MSYS2/Windows path ────────────────────────────────────────────────────
    if [[ "${_HOST_OS}" == "windows" ]]; then
        info "Detected MSYS2/Windows host (MSYS2_PREFIX=${MSYS2_PREFIX})."
        if ! command -v pacman &>/dev/null; then
            error "pacman not found. Windows setup requires MSYS2 (clang64 environment).\n" \
                  "  Launch the 'MSYS2 CLANG64' terminal from the Start Menu and re-run."
        fi
        info "Installing toolchain and build tools via pacman..."
        pacman -S --needed --noconfirm \
            base-devel git curl wget \
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

        # ── Shared toolchain-dependent artifacts ─────────────────────────────
        mkdir -p "${BUILD_ROOT}"
        create_vcpkg_llvm_triplet
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

    # ── Host LLVM (for PGO merging, BOLT, native ELF build) ─────────────────
    # Cross-compilation uses llvm-mingw; these host tools are for profdata
    # merging, BOLT instrumentation, and the Linux ELF build (build-elf stage).
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

    # BOLT: not in the LLVM apt repo for noble on current versions, build from source
    if command -v "llvm-bolt-${CLANG_VERSION}" &>/dev/null; then
        info "llvm-bolt-${CLANG_VERSION} already installed, skipping."
    else
        build_bolt_from_source
    fi

    # ── llvm-mingw cross-compilation toolchain ───────────────────────────────
    # Clang + LLD + libc++ + compiler-rt for Windows x86_64.
    # Replaces GCC MinGW packages for cross-compilation entirely.
    info "Setting up llvm-mingw cross-compilation toolchain..."
    mkdir -p "${BUILD_ROOT}"
    ensure_llvm_mingw

    # ── Citron build dependencies ─────────────────────────────────────────────
    info "Installing citron build dependencies..."
    _sudo apt-get install -y \
        libboost-all-dev libvulkan-dev libopenal-dev libssl-dev \
        zlib1g-dev libzstd-dev liblz4-dev libfmt-dev \
        nlohmann-json3-dev libsdl2-dev nasm yasm glslang-tools \
        qt6-base-dev qt6-base-private-dev qt6-svg-dev qt6-multimedia-dev qt6-tools-dev qt6-tools-dev-tools

    # ── Toolchain-dependent artifacts ────────────────────────────────────────
    # Idempotent (sentinel-guarded) — fast no-ops on re-run.
    # Running them here lets subsequent stages (csgenerate, use, bolt, propeller)
    # skip redundant calls on a properly set-up machine.
    mkdir -p "${BUILD_ROOT}"
    create_vcpkg_llvm_triplet
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
# PGO profile runtime for Windows
#
# llvm-mingw ships libclang_rt.profile.a for Windows targets.
# This function verifies it exists; if not, builds from LLVM sources as fallback.
# =============================================================================
ensure_profile_runtime_mingw() {
    [[ -x "${MINGW_CLANG}" ]] || error "MINGW_CLANG not set — call ensure_llvm_mingw first"

    local resource_dir
    resource_dir=$("${MINGW_CLANG}" --print-resource-dir 2>/dev/null || true)
    if [[ -z "${resource_dir}" ]]; then
        warn "Could not determine llvm-mingw resource dir — skipping profile runtime check"
        return 0
    fi

    # The clang MinGW driver (lib/Driver/ToolChains/MinGW.cpp) resolves the
    # profile runtime using ToolChain.getTriple().str() which for llvm-mingw is
    # "x86_64-w64-mingw32", NOT "x86_64-w64-windows-gnu".
    # We install to the mingw32 directory. A windows-gnu symlink is also created
    # as a fallback for older clang versions that used that name.
    local target_triple="${MINGW_TRIPLE}"           # x86_64-w64-mingw32
    local runtime_dir="${resource_dir}/lib/${target_triple}"
    local runtime_lib="${runtime_dir}/libclang_rt.profile.a"

    # Also accept the old "windows" layout: libclang_rt.profile-x86_64.a
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

        # LLVM 17+ may internalize some runtime entry points, but the archive
        # still must provide the Windows mmap/flock helpers used by
        # InstrProfilingFile/Util.  The llvm-mingw 20260224 x86_64 archive can
        # expose __llvm_profile_raw_version while still missing those helpers,
        # which causes the exact undefined symbols seen during PE linking.
        echo "${nm_out}" | grep -q '__llvm_profile_raw_version' || return 1

        if [[ "${lib}" == *profile-x86_64.a || "${lib}" == *x86_64-w64-mingw32/libclang_rt.profile.a ]]; then
            local required=(
                ' lprofProfileDumped$'
                ' __llvm_profile_mmap$'
                ' __llvm_profile_flock$'
                ' __llvm_profile_munmap$'
                ' __llvm_profile_madvise$'
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
    # InstrProfilingRuntime was renamed from .c to .cpp in LLVM 16.
    # Build the source list dynamically, probing for the correct extension.
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
        # .cpp is canonical for LLVM 16+
        local stale="${src_dir}/InstrProfilingRuntime.c"
        [[ -f "${stale}" ]] && rm -f "${stale}"   # remove stale fallback if it exists
        runtime_src="InstrProfilingRuntime.cpp"
    else
        # Legacy: probe for correct extension
        for ext in c cpp; do
            local candidate="InstrProfilingRuntime.${ext}"
            # If a non-empty local copy exists, reuse it
            if [[ -s "${src_dir}/${candidate}" ]]; then
                runtime_src="${candidate}"; break
            fi
        done
        [[ -n "${runtime_src}" ]] || runtime_src="InstrProfilingRuntime.c"
    fi
    [[ -n "${runtime_src}" ]] \
        || { warn "Cannot determine InstrProfilingRuntime source for ${llvm_tag}"; return 1; }
    profile_c_srcs+=("${runtime_src}")

    # Remove any zero-byte or partial files from previous failed attempts so
    # the download loop doesn't skip them and silently use corrupt stubs.
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
# vcpkg triplet and chainload toolchain for llvm-mingw
#
# Writes a custom triplet (x64-mingw-llvm-static) so that vcpkg builds all
# dependencies (opus, OpenAL, etc.) with Clang+libc++ instead of GCC.
# Without this, vcpkg's built-in x64-mingw-static triplet uses GCC, producing
# libraries linked against libstdc++ which mixes ABI with the libc++ main build.
#
# The triplet uses two flags together:
#
#   VCPKG_CMAKE_SYSTEM_NAME Windows  — selects Windows portfile variants so
#     boost-thread uses Win32 threads, not POSIX pthreads (unavailable in
#     llvm-mingw).  Using "MinGW" here would cause boost-thread to pull in
#     pthreads and fail.
#
#   VCPKG_TARGET_IS_MINGW TRUE  — tells packages with MinGW-specific build
#     logic (notably openssl, which has a dedicated "mingw64" configure
#     target) to take that path rather than trying to invoke MSVC tooling and
#     emitting "Unknown platform".
# =============================================================================
create_vcpkg_llvm_triplet() {
    local triplet_dir="${BUILD_ROOT}/vcpkg-triplets"
    local triplet_file="${triplet_dir}/x64-mingw-llvm-static.cmake"
    local chainload_file="${BUILD_ROOT}/vcpkg-llvm-mingw-toolchain.cmake"
    
    local CMAKE_CHAINLOAD_FILE="${chainload_file}"
    local rc_compiler="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-windres"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        CMAKE_CHAINLOAD_FILE="$(cygpath -m "${chainload_file}")"
        rc_compiler="windres.exe"
    fi

    mkdir -p "${triplet_dir}"

    info "Writing vcpkg triplet: x64-mingw-llvm-static"
    cat > "${triplet_file}" << TRIPLET_EOF
# vcpkg triplet: Windows x64 static libs built with llvm-mingw Clang+libc++.
# Generated by build-clangtron-windows.sh — do not edit manually.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
# Use "Windows" (not "MinGW") so vcpkg selects Windows portfile variants.
# "MinGW" would tell vcpkg to use POSIX pthreads for boost-thread etc., but
# llvm-mingw uses the Win32 threading model — pthreads are not available.
# The actual compiler is still clang via VCPKG_CHAINLOAD_TOOLCHAIN_FILE.
# VCPKG_CMAKE_SYSTEM_NAME Windows  — selects Windows portfile variants and
#   tells Boost to use Win32 threads (not POSIX pthreads, which llvm-mingw
#   does not ship).
# VCPKG_TARGET_IS_MINGW TRUE  — required alongside Windows so that packages
#   with MinGW-specific build paths (most importantly openssl, which uses a
#   dedicated "mingw64" configure target) don't fall through to the MSVC path
#   and emit "Unknown platform".
set(VCPKG_CMAKE_SYSTEM_NAME Windows)
set(VCPKG_TARGET_IS_MINGW TRUE)
set(VCPKG_BUILD_TYPE release)
# _WIN32_WINNT=0x0A00 (Windows 10): required so that boost::winapi exposes
# WaitOnAddress / WakeByAddressSingle / WakeByAddressAll, which are Windows 8+
# APIs (0x0602).  Without this boost-atomic's wait_ops_windows.hpp fails to
# compile because those names are guarded by the version macro and the
# llvm-mingw ucrt headers default to a version that predates them.
# 0x0A00 matches the minimum target already set in citron's root CMakeLists.
set(VCPKG_CXX_FLAGS "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00")
set(VCPKG_C_FLAGS   "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00")
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CHAINLOAD_FILE}")
TRIPLET_EOF

    cat > "${chainload_file}" << CHAINLOAD_EOF
# vcpkg chainload toolchain for llvm-mingw.
# Generated by build-clangtron-windows.sh — do not edit manually.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(MINGW TRUE)
set(MINGW64 TRUE)
set(CMAKE_C_COMPILER   "${MINGW_CLANG}")
set(CMAKE_CXX_COMPILER "${MINGW_CLANGPP}")
set(CMAKE_RC_COMPILER  "${rc_compiler}")
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_DIR}/${MINGW_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_C_FLAGS_INIT   "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -D__INTRINSIC_DEFINED___cpuidex -D__USE_MINGW_STAT64")
# -include cstdlib: force-include <cstdlib> before every C++ TU.
# libc++ is stricter than libstdc++ about header self-containment; several
# vcpkg dependencies use malloc/free in headers without including <cstdlib>.
# libstdc++ leaked these symbols via its own internal headers; libc++ does not.
set(CMAKE_CXX_FLAGS_INIT "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -D__INTRINSIC_DEFINED___cpuidex -D__USE_MINGW_STAT64 -include cstdlib")
CHAINLOAD_EOF

    success "vcpkg llvm-mingw triplet written"
}

# =============================================================================
# comsupp stub
# MSVC provides _com_util::ConvertStringToBSTR via comsuppw.lib.
# llvm-mingw (and all MinGW toolchains) do not ship it.
# =============================================================================
compile_comsupp_stubs() {
    local stub_src="${BUILD_ROOT}/comsupp_stubs.cpp"
    local stub_obj="${BUILD_ROOT}/comsupp_stubs.o"

    [[ -x "${MINGW_CLANGPP}" ]] || error "MINGW_CLANGPP not set — call ensure_llvm_mingw first"

    if [[ -f "${stub_obj}" ]]; then
        info "comsupp_stubs.o already compiled: ${stub_obj}"
        return 0
    fi

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

    # Search llvm-mingw's sysroot first, then fall back to system MinGW.
    # On MSYS2 clang64, headers are directly in ${LLVM_MINGW_DIR}/include.
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
# Patch: silence MSVC-only #pragma comment(lib, ...)

# =============================================================================
# patch_vfs_stat
#
# vfs_real.cpp has:
#   #ifdef _MSC_VER
#   #define stat _stat64
#   #endif
#
# MinGW defines _WIN32 but NOT _MSC_VER, so the guard is wrong: _wstat64
# expects a struct _stat64* but gets a POSIX struct stat*.  We broaden the
# guard to also cover __MINGW32__.

# =============================================================================
# patch_bfd_linker
#
# New citron (post-3.1.2) sets target_link_options(... -fuse-ld=bfd) for all
# non-MSVC non-Apple builds.  GNU ld.bfd cannot process llvm-mingw COFF objects.
# This wraps -fuse-ld=bfd calls in a CMAKE_CXX_COMPILER_ID STREQUAL "GNU" check.

# =============================================================================
# Patch: suppress CS-IRPGO instrumentation on hot functions with high discard rates
#
# CS-IRPGO inserts per-call-site counters at the LLVM IR level AFTER the
# stage1 PGO optimisation pass.  For functions that are extremely hot and
# heavily inlined by full LTO (particularly inner-loop dispatch functions),
# the post-optimisation IR seen by CS instrumentation differs substantially
# from the IR the use-stage produces from the same stage1 profile — causing
# pervasive hash mismatches and large profile discard counts.
#
# Two functions account for >86M discarded counts in the CSIR use stage:
#
#   Service::HID::NPad::OnUpdate      ~76M discarded  (src/hid_core/)
#   Common::Log::FmtLogMessageImpl    ~10.8M discarded (src/common/)
#
# The correct fix is __attribute__((no_profile_instrument_function)), a
# Clang/GCC attribute (also spelable as [[clang::no_profile_instrument_function]]
# in C++11) that tells the compiler NOT to insert PGO counter code into that
# specific function, while leaving -fprofile-use optimisation fully intact.
# The function still gets profile-guided inlining/branch decisions; it simply
# does not COLLECT new counters during the CS profiling run.
#
# This is applied ONLY during the csgenerate stage, before cmake configure.
# It is idempotent (guarded by a marker comment) and reversible (plain text).
#
# Note: this cannot be done via target_compile_options() because the script
# places -fcs-profile-generate in CMAKE_CXX_FLAGS_RELEASE globally; CMake
# has no mechanism to subtract a flag from that variable per-target.


# =============================================================================
# Patch: make CMakeModules/PGO.cmake defer PGO flags to this script
#
# Fresh upstream clones may not yet contain the CITRON_PGO_FLAGS_MANAGED_BY_SCRIPT
# guard in PGO.cmake. Without that guard, Clang builds can receive both the
# script-managed IR/FE PGO flags and CMake's own frontend PGO flags, producing
# invalid combinations such as:
#   -fprofile-generate=... with -fprofile-instr-generate
# This patch is idempotent and safe to run before any PGO configure stage.

# =============================================================================
# Normalize profraw directories produced by LLVM instrumentation.
# Default IR/FE instrumentation produces:
#   default-<pid>.profraw/ 
#        default_<hash>_0.profraw
# (same file-name inside every directory, collisions prevented by unique directories).
# This helper flattens those directories into standalone .profraw files in the
# same folder so later steps can glob "*.profraw" without walking directories.
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
# Vulkan import library
#
# cmake's FindVulkan needs a libvulkan-1.a import library at configure time.
# We generate it from the bundled Vulkan-Headers submodule — the same headers
# citron is compiled against — so the symbol set is always correct and no
# network access or hardcoded version string is required.
#
# WHY NOT gendef / downloading vulkan-1.dll:
#   gendef extracts symbols from a pre-built Windows DLL. That DLL would be a
#   specific Vulkan Loader release, potentially older than the Vulkan-Headers
#   submodule citron uses, and the download URL breaks whenever a new loader
#   version is released. Parsing the vendored headers directly is strictly
#   more correct: it matches exactly the API surface citron is built against,
#   requires no network, and stays in sync with submodule updates automatically.
#
# WHY NOT --kill-at:
#   On x86_64, the Windows ABI uses cdecl for all functions (including those
#   declared WINAPI/VKAPI_CALL). There is no @N stack-size decoration in
#   64-bit PE exports. --kill-at is only meaningful for 32-bit stdcall.
# =============================================================================
ensure_vulkan_import_lib() {
    local out_dir="${BUILD_ROOT}/vulkan-stub"
    local def_file="${out_dir}/vulkan-1.def"
    local lib_file="${out_dir}/libvulkan-1.a"

    if [[ -f "${lib_file}" ]]; then
        info "Vulkan import lib already exists: ${lib_file}"
        return 0
    fi

    mkdir -p "${out_dir}"
    info "Building vulkan-1 MinGW import library from vendored headers..."

    local vulkan_include="${SOURCE_DIR}/externals/Vulkan-Headers/include/vulkan"
    [[ -d "${vulkan_include}" ]] \
        || error "Vulkan-Headers not found at ${vulkan_include} — check submodules"

    # Parse every header under externals/Vulkan-Headers/include/vulkan/ for
    # exported function declarations.  VKAPI_ATTR/VKAPI_CALL mark all public
    # Vulkan entry points in both vulkan_core.h and the platform-specific
    # extension headers.  Using glob over the entire directory tree ensures
    # platform extension functions (Win32, Xlib, etc.) are included; citron
    # and Qt link some of these directly rather than loading via vkGet*ProcAddr.
    info "  Parsing vendored Vulkan headers for exported symbols..."
    python3 - "${vulkan_include}" "${def_file}" <<'PYEOF_VULKAN'
import sys, re, glob, os

include_dir = sys.argv[1]
def_path    = sys.argv[2]

# Match any function declared with VKAPI_ATTR <ret> VKAPI_CALL <name>(
pattern = re.compile(
    r'VKAPI_ATTR\s+\S+\s+VKAPI_CALL\s+(vk\w+)\s*\('
)

functions = set()
for hdr in sorted(glob.glob(
        os.path.join(include_dir, '**', '*.h'), recursive=True)):
    try:
        text = open(hdr, encoding='utf-8', errors='replace').read()
        functions.update(pattern.findall(text))
    except OSError as e:
        print(f"  Warning: could not read {hdr}: {e}", flush=True)

if not functions:
    print("ERROR: no vk* functions found in headers — check submodule init",
          flush=True)
    sys.exit(1)

with open(def_path, 'w', newline='\n') as f:
    f.write('LIBRARY vulkan-1.dll\n')
    f.write('EXPORTS\n')
    for fn in sorted(functions):
        f.write(f'    {fn}\n')

print(f"  Generated .def with {len(functions)} Vulkan entry points", flush=True)
PYEOF_VULKAN

    # Use llvm-mingw's llvm-dlltool.  It is always present in the llvm-mingw
    # distribution and is the correct tool for the llvm-mingw toolchain.
    # Fall back to system binutils dlltool only if llvm-mingw is not yet
    # extracted (e.g. running ensure_vulkan_import_lib standalone).
    local dlltool="${LLVM_MINGW_DIR}/bin/llvm-dlltool"
    if [[ ! -x "${dlltool}" ]]; then
        warn "llvm-mingw dlltool not found at ${dlltool}, trying system fallback"
        dlltool="x86_64-w64-mingw32-dlltool"
        command -v "${dlltool}" &>/dev/null \
            || error "No dlltool available. Run setup or ensure llvm-mingw is extracted."
    fi

    info "  Running ${dlltool##*/} to generate libvulkan-1.a..."
    # -m i386:x86-64  — target machine (x86_64 PE)
    # No --kill-at    — not needed for x86_64 cdecl exports (see function comment)
    "${dlltool}" \
        -m i386:x86-64 \
        --input-def "${def_file}" \
        --output-lib "${lib_file}" \
        || error "dlltool failed to generate ${lib_file}"

    local sym_count
    sym_count=$(grep -c '^    vk' "${def_file}" 2>/dev/null || echo "?")
    success "Vulkan import lib built: ${lib_file} (${sym_count} entry points)"
}

# =============================================================================
# detect_ffmpeg_version — Read FFmpeg version from the submodule RELEASE file
#   and export FFMPEG_VERSION plus per-library soname variables.
#
# Sets globals:
#   FFMPEG_VERSION        e.g. "7.1.3"
#   FFMPEG_AVCODEC_VER    e.g. "61"
#   FFMPEG_AVFORMAT_VER   e.g. "61"
#   FFMPEG_AVFILTER_VER   e.g. "10"
#   FFMPEG_AVUTIL_VER     e.g. "59"
#   FFMPEG_SWSCALE_VER    e.g. "8"
#   FFMPEG_SWRESAMPLE_VER e.g. "5"
#
# The soname major numbers are NOT mechanically derivable from the package
# version (7.1.3 → 61 is an upstream decision), so they live in a lookup table
# keyed by FFmpeg major version.  Only one new entry is needed per major release.
# =============================================================================
detect_ffmpeg_version() {
    local release_file="${SOURCE_DIR}/externals/ffmpeg/ffmpeg/RELEASE"

    if [[ ! -f "${release_file}" ]]; then
        error "[detect_ffmpeg_version] RELEASE file not found: ${release_file}
  Ensure the ffmpeg submodule is initialised:
    git submodule update --init externals/ffmpeg/ffmpeg"
    fi

    # Strip whitespace/newlines from the RELEASE file content
    FFMPEG_VERSION="$(tr -d '[:space:]' < "${release_file}")"

    if [[ -z "${FFMPEG_VERSION}" ]]; then
        error "[detect_ffmpeg_version] RELEASE file is empty: ${release_file}"
    fi

    # Extract the upstream major version number (first component of X.Y.Z)
    local _major
    _major="$(echo "${FFMPEG_VERSION}" | cut -d. -f1)"

    # Soname lookup table — one entry per FFmpeg major release.
    # Library sonames are set independently by upstream FFmpeg and do not follow
    # the package version number.  Update this table when a new major is released.
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

# Pre-build FFmpeg for Windows (pthread-free, llvm-mingw, shared DLLs).
#
# WHY THIS EXISTS:
#   The citron WIN32 cmake path calls download_bundled_external() which fetches
#   a pre-built FFmpeg archive from yuzu-mirror/ext-windows-bin.  That repo is
#   a frozen mirror of the original yuzu binaries and only carries FFmpeg 6.0
#   (and earlier) archives; newer versions produce an HTTP 404 at cmake configure
#   time, aborting the build.
#
#   Additionally, the pre-built GCC DLLs import libwinpthread-1.dll, whose TLS
#   initialiser races with llvm-mingw's libc++ at game boot and triggers an
#   interval_map.hpp assertion crash.
#
#   This function solves both problems by:
#     1. Running BEFORE cmake configure so the externals/ffmpeg-VERSION/ directory
#        tree already exists and cmake's download_bundled_external() is skipped.
#     2. Building FFmpeg with llvm-mingw using --disable-pthreads --enable-w32threads,
#        eliminating the libwinpthread dependency entirely.
#
# PREREQUISITES:
#   - detect_ffmpeg_version() has been called (sets FFMPEG_VERSION and soname vars)
#   - require_llvm_mingw() has been called (sets LLVM_MINGW_DIR, MINGW_CLANG, etc.)
#
# ARGS:
#   $1  build_dir  — e.g. BUILD_GENERATE, BUILD_USE, BUILD_BOLT, BUILD_PROPELLER
# =============================================================================
rebuild_ffmpeg_pthread_free() {
    local build_dir="$1"

    # detect_ffmpeg_version must have been called by the caller.
    [[ -n "${FFMPEG_VERSION:-}" ]] \
        || error "[ffmpeg-rebuild] FFMPEG_VERSION not set — call detect_ffmpeg_version() first"

    local ffmpeg_ext_dir="${build_dir}/externals/ffmpeg-${FFMPEG_VERSION}"
    local ffmpeg_dst="${ffmpeg_ext_dir}/bin"
    local ffmpeg_hdr="${ffmpeg_ext_dir}/include"
    local ffmpeg_bld="${build_dir}/externals/ffmpeg-${FFMPEG_VERSION}-llvm-bld"
    local ffmpeg_src_dir="${BUILD_ROOT}/ffmpeg-${FFMPEG_VERSION}-src"  # shared across stages
    local sentinel="${ffmpeg_dst}/.llvm_built"

    # Skip if already built for this build_dir
    if [[ -f "${sentinel}" ]]; then
        info "[ffmpeg-rebuild] pthread-free DLLs already in place — skipping"
        return 0
    fi

    # ── Locate or download FFmpeg source matching FFMPEG_VERSION ─────────────
    #
    # _ffmpeg_abi_matches: verify that a source tree's library sonames match
    # the version declared by detect_ffmpeg_version().  Uses the env vars set
    # by detect_ffmpeg_version() so no version strings are hardcoded here.
    _ffmpeg_abi_matches() {
        local dir="$1"
        [[ -f "${dir}/configure" ]] || return 1
        [[ -f "${dir}/libavcodec/version_major.h" ]]    || return 1
        [[ -f "${dir}/libavformat/version_major.h" ]]   || return 1
        [[ -f "${dir}/libswscale/version_major.h" ]]    || return 1
        [[ -f "${dir}/libswresample/version_major.h" ]] || return 1

        # Use awk instead of grep -oP lookbehind — variable-length lookbehinds
        # are not supported by the grep shipped in MSYS2/clang64 (and many other
        # non-GNU environments), causing silent empty matches and false negatives.
        local _codec _fmt _scale _resample
        _codec=$(awk '/^#define LIBAVCODEC_VERSION_MAJOR/{print $NF; exit}' \
                     "${dir}/libavcodec/version_major.h")
        _fmt=$(awk '/^#define LIBAVFORMAT_VERSION_MAJOR/{print $NF; exit}' \
                   "${dir}/libavformat/version_major.h")
        _scale=$(awk '/^#define LIBSWSCALE_VERSION_MAJOR/{print $NF; exit}' \
                     "${dir}/libswscale/version_major.h")
        _resample=$(awk '/^#define LIBSWRESAMPLE_VERSION_MAJOR/{print $NF; exit}' \
                        "${dir}/libswresample/version_major.h")

        [[ "${_codec}"    == "${FFMPEG_AVCODEC_VER}" ]] &&
        [[ "${_fmt}"      == "${FFMPEG_AVFORMAT_VER}" ]] &&
        [[ "${_scale}"    == "${FFMPEG_SWSCALE_VER}" ]] &&
        [[ "${_resample}" == "${FFMPEG_SWRESAMPLE_VER}" ]]
    }

    local ffmpeg_src=""

    # Priority 1: previously downloaded/extracted source tree
    if [[ -f "${ffmpeg_src_dir}/configure" ]]; then
        if _ffmpeg_abi_matches "${ffmpeg_src_dir}"; then
            ffmpeg_src="${ffmpeg_src_dir}"
            info "[ffmpeg-rebuild] Using cached FFmpeg ${FFMPEG_VERSION} source"
        else
            warn "[ffmpeg-rebuild] Cached source ABI does not match FFmpeg ${FFMPEG_VERSION} — ignoring"
        fi
    fi

    # Priority 2: vendored submodule (only if sonames match)
    local submodule="${SOURCE_DIR}/externals/ffmpeg/ffmpeg"
    if [[ -z "${ffmpeg_src}" && -f "${submodule}/configure" ]]; then
        if _ffmpeg_abi_matches "${submodule}"; then
            ffmpeg_src="${submodule}"
            info "[ffmpeg-rebuild] Using vendored FFmpeg submodule (ABI matches ${FFMPEG_VERSION})"
        else
            warn "[ffmpeg-rebuild] Vendored submodule ABI does not match FFmpeg ${FFMPEG_VERSION} — ignoring"
        fi
    fi

    # Priority 3: download tarball from ffmpeg.org
    if [[ -z "${ffmpeg_src}" ]]; then
        # Git snapshot versions (e.g. "8.0.git") have no release tarball on
        # ffmpeg.org.  If the submodule ABI check failed for a git version it
        # means the soname table in detect_ffmpeg_version() is out of date —
        # not that we should construct a bogus URL.
        if [[ "${FFMPEG_VERSION}" == *git* || "${FFMPEG_VERSION}" == *dev* ]]; then
            error "[ffmpeg-rebuild] FFmpeg version '${FFMPEG_VERSION}' is a git snapshot with no release tarball.
  The vendored submodule ABI check failed — the soname table in
  detect_ffmpeg_version() may be wrong for this development version.
  Check libavcodec/version_major.h in the submodule and update the
  soname table, or check out a tagged FFmpeg release."
        fi
        local tarball="${BUILD_ROOT}/ffmpeg-${FFMPEG_VERSION}.tar.bz2"
        local ffmpeg_url="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2"
        info "[ffmpeg-rebuild] Downloading FFmpeg ${FFMPEG_VERSION} source from ffmpeg.org..."
        mkdir -p "${BUILD_ROOT}"
        if ! wget -q --show-progress -O "${tarball}" "${ffmpeg_url}"; then
            warn "[ffmpeg-rebuild] Download failed — FFmpeg DLLs will NOT be built (cmake will fail)"
            return 0
        fi
        info "[ffmpeg-rebuild] Extracting FFmpeg ${FFMPEG_VERSION}..."
        mkdir -p "${ffmpeg_src_dir}"
        tar -xjf "${tarball}" -C "${ffmpeg_src_dir}" --strip-components=1 || {
            warn "[ffmpeg-rebuild] Extraction failed"
            return 0
        }
        ffmpeg_src="${ffmpeg_src_dir}"
        success "[ffmpeg-rebuild] FFmpeg ${FFMPEG_VERSION} source ready"
    fi

    info "[ffmpeg-rebuild] Building pthread-free FFmpeg ${FFMPEG_VERSION} DLLs with llvm-mingw..."
    mkdir -p "${ffmpeg_bld}" "${ffmpeg_dst}" "${ffmpeg_hdr}"

    local cross_prefix="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-"
    local cc="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang"
    local ar="${LLVM_MINGW_DIR}/bin/llvm-ar"
    local nm_bin="${LLVM_MINGW_DIR}/bin/llvm-nm"
    local strip_tool="${LLVM_MINGW_DIR}/bin/llvm-strip"
    local ranlib="${LLVM_MINGW_DIR}/bin/llvm-ranlib"
    local windres="${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-windres"
    local dlltool_bin="${LLVM_MINGW_DIR}/bin/llvm-dlltool"
    [[ -x "${dlltool_bin}" ]] || dlltool_bin="$(command -v llvm-dlltool 2>/dev/null || true)"

    # On MSYS2/Windows the host and target are the same machine (both
    # x86_64-w64-mingw32), so --enable-cross-compile must NOT be used.
    # With cross-compile enabled FFmpeg looks for a separate host compiler
    # (defaults to bare `cc`) for build-time utilities; on MSYS2 that
    # resolves to nothing or MSVC, producing "Host compiler lacks C11 support".
    # On a real Linux→Windows cross, --enable-cross-compile is still required.
    local _ffmpeg_cross_flags=()
    if [[ "${_HOST_OS}" == "linux" || "${_HOST_OS}" == "macos" ]]; then
        # True cross-compilation: Linux/macOS host → Windows target
        _ffmpeg_cross_flags=(
            --enable-cross-compile
            "--cross-prefix=${cross_prefix}"
        )
    else
        # MSYS2/Windows native build: host == target, no cross-compile mode.
        # Set --host-cc explicitly so FFmpeg's build-tool probes don't fall
        # back to bare `cc` (which may be MSVC or absent on MSYS2).
        local _host_clang
        _host_clang="$(command -v clang 2>/dev/null \
                       || echo "${MSYS2_PREFIX}/bin/clang")"
        _ffmpeg_cross_flags=("--host-cc=${_host_clang}")
    fi

    info "[ffmpeg-rebuild] Configuring FFmpeg..."
    (
        cd "${ffmpeg_bld}"
        bash "${ffmpeg_src}/configure" \
            --arch=x86_64 \
            --target-os=mingw32 \
            "${_ffmpeg_cross_flags[@]}" \
            "--cc=${cc}" \
            "--ar=${ar}" \
            "--nm=${nm_bin}" \
            "--strip=${strip_tool}" \
            "--ranlib=${ranlib}" \
            "--windres=${windres}" \
            --disable-pthreads \
            --enable-w32threads \
            --enable-shared \
            --disable-static \
            --disable-doc \
            --disable-programs \
            --disable-avdevice \
            --disable-network \
            --disable-everything \
            --enable-decoder=h264,vp8,vp9,aac,mp3,opus,flac \
            --enable-demuxer=mp4,matroska,ogg \
            --enable-filter=yadif,scale,aresample \
            --enable-protocol=file \
            2>&1 | tail -8
    ) || {
        warn "[ffmpeg-rebuild] FFmpeg configure failed — cmake will fail at link time"
        return 0
    }

    info "[ffmpeg-rebuild] Compiling (this takes a few minutes)..."
    make -C "${ffmpeg_bld}" -j"${JOBS}" 2>&1 | tail -5 || {
        warn "[ffmpeg-rebuild] FFmpeg make failed"
        return 0
    }

    # ── Install DLLs and import libraries ────────────────────────────────────
    local installed=0
    local bin_dir="${build_dir}/bin"

    _ffmpeg_install_dll() {
        local dst_dir="$1"
        mkdir -p "${dst_dir}"
        for lib in avutil avcodec avfilter swscale swresample avformat; do
            # DLL (named with soname, e.g. avcodec-${FFMPEG_AVCODEC_VER}.dll)
            local dll
            dll="$(find "${ffmpeg_bld}" -maxdepth 2 -name "${lib}-*.dll" 2>/dev/null | head -1)"
            [[ -z "${dll}" ]] && continue
            local dll_name; dll_name="$(basename "${dll}")"
            cp -f "${dll}" "${dst_dir}/${dll_name}"
            info "  [ffmpeg-rebuild] ${dst_dir##*/}/: ${dll_name}"

            if [[ "${dst_dir}" == "${ffmpeg_dst}" ]]; then
                (( installed++ )) || true

                # Import library for linking.  FFmpeg MinGW cross-compile generates
                # GNU-format lib${lib}.dll.a files; lld accepts these directly.
                # We also probe for MSVC-format .lib in case FFmpeg generated one.
                local lib_file=""
                lib_file="$(find "${ffmpeg_bld}" -maxdepth 2 -name "${lib}.lib" 2>/dev/null | head -1)"
                if [[ -z "${lib_file}" ]]; then
                    lib_file="$(find "${ffmpeg_bld}" -maxdepth 2 -name "lib${lib}.dll.a" 2>/dev/null | head -1)"
                fi
                if [[ -n "${lib_file}" ]]; then
                    cp -f "${lib_file}" "${dst_dir}/${lib}.lib"
                elif [[ -n "${dlltool_bin}" && -x "${dlltool_bin}" ]]; then
                    # Generate .lib from .def via llvm-dlltool as last resort
                    local deffile
                    deffile="$(find "${ffmpeg_bld}" -maxdepth 2 -name "${lib}.def" 2>/dev/null | head -1)"
                    if [[ -n "${deffile}" ]]; then
                        "${dlltool_bin}" -m i386:x86-64 \
                            --input-def "${deffile}" \
                            --output-lib "${dst_dir}/${lib}.lib" 2>/dev/null \
                            && info "  [ffmpeg-rebuild] ${lib}.lib (generated from ${lib}.def)"
                    fi
                fi
            fi
        done
        # Remove any GCC runtime DLLs that may have landed here from a previous run
        for gcc_dll in libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll; do
            if [[ -f "${dst_dir}/${gcc_dll}" ]]; then
                rm -f "${dst_dir}/${gcc_dll}"
                info "  [ffmpeg-rebuild] removed: ${dst_dir##*/}/${gcc_dll}"
            fi
        done
    }

    info "[ffmpeg-rebuild] Installing to externals/ffmpeg-${FFMPEG_VERSION}/bin/..."
    _ffmpeg_install_dll "${ffmpeg_dst}"

    # ── Install public headers (needed by cmake at configure time) ────────────
    info "[ffmpeg-rebuild] Installing headers to externals/ffmpeg-${FFMPEG_VERSION}/include/..."
    for lib in libavcodec libavfilter libavformat libavutil libswresample libswscale; do
        local inc_dst="${ffmpeg_hdr}/${lib}"
        mkdir -p "${inc_dst}"
        # Source-tree public headers
        if [[ -d "${ffmpeg_src}/${lib}" ]]; then
            find "${ffmpeg_src}/${lib}" -maxdepth 1 -name "*.h" \
                -exec cp -f {} "${inc_dst}/" \; 2>/dev/null || true
        fi
        # Build-generated headers (version.h, config.h, etc.)
        if [[ -d "${ffmpeg_bld}/${lib}" ]]; then
            find "${ffmpeg_bld}/${lib}" -maxdepth 1 -name "*.h" \
                -exec cp -f {} "${inc_dst}/" \; 2>/dev/null || true
        fi
    done

    # ── Also seed build_dir/bin/ so the final output dir is populated ─────────
    if [[ -d "${bin_dir}" ]]; then
        info "[ffmpeg-rebuild] Seeding ${bin_dir}/..."
        _ffmpeg_install_dll "${bin_dir}"
    fi

    if [[ "${installed}" -eq 0 ]]; then
        warn "[ffmpeg-rebuild] No DLLs were installed — FFmpeg configure/make may have failed"
        return 0
    fi

    # ── Hard verification: none of our FFmpeg DLLs may import libwinpthread ───
    local readobj="${LLVM_MINGW_DIR}/bin/llvm-readobj"
    [[ -x "${readobj}" ]] || \
        readobj="$(command -v "llvm-readobj-${CLANG_VERSION}" 2>/dev/null \
                   || command -v llvm-readobj 2>/dev/null || true)"

    if [[ -n "${readobj}" ]]; then
        while IFS= read -r -d '' dll; do
            if "${readobj}" --coff-imports "${dll}" 2>/dev/null \
                    | grep -qi 'libwinpthread'; then
                error "[ffmpeg-rebuild] FATAL: ${dll##*/} still imports libwinpthread-1.dll " \
                      "after rebuild. Check configure output for '--enable-pthreads'."
            fi
        done < <(find "${ffmpeg_dst}" -maxdepth 1 -name "*.dll" -print0 2>/dev/null)
        success "[ffmpeg-rebuild] Verified: all FFmpeg DLLs are pthread-free"
    else
        warn "[ffmpeg-rebuild] llvm-readobj not found — skipping pthread verification"
    fi

    touch "${sentinel}"
    success "[ffmpeg-rebuild] pthread-free FFmpeg ${FFMPEG_VERSION} DLLs installed (${installed} libs)"
}


# =============================================================================
# Runtime DLL deployment
# =============================================================================
deploy_runtime_dlls() {
    local bin_dir="$1"
    # This step is now redundant as CMake (CopyMinGWDeps.cmake) handles 
    # synchronized, recursive DLL and plugin deployment during the build.
    success "All runtime DLLs deployed to \${bin_dir} (synchronized via CMake)"
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
# CMake toolchain file for llvm-mingw cross-compilation
#
# Uses llvm-mingw wrapper scripts which automatically set --target, --sysroot,
# -stdlib=libc++, -rtlib=compiler-rt, and -fuse-ld=lld. No extra cross flags
# are needed beyond pointing CMAKE_C/CXX_COMPILER at the wrappers.
# =============================================================================
write_toolchain_file() {
    local path="$1"
    mkdir -p "$(dirname "$path")"

    local CMAKE_BUILD_ROOT="${BUILD_ROOT}"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        CMAKE_BUILD_ROOT="$(cygpath -m "${BUILD_ROOT}")"

        # MSYS2/Windows: native compilation — CMAKE_SYSTEM_NAME is auto-detected
        # as Windows; no cross-compile sysroot is needed.  The MSYS2 clang64
        # toolchain targets Windows natively with the same libc++/compiler-rt ABI
        # as llvm-mingw.  On a case-insensitive Windows filesystem the
        # mingw-case-fixups include dir is unnecessary.
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
set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_BUILD_ROOT}/comsupp_stubs.o -loleaut32")
set(CMAKE_AUTORCC_OPTIONS "--compress-algo;zlib")
MSYS2_TC_EOF
        return
    fi

    cat > "$path" <<EOF
# CMake toolchain: cross-compile for Windows x86_64 with llvm-mingw
# (Clang + LLD + libc++ + compiler-rt — no GCC runtime dependency)
# Generated by build-clangtron-windows.sh — do not edit manually

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# llvm-mingw wrapper scripts handle: --target, --sysroot, -stdlib=libc++,
# -rtlib=compiler-rt, -fuse-ld=lld. No additional cross flags needed.
set(CMAKE_C_COMPILER   "${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-clang++")
set(CMAKE_RC_COMPILER  "${LLVM_MINGW_DIR}/bin/${MINGW_TRIPLE}-windres")

# Sysroot for cmake find_library / find_file / find_path
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_DIR}/${MINGW_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# -D__INTRINSIC_DEFINED___cpuidex: prevents MinGW intrin-impl.h from defining
# __cpuidex with external linkage. Clang's cpuid.h has a static __inline
# definition; the guard macro ensures it is the sole definer, eliminating
# duplicate-symbol link errors from SDL2 and other libraries.
# Applied to both C and C++ (SDL2 is compiled as C and triggers the duplicate).
set(CMAKE_C_FLAGS_INIT   "-D__INTRINSIC_DEFINED___cpuidex -D__USE_MINGW_STAT64 -isystem ${CMAKE_BUILD_ROOT}/mingw-case-fixups -Wno-unknown-pragmas")
set(CMAKE_CXX_FLAGS_INIT "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -D__INTRINSIC_DEFINED___cpuidex -D__USE_MINGW_STAT64 -U__GLIBCXX__ -isystem ${CMAKE_BUILD_ROOT}/mingw-case-fixups -Wno-unknown-pragmas")

# --allow-multiple-definition: belt-and-suspenders for any residual __cpuidex
# duplicates inside libSDL2.a (SDL_dynapi.c include-all mechanism).
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-Wl,--allow-multiple-definition")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,--allow-multiple-definition")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-Wl,--allow-multiple-definition")

# Standard libraries (appended after all user archives by CMake):
#   comsupp_stubs.o: _com_util::ConvertStringToBSTR (MSVC-specific, no MinGW equivalent)
#   -loleaut32: COM/OLE Automation symbols (SysAllocString etc.) for WMI code
#   libc++ and libunwind are linked automatically by the llvm-mingw wrappers.
set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_BUILD_ROOT}/comsupp_stubs.o -loleaut32")

# Force Qt rcc to use zlib resource compression instead of zstd.
# aqt's llvm_mingw Qt6Core lacks zstd resource support; default zstd calls
# qResourceFeatureZstd() which is missing from Qt6Core.a.
set(CMAKE_AUTORCC_OPTIONS "--compress-algo;zlib")
EOF
}

# =============================================================================
# Common CMake arguments for cross-compilation to Windows
# =============================================================================
common_cmake_args() {
    local lto_flag; lto_flag="$(lto_cmake_flag)"
    local toolchain_file="${BUILD_ROOT}/mingw-clang-toolchain.cmake"
    write_toolchain_file "$toolchain_file"

    local host_triplet="x64-linux"
    
    local CMAKE_BUILD_ROOT="${BUILD_ROOT}"
    local CMAKE_SOURCE_DIR="${SOURCE_DIR}"
    local CMAKE_SPIRV_HEADERS_INSTALL="${SPIRV_HEADERS_INSTALL}"
    local CMAKE_VULKAN_HEADERS_INSTALL="${VULKAN_HEADERS_INSTALL}"
    local CMAKE_TOOLCHAIN_FILE_PATH="${toolchain_file}"

    if [[ "${_HOST_OS}" == "windows" ]]; then
        host_triplet="x64-windows"
        CMAKE_BUILD_ROOT="$(cygpath -m "${BUILD_ROOT}")"
        CMAKE_SOURCE_DIR="$(cygpath -m "${SOURCE_DIR}")"
        CMAKE_SPIRV_HEADERS_INSTALL="$(cygpath -m "${SPIRV_HEADERS_INSTALL}")"
        CMAKE_VULKAN_HEADERS_INSTALL="$(cygpath -m "${VULKAN_HEADERS_INSTALL}")"
        CMAKE_TOOLCHAIN_FILE_PATH="$(cygpath -m "${toolchain_file}")"
    fi

    echo \
        "-G" "Ninja" \
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" \
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE_PATH}" \
        "-DCMAKE_DISABLE_FIND_PACKAGE_LLVM=ON" \
        "-DCITRON_ENABLE_LTO=${lto_flag}" \
        "-DCITRON_TESTS=OFF" \
        "-DCITRON_USE_BUNDLED_FFMPEG=ON" \
        "-DCITRON_USE_EXTERNAL_SDL2=ON" \
        "-DCITRON_USE_EXTERNAL_VULKAN_HEADERS=ON" \
        "-DCITRON_USE_EXTERNAL_VULKAN_UTILITY_LIBRARIES=ON" \
        "-DCITRON_USE_BUNDLED_VCPKG=ON" \
        "-DSPIRV-Headers_DIR=${CMAKE_SPIRV_HEADERS_INSTALL}/share/cmake/SPIRV-Headers" \
        "-DVulkanHeaders_DIR=${CMAKE_VULKAN_HEADERS_INSTALL}/share/cmake/VulkanHeaders" \
        "-DCMAKE_PREFIX_PATH=${CMAKE_VULKAN_HEADERS_INSTALL};${CMAKE_SPIRV_HEADERS_INSTALL}" \
        "-DVulkanMemoryAllocator_FOUND=TRUE" \
        "-Ddynarmic_FOUND=TRUE" \
        "-Dxbyak_FOUND=TRUE" \
        "-Dcubeb_FOUND=TRUE" \
        "-DVCPKG_TARGET_TRIPLET=x64-mingw-llvm-static" \
        "-DVCPKG_HOST_TRIPLET=${host_triplet}" \
        "-DVCPKG_OVERLAY_TRIPLETS=${CMAKE_BUILD_ROOT}/vcpkg-triplets" \
        "-DENABLE_LIBUSB=OFF" \
        "-DVulkan_LIBRARY=${CMAKE_BUILD_ROOT}/vulkan-stub/libvulkan-1.a" \
        "-DVulkan_INCLUDE_DIR=${CMAKE_SOURCE_DIR}/externals/Vulkan-Headers/include" \
        "-DVulkan_INCLUDE_DIRS=${CMAKE_SOURCE_DIR}/externals/Vulkan-Headers/include" \
        ${GLSLC_PATH:+"-DVulkan_GLSLC_EXECUTABLE=${GLSLC_PATH}"} \
        ${GLSLC_PATH:+"-DVulkan_GLSLANG_VALIDATOR_EXECUTABLE=${GLSLC_PATH}"} \
        "-DCITRON_USE_PRECOMPILED_HEADERS=OFF" \
        "-Wno-dev"
    [[ -n "${CITRON_BUILD_TYPE:-}" ]] && echo "-DCITRON_BUILD_TYPE=${CITRON_BUILD_TYPE}"
    [[ "${UNITY_BUILD}" == "ON" ]] && echo "-DENABLE_UNITY_BUILD=ON"
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

    # PGO instrumentation flag: IR PGO (-fprofile-generate) or Frontend PGO
    # (-fprofile-instr-generate). Both write default-%p.profraw relative to
    # the binary's working directory on clean exit. %p = PID.
    # IR PGO inserts counters at the LLVM IR level after early optimizations,
    # so the profile accurately reflects what the optimizer will see — but
    # IR PGO inserts counters at the LLVM IR level after early optimizations.
    # CRITICAL: generate and use must use the same -O level. IR PGO hashes
    # are computed from the post-optimization IR — if generate uses -O2 and
    # use uses -O3, the additional O3 passes (loop unrolling, vectorisation,
    # extra inlining) restructure basic blocks and every affected function's
    # hash mismatches, discarding its profile data entirely. With full LTO
    # this affects nearly the entire program. Use -O3 here to match use stage.
    local pgo_gen_flag
    if [[ "${PGO_MODE}" == "ir" ]]; then
        pgo_gen_flag="-fprofile-generate=default-%p.profraw"
    else
        pgo_gen_flag="-fprofile-instr-generate=default-%p.profraw"
    fi
    local debug_flag=""
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
    local c_flags="-O3 -DNDEBUG ${debug_flag} ${pgo_gen_flag}${lto_generate_flag:+ ${lto_generate_flag}}"
    local cxx_flags="${c_flags}"

    # Force-keep the profile runtime symbols so lld does not dead-strip them.
    # -u,__llvm_profile_write_file: pulls InstrProfilingFile.o (write logic)
    # -u,__llvm_profile_runtime: pulls InstrProfilingRuntime.o whose constructor
#   initializes __llvm_profile_write_file_internal.
    local extra_link_flags="-Wl,-u,__llvm_profile_write_file,-u,__llvm_profile_runtime"

    # Pre-install SPIRV-Headers
    if [[ ! -f "${SPIRV_HEADERS_INSTALL}/share/cmake/SPIRV-Headers/SPIRV-HeadersConfig.cmake" ]]; then
        info "Pre-installing SPIRV-Headers from submodule..."
        cmake -S "${SOURCE_DIR}/externals/SPIRV-Headers" \
              -B "${BUILD_ROOT}/spirv-headers-build" \
              -DCMAKE_INSTALL_PREFIX="${SPIRV_HEADERS_INSTALL}" \
              -DCMAKE_BUILD_TYPE=Release -Wno-dev
        cmake --install "${BUILD_ROOT}/spirv-headers-build"
        success "SPIRV-Headers installed"
    fi

    # Pre-install bundled Vulkan-Headers
    if [[ ! -f "${VULKAN_HEADERS_INSTALL}/share/cmake/VulkanHeaders/VulkanHeadersConfig.cmake" ]]; then
        info "Pre-installing Vulkan-Headers from submodule..."
        cmake -S "${SOURCE_DIR}/externals/Vulkan-Headers" \
              -B "${BUILD_ROOT}/vulkan-headers-build" \
              -DCMAKE_INSTALL_PREFIX="${VULKAN_HEADERS_INSTALL}" \
              -DCMAKE_BUILD_TYPE=Release -Wno-dev
        cmake --install "${BUILD_ROOT}/vulkan-headers-build"
        success "Vulkan-Headers installed"
    fi

    # Qt via aqt
    local qt_install_dir="${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64"
    local qt_host_dir="${BUILD_GENERATE}/externals/qt-host/6.9.3/gcc_64"
    local qt6_cmake_dir="${qt_install_dir}/lib/cmake/Qt6"

    if ! command -v aqt &>/dev/null && ! "${HOME}/.local/bin/aqt" --version &>/dev/null; then
        python3 -m pip install aqtinstall --break-system-packages --quiet
    fi
    local aqt_bin
    aqt_bin="$(command -v aqt 2>/dev/null || echo "${HOME}/.local/bin/aqt")"

    if [[ ! -f "${qt_install_dir}/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
        info "Downloading Qt 6.9.3 Windows/MinGW target via aqt..."
        mkdir -p "${BUILD_GENERATE}/externals/qt"
        "${aqt_bin}" install-qt windows desktop 6.9.3 win64_llvm_mingw \
            --outputdir "${BUILD_GENERATE}/externals/qt"
    fi

    # Install optional modules: multimedia, image formats, TLS (tls is in qtbase but
    # imageformats ships as a separate module and must be fetched explicitly).
    local _qt_win_needs_modules=0
    [[ ! -f "${qt_install_dir}/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake" ]] && _qt_win_needs_modules=1
    # qtimageformats is required for SVG/PNG/JPEG plugin deployment; it does not ship
    # with the base llvm_mingw package and must be installed as a separate module.
    [[ ! -d "${qt_install_dir}/plugins/imageformats" ]] && _qt_win_needs_modules=1
    if [[ "${_qt_win_needs_modules}" -eq 1 ]]; then
        info "Installing Qt6 modules (multimedia + imageformats)..."
        local qt_mm_ok=0
        for _attempt in 1 2 3; do
            "${aqt_bin}" install-qt windows desktop 6.9.3 win64_llvm_mingw \
                --outputdir "${BUILD_GENERATE}/externals/qt" \
                --modules qtmultimedia qtimageformats && { qt_mm_ok=1; break; }
            warn "Qt6 module install attempt ${_attempt} failed — retrying..."; sleep 5
        done
        [[ "$qt_mm_ok" -eq 0 ]] && warn "Qt6 modules install failed after 3 attempts"
    fi

    # Qt host tools (Linux gcc_64) are only needed on Linux for cross-compilation.
    # On Windows we are doing a native build — the target Qt IS the host Qt, and
    # CMake must NOT receive QT_HOST_PATH (it would switch to cross-compile mode).
    # The Linux Qt package contains Unix symlinks that Windows cannot create without
    # Developer Mode enabled (OSError WinError 1314), so skip this entirely here.
    if [[ "${_HOST_OS}" != "windows" ]]; then
        if [[ ! -f "${qt_host_dir}/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
            info "Downloading Qt 6.9.3 Linux host tools via aqt..."
            mkdir -p "${BUILD_GENERATE}/externals/qt-host"
            "${aqt_bin}" install-qt linux desktop 6.9.3 linux_gcc_64 \
                --outputdir "${BUILD_GENERATE}/externals/qt-host"
        fi
        # Ensure qtsvg + qtmultimedia are present in the host Qt — needed by the native ELF (BOLT) build
        # aqt often cannot install multimedia for linux desktop; fall back to system Qt if needed.
        local _qt_host_needs_modules=0
        [[ ! -f "${qt_host_dir}/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake" ]] && _qt_host_needs_modules=1
        [[ ! -f "${qt_host_dir}/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake" ]] && _qt_host_needs_modules=1
        if [[ "${_qt_host_needs_modules}" -eq 1 ]]; then
            info "Attempting aqt install of Qt6Svg + Qt6Multimedia for Linux host Qt..."
            "${aqt_bin}" install-qt linux desktop 6.9.3 linux_gcc_64 \
                --outputdir "${BUILD_GENERATE}/externals/qt-host" \
                --modules qtsvg qtmultimedia 2>/dev/null \
                || warn "aqt Qt6 module install failed — will try system Qt for ELF build"
        fi
    else
        info "Windows native build: skipping Linux host Qt download (not needed for native builds)."
        qt_host_dir=""  # Ensure QT_HOST_PATH is NOT passed to CMake on Windows
    fi

    info "Qt6 cmake dir: ${qt6_cmake_dir}"

    create_vcpkg_llvm_triplet
    ensure_profile_runtime_mingw
    compile_comsupp_stubs
    rm -f "${BUILD_ROOT}/vulkan-stub/libvulkan-1.a" 2>/dev/null || true
    ensure_vulkan_import_lib
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

    # Detect FFmpeg version from the submodule and pre-build pthread-free DLLs
    # BEFORE cmake configure so download_bundled_external() finds them already
    # present and skips the network download (yuzu-mirror lacks FFmpeg >= 7.x).
    detect_ffmpeg_version
    rebuild_ffmpeg_pthread_free "${BUILD_GENERATE}"

    info "Configuring CMake (instrumented build)..."
    cd "${BUILD_GENERATE}"
    rm -f CMakeCache.txt; rm -rf CMakeFiles
    [[ -d "src/citron/citron_autogen" ]] && rm -rf src/citron/citron_autogen

    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
    # shellcheck disable=SC2046
    cmake "${SOURCE_DIR}" \
        $(common_cmake_args) \
        ${qt6_cmake_dir:+"-DQt6_DIR=${qt6_cmake_dir}"} \
        ${qt_host_dir:+"-DQT_HOST_PATH=${qt_host_dir}"} \
        "-DCITRON_ENABLE_PGO_GENERATE=ON" \
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
        "-DCITRON_ENABLE_LTO=${generate_lto_cmake}" \
        "-DCMAKE_C_FLAGS_${bt_upper}=${c_flags}" \
        "-DCMAKE_CXX_FLAGS_${bt_upper}=${cxx_flags}" \
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=${c_flags} ${PROFILE_RUNTIME_LIB:+${PROFILE_RUNTIME_LIB}} ${extra_link_flags}" \
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}" \
        2>&1 | grep -v '^-- '; cmake_exit=${PIPESTATUS[0]}
    [[ ${cmake_exit} -eq 0 ]] || error "CMake configure failed"
    info "Building instrumented citron (${BUILD_TYPE})..."
    cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}"

    success "Instrumented build complete: ${BUILD_GENERATE}/bin/citron.exe"

    # ── Verify instrumentation was actually linked into the binary ────────────
    # A PGO-instrumented binary must export __llvm_profile_raw_version (marks
    # the counters segment) and __llvm_profile_runtime (the atexit hook that
    # flushes profile data on clean exit).  If either is absent the binary will
    # run fine but produce no .profraw — exactly the silent failure mode we
    # want to catch before the user spends 30 minutes profiling a bad build.
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
            warn "    3. citron cmake config overrode CMAKE_EXE_LINKER_FLAGS_RELEASE"
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

    # Write sentinel recording this generate config.
    # stage_use and stage_csgenerate verify LTO + PGO match to catch
    # profile mismatches before a long build wastes time.
    printf "LTO=%s\nPGO=%s\n" "${LTO_MODE}" "${PGO_MODE}" \
        > "${BUILD_ROOT}/.citron-gen-config"

    print_profiling_instructions "${BUILD_GENERATE}/bin/citron.exe"
}

# =============================================================================
# Stage 1b: csgenerate — Context-Sensitive IR PGO instrumented build
#
# CS-IRPGO layers a context-sensitive instrumentation pass on top of a binary
# that has already been optimized with stage1 IR PGO profiles. The resulting
# binary collects per-call-site counter data rather than per-function-definition
# data, letting the compiler make inlining and branch decisions with full context
# for each inlined copy of a function.
#
# Requirements:
#   - --pgo-type ir must be set (CS-IRPGO requires IR PGO; not available for FE)
#   - --lto and --pgo-type must match the prior generate run (enforced by sentinel)
#   - default.profdata must exist in pgo-profiles/ (produced by 'use' or by
#     merging the stage1 profraw from generate). merged.profdata is NOT accepted
#     as a substitute — see CRITICAL INVARIANT in the script header.
#
# Compile flags for the CS binary:
#   -fprofile-use=default.profdata   Apply stage1 IR profile (optimizes this build)
#   -fcs-profile-generate=...        Layer CS counters on top of the optimized IR
#   (Both flags are passed together to C, C++, and linker command lines.)
#
# The CS-instrumented binary writes cs-default-<pid>.profraw next to itself on
# exit (same mechanism as stage1). The user copies these to pgo-profiles/cs/,
# then re-runs 'use' — which auto-detects pgo-profiles/cs/ and merges both
# profiles into merged.profdata (stage1 + CS combined) before building.
#
# Profdata merging (performed by stage_use, not here):
#   Step 1: llvm-profdata merge --sparse cs-default-*.profraw → cs-only.profdata
#   Step 2: llvm-profdata merge --sparse default.profdata cs-only.profdata
#              → merged.profdata
#   Step 3: use builds with -fprofile-use=merged.profdata (compile + linker)
#
# Profile runtime:
#   CS-IRPGO uses the same LLVM InstrProfiling runtime as standard IR/FE PGO.
#   ensure_profile_runtime_mingw() and extra_link_flags apply identically here.
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
        _gen_lto=$(grep -oP "(?<=LTO=)\S+" "${_gen_cfg}" || true)
        _gen_pgo=$(grep -oP "(?<=PGO=)\S+" "${_gen_cfg}" || true)
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

    # ── Locate stage1 profdata (MUST be default.profdata, never merged.profdata) ─
    #
    # CRITICAL: csgenerate must use ONLY the plain stage1 default.profdata for
    # -fprofile-use. merged.profdata (if it exists) contains CS records from a
    # prior CS cycle keyed to the previous csgenerate binary's IR. Feeding those
    # CS records through -fprofile-use during a new csgenerate changes inlining
    # decisions relative to the plain stage1 baseline, restructuring the IR that
    # the new CS counters are keyed to. When the use stage then compiles from the
    # plain stage1 baseline (as it must), every function reshaped by the stale CS
    # influence hash-mismatches — producing a CS binary that is worse than plain
    # IR PGO rather than better. Always start the CS layer from the clean stage1
    # profile only.
    local stage1_pd="${PROFILE_DIR}/default.profdata"

    if [[ ! -f "${stage1_pd}" ]]; then
        # default.profdata is absent — try building it from profraw files.
        # normalize_profraw_dirs must run first: LLVM IR PGO writes profraw
        # *directories* named default-<pid>.profraw/ containing numbered chunk
        # files; without normalization the glob below passes directory paths to
        # llvm-profdata, which may silently skip or error on them.
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

    # CS-IRPGO compile flags:
    #   -fprofile-use=<stage1>       Apply stage1 IR profile (optimizes this build).
    #   -fcs-profile-generate=...    Layer CS counters on top.
    # Both flags are passed together. The compiler applies stage1 PGO optimizations
    # first, then inserts CS counters into the optimized IR.
    # cs-default-%p.profraw — %p expands to PID so parallel runs don't collide.
    # The output is relative (no directory prefix) so it writes next to the .exe
    # on Windows, where the Linux absolute path would be meaningless.
    local stage1_pd_compiler="${stage1_pd}"
    [[ "${_HOST_OS}" == "windows" ]] && stage1_pd_compiler="$(cygpath -m "${stage1_pd}")"
    local cs_gen_flag="-fcs-profile-generate=cs-default-%p.profraw"
    local pgo_use_flag="-fprofile-use=${stage1_pd_compiler}"
    local debug_flag=""
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
    local c_flags="-O3 -DNDEBUG ${debug_flag} ${pgo_use_flag} ${cs_gen_flag}${lto_generate_flag:+ ${lto_generate_flag}}"
    local cxx_flags="${c_flags}"
    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')

    # Force-keep profile runtime entry points.
    # CS-IRPGO uses the same LLVM InstrProfiling runtime as standard IR/FE PGO.
    # lld may dead-strip __llvm_profile_write_file when instrumented counter code
    # lives in archived libraries that are not directly referenced from main().
    # -u,__llvm_profile_runtime ensures InstrProfilingRuntime.o's constructor
    # fires on startup, initializing the write-file machinery.
    local extra_link_flags="-Wl,-u,__llvm_profile_write_file,-u,__llvm_profile_runtime"

    ensure_profile_runtime_mingw
    ensure_vulkan_import_lib
    local qt_install_dir="${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64"
    local qt_host_dir="${BUILD_GENERATE}/externals/qt-host/6.9.3/gcc_64"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        qt_host_dir=""
    fi
    local qt6_cmake_dir="${qt_install_dir}/lib/cmake/Qt6"

    GLSLC_PATH="$(command -v glslc 2>/dev/null || true)"
    [[ -z "${GLSLC_PATH}" ]] && GLSLC_PATH="$(command -v glslangValidator 2>/dev/null || true)"

    # Pre-build FFmpeg (sentinel makes this a fast no-op if already built for this dir)
    detect_ffmpeg_version
    rebuild_ffmpeg_pthread_free "${BUILD_CSGENERATE}"

    info "Configuring CMake (CS-IRPGO instrumented build)..."
    cd "${BUILD_CSGENERATE}"
    rm -f CMakeCache.txt; rm -rf CMakeFiles
    [[ -d "src/citron/citron_autogen" ]] && rm -rf src/citron/citron_autogen

    # shellcheck disable=SC2046
    cmake "${SOURCE_DIR}" \
        $(common_cmake_args) \
        ${qt6_cmake_dir:+"-DQt6_DIR=${qt6_cmake_dir}"} \
        ${qt_host_dir:+"-DQT_HOST_PATH=${qt_host_dir}"} \
        "-DCITRON_ENABLE_PGO_GENERATE=ON" \
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
        "-DCITRON_ENABLE_LTO=${generate_lto_cmake}" \
        "-DCMAKE_C_FLAGS_RELEASE=${c_flags}" \
        "-DCMAKE_CXX_FLAGS_RELEASE=${cxx_flags}" \
        "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=${c_flags} ${PROFILE_RUNTIME_LIB:+${PROFILE_RUNTIME_LIB}} ${extra_link_flags}" \
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}" \
        2>&1 | grep -v '^-- '; cmake_exit=${PIPESTATUS[0]}
    [[ ${cmake_exit} -eq 0 ]] || error "CMake configure failed"

    info "Building CS-IRPGO instrumented citron..."
    cmake --build . --config Release -j "${JOBS}"

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
            success "  __llvm_profile_raw_version  ✓"
            success "  __llvm_profile_runtime      ✓"
            success "  __llvm_profile_write_file   ✓"
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

            ensure_vulkan_import_lib
        create_vcpkg_llvm_triplet
        compile_comsupp_stubs
        setup_case_fixup_headers

        # ── Qt path detection ─────────────────────────────────────────────────
        # Search order: (1) generate's cached Qt (correct llvm-mingw variant),
        # (2) a prior nopgo run, (3) aqt download into nopgo's own externals.
        # Using find avoids hardcoding the Qt version and works after source upgrades.
        _nopgo_find_qt_target() {
            local root="$1"
            # Prefer llvm-mingw_64 variant; fall back to any Qt6Config.cmake found
            local hit
            hit=$(find "${root}/externals/qt" -maxdepth 6 \
                -name "Qt6Config.cmake" -path "*/llvm-mingw_64/*" 2>/dev/null | head -1)
            [[ -z "${hit}" ]] && \
                hit=$(find "${root}/externals/qt" -maxdepth 6 \
                    -name "Qt6Config.cmake" 2>/dev/null | head -1)
            [[ -n "${hit}" ]] && dirname "${hit}" || true
        }
        _nopgo_find_qt_host() {
            local root="$1"
            local hit
            hit=$(find "${root}/externals/qt-host" -maxdepth 6 \
                -name "Qt6Config.cmake" -path "*/gcc_64/*" 2>/dev/null | head -1)
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

        # If neither cache has Qt, download via aqt directly (same logic as generate).
        # This avoids citron's CMakeLists.txt auto-downloading the wrong MinGW variant.
        if [[ -z "${qt6_cmake_dir}" ]]; then
            warn "No cached Qt found in generate or prior nopgo build."
            warn "Downloading Qt via aqt into ${nopgo_dir}/externals/qt ..."
            if ! command -v aqt &>/dev/null && \
               ! "${HOME}/.local/bin/aqt" --version &>/dev/null 2>&1; then
                python3 -m pip install aqtinstall --break-system-packages --quiet
            fi
            local _aqt; _aqt="$(command -v aqt 2>/dev/null || echo "${HOME}/.local/bin/aqt")"
            mkdir -p "${nopgo_dir}/externals/qt"
            "${_aqt}" install-qt windows desktop 6.9.3 win64_llvm_mingw \
                --outputdir "${nopgo_dir}/externals/qt" \
                || error "Qt download failed.\n" \
                         "       Run generate first to cache Qt, then re-run:\n" \
                         "         ./build-clangtron-windows.sh use --pgo none --lto ${LTO_MODE}"
            qt6_cmake_dir="$(_nopgo_find_qt_target "${nopgo_dir}")"
            [[ -z "${qt6_cmake_dir}" ]] && \
                error "Qt downloaded but Qt6Config.cmake not found — check aqt output above."
        fi

        # ── Qt6Multimedia module ──────────────────────────────────────────────
        # The base aqt install omits optional modules. Mirror what stage_generate
        # does: check for Qt6MultimediaConfig.cmake and imageformats, and install if absent.
        if [[ -n "${qt6_cmake_dir}" ]]; then
            local _qt_install_root
            _qt_install_root="$(dirname "$(dirname "$(dirname "${qt6_cmake_dir}")")")"
            local _nopgo_needs_modules=0
            [[ ! -f "${_qt_install_root}/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake" ]] && _nopgo_needs_modules=1
            [[ ! -d "${_qt_install_root}/plugins/imageformats" ]] && _nopgo_needs_modules=1
            if [[ "${_nopgo_needs_modules}" -eq 1 ]]; then
                info "Qt6 modules (multimedia + imageformats) missing — installing via aqt..."
                if ! command -v aqt &>/dev/null && \
                   ! "${HOME}/.local/bin/aqt" --version &>/dev/null 2>&1; then
                    python3 -m pip install aqtinstall --break-system-packages --quiet
                fi
                local _aqt_mm; _aqt_mm="$(command -v aqt 2>/dev/null || echo "${HOME}/.local/bin/aqt")"
                local _mm_ver _mm_outdir
                _mm_ver="$(basename "$(dirname "${_qt_install_root}")")"
                _mm_outdir="$(dirname "$(dirname "${_qt_install_root}")")"
                local _mm_ok=0
                for _attempt in 1 2 3; do
                    "${_aqt_mm}" install-qt windows desktop "${_mm_ver}" win64_llvm_mingw \
                        --outputdir "${_mm_outdir}" \
                        --modules qtmultimedia qtimageformats && { _mm_ok=1; break; }
                    warn "Qt6Multimedia attempt ${_attempt} failed — retrying..."; sleep 5
                done
                [[ "${_mm_ok}" -eq 0 ]] && warn "Qt6Multimedia install failed after 3 attempts — build may fail"
            else
                info "Qt6Multimedia already present"
            fi
        fi

        if [[ -z "${qt_host_dir}" ]]; then
            if [[ "${_HOST_OS}" == "windows" ]]; then
                # Native build uses target Qt tools automatically (rcc, uic, moc)
                # Setting QT_HOST_PATH breaks native builds by triggering cross-compilation mode
                qt_host_dir=""
            else
                local _aqt; _aqt="$(command -v aqt 2>/dev/null || echo "${HOME}/.local/bin/aqt")"
                # Derive the Qt version from the target Qt we just located or downloaded
                # Walk up from .../qt/<ver>/<variant>/lib/cmake/Qt6 to extract the version:
                #   dirname x3 → .../qt/<ver>/<variant>  (variant dir)
                #   dirname x1 → .../qt/<ver>             (version dir)
                #   basename   → <ver>
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
        [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
        local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
        local lto_flag; lto_flag="$(lto_clang_flag)"

        # Pre-build FFmpeg for this build directory
        detect_ffmpeg_version
        rebuild_ffmpeg_pthread_free "${nopgo_dir}"

        info "Configuring CMake (no-PGO Windows PE, LTO=${LTO_MODE})..."
        cd "${nopgo_dir}"
        rm -f CMakeCache.txt; rm -rf CMakeFiles

        # shellcheck disable=SC2046
        cmake "${SOURCE_DIR}" \
            $(common_cmake_args) \
            "-DCITRON_ENABLE_PGO_USE=OFF" \
            "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
            "-DCMAKE_C_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_flag}" \
            "-DCMAKE_CXX_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_flag}" \
            ${qt6_cmake_dir:+"-DQt6_DIR=${qt6_cmake_dir}"} \
            ${qt_host_dir:+"-DQT_HOST_PATH=${qt_host_dir}"}
        info "Building citron.exe (no PGO, ${BUILD_TYPE})..."
        cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}"

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
    create_vcpkg_llvm_triplet
    compile_comsupp_stubs
    setup_case_fixup_headers
    ensure_vulkan_import_lib

    # ── Sentinel check: verify generate/use LTO and PGO modes match ─────────
    local _gen_cfg="${BUILD_ROOT}/.citron-gen-config"
    if [[ -f "${_gen_cfg}" ]]; then
        local _gen_lto _gen_pgo
        _gen_lto=$(grep -oP "(?<=LTO=)\S+" "${_gen_cfg}" || true)
        _gen_pgo=$(grep -oP "(?<=PGO=)\S+" "${_gen_cfg}" || true)
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

    # Auto-merge CS profraw if present and merged.profdata not yet written
    local cs_dir="${PROFILE_DIR}/cs"
    if [[ ! -f "${merged_pd}" && -d "${cs_dir}" ]]; then
        normalize_profraw_dirs "${cs_dir}"
        local cs_count
        cs_count=$(find "${cs_dir}" -name "*.profraw" 2>/dev/null | wc -l)
        if [[ "${cs_count}" -gt 0 ]]; then
            info "CS profraw detected (${cs_count} files) — merging with stage1..."
            # Step 1: merge CS profraw files → cs-only.profdata
            #   llvm-profdata auto-detects the CSIRInstr kind from the profraw header;
            #   no special flag needed beyond --sparse.
            local cs_tmp="${PROFILE_DIR}/cs-only.profdata"
            "${LLVM_PROFDATA}" merge --sparse \
                --output="${cs_tmp}" "${cs_dir}"/*.profraw
            # Step 2: merge stage1 default.profdata + cs-only.profdata → merged.profdata
            #   The result contains both regular IR records (from stage1) and CS records
            #   (from csgenerate). -fprofile-use= in the use stage consumes both kinds.
            "${LLVM_PROFDATA}" merge --sparse \
                --output="${merged_pd}" "${profdata}" "${cs_tmp}"
            rm -f "${cs_tmp}"
            success "CS-IRPGO merged profile written: ${merged_pd}"
            info "  Stage1 (IR)   : ${profdata}"
            info "  CS layer      : ${cs_dir}/*.profraw  (${cs_count} file(s))"
            info "  Merged output : ${merged_pd}"
            profdata="${merged_pd}"
        fi
    fi

    local lto_flag; lto_flag="$(lto_clang_flag)"
    local profdata_compiler="${profdata}"
    [[ "${_HOST_OS}" == "windows" ]] && profdata_compiler="$(cygpath -m "${profdata}")"
    local pgo_flag
    if [[ "${PGO_MODE}" == "ir" ]]; then
        pgo_flag="-fprofile-use=${profdata_compiler}"
    else
        pgo_flag="-fprofile-instr-use=${profdata_compiler} -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
    fi
    local lto_pgo_flag="${lto_flag:+${lto_flag} }${pgo_flag}"

    ensure_vulkan_import_lib

    # ── 2a. Cross-compiled Windows PE ────────────────────────────────────────
    # Pre-build FFmpeg for this build directory
    detect_ffmpeg_version
    rebuild_ffmpeg_pthread_free "${BUILD_USE}"

    info "Configuring CMake (PGO+LTO Windows PE)..."
    mkdir -p "${BUILD_USE}"; cd "${BUILD_USE}"
    rm -f CMakeCache.txt; rm -rf CMakeFiles

    # Reuse generate's already-downloaded Qt — passing Qt6_DIR prevents citron's
    # cmake from re-downloading Qt into use/externals/ with the wrong variant
    # (win64_mingw → mingw_64 instead of win64_llvm_mingw → llvm-mingw_64).
    local qt_install_dir="${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64"
    local qt_host_dir="${BUILD_GENERATE}/externals/qt-host/6.9.3/gcc_64"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        qt_host_dir=""
    fi
    local qt6_cmake_dir="${qt_install_dir}/lib/cmake/Qt6"

    # Note on BOLT PE relocation coverage:
    # --emit-relocs is an ELF-only lld flag and has no equivalent in lld's MinGW
    # COFF mode. This is fine: Windows PE binaries always contain a base relocation
    # table (.reloc section) for ASLR, and BOLT's PE/COFF mode uses that table to
    # locate and patch code references. The ELF build (use-elf) retains its own
    # --emit-relocs flag for the ELF-proxy BOLT path.

    local debug_flag=""
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
    # shellcheck disable=SC2046
    cmake "${SOURCE_DIR}" \
        $(common_cmake_args) \
        "-DCITRON_ENABLE_PGO_USE=ON" \
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
        "-DCMAKE_C_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}" \
        "-DCMAKE_CXX_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}" \
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}" \
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}" \
        ${qt6_cmake_dir:+"-DQt6_DIR=${qt6_cmake_dir}"} \
        ${qt_host_dir:+"-DQT_HOST_PATH=${qt_host_dir}"}
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
    #
    # TWO PROBLEMS with DownloadExternals.cmake's Qt download for Linux:
    #
    # Problem 1 — aqt command uses wrong syntax with aqt 3.x:
    #   DownloadExternals invokes aqt with 'qt_base' and 'qtmultimedia' as
    #   package/module names. In aqt 3.x, 'qt_base' is not a valid module name
    #   and the entire command errors:
    #     ERROR: The packages ['qt_base', 'qtmultimedia'] were not found while
    #            parsing XML of package information!
    #   The base Qt is then NOT downloaded even though cmake prints
    #   "Downloaded Qt binaries" (it prints that unconditionally).
    #
    # Problem 2 — Qt6_DIR is set to install root, not cmake subdir:
    #   DownloadExternals sets Qt6_DIR via FORCE to the install root
    #   (.../linux) rather than the cmake config subdir (.../linux/lib/cmake/Qt6).
    #   find_package(Qt6) can't find Qt6Config.cmake at the install root →
    #   falls back to system Qt 6.4.2 → no Qt6GuiPrivate cmake config → FAIL.
    #
    # FIX:
    #   1. Pre-download Qt 6.9.3 linux via aqt using correct 3.x syntax.
    #      The linux_gcc_64 base package INCLUDES Qt6GuiPrivate cmake configs,
    #      unlike qt-host (downloaded with broken old syntax) which does not.
    #   2. Qt6Multimedia: aqt cannot install it for linux desktop. Inject a
    #      minimal stub + pass -DCITRON_USE_QT_MULTIMEDIA=OFF so it is never
    #      actually linked.
    #   3. Do NOT set QT_HOST_PATH — triggers cross-compilation mode on a native
    #      build, causing cmake to ignore Qt6_DIR entirely.
    #
    local elf_qt_dir="${BUILD_USE_ELF}/externals/qt/6.9.3/linux"
    local elf_qt_cmake_dir="${elf_qt_dir}/lib/cmake/Qt6"

    # Remove a stale symlink to qt-host (which lacks GuiPrivate) if present.
    if [[ -L "${elf_qt_dir}" ]]; then
        info "ELF build: removing qt-host symlink (qt-host lacks Qt6GuiPrivate)"
        rm -f "${elf_qt_dir}"
    fi

    # Verify that the key Qt6 component cmake configs are all present.
    # Qt6::Network, Qt6::Widgets, Qt6::Svg, Qt6::DBus must exist.
    # If the cached dir is missing any of these (partial old download),
    # wipe it and re-download with all required modules.
    local _elf_qt_ok=1
    for _qtmod in Qt6 Qt6Network Qt6Widgets Qt6Gui Qt6DBus Qt6Svg Qt6OpenGL; do
        if [[ ! -f "${elf_qt_dir}/lib/cmake/${_qtmod}/${_qtmod}Config.cmake" ]]; then
            warn "ELF build: missing Qt cmake config: ${_qtmod}Config.cmake"
            _elf_qt_ok=0
        fi
    done

    if [[ "${_elf_qt_ok}" -eq 0 || ! -f "${elf_qt_cmake_dir}/Qt6Config.cmake" ]]; then
        info "ELF build: (re-)downloading Qt 6.9.3 linux via aqt (linux_gcc_64 + modules)..."
        python3 -m pip install aqtinstall --break-system-packages --quiet 2>/dev/null || true
        local aqt_base_dir="${BUILD_USE_ELF}/externals/qt"
        # Wipe any partial previous download
        rm -rf "${aqt_base_dir}/6.9.3"
        mkdir -p "${aqt_base_dir}"
        # Base install: linux_gcc_64 includes Core/Gui/Widgets/Network/DBus/OpenGL/etc.
        python3 -m aqt install-qt             --outputdir "${aqt_base_dir}"             linux desktop 6.9.3 linux_gcc_64             || warn "aqt Qt 6.9.3 base download failed"
        # Rename aqt output dir to 'linux' (what DownloadExternals.cmake expects)
        for _arch in gcc_64 linux_gcc_64; do
            if [[ -d "${aqt_base_dir}/6.9.3/${_arch}" && "${_arch}" != "linux" ]]; then
                rm -rf "${aqt_base_dir}/6.9.3/linux"
                mv "${aqt_base_dir}/6.9.3/${_arch}" "${aqt_base_dir}/6.9.3/linux"
                success "ELF build: Qt 6.9.3 linux downloaded (arch was ${_arch})"
                break
            fi
        done
        # Install extra modules: qtsvg is required; qtnetwork is qtbase but add explicitly
        python3 -m aqt install-qt             --outputdir "${aqt_base_dir}"             linux desktop 6.9.3 linux_gcc_64             --modules qtsvg 2>/dev/null             || warn "aqt qtsvg module install failed (may already be present)"
        if [[ ! -f "${elf_qt_cmake_dir}/Qt6Config.cmake" ]]; then
            warn "ELF build: Qt6Config.cmake still missing after aqt download — check aqt output"
        fi
    else
        info "ELF build: Qt 6.9.3 already present at ${elf_qt_dir}"
    fi

    # Qt6Multimedia: aqt cannot install this for linux desktop (GStreamer dependency).
    # Inject a stub so find_package(Qt6 REQUIRED COMPONENTS Multimedia) doesn't abort.
    # -DCITRON_USE_QT_MULTIMEDIA=OFF (passed below) ensures it is never linked.
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

    # Qt6GuiPrivate: the aqt base package DOES ship private headers at
    # include/QtGui/6.9.3/ but does NOT ship the cmake config that wraps them.
    # Without a Qt6GuiPrivateConfig.cmake, find_package fails even though the
    # headers are physically present. Inject a real stub that points cmake at
    # the actual private headers in the aqt download.
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

    # CMAKE_PREFIX_PATH must include Qt AND the pre-built Vulkan/SPIRV header
    # installs. The generate stage builds these header-only packages into
    # VULKAN_HEADERS_INSTALL and SPIRV_HEADERS_INSTALL; they have no Windows-
    # specific binaries and work identically for the native ELF build.
    # Not including them here causes VulkanHeaders version mismatch (system has
    # 1.3.275, externals submodule requires 1.4.337+) and missing SPIRV-Headers.
    local elf_cmake_prefix="${elf_qt_dir};${VULKAN_HEADERS_INSTALL};${SPIRV_HEADERS_INSTALL}"
    info "ELF build: CMAKE_PREFIX_PATH → ${elf_cmake_prefix}"

    # The ELF is the profiling target for both BOLT and Propeller stages.
    # CITRON_ENABLE_LTO=OFF bypasses citron's cmake check_ipo_supported() path
    # which fails silently on native Linux builds. No LTO is used for this ELF:
    # LTO prevents -fbasic-block-address-map from emitting the .llvm_bb_addr_map
    # section (ThinLTO backend in lld does not propagate the flag). Without LTO,
    # every TU compiles to native code directly and the section is always present.
    # PGO data alone provides representative hot-path guidance for profiling.
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
        elf_pgo_flag="-fprofile-use=${profdata}"
    else
        elf_pgo_flag="-fprofile-instr-use=${profdata} -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
    fi
    # -fbasic-block-address-map: emit the .llvm_bb_addr_map section that
    # create_llvm_prof reads to generate a Propeller BB+function layout profile.
    #
    # NOTE: LTO is intentionally DISABLED for the Propeller ELF build.
    # With -flto=thin the compiler emits LLVM IR bitcode (not native code) per
    # translation unit, and native code + section emission only happens in lld's
    # ThinLTO backend at link time. lld's ThinLTO backend does not propagate
    # -fbasic-block-address-map, so the section never appears in the final binary.
    # Without LTO, every TU is compiled directly to native code and the section
    # is always emitted. PGO data alone provides representative hot-path coverage.
    local debug_flag=""
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
    local elf_compile_flags="-O3 -DNDEBUG ${debug_flag} -D_stat64=stat ${elf_pgo_flag} -fbasic-block-address-map -Wno-error=backend-plugin"
    local elf_linker_flags="-fuse-ld=lld-${CLANG_VERSION} -Wl,--emit-relocs"

    # ── Flag-change detection: wipe stale object cache if compile flags changed ──
    # Wiping CMakeCache.txt alone is not enough — ninja reuses cached .o files
    # whose flags are baked in at compile time. Detect changes via md5 sentinel
    # and wipe all build artifacts except externals/ (Qt/ffmpeg, ~500 MB).
    local _elf_flags_hash _elf_flags_stored=""
    _elf_flags_hash=$(printf '%s' "${elf_compile_flags}" | md5sum | cut -d' ' -f1)
    local _elf_flags_sentinel="${BUILD_USE_ELF}/.elf_flags_hash"
    [[ -f "${_elf_flags_sentinel}" ]] && _elf_flags_stored=$(cat "${_elf_flags_sentinel}" 2>/dev/null || true)

    if [[ "${_elf_flags_hash}" != "${_elf_flags_stored}" ]]; then
        info "ELF compile flags changed — wiping object cache (preserving externals/)..."
        find "${BUILD_USE_ELF}" -mindepth 1 -maxdepth 1 \
            ! -name "externals" -exec rm -rf {} + 2>/dev/null || true
        mkdir -p "${BUILD_USE_ELF}"
        success "ELF build cache wiped — full recompile will run with new flags"
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
        "-DVulkan_INCLUDE_DIR=${SOURCE_DIR}/externals/Vulkan-Headers/include"
        "-DVulkan_INCLUDE_DIRS=${SOURCE_DIR}/externals/Vulkan-Headers/include"
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

    # ── 3a. Instrument ELF ───────────────────────────────────────────────────
    # Ensure the BOLT runtime library is in place before instrumenting
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

    # ── 3d. Extract BOLT function order for PE linker ─────────────────────────
    #
    # BOLT cannot rewrite PE/COFF binaries directly, and its --symbol-ordering-file
    # flag is ELF-lld only. However, lld's COFF/PE mode supports /order:@<file>,
    # which controls the placement order of functions in .text — the same benefit
    # at function granularity (not basic-block, but still meaningful i-cache gain).
    #
    # We recover BOLT's computed layout from the optimized ELF: symbols in the
    # .text section sorted by address == the order BOLT placed them. We strip
    # BOLT's own internal symbols and cold-clone suffixes, then write a /order
    # file that lld-link will use to place functions in the same hot-first order
    # in the PE's .text section.
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

    # ── 3e. Re-link Windows PE with BOLT function order ───────────────────────
    info "Re-linking final Windows PE (PGO + LTO + BOLT function order)..."
    # Always wipe the cmake cache — stale CMAKE_EXE_LINKER_FLAGS can persist
    # from a previous failed run and cause the compiler test to fail.
    rm -rf "${BUILD_BOLT}"
    mkdir -p "${BUILD_BOLT}"; cd "${BUILD_BOLT}"

    # Use LTO_MODE (default: full) for the final PE re-link.
    # generate and use stages ran with ThinLTO, giving BOLT a consistent
    # profile and function-order file. The re-link is free to apply full
    # LTO on top — PGO profiles and BOLT ordering are already baked in,
    # and full LTO's whole-program inlining yields better runtime performance.
    local debug_flag=""
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
    local lto_flag; lto_flag="$(lto_clang_flag)"
    local _bolt_merged="${PROFILE_DIR}/merged.profdata"
    local profdata
    [[ -f "${_bolt_merged}" ]] && profdata="${_bolt_merged}" || profdata="${PROFILE_DIR}/default.profdata"
    local pgo_flag
    if [[ "${PGO_MODE}" == "ir" ]]; then
        pgo_flag="-fprofile-use=${profdata}"
    else
        pgo_flag="-fprofile-instr-use=${profdata} -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
    fi
    local lto_pgo_flag="${lto_flag:+${lto_flag} }${pgo_flag}"

    # /order:@<file> — COFF/PE lld flag for function placement order in .text.
    # Functions listed first are placed at the start of .text (hot region).
    # Functions not in the list keep their default LTO order after the listed ones.
    # -Wl, prefix passes the flag through clang to lld-link.
    # /ignore:4037 — suppress LNK4037 "missing symbol" warnings for /order entries.
    #   These warnings are harmless: symbols absent from a given binary (because
    #   ThinLTO inlined them, or because the binary is citron-room/citron-cmd which
    #   doesn't contain emulator-core code) simply keep their default link order.
    #   Without /ignore:4037 the 13k-entry order file produces ~28k warnings across
    #   the three executables that share CMAKE_EXE_LINKER_FLAGS_RELEASE.
    local order_linker_flag=""
    if [[ -n "${order_file}" ]]; then
        order_linker_flag="-Wl,/order:@${order_file} -Wl,/ignore:4037"
    fi

    local qt_install_dir="${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64"
    local qt_host_dir="${BUILD_GENERATE}/externals/qt-host/6.9.3/gcc_64"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        qt_host_dir=""
    fi
    local qt6_cmake_dir="${qt_install_dir}/lib/cmake/Qt6"

    # Pre-build FFmpeg for this build directory
    detect_ffmpeg_version
    rebuild_ffmpeg_pthread_free "${BUILD_BOLT}"

    # shellcheck disable=SC2046
    cmake "${SOURCE_DIR}" \
        $(common_cmake_args) \
        "-DCITRON_ENABLE_PGO_USE=ON" \
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
        "-DCMAKE_C_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}" \
        "-DCMAKE_CXX_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}" \
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}${order_linker_flag:+ ${order_linker_flag}}" \
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}" \
        ${qt6_cmake_dir:+"-DQt6_DIR=${qt6_cmake_dir}"} \
        ${qt_host_dir:+"-DQT_HOST_PATH=${qt_host_dir}"} \
        -Wno-dev

    info "Building final optimized Windows PE (PGO + LTO + BOLT function order, ${BUILD_TYPE})..."
    cmake --build . --config "${BUILD_TYPE}" -j "${JOBS}"

    deploy_runtime_dlls \
        "${BUILD_BOLT}/bin" \
        "${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64" \
        "${BUILD_BOLT}"

    # ── 3f. BOLT reorder summary ─────────────────────────────────────────────
    # Cross-reference the order file against citron.exe's actual symbol table
    # to report exactly how many hot functions were successfully placed.
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
# ensure_create_llvm_prof
#
# Builds generate_propeller_profiles from google/llvm-propeller and installs
# it as /usr/local/bin/create_llvm_prof for use by the propeller stage.
#
# google/llvm-propeller is the correct modern repo (autofdo's README says the
# Propeller codebase moved there as of 2025Q1). It has its own self-contained
# cmake build with FetchContent — no LLVM_PATH, no ENABLE_TOOL, no bundled-LLVM
# whack-a-mole. It natively understands BBAddrMap v3 (Clang 19+ format).
#
# Interface: --cc_profile / --ld_profile  (not --out/--format/--propeller_symorder)
#
# The installed binary is version-stamped; rebuilds on Clang version change.
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
# Stage: propeller — Propeller basic-block + function layout optimization
#
# Propeller is Google's feedback-directed optimization that operates at the
# basic-block level, feeding profiles back into the compiler before LTO inlining
# decisions are made. Unlike BOLT's post-link binary rewriting, Propeller works
# at compile time, which means:
#
#   - Inlined functions inherit layout guidance at their call sites (LTO-resilient)
#   - Basic-block layout influences the compiler's register allocation and code
#     generation, not just the final binary section placement
#   - Profile collection uses the Linux ELF (same binary already built for BOLT)
#     running under perf record -b (branch stack sampling)
#
# HARDWARE REQUIREMENTS:
#   Branch-stack sampling requires hardware branch recording support:
#     - AMD Zen 4 (Ryzen 7940HS): uses BRBS (Branch Record Buffer Stores)
#       Requires kernel 6.1+ and linux-tools-$(uname -r)
#     - Intel 13th gen (i9-13900H): uses LBR (Last Branch Records)
#       Requires linux-tools-$(uname -r)
#   Both work with the same perf -b flag — the kernel picks the right backend.
#
# WORKFLOW:
#   1. The build-elf stage builds the ELF with -fbasic-block-address-map,
#      which embeds a .llvm_bb_addr_map section mapping basic blocks to addresses.
#   2. This stage runs citron under perf record -b to collect branch stacks.
#   3. create_llvm_prof converts perf.data + ELF to two Propeller profile files:
#        propeller_cc.prof    — basic-block layout list (passed via -fbasic-block-sections=list=)
#        propeller_symorder.txt — hot function order (passed to linker /order:@)
#   4. The Windows PE is rebuilt with:
#        -fbasic-block-sections=list=propeller_cc.prof  (BB-level layout in PE — distinct flag, still valid)
#        /order:@propeller_symorder.txt                 (function ordering)
#      plus the same PGO+LTO flags as the use stage.
#
# NOTE on PE/COFF + -fbasic-block-sections=list (for the Propeller rebuild):
#   This flag (which feeds a BB profile back to the compiler) is different from
#   -fbasic-block-address-map (which annotates the ELF for profiling). It is
#   primarily designed for ELF targets. For PE/COFF, the compiler
#   still emits separate COFF sections per basic block, and lld's COFF mode will
#   merge them per the order file. In practice the BB-level benefit may be partial
#   (COFF section granularity is coarser than ELF). The function-order benefit
#   from propeller_symorder.txt is identical to the BOLT /order:@ path.
#
# OUTPUT:
#   build/propeller/bin/citron.exe   — Propeller-optimized Windows PE
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
    # generate_propeller_profiles (google/llvm-propeller) uses:
    #   --cc_profile  = BB layout profile (was: --out --format=propeller)
    #   --ld_profile  = function order     (was: --propeller_symorder)
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
    [[ "${BUILD_TYPE}" == "RelWithDebInfo" ]] && debug_flag="-g"
    local bt_upper; bt_upper=$(echo "${BUILD_TYPE}" | tr '[:lower:]' '[:upper:]')
    local lto_flag; lto_flag="$(lto_clang_flag)"
    local _prop_merged="${PROFILE_DIR}/merged.profdata"
    local profdata
    [[ -f "${_prop_merged}" ]] && profdata="${_prop_merged}" || profdata="${PROFILE_DIR}/default.profdata"
    local pgo_flag
    if [[ "${PGO_MODE}" == "ir" ]]; then
        pgo_flag="-fprofile-use=${profdata}"
    else
        pgo_flag="-fprofile-instr-use=${profdata} -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
    fi
    local lto_pgo_flag="${lto_flag:+${lto_flag} }${pgo_flag}"

    # -fbasic-block-sections=list=<cc_profile>:
    #   Compiler reads the Propeller CC profile and splits the listed basic blocks into
    #   separate COFF sections. lld then orders those sections per the symorder.
    #   Falls back gracefully if the profile references functions absent in this
    #   build (e.g. inlined away by LTO) — those entries are silently ignored.
    # /order:@<symorder>: COFF/PE lld function placement (same mechanism as BOLT).
    # /ignore:4037: suppress LNK4037 for symorder entries absent from the PE.
    local propeller_linker_flag=""
    if [[ ${have_sym} -eq 1 ]]; then
        propeller_linker_flag="-Wl,/order:@${symorder} -Wl,/ignore:4037"
    fi

    local qt_install_dir="${BUILD_GENERATE}/externals/qt/6.9.3/llvm-mingw_64"
    local qt_host_dir="${BUILD_GENERATE}/externals/qt-host/6.9.3/gcc_64"
    if [[ "${_HOST_OS}" == "windows" ]]; then
        qt_host_dir=""
    fi
    local qt6_cmake_dir="${qt_install_dir}/lib/cmake/Qt6"

    # Pre-build FFmpeg for this build directory
    detect_ffmpeg_version
    rebuild_ffmpeg_pthread_free "${BUILD_PROPELLER}"

    # shellcheck disable=SC2046
    cmake "${SOURCE_DIR}" \
        $(common_cmake_args) \
        "-DCITRON_ENABLE_PGO_USE=ON" \
        "-DCITRON_PGO_FLAGS_MANAGED_BY_SCRIPT=ON" \
        "-DCMAKE_C_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}" \
        "-DCMAKE_CXX_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}" \
        "-DCMAKE_EXE_LINKER_FLAGS_${bt_upper}=-O3 -DNDEBUG ${debug_flag} ${lto_pgo_flag}${propeller_linker_flag:+ ${propeller_linker_flag}}" \
        "-DCITRON_PGO_PROFILE_DIR=${PROFILE_DIR}" \
        ${qt6_cmake_dir:+"-DQt6_DIR=${qt6_cmake_dir}"} \
        ${qt_host_dir:+"-DQT_HOST_PATH=${qt_host_dir}"} \
        -Wno-dev
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
        --relwithdebinfo)
            BUILD_TYPE="RelWithDebInfo"; shift ;;
        --unity)
            UNITY_BUILD="ON"; shift ;;
        --no-unity)
            UNITY_BUILD="OFF"; shift ;;
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
        --help|-h)
            sed -n '/^# USAGE/,/^# ====/p' "$0"
            exit 0 ;;
        *)
            error "Unknown argument: $1\nRun with --help for usage." ;;
    esac
done

[[ -n "$STAGE" ]] || error "No stage specified. Run with --help for usage."

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
