# M4 phase two: exceptional dispatcher return

Date: 2026-10-23. Branch: `codex/issue-372-fixes`, baseline `0bb6abcc71`.
Main is unchanged. This is a bounded follow-up to checked UnmapMemory. All THREE
M4 phases remain unfinished; GuestBackingAdmission remains Unknown.

## Failure first

RunAcknowledgedCallback previously let an exception escape dispatch before
performing its Pending cancellation or Running completion wait. A dispatcher
could retain a closure, then throw; the caller would return failure while that
closure could later enter the action. If dispatch threw after the action started,
the helper also returned before that action finished using its caller's state.

The new actual-header fixture compiled before the production change and failed
at both deterministic oracles:

- `FAIL throwing dispatch left a runnable callback after return`
- `FAIL throwing dispatch returned while action still running`

The delayed-closure witness invokes a retained callback after the failed caller
returns, using a counter rather than dereferencing a destroyed receiver. The
active witness gates the action with events and proves the early return before
releasing it. Neither needs a crash, timeout or timing-dependent random race.

## Repair and result boundary

The helper records an exceptional dispatch return and then uses the same atomic
admission race as ordinary dispatch. If still Pending, it changes to Cancelled;
a retained closure can never start the action after this point. If Running, the
helper waits for the actual promise before returning. A dispatch exception always
reports Failed, even if the action itself finished normally before that exception.
An action exception likewise reports Failed. Earlier effects are not rolled back.

Normal completed dispatch still reports Completed only after the callback returns.
Ordinary rejection still reports Cancelled. Queue scheduling, command types,
producer admission, native completion and production shutdown order are unchanged.
Request creation/allocation before dispatch can still throw; this repair concerns
exceptional dispatch after request ownership exists. Existing worker-side callback
rethrow/failure handling is retained; inline dispatch exceptions now become the
checked Failed result instead of escaping the helper.

## Windows deterministic validation

```powershell
python tools/gpu_lifecycle_tests/run_dispatch_exception_tests.py --repeat 3 --output-dir .diagnostics/m4-dispatch-final
python tools/gpu_lifecycle_tests/run_checked_unmap_tests.py --repeat 3 --output-dir .diagnostics/m4-dispatch-unmap-regression
python tools/physical_coherency_tests/run_windows_production_checks.py --output-dir .diagnostics/m4-dispatch-production
```

The focused suite covers retained pending closures invoked twice after exceptional
return, active actions that complete or fail, inline failure, failure before enqueue,
exception after successful action, ordinary completion and ordinary cancellation.
It runs the real helper with controlled dispatchers/worker threads. Three controls
remove pending cancellation, return immediately on dispatch error, or turn an
exception into completion. Each must hit its named runtime failure. An early
control implementation produced an unused-variable compiler error; it was corrected
and was not accepted as runtime evidence.

The real ThreadManager/RunThread/queue + actual Vulkan UnmapMemory-body regression
also repeats three times with its five precise controls. It retains normal sync/
async and inline behavior, active/queued shutdown, failed-worker acknowledgement,
partial-stage failures and optional false/nullopt distinctions. Cache/backend
objects remain fixtures; this is not native Vulkan execution.

Nine complete production units are compiled with real headers to Windows AMD64
COFF objects. This is object compilation, not a full application link. No new
remote/Android/device run is claimed. The existing workflow includes the new
fixture for a future dispatch.

Both positive suites passed three repetitions. All eight precise controls
(three exceptional-dispatch and five checked-unmap) hit their expected runtime
failures. Nine real production files generated AMD64 COFF objects with zero
compiler diagnostics. All 32 recorded source-hash entries across three manifests
(26 unique inputs), object digests and machine signatures were independently matched.
See the committed [result summary](M4_F3_DISPATCH_EXCEPTION_RESULTS.json).

Final detailed logs and manifests remain locally in `.diagnostics/m4-dispatch-results/`.
Cleanup removed 71 temporary files from this turn, including all twelve generated
exe files and nine objects. Previous result bundles and dependency caches remain.

## Remaining boundary

Failed can include an already completed action or partial effects. It never grants
permission to release unknown native work or to retry/reopen a quarantined mapping.
The helper assumes its action owns or borrows state valid through the helper call;
it cannot repair a dispatcher's independently destroyed local resources. Native
GPU completion, visibility and full guest physical publication still need separate
proof. HostMemory remap, escaped pointers and independent producers remain outside
this slice; no ordinary BufferCache/F2 admission is enabled.
