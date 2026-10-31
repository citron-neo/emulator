# M4 P2: applet failed-Draw content and readback boundary

2026-10-24 Windows checkpoint, from `f892213b0a26175d9ffa925d578447ccb7a78281`.
Branch `codex/issue-372-fixes`; main is untouched.

## Findings and selected protocol

Complete image/view/framebuffer construction did not prove the final composite
was recorded. DrawToFrame can throw before a final draw, after partial recording,
a submitted prefix, or an internal Finish. Existing GetAppletCaptureBuffer tested
only image existence and could download undefined/partial pixels. A later retry
could replay that uncertain applet path. Completing a prefix cannot prove that
missing final commands ever existed. A previously good image may also have been
partially overwritten, so returning the old capture is not a safe fallback.

A second shutdown witness was found while testing this boundary. ConfigureDraw's
scope-exit can save the current, unsubmitted tick on failure. Layer destruction
then called Scheduler::Wait(tick), which can Flush that tick even after Scheduler
worker closure. This must not be used to dispose an abandoned partial applet path.

Ten historical witnesses are retained: five first-Draw failure positions, failure
after previous good capture, replay, completed-prefix replay/readback, observation
inside Recording, and a cached current tick released after shutdown. Each requires
a named exit-91 runtime failure. Candidate transformations are preserved in
`applet_content_candidate.json`; each protocol addition passed candidate tests
before its production edit. Final expanded candidate and production runs passed.

## State and normal readback

The owning Renderer now tracks Empty, Recording, Recorded, Failed and Closed.
These are serialized owner states, not atomics or independently acquired leases.
The GPU emulation/callback producer must join before terminal teardown, as in the
existing outer lifecycle protocol.

| State | Meaning and allowed action |
| --- | --- |
| Empty | No successful Draw return. Failed image/view/framebuffer construction stays here and remains retryable; the existing no-image capture returns its zero-filled buffer of the normal fixed byte length |
| Recording | Entered before DrawToFrame; nested readback/reentry refuses without recording work |
| Recorded | DrawToFrame returned normally; permits requesting checked readback, not immediate content publication |
| Failed | Draw threw with unknown prefix effects; readback returns an empty vector and later applet Draw attempts emit no work |
| Closed | Set at the start of terminal Shutdown, before rasterizer/worker drains; applet draw/readback refuse |

Only a normally returning Draw sets Recorded. A thrown Draw sets Failed and
rethrows the original error. The frame and BlitScreen/helper owners remain members;
no reset, rollback, resource recreation or prefix replay occurs. Failed is sticky
for this Renderer lifetime, including after a prefix completes, empty input or a
later request. A new Renderer is needed to re-enable that applet path. This is
conservative quarantine, not an implementation of recoverable partial rendering
or a promise that the application survives a Vulkan/worker exception.

Normal Draw remains asynchronous with no new Finish, submit or wait. Recorded
merely admits the existing download callback. Bytes may return only after the
callback executes, the same existing graphics prefix completes through
FinishNativeBorrow's checked timeline/fallback lifetime boundary, and output
invalidation succeeds. Enqueue success is not capture success. Buffer allocation
failure before recording and invalidation failure after confirmed completion keep
their existing recoverable behavior; tests retry both. Uncertain readback failures
retain the existing terminal fail-stop with local buffer/frame owners alive.

Failed/Recording/Closed return an empty vector, distinct from the legacy no-frame
fixed-length zero buffer. SharedBufferManager::WriteAppletCaptureBuffer already
rejects an empty capture before guest stores. GetIndirectLayerImageMap skips its
copy for empty capture and retains its existing service-result policy; no claim
is made that every VI API now returns a failure Result. No service API is changed.

## Terminal release and shutdown

Shutdown marks Closed before any drain and keeps Frame/helper objects alive while
recording and presentation workers drain/join, Scheduler closes/joins and device
WaitIdle is checked. A failed idle/drain does not clear the owners or re-enable
capture. Submitted partial work can finish; unsubmitted recording is cancelled
under the existing terminal retirement path. Neither outcome changes Closed to
Recorded or supplies readable content.

Scheduler now publishes its internal native_resources_retired flag only after
RetirePresentationSourcesAfterDeviceIdle succeeds with admission closed, worker
joined and no active source scope. Its trusted Renderer call remains inside the
successful checked device-idle branch, after controlled ownership retirement.
This flag is terminal disposal evidence, not a guest/content/admission token.
The completion helper joins before Renderer member destruction as before.

