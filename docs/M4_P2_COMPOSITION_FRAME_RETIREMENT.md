# M4 P2: final composition, target Frame ownership and error retirement

2026-10-25 Windows checkpoint from `7b2225e854c2784314177b9f1da0634cc3aa1e58`.
Branch `codex/issue-372-fixes`; main is untouched.

## Audit and ownership contexts

All ConfigureDraw calls can return successfully before WindowAdaptPass builds and
enqueues the final composition callback. That callback owns its copied vectors and
native handles, but not the target Frame allocation or the adapting pass/source
resources behind those handles. Scheduler::Record has no rollback receipt: an
exception during argument capture/enqueue may coexist with queued or submitted
work. Returning the original exception without first resolving that borrow is not
a sufficient standalone API lifetime contract.

Twelve controlled low-level witnesses reproduce actual Frame destruction with
outstanding modeled composition references: enqueue-after and partial submission,
three selected slots, with/without source-helper internal Finish, three repetitions.
They are not a reproduced device/game UAF. Current production callers already
provide additional ownership: RenderToBuffer has a no-unwind stack-owner guard;
applet_frame is held by Renderer with Failed quarantine; presentation frames are
owned by PresentManager. Those safeguards must be preserved rather than ignored
when choosing the failure policy.

PresentManager::GetRenderFrame removes a Frame from free_queue, increments in-flight
accounting, waits for presentation completion and resets present_done. A draw,
flush or Present failure after that has no proven cancellation/recycle receipt.
Keeping the allocation alive alone does not restore pool/fence bookkeeping or
justify continuing with the same slot. This is an independent boundary from the
raw/AA source-owner lifetimes already repaired.

## Final protocol

BlitScreen::DrawToFrame and direct WindowAdaptPass::Draw check existing Scheduler
admission before metadata exchange, source-scope creation or target handle access.
These remain serialized-owner checks, not independent concurrent producer leases.
Pending admitted callbacks may still run while shutdown closes entry.

The final callback keeps its existing by-value vectors/handles. The actual
source_scope and target/pass owners remain outside the enqueue handler:

1. Every source ConfigureDraw runs normally, including its internal Finish and
   retained native source renewal. No extra normal submission is added.
2. Attempt the final Record, including its C++ argument capture/construction.
3. If host enqueue throws, call the existing actual FinishNativeBorrow while all
   owners/source_scope remain alive. It saves the current tick, Finish-es the
   prefix, drains host recording and checks PollNativeResourceLifetime.
4. Only successful checked retirement permits rethrowing the original enqueue
   error. The call still failed; it does not publish success or advance image_index.
   A respectful direct caller may retry after that boundary; no source/helper or
   target-content rollback is claimed.
5. Retirement exceptions fail-stop at composition.retirement; a pending/failed
   native receipt retains the existing native_borrow.completion fail-stop. No
   owner or source scope unwinds under uncertainty.
6. The actual deferred composition callback has its own Vulkan/host catch at
   composition.recording. A partially recorded renderpass cannot be recovered by
   attempting another Finish; it stops without unwinding owners.

The error retirement may execute a queued composition that the caller regards as
failed, or finish a previously submitted prefix. That is a partial-side-effect
boundary, not cancellation, no-effects, GPU-content success or permission to read
failed output. No implicit ConfigureDraw replay occurs during retirement. Shared
helper counters/history are not rewound. The existing image-index advancement
occurs once only after successful WindowAdaptPass return.

The existing checked prefix adapter is sufficient for the bounded serialized
borrow. No Frame member reordering, ownership registry, scheduler continuation,
new successful-path completion or immutable physical transfer plan is introduced.
Argument vectors were already owned by the callback; their ownership does not
substitute for target/pass/source native lifetimes.

## Applet versus presentation failure

A host composition enqueue error with successful retirement still reaches the
actual RenderAppletCaptureLayer catch and sets sticky Failed. Even if that prefix
fully rendered during retirement, the applet refuses Draw and readback. The
connected fixture substitutes a healthy BlitScreen afterward; refusal remains.
ConfigureDraw failures before the final callback retain the same applet isolation.
Existing RenderToBuffer error handling remains terminal for its stack-owned frame
and download buffer. No prior Failed helper/Layer/app state is reopened.

For the presentation pool, RendererVulkan::Composite now guards the exact interval
after GetRenderFrame: DrawToFrame, Flush(render_ready), then Present. Any propagated
host/Vulkan error there is terminal at present.composition. This applies even if
composition error retirement succeeded, because the reset presentation fence and
pool accounting still have no recovery receipt. A Present error may have partial
queue effects; it is never treated as an unused frame that can simply be returned
to free_queue. This deliberately stops that render failure rather than risking a
stuck/reused pool slot. Recoverable presentation cancellation remains separate work.

Asynchronous driver/submission errors that are not propagated to these calls keep
their existing policy. A successful void Present/Record return is not a new checked
native receipt. The change does not add a complete Vulkan completion/error registry.

