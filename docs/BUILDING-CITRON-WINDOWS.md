# Building Citron Neo for Windows with Clang

This document covers building a Windows `citron.exe` using `build-clangtron-windows.sh` — a multi-stage pipeline that combines Clang with Profile-Guided Optimization (PGO) and Link-Time Optimization (LTO) for a fully optimized release binary.

The script supports two Clang toolchains, selected with `--compiler`:

- **`clang-cl` (recommended for Windows users)** — a fully native Windows build using Visual Studio's bundled `clang-cl` + `lld-link`. Everything happens on one Windows machine: you build the instrumented binary and run it in place, no copying files between machines. Output matches what Visual Studio's own clang-cl toolset would produce (MSVC ABI, COFF/PDB debug info).
- **`llvm-mingw` (default)** — builds against a self-contained Clang/LLD/libc++ toolchain targeting `x86_64-w64-mingw32`. This can run as a true cross-compile from a Linux host, or from a Windows MSYS2 CLANG64 shell (in which case it's still targeting the MinGW ABI, not MSVC). This is also the only path with the optional BOLT/Propeller stages — experimental, and **not recommended** (see [below](#experimental-bolt-and-propeller-linux-only)).

If you just want a working Windows build and you're sitting at a Windows PC, use **clang-cl** — that's what the rest of this document leads with. `llvm-mingw` is documented afterward for cross-compiling from Linux, or if you specifically want a MinGW-ABI binary.

| You are... | Use |
|---|---|
| On Windows, want the simplest path to a working `citron.exe` | `clang-cl` |
| On Linux, cross-compiling for Windows | `llvm-mingw` |
| On Windows but specifically want a MinGW/libc++-ABI binary | `llvm-mingw` from an MSYS2 CLANG64 shell |

---

## Quick Start — clang-cl (recommended, native Windows)

Run everything below from an **MSYS2 CLANG64** shell (Start Menu → MSYS2 CLANG64). The build script is bash, so it needs that shell even though the actual compile happens via Visual Studio's toolchain.

```bash
# 1. Clone citron Neo
git clone https://github.com/citron-neo/emulator.git
cd emulator

# 2. First time only: install/verify Visual Studio components, MSYS2
#    packages, and native Perl/Python. Installs whatever's missing via
#    winget, so this is safe to re-run.
./build-clangtron-windows.sh setup --compiler clang-cl

# 3. Build the PGO-instrumented binary
./build-clangtron-windows.sh generate --compiler clang-cl --pgo-type ir --lto full
# Output: build/clang-cl/generate/citron.exe
```

Step 3 finishes by printing the exact next steps, including a ready-to-paste PowerShell command. It looks like this (paths will match your own checkout):

```powershell
# In PowerShell, same machine:
$env:LLVM_PROFILE_FILE = 'C:/.../build/pgo-profiles/citron-generate-%p.profraw'
& 'C:/.../build/clang-cl/generate/citron.exe'
# Play games / navigate menus for 15-30 minutes, then exit cleanly
# (File > Exit or Ctrl+Q — do not kill the process)
```

Back in the MSYS2 shell:

```bash
# 4. Build the optimized binary
./build-clangtron-windows.sh use --compiler clang-cl --pgo-type ir --lto full
# Output: build/clang-cl/use/citron.exe   <- this is your final build
```

That's it — no second machine, no manual file transfer. If you skip setting `LLVM_PROFILE_FILE`, the instrumented binary still writes a profile next to itself (`citron-generate-ir-<pid>.profraw`); just move that file into `build/pgo-profiles/` yourself before running `use`.

Want the extra Context-Sensitive IR PGO pass for a bit more performance? See [`csgenerate`](#csgenerate--stage-1b-context-sensitive-pgo-optional-ir-pgo-only) below — it's the same idea as steps 3–4, run twice.

---

## Alternative Quick Start — cross-compiling from Linux (llvm-mingw)

```bash
# 1. Clone citron Neo
git clone https://github.com/citron-neo/emulator.git
cd emulator

# 2. First time only: install toolchain and dependencies
./build-clangtron-windows.sh setup

# 3. Build the PGO instrumentation binary
./build-clangtron-windows.sh generate --pgo-type ir --lto full
# Output: build/generate/bin/citron.exe

# 4. Copy build/generate/bin/ to a Windows machine and run citron.exe.
#    Play games for 15-30 minutes, then exit cleanly (File > Exit or Ctrl+Q).
#    A directory named default-<pid>.profraw/ (containing numbered chunk
#    files, not a single flat file) appears next to citron.exe. Copy the
#    entire directory back to build/pgo-profiles/ on the Linux build machine.

# 5. Build the optimized binary
./build-clangtron-windows.sh use --pgo-type ir --lto full
# Output: build/use/bin/citron.exe
```

> **Building on Windows via MSYS2 instead of Linux?** Same commands, just drop the "copy to a Windows machine" step — you're already there. Run `build/generate/bin/citron.exe` directly, then continue with `use` in the same shell.

---

## Requirements

### `clang-cl` (native Windows)

- Windows 10/11 x64.
- **Visual Studio 2022** with the **Desktop development with C++** workload, plus the **C++ Clang tools for Windows** individual component (provides `clang-cl.exe`, `lld-link.exe`, `llvm-profdata.exe` under `VC/Tools/Llvm/x64/bin`), and the **Windows 11 SDK**.
- **MSYS2**, CLANG64 environment — this is where you run the script from. `setup --compiler clang-cl` installs the CLANG64 packages it needs: `nasm`, `yasm`, `glslang`, `ninja`, `sccache`, `jom` (plus `base-devel`, `git`, `curl`, `wget`).
- **Native (non-MSYS2) Strawberry Perl and Python 3.12** — OpenSSL's and FFmpeg's build systems need a real Win32 Perl/Python, not MSYS2's POSIX-emulated ones. `setup --compiler clang-cl` installs whichever of these (plus CMake and Git) are missing via `winget`.
- **aqtinstall** (pip package), installed by `setup` into that native Windows Python, so CMake's Qt download step can find `aqt.exe`.

All of the above is handled by running this once per machine:

```bash
./build-clangtron-windows.sh setup --compiler clang-cl
```

It verifies every tool at the end and tells you exactly what's still missing if anything failed to install.

### Web Applet Backend

Windows defaults to WebView2 (`CITRON_USE_WEBVIEW2_WEB_ENGINE=ON`), with
QtWebEngine off. To select QtWebEngine instead on the MSVC or clang-cl toolchain,
disable WebView2 explicitly:

```bash
cmake -S . -B build -DCITRON_USE_QT_WEB_ENGINE=ON \
  -DCITRON_USE_WEBVIEW2_WEB_ENGINE=OFF
```

Only one backend may be enabled. Set both flags when switching an existing build
directory because CMake preserves cached option values. QtWebEngine is not
available for the `llvm-mingw` Windows target; use WebView2 there.
The clang-cl build script explicitly selects WebView2 in its generated configure
command; to switch that script, change both backend flags there as shown above.

### `llvm-mingw` — Linux (cross-compile, full pipeline)

The `setup` stage installs all of these automatically:

| Tool | Purpose |
|---|---|
| `clang-21` / `clang++-21` | Host compiler for PGO merge and Linux ELF |
| `lld-21` | Linker for LTO |
| `llvm-profdata-21` | Merges `.profraw` → `.profdata` |
| `llvm-bolt-21` | ELF binary optimization (BOLT stage) |
| `perf` | Linux branch-stack profiling (Propeller stage) |
| `cmake` + `ninja` | Build system |
| `llvm-mingw` | Downloaded automatically: Clang + libc++ + compiler-rt for Windows x86_64 |
| `aqt` (Python) | Downloads Qt for the Windows target (cached in `CPM_SOURCE_CACHE`) |
| `CPM_SOURCE_CACHE` | Environment variable: global cache for all dependencies (default: `~/.cache/cpm`) |

### `llvm-mingw` — Windows (MSYS2 CLANG64)

Install [MSYS2](https://www.msys2.org/) and run `setup` from the CLANG64 terminal:

```bash
./build-clangtron-windows.sh setup
```

`pacman` handles the toolchain. This gives you `generate`, `csgenerate`, and `use` — the same PGO pipeline as Linux. `build-elf`, `bolt`, and `propeller` require a native Linux ELF and will exit with an error on Windows until COFF/PE BBAddrMap support lands in LLVM (see [RFC](https://discourse.llvm.org/t/rfc-extend-bbaddrmap-support-to-coff-windows/90232)).

---

## Build Strategy

### Why Clang, and which Clang?

Both paths use Clang rather than MSVC's `cl.exe` or plain MinGW GCC, because:

- **IR PGO and CS-IRPGO are available.** MSVC's own PGO operates at the linker level and can't instrument the same code paths.
- **Full LTO works end-to-end.** LLD handles the LTO backend in a single pass, on both paths.

Where they differ is ABI and where they run:

| | `clang-cl` | `llvm-mingw` |
|---|---|---|
| Host | Native Windows only | Linux (cross-compile) or Windows/MSYS2 |
| ABI / runtime | MSVC ABI, COFF/PDB debug info, MSVC runtime | libc++ / compiler-rt, MinGW-w64 ABI, ships `libc++.dll`/`libunwind.dll` |
| IR PGO, CS-IRPGO, full LTO | Yes | Yes |
| BOLT / Propeller | Not available | Available, Linux host only |
| Needs Visual Studio | Yes | No |

Pick `clang-cl` if you want a binary that behaves and debugs like something Visual Studio itself produced. Pick `llvm-mingw` if you're on Linux, don't want to install Visual Studio, or need BOLT/Propeller.

### Dependency handling

**System dependencies** (Boost, zlib, zstd, fmt, etc.) are managed by **CPM (CMake Package Manager)**, built from source with the active toolchain, and cached globally in `CPM_SOURCE_CACHE` to speed up builds across repository clones — this applies to both paths.

**Qt** is downloaded via `aqt` directly into the build tree. On the `llvm-mingw` path the target variant is `win64_llvm_mingw`, matching the llvm-mingw ABI; the script also fetches `qtmultimedia`, `qtimageformats`, and `qtsvg` alongside the base package. On the `clang-cl` path, Qt's prebuilt `msvc2022_64` variant is used instead, matching the MSVC ABI.

**FFmpeg** is built from source rather than using the upstream GCC-built DLLs, to avoid the `libwinpthread-1.dll` dependency and TLS issues those carry. On `llvm-mingw` this happens via a dedicated script-level rebuild stage; both source and pre-built binaries are cached under `CPM_SOURCE_CACHE`. On `clang-cl`, FFmpeg and OpenSSL are cached per `(LTO mode, PGO mode, stage)` combination rather than shared across all three stages — the first time you build a given combination, expect FFmpeg to take tens of minutes and OpenSSL a few minutes; repeating the same combination hits cache.

**Precompiled headers** are disabled globally. IR PGO instruments the PCH itself, causing flag-set mismatches between stages that silently invalidate it. Unity builds already batch translation units more aggressively than PCH does, so there's no compile-time penalty.

---

## Stages

```text
setup → generate → [profiling session] → use → [optional: csgenerate → use]
                                              → [llvm-mingw + Linux host only: bolt / propeller]
```

Which stages are available depends on `--compiler`:

- `--compiler clang-cl` supports **`setup`, `generate`, `csgenerate`, `use`** only.
- `--compiler llvm-mingw` (default) supports all stages, but `build-elf`/`bolt`/`propeller` additionally require a Linux host.

### `setup`

Run once per machine.

```bash
./build-clangtron-windows.sh setup                       # llvm-mingw path (default)
./build-clangtron-windows.sh setup --compiler clang-cl    # clang-cl path
```

On Linux this installs apt packages, downloads/builds `llvm-bolt` from source (not in the LLVM apt repository for current versions), and downloads llvm-mingw. On MSYS2 without `--compiler clang-cl`, it installs the llvm-mingw-equivalent toolchain via `pacman`. With `--compiler clang-cl`, it instead installs the MSYS2/winget/aqtinstall prerequisites described above and verifies your Visual Studio clang-cl install.

### `generate` — Stage 1: PGO instrumentation build

Compiles `citron.exe` with PGO counters embedded. The binary runs slower but writes a `.profraw` profile on clean exit, capturing which code paths are hot at runtime.

**clang-cl:**

```bash
./build-clangtron-windows.sh generate --compiler clang-cl --pgo-type ir --lto full
# Output: build/clang-cl/generate/citron.exe
```

The command finishes by printing a ready-to-run PowerShell snippet that sets `LLVM_PROFILE_FILE` to a path under `build/pgo-profiles/` and launches the binary — copy-paste it, no manual path bookkeeping needed. Play for 15–30 minutes covering a representative mix of games and menus, then exit cleanly (File → Exit or Ctrl+Q). If you don't set `LLVM_PROFILE_FILE`, the binary still writes `citron-generate-ir-<pid>.profraw` next to itself; move that into `build/pgo-profiles/` before continuing.

**llvm-mingw:**

```bash
./build-clangtron-windows.sh generate --pgo-type ir --lto full
# Output: build/generate/bin/citron.exe
```

On a Linux host, copy the entire `build/generate/bin/` directory to a Windows machine, run `citron.exe`, play 15–30 minutes, exit cleanly, then copy the resulting `default-<pid>.profraw` (a *directory* for IR PGO, containing numbered chunk files — copy the whole thing) back to `build/pgo-profiles/` on the Linux machine. On MSYS2/Windows, just run `build/generate/bin/citron.exe` directly and copy the profraw locally — no second machine involved.

**Important either way:** exit citron cleanly. Killing the process prevents the profraw from being written.

### `use` — Stage 2: Optimized build

Merges the collected `.profraw` files into a profile, then rebuilds `citron.exe` with `-fprofile-use` applied at both compile and link time. Full LTO re-runs the optimizer across all bitcode modules at link time with the profile available, maximizing inlining and branch prediction on hot paths.

```bash
./build-clangtron-windows.sh use --compiler clang-cl --pgo-type ir --lto full
# Output: build/clang-cl/use/citron.exe

./build-clangtron-windows.sh use --pgo-type ir --lto full   # llvm-mingw
# Output: build/use/bin/citron.exe
```

The `--pgo-type` and `--lto` flags **must match** between `generate` and `use` (and `csgenerate`, if used) when using IR PGO — the IR-level profile is keyed to the specific optimized IR produced at generate time, and a flag mismatch causes the whole profile to hash-mismatch and be discarded. Both paths write a sentinel file after a successful `generate` (`.citron-clangcl-gen-config` for clang-cl, `.citron-gen-config` for llvm-mingw) and refuse to run `use`/`csgenerate` with mismatched flags rather than silently producing a bad build. Because the sentinels and profile filenames differ between the two paths, you can safely point `--build` at the same directory for both without them colliding.

On `clang-cl`, `ir`/`fe`/`none` mean the same thing as on `llvm-mingw`; internally they're passed to `clang-cl.exe` at compile time as `/clang:-fprofile-generate=...`-style flags rather than bare `-f` flags, plus explicit native `/INCLUDE:` flags passed directly to `lld-link.exe` at link time to stop `/OPT:REF` from stripping the profiling runtime — this is all transparent to you as a script user.

### `csgenerate` — Stage 1b: Context-Sensitive PGO (optional, IR PGO only)

CS-IRPGO adds a second instrumentation layer on top of a binary already optimized with the stage 1 profile. It captures per-call-site counters rather than per-function counters, giving the compiler separate profiles for each inlined copy of a hot function.

```bash
# Requires: default.profdata / clang-cl-ir.profdata already exists
# (produced by running `use` after stage 1)
./build-clangtron-windows.sh csgenerate --compiler clang-cl --pgo-type ir --lto full
# Output: build/clang-cl/csgenerate/citron.exe

./build-clangtron-windows.sh csgenerate --pgo-type ir --lto full   # llvm-mingw
# Output: build/cs-generate/bin/citron.exe
```

(Note the folder name mirrors the stage name exactly on each path — `csgenerate` for clang-cl, `cs-generate` for llvm-mingw.)

Run this binary the same way as stage 1 (PowerShell + `LLVM_PROFILE_FILE` for clang-cl; directly or copied to Windows for llvm-mingw) for another 15–30 minutes of the same kind of gameplay. Copy the resulting `cs-*.profraw` files into `build/pgo-profiles/cs/`, then re-run `use`. The `use` stage auto-detects that directory and merges both profiles automatically.

**Critical invariant:** `csgenerate` must always use the plain stage-1 profile (never a previously-merged CS profile) as its `-fprofile-use` input. Using merged data changes the IR the new CS counters are keyed to, making the resulting profile unloadable by the following `use` build. The script enforces this and will error out rather than silently produce a bad profile.

### No-PGO baseline build

To produce an unoptimized-by-PGO release binary (useful for comparison or debugging):

```bash
./build-clangtron-windows.sh use --compiler clang-cl --pgo-type none --lto full
# Output: build/clang-cl/use/citron.exe (overwrites any PGO build in the same --build dir —
# use a separate --build path if you want to keep both)

./build-clangtron-windows.sh use --pgo-type none --lto full   # llvm-mingw
# Output: build/use-nopgo/bin/citron.exe

# Fully unoptimized (no PGO, no LTO), llvm-mingw:
./build-clangtron-windows.sh use --pgo-type none --lto none
```

`--pgo-type none` is only valid on the `use` stage for `clang-cl` (there's no baseline `generate`/`csgenerate` to skip).

---

## LTO Modes

| Mode | Flag | Build time | Runtime perf | Notes |
|---|---|---|---|---|
| `full` | `-flto` | Slowest | Best | Default. Whole-program IR merged at link time. |
| `thin` | `-flto=thin` | Faster | Good | Parallel ThinLTO. Slightly weaker inlining. |
| `none` | — | Fastest | Baseline | Not recommended for release. |

`--lite-lto` is an alias for `--lto thin`. `--no-lto` is an alias for `--lto none`.

---

## PGO Modes

| Mode | Flag set | Notes |
|---|---|---|
| `ir` | `-fprofile-generate` / `-fprofile-use` | Default. Counters at optimized-IR level. Most accurate for inlining. CS-IRPGO available. LTO mode must match between stages. |
| `fe` | `-fprofile-instr-generate` / `-fprofile-instr-use` | Frontend PGO. Counters before optimization passes. More robust to flag changes between stages. CS-IRPGO not available. |
| `none` | — | No PGO. Used for the baseline build, or `build-elf` without profile data. |

These apply identically on both `--compiler` paths; only the underlying flag syntax passed to the compiler differs (see the `use` stage notes above).

---

## Additional Options

| Option | Default | Description |
|---|---|---|
| `--compiler llvm-mingw\|clang-cl` | `llvm-mingw` | Toolchain to use. `clang-cl` requires a native Windows/MSYS2 host and supports only `generate`, `csgenerate`, and `use`. |
| `--source DIR` | current directory | Path to the citron Neo source tree |
| `--build DIR` | `./build` | Build root directory |
| `--jobs N` | `nproc` | Parallel compile jobs |
| `--unity` | off | Enable unity builds (~30–90% faster compilation, no runtime effect) |
| `--relwithdebinfo` | off | Enable RelWithDebInfo build (Release with debug symbols). Injects `-g`/`/Z7` while keeping O3/LTO/PGO. |
| `--clang-version N` | `21` | Host Clang version (`llvm-mingw` path, Linux only) |
| `--llvm-mingw-version VER` | `20260224` | llvm-mingw release tag to download (`llvm-mingw` path, Linux only) |

---

## Experimental: BOLT and Propeller (Linux only)

> **These stages are experimental, require a Linux host with the `llvm-mingw` path, and currently provide little to no measurable performance gain for typical usage. They are documented here for completeness. Not available with `--compiler clang-cl`.**

Both stages use a native Linux ELF binary as a profiling proxy for the Windows PE, since BOLT and Propeller operate on ELF binaries and LLVM does not yet support COFF/PE BBAddrMap (tracking: [RFC](https://discourse.llvm.org/t/rfc-extend-bbaddrmap-support-to-coff-windows/90232)). Layout information is extracted from the ELF and applied to the PE via the linker's `/order:@` flag — function-level reordering only, not basic-block layout. Because Full LTO inlines many hot functions into their callers, agreement rates between the ELF profile and the PE are typically 38–64%, meaning a significant portion of the ordering guidance never reaches the PE.

If you built with `clang-cl` and still want a BOLT/Propeller-optimized binary, run the `clang-cl use` stage for your PGO+LTO profile data, then feed that same `pgo-profiles/` directory into the `llvm-mingw` path's `bolt`/`propeller` stages on a Linux host — both paths use a compatible profile format.

### `build-elf` — Stage 2b: Linux ELF for profiling

```bash
./build-clangtron-windows.sh build-elf --pgo-type ir --lto full
# Output: build/use-elf/bin/citron  (Linux ELF, not a Windows binary)
```

Invoked automatically by `bolt` and `propeller` if the ELF isn't already present.

### `bolt` — Stage 3A: BOLT function-order optimization

Instruments the Linux ELF with BOLT, profiles it natively, extracts the hot function order, and re-links the Windows PE with `/order:@` to place hot functions at the start of `.text`.

```bash
./build-clangtron-windows.sh bolt --pgo-type ir --lto full
# Pauses mid-stage: run the instrumented ELF, play for 15-30 min, press Enter
# Output: build/bolt/bin/citron.exe
```

Requires `llvm-bolt`, built from source by `setup` since it's not in the LLVM apt repository for current versions.

### `propeller` — Stage 3B: Propeller BB+function layout

Collects a branch-stack profile of the Linux ELF via `perf record -b`, converts it to a Propeller layout profile with `generate_propeller_profiles`, and rebuilds the Windows PE with the function ordering applied. Basic-block layout is generated but can't currently be applied to the PE (ELF-only flag), so only function ordering benefits the final binary.

```bash
./build-clangtron-windows.sh propeller --pgo-type ir --lto full
# Pauses mid-stage: run citron under perf, play for 15-30 min, press Enter
# Output: build/propeller/bin/citron.exe
```

Requires hardware branch-stack support (`perf -b`): AMD Zen 4+ with kernel 6.1+, or Intel with LBR. `setup` installs `generate_propeller_profiles` from [google/llvm-propeller](https://github.com/google/llvm-propeller).

---

## Build Output Structure

```text
build/
├── clang-cl/
│   ├── generate/citron.exe        Stage 1 instrumented binary (clang-cl)
│   ├── csgenerate/citron.exe      Stage 1b CS-instrumented binary (clang-cl)
│   ├── use/citron.exe             Stage 2 optimized binary (clang-cl, main output)
│   └── .work/<stage>/             Internal CMake/ninja build trees, not for direct use
├── generate/bin/citron.exe        Stage 1 instrumented binary (llvm-mingw)
├── cs-generate/bin/citron.exe     Stage 1b CS-instrumented binary (llvm-mingw)
├── use/bin/citron.exe             Stage 2 optimized binary (llvm-mingw, main output)
├── use-nopgo/bin/citron.exe       No-PGO baseline binary (llvm-mingw)
├── bolt/bin/citron.exe            BOLT-relinked binary (experimental, llvm-mingw)
├── propeller/bin/citron.exe       Propeller-relinked binary (experimental, llvm-mingw)
├── pgo-profiles/                  Shared between both --compiler paths
│   ├── default-<pid>.profraw/     llvm-mingw: copy the whole directory here from Windows
│   ├── default.profdata           llvm-mingw: merged stage 1 profile (auto-generated)
│   ├── merged.profdata            llvm-mingw: merged stage1 + CS profile (auto-generated)
│   ├── clang-cl-ir.profdata       clang-cl: merged stage 1 profile (auto-generated)
│   ├── clang-cl-merged.profdata   clang-cl: merged stage1 + CS profile (auto-generated)
│   └── cs/                        Copy CS profraw directories here (both paths)
├── llvm-mingw/                    Downloaded llvm-mingw toolchain (Linux only)
└── generate/externals/qt/         Downloaded Qt for Windows target
```

Each `citron.exe` output directory also contains its runtime DLLs, Qt plugins, and (for RelWithDebInfo builds) `.pdb` files, plus an empty `user/` folder for a portable profile.

`clang-cl` also grows `CPM_SOURCE_CACHE` (default `~/.cache/cpm`, separate from `build/`) with a separate FFmpeg and OpenSSL install per `--lto`/`--pgo-type`/stage combination you've built; it's safe to delete old ones there if disk space is a concern, they'll just rebuild next time they're needed.

---

## Troubleshooting

**No `.profraw` file after running the generate binary**

The profile is only written on a clean exit — use File → Exit or Ctrl+Q, don't kill the process. On the `llvm-mingw` path, the generate/csgenerate binary also runs a self-check immediately after build and warns if profile runtime symbols were stripped. The `clang-cl` path doesn't run that same self-check, but ships explicit `/INCLUDE:` linker flags specifically to stop this failure mode, and bakes a foolproof default filename (`citron-generate-<mode>-<pid>.profraw` / `citron-csgenerate-<mode>-<pid>.profraw`) directly next to the exe so a file always appears even without `LLVM_PROFILE_FILE` set. If you still see nothing at all, double-check `LLVM_PROFILE_FILE` (if you did set it) points to a writable path.

**`LTO mismatch` / `PGO mismatch` error when running `use` or `csgenerate`**

The `--lto` and `--pgo-type` values must match `generate` on both paths for IR PGO. Re-run `generate` with the matching flags, or re-run the later stage with the flags `generate` used.

**`default.profdata not found` for csgenerate**

Run `use` first after collecting stage 1 profraw files — it produces the merged stage-1 profile (`default.profdata` on llvm-mingw, `clang-cl-ir.profdata` on clang-cl) that `csgenerate` requires.

**`clang-cl requires a native Windows host`**

The `--compiler clang-cl` path can't run on Linux. Use the default `--compiler llvm-mingw` path there instead.

**`Visual Studio clang-cl component missing`**

Open the Visual Studio Installer and add **Desktop development with C++**, the **C++ Clang tools for Windows** individual component, and the **Windows 11 SDK**, then re-run `setup --compiler clang-cl`.

**`Native Win32 Perl required` / `Native Windows Python 3.12 required`**

OpenSSL's and FFmpeg's build systems need a real Win32 Perl/Python, not MSYS2's. Re-run `setup --compiler clang-cl` to install Strawberry Perl / Python 3.12 via winget, or point `PERL_EXECUTABLE` / `PYTHON_EXECUTABLE` at existing native installs.

**`winget.exe not found`**

Install "App Installer" from the Microsoft Store, then re-run `setup --compiler clang-cl`.

**`sccache.exe missing` warning**

Optional — install with `pacman -S mingw-w64-clang-x86_64-sccache` from the MSYS2 CLANG64 shell for faster incremental rebuilds. The build still works without it.

**MSYS2: `pacman: command not found`**

Launch the script from the **MSYS2 CLANG64** terminal, not a standard Windows Command Prompt or PowerShell.
