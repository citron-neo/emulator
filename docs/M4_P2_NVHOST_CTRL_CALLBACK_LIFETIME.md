# nvhost_ctrl Host1x callback lifetime repair

Validated 2026-10-25 (Asia/Taipei), resuming PR #375 from `31b25c128f` on
`codex/issue-372-fixes`. Production changes are confined to
`src/core/hle/service/nvdrv/devices/nvhost_ctrl.cpp`.
Machine-readable commands, source/object hashes, observations and cleanup are in
[the results record](M4_P2_NVHOST_CTRL_CALLBACK_RESULTS.json).

Follow-up: the [lock/cancellation/final-owner review](M4_P2_NVHOST_CTRL_OWNER_COMPOSITION.md)
adds six deterministic schedules and five controls without production changes.
Sixteen normal cases/one terminal case/nine controls pass x10; actual retirement
counting must span final Module destruction and callback joins. A drained NVDRV
counter with a live Host1x callback remains a negative admission witness.

## 2026-10-25 local revalidation at 4a66873eb6

Source `4a66873eb6aaa01c70b86ce04baeda346d5b8f6a` was revalidated on
Windows without changing source, workflow or main. No new nvhost_ctrl lifecycle
defect was reproduced in the named checks:

- Ten normal callback/owner schedules each pass x20 (200 runs).
- Injected cancellation failure passes x20 with exit 86 before dependent owner
  unwinding; four negative controls each produce their exact exit-91 assertion
  x20 (80 required failures).
- Eight syncpoint retirement schedules and seven controls pass x10. NVDRV session
  retirement, dispatch and OwnedRetirementQueue positive suites pass x10; their
  existing mutation controls each pass once, as implemented by those runners.
- The same seven real Windows production units compile to AMD64 COFF objects.
  Manifest inputs and object hashes were checked; all 148 generated exe, object
  and fixture source files from this revalidation were removed. Logs and manifests
  remain locally, and their manifests, observations, log contents deduplicated
  by hash, commands and cleanup records are embedded under
  `local_revalidation_4a66873eb6` in the results record.

The first owner-suite compilation failed before C++ test execution: the output
path had a Windows extended-path prefix and GCC could not find the generated
production.cpp/terminal.cpp paths. The unchanged command passed after the output
directories existed. The initial compiler log is retained as an infrastructure
failure, not a lifecycle witness; the precise path-conversion mechanism remains
unproven. No source fix was made for this observation.

This remains controlled Windows concurrency testing and production-object
compilation, without application linking, native KEvent scheduler/runtime,
Linux/Android, device/game or sanitizer validation. CI is deferred at the user's
request; no workflow is dispatched or Actions setting changed by this
documentation update. The earlier follow-up `4a66873eb6` only connected the named
suite and evidence upload to the integration workflow; no successful remote
validation is claimed. Global P2 remains incomplete; F2/F3 Open and
GuestBackingAdmission Unknown.

## Reproduced failures and repair

Historical production source `33ca3eb2ad540b669deed958eedb751be9cafb20`
reproduces three named assertion failures, each five times:

| Schedule | Historical observation | Fixed ordering |
| --- | --- | --- |
| Queue an unsatisfied host action, release final facade/Module owners, then increment | `queued callback used retired target` before dereferencing the retired device | Remove the device's registered actions before releasing KEvents/device storage |
| Block actual callback inside fixture Signal, release the final owner on another thread | `event freed during active Host1x callback` | DeregisterHostAction waits on the real queue guard while callback execution owns it |
| Block callback after Waiting became Signalling, issue ClearEventWait | `clear overtook active callback` | Join by handle even when the previous status was Signalling; then publish Cancelled and Clear |

The destructor uses two passes: deregister/join all registered event handles,
then release all KEvents through the still-live EventInterface/ServiceContext.
It holds no events_mutex during this join. A cancellation exception calls the
existing TerminalFailStop before unwinding dependent owners. The injected failure
exits 86 with `stage=nvhost_ctrl.callback_join`, without Module/context destruction
or atexit. This is an injected conservative failure boundary, not a claim that
normal deregistration throws.

