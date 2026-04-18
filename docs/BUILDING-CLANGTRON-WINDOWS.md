# Building Citron Neo for Windows with Clang/LLVM

This document covers building a Windows PE (`citron.exe`) using `build-clangtron-windows.sh` — a multi-stage pipeline that combines Clang cross-compilation with Profile-Guided Optimization (PGO) and Link-Time Optimization (LTO) for a fully optimized release binary.

---

## Quick Start (Recommended: IR PGO + Full LTO)

```bash
# 1. First time only: install toolchain and dependencies
./build-clangtron-windows.sh setup

# 2. Clone citron Neo and its submodules if you haven't already
git clone --recursive https://github.com/citron-neo/emulator.git
cd emulator

# 3. Build the PGO instrumentation binary
./build-clangtron-windows.sh generate --pgo-type ir --lto full

# 4. Copy build/generate/bin/ to a Windows machine and run citron.exe
#    Play games for 15-30 minutes, then exit cleanly (File > Exit or Ctrl+Q)
#    A file named default-<pid>.profraw will appear next to citron.exe
#    Copy that file back to build/pgo-profiles/ on the build machine

# 5. Build the optimized binary
./build-clangtron-windows.sh use --pgo-type ir --lto full
# Output: build/use/bin/citron.exe
```

> **Windows users:** double-click `build-clangtron-windows.bat` to open an MSYS2 CLANG64 shell with help pre-printed, then run stages manually.

---

## Requirements

### Linux (cross-compile, full pipeline)

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
| `aqt` (Python) | Downloads Qt for the Windows target |

### Windows (MSYS2 CLANG64, generate/use stages only)

