# M4 P2: active NVDRV, Host1x, cohort and GPU owner composition

Date: 2026-10-26 (Asia/Taipei). Reviewed baseline:
`ae5e649630419625693dcdae9c88b601f7226829`, PR #375,
`codex/issue-372-fixes`. CI is deferred; main is untouched.
[Commands, hashes, logs and cleanup](M4_P2_NVDRV_COHORT_GPU_RESULTS.json).

## Result and evidence boundary

The actual NVDRV Module/session/retirement graph, Host1x action execution,
ProcessCohortStop and GPU ThreadManager now participate in the same active
scenario. Eight schedules and eight precise assertion controls pass x5 on Windows.
The original nvhost owner suite passes x3. Original Audio/Memory/GPU composition
also passes x3 after adding the reusable emission entry points.
Seven related production translation units compile to real AMD64 COFF objects.
No new production defect was reproduced; changes are confined to tests and
documentation. Successful named lifetime joins do not enable guest backing.

This is not a full application, native KProcess/kernel scheduler or Vulkan test.
There is one shared fixture System and KProcess/refcount/backing/PTE/backend
collaborator, not incompatible duplicate owner declarations. The actual
Module, Container/session, nvhost_ctrl, ProcessCohortStop, ProcessExecutionStop,
roster, PhysicalCore execution bodies, ThreadManager/queue/acknowledgement/Stop,
SyncptIncrManager and SyncpointManager types/bodies are composed around it.
KEvent Signal, renderer/native execution, SMMU/NvMap and unrelated device builders
remain explicit collaborators; optional application heap preallocation is omitted.

## Same active owner chain

1. Actual Module::Open and Ioctl1 register a real nvhost_ctrl wait. Actual NVDRV
   session retirement prepares Module/Process/backing owners before publication.
2. The real ThreadManager worker executes a test-scheduled callback calling
   actual SyncptIncrManager::Increment/IncrementAllDone. These increment guest
   and host syncpoints and invoke the registered raw nvhost_ctrl callback under
   SyncpointManager::guard. This is a scheduling fixture for the actual increment
   path, not a claim to execute a GPU command stream or Vulkan fence manager.
3. Signal pauses at a fixture gate while the worker and raw callback are active.
   Release external Module/facade roots: actual NVDRV::~NVDRV retires its session
   record, whose final Module destruction clears devices and waits for that
   callback. The actual retirement entry stays counted through the blocked join.
4. Attempt actual KProcess::TryStopProcessCohort. Its NVDRV stage remains Pending,
   refuses new NVDRV entry and leaves inner Host1x/Memory domains open so the outer
   operation can finish. Backend acknowledgement precedes this stage.
5. Release Signal and wait for actual final retirement. Hold the GPU callback's
   remaining tail: Module/KEvents may now have retired their final use, but the
   worker itself is still live. BackendsStopped is insufficient for the joint
   lifetime oracle; require actual callback return and ThreadManager::Stop join.
6. Remove the external allocation root. The actual retained backing type and
   cohort Process owners remain until the closure object is destroyed; then the
   checked fixture references/storage retire. Effects occurred, so this path
   never calls the no-effects reopening protocol.

The joint lifetime oracle is test-only. It checks the named owners/joins; it is
not a new production completion API or an admission capability. System/Kernel/
Host1x facade lifetime is continuously provided by the caller's fixture scope,
not inferred from a Module shared_ptr.

## Schedules and required counterexamples

| Schedule | Required observation |
| --- | --- |
| normal | Cohort remains Pending through final Module callback join; GPU tail/join is independently required |
| gpu-stop | Stop can wake the submitting caller before the active worker returns, but cannot complete its join while Signal/tail are held |
| backend-active | Hold actual PhysicalCore at fixture backend UnlockThread; NVDRV closure cannot begin before backend acknowledgement; it is still Pending afterward |
| timeout | Exhaust the bounded cohort poll budget while retirement is active; later callback/GPU joins cannot reopen Unknown |
| late-seal | Seal NVDRV after the named joins; stopped-state revalidation becomes Unknown and refuses cancellation |
| queued-cancel | An inert GPU blocker holds a queued callback owning the last real NVDRV facade. Stop cancels it without incrementing syncpoints; its session record defers under the closed cohort. Only after proven cancellation with no guest/native writes may actual CancelNoEffects reopen the subset and the real queue drain final owners |
| subset-gap | With no active retirement entry, actual cohort reaches BackendsStopped while GPU/Host1x Signal is still active. GuestBackingAdmission must remain Unknown. Finish the worker, then retain the deferred actual owner graph under a sealed domain; do not invent terminal disposal |
| escaped-alias | A pointer returned by actual GetPointerImpl before close still writes a witnessed scratch byte afterward; its backing remains owned. This is a coverage-negative witness and must not grant admission |

The subset-gap case explicitly emits its witness and exits the test process
without unwinding the retained Unknown graph. This prevents fixture System/Kernel
destruction beneath opaque deferred owners and is not a production cleanup
solution. It is recorded separately from successful named lifetime completion.

Eight controls each require exit 91 and the specific assertion (x5):

- Skip actual cohort NVDRV drainage: reject acknowledgement of active final
  retirement.
- Replace the session record's Module owner with a borrow: the final destructor
  can no longer be hidden outside the counted retirement interval.
- Treat cohort acknowledgement as GPU join: reject readiness while the real
  worker tail is paused.
- Replace queued GPU facade ownership with a raw pointer: reject owner loss
  before allowing the queued callback to run or dereference it.
- Close inner Host1x before outer retirement: reject an inner writer closure
  that prevents the intended dependency order.
- End Retire's counted entry before record.reset: reject premature cohort
  acknowledgement during actual final Module destruction.
- Replace GuestBackingAdmission Unknown with DrainedSubset: reject admission
  in the active Host1x/GPU counterexample.
- Replace actual retained backing ownership with a weak reference: reject
  backing loss after external allocation roots are removed.

Timeouts, generic crashes, compiler errors or different assertions do not pass a
control. Generated source/header hashes, fixture headers and runtime logs are
retained. Reused CPU prerequisites require exact production-input hashes; GPU,
cohort and nvhost emitters regenerate their current actual bodies for every run.
Default suite behavior is preserved and tested separately from emission mode.

## Windows validation and next M4 boundary

The seven fresh real production objects are ProcessCohortStop,
ProcessExecutionStop, GPU ThreadManager, Host1x SyncptIncrManager, nvhost_ctrl,
NVDRV Module and Container. Compilation uses pinned real dependency headers,
without fixture headers; no application link or native runtime is asserted.
Generated executable/object/fixture files are removed after exact path/hash
checks, while committed results keep the evidence needed to review this slice.

This advances the previous separately active NVDRV/Host1x proof into one actual
cohort/GPU lifetime graph. P2 is still incomplete: full native KProcess/KEvent
scheduling, System retention through arbitrary closed/Unknown record lifetimes,
all service/direct/escaped owner routes, every writable alias and mapping freeze,
and native GPU submission/completion/visibility remain independent requirements.
Next is a restricted real KProcess-backed composition with those continuous
owners and explicit refusal for missing native/mapping coverage, before any
Vulkan Copy acceptance. Do not turn the fixture lifetime oracle or
BackendsStopped into guest exclusion. F2/F3 remain Open; GuestBackingAdmission
remains Unknown. No production repair or admission change was justified by this
round, and CI remains deferred.