ClearEventWait retains its existing events_mutex. The callback never takes that
mutex; its execution is serialized by SyncpointManager::guard. Clear now waits
for that execution before clearing the KEvent or allowing slot reuse. Waiting
still triggers the existing UpdateMin behavior; other statuses do not.

EventState is not a completion acknowledgement. SyncpointManager executes queued
callbacks under guard, and DeregisterHostAction removes a pending callback or
acquires guard after an active callback returns. Captured-owner retirement stays
outside guard as implemented before this repair. The immediate registration path
runs synchronously inside the owning dispatch and is tested separately.

## Owner chain and deterministic tests

The suite extracts complete Module, Container/session and nvhost_ctrl class
declarations, actual constructors/builders, dispatch, retirement factories,
NVDRV destructor and device methods. It composes these with the real
SyncpointManager implementation/header, OwnedRetirementQueue and retained backing
types. Only optional application heap optimization is omitted from OpenSession.
Forward declarations and registration observation missing from the saved draft
were repaired before accepting a witness. Compiler failures, crashes and timeouts
are never accepted as historical or mutation witnesses.

Gates hold Signal while final-owner retirement or Clear reaches deregistration.
Observations run before fixture storage can be dereferenced after destruction.
The active case releases the caller's Module reference, then destroys the facade:
the actual retirement record's shared Module owner drives Module::~Module,
open_files.clear and device destruction. Process/backing remain retained through
the join by the actual Container session and retirement graph. In closed-deferred
mode the dispatch domain is closed, facade owners are released, and the real
retirement queue must retain Module/Process/backing until resumed and drained.
No prepared replacement owner packet stands in for these relationships.

Ten positive schedules cover queued, active, multiple slots on one device while
active, Clear while active, immediate registration execution, completed callback,
closed/deferred final retirement, two independent devices, throwing Signal,
and FD close. Candidate passes each x5; final production source passes each x10.
The injected terminal cancellation case passes the same repetition counts.
Four controls must produce exact exit-91 assertions: omit cancellation, free
before cancellation, borrow the retirement Module instead of owning it, and
restore the historical Clear path. Candidate and applied production text match.

Related Windows regressions pass x5 with their assertion controls:

- Eight syncpoint capture-retirement paths and seven controls.
- NVDRV session/GraphicBuffer retirement and all existing owner/failure controls.
- NVDRV dispatch admission/lifetime/exception controls, including Initialize.

Seven real Windows production translation units compile to AMD64 COFF objects:
nvhost_ctrl, Host1x syncpoint_manager, nvdrv, nvdrv_interface, Container, KEvent,
and kernel_helpers. The native compiler uses real pinned dependency headers;
there are no fixture headers in these production builds. This is object
compilation, without application linking or native runtime validation.

## Limits and continuation

System/Host1x facade, KProcess refcount, host allocation, SMMU/NvMap, unused device
builders, KEvent and ServiceContext remain controlled collaborators in the runtime
suite. Real KEvent::Signal takes the kernel scheduler lock and CloseEvent drops
readable/writable event references; those production files were inspected and
compiled, but native kernel scheduling was not exercised by the fixture.

The repair relies on existing owner-mediated dispatch: no new ioctl may race
destruction of its C++ owner, and System/Host1x/Kernel must remain alive. Escaped
raw/direct owners and arbitrarily late operations after System destruction are
not covered. Current production GetDevice call sites do not export nvhost_ctrl.
No Module/session/queue/KEvent implementation changes were needed for the
reproduced failures. The original production repair did not modify another
teardown subsystem, dependency pin, main branch or workflow. The later CI-only
test wiring is described above. No Android build or device/game run is claimed.
Global P2 remains incomplete, F2/F3 Open and GuestBackingAdmission Unknown.
