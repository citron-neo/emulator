# Terminal CPU-read geometry and cancelled readback coverage

Date: 2026-10-22 (Asia/Singapore).
Baseline: `854d288a4d` plus the implementation committed with this report.
This is a phase-two prerequisite, not completion of any M4 phase.

## Change

GPU::Impl::OnCPURead queries optional geometry through
ThreadManager::TryGetFlushArea. External queries retain the terminal counter
until the rasterizer returns or throws. Stop closes this ingress and joins its
entered callers before rasterizer teardown. Already executing GPU-owner queries
remain covered by worker join. Healthy geometry/preemptive values are preserved;
closed query refuses before touching the rasterizer.

The query entry is destroyed before the facade queues/waits for a non-preemptive
readback. Existing sync-request shutdown and worker join own that separate flush.
Both closed query and cancelled flush now return an empty area, preventing the
Memory caller from caching the requested interval as newly flushed coverage.
Neither result cancels the underlying guest CPU load or grants exclusion.
Facade lifetime, including caller work before/after the query, remains externally
owned; joining the query callback does not join the whole OnCPURead call.

## Local validation

Windows x64, MSYS2 MinGW GCC 15.2.0 (Rev8), Python 3.14.4.
The commands run from `E:\retro\emulator` are:

```powershell
python tools/gpu_lifecycle_tests/run_thread_tests.py --repeat 3 --handoff-controls --output-dir .diagnostics/m4-cpu-read-windows
python tools/gpu_lifecycle_tests/run_tests.py --repeat 3 --output-dir .diagnostics/m4-cpu-read-windows/gpu
g++ -std=c++23 -DNDEBUG -fsyntax-only -Isrc -Iexternals/dynarmic/externals/fmt/include -Iexternals/Vulkan-Headers/include src/video_core/gpu_thread.cpp
```

| Check | Result |
| --- | --- |
| Actual ThreadManager/RunThread/queue fixture | PASS, 3 repetitions |
| Pre-start/post-stop facade query has no rasterizer effects or cached coverage | PASS |
| Original geometry, both preemptive flag values, query exception exit | PASS |
| Entered query survives worker exit until callback returns | PASS, synchronous and asynchronous modes |
| Entered GPU-owner query can finish after external closure | PASS |
| Existing five invalidation and four CPU-write mutation controls | PASS, precise rejections |
| Restore direct query in actual GPU facade | Rejected: `pre-start CPU-read query touched rasterizer` |
| Release query entry before callback | Rejected: `Stop failed to join direct CPU-read query` |
| Invent reusable geometry on closed query | Rejected: `closed CPU-read returned reusable interval` |
| Refuse an already executing GPU-owner query | Rejected: `Stop rejected query from an already entered GPU owner` |
| Existing blocking CPU/GPU handoff control | PASS, precise rejection |
| GPU lifecycle, cancellation/reentry, capture, CPU read, presentation/active presentation, normal CPU read/composite, success | PASS, each 3 repetitions |
| Ignore unsuccessful actual readback wait result | Rejected: `cancelled CPU read advertised flushed coverage` |
| Production gpu_thread.cpp Windows syntax | PASS |
| Actual GPU::Impl::FlushCommands against live fixture rasterizer after Stop | Successful independent bypass, intentionally retained |

ThreadManager tests use actual GPU forwarding/CPU-read method bodies, actual
ThreadManager definitions, actual queue and terminal ingress operations. Timing
observers only rendezvous at the tested boundaries. Renderer/engine/scheduler,
platform and Core/System remain fixtures. GPU lifecycle tests use actual sync
enqueue/wait/cancellation and CPU-read logic with a fixture manager/rasterizer.
Normal CPU read remains blocked until its flush callback has finished; shutdown
cancellation returns no new coverage. This is not native GPU execution proof.

## Limits and retained outputs

Existing cached geometry is not revoked. Underlying CPU loads/native stores, raw
pointers, services, suspended channels, other rasterizer routes and native device
work remain independent. The retained actual FlushCommands bypass demonstrates
that ThreadManager Stop does not close every rasterizer entry. There is no new
reversible GPU freeze, backing/alias protection, native checked completion or
durable Unknown owner. All THREE M4 phases remain unfinished; ordinary
GuestBackingAdmission stays Unknown.

No new remote workflow, Android build, full Windows application build or native
Vulkan test was run for this change. The earlier successful Linux run
[39729352143](https://github.com/Splaser/emulator/actions/runs/39729352143) tested
`d8b0c3897d`, not this revision; independent artifact recovery remains incomplete.

After reviewing results, this round's temporary directory (executables, generated
sources, manifests and logs) and outer log files are removed. This Markdown
conclusion and committed test sources remain. Older unrelated diagnostics are
untouched. Main remains unchanged until the final M7 zero-copy merge.
