# Focused M4 production compilation on Windows

Date: 2026-10-22 (Asia/Taipei). Production baseline: `c6034b8e45`.

The earlier missing-Boost blocker is resolved. The old benchmark cache contained
only 32 component include trees and lacked Unordered's flat map. A complete Boost
1.87.0 release is now installed outside the repository at
`C:/Users/BDM11/.cache/citron-deps/boost_1_87_0`.
The official archive's SHA-256 was checked against its published metadata:
`af57be25cb4c4f4b413ed692fe378affb4352ea50fbe294a11ef548f4d527d89`.
[Official archive metadata](https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.bz2.json).
Project/provider pins and submodule gitlinks were not changed.

The reproducible helper can populate this shared header cache on another machine:

```powershell
$env:PYTHONUTF8='1'
python tools/physical_coherency_tests/run_windows_production_checks.py --fetch-boost
```

For an existing complete Boost installation use `--boost-root PATH`. The helper
uses the real pinned fmt headers and production includes, C++23, Release NDEBUG,
x64 architecture, no-strict-aliasing and the targets' conversion-error/sign-conversion
settings. Third-party includes are system headers. It does not substitute fake
System, Module, GPU, channel, mutex or engine types.

Windows MinGW-w64 UCRT GCC 13.2.0, Python 3.14.6: all five complete source files
compiled to actual AMD64 COFF objects with no compiler diagnostics:

| Production input | COFF object bytes |
| --- | ---: |
| src/video_core/gpu.cpp | 136861 |
| src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp | 164113 |
| src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp | 82124 |
| src/core/hle/service/nvdrv/nvdrv.cpp | 172075 |
| src/video_core/control/channel_state.cpp | 5083 |

All 11 manifest input hashes and the five object digests/machine signatures were
independently matched after compilation. This establishes focused Windows code
generation for the channel-binding change and its main header consumers, replacing
the previous syntax attempt stopped by missing Boost. It is not an application
link, full target build, runtime, native Vulkan or Android acceptance. Boost.Context
binary linkage and other full-application dependencies are not exercised here.
The earlier actual-body regression and mutation results retain their separate
scope. Main remains unchanged; no remote workflow was dispatched for this check.
