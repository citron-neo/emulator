# M3B-F2 restricted eligibility: blocked before production activation

The requested acceptance is **not achieved**. An ordinary, unbound production scalar
call still cannot establish complete producer exclusion before handoff acceptance.
No automatic Memory::Write routing or weaker qualification switch is enabled.

This follows the fallback in the acceptance requirement: record the concrete blocker
and advance producer isolation first. The [producer inventory](M3B_F2_PRODUCER_INVENTORY.md)
is the classification of access paths; it is not an executable exclusion certificate.

## The qualification that is required

A successful restricted attempt must establish, before Prepared becomes Accepted:

1. The calling ordinary scalar instruction, process and immutable payload are identified;
   it has not already performed a store or another irreversible effect.
2. Every physical page touched by coarse preservation is resolved with all CPU/device/GPU
   aliases and owning processes. Qualifying only the scalar address is insufficient.
3. Every overlapping producer and coherent reader is either admitted by the same physical
   protocol or excluded for the entire execution/publication interval. Previously returned
   raw pointers, already bound GPU buffers/images and submitted callbacks count too.
4. Mapping changes, alias teardown and kernel frame reuse obey that same lifetime ordering.
   A generation check or retained host allocation alone does not exclude these mutations.
5. The exclusion remains effective until checked completion, including while the CPU fiber
   sleeps. GPU progress never depends on a suspended CPU releasing a lock or running work.
6. Refused permits requalification only with certified no effects; Unknown retains execution
   and resource identities and never falls back to the original ordinary store or replay.

Only then may the ordinary caller use the existing immutable request, GPU continuation,
guest fiber suspension and short execution visits. Tests must exercise that caller entry
and rerun the original unbound F2 witness; explicit controlled-body success is insufficient.

## Why current production cannot satisfy this

| Blocker | Concrete missing authority | Consequence |
| --- | --- | --- |
| RE-CPU | Other cores, WriteBlock/unsafe copies/CAS, service and kernel producers have no common physical execution gate | Suspending the originating fiber does not exclude concurrent backing access |
| RE-FAST | JIT page-table/fastmem stores, NCE native stores and exclusives bypass scalar Memory callbacks | Disabling one optimization or watching a page is not a complete CPU exclusion proof |
| RE-RAW | GetPointer/GetSpan/NVMap/physical-pointer clients may retain mutable aliases | Pointer lookup completion does not prove the client has stopped accessing the frame |
| RE-GPU | Existing bindings, DMA, Texture Cache, query results and other engines do not all enroll | GPU owner serialization does not retire native commands or delayed host callbacks |
| RE-LIFE | Originating owner Close is proved, but every alias-process mutation/frame owner is not enrolled | Physical pins do not prevent kernel frame return/reuse or an independent alias owner |
| RE-ENTRY | Unbound Impl::Write has no executable certificate issuer or automatic handoff consumer | Explicit Prepare/Queue/WriteOrdinaryControlled tests cannot count as the requested ordinary-entry acceptance |

These are missing exclusion mechanisms, rather than merely overly conservative numeric
limits. There is currently no sound production certificate to issue to a game, so a game
eligibility rate cannot yet be measured. No ARM game/device run is claimed. A caller-supplied
“producers excluded” boolean, an allowlist of test processes, or a manually held fixture lock
would not close any of these blockers.

## First isolation slice: BufferCache legacy readbacks

This continuation narrows RE-GPU for one producer family without claiming complete closure:

- `ControlledReadbacksIdle` runs on the existing GPU/cache owner **before TryAccept**. It
  uses a nonblocking device mapping qualification to enumerate current physical aliases
  across the complete immutable preservation footprint. It checks uncommitted/committed
  flush ranges and all queued pending-download destination batches.
- A conflicting or unprovable snapshot cancels the still-unaccepted request. It neither
  submits preservation, consumes dirty tracking nor allocates a controlled transaction.
  Busy mapping/alias overflow is not treated as absence of a producer.
- Native completion is insufficient while its pending writeback identity remains queued.
  Even an invalidated pending copy conservatively needs queue retirement. Disjoint pending
  destinations do not require a global queue drain.