## Deterministic evidence

The final candidate and production suites each pass 84 returning paths, 114
terminal paths and eleven exact controls, all repeated three times. Fourteen cases
that may return (successful draw or explicitly handled error) run on three selected
slots with/without internal source Finish: applet Configure and composition failure,
normal/Lanczos/CRT, paused enqueue, active-worker shutdown, capture boundary,
three simultaneously retained target slots with enqueue/partial submission failure,
Record before/after enqueue, partial/full submission. Recovery paths check that
retirement finished modeled host/native work, original error propagated, image
index did not advance, and a permitted direct retry completes a fresh composition.
Successful applet error retirement still refuses output and replay.

Nineteen terminal cases per slot/split combination cover retirement before/partial/
worker failures, pending/error native receipts, presentation draw/flush/Present
failure, closed Blit/direct entry and metadata-changing closed requests, callback
begin/clear/pipeline/push/bind/draw/end faults, and failed shutdown idle. All terminal
children require exact stages and exit 86. No destruction after the target borrow,
owner release or atexit may occur. Safe pre-borrow pass cleanup during fixture
preparation is distinguished from forbidden post-borrow release.

Eleven controls omit checked retirement/native receipt, weaken either presentation
catch, unwind either callback catch, add a normal Finish, advance the slot early,
omit Blit admission, omit direct Window admission or remove applet isolation. Each
must produce its named runtime assertion at exit 91; build failures, crashes and
timeouts never qualify as witnesses. The applet and presentation call chains use
the actual extracted handlers, not only an independent state-machine model.

Actual extracted bodies: complete WindowAdaptPass::Draw with callback, complete
BlitScreen::DrawToFrame, real Frame and PresentPushConstants declarations, actual
Scheduler::FinishNativeBorrow, the exact Composite draw/flush/present guard block,
complete RenderAppletCaptureLayer and actual readback refusal prefix. ConfigureDraw
and its internal source Finish are collaborators in this suite; actual Layer/helper
code is covered by its separate regression. Resource allocation/use graphs,
recording/native execution, source ownership/ticks, preparation, applet allocation/
readback success tail and presentation/pool collaborators are controlled. The
native completion query is a fixture; its actual timeline/fence protocol has a
separate native source/fallback regression. No device command queue, full real
Composite runtime, guest Memory/GetPointerImpl or game/pixel acceptance is claimed.

Real std::jthread and semaphore gates pause owner enqueue and deferred callback
work. Active shutdown closes entry, observes a blocked worker join, resumes the
callback, joins, then performs modeled checked idle before owners may release.
Actual renderer/worker shutdown and presentation drainage are separate regressions.
No arbitrary concurrent owner mutation or Draw-versus-Shutdown safety is inferred.

Regressions pass with three repetitions: post-upload helper (288/36/ten controls),
capture/native borrows, applet content (21/nine/six), native source ownership/fence
fallback (26 regular/seven protocol/seven terminal/fourteen controls), ten actual
renderer/worker shutdown paths, and Blit idle (four successful/eleven failure/six
controls). Exact manifests and input hashes are in the result file.

Three real Windows GCC 13.2 AMD64 COFF production objects compile with zero
diagnostics: WindowAdaptPass, BlitScreen and RendererVulkan. Real dependency headers
and a glslang-generated presentation shader are used. No application link, MSVC,
Linux/Android build, device/game run or current-head CI acceptance is claimed. The
integration workflow includes the fixed suite; no manual dispatch was performed.

[Results, hashes, commands and cleanup](M4_P2_COMPOSITION_FRAME_RETIREMENT_RESULTS.json)
retain current final-suite JSON/log evidence. Temporary executables, objects and
generated sources/headers from this turn, including superseded local candidates,
are removed after exact scope/hash verification. Superseded candidate results are
not acceptance evidence. Ignored local logs do not travel by Git; committed
results record the exact rerun inputs and outcomes.

## Next boundary and limits

Pre-ConfigureDraw resource recreation is not closed here. Blit exchanges image
count/format before all replacement resources exist; SetWindowAdaptPass clears
layers, and PresentManager::RecreateFrame publishes dimensions and replaces image,
view and framebuffer in-place. Allocation failure, child/parent replacement order,
same-request retry and image-count/index shrink need their own deterministic
publication/retirement proof. Only closed-entry admission for that branch is tested
here; resource preparation itself is a collaborator.

HostMemory remap/protection remains high risk. M4 incomplete, F2/F3 open and
GuestBackingAdmission Unknown. Failed output never becomes readable merely because
its native prefix was drained, a scope ended or a slot index stayed unchanged.


Follow-up: [Frame recreation publication](M4_P2_FRAME_RECREATION_PUBLICATION.md)
closes the selected-Frame construction/replacement and Blit metadata/shrink
boundary. Per-Frame format repair is independent of the global pass key. The
borrowed-output retirement and presentation failure policies above are preserved.
