# M4 P2: capture output and local native upload borrows

2026-10-24, Windows local checkpoint, from `ce2007b440cad4677882a35382f4fec1b8cd30de`.
Branch: `codex/issue-372-fixes`; main is not merged or edited.

The selected defect is exception-driven destruction of stack-owned native output
and upload resources while queued recording or submitted device work can still
refer to them. This follows the accelerated image/view handoff repair; that
source packet does not own capture output or local upload staging.

## Audit and selected boundary

| Route | Owner and use | Audit disposition |
| --- | --- | --- |
| RendererVulkan::RenderToBuffer | Local Frame owns image/view/framebuffer; local destination buffer; draw plus a reference-capturing download callback | Reproduced early destruction and repaired in this change |
| RendererVulkan::GetAppletCaptureBuffer | Renderer owns applet_frame; local download buffer is captured by reference | Reproduced local-buffer destruction and repaired in this change |
| present/util.cpp UploadImage | Caller owns image; function owns upload_buffer and regions; queued callback borrows all three | Reproduced unwind before queued or native use ends; repaired in this change |
| RenderScreenshot | Receives completed download buffer, then copies and calls completion callback | Post-completion exceptions may unwind normally; external screenshot pointer/callback validity is separate |
| BlitScreen adaptation/filter replacement | Existing WaitIdle drains recording, presentation and checked device idle before clearing/replacing pass/layers | Existing controlled idle regression remains the evidence; no new whole-helper guarantee |
| Layer::RefreshResources / SetAntiAliasPass | anti_alias.reset/replacement precedes ReleaseRawImages/per-slot Wait in the source order | Next concrete audit target: old helper ownership across replacement, internal Finish and pending native use; source-order risk confirmed, deterministic native-helper witness/fix still open |
| FXAA/SMAA/TAA/FSR/FSR2 member resources | Member-owned images/views/framebuffers, descriptor sets and pipelines; preparation uses Finish | New UploadImage boundary covers its local staging only. It does not certify every member or exceptional constructor/replacement path |
| RenderAppletCaptureLayer initialization | applet_frame.image is published before view/framebuffer creation finishes | Partial initialization/retry remains a separate open audit item; no transactional frame publication claim |

The selected capture handler also encloses DrawToFrame. If a preparatory helper
has already submitted a prefix and then throws back to the capture owner, the
local output and the owning Renderer/BlitScreen cannot unwind through that
handler. Helper locals destroyed *inside* an unrelated throwing helper, arbitrary
escaped raw APIs, and full application Composite error recovery remain outside
this slice.

## Reproduction and candidate before production

The pinned old production bodies are extracted into a controlled fixture.
Four exception positions for each of the three routes expose a destruction
witness: Record after retaining its callback, Finish before callback execution,
Finish after possible native effects, and a semaphore-gated worker entered before
its delayed read. Draw and an internal-split/prefix failure add two capture
witnesses: fourteen historical witnesses total, each required to exit 91 with a
named lifetime failure rather than timeout or crash.

The candidate is preserved in `capture_borrow_candidate.json`. It transforms
pinned pre-fix bodies in memory, so the candidate suite can run again without
editing production. Candidate normal/error/control cases passed before applying
the production edit. The current suite then extracts the actual repaired bodies.
No production scheduling change preceded the candidate acceptance.

## Repair and normal semantics

Each route checks Scheduler's existing submission admission before allocating or
borrowing. This is a GPU-owner-thread check under the existing serialized caller
and shutdown protocol, not a newly acquired producer lease. It does not make a
concurrent arbitrary caller versus Shutdown legal.

The potentially effectful region starts before RequestOutsideRenderPass or
DrawToFrame. Its handlers run in the scope that still owns the frame/buffer and
upload regions. Vulkan and host errors there call the existing terminal fail-stop
without C++ unwind, logging/draining, atexit or retry. This is terminal retention,
not a successful cancellation or a recoverable screenshot error. Even Record or
context allocation may have partial effects; the handler never infers no effects
from an exception.

`FinishNativeBorrow()` performs:

1. Recheck existing admission and save the exact current tick.
2. Run the existing Finish unchanged (one submission, original wait and context allocation).
3. Drain host recording with WaitWorker, without holding submit_mutex.
4. Require PollNativeResourceLifetime(saved tick) to return VK_SUCCESS.

Timeline mode uses the existing checked counter and zero-time checked host wait.
Fallback mode requires the actual successful native fence wait/reset/recycle
acknowledgment; recorded completion failure never grants release. Pending,
unsupported or failed completion is terminal here. The adapter does not pump,
retry, add another submission, use successful enqueue as completion, or establish
new guest-content provenance. Ordinary Finish keeps its existing ordinary queue
failure behavior; this change does not repurpose observed/data receipts.

