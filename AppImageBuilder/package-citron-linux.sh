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
#   DEPLOY_VULKAN   Deploy bundled Vulkan/Mesa support (default: 1)
#   DEPLOY_OPENGL   Bundle Mesa OpenGL/EGL/GLX (default: 0)
#   DEPLOY_PIPEWIRE Bundle PipeWire tree (default: 0)
#   DEPLOY_GLIBC    Bundle glibc from Arch container (default: 1)
#   DEPLOY_GTK      Bundle GTK/WebKitGTK libraries and modules (default: 1)
#   CITRON_QT_PATH  CPM Qt6 prefix
#   CITRON_XCB_PATH CPM XCB prefix

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
# Bundle the portable Vulkan loader and open Mesa driver closure.  A runtime
# hook appends only proprietary host NVIDIA ICDs.
export DEPLOY_VULKAN="${DEPLOY_VULKAN:-1}"
export DEPLOY_OPENGL="${DEPLOY_OPENGL:-0}"
export DEPLOY_PIPEWIRE="${DEPLOY_PIPEWIRE:-0}"
# DEPLOY_GLIBC=1: Bundle glibc from Arch container for broad host compatibility.
export DEPLOY_GLIBC="${DEPLOY_GLIBC:-1}"
# WebKitGTK is the default web applet backend, so bundle its GTK stack rather than
# relying on host packages. libqgtk3.so is still purged below: Qt then uses the
# D-Bus XDG-desktop-portal theme path without affecting WebKitGTK itself.
export DEPLOY_GTK="${DEPLOY_GTK:-1}"

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
# systems (Debian/Ubuntu) keep them under /usr/lib/<triplet>/.
# (Note: quick-sharun already handles libpulse via binary-string scanning and
# libwayland-client via DEPLOY_COMMON_LIBS).
EXTRA_LIBS=""

GAMEMODE_LIB="$(ldconfig -p 2>/dev/null | awk '/libgamemode\.so/ {print $NF; exit}')"
[ -n "$GAMEMODE_LIB" ] && EXTRA_LIBS="$EXTRA_LIBS $GAMEMODE_LIB"


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

# ── Post-dep-scan cleanup ────────────────────────────────────────────────────
# Stage ld-linux into AppDir/shared/lib/ for sharun AppRun.lib.
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

# quick-sharun owns the complete Vulkan/Mesa closure.  Do not prune graphics
# drivers or LLVM here: that would create an unsupported partial driver stack.
# libqgtk3.so: Qt GTK3 platform theme. Purged to force Qt to use D-Bus libqxdgdesktopportal.so,
# preventing host GTK module crashes on GTK desktops (Linux Mint, Ubuntu GNOME, XFCE).
find "${_appdir}" -name 'libqgtk3.so'                -delete 2>/dev/null || true
# quick-sharun's bundled Vulkan loader is the only loader. Its ICD handling
# appends compatible host NVIDIA ICDs when proprietary NVIDIA is installed.

# Sanity check: confirm citron's final AppDir dependencies after cleanup.
# Grep the build log for "ldd:" to find this.
if [ "${PACKAGE_DIAGNOSTICS:-1}" = "1" ]; then
    echo "ldd: citron"
    ldd "${_appdir}/shared/bin/citron" 2>&1 || true
fi

# qt.conf next to the real citron binary in shared/bin/, not usr/bin/.
#
# CPM Qt's libQt6Core.so.6 has a baked-in INSTALL_PREFIX (the CPM cache path)
# that works on the build machine but not anywhere else without an explicit
# qt.conf overriding it. Qt looks for qt.conf next to the running executable.
# The AppImage launcher chain is:
#   AppRun (sharun) -> AppDir/usr/bin/citron (citron.sh wrapper)
#                   -> AppDir/usr/bin/citron.bin (symlink or copy by sharun into
#                      AppDir/shared/bin/citron — the actual ELF Qt inspects)
# Qt resolves its own executable path via /proc/self/exe, which points to
# the real ELF in AppDir/shared/bin/. qt.conf must therefore live there.
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
chmod +x ./AppDir/bin/01-llvm-profile.hook