Install [MSYS2](https://www.msys2.org/) and run `setup` from the CLANG64 terminal:

```bash
./build-clangtron-windows.sh setup
```

`pacman` handles the toolchain. The ELF, BOLT, and Propeller stages require a Linux host and will exit with an error on Windows until COFF/PE BBAddrMap support lands in LLVM (see [RFC](https://discourse.llvm.org/t/rfc-extend-bbaddrmap-support-to-coff-windows/90232)).

---

## Build Strategy

### Why Clang and not MSVC or GCC?

Citron Neo's Windows builds use Clang via [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) — a self-contained toolchain that provides Clang, LLD, libc++, and compiler-rt for MinGW-w64 targets. Using Clang uniformly across both the build host and target means:

- **IR PGO and CS-IRPGO are available.** MSVC PGO operates at the linker level and cannot instrument the same code paths.
- **Full LTO works end-to-end.** LLD handles both the cross-link and the LTO backend in a single pass.
- **No GCC runtime dependencies.** The binary ships `libc++.dll` and `libunwind.dll` instead of `libstdc++-6.dll` / `libgcc_s_seh-1.dll`, avoiding the `libwinpthread-1.dll` TLS race that GCC FFmpeg DLLs trigger.

### Dependency handling

**System dependencies** (Boost, zlib, zstd, fmt, etc.) are built from source by vcpkg using the same llvm-mingw toolchain, with a custom triplet (`x64-mingw-llvm-static`) that forces static linkage against libc++ instead of libstdc++.

**Qt** is downloaded via `aqt` directly into the build tree. The Windows target variant (`win64_llvm_mingw`) is a pre-built Qt that matches the llvm-mingw ABI. The build script fetches `qtmultimedia` and `qtimageformats` alongside the base package because the base aqt install omits them.

**FFmpeg** is re-built from source with llvm-mingw after cmake downloads the GCC variant, to eliminate the `libwinpthread-1.dll` dependency that the upstream GCC FFmpeg DLLs carry.

**Precompiled headers** are disabled globally. IR PGO instruments the PCH itself, causing flag-set mismatches between stages that silently invalidate it. Unity builds already batch translation units more aggressively than PCH does, so there is no compile-time penalty.

---

## Stages

```
setup → generate → [Windows profiling session] → use → [optional: csgenerate → use]
                                                      → [experimental: bolt / propeller]
```

### `setup`

Run once per machine. On Linux: installs apt packages, downloads and builds llvm-bolt from source (not in the LLVM apt repository for current versions), downloads llvm-mingw, and runs shared setup steps. On MSYS2: installs packages via pacman.

```bash
./build-clangtron-windows.sh setup
```

### `generate` — Stage 1: PGO instrumentation build

Compiles citron.exe with PGO counter instrumentation embedded. The binary runs at reduced speed but writes a `.profraw` profile file on clean exit. This file captures which code paths are hot at runtime.

```bash
./build-clangtron-windows.sh generate --pgo-type ir --lto full
# Output: build/generate/bin/citron.exe
```

After building, copy the entire `build/generate/bin/` directory to a Windows machine, run `citron.exe`, play for 15–30 minutes covering a representative mix of games and menus, then exit cleanly. Copy the resulting `default-<pid>.profraw` file (or directory, for IR PGO) back to `build/pgo-profiles/`.

**Important:** exit citron cleanly (File → Exit or Ctrl+Q). Killing the process prevents the profraw from being written.

### `use` — Stage 2: Optimized build

Merges any `.profraw` files in `build/pgo-profiles/` into `default.profdata`, then rebuilds citron.exe with `-fprofile-use` applied at both compile and link time. Full LTO re-runs the optimizer across all bitcode modules at link time with the profile data available, maximizing inlining and branch prediction on hot paths.

```bash
./build-clangtron-windows.sh use --pgo-type ir --lto full
# Output: build/use/bin/citron.exe
```

The `--pgo-type` and `--lto` flags **must match** between `generate` and `use` when using IR PGO. The IR-level profile is keyed to the specific optimized IR produced at generate time; a flag mismatch restructures the IR and causes the entire profile to hash-mismatch and be discarded.

### `csgenerate` — Stage 1b: Context-Sensitive PGO (optional, IR PGO only)

CS-IRPGO adds a second instrumentation layer on top of a binary that is already optimized with the stage 1 profile. It captures per-call-site counters rather than per-function counters, giving the compiler separate profiles for each inlined copy of a hot function.

```bash
# Requires: default.profdata already exists (produced by running `use` after stage 1)
./build-clangtron-windows.sh csgenerate --pgo-type ir --lto full
# Output: build/cs-generate/bin/citron.exe
```

Run this binary on Windows for another 15–30 minutes using the same gameplay as session 1. Copy the resulting `cs-default-<pid>.profraw` files to `build/pgo-profiles/cs/`, then re-run `use`. The `use` stage auto-detects the `cs/` directory and merges both profiles automatically.

**Critical invariant:** `csgenerate` must always use `default.profdata` (stage 1 only) as its `-fprofile-use` input — never `merged.profdata`. Using merged data changes the IR that the CS counters are keyed to, making the resulting profile unloadable in the final `use` build.

### No-PGO baseline build

To produce an unoptimized release binary (useful for comparison or debugging):

```bash
./build-clangtron-windows.sh use --pgo-type none --lto full
# Output: build/use-nopgo/bin/citron.exe

# Fully unoptimized (no PGO, no LTO):
./build-clangtron-windows.sh use --pgo-type none --lto none
```

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
| `none` | — | No PGO. Used for baseline or `build-elf` without profile data. |

---

## Additional Options

| Option | Default | Description |
|---|---|---|
| `--source DIR` | current directory | Path to the citron Neo source tree |
| `--build DIR` | `./build` | Build root directory |
| `--jobs N` | `nproc` | Parallel compile jobs |
| `--unity` | off | Enable unity builds (~30–90% faster compilation, no runtime effect) |
| `--clang-version N` | `21` | Host Clang version (Linux only) |
| `--llvm-mingw-version VER` | `20260224` | llvm-mingw release tag to download (Linux only) |

---

## Experimental: BOLT and Propeller (Linux only)

> **These stages are experimental, require a Linux host, and currently provide little to no measurable performance gain for typical usage. They are documented here for completeness.**

Both stages use a native Linux ELF binary as a profiling proxy for the Windows PE. Because BOLT and Propeller operate on ELF binaries and LLVM does not yet support COFF/PE BBAddrMap (tracking: [RFC](https://discourse.llvm.org/t/rfc-extend-bbaddrmap-support-to-coff-windows/90232)), the layout information is extracted from the ELF and applied to the PE via the linker's `/order:@` flag. This gives function-level reordering but not basic-block layout, and because Full LTO inlines many hot functions into their callers, agreement rates between the ELF profile and the PE are typically 38–64% — meaning a significant portion of the ordering guidance is already lost before it reaches the PE.

### `build-elf` — Stage 2b: Linux ELF for profiling

```bash
./build-clangtron-windows.sh build-elf --pgo-type ir --lto full
# Output: build/use-elf/bin/citron  (Linux ELF, not a Windows binary)
```

This stage is invoked automatically by `bolt` and `propeller` if the ELF is not already present.

### `bolt` — Stage 3A: BOLT function-order optimization

Instruments the Linux ELF with BOLT, profiles it natively, extracts the hot function order from the optimized ELF, and re-links the Windows PE with `/order:@` to place hot functions at the start of `.text`.

```bash
./build-clangtron-windows.sh bolt --pgo-type ir --lto full
# Pauses mid-stage: run the instrumented ELF, play for 15-30 min, press Enter
# Output: build/bolt/bin/citron.exe
```

Requires `llvm-bolt`, built from source by `setup` since it is not in the LLVM apt repository for current versions.

### `propeller` — Stage 3B: Propeller BB+function layout

Collects a branch-stack profile of the Linux ELF via `perf record -b`, converts it to a Propeller layout profile using `generate_propeller_profiles`, and rebuilds the Windows PE with the function ordering applied. Basic-block layout is generated but cannot currently be applied to the PE (ELF-only flag), so only function ordering benefits the final binary.

```bash
./build-clangtron-windows.sh propeller --pgo-type ir --lto full
# Pauses mid-stage: run citron under perf, play for 15-30 min, press Enter
# Output: build/propeller/bin/citron.exe
```

Requires hardware branch-stack support (`perf -b`): AMD Zen 4+ with kernel 6.1+, or Intel with LBR. The setup stage installs `generate_propeller_profiles` from [google/llvm-propeller](https://github.com/google/llvm-propeller).

---

## Build Output Structure

```
build/
├── generate/bin/citron.exe        Stage 1 instrumented binary (run on Windows for profiling)
├── cs-generate/bin/citron.exe     Stage 1b CS-instrumented binary
├── use/bin/citron.exe             Stage 2 optimized binary (main output)
├── use-nopgo/bin/citron.exe       No-PGO baseline binary
├── bolt/bin/citron.exe            BOLT-relinked binary (experimental)
├── propeller/bin/citron.exe       Propeller-relinked binary (experimental)
├── pgo-profiles/
│   ├── default-<pid>.profraw      Copy profraw files here from Windows
│   ├── default.profdata           Merged stage 1 profile (auto-generated)
│   ├── merged.profdata            Merged stage1 + CS profile (auto-generated)
│   └── cs/                        Copy CS profraw files here
├── llvm-mingw/                    Downloaded llvm-mingw toolchain (Linux)
└── generate/externals/qt/         Downloaded Qt for Windows target
```

---

## Troubleshooting

**No `.profraw` file after running the generate binary**

The profile is only written on a clean exit. Exit via File → Exit or Ctrl+Q. Do not kill the process. The generate binary also performs an instrumentation check immediately after build and will warn if profile runtime symbols were stripped.

**`LTO mismatch` error when running `use`**

The `--lto` value must match between `generate` and `use` when using IR PGO. Re-run `generate` with the matching `--lto` flag, or re-run `use` with the flag that `generate` used.

**`default.profdata not found` for csgenerate**

Run `use` first after collecting stage 1 profraw files. The `use` stage merges the profraw files and produces `default.profdata`, which `csgenerate` requires.

**Qt deploy warning on Linux: "Qt plugin base not found"**

This warning comes from `CopyMinGWDeps.cmake` during the cmake build step. The script already handles plugin deployment independently via `deploy_runtime_dlls` after the build completes. Qt plugins including TLS backends are copied from the aqt installation into `bin/` at that point. The warning is cosmetic on a Linux cross-compile host.

**MSYS2: `pacman: command not found`**

Launch the script from the **MSYS2 CLANG64** terminal, not a standard Windows Command Prompt or PowerShell. Use `build-clangtron-windows.bat` to open the correct environment automatically.

**Build fails with `-fuse-ld=bfd` not found**

This flag is GCC-only and should not reach Clang builds. Ensure you are using the upstream `src/citron/CMakeLists.txt` with the fix applied (see commit notes), or update from the repository.
