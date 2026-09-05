# Building Citron Neo for Linux

`build-citron-linux.sh` builds a native Linux `citron` binary using Clang,
Profile-Guided Optimization (PGO), and Link-Time Optimization (LTO), then
packages the result into a portable AppImage.


|You want...                  |Run                                               |
|---|---|
|A working build, fastest path|`use --pgo none`                                  |
|**Recommended: best performance**|`generate` → `use` → `csgenerate` → `use` (IR + CS-IR PGO)|

BOLT is available as an additional, experimental stage — see 
[Experimental: BOLT](#experimental-bolt). It doesn't reliably improve
performance yet; IR + CS-IR PGO is the best-supported combination.

Every build stage produces a raw binary (`build/<stage>/bin/citron`) and a
portable `.AppImage` \+ `.tar.zst` pair. Pass `\--nopackage` to skip AppImage
packaging and only build the raw binary.

- - -
## Quick Start

```bash
git clone https://github.com/citron-neo/emulator.git
cd emulator

./build-citron-linux.sh setup                          # once per machine
./build-citron-linux.sh generate --pgo ir --lto full    # -> build/generate/bin/citron
```
Run the binary (or AppImage), play a representative mix of games and menus for
5–10 minutes, then exit cleanly (don't kill the process):

```bash
./build/generate/bin/citron
```
Build the optimized binary:

```bash
./build-citron-linux.sh use --pgo ir --lto full
# Binary:   build/use/bin/citron
# AppImage: build/use/AppImage/citron_nightly-*.AppImage
```
For the best result, continue with a second, context-sensitive profiling round:

```bash
./build-citron-linux.sh csgenerate --pgo ir --lto full  # -> build/cs-generate/bin/citron
./build/cs-generate/bin/citron                           # profile again, 5-10 min, exit cleanly
./build-citron-linux.sh use --pgo ir --lto full          # rebuild -- auto-detects the CS profile
```
No PGO round-trip at all:

```bash
./build-citron-linux.sh setup
./build-citron-linux.sh use --pgo none --lto full
# Binary:   build/use-nopgo/bin/citron
# AppImage: build/use-nopgo/AppImage/citron_nightly-*.AppImage
```
- - -
## Requirements

`setup` auto-detects your package manager (`apt`, `pacman`, `dnf`, `yum`, `zypper`
, `emerge`) and installs everything below. Re-running `setup` is always safe.


|Tool                                               |Purpose                                                                                                                   |
|---|---|
|`clang` / `clang++` / `lld` / `llvm-profdata` (v21 default)|Compiler toolchain                                                                                                        |
|`cmake` \+ `ninja`                                 |Build system                                                                                                              |
|`git`                                              |CPM source fetches                                                                                                        |
|`nasm`                                             |FFmpeg assembly optimizations                                                                                             |
|`perl`                                             |OpenSSL `Configure` script                                                                                                |
|`python3` \+ `aqtinstall`                          |Qt binary download (invoked by CMake)                                                                                     |
|`autoconf` \+ `automake` \+ `libtool` \+ `make`    |FFmpeg/libusb autotools builds                                                                                            |
|`glslang` (`glslc`)                                |Vulkan shader compilation                                                                                                 |
|`patchelf`                                         |AppImage bundle RPATH normalization                                                                                       |
|ALSA + PulseAudio dev packages                     |Required for a functional Linux audio backend — without these, SDL3/cubeb compile with no audio output and no build error |
|VAAPI / VDPAU / X11 / XCB dev packages             |Hardware video decode + windowing                                                                                         |
|`libgl-dev` / `libopengl-dev`                      |Required by Qt6's `WrapOpenGL` detection at configure time (citron's renderer is Vulkan-only, but Qt6 still probes for this)|
|`gamemode` (optional)                              |Bundled into the AppImage automatically if present                                                                        |

If your package manager isn't one of the six recognized ones, `setup` prints the
manual package list and continues.

A plain `git clone` (no `\--recurse-submodules`) is all you need — every
dependency, including `libusb` and `dynarmic`, is fetched fresh via CPM at
configure time.

- - -
## Stages

```text
setup → generate → [profile] → use → csgenerate → [profile] → use
```
### `setup`

```bash
./build-citron-linux.sh setup
```
Installs the Requirements table above, then the Clang toolchain and `aqtinstall`.

### `generate` — PGO instrumentation build

```bash
./build-citron-linux.sh generate --pgo ir --lto full
# Output: build/generate/bin/citron
```
Run it, play for 5–10 minutes, exit cleanly. Profile data lands under 
`build/pgo-profiles/`.

### `use` — Optimized build

```bash
./build-citron-linux.sh use --pgo ir --lto full
# Binary:   build/use/bin/citron
# AppImage: build/use/AppImage/citron_nightly-*.AppImage
```
Merges `.profraw` into `.profdata`, rebuilds with `\-fprofile-use` at both
compile and link time. `\--pgo` and `\--lto` must match `generate` (and `
csgenerate`, if used) — the profile is keyed to the specific optimized IR `
generate` produced.

### `csgenerate` — Context-Sensitive PGO (recommended second pass)

```bash
# Requires build/pgo-profiles/default.profdata (produced by 'use' after stage 1)
./build-citron-linux.sh csgenerate --pgo ir --lto full
# Output: build/cs-generate/bin/citron
```
Layers per-call-site counters on top of a binary already optimized with the
stage-1 profile, rather than per-function counters — separate profiles for each
inlined copy of a hot function. Profile it the same way as `generate`, then
re-run `use`; it auto-detects and merges both layers.

`csgenerate` always uses the plain stage-1 `default.profdata` as input, never `
merged.profdata` — using already-merged data would shift inlining decisions the
new CS counters are keyed to. The script enforces this.

### No-PGO baseline

```bash
./build-citron-linux.sh use --pgo none --lto full     # no PGO, LTO only
./build-citron-linux.sh use --pgo none --lto none      # fully unoptimized
```
### `clean`

```bash
./build-citron-linux.sh clean
```
Removes build directories. `build/pgo-profiles/` is preserved.

- - -
## Experimental: `bolt`

```bash
./build-citron-linux.sh bolt --pgo ir --lto full
```
BOLT reorders hot-function layout post-link. It's available but experimental —
it doesn't reliably improve performance and isn't recommended over IR + CS-IR
PGO. Use IR + CS-IR PGO for the best result instead.

- - -
## LTO Modes


|Mode|Flag      |Notes                                                           |
|---|---|---|
|`full`|`\-flto`  |Default. Whole-program IR merged at link time. Best performance.|
|`thin`|`\-flto=thin`|Faster build, parallel ThinLTO, slightly weaker inlining.       |
|`none`|—         |No LTO. Not recommended for release.                            |

`\--lite-lto` = `\--lto thin`. `\--no-lto` = `\--lto none`.

- - -
## PGO Modes


|Mode|Notes                                                                                                                    |
|---|---|
|`ir`|Default and recommended. Counters at optimized-IR level. Supports CS-IR (`csgenerate`). LTO mode must match between stages.|
|`fe`|Frontend PGO — counters before optimization passes. More robust to flag changes between stages. No CS-IR support.        |
|`none`|No PGO.                                                                                                                  |

- - -
## Additional Options


|Option                          |Default|Description                                                           |
|---|---|---|
|`\--build DIR`                  |`./build`|Build root directory                                                  |
|`\--jobs N` / `\-j N`           |`nproc`|Parallel compile jobs                                                 |
|`\--arch x86_64\|v3\|aarch64\|auto`|`auto` |Optimization target. `v3` = x86-64-v3 (AVX2, BMI, FMA — Haswell+/Zen 2+)|
|`\--unity` / `\--no-unity`      |off    |Unity (jumbo) builds — faster compilation, no runtime effect          |
|`\--relwithdebinfo`             |off    |Include debug symbols alongside optimizations                         |
|`\--clang-version N`            |`21`   |Host Clang version                                                    |
|`\--nopackage`                  |off    |Skip AppImage packaging; produce only the raw binary                  |

- - -
## AppImage Packaging

Every stage (unless `\--nopackage`) packages its output via `
AppImageBuilder/package-citron-linux.sh`, alongside a companion `.tar.zst`
archive.

- **Vulkan loader bundled by default** (`DEPLOY_VULKAN=1`) — GPU-vendor ICD
  drivers are never bundled either way, only the loader itself.
- **OpenGL not bundled** (`DEPLOY_OPENGL=0`) — citron has no OpenGL renderer.
- **Qt Multimedia excluded** (`CITRON_USE_QT_MULTIMEDIA=OFF`) — its FFmpeg
  backend adds ~80 MB citron doesn't use.
- Runtime is `uruntime` (fuse2/fuse3/no-fuse fallback) over DWARFS compression.

- - -
## Build Output Structure

```text
build/
├── generate/bin/citron              Stage 1 instrumented binary
├── generate/AppImage/
├── cs-generate/bin/citron           CS-instrumented binary
├── cs-generate/AppImage/
├── use/bin/citron                   Optimized binary (main output)
├── use/AppImage/
├── use-nopgo/bin/citron             No-PGO baseline
├── use-nopgo/AppImage/
├── bolt/citron                      Experimental BOLT output
├── pgo-profiles/
│   ├── default.profdata             Merged stage-1 profile
│   ├── merged.profdata              Merged stage1 + CS profile
│   ├── cs/                          CS profraw files
```
Each `AppImage/` directory also contains a `.tar.zst` and a `.zsync` for delta
updates.

- - -
## Troubleshooting

`error while loading shared libraries: libLLVM-17.so.1: cannot open shared
object file` (running a built AppImage)

Built with `CITRON_USE_LLVM_DEMANGLE` linking against a system LLVM. Rebuild
with the current default (`CITRON_USE_LLVM_DEMANGLE=OFF`), which uses citron's
own statically-linked demangle implementation.

`Required program 'libtoolize' not found`

The `libtool` package is missing. `setup` installs it automatically; on an
unrecognized package manager, install it alongside `autoconf`/`automake`.

`sh: cannot open .../externals/libusb/libusb/bootstrap.sh: No such file`

Not a submodule issue — `libusb` is fetched via CPM regardless of submodule
state. Pull the latest `main`; this was a CMake bug where the CPM-fetched path
got discarded in favor of the (uninitialized) local submodule path.

**AppImage builds fine but has no sound**

Missing ALSA/PulseAudio dev packages at compile time. No build error for this —
check the CMake configure log for `SDL_ALSA` / `SDL_PULSEAUDIO`, both should
read `ON`.

`LTO mismatch`** / `PGO mismatch` running `use` or `csgenerate`**

`\--pgo` and `\--lto` must match `generate` (and `csgenerate`, if used).

`default.profdata not found`** for `csgenerate`**

Run `use` first after collecting stage-1 profraw — it produces the profile `
csgenerate` requires as its base.

**Unrecognized package manager**

`setup` prints the manual package list and continues. Install the equivalents,
then proceed with `generate`/`use` as normal.

