#!/bin/sh
# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
#
# package-citron-linux.sh — build a citron AppImage + portable tar.zst using
# pkgforge's quick-sharun (fetched fresh below, not vendored — see the fetch
# block for why).
#
# Called by build-citron-linux.sh's build_appimage_stage() once citron is
# staged under build_dir/install-root. Can also run standalone if that
# install tree already exists.
#
# Env vars:
#   APP_VERSION     Version string for artifact filenames (required)
#   ARCH            CPU arch (default: uname -m)
#   ARCH_SUFFIX     Extra filename suffix, e.g. "_v3"
#   DEVEL           "true" renames the app to "citron nightly"
#   OUTPATH         Output dir for finished artifacts (default: $PWD/dist)
#   DESTDIR         Staging root under which citron was installed (/usr layout)
#   STRACE_MODE     dlopen/LD_DEBUG scan on startup (default: 0)
#   DEPLOY_VULKAN   Bundle Vulkan loader (default: 1)
#   DEPLOY_OPENGL   Bundle Mesa OpenGL/EGL/GLX (default: 0)
#   DEPLOY_PIPEWIRE Bundle PipeWire tree (default: 0)
#   DEPLOY_GLIBC    Bundle glibc from Arch container (default: 1)
#   DEPLOY_GTK      Bundle GTK modules (default: 0)
#   CITRON_QT_PATH  CPM Qt6 prefix
#   CITRON_XCB_PATH CPM XCB prefix
#   CITRON_VULKAN_LOADER_PATH CPM Vulkan loader lib dir

set -ex

ARCH="${ARCH:-$(uname -m)}"

if [ -z "${APP_VERSION:-}" ]; then
    echo "Error: APP_VERSION environment variable is not set." >&2
    exit 1
fi

DESTDIR="${DESTDIR:-}"
OUTNAME_BASE="citron_nightly-${APP_VERSION}-linux-${ARCH}${ARCH_SUFFIX:-}"
OUTPATH="${OUTPATH:-$PWD/dist}"
export OUTNAME="${OUTNAME_BASE}.AppImage"
export DESKTOP="${DESTDIR}/usr/share/applications/org.citron_emu.citron.desktop"

# Prefer a rasterised 256x256 PNG for .DirIcon — bare SVG fails silently as
# an AppImage icon on most desktops (appimaged, KDE, GNOME all want a PNG).
# Priority: repo-committed PNG > convert the installed SVG > raw SVG.
_png_from_repo="${DESTDIR}/usr/share/icons/hicolor/256x256/apps/org.citron_emu.citron.png"
_svg_icon="${DESTDIR}/usr/share/icons/hicolor/scalable/apps/org.citron_emu.citron.svg"
_png_icon="/tmp/citron-diricon-$$.png"
_try_svg2png() {
    if command -v rsvg-convert >/dev/null 2>&1; then
        rsvg-convert -w 256 -h 256 "$1" -o "$2" 2>/dev/null && return 0
    fi
    if command -v inkscape >/dev/null 2>&1; then
        inkscape --export-filename="$2" --export-width=256 --export-height=256 \
            "$1" >/dev/null 2>&1 && return 0
    fi
    if command -v convert >/dev/null 2>&1; then
        convert -background none -size 256x256 "$1" "$2" 2>/dev/null && return 0
    fi
    if command -v magick >/dev/null 2>&1; then
        magick -background none -size 256x256 "$1" "$2" 2>/dev/null && return 0
    fi
    return 1
}
if [ -f "$_png_from_repo" ]; then
    export ICON="$_png_from_repo"
elif [ -f "$_svg_icon" ] && _try_svg2png "$_svg_icon" "$_png_icon"; then
    export ICON="$_png_icon"
else
    export ICON="$_svg_icon"
fi

# ── quick-sharun deployment flags ───────────────────────────────────────────
export STRACE_MODE="${STRACE_MODE:-0}"
export DEPLOY_VULKAN="${DEPLOY_VULKAN:-1}"
export DEPLOY_OPENGL="${DEPLOY_OPENGL:-0}"
export DEPLOY_PIPEWIRE="${DEPLOY_PIPEWIRE:-0}"
# DEPLOY_GLIBC=1: Bundle glibc from Arch container (glibc 2.41+) for broad host compatibility.
export DEPLOY_GLIBC="${DEPLOY_GLIBC:-1}"
# DEPLOY_GTK=0: GTK3 is not bundled. libqgtk3.so is purged in cleanup below to force Qt
# to use D-Bus libqxdgdesktopportal.so, avoiding host GTK module crashes on GTK distros.
export DEPLOY_GTK="${DEPLOY_GTK:-0}"

