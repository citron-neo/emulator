# Dependency providers

This audit applies to the Android CPM PR (`codex/android-oboe-cpm`) and its
`codex/dynarmic-latest-android-test` validation branch. It does not change the
personal `main` branch or upgrade all dependencies to a common version.

## Contract

Consumers link canonical targets. Provider discovery, source population, path
adapters and aliases belong in the root CMake/provider modules and `externals`.
`DependencyTargets.cmake` normalizes Opus, Sirit, Adrenotools and FFmpeg. Oboe's vcpkg
layout stays in `OboeVcpkg.cmake`; audio consumers use `oboe::oboe`.

Submodule fallbacks remain useful for non-CPM builds. They must check for the
canonical target before adding sources. Sirit accepts a parent-provided
`SPIRV-Headers::SPIRV-Headers` target instead of searching a second installation
or using its nested submodule. This also removes the clang-cl-specific alternate
SPIRV-Headers source.

FFmpeg consumers now link `FFmpeg::FFmpeg`. Legacy bundled library paths,
include directories and linker options are confined to its adapter. Linux VA-API
uses `PkgConfig::LIBVA` so its include and link requirements travel together.

## Version matrix (verified 2026-10-02)

The vcpkg column comes from manifest baseline
`c3173be258001c60814b6adf161322b6eb3688ee`, with manifest overrides applied.
System-provider versions depend on the build host and must be recorded in CI.

| Dependency | CPM selection | vcpkg selection | Bundled/submodule selection | Decision |
| --- | --- | --- | --- | --- |
| OpenSSL | 3.6.1 | 3.6.1, port 2 | No active OpenSSL submodule | Already aligned; older 3.4.1 notes are not the current baseline |
| Boost | 1.87.0 | 1.90.0 (context port 1) | System/vcpkg for non-CPM | Significant drift; keep separate until desktop validation |
| fmt | `e8244777ee1c32df8233c215ac9ff626b2dd2c38` | 12.1.0 override | System/vcpkg | Keep exact CPM pin; do not assume a commit is a release tag |
| lz4 | 1.10.0 | 1.10.0 | System/vcpkg | Aligned |
| zstd | 1.5.6 | 1.5.7 | System/vcpkg | Small drift; no bulk upgrade |
| Opus | 1.5.2 | 1.5.2, port 1 | `101a71e03bbf860aaafb7090a0e440675cb27660` (post-1.4) | Bundled drift merits its own audio regression change |
| Oboe | `a81bb9f87d4105b84b682685d3bfbb5beca371d1` | 1.10.0 | Android provider adapter | Compare API/version before changing pin |
| Cubeb | `48689ae7a73caeb747953f9ed664dc71d2f918d8` | Not in manifest | Same commit | Aligned |
| Dynarmic | `b1440b456b80f3dde0c01665932d114c4961ee93` | Not in manifest | Same commit | Keep tested Android baseline |
| Sirit | `ab75463999f4f3291976b079d42d52ee91eebf3f` | Not in manifest | Same commit | Target reuse patch applies to both source providers |
| SPIRV-Headers | `vulkan-sdk-1.4.304.1` | Not in manifest (baseline port is 1.4.341.0) | `00898b201b4153d7198c3e0134dbf953c83bbfd7` | Provider versions still differ; explicit, not silently unified |
| Sirit nested SPIRV-Headers | Previously used by clang-cl | Not applicable | `c214f6f2d1a7253bb0e9f195c2dc5b0659dc99ef` | No longer selected when parent supplies a target |
| Vulkan-Headers / Utility-Libraries | 1.4.337 | Not in manifest | Separate source pins | Not assumed to share SPIRV-Headers release numbering |

### Transitive pins and duplicate-version risks

Different versions in separate provider builds are drift, not proof that two
versions enter one graph. The risks below distinguish those cases. Evidence is
the checked-in CPM declarations, manifest baseline/port recipes, root gitlinks,
and dependency CMake files at the listed commits; installed system versions are
unknown until a build records them. No dependency versions were changed.

