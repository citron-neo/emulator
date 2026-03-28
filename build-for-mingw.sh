#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Automated MinGW (MSYS2 UCRT64) build script for Citron Neo.
# Run from the MSYS2 UCRT64 shell, or use build-for-mingw.bat to launch it.

set -euo pipefail

# ---------- configuration ----------
BUILD_DIR="${1:-build-mingw}"
BUILD_TYPE="${2:-Release}"
JOBS="${3:-$(nproc)}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ OK ]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
fail()  { echo -e "${RED}[FAIL]${NC}  $*"; exit 1; }

# ---------- environment check ----------
if [[ -z "${MSYSTEM:-}" ]]; then
    fail "Not running inside an MSYS2 shell. Use the UCRT64 terminal or build-for-mingw.bat."
fi
if [[ "$MSYSTEM" != "UCRT64" ]]; then
    warn "MSYSTEM is '$MSYSTEM', expected 'UCRT64'. Builds may use the wrong toolchain."
fi

command -v pacman >/dev/null 2>&1 || fail "pacman not found — is this really MSYS2?"

# ---------- dependency installation ----------
info "Installing / updating build tools and dependencies via pacman …"

PACKAGES=(
    # build tools
    mingw-w64-ucrt-x86_64-cmake
    mingw-w64-ucrt-x86_64-ninja
    mingw-w64-ucrt-x86_64-toolchain

    # required libraries
    mingw-w64-ucrt-x86_64-boost
    mingw-w64-ucrt-x86_64-fmt
    mingw-w64-ucrt-x86_64-nlohmann-json
    mingw-w64-ucrt-x86_64-opus
    mingw-w64-ucrt-x86_64-SDL2
    mingw-w64-ucrt-x86_64-qt6-base
    mingw-w64-ucrt-x86_64-qt6-multimedia
    mingw-w64-ucrt-x86_64-qt6-svg
    mingw-w64-ucrt-x86_64-qt6-tools
    mingw-w64-ucrt-x86_64-ffmpeg
    mingw-w64-ucrt-x86_64-openal
    mingw-w64-ucrt-x86_64-vulkan-headers
    mingw-w64-ucrt-x86_64-vulkan-utility-libraries
    mingw-w64-ucrt-x86_64-vulkan-memory-allocator
    mingw-w64-ucrt-x86_64-libusb
    mingw-w64-ucrt-x86_64-enet
    mingw-w64-ucrt-x86_64-stb
)

pacman -S --needed --noconfirm "${PACKAGES[@]}" || fail "pacman install failed"
ok "All dependencies installed."

# ---------- configure ----------
info "Configuring CMake (${BUILD_TYPE}) in ${BUILD_DIR} …"
mkdir -p "${SOURCE_DIR}/${BUILD_DIR}"
cd "${SOURCE_DIR}/${BUILD_DIR}"

cmake "$SOURCE_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCITRON_USE_BUNDLED_VCPKG=OFF \
    -DCITRON_USE_BUNDLED_SDL2=OFF \
    -DCITRON_USE_BUNDLED_QT=OFF \
    -DCITRON_USE_BUNDLED_FFMPEG=OFF \
    -DCITRON_USE_EXTERNAL_VULKAN_HEADERS=OFF \
    -DCITRON_USE_EXTERNAL_VULKAN_UTILITY_LIBRARIES=OFF \
    -DENABLE_QT_TRANSLATION=OFF \
    -DUSE_DISCORD_PRESENCE=OFF \
    -DCITRON_TESTS=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DCITRON_USE_FASTER_LD=OFF \
    || fail "CMake configuration failed"

ok "CMake configuration succeeded."

# ---------- build ----------
info "Building with ${JOBS} parallel jobs …"
ninja -j"$JOBS" || fail "Build failed"

ok "Build complete!"
echo ""
info "Executables are in: ${SOURCE_DIR}/${BUILD_DIR}/bin/"
ls -lh "${SOURCE_DIR}/${BUILD_DIR}/bin/"*.exe 2>/dev/null || true
echo ""
ok "Done. You can run citron from: ${BUILD_DIR}/bin/citron.exe"
