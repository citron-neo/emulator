# Dependency providers

CPM is the default provider on Android, Windows and Linux. To select bundled
vcpkg explicitly, configure with `CITRON_USE_CPM=OFF` and
`CITRON_USE_BUNDLED_VCPKG=ON`. The two providers are mutually exclusive.
Further vcpkg cleanup is separate from this integration.

Consumers link canonical CMake targets. Provider discovery and legacy path
adapters belong in `CMakeModules` and `externals`, rather than consumer targets.
Oboe uses `oboe::oboe`; Opus, Sirit, Adrenotools and FFmpeg targets are normalized
by `DependencyTargets.cmake`.

Sirit uses the parent SPIRV-Headers provider through a build-tree
`SPIRV-HeadersConfig.cmake` and `SPIRV-Headers_DIR`. It does not configure its
nested headers or require a source patch.

Dynarmic is pinned to the Citron integration fork for its C++23 requirements,
direct `<print>` include and RegList formatter correction. Its containers preserve `std::hash` for ordinary keys and use Boost hashing
for tuple/pair keys. Citron's tuple/pair hasher stays in `common/container_hash.h`;
no Citron headers or compatibility patches are injected into Dynarmic.

OpenSSL remains at 3.6.1. Android keeps its ABI/API-aware OpenSSL builder and
existing FFmpeg integration. Adrenotools uses an upstream revision containing
runtime page-size fixes, including its linkernsbypass dependency.

The Opus clang-cl SSE4 and stb overflow patches remain provider-specific fixes.