# ── CPM over system: steer dependency scan ──────────────────────────────────
if [ -n "${CITRON_QT_PATH:-}" ] && [ -z "${QT_LOCATION:-}" ]; then
    export QT_LOCATION="${CITRON_QT_PATH}"
fi
_cpm_lib_path=""
for _p in "${CITRON_QT_PATH:+${CITRON_QT_PATH}/lib}" \
          "${CITRON_XCB_PATH:+${CITRON_XCB_PATH}/lib}"; do
    [ -n "${_p}" ] || continue
    _cpm_lib_path="${_cpm_lib_path}${_cpm_lib_path:+:}${_p}"
done
if [ -n "${_cpm_lib_path}" ]; then
    export LD_LIBRARY_PATH="${_cpm_lib_path}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
fi

# ── Fetch quick-sharun ──────────────────────────────────────────────────────
#
# Pinned to a commit rather than tracking refs/heads/main
# so a change upstream can't silently alter DEPLOY_*
# defaults or the AppDir layout underneath us; bump QUICK_SHARUN_REF
# deliberately when needed. HOOKSRC is pinned to the same commit so
# ADD_HOOKS below resolves against a matching tree.
QUICK_SHARUN_REF="e9414c02f713359b551bcfa3832576d2992b13da"
QUICK_SHARUN_URL="https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/${QUICK_SHARUN_REF}/useful-tools/quick-sharun.sh"
export HOOKSRC="https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/${QUICK_SHARUN_REF}/useful-tools/hooks"
curl -fL --retry 30 "${QUICK_SHARUN_URL}" -o quick-sharun \
    || { echo "Error: failed to fetch quick-sharun from ${QUICK_SHARUN_URL}" >&2; exit 1; }
chmod +x quick-sharun


# Bundle binary + runtime libs that are dlopen'd rather than linked, so
# quick-sharun's static ldd scan can't find them on its own. Located via
# `ldconfig -p` instead of a hardcoded /usr/lib*/*.so glob because multiarch
# systems (Debian/Ubuntu) keep them under /usr/lib/<triplet>/. All are
# best-effort: a glob matching nothing would otherwise pass quick-sharun a
# literal nonexistent path (POSIX sh has no nullglob) and it would abort.
EXTRA_LIBS=""

GAMEMODE_LIB="$(ldconfig -p 2>/dev/null | awk '/libgamemode\.so/ {print $NF; exit}')"
[ -n "$GAMEMODE_LIB" ] && EXTRA_LIBS="$EXTRA_LIBS $GAMEMODE_LIB"

PULSE_LIB="$(ldconfig -p 2>/dev/null | awk '/libpulse\.so/ {print $NF; exit}')"
if [ -n "$PULSE_LIB" ]; then
    EXTRA_LIBS="$EXTRA_LIBS $PULSE_LIB"
    _pulse_dir="$(dirname "$PULSE_LIB")"
    PULSECOMMON_LIB="$(find "$_pulse_dir" "${_pulse_dir}/pulseaudio" -maxdepth 1 -name 'libpulsecommon-*.so' 2>/dev/null | head -1)"
    [ -n "$PULSECOMMON_LIB" ] && EXTRA_LIBS="$EXTRA_LIBS $PULSECOMMON_LIB"
fi

# The bundled Vulkan loader has Wayland entry points compiled in (confirmed
# via nm -D: vkCreateWaylandSurfaceKHR, vkGetPhysicalDeviceWaylandPresent-
# ationSupportKHR) but dlopen()s libwayland-client.so.0 for them rather than
# linking it directly — invisible to the static ldd scan. Without it,
# VK_KHR_wayland_surface reports unavailable even though the loader supports
# it.
WAYLAND_CLIENT_LIB="$(ldconfig -p 2>/dev/null | awk '/libwayland-client\.so/ {print $NF; exit}')"
[ -n "$WAYLAND_CLIENT_LIB" ] && EXTRA_LIBS="$EXTRA_LIBS $WAYLAND_CLIENT_LIB"

# shellcheck disable=SC2086
./quick-sharun "${DESTDIR}/usr/bin/citron"* $EXTRA_LIBS
_appdir="${PWD}/AppDir"

