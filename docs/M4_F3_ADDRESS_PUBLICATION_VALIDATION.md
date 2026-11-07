# Retained address-space owner publication: local validation

Date: 2026-10-22 (Asia/Singapore).
Baseline: `fc61b3528f` plus the implementation committed with this report.
Scope: terminal address-space publication; no M4 phase is completed.

## Ownership contract

GPU's cold owner-publication counter opens at construction, allowing ordinary
initialization before GPU Start. NotifyShutdown closes entry without waiting.
The GPU destructor stops its worker, then joins owner publications before
composite/renderer teardown.

GPU::InitAddressSpace returns an optional owned TerminalIngress::Entry after the
raw rasterizer binding. NV AllocAsEx retains that Entry through its final owner
commit. VM, allocator and GMMU candidates are staged locally. Closed entry returns
NvResult::InvalidState with no bind, GMMU replacement or initialized VM. Nothrow
VM/GMMU moves commit only after successful admission. Returning from binding alone
cannot release the caller's publication protection.

## Executed checks

Windows x64, MSYS2 MinGW GCC 15.2.0 (Rev8), Python 3.14.4:

```powershell
python tools/gpu_lifecycle_tests/run_tests.py --repeat 3 --output-dir .diagnostics/m4-address-publication-windows/gpu
python tools/gpu_lifecycle_tests/run_owner_publication_tests.py --repeat 3 --output-dir .diagnostics/m4-address-publication-windows
```

Both commands exited 0. Negative controls report the precise forbidden effect;
unrelated termination, timeouts or compilation errors are not accepted as
successful negative evidence.

| Check | Result |
| --- | --- |
| Existing GPU lifecycle/readback/presentation regressions | PASS, all 10 cases, 3 repetitions |
| Cancelled-read coverage control | PASS, precise rejection |
| Retained publication protocol | PASS, 3 repetitions |
| Healthy publication before GPU Start; duplicate refusal | PASS |
| Closed entry leaves raw bind and service owner state untouched | PASS |
| First/second allocator and GMMU construction failures retire staged candidates | PASS |
| Teardown waits during binding callback | PASS |
| Teardown waits after bind returns, before NV owner commit | PASS |
| Omit Close | Rejected: `closed publication bound rasterizer` |
| Omit Join | Rejected: `teardown escaped retained owner publication` |
| NV caller releases Entry before commit | Rejected: `teardown escaped retained owner publication` |
| NV caller ignores admission refusal | Rejected: `refused publication reported service success` |
| Mutate service VM directly instead of staging it | Rejected: `refused publication changed service owner state` |
| Complete production gpu.cpp and nvhost_as_gpu.cpp | PASS, local WSL Ubuntu syntax |

The production syntax command was:

```bash
g++ -std=c++23 -DNDEBUG -fsyntax-only -Isrc -Iexternals/dynarmic/externals/fmt/include -Iexternals/Vulkan-Headers/include src/video_core/gpu.cpp src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp
```

Publication fixtures execute actual GPU destructor/NotifyShutdown and publication
method bodies, public forwarding, MemoryManager's raw binding assignment, complete
NV VM layout and AllocAsEx body. VM's allocator type, GMMU construction, renderer,
worker and complete System owner lifetime are fixtures. Hooks pause before raw
binding and before the final caller commit. The tests fail immediately before
touching an Entry whose owner a teardown mutant has already destroyed.

## Limits and cleanup

This counter does not stop candidate allocation before entry, serialize service
clients, close other channel creation/init/bind routes, revoke existing mappings
or exported raw aliases, or keep the renderer alive for later GMMU operations.
External facade/System lifetime and ordinary thread-serialization requirements
remain. No reversible ProcessCohortStop coupling, native writer drain, checked
Vulkan completion or durable Unknown registry is added. All THREE M4 phases remain
unfinished; ordinary GuestBackingAdmission remains Unknown.

No new workflow, Android build, full application build or native Vulkan test was
run. Future Linux workflow runs include this regression and its source/log
artifacts. This round's temporary executables, generated fixtures, manifests and
logs are removed after inspection; this Markdown conclusion and committed test
sources remain. Older unrelated diagnostics and main are untouched; merge main
only after M7's final zero-copy version is ready.
