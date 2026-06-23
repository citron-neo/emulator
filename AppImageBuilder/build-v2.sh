#!/bin/bash
set -e

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BUILD_DIR_RAW="${1:-$SCRIPT_DIR/../build/generate}"
BUILD_DIR="$(readlink -f "${BUILD_DIR_RAW}")"

# Save environment overrides
ENV_QT_PATH="${CITRON_QT_PATH:-}"
ENV_ICU_PATH="${CITRON_ICU_PATH:-}"
ENV_XCB_PATH="${CITRON_XCB_PATH:-}"
ENV_BINARY_DIR="${CITRON_BINARY_DIR:-}"

if [ ! -f "$BUILD_DIR/AppImageBuilder/config.sh" ]; then
    echo "Error: config.sh not found in $BUILD_DIR/AppImageBuilder/"
    echo "Please run CMake first."
    exit 1
fi

source "$BUILD_DIR/AppImageBuilder/config.sh"

# Allow environment variables to override config.sh
export CITRON_QT_PATH="${ENV_QT_PATH:-$CITRON_QT_PATH}"
export CITRON_ICU_PATH="${ENV_ICU_PATH:-$CITRON_ICU_PATH}"
export CITRON_XCB_PATH="${ENV_XCB_PATH:-$CITRON_XCB_PATH}"
export CITRON_BINARY_DIR="${ENV_BINARY_DIR:-$CITRON_BINARY_DIR}"

# Tools setup
TOOLS_DIR="$BUILD_DIR/AppImageBuilder/tools"
mkdir -p "$TOOLS_DIR"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

download_tool() {
    local url=$1
    local file=$2
    if [ ! -f "$file" ]; then
        echo "Downloading $file..."
        wget -q --show-progress "$url" -O "$file"
        chmod +x "$file"
    fi
}

download_tool "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "$LINUXDEPLOY"
download_tool "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" "$LINUXDEPLOY_QT"

# AppDir setup
APPDIR="$BUILD_DIR/AppImageBuilder/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR"

# Build the AppImage
# We need to tell linuxdeploy where our custom libraries are
export LD_LIBRARY_PATH="$CITRON_ICU_PATH:$CITRON_XCB_PATH/lib:$LD_LIBRARY_PATH"
export EXTRA_QT_PLUGINS="wayland" # Add any extra plugins needed
export NO_STRIP=1 # Avoid strip failures on modern binaries (SHT_RELR)

# Run linuxdeploy
# --plugin qt handles Qt dependencies
export QMAKE="$CITRON_QT_PATH/bin/qmake"
export QT_ROOT="$CITRON_QT_PATH"
export LD_LIBRARY_PATH="$CITRON_QT_PATH/lib:$CITRON_ICU_PATH:$CITRON_XCB_PATH/lib:${LD_LIBRARY_PATH:-}"

pushd "$BUILD_DIR" > /dev/null
# Run linuxdeploy to populate AppDir
"$LINUXDEPLOY" --appdir "$APPDIR" \
    --executable "$CITRON_BINARY_DIR/citron" \
    --desktop-file "$SCRIPT_DIR/assets/citron.desktop" \
    --icon-file "$SCRIPT_DIR/assets/citron.svg" \
    --library "$CITRON_ICU_PATH/libicuuc.so.73" \
    --library "$CITRON_ICU_PATH/libicui18n.so.73" \
    --library "$CITRON_ICU_PATH/libicudata.so.73" \
    --library "$CITRON_XCB_PATH/lib/libxcb.so.1" \
    --library "$CITRON_XCB_PATH/lib/libxcb-cursor.so.0" \
    --library "$CITRON_XCB_PATH/lib/libxcb-xkb.so.1" \
    --library "$CITRON_XCB_PATH/lib/libxcb-render.so.0" \
    --library "$CITRON_XCB_PATH/lib/libxcb-render-util.so.0" \
    --library "$CITRON_XCB_PATH/lib/libxcb-shape.so.0" \
    --library "$CITRON_XCB_PATH/lib/libxcb-shm.so.0" \
    --library "$CITRON_XCB_PATH/lib/libxcb-sync.so.1" \
    --library "$CITRON_XCB_PATH/lib/libxcb-xfixes.so.0" \
    --library "$CITRON_XCB_PATH/lib/libxcb-randr.so.0" \
    --library "$CITRON_XCB_PATH/lib/libxcb-image.so.0" \
    --library "$CITRON_XCB_PATH/lib/libxcb-keysyms.so.1" \
    --library "$CITRON_XCB_PATH/lib/libxcb-icccm.so.4" \
    --library "$CITRON_XCB_PATH/lib/libXau.so.6" \
    --library "$CITRON_XCB_PATH/lib/libXdmcp.so.6" \
    --plugin qt

# Inject the wrapper script to support portable mode (user folder) and PGO profiles
# We rename the real binary to citron.bin and put the script as 'citron'
if [ -f "$APPDIR/usr/bin/citron" ]; then
    mv "$APPDIR/usr/bin/citron" "$APPDIR/usr/bin/citron.bin"
    cp "$SCRIPT_DIR/assets/citron.sh" "$APPDIR/usr/bin/citron"
    chmod +x "$APPDIR/usr/bin/citron"
fi

# Build the final AppImage
"$LINUXDEPLOY" --appdir "$APPDIR" --output appimage

popd > /dev/null

echo "AppImage build complete in $BUILD_DIR"
