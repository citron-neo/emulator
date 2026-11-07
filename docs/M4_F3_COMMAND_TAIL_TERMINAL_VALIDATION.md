# Terminal command tail and dirty gather: Windows validation

Date: 2026-10-22 (Asia/Singapore).
Baseline: `7534a5a852` plus the implementation committed with this report.
Scope: a phase-two terminal lifetime prerequisite, not completion of an M4 phase.

## Change and ownership

The actual GPU facade now delegates FlushCommands, InvalidateGPUCache and
OnCommandListEnd to ThreadManager. Its existing terminal counter retains entered
external callbacks through return or exception. Already executing GPU-worker
tails may finish after external closure under worker-join ownership. Backend
FlushCommands remains direct; command end still calls ReleaseFences(false), then
UpdateGPUAccuracy. Backend thread-affinity/serialization requirements are unchanged.

Dirty gather acquires entry **before** consuming any records and holds it over
every owner and synchronous callback. Entering only within callbacks would allow
closed consumers to discard already-consumed metadata. Stop still closes entry,
joins the worker, retires queued captures, then joins external callbacks while
the rasterizer is alive. Facade lifetime remains externally owned.

## Executed Windows checks

Windows x64, MSYS2 MinGW GCC 15.2.0 (Rev8), Python 3.14.4.
Commands from `E:\retro\emulator`:

```powershell
python tools/gpu_lifecycle_tests/run_thread_tests.py --repeat 3 --handoff-controls --output-dir .diagnostics/m4-command-tail-windows
python tools/gpu_lifecycle_tests/run_tests.py --repeat 3 --output-dir .diagnostics/m4-command-tail-windows/gpu
g++ -std=c++23 -DNDEBUG -fsyntax-only -Isrc -Iexternals/dynarmic/externals/fmt/include -Iexternals/Vulkan-Headers/include src/video_core/gpu_thread.cpp
```

| Check | Result |
| --- | --- |
| Actual ThreadManager/RunThread/queue plus GPU facade | PASS, 3 repetitions |
| All three command callbacks retain external lifetime through worker exit | PASS, sync/async modes |
| Closed entry neither touches rasterizer/settings nor consumes dirty metadata | PASS, pre-start/post-stop |
| Entered gather completes callbacks for both fixture dirty owners after Close | PASS |
| Callback exceptions propagate and release entry | PASS, all three callbacks |
| Entered GPU worker finishes all three tail callbacks after Close | PASS |
| ReleaseFences(false), completed accuracy-update count | PASS |
| Earlier 13 direct invalidation/CPU-write/read-query controls | PASS, precise rejections |
| Restore direct facade FlushCommands | Rejected: `closed command ingress touched resources` |
| Restore direct facade InvalidateGPUCache | Rejected: `closed command ingress consumed dirty records` |
| Restore direct facade OnCommandListEnd | Rejected: `closed command ingress touched resources` |
| Release command entry before callback | Rejected: `Stop failed to join direct command callback` |
| Reject entered GPU-worker tail | Rejected: `Stop rejected already entered command tail` |
| Enter dirty protection only within each callback | Rejected: `closed command ingress consumed dirty records` |
| Existing blocking CPU/GPU handoff control | PASS, precise rejection |
| GPU lifecycle/cancellation/capture/readback/presentation regressions | PASS, all 10 cases, 3 repetitions each |
| Cancelled-read false coverage control | PASS, precise rejection |
| Production gpu_thread.cpp Windows syntax | PASS |
| Existing registration-pipeline fixture rebuilt with current ThreadManager prefix | PASS, local WSL Linux syntax only; no execution |
| Actual dirty Collect after Stop | Successful independent producer counterexample |

The fixture uses actual GPU/ThreadManager method bodies, actual System gather
loop and the complete actual GPUDirtyMemoryManager class. A fixture System owns
two managers; its gather counter is timing/effect instrumentation. The device
header shim copies the production DEVICE_PAGEBITS declaration. Renderer, backend,
scheduler, platform and the complete System owner inventory remain fixtures.
This is not native Vulkan execution or whole-guest producer-drain evidence.

The reused registration fixture could not be syntax-checked with MinGW because
its Boost/container/small_vector.hpp dependency is absent locally. Its existing
generated source was reconstructed with the current ThreadManager prefix and
checked using WSL Ubuntu g++ `-std=c++23 -fsyntax-only`, the existing registration
stubs, current thread shims, src, Vulkan and bundled fmt headers. This compatibility
check passed; it does not rerun registration-pipeline assertions or negative
controls. The missing Windows dependency is not a passed Windows pipeline check.

## Limits and retained outputs

Actual Collect remaining active proves that closing these consumers does not
close CPU producers. No new backend serialization, dirty-gather rollback or
partial-callback Unknown retention is implemented. FlushCommands may leave
queued native device work; return/worker join is not checked completion. Remaining
raw/service/publication routes and all native writers still need owned closure,
backing/alias retention and durable Unknown handling. All THREE M4 phases remain
unfinished; ordinary GuestBackingAdmission remains Unknown.

No new workflow, Android build, full Windows app build or native Vulkan test was
run. The older successful Linux run 39729352143 tested `d8b0c3897d`, not this
revision; independent artifact recovery remains incomplete. This round's temporary
directory (executables, generated sources, manifests and logs) and outer logs are
removed after inspection. This Markdown conclusion and committed test sources
remain; older unrelated diagnostics are untouched. Main remains unchanged until
M7's final zero-copy merge.
