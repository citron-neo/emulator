# M4 P2: deferred-frame handoff audit and action-owner retirement

2026-10-23 (Asia/Taipei). Baseline `f029ee7bd36b7a1fe3fdd89c2501e842be37b502`;
local, fork and PR375 heads agreed before work. Work stays on
`codex/issue-372-fixes`; main `b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`
is unchanged. This fixes a prerequisite capture-release boundary. No per-frame
source-owner packet or native lease is connected to production by this patch.

Follow-up: [per-frame source packet](M4_P2_FRAME_SOURCE_PACKET.md) now connects
source object retention through the production composer/nvdisp/GPU chain, building
on the capture-release boundary here. Mapping/native ownership remains separate.

## Handoff census and the blocker to adding source ownership

HardwareComposer keeps a GraphicBuffer in its cached BufferItem. It constructs
HwcLayer using descriptor/handle fields, nvdisp resolves an address and constructs
FramebufferConfig, and GPU::RequestComposite captures the configuration vector
in shared Request state. Neither data structure currently transfers the original
GraphicBuffer/retirement owner. WaitForSyncOperation acknowledges the registration
callback, while guest-fence actions can invoke renderer Composite later.
Dropping original layers/slots/cache can therefore end source ownership before
the deferred source read. This remains an unresolved source-owner handoff gap.

A direct shared_ptr insertion is insufficient as an implementation protocol.
The last captured owner can retire inside SyncpointManager. Its Increment invoked
an action under guard, then erased/destroyed that action under guard;
DeregisterAction likewise erased the cancelled function under guard. A captured
frame/resource owner's destructor can retire nvmap resources or re-enter guest
action registration, creating a lock dependency on that same manager. This is
a concrete existing captured-owner release defect and a prerequisite to a safe
per-frame owner packet.

Before production edits, actual SyncpointManager bodies reproduced precise
exit-91 witnesses for both executed and cancelled actions releasing their final
captured owner while holding guard. The observer stops before attempting the
reentrant lock; no hang or crash is counted as evidence. After repair, those
destructors actually register later actions and those actions execute normally.

## Repair without moving callback execution outside its existing lock

DeregisterAction now moves the matching function into a local declared before
the unique_lock, then erases the moved-from entry. The local's final capture
release follows guard destruction. Cancellation still never executes an action;
zero, missing or already-consumed IDs do not affect unrelated actions.

Increment declares a completed-function vector before its unique_lock. Under
guard it computes the ready prefix and reserves that many slots before any
callback invocation or entry erasure. Each successful callback still executes
under guard in the existing sorted action order, then its function moves into
the reserved vector before the entry is erased. Guard unlocks before completed
captures are destroyed. Move/erase of the emptied entries cannot cause their
last captured-owner release while guard is held. No completion queue, worker,
scheduler continuation, callback execution unlock or source pointer is added.

The advance counter still increments before acquiring guard, as before. A reserve
allocation failure can therefore leave a progressed counter with queued actions
intact and no callback executed. This is **not** a no-effect cancellation or
successful completion; the exception propagates. The new one-shot allocation
test requires that exact partial boundary and explicitly cancels, never replays,
the retained action. Allocation occurs only for a nonempty ready prefix; this
patch is not a performance benchmark or allocation-free completion mechanism.

If a later callback throws, earlier successful actions are already removed but
their captures still retire after guard unwinds. The throwing action remains in
storage under the existing exception policy. This patch supplies no authority
to retry it or infer no effects; the test removes it explicitly without replay.
Wait notification remains after successful callback processing. A waiter can
observe the syncpoint while completed capture destruction is still running;
WaitGuest/WaitHost are not owner-retirement/native-completion acknowledgements.

Cancellation still only removes a queued action. If another thread already ran
it, Deregister returning does not join that thread's newly unlocked capture
destruction. Manager/provider lifetime must span all those calls and releases.
Callback-body explicit resource destruction or self-reentrant manager calls
while guard is held remain separate; only final storage-capture release moves.

## Deterministic Windows evidence