- Ordinary DownloadMemory/DownloadBufferMemory/WriteBackDownload reject conflicting
  Writing/Unknown registrations. Async scheduling checks its whole candidate range batch
  before ResolveScatter/AccumulateFlushes; retirement checks the entire front download batch
  before the first copy or bookkeeping change. These use the existing explicit exception
  rejection contract, with no synchronous GPU/CPU dependency wait or silent skipped write.
  A dedicated ControlledProducerConflict is caught by the real GPU loop's failure path:
  stop later dispatch, cancel/quarantine continuation captures, notify shutdown and retire
  queued acknowledgement fences. A conflict is not an unhandled runtime exception or retry.
- Vulkan retires async downloads on a separate GPUFencingThread under the cache mutexes.
  That worker catches the same typed conflict, stops without running its fence callbacks,
  and publishes an atomic failure to GPU. RunThread checks it before subsequent dispatch.
  FenceManager also latches the error for owner APIs: WaitPendingFences uses a retained
  promise with bounded failure polling instead of retaining stack references and waiting
  forever for a callback that the failed worker will never execute. Polling holds no
  cache/PTE/owner lease. No native-completion success is manufactured by this failure path.
- This does not qualify all GC, gather/scatter, CPU notifications, remaps, bound GPU work or
  native writes. New access outside these guarded entries remains blocked by the activation
  policy, not by a global implemented barrier. Exceptions are defensive isolation for the
  explicit endpoint, not a guest scheduler retry policy.

The original acceptance harness still explicitly owns and orders setup work. Its newly
queued legacy readback and subsequent denied request demonstrate this producer's real
scheduling boundary; the later successful controlled scalar remains an explicit test call.
It must not be relabelled as a production eligibility success.

## Validation record

- The original `run_write_coherency_witnesses.py --case f2 --require-fixed` was rerun after
  the isolation changes: `F2 expected=77 observed=10 backing=77`, exit 1. **F2 remains Open.**
  Its unmap/mixed-page preservation prerequisite cases passed. The witness was not rerouted
  through controlled test helpers or modified to hide the ordinary-entry defect.
- New host cases cover queued, committed and uncommitted readbacks through another device
  alias on another page of the coarse footprint, busy mapping rejection and disjoint queues.
  Three repetitions and all existing controls plus mutation 95 passed. Mutation 95 removes
  the pre-accept exclusion and demonstrably accepts the forbidden request/fails the test.
  The real GPU-loop extraction also passes typed conflict shutdown/fence retirement, and
  real ThreadManager topology passed three repetitions plus its dependency-cycle control.
  The complete production FenceManager template, with native fence/cache/platform shims,
  passed ten repetitions of conflict notification, rejected waiter/late callback and join.
  This host topology check is distinct from the native Vulkan cache/readback acceptance.
- The native executable adds a real Vulkan async download, keeps its completed copyback
  queued and checks that a real guest consumer observes Cancelled with zero store publications.
  After local retirement, existing controlled acceptance can proceed. A parked accepted request
  also probes real download/writeback/scheduling entries for rejection before side effects.
- Final Linux validation at `46687f1bfa` passed the complete workflow in
  [38873324462](https://github.com/Splaser/emulator/actions/runs/38873324462), including 1,708
  native assertions, 130 production test cases and all configured integration regressions.
  Android production compilation passed at `3ebb655b97` in
  [38870993595](https://github.com/Splaser/emulator/actions/runs/38870993595). Production `src/`
  is identical between these revisions; the follow-up fixes a renderer extraction fixture
  and validation workflow/docs. The Linux-only rerun intentionally skipped Android.
- Latest host/native results and exact workflow revision are recorded in
  [current progress](../tools/physical_coherency_tests/M3B_PROGRESS.md). Earlier green builds
  do not validate this continuation's new code.

## Next production gate

Continue with enforceable physical execution/alias ownership for RE-CPU/RE-RAW/RE-LIFE and
remaining RE-GPU producers. A producer must either acquire a physically keyed permit for
its real execution/deferred lifetime or be provably unavailable for the candidate mapping.
The authority issuing a restricted certificate must own those exclusions, not trust assertions
from the scalar caller. Then connect the real ordinary scalar entry and require the original
F2 witness to pass before recording restricted production acceptance.