Layer::ReleaseRawImages now calls WaitForResourceRelease. Before terminal disposal
it requires open admission and keeps normal tick waiting. After the verified
terminal retirement it skips the cached tick: the joined host/native boundary
already permits release, and a cached current tick must not be flushed. Closed
admission without that confirmation fails terminally before a wait or member
unwind. Failed native retirement never publishes the flag. The direct closed-
before-idle test verifies refusal; the old direct Wait control reproduces a late
submission attempt after worker join.

No scheduler continuation, range admission, new native completion registry or
normal scheduling policy is introduced. There is no retry of a failed prefix and
no guest write coherency routing. Arbitrary concurrent raw Renderer access is not
made legal by these owner-local states.

## Deterministic Windows evidence

The baseline has ten named witnesses, each repeated three times. Candidate and
fixed suites each pass 21 normal/refusal/lifetime paths, nine terminal paths and
six exact controls, all repeated three times. Paths cover first failure before
recording, recorded/submitted/internal-Finish/full-record prefixes, prior good
content followed by partial overwrite, sticky retry refusal after completion,
Recording reentry, creation retry, empty capture, native wait gate, read allocation
and invalidate retry, unsubmitted/submitted/split shutdown, idle gate, and cached
current-tick disposal. Terminal paths cover read submit/wait/host/query/pending/
callback failure, shutdown worker/idle failure and release before checked idle.

The fixture records partial/full writes and download staging effects in a native
job model. Normal output must match the full modeled write after checked
completion; failed capture must export no bytes or new download commands. Real
std::jthread/semaphore gates inspect owners and return status before native
readback or device idle can complete. No sleep establishes ordering. Terminal
children require exact stage/exit 86 and no frame/buffer destruction or atexit.
Controls incorrectly promote failure, retry failure, publish Recorded early,
omit close, omit readback Finish or restore the late release Wait, and must reach
the named runtime witness. Timeout/crash/build failure is not a passing control.

Actual extracted bodies: Renderer applet Draw/readback/Shutdown, Frame declaration,
Scheduler Wait/Finish/native-borrow/native-retirement/resource-release methods, Layer
destructor/ReleaseRawImages/ClearRawImages and TerminalFailStop. The partial native
jobs, bytes, resource probes, Draw/helper preparation, SubmitExecution/native wait/
WaitWorker and shutdown collaborators are fixtures. The job model is not Vulkan
execution or a visibility proof. Callback exceptions are injected synchronously;
real uncaught worker errors retain existing process termination behavior.

Regressions passed: Frame construction (24 paths/seven controls, including updated
post-Draw failure refusal); capture borrow (55 paths/three repetitions plus seven
controls); AA retirement (25 normal/24 terminal/ten controls); native source
(26 paths/seven protocol/seven terminal/fourteen controls); actual native fence
failure (three repetitions/eight controls); actual renderer/worker shutdown
(ten paths/three repetitions). Existing suites use an extracted state declaration
or an explicit collaborator stub; the new suite executes the actual release API.
Their manifests distinguish these fixture boundaries.

Three real Windows GCC 13.2 AMD64 COFF units compile with zero diagnostics using
pinned real headers and cached Boost: RendererVulkan, Scheduler and Layer. An
earlier BlitScreen object/log in the same diagnostics folder is historical only,
not part of the final three-object acceptance. No app link, MSVC, local Android,
Linux/Android/native device/game or current-head CI acceptance is claimed. The
integration workflow includes this suite but was not manually dispatched.

See [manifests, hashes, commands and cleanup](M4_P2_APPLET_CONTENT_READBACK_RESULTS.json).
Current-run executables/objects/generated fixtures are removed after verification;
logs and JSON remain under `.diagnostics/m4-content-results`. Ignored logs do not
travel by Git; the committed summary records exact inputs and resume commands.

## Continuation boundary

The selected failed-Draw export/replay and terminal Layer release gaps are fixed
within the serialized route. Recovery from failed drawing, whole native helper
constructor/recreation transactions, partial raw resource publication, genuine
pixel/visibility evidence and arbitrary escaped native/guest aliases remain open.
HostMemory remap/protection remains high risk; M4 incomplete, F2/F3 open and
GuestBackingAdmission Unknown.

Follow-up: [raw resource candidate publication](M4_P2_RAW_RESOURCE_PUBLICATION.md)
now preserves old complete raw owners through replacement creation failure and
retry, including stride sizing. This does not clear Failed applet quarantine or
certify raw upload/content completion.