# hwaccel hook: VAAPI, VDPAU, Vulkan video decode.
#
# VAAPI: open Mesa drivers are bundled under AppDir/lib/dri.  Only proprietary
# NVIDIA uses host video drivers; an NVIDIA PCI ID can also mean Mesa NVK.
#
# VDPAU: select bundled radeonsi on AMD systems unless proprietary NVIDIA is
# installed; the latter supplies its own matching host implementation.
#
# Vulkan video: RADV decode extensions are off by default on most Mesa/HW
# combos. Set both RADV_PERFTEST and RADV_EXPERIMENTAL for forward/backward
# compat (unknown names are silently ignored).
cat <<-'HOOK_EOF' > ./AppDir/bin/02-hwaccel.hook
#!/bin/sh
_citron_has_amd=""
_citron_has_intel=""
_citron_has_legacy_intel=""
_citron_has_proprietary_nvidia=""
for _vendor in /sys/class/drm/card*/device/vendor; do
    case "$(cat "${_vendor}" 2>/dev/null)" in
        0x1002) _citron_has_amd=1 ;;
        0x8086)
            _citron_has_intel=1
            # iHD starts at Intel Gen8.  Earlier generations need i965.
            case "$(cat "$(dirname "${_vendor}")/device" 2>/dev/null)" in
                0x00*|0x01[0-6]*|0x04*|0x0a*|0x0c*|0x0d*|0x0f*|0x2[!2]*)
                    _citron_has_legacy_intel=1
                    ;;
            esac
            ;;
        0x10de)
            # NVK/Nouveau also reports NVIDIA's PCI vendor ID.  Treat it as
            # proprietary only when the host exposes NVIDIA's actual driver.
            [ -r /proc/driver/nvidia/version ] && _citron_has_proprietary_nvidia=1
            if [ -z "${_citron_has_proprietary_nvidia}" ]; then
                for _citron_nvidia_dir in \
                    /usr/share/vulkan/icd.d \
                    /etc/vulkan/icd.d \
                    /run/opengl-driver/share/vulkan/icd.d; do
                    for _citron_nvidia_icd in "${_citron_nvidia_dir}"/*nvidia*.json; do
                        [ -r "${_citron_nvidia_icd}" ] || continue
                        _citron_has_proprietary_nvidia=1
                        break 2
                    done
                done
            fi
            ;;
    esac
done

# Use bundled VAAPI only when it has a driver for an installed open GPU.
# Proprietary NVIDIA remains host-side because its video driver must match the
# installed kernel module.  Otherwise let the host-directory fallback choose.
if [ -z "${LIBVA_DRIVERS_PATH:-}" ]; then
    _citron_vaapi_driver=""
    if [ -n "${_citron_has_amd}" ] \
        && [ -n "${APPDIR:-}" ] \
        && [ -e "${APPDIR}/lib/dri/radeonsi_drv_video.so" ]; then
        _citron_vaapi_driver=radeonsi
    elif [ -n "${_citron_has_intel}" ] \
        && [ -n "${_citron_has_legacy_intel}" ] \
        && [ -n "${APPDIR:-}" ] \
        && [ -e "${APPDIR}/lib/dri/i965_drv_video.so" ]; then
        _citron_vaapi_driver=i965
    elif [ -n "${_citron_has_intel}" ] \
        && [ -z "${_citron_has_legacy_intel}" ] \
        && [ -n "${APPDIR:-}" ] \
        && [ -e "${APPDIR}/lib/dri/iHD_drv_video.so" ]; then
        _citron_vaapi_driver=iHD
    fi

    if [ -n "${_citron_vaapi_driver}" ]; then
        export LIBVA_DRIVERS_PATH="${APPDIR}/lib/dri"
        [ -n "${LIBVA_DRIVER_NAME:-}" ] || export LIBVA_DRIVER_NAME="${_citron_vaapi_driver}"
    else
        for _d in /usr/lib64/dri /usr/lib/x86_64-linux-gnu/dri /usr/lib/dri /run/opengl-driver/lib/dri; do
            for _va in "${_d}"/*_drv_video.so; do
                [ -e "${_va}" ] || continue
                export LIBVA_DRIVERS_PATH="${_d}"
                break 2
            done
        done
    fi
fi

if [ -n "${_citron_has_amd}" ]; then
    case ",${RADV_PERFTEST:-}," in
        *,video_decode,*) ;;
        ,,) export RADV_PERFTEST=video_decode ;;
        *) export RADV_PERFTEST="${RADV_PERFTEST},video_decode" ;;
    esac
    case ",${RADV_EXPERIMENTAL:-}," in
        *,video_decode,*) ;;
        ,,) export RADV_EXPERIMENTAL=video_decode ;;
        *) export RADV_EXPERIMENTAL="${RADV_EXPERIMENTAL},video_decode" ;;
    esac
fi

if [ -z "${VDPAU_DRIVER:-}" ] && [ -n "${_citron_has_amd}" ] \
    && [ -z "${_citron_has_proprietary_nvidia}" ] \
    && [ -n "${APPDIR:-}" ] \
    && [ -e "${APPDIR}/lib/vdpau/libvdpau_radeonsi.so" ]; then
    export VDPAU_DRIVER=radeonsi
fi

HOOK_EOF
chmod +x ./AppDir/bin/02-hwaccel.hook

# Force XCB (X11/Xwayland) platform on GNOME desktops.
# GNOME 48 has a Mutter compositor bug where native Wayland Qt windows render
# incorrectly (black/frozen, missing icons). XCB via Xwayland sidesteps this.
# Only applied when GNOME is detected AND Xwayland is running ($DISPLAY set).
# If X11 is unavailable (e.g. GNOME 50+ pure Wayland), explicitly select native
# Wayland. The user can still override QT_QPA_PLATFORM.
# See: https://codeberg.org/pkgforge-dev/Citron-AppImage/issues/50
cat <<-'HOOK_EOF' > ./AppDir/bin/03-gnome-xcb.hook
#!/bin/sh
if [ -z "${QT_QPA_PLATFORM:-}" ]; then
    if [ -n "${WAYLAND_DISPLAY:-}" ] && [ -z "${DISPLAY:-}" ]; then
        export QT_QPA_PLATFORM=wayland
    else
        case "${XDG_CURRENT_DESKTOP:-}" in
            *GNOME*|*gnome*)
                if [ -n "${DISPLAY:-}" ]; then
                    export QT_QPA_PLATFORM=xcb
                fi
                ;;
        esac
    fi
fi
HOOK_EOF
chmod +x ./AppDir/bin/03-gnome-xcb.hook

# WebKitGTK launches its network, GPU, and web-content helper processes from a
# build-time libexec directory (normally /usr/lib/webkit2gtk-4.1).  The AppImage
# bundles those helpers and the injected bundle under AppDir/lib/webkit2gtk-4.1.
cat <<-'HOOK_EOF' > ./AppDir/bin/04-webkitgtk.hook
#!/bin/sh
if [ -n "${APPDIR:-}" ] && [ -d "${APPDIR}/lib/webkit2gtk-4.1" ]; then
    export WEBKIT_EXEC_PATH="${APPDIR}/lib/webkit2gtk-4.1"
    export WEBKIT_INJECTED_BUNDLE_PATH="${APPDIR}/lib/webkit2gtk-4.1/injected-bundle"
fi
HOOK_EOF
chmod +x ./AppDir/bin/04-webkitgtk.hook

# quick-sharun's WebKitGTK deployment creates the upstream Anylinux-sharun bwrap wrapper and
# deploys xdg-dbus-proxy automatically. Verify that supported integration made it into the final
# AppDir rather than duplicating its launcher construction here.
if [ -d "${_appdir}/lib/webkit2gtk-4.1" ]; then
    for _webkit_tool_path in bin/bwrap shared/bin/bwrap \
                                bin/xdg-dbus-proxy shared/bin/xdg-dbus-proxy; do
        if [ ! -x "${_appdir}/${_webkit_tool_path}" ]; then
            echo "Error: quick-sharun did not deploy required WebKitGTK helper ${_webkit_tool_path}" >&2
            exit 1
        fi
    done
fi

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