Only after the adapter returns may capture invalidate its mapped output and
return/copy bytes, or UploadImage release staging/regions. No success is published
at enqueue time. Admission rejection and failures retain the selected owner set;
there is no retry of a submitted prefix. Resource allocation, upload fill/Flush,
and view/framebuffer creation failures before this new effectful region can
unwind normally if no borrow was started. A readback Invalidate failure *after*
confirmed completion also unwinds normally. The empty applet route does no work.

The checked poll is a native lifetime boundary. Existing transfer barriers and
invalidate remain unchanged; this is not an all-platform pixel-content, arbitrary
visibility, guest coherency, or CPU write transaction acceptance.

## Shutdown and owner scope

Renderer shutdown still closes the outer GPU/callback producer protocol, joins
recording and presentation workers, and checks device idle before releasing
native owners. No change moves the submission lock across joins. The admission
check rejects new selected calls after Scheduler closes. A failed selected
operation cannot unwind into renderer member destruction and then race the
remaining worker. The fail-stop intentionally preserves every live local and
outer owner until process termination.

This does not introduce a scheduler continuation, new range admission, a native
completion registry, Device lifetime ownership, or a reversible GPU closure.
Existing normal renderer shutdown/failed-idle retention is rerun independently.

## Windows evidence

The fixed and candidate suites each run 55 cases three times, plus seven exact
mutation controls once. Cases include three normal routes; 14 terminal positions
per route (host/Vulkan exceptions, active worker, pre/post submit, context failure,
closed entry, checked pending/query/fallback reset failure); two capture draw/split
errors; three pre-allocation failures; frame creation failure; empty applet;
upload Flush failure; and two post-completion invalidation failures.

Normal assertions require exactly one submission, actual callback execution,
completed native work, host drain and capture invalidation. Terminal children
must exit 86 with exact stage/reason and no selected buffer/image destruction,
no owner destruction after borrow begins, and no atexit. A real semaphore gate
starts a controlled worker before the delayed read; no sleep establishes order.
Controls remove each route's error handler, native completion check, admission,
host drain or correct saved tick, and require their named runtime witness.

Actual extracted code: RenderToBuffer, GetAppletCaptureBuffer, UploadImage,
Scheduler::Finish, the two new Scheduler adapters and TerminalFailStop. Allocation,
image/buffer wrappers, draw preparation, command emission, native progress,
SubmitExecution/Wait/WaitWorker collaborators and the queued/active-worker gate
are fixtures. Callback exceptions are injected synchronously by the fixture;
real uncaught worker exceptions retain the pre-existing process-termination
behavior. This suite does not certify that behavior as recoverable propagation.
The actual native-source/full SubmitExecution suite and actual shutdown worker
suite separately cover those production portions.

Regressions passed: accelerated native source (26 paths, seven protocol paths,
seven terminal scenarios and fourteen controls, three repetitions); native fence
Wait/WaitThread/SubmitQueueFence (three repetitions and eight controls); renderer
shutdown/worker order (ten paths, three repetitions).

Three real Windows GCC 13.2 AMD64 COFF units compile with zero diagnostics:
renderer_vulkan.cpp, vk_scheduler.cpp, present/util.cpp. Pinned production headers
and cached Boost are used; no stub headers or app link. There is no local Android
build, MSVC acceptance, native Vulkan/game run or current-head CI acceptance.
The integration workflow now includes the fixed capture-borrow suite; it was not
manually dispatched by this change.

See [hashes, commands, manifests and cleanup](M4_P2_CAPTURE_NATIVE_BORROW_RESULTS.json).
Current-run temporary executables/objects/generated fixtures are removed after
hash verification; logs and manifests remain under `.diagnostics/m4-capture-results`.
The committed result summary is sufficient to identify inputs and rerun at work;
ignored local logs are not transferred by Git.

## Next continuation

First reproduce helper replacement before old use completion around
Layer::RefreshResources/SetAntiAliasPass, including warm filter changes, all
slots, internal Finish and failed waits, before changing production. Also audit
partial applet_frame construction/retry separately. Do not treat an accelerated
source packet as ownership of these helpers or output frame resources.

HostMemory remap/protection remains high risk. All M4 phases are incomplete,
F2/F3 remain open, and GuestBackingAdmission remains Unknown.

Follow-up: [AA helper replacement retirement](M4_P2_AA_HELPER_RETIREMENT.md) now
covers RefreshResources/SetAntiAliasPass old-use completion, including late final
sampling beyond cached slot ticks. Partial applet/raw frame publication remains
open as recorded there.