`run_syncpoint_retirement_tests.py` extracts the entire production namespace and
class declaration. Actual register/deregister/increment/wait methods, vectors,
atomics and predicates run with real host threads and gates. The guard is an
observed wrapper over std::mutex with current-thread ownership, private members
are exposed for assertions, and condition_variable_any adapts the real wait bodies
to that wrapper. This instrumentation does not change the production mutex/CV
types; their actual definitions compile separately in the production object.

Payload/destructor/backing stand-ins are fixtures, not real GraphicBuffer,
KProcess, Memory/GetPointerImpl, HostMemory mapping or native renderer work. A
one-shot global operator new fault targets the completed-vector reserve before
callbacks. These are action ownership/lock tests, not complete frame pipeline,
device execution, physical content or native completion acceptance.

Eight paths run three times: completed capture with destructor reentry; cancelled
capture with reentry; exact host cancellation IDs/unrelated owner retention;
active callback versus cancellation; successful prefix plus throwing callback;
ready/delayed guest ordering and waits; shared owner across two actions; reserve
failure after counter advance. Seven exact controls run three times: release
completed/cancelled captures under guard; declare either retired local after the
lock; execute callbacks after unlocking; execute a cancelled action; discard an
unrelated action. Each requires its specific exit-91 message.

Regression: ten GPU request paths run three times, including normal synchronous
OnCPURead/RequestComposite, deferred/active presentation and cancellation, plus
the cancelled-read coverage control. Existing OwnedRetirementQueue production
runs three times with seven controls once. Blit idle four success/eleven failure
paths and six controls run three times. All passed.

Four actual Windows AMD64 COFF production objects compile with GCC 13.2, existing
pinned real headers and zero diagnostics: SyncpointManager, GPU, nvdisp and
HardwareComposer. See [results and current input hashes](M4_P2_SYNCPOINT_RETIREMENT_RESULTS.json).
No application link, native driver/game, Linux/Android or current-head CI acceptance
is claimed. Existing integration workflow gains the suite and artifact paths;
no manual workflow run is dispatched.

```powershell
$env:PYTHONUTF8='1'
$env:CXX='C:/Strawberry/c/bin/g++.exe'
python tools/gpu_lifecycle_tests/run_syncpoint_retirement_tests.py --baseline --revision f029ee7bd3 --output-dir .diagnostics/m4-action-baseline
python tools/gpu_lifecycle_tests/run_syncpoint_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-action-fixed
python tools/gpu_lifecycle_tests/run_tests.py --repeat 3 --output-dir .diagnostics/m4-action-requests
python tools/physical_coherency_tests/run_owned_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-action-queue
python tools/gpu_lifecycle_tests/run_blit_idle_tests.py --repeat 3 --output-dir .diagnostics/m4-action-blit
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/video_core/host1x/syncpoint_manager.cpp --source src/video_core/gpu.cpp --source src/core/hle/service/nvdrv/devices/nvdisp_disp0.cpp --source src/core/hle/service/nvnflinger/hardware_composer.cpp --output-dir .diagnostics/m4-action-production
```

## Next ownership protocol

The original source-owner omission remains: HwcLayer/FramebufferConfig still
carry descriptor addresses without an owner packet. Next define and test the
smallest immutable per-frame source handoff through deferred registration and
actual read, now with a lock-safe final capture-release boundary. Require tests
where every original layer/slot/cache owner is gone, multiple fences share the
request, registration fails partway, and shutdown cancels while a source callback
is active. A retained object alone does not freeze descriptor/mapping epochs.

Separate source-read lifetime from native upload/image/presentation resources;
do not release an owner merely because registration, a CPU callback or a cached
tick completed. Do not use the previous blit recreation idle fix as global VI
retirement permission. No per-frame native completion registry, mapping-use lease,
whole producer exclusion or physical coherency transaction is implemented here.
F2/F3 stay open, M4 incomplete and GuestBackingAdmission Unknown.

Current-run exe/object/generated fixture files are cleaned after preserving
JSON/logs and checking current input/COFF hashes. The committed results and
`.diagnostics/m4-action-results` preserve the handoff; main is not merged.