| Dependency edge | CPM / vcpkg selection | Source/transitive selection | Same-graph risk and resolution |
| --- | --- | --- | --- |
| Citron → Boost; Dynarmic → Boost | CPM 1.87.0; vcpkg headers/context 1.90.0#1 | Dynarmic calls `find_package(Boost 1.57 REQUIRED)`; it does not pin a private Boost source | **Possible mixed discovery**, not confirmed duplication: a partial preexisting Boost provider can supply headers while discovery supplies compiled components elsewhere. Check `Boost::headers` and `Boost::context` usage paths together; do not mix their include/library installations. Cross-provider drift remains intentional. |
| Citron → Opus | CPM 1.5.2; vcpkg 1.5.2#1 | Bundled `101a71e03bbf860aaafb7090a0e440675cb27660` | **Former duplicate-source risk:** pkg-config-only discovery could miss the vcpkg CMake export and add old bundled Opus. Config-first discovery now reuses `Opus::opus`; a raw `opus` target also blocks a second source build and is aliased centrally. Missing target plus missing source fails explicitly. |
| FFmpeg → Opus | CPM FFmpeg `n8.0`; vcpkg FFmpeg 8.0.1#2 | FFmpeg submodule `c1b19ee69f2142bb4b098936cc7421d80b3db7e4` | **Conditional risk:** vcpkg's optional `opus` feature depends on the same vcpkg Opus version. This manifest requests only avcodec/avfilter/swscale, with default features off; the bundled wrapper does not enable libopus. No second external Opus is implied by FFmpeg's native codecs. Reaudit if libopus is enabled later. |
| Citron → SPIRV-Headers; Sirit → SPIRV-Headers | CPM SDK tag 1.4.304.1 resolves to `3f17b2af6784bfa2c5aa5dbb8e0e74a607dd8b3b`; vcpkg baseline 1.4.341.0 is not requested | Root `00898b201b4153d7198c3e0134dbf953c83bbfd7`; Sirit nested `c214f6f2d1a7253bb0e9f195c2dc5b0659dc99ef` | **Prevented duplicate-tree risk:** Sirit must receive the parent's raw or canonical SPIRV-Headers target. Its nested gitlink remains on disk but is not configured. Both CPM and bundled routes fail before Sirit if that parent target is absent. |
| Citron → Sirit | CPM and bundled `ab75463999f4f3291976b079d42d52ee91eebf3f`; no vcpkg manifest entry | Transitive headers as above | A preexisting `sirit::sirit` or raw `sirit` target takes priority; no second Sirit source is added. An externally built imported Sirit already owns its dependency graph. |
| Dynarmic → xbyak | CPM, root gitlink and Dynarmic nested gitlink all `c506ecd5134122115a981fdd45c2a756f9ce20ac` | Dynarmic nested source used only without `xbyak::xbyak` | Pins aligned; parent target blocks the nested build. |
| Dynarmic → oaknut | CPM, root and nested all `94c726ce0338b054eb8cb5ea91de8fe6c19f4392` | ARM64 only unless Dynarmic tests are enabled | Pins aligned; parent `merry::oaknut` blocks nested build. |
| Dynarmic → unordered_dense | CPM, root and nested all `7b55cab8418da1603496462ce3ccdb4cb1dc3368` | Nested fallback checks `unordered_dense::unordered_dense` | Pins aligned; parent target blocks nested build. |
| Dynarmic → Catch2 | CPM and nested `675f9eaeb191c51b9d2ffb2bb198009533895051`; vcpkg override 3.3.1 | Nested dependency is gated by Dynarmic tests | Dynarmic tests are off in this integration; no nested Catch2 is configured. |

### Remaining provider paths

The scan includes `VCPKG_INSTALLED_DIR`, archive paths, `externals/` paths and
`add_subdirectory` in CMake provider modules and consumers. Remaining literal
paths have specific owners:

- `OboeVcpkg.cmake` adapts a port without a CMake export. The path is confined
  to the provider; consumers link `oboe::oboe`.
- FFmpeg's autotools provider must name its output archives. Consumers link
  `FFmpeg::FFmpeg`; archive paths are not spread into source targets.
- `wininet.lib` and `version.lib` are Windows SDK libraries, not alternate
  external dependency providers.
- MoltenVK frameworks and FidelityFX shader source inputs are platform/build
  resources, not interchangeable library targets. They remain separate work.
- Project vendored sources such as glad and tz legitimately use
  `add_subdirectory`. External sources use target guards; the required Opus
  fallback now diagnoses absent source rather than silently continuing.

## External patch audit (2026-10-02)

