# M4 P2: raw upload recording eligibility, content and retry

2026-10-25 Windows checkpoint from `3165ece04dccac87feaff78eeeed1e2e767ae3fd`.
Branch `codex/issue-372-fixes`; main is untouched.

## Audit and selected gap

A complete raw staging/image/view tuple does not initialize its pixels.
ConfigureDraw waits the selected slot tick, invokes UpdateRawImage, then records
AA and later sampling only when it returns true. UpdateRawImage previously had
no failed-prefix state: after Scheduler::Record threw, another raw attempt could
read a fresh source and overwrite the same staging slice. Record has no rollback
receipt and may have enqueued work before an exception; returning from it only
establishes enqueue/recording eligibility. The old implementation cannot classify
such a failure as an effect-free retry.

Nine historical witnesses cover three slots and first enqueue, partial native
submission, and full native submission after a previous successful upload. Each
requires the named runtime assertion, repeated three times, never a build error,
crash or timeout. The saved candidate transformation and core tests passed before
production mutation; the final matrix also checks failure-path tick retention.

## Per-slot protocol

RawUploadState is mutated only by the existing serialized GPU owner:

| State | Meaning | Raw consumption/retry |
| --- | --- | --- |
| Empty | New generation, no accepted upload yet | Prepare a source and attempt upload |
| Recording | Owner is inside Record | Refuse reentry; no completed-content claim |
| Recorded | Record returned normally; callback may still be queued | Existing FIFO sampling and selected-slot wait govern use/reuse |
| Failed | Record threw, effects cannot be classified as absent | Refuse another raw upload in this generation |

UpdateRawImage checks existing Scheduler admission before mapped-memory access,
source copying or slot-state changes. This is an owner-thread check, not a new
concurrent producer lease. The private method still relies on ConfigureDraw's
existing selected-slot wait; it is not an independently synchronized upload API.

Source rejection, mapping-use failure and exceptions in mapping, unswizzle or
Flush occur before Record. They return false or propagate, never reach AA, and
can retry through the normal ConfigureDraw wait. A previous Recorded state does
not grant success for a failed current call. Guest source capture/copy, mapping-use
and existing flush behavior are unchanged.

Immediately before Record, the slot becomes Recording. A Record exception marks
only that slot Failed and rethrows the original exception. ConfigureDraw's actual
scope-exit still captures CurrentTick even on failure. Rejected retries do not
rewrite staging or sample the uncertain image, even if that prefix subsequently
finishes or had already fully submitted. Other slots retain their own eligibility.
A same-key RefreshResources preserves failure; no implicit same-generation replay
or retry-as-success is introduced. This deliberately trades availability of that
slot after a rare uncertain error for a bounded correctness guarantee.

The slot state vector is prepared with the complete RawResources candidate and
committed with a no-throw swap after WaitForResourceUses. New states are Empty;
failed construction preserves old state, and failed retirement is terminal before
swapping. A successfully rebuilt resource generation can accept raw uploads again.
There is no automatic same-key reallocation loop, extra normal submission, new
completion registry or scheduler continuation.

The actual worker callback catches Vulkan and host exceptions locally and enters
TerminalFailStop (`layer.raw_upload.recording`). It never mutates owner-thread
state from the worker, reports success for a partial recording, or unwinds borrowed
native owners. Errors outside this callback, including general worker/driver
failures, retain their existing policy; this is not global recoverable worker
exception propagation. Submission outcomes are controlled failure injections in
the tests, not a newly observed per-upload native submission receipt.

## Content and applet isolation

On success the existing barrier/copy/final-barrier sequence is unchanged. AA and
final sampling remain after the upload on the existing command stream, including
when an AA helper internally calls Finish on a prefix. Staging reuse still waits the
slot's previous tick. Recorded is never a host/native completion or readback
receipt; CPU readback still needs the existing checked completion and invalidate.
The fixture's byte/ordering oracle is not real Vulkan visibility evidence.

