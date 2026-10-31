# Retained channel initialization and NV acknowledgement

Date: 2026-10-22 (Asia/Singapore).
Baseline: `d8ceb7f78f` plus the implementation committed with this report.
Scope: terminal initialization/publication, not completion of an M4 phase.

## Behavior and ownership

GPU::InitChannel returns the optional cold publication Entry already used for
address-space ownership. Both actual NV SetupGPFIFOChannel and SubmitGPFIFOImpl
retain it through final caller metadata/submission acknowledgement. Ready-channel
Submit must also acquire Entry. Setup now takes Submit's channel mutex.

Closed entry refuses before program-id/fence output writes, syncpoint increments
or queue calls. Deferred Setup without an address space still records metadata
under Entry; Submit without a bound address space refuses. Ready duplicates keep
the existing AlreadyAllocated Setup result.

ChannelState::Init no longer marks initialized after engine construction alone.
The GPU sets ready only after rasterizer binding and backend initialization
complete. initialization_failed is set before possible partial effects. An
exception propagates unchanged, leaves ready false, and prevents subsequent
initialization retry on that owner. Existing parent channel ownership retains
partial state until teardown; this is not durable GPU Unknown retirement proof.

## Executed validation

Windows x64, MSYS2 MinGW GCC 15.2.0 (Rev8), Python 3.14.4:

```powershell
python tools/gpu_lifecycle_tests/run_owner_publication_tests.py --repeat 3 --output-dir .diagnostics/m4-channel-init-windows
```

The suite also runs the existing actual-body GPU lifecycle suite to generate its
fixture, with all 10 cases repeated three times and the cancelled-read mutation.

| Check | Result |
| --- | --- |
| Address-space and channel publication positives | PASS, 3 repetitions |
| Closed cold/ready channel Setup/Submit, unchanged outputs and fence/queue counters | PASS |
| Deferred Setup, unbound Submit refusal, later bound materialization | PASS |
| Ready Submit reuses engines while retaining a fresh Entry | PASS |
| Engine/backend exceptions retain failure marker and reject retry | PASS |
| Teardown waits in engine/backend initialization | PASS |
| Teardown waits at Setup metadata and ready Submit acknowledgement | PASS |
| Prior five address-space publication mutation controls | PASS, precise rejections |
| Publish ready before backend initialization | Rejected: `channel advertised ready before backend initialization` |
| Omit partial-failure marker | Rejected: `partial channel failure lost retry isolation` |
| Setup ignores refused Entry | Rejected: `closed setup published channel metadata` |
| Submit ignores refused Entry | Rejected: `closed submit changed fence or queue state` |
| Setup/Submit release Entry early | Rejected separately: `teardown escaped retained channel initialization` |
| Actual CreateChannel after NotifyShutdown | Successful independent owner-creation counterexample |
| Production gpu.cpp, nvhost_gpu.cpp and channel_state.cpp | PASS, local WSL syntax |

The production command was:

```bash
g++ -std=c++23 -DNDEBUG -fsyntax-only -Isrc -Iexternals/dynarmic/externals/fmt/include -Iexternals/Vulkan-Headers/include src/video_core/gpu.cpp src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp src/video_core/control/channel_state.cpp
```

Actual GPU close/join, channel initialization/creation methods, public forwarding
and complete NV Setup/Submit method bodies run in the fixture. Engine allocation,
alias binding, renderer initialization, syncpoints, queue work and complete System
lifetime remain fixtures. The actual ChannelState engine constructor is syntax
checked, not executed; the test checks its premature ready assignment stays
absent. Normal-case backend hooks assert that ready remains false until return.

## Limits and retained outputs

Channel creation is still unclosed, demonstrated by the actual-body counterexample.
Channel address-space binding and other service configuration are not covered.
Command-buffer reads/copies in SubmitGPFIFOBase1/Base2 occur before this Entry. Entry does
not certify actual queue acceptance: an admitted caller racing later Stop can
still have void PushGPUEntries cancelled. No native GPU execution, checked
completion, raw-alias revocation, durable Unknown registry, or reversible
ProcessCohortStop coupling is established. All THREE M4 phases remain unfinished;
ordinary GuestBackingAdmission remains Unknown.

No new remote workflow, Android/full application build or native Vulkan test was
run. Future workflow runs include the extended publication suite. This round's
temporary binaries, generated fixtures, manifests and logs are removed after
inspection; this Markdown conclusion and committed test sources remain. Older
diagnostics and main are unchanged; main waits for M7's final zero-copy merge.