# Qt translations: quick-sharun's own copy logic hardcodes the source as
# /usr/share/$QT_DIR/translations (a system path) — CPM Qt has no such
# directory on the build machine, so that copy silently never fires. Copy
# explicitly from the real CPM Qt SDK prefix instead.
if [ -n "${CITRON_QT_PATH:-}" ] && [ -d "${CITRON_QT_PATH}/translations" ]; then
    mkdir -p ./AppDir/usr/share/qt6
    cp -r "${CITRON_QT_PATH}/translations" ./AppDir/usr/share/qt6/translations
    rm -f ./AppDir/usr/share/qt6/translations/qtassistant*.qm \
          ./AppDir/usr/share/qt6/translations/qtdesigner*.qm \
          ./AppDir/usr/share/qt6/translations/linguist*.qm 2>/dev/null || true
fi

# Vulkan loader: Replace quick-sharun's system glob with CPM-built loader if path is provided.
if [ -n "${CITRON_VULKAN_LOADER_PATH:-}" ] && [ -d "${CITRON_VULKAN_LOADER_PATH}" ]; then
    find ./AppDir/lib -maxdepth 1 -iname 'libvulkan.so*' -delete 2>/dev/null || true
    cp -P "${CITRON_VULKAN_LOADER_PATH}"/libvulkan.so* ./AppDir/lib/
fi

# ── Post-dep-scan cleanup ────────────────────────────────────────────────────
# Stage ld-linux into AppDir/shared/lib/ for sharun AppRun.lib
_interp=$(patchelf --print-interpreter "${DESTDIR}/usr/bin/citron" 2>/dev/null)
if [ -z "${_interp}" ]; then
    _interp=$(readelf -l "${DESTDIR}/usr/bin/citron" 2>/dev/null \
        | awk '/\[Requesting program interpreter:/{gsub(/[][]/,""); print $NF}')
fi
if [ -n "${_interp}" ] && [ -f "${_interp}" ]; then
    mkdir -p "${_appdir}/shared/lib"
    cp -L "${_interp}" "${_appdir}/shared/lib/${_interp##*/}"
    printf 'Staged interpreter: %s\n' "${_interp##*/}"
else
    printf 'WARNING: could not determine PT_INTERP of citron; AppImage may fail with "Interpreter not found!"\n' >&2
fi

# Remove unused OpenGL/Mesa bloat (citron uses Vulkan only). Keep libqxcb-egl-integration.so.
find "${_appdir}" -name 'libqxcb-glx-integration.so' -delete 2>/dev/null || true
find "${_appdir}" -name 'libgallium-*.so'            -delete 2>/dev/null || true
find "${_appdir}" -name 'libLLVM.so.*'               -delete 2>/dev/null || true
find "${_appdir}" -path '*/dri/*'                    -delete 2>/dev/null || true
find "${_appdir}" -name 'libxcb-glx.so*'             -delete 2>/dev/null || true
# libqgtk3.so: Qt GTK3 platform theme. Purged to force Qt to use D-Bus libqxdgdesktopportal.so,
# preventing host GTK module crashes on GTK desktops (Linux Mint, Ubuntu GNOME, XFCE).
find "${_appdir}" -name 'libqgtk3.so'                -delete 2>/dev/null || true

# Sanity check: confirm citron and the bundled Vulkan loader don't have an
# unresolved dependency, and confirm which loader actually shipped. Runs
# after cleanup, not before — an earlier version of this ran before the
# Vulkan loader override above and consequently always showed the
# pre-override state, never catching that the override wasn't happening.
# Grep the build log for "ldd:" to find this.
if [ "${PACKAGE_DIAGNOSTICS:-1}" = "1" ]; then
    echo "ldd: citron"
    ldd "${_appdir}/shared/bin/citron" 2>&1 || true
    _vk="$(find "${_appdir}" -iname 'libvulkan.so.1' 2>/dev/null | head -1)"
    if [ -n "${_vk}" ]; then
        echo "shipped Vulkan loader: ${_vk}"
        ldd "${_vk}" 2>&1 || true
    fi
fi

