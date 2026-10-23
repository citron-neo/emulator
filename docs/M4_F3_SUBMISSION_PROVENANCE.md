# M4 F3: isolated submission-linked write provenance

The subsequent [opt-in ordinary storage observer](M4_F3_STORAGE_WRITE_OBSERVER.md)
extends the ordinary Draw/Dispatch capture boundary. The sections below retain
the verified isolated slice's scope and evidence; neither slice provides physical
latest-writer authority.

This is the first submission-provenance slice following the review of
`d8764c0cf0`. Resource-local revisions remain write intent. F2/F3 remain Open,
restricted eligibility remains blocked, and ordinary Memory::Write/UMA routing
remains inactive. This adds no physical latest-writer authority or ordinary shader
producer enrollment.

## Evidence recorded

`Scheduler::RecordCheckedWrite` accepts an externally qualified, retained resource
owner, its native identity, native byte offset/length and resource-local intent
revision. On the existing GPU owner, it records one worker command and immediately
calls `FlushChecked`. No intervening host flush can associate the command with a
later submission. The identity is the actual CheckedSubmission object, scoped to
its Scheduler owner, rather than a sortable integer or CurrentTick certificate.
The callback returns true only after recording its write commands, false only
when no write was emitted. Partial recording exceptions are Unknown and never
replayed. A submission identity is reserved before queue acceptance; its existence
does not itself mean Submitted.

| State | Evidence |
| --- | --- |
| Pending | Worker recording or actual queue submission is still pending. |
| Submitted | Actual checked queue submission returned success, but checked device completion is pending. |
| Skipped | This command emitted no declared write; completing its enclosing submission cannot turn it into a completed write. |
| Complete | Worker emitted the declared write and checked submit, timeline counter and zero-time host wait all succeeded. |
| Unknown | Recording, submission, counter/wait, owner lifetime or retirement is uncertain. Subsequent successful polling cannot recover the same event. |

Complete describes historical execution, not current resource validity, host
readback visibility or physical content authority. A later write can invalidate an
old resource/intent snapshot without erasing the earlier submission's history.
The event covers one declared command footprint, not every possible writer in the
enclosing batch. Binding uploads, scatter and other producers require their own
events before any batch-wide or physical content claim.
No revision or submission identities are ordered across allocations or queues.
Unknown retains the resources; physical admission/quarantine still belongs to the
existing backing range owner, and this API does not enroll producers in it.

## Bounded actual producer

`BufferCacheRuntime::ClearBufferChecked` is an explicit isolated staged fill.
It uses the same actual fill/barrier recorder as ordinary ClearBuffer, retains the
actual StagedBufferPin, and checks the pin again on the worker before recording.
An optional worker predicate supplies a supported no-emission path. The caller
captures the intent revision with the pin under the existing cache mutex; metadata
allocation and checked submission happen outside that lock. There is no ordinary
game call site and no per-draw flush policy change.

Ordinary Draw's existing worker command now returns whether it emitted a draw;
ordinary Scheduler::Record ignores that return value. Its actual failed-pipeline
guard is extracted into the fault fixture and observed with RecordCheckedWrite.
This tests the real guard body against fake pipeline/Vulkan dispatch, not a native
shader or a full storage-binding/draw integration. Ordinary draw/indirect/compute
provenance capture is still unimplemented and must not be inferred from that fixture.

Accepted evidence records and their real allocations are retained in the existing
Scheduler, bounded to 256 isolated records. They are disarmed only through the
renderer's existing successful-device-idle retirement boundary, after producer
joins. Retirement removes native identity/resource access and returns Unknown;
it cannot certify content. This is deliberately unsuitable for automatic ordinary
rendering enrollment until retirement/consumer integration is established.
Pins retain allocations, not VkDevice/VMA owner lifetimes. Callers must retain the
renderer/device/allocator domains until accepted records are disarmed; failed idle
is not permission to destroy those domains while external records still own pins.
The existing failed-idle/whole-device teardown exclusion remains a separate gate.

## Validation boundary

The new hidden native `[f3-provenance]` case has its own CTest process, preserving
the existing per-process System lifetime restriction. It uses real ordinary cache
intent publication, staged resources, checked Vulkan fill/queue completion,
System/Kernel/process and device mappings. It checks skipped fill intent, distinct
submissions for the same dirty interval, old intent snapshot invalidation without
rewriting historical completion, a worker-pending later command, independent stale
replicas, remap before old-command completion, retained old bytes and rejection of
the old physical plan even after the replacement representation is warmed. A
completed old allocation is never associated with the new mapping. No physical
publisher exists in this slice.

The separate actual-body fault fixture checks the failed-pipeline Draw guard,
indexed draw emission, queue-accepted/device-pending distinction, submit/counter/
wait/partial-record/sealed-pin/owner failures, sticky Unknown and native payload
retention after cache/caller owners are dropped. Vulkan dispatch/device execution
and pipeline state in that fixture remain mocks. Existing mutation gates, native
alias adapter and original F2/F3 negative controls remain separate.

## Verification record

The first Linux build at `64c59e9208` exposed a QueryCacheParams incomplete-type
dependency after adding the Scheduler header to the buffer runtime interface.
`41089c1dbc` moved SetQueryCache's unchanged definition after the complete query
traits. Its [run 38914141764](https://github.com/Splaser/emulator/actions/runs/38914141764)
passed all five Linux CTest processes, including 53 provenance assertions, and
three additional provenance processes with 53 assertions each. The full configured
Linux integration/fault/lifecycle gates passed, including the new actual-body
provenance fixture and original F2/F3 negative controls. Android in that same run
failed two Clang shadow-uncaptured-local diagnostics on init-captures; that run is
not full platform acceptance. Logs and generated inputs are retained under
`.diagnostics/ci-38914141764/`.

`102b0486f7` renames only those captured callables, preserving the warning policy
and write behavior. Its full Linux/Android validation is tracked separately in
[run 38984914456](https://github.com/Splaser/emulator/actions/runs/38984914456).
A separate Windows actual-body fixture check passed the Submitted/completion
separation before the capture rename; its Vulkan execution remains simulated.
Final native acceptance belongs to the exact remote source run, not that earlier
local extraction. Android builds are remote; no local Android compilation is used.

The final source's Linux job passed all five CTest processes: 130 ordinary cases,
1708 original scheduler assertions, 38 original F3 assertions, 88 isolated alias
adapter assertions and 53 provenance assertions. Three additional native
provenance processes each passed 53 assertions, alongside the three alias adapter
repetitions. All configured HostMemory/import, coherency/lifecycle and fault/mutation
gates passed. F3 fixed mode still fails exactly its two known alias checks;
the F2 witness still observes backing 77/cache 10 and retains its fixed-mode
negative. Exact source stamp, per-process logs, generated fixtures and Linux job
output are retained under `.diagnostics/ci-38984914456/`.

The complete run succeeded on exact production source `102b0486f7`, including
Android's actual arm64 core/video/shader libraries compiled remotely with CPM and
NDK 29. This is native library compilation, not an APK/device execution result.
Full job output and Android configure/source artifacts are retained alongside the
Linux evidence. The final documentation follow-up changes no production/test
sources or workflow; no duplicate validation is required for those records.

## Next gate

Before any physical latest-writer authority, capture immutable historical
GuestBackingEpoch/physical geometry and actual representation association at the
qualified producer boundary, carry the existing admitted footprint through
submission and validate it at publication. Ordinary skipped draw/dispatch and
multiple writable resources need actual producer integration, not bind-time
assumptions. Establish real queue/synchronization relationships, merge/GC and
mapping/frame-reuse exclusion before choosing between staged or backing sources.