| Patch | Pinned/upstream evidence | Decision |
| --- | --- | --- |
| Dynarmic tuple/pair hash | Upstream HEAD is still [`b1440b45`](https://github.com/xinitrcn1/dynarmic/tree/b1440b456b80f3dde0c01665932d114c4961ee93); x64 containers still use default hash with tuple keys | Keep explicit standalone hasher; no Citron header or `std::hash` specialization. |
| Dynarmic `<print>` include | The pinned ARM64 address-space file uses printing without the direct include | Keep the one-line include. |
| Dynarmic RegList formatter removal | The pinned formatter specializes the `u16` alias and collides with integer formatting | Keep; no fmt usage changes in this audit. |
| Sirit target reuse | Pinned `ab754639` and current upstream [`4ab79a8c`](https://github.com/yuzu-mirror/sirit/tree/4ab79a8c023aa63caaa93848b09b9fe8b183b1a9) still unconditionally find SPIRV-Headers in system mode | Keep the small target-first patch for both source providers. |
| Legacy MCL Clang patch | Current Dynarmic's external gitlinks and CMake no longer contain MCL | Delete the patch and obsolete root hook; no replacement needed. |
| Adrenotools runtime page-size / Opus clang-cl SSE4 / stb overflow patches | Separate platform or source compatibility fixes; not part of the removed MCL dependency | Retain; this audit does not establish that newer upstream versions supersede them. |

Dynarmic and Sirit patch application accepts clean pinned sources and an
already-applied patch, but rejects mismatched source drift. A future pin bump
must check each hunk rather than treating a failed patch as success.

## Android branches

Keep Android audio backends, ARM64 driver hooks, NDK/API-specific OpenSSL
configuration, system library linking and the missing `wordexp.h` exclusion for
Boost.Process. These are platform/toolchain requirements, not provider discovery.
No Android condition was removed merely because the CPM build succeeded.

## Dynarmic boundary

The compatibility patch still fixes missing `<print>` and tuple-key hashing in
Dynarmic, but no longer includes Citron's `common/container_hash.h`. A standalone
`Dynarmic::TupleHash` is passed explicitly to the x64 tuple maps and marker sets.
Other containers retain their existing default hash behavior. This removes the
host-header coupling without inventing `std::hash` specializations for tuples
containing only standard/built-in types.

The patch also removes the string formatter for `A32::RegList`, which aliases
`u16` and therefore replaces the standard integer formatter. That collision
breaks hexadecimal IR dump formatting with GCC/libstdc++. Register-list text
remains available through `RegListToString`; JIT execution is unchanged.

This is not a claim that upstream Dynarmic requires no patches. The remaining
self-contained source changes can be proposed upstream independently.

## Validation and remaining boundaries

Configure the small adapter fixture with `CANONICAL_PROVIDER=ON` and `OFF` to
check canonical provider priority, idempotence and FFmpeg usage requirements:

```sh
cmake -S CMakeModules/tests/provider_contract -B build-contract-raw
cmake -S CMakeModules/tests/provider_contract -B build-contract-canonical -DCANONICAL_PROVIDER=ON
cmake -DCHECK_BINARY_DIR=/absolute/path/to/provider-checks -P CMakeModules/tests/CheckProviderResolution.cmake
```

The resolution checks use the actual Opus/Sirit fallback blocks, not copies of
their implementation. They cover canonical/raw target priority, bundled Opus,
missing sources/parent headers, config-only Opus and absent pkg-config. These
are configure-only checks; they do not claim an APK or desktop rebuild.

The separate CI repository has manual fallback/provider compatibility tests:
Windows/MSVC + vcpkg/submodules and Linux + system/submodules. They build the SDL
CLI and tests without Qt and do not publish releases. The Windows workflow is
**fallback/provider compatibility test only, not the supported production
Windows build path**. The production Windows path is clang-cl/Clangtron; this
test does not restore MSVC/vcpkg as the main Windows CI or establish support for
shipping that configuration.

Clangtron/Linux packaging scripts use CPM and provide separate coverage.
Compatibility test success, production build validation and game/runtime
correctness must be reported separately.

Remaining work includes packaged Qt frontends, iOS framework paths, local
FidelityFX shader-generation inputs, default-provider version drift and real
desktop CPM builds. Project-owned vendored sources (`tz`, `glad`, `bc_decoder`)
are not duplicate external provider sources simply because they use bare targets.