# qt.conf next to the real citron binary in shared/bin/, not shared/lib/.
#
# CPM Qt's libQt6Core.so.6 has a baked-in INSTALL_PREFIX (the CPM cache path)
# that works on the build machine but not anywhere else without an explicit
# qt.conf overriding it. Qt looks for qt.conf next to the running
# executable — sharun relocates the real citron binary to
# AppDir/shared/bin/citron (AppDir/bin/citron is a thin exec wrapper, not
# what Qt sees as its own executable path at runtime), so qt.conf has to live
# there, not next to libQt6Core.so.6. Also clobbers any qt.conf that lib4bin
# staged from the CPM install itself, so no build-machine path can leak in.
mkdir -p ./AppDir/shared/bin
cat > ./AppDir/shared/bin/qt.conf << 'QTCONF_EOF'
[Paths]
Prefix = ..
Plugins = lib
Imports = lib/qt6/qml
Qml2Imports = lib/qt6/qml
Translations = ../usr/share/qt6/translations
QTCONF_EOF

if [ "${DEVEL:-false}" = 'true' ]; then
    sed -i 's|^Name=citron$|Name=citron nightly|' ./AppDir/*.desktop 2>/dev/null || true
fi

# Allow the host's Vulkan ICD to override the bundled loader at runtime.
# Replace-or-append rather than truncate: quick-sharun's own deployment pass
# writes its own entries to this same .env (GTK/Qt renderer settings,
# ANYLINUX_DO_NOT_LOAD_LIBS, etc.) and a truncating write here would discard
# them.
sed -i '/^SHARUN_ALLOW_SYS_VKICD=/d' ./AppDir/.env 2>/dev/null || true
printf 'SHARUN_ALLOW_SYS_VKICD=1\n' >> ./AppDir/.env

# PGO profile data next to the running AppImage on exit ($APPIMAGE is
# exported by sharun's AppRun at runtime). Can't live in .env: that file is
# parsed by a plain key=value dotenv reader and can't execute
# $(dirname "$APPIMAGE") as a command substitution — AppDir/bin/*.hook files
# are sourced as real shell, so it works there instead.
mkdir -p ./AppDir/bin
cat <<-'HOOK_EOF' > ./AppDir/bin/01-llvm-profile.hook
#!/bin/sh
export LLVM_PROFILE_FILE="$(dirname "$APPIMAGE")/default-%p.profraw"
HOOK_EOF

# Build the AppImage
./quick-sharun --make-appimage

# Defensive: appimagetool already produces an executable AppImage (it has to
# be +x to self-mount as an ELF), and mv below preserves that bit as a
# same-filesystem rename. Costs nothing; guards against some future
# release/mirror step dropping the bit silently.
chmod +x ./*.AppImage 2>/dev/null || true

mkdir -p "${OUTPATH}"
# OUTPATH is normally the same directory this script is already running in
# (build-citron-linux.sh sets it that way) — in that case the files are
# already where they need to be and `mv x ./` would fail with a real "same
# file" error. Only move when OUTPATH genuinely resolves elsewhere.
if [ "$(cd . && pwd -P)" != "$(cd "${OUTPATH}" && pwd -P)" ]; then
    mv -v ./*.AppImage "${OUTPATH}/"
    # .AppImage.* sidecars (.zsync etc.) are optional; loop with an existence
    # check since POSIX sh has no nullglob and set -e would otherwise abort
    # the build if none exist.
    for f in ./*.AppImage.*; do
        [ -e "$f" ] || continue
        mv -v "$f" "${OUTPATH}/"
    done
fi

# Pack the portable tar.zst alongside the AppImage from the same fully
# debloated AppDir tree that was just squashed into it (every DEPLOY_*
# suppression and the cleanup block above already ran before
# --make-appimage) — a complete, AppRun-runnable, install-free alternative
# to the AppImage, not a partial copy.
#
# AppDir is removed afterward: it has no further purpose once packed, and
# leaving it in OUTPATH means anything that uploads/copies OUTPATH wholesale
# (a CI artifact step, say) drags along every loose unpacked library a
# second time alongside the AppImage that already contains them compressed.
if [ -d ./AppDir ]; then
    tar -c --zstd -f "${OUTPATH}/${OUTNAME_BASE}.tar.zst" -C ./AppDir .
    rm -rf ./AppDir
fi

# Clean up build-tool byproducts that land in this same directory (OUTPATH),
# not just AppDir: quick-sharun itself (fetched near the top of this script)
# and appinfo (a debug/metadata file appimagetool writes to cwd during
# --make-appimage, not meant for redistribution).
rm -f ./quick-sharun ./appinfo

echo "Artifacts in: ${OUTPATH}"