The applet renderer is unchanged. A propagated raw Record exception inside Draw
still triggers its sticky Failed quarantine. Rebuilding a raw generation does
not change AppletCaptureState or permit that applet to Draw/export again. Failed
raw preparation returning false instead of throwing skips that layer via the
existing ConfigureDraw/WindowAdaptPass drawable flag; the resulting frame may
contain background/other layers, never an uninitialized raw layer. The applet
content regression extracts real Renderer draw/readback/shutdown bodies; its Draw
and native jobs remain collaborators, so no full Renderer-to-Layer runtime test
or target-device pixel acceptance is claimed.

## Shutdown and ownership

No lifecycle member is reordered. Layer's existing tick release and successful
joined-worker/checked-idle retirement remain the destruction boundary. A queued
callback borrows the unchanged raw member set until that boundary; failed slot
state is not permission to destroy it. Rebuild drains old host/native uses before
swapping, including partial prefixes and internal submissions.

New semaphore cases pause the actual extracted callback before barriers/copy/end,
or pause the owner inside enqueue. A separate active-worker shutdown case closes
admission while the worker is paused, observes join blocked, then allows callback
exit, joins and performs modeled checked idle before releasing raw resources.
Failed idle and failed old-generation wait require terminal exit 86 with no
buffer destruction, owner release or atexit. Actual Renderer/Scheduler shutdown
order and retained-owner probes are separate production-body regressions; the
new queue, device idle and native execution are controlled collaborators.

## Evidence and limits

Candidate and fixed suites each pass 25 cases on three slots (75 paths), nine
terminal cases on three slots (27 paths), and eight exact controls, all repeated
three times. Cases cover first/warm enqueue failure, modeled partial/full
submission failure, pre-record preparation failures/retries, independent-slot
progress, generation replacement, internal Finish, four recording gates,
active-worker shutdown, closed direct upload, callback faults and failed waits.
Seven controls mutate extracted production behavior: failed-state check,
quarantine publication, generation reset, premature Recorded, final barrier,
direct admission and failure tick retention. One queue-fixture control executes
native work during enqueue and must fail the pending-content oracle. Every
control requires its named exit-91 assertion. Terminal cases require the precise
fail-stop stage, rather than accepting an arbitrary exception/process failure.

Actual extracted bodies are full UpdateRawImage including its deferred callback,
ConfigureDraw through AA Draw and tick scope-exit, RefreshResources, wait/clear/
release/destructor, GetRawImageOffset and the state/bundle declarations. The
new suite's candidate factory, source copy/unswizzle, allocation, native wrappers,
AA consumer, queues and bytes are fixtures. Its selected source-copy collaborators
do not replace the real mapping-use regression. There is no device command queue,
game, real pixel validation or new guest Memory/GetPointerImpl claim.

Regressions pass: raw tuple publication (66 paths/four terminal/six controls),
AA retirement (25/24/ten), mapping-use (ten protocol/seventeen raw/ten controls),
applet content (21/nine/six), each repeated three times; actual renderer shutdown
has ten paths repeated ten times. Three real Windows GCC 13.2 AMD64 COFF production
objects compile with zero diagnostics: Layer, BlitScreen and WindowAdaptPass.
Real pinned dependency headers and glslang-generated presentation shader are used.
No app link, MSVC, Linux/Android build, native-device/game or current-head CI
acceptance is claimed. The integration workflow now runs the fixed upload suite;
no workflow was manually dispatched.

[Results, commands, hashes and cleanup](M4_P2_RAW_UPLOAD_RETRY_RESULTS.json) retain
the suite manifests and current-run JSON/log inventory. Temporary executable,
object, generated source/header removal follows exact scope and hash verification.
Ignored local logs do not travel by Git; the committed result records rerun inputs.

## Continuation

Next audit remaining helper/descriptor and output exceptions after raw upload
returned, without converting enqueue eligibility into content readiness. Keep
applet Failed isolation. This does not close arbitrary native escapes or guest
producers. HostMemory remap/protection remains high risk; M4 incomplete, F2/F3
open and GuestBackingAdmission Unknown.


Follow-up: [post-upload helper failure](M4_P2_POST_UPLOAD_HELPER_QUARANTINE.md)
retains a failed shared helper graph and closes ConfigureDraw for that whole Layer.
Its sticky state survives raw-generation replacement. The raw-only failure state
and pre-record retry policy above remain independent; applet Failed is preserved.
