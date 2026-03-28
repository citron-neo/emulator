# Building Citron Neo with MinGW (MSYS2)

This guide covers building Citron Neo on Windows using the **MinGW-w64** toolchain
via **MSYS2**, as an alternative to the Visual Studio (MSVC) build.

---

## Quick Start (One-Click)

If you just want to build with no fuss:

1. **Install MSYS2** from <https://www.msys2.org/> (use the default `C:\msys64` path).
2. **Double-click `build-for-mingw.bat`** in the repository root.

That's it. The script will automatically install every dependency, configure CMake,
and compile. Executables appear in `build-mingw/bin/`.

---

## Manual Setup

### 1. Install MSYS2

Download and run the installer from <https://www.msys2.org/>.
After installation, open the **MSYS2 UCRT64** terminal (not MSYS, not MINGW64).

> **Why UCRT64?** It links against the Universal C Runtime shipped with Windows 10+,
> producing binaries that behave most like native Windows apps.

### 2. Install Dependencies

In the UCRT64 terminal, run:

```bash
pacman -Syu

pacman -S --needed \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-toolchain \
    mingw-w64-ucrt-x86_64-boost \
    mingw-w64-ucrt-x86_64-fmt \
    mingw-w64-ucrt-x86_64-nlohmann-json \
    mingw-w64-ucrt-x86_64-opus \
    mingw-w64-ucrt-x86_64-SDL2 \
    mingw-w64-ucrt-x86_64-qt6-base \
    mingw-w64-ucrt-x86_64-qt6-multimedia \
    mingw-w64-ucrt-x86_64-qt6-svg \
    mingw-w64-ucrt-x86_64-qt6-tools \
    mingw-w64-ucrt-x86_64-ffmpeg \
    mingw-w64-ucrt-x86_64-openal \
    mingw-w64-ucrt-x86_64-vulkan-headers \
    mingw-w64-ucrt-x86_64-vulkan-utility-libraries \
    mingw-w64-ucrt-x86_64-vulkan-memory-allocator \
    mingw-w64-ucrt-x86_64-libusb \
    mingw-w64-ucrt-x86_64-enet \
    mingw-w64-ucrt-x86_64-stb
```

When prompted for toolchain members, press Enter to install all.

### 3. Clone and Build

```bash
git clone --recursive https://github.com/YourOrg/Citron-Neo.git
cd Citron-Neo

# Option A — use the automated script:
bash build-for-mingw.sh

# Option B — manual cmake + ninja:
mkdir build-mingw && cd build-mingw

cmake .. -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
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
    -DCITRON_USE_FASTER_LD=OFF

ninja
```

### 4. Run

```bash
./bin/citron.exe       # Qt GUI
./bin/citron-cmd.exe   # Command-line interface
```

All required DLLs and Qt plugins are automatically copied into `bin/` during the build.

---

## Script Reference

| File | Purpose |
|---|---|
| `build-for-mingw.bat` | Windows double-click launcher. Finds MSYS2, opens UCRT64 shell, runs the build script. |
| `build-for-mingw.sh`  | Main build script. Installs deps, configures, compiles. Meant to run inside MSYS2 UCRT64. |

### Script Arguments

`build-for-mingw.sh` accepts optional positional arguments:

```
bash build-for-mingw.sh [BUILD_DIR] [BUILD_TYPE] [JOBS]
```

| Argument | Default | Description |
|---|---|---|
| `BUILD_DIR`  | `build-mingw` | Output directory name |
| `BUILD_TYPE` | `Release`     | CMake build type (`Release`, `Debug`, `RelWithDebInfo`) |
| `JOBS`       | `$(nproc)`    | Parallel compilation jobs |

Examples:

```bash
# Debug build with 4 jobs
bash build-for-mingw.sh build-debug Debug 4

# RelWithDebInfo in default directory
bash build-for-mingw.sh build-mingw RelWithDebInfo
```

---

## CMake Flags Explained

| Flag | Value | Why |
|---|---|---|
| `CITRON_USE_BUNDLED_VCPKG` | `OFF` | vcpkg is MSVC-oriented; we use pacman packages instead |
| `CITRON_USE_BUNDLED_SDL2` | `OFF` | Use the MSYS2-provided SDL2 |
| `CITRON_USE_BUNDLED_QT` | `OFF` | Use the MSYS2-provided Qt6 (CMake may still download a bundled Qt — this is fine) |
| `CITRON_USE_BUNDLED_FFMPEG` | `OFF` | Use the MSYS2-provided FFmpeg |
| `CITRON_USE_EXTERNAL_VULKAN_HEADERS` | `OFF` | Use system Vulkan headers from pacman |
| `CITRON_USE_EXTERNAL_VULKAN_UTILITY_LIBRARIES` | `OFF` | Use system Vulkan utility libs from pacman |
| `ENABLE_WEB_SERVICE` | `OFF` | Optional; avoids pulling in extra HTTP dependencies |
| `CITRON_USE_FASTER_LD` | `OFF` | The faster linker check is for Linux (mold/lld); not needed on MinGW |

---

## Troubleshooting

### `cmake: command not found` / `ninja: command not found`
You're in the wrong MSYS2 shell. Make sure you opened **UCRT64** (not plain MSYS2).
The `build-for-mingw.bat` launcher handles this automatically.

### `error: class-memaccess` on SystemSettings
GCC correctly warns about `std::memset` on non-trivially-copyable types. The codebase
should use `settings = {}` instead. This is already fixed in current sources.

### Missing DLLs when running
The build copies DLLs automatically. If you move `citron.exe` elsewhere, either copy
the entire `bin/` folder or add `C:\msys64\ucrt64\bin` to your PATH.

### Submodule errors
Make sure submodules are initialized:
```bash
git submodule update --init --recursive
```

### Out of memory during compilation
Reduce parallelism:
```bash
ninja -j4    # instead of using all cores
```

---

## Differences from the MSVC Build

| Aspect | MSVC | MinGW |
|---|---|---|
| Dependency management | vcpkg | MSYS2 pacman |
| C runtime | MSVC CRT | Universal CRT (ucrt) |
| Compiler | cl.exe | g++ (GCC 15+) |
| Build generator | Visual Studio / Ninja | Ninja |
| Debug experience | Full VS debugger | GDB via MSYS2 |
| Bundled externals | Most libraries bundled | System packages preferred |
