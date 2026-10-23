# Terminal CPU-write cache callback: Windows validation

Date: 2026-10-22 (Asia/Singapore).
Baseline: `e5a1ffe36223e5ddf342ef7290529cd7e91c7eff` plus the implementation
committed with this report. This is a phase-two prerequisite, not M4 completion.

## Change and contract

`GPU::Impl::OnCPUWrite` forwards through `ThreadManager::OnCPUWrite` instead of
calling the rasterizer directly. It shares the terminal ingress counter used by
direct invalidations. External callbacks retain their entry through return or
exception. Stop closes entry, joins the worker, destroys queued captures outside
the queue lock, then joins external callbacks before rasterizer teardown.
Already executing GPU-owner callbacks finish under worker-join ownership.

Healthy true/false rasterizer results are preserved. Closed entry returns false
without touching the rasterizer. This only declines dirty collection in the
existing Memory caller; it neither stops nor authorizes the actual CPU store.
Caller/facade lifetime and serialized start/terminal teardown remain external
preconditions. Callback return does not prove asynchronous GPU completion.

## Executed checks

Windows x64, MSYS2 MinGW GCC `15.2.0` (Rev8), Python `3.14.4`.
All commands below exited 0 in `E:\retro\emulator`:

```powershell
python tools/gpu_lifecycle_tests/run_thread_tests.py --repeat 3 --handoff-controls --output-dir .diagnostics/m4-cpu-write-windows
python tools/gpu_lifecycle_tests/run_tests.py --repeat 3 --output-dir .diagnostics/m4-cpu-write-windows/gpu
g++ -std=c++23 -DNDEBUG -fsyntax-only -Isrc -Iexternals/dynarmic/externals/fmt/include -Iexternals/Vulkan-Headers/include src/video_core/gpu_thread.cpp
```

| Check | Result |
| --- | --- |
| Actual ThreadManager/RunThread/queue with fixture renderer and scheduler | PASS, 3 repetitions |
| CPU-write pre-start/post-stop refusal; original true/false results | PASS, synchronous and asynchronous modes |
| Entered CPU-write remains owned after worker exit until callback returns | PASS |
| CPU-write exception releases entry; entered GPU owner finishes after close | PASS |
| Existing five direct-invalidation mutation controls | PASS, each rejected at its intended assertion |
| Restore actual GPU facade's old CPU-write bypass | Rejected: `pre-start CPU-write touched rasterizer` |
| Release CPU-write entry before rasterizer callback | Rejected: `Stop failed to join direct CPU-write` |
| Return true when terminal entry is closed | Rejected: `closed CPU-write falsely requested dirty collection` |
| Reject CPU-write from an already entered GPU owner | Rejected: `Stop rejected CPU-write from an already entered GPU owner` |
| Existing blocking CPU/GPU handoff dependency control | PASS, precise rejection |
| GPU lifecycle, cancel, cancel_reentry, capture, cpu_read, presentation, active_presentation, normal_cpu_read, normal_composite, success | PASS, each 3 repetitions |
| Production gpu_thread.cpp Windows translation unit syntax | PASS |
| Actual preemptive GPU::Impl::OnCPURead/GetFlushArea after Stop | Successful independent bypass, intentionally retained |

The ThreadManager test executes the actual GPU CPU-write forwarding method and
actual ThreadManager class/definitions, actual queue, and terminal ingress atomic
operations. Observers only establish deterministic rendezvous points. Renderer,
engine, scheduler and platform/Core/System remain fixtures. The CPU-read bypass
uses the actual method's preemptive branch against a live fixture rasterizer;
it does not prove real readback completion.

## CI baseline and limits

[Linux run 39729352143](https://github.com/Splaser/emulator/actions/runs/39729352143)
passed at **earlier** source `d8b0c3897de6cbd57f10c9d02bcfa27a35086637`, including
production compilation/unit tests, native Vulkan/F2/F3 controls and integration
regressions. Independent artifact recovery has not been verified. That result
does not cover this newer CPU-write change or the later ScarletNX merge.

This report covers local Windows fixtures and syntax, not a full Windows app
build, new Linux/native Vulkan validation, Android build, signal-safety proof,
reversible GPU freeze, or general producer exclusion. No new workflow was
dispatched for this local validation. CPU native stores, raw pointers, services,
other rasterizer entries, suspended channels and native GPU work still need
owned closure and completion/Unknown retention. All three M4 phases remain
unfinished; ordinary GuestBackingAdmission remains Unknown.

Per user request, this round's temporary output directory, executable fixtures,
generated sources, manifests and logs are removed after inspection. This report
and committed test sources remain. Older unrelated diagnostic outputs are not
part of that cleanup. Keep main unchanged until the final M7 zero-copy merge.
