# nvhost_ctrl lifecycle review and named M4 owner composition

The subsequent [2026-10-26 joint fixture](M4_P2_NVDRV_COHORT_GPU_COMPOSITION.md)
now exercises the restricted cohort/Module/GPU composition target identified
below. Its controlled evidence does not close native System/KProcess, mapping
or GPU completion obligations.

Reviewed source: `44c6fbdccc67b76046df8a75fe1be209ff091956`, 2026-10-25
(Asia/Taipei). This follow-up changes tests and documentation only. CI remains
deferred and main is untouched. Results are stored under `owner_review_44c6fbdccc`
in [the callback results record](M4_P2_NVHOST_CTRL_CALLBACK_RESULTS.json).

## Review outcome

No additional production defect was reproduced in the requested lock-order,
duplicate-cancellation or final-owner paths. The following ordering is required
and is preserved by the current production implementation:

| Path | Locks and release order | Review result |
| --- | --- | --- |
| Register a wait | events_mutex -> SyncpointManager::guard for queued registration; an immediate callback executes synchronously under its dispatch owner | A zero handle denotes synchronous completion, not a missing asynchronous join |
| Execute a queued callback | guard -> KEvent::Signal -> kernel scheduler lock | The nvhost_ctrl callback never acquires events_mutex |
| Clear a wait | events_mutex -> guard for deregister/join; guard is released before KEvent::Clear takes scheduler lock | Waiting and Signalling both join by handle before publishing Cancelled; UpdateMin remains conditional on the old Waiting state |
| Destroy nvhost_ctrl | No events_mutex across callback joins; deregister every registered handle, then free any KEvent | Internal event storage, EventInterface and Module providers remain available until the last callback has returned |
| Retire queue captures | Move completed/cancelled functions out of action storage and destroy them after guard unlock | A final facade/Module capture may re-enter the same manager during device destruction; releasing it under guard would deadlock |
| Retire final session owner | OwnedRetirementQueue releases its mutex before effects/capture destruction; counted ProducerDrain entry spans record.reset and final Module destruction | A waiting destructor is still an active retirement operation, even after the session retirement callback itself returned |

Module::~Module clears open_files in its body before EventInterface,
ServiceContext and Container member destruction. Container sessions retain their
Process/backing until device callbacks have joined. NVDRV::~NVDRV drops the
facade's extra Module reference before its prepared record retires; the queue is
retained separately. Native KEvent readable/writable closes and scheduler locks
were inspected, but are still controlled collaborators in these runtime tests.

Repeated Clear is serialized by events_mutex, clears wait_handle after the first
join and may safely call DeregisterHostAction with zero. A completed nonzero
handle is also harmless: the guarded lookup finds no stored action. Cancelled
numeric identities are not reused by the manager. EventState is not substituted
for the guard join. No concurrent destruction versus an unowned ioctl is made
safe by this repair; caller ownership remains necessary.

## New deterministic schedules

Six new schedules extend the existing ten, for sixteen positive cases:

1. Clear twice, replace/rearm the event at a later threshold, advance through the
   cancelled threshold, complete once, then Clear twice again. Count nonzero
   cancellation attempts and retire each KEvent once.
2. Hold a callback in Signal while two Clear operations target the same event.
   Join both operations before final owner release; only one uses the old handle.
3. Put the last real NVDRV facade owner in another Host1x action capture, then
   complete that action. Its capture destruction must release Module outside
   guard so device destruction can cancel the pending raw callback.
4. Cancel that final-owner action instead, then cancel its old handle again.
   The same owner reentry and exactly-once retirement requirements apply.
5. While final facade retirement is blocked joining Signal, close the real
   NVDRV dispatch domain. It must remain Pending until callback and final Module
   destruction complete, then report only DrainedSubset.
6. Start Signal, close an idle dispatch domain and observe DrainedSubset despite
   the active Host1x callback. Releasing facade roots now defers the actual
   session record, which must retain Module/Process/backing and all KEvents.
   Reopen only the controlled fixture's managed subset, drain the deferred record
   on another thread, close again and require Pending through the real callback
   join and final owner destruction.

The sixth case deliberately emits the counterexample:
`dispatch DrainedSubset with active Host1x callback; admission Unknown`.
It does not turn an idle managed counter into full Host1x exclusion. The tested
subset reopening has no guest store or native GPU operation; it is not a whole
guest no-effects/cancellation receipt.

All sixteen positive cases, one injected terminal cancellation case and nine
exact assertion controls pass x10. Three historical production failures still
reproduce x3 after instrumentation. Five new controls retain a cancelled handle,
retire completed/cancelled final-owner captures under guard, or release either
Retire/Drain's counted entry before record.reset. The two under-guard mutations
are rejected before reentrant destruction can hang; timeouts/crashes/compiler
failures are not accepted witnesses.

The observer is a stack scope created inside each actual unique_lock. It tracks
guard ownership on that thread, without replacing the real mutex or moving
callback execution. Header controls shadow only OwnedRetirementQueue with its
specific early-release mutation. GCC 13 emitted a maybe-uninitialized warning
when a deliberately early optional reset was inlined in the first draft mutant;
the final mutant moves that reset to an out-of-line helper and retains -Werror.
That draft compiler failure is retained separately and is not an accepted result.

No production file changed. The seven production translation units compiled in
the preceding Windows revalidation retain their checked input hashes. This run
does not claim a new production compilation or link.

## M4 progress and next boundary

This advances P2 from isolated callback/destructor tests to a named actual
NVDRV Module/session/retirement-record -> Host1x action -> KEvent owner chain
while closure is attempted. It closes the test gap around counting final owner
destruction itself, not a new whole-service or reversible-stop capability.

The earlier [Audio/Memory/GPU composition](M4_P2_OWNER_CLOSURE_COMPOSITION.md)
still runs with idle NVDRV/Host1x domains; these separate active schedules do not
retroactively make that a single joint live graph. The next composition target
is one restricted retained session with actual ProcessCohortStop, this concrete
Module/retirement graph and the real GPU ThreadManager, including a callback
active while the cohort closes. Keep a negative control that omits downstream
Host1x activity and still refuses admission. Native KProcess/KEvent scheduling,
complete System lifetime through closed/Unknown records, escaped device/alias
owners, mapping freeze and native completion remain independent open obligations.
No direct guest backing or F2 transaction is enabled. Global P2 is incomplete;
F2/F3 remain Open and GuestBackingAdmission Unknown.
