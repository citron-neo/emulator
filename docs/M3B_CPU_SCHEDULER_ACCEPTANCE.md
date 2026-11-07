# M3B CPU scheduler acceptance and lock audit

The native acceptance target is `controlled_cpu_scheduler_tests`. It links production
CpuManager, KThread, KScheduler, KProcess, PageTable, GPU ThreadManager, Vulkan Rasterizer,
BufferCache, checked submissions and transfers. The renderer omits presentation; CI supplies
software Vulkan. The CPU entrypoint is a production KThread service fiber, not an ARM game
image. No ordinary game Write call is automatically enrolled.

## CPU consumer contract

`TryQueueCpuWrite() == true` acknowledges only insertion into the GPU command queue. The GPU
owner may later discard a continuation because channels are suspended or all 64 continuation
slots are occupied. The guest must observe the request's terminal action after enqueue:

| Request outcome | CPU scheduler action |
| --- | --- |
| Prepared / Accepted | Await on the guest timer; no second store or replacement request |
| Stored | Complete; the original store and tracker publication have finished |
| Cancelled | DoNotExecute; this request was never accepted |
| Refused with certified no effects | ReprepareCertified; only a freshly qualified attempt or independently proved path is permitted |
| Unknown | Quarantine; retain resource/execution identities, prohibit replay, await trusted recovery |

Observation interrupted by guest termination returns no terminal action. It does not certify
cancellation, completion or retry. An accepted request cannot be cancelled by the guest. Close
seals the Memory execution gate before page-table destruction, cancelling Prepared requests and
quarantining Accepted ones. Unknown keeps a sealed owner identity without keeping Memory or
KProcess alive; later visits cannot recover their pointers. Owned physical storage alone is
not permission to write a kernel frame after the process has returned it to its allocator.

## Dependency and execution boundaries

| Stage | Actual synchronization and work | Dependency wait policy |
| --- | --- | --- |
| CPU preparation | Cold context/PTE snapshot and owned physical view; bounded owner registry | Before suspension; no GPU queue/fence wait |
| Enqueue | One queue try-lock/TryEmplace | Queue acknowledgement is not acceptance or completion |
| Preaccept buffer-readback exclusion | Current mapping try-guard, all coarse-footprint physical aliases, flush ranges and pending destination batches | Refuses before acceptance; no native wait or Memory visit. This excludes one producer family, not all production access |
| Acceptance | One owner try-lock, CAS Prepared to Accepted, bounded strong identity retention | Races Close under the same lifetime gate; failure leaves an unaccepted request |
| GPU continuation | Cache try-lock; one admission/preservation poll | Park/Pending returns and releases the cache; later ordinary commands can supply progress |
| Mapping qualification | Device mapping try-guard against the immutable plan's epoch/generation | A contended/stale mapping refuses or quarantines; no blocking rewalk for requests |
| Checked device completion | Checked submission state and zero-time timeline poll | No CPU producer callback or native completion wait |
| Preservation backing copy | Mapping try-guard plus short owner visit | At most 64 pages / 256 KiB; no submit, allocation, tracker callback or suspension in the visit |
| Ordinary scalar store | Short owner visit; context and PTE shared try-locks; scoped ordinary Write | At most eight bytes, one attempt; binding precedes legacy/sys-core pointer resolution |
| CPU tracker publication | Existing bitmap managers, retaining existing protection/cache references | No allocation or device range-counter/PTE callback; outside the Memory visit |
| Gather/range bookkeeping | GPU-owned cache metadata/container reclamation | Outside the Memory visit, including allocator/pool work |
| Final completion | Short owner visit; one range-domain TryComplete; terminal publication | No native/GPU/kernel callback, blocking metadata lock or cache-container reclamation |
| Process Close | Exclusive owner gate, then process TLS/PTE finalization | Joins only entered bounded visits; neither GPU queue nor device completion is drained |
| Slab finalization | Explicit Memory Finalize releases Impl/context and the ordinary owner reference | Required because KProcess slab destruction skips C++ member destructors; Unknown retains its sealed identity |

Range admission has short internal metadata mutex critical sections. It does not park a
thread behind a conflicting predecessor: TryStart returns Park. Ticket/resource reclamation
does not run GPU/CPU callbacks under that mutex. The owner visit uses TryComplete so it cannot
wait for even that metadata mutex. This is a dependency/lock-order audit, not a hard real-time
latency guarantee against OS preemption or driver internals.

The previous request path called MarkRegionAsCpuModified inside its owner visit. That enters
WordManager notification, DeviceMemoryManager::UpdatePagesCachedCount's range lock, and
Memory::RasterizerMarkRegionCached's PTE mutation. Controlled request publication now retains
the existing tracking reference instead. Upload/ordinary untracking still balance that
reference, as checked by the tracker unit test.

## Native assertions

The executable checks:

- A real guest KThread enters Waiting through the kernel timer, switches through KScheduler
  fibers, resumes and consumes Complete, Cancelled, certified Refused and Unknown separately.
- The actual cache mutex can be held by a CPU producer waiting for a later GPU marker. The
  continuation returns without acceptance; the marker runs; releasing the cache permits work.
- Actual sys-core and counter locks remain held while a real Vulkan fill, preservation and
  ordinary CPU77 store complete. Adjacent bytes remain GPU99; repeated queue attempts do not
  increase the physical store publication sequence.
- An accepted request parks behind real physical admission. With its real PTE lock contended,
  it becomes Unknown and a later GPU marker still runs.
- Legacy publication and PTE mutation on the same objects provide negative witnesses: the
  marker stalls until the CPU deliberately releases the corresponding lock. Tests release
  these locks before reporting failure so the witness itself does not hang the run.
- Sixty-four initialized processes allocate heap/TLS/PTE resources, suspend a guest fiber,
  terminate and finalize. PTE/context tokens retire, later execution visits refuse, and a
  Cancelled owner's weak reference expires after its queued capture retires.
- With the actual Vulkan submission mutex held, an accepted native request's process releases
  its final thread references and retires its PTE within the exit deadline. Unrelated GPU work
  still runs. After a simulated physical-frame reuse, delayed GPU99 preservation and scalar
  store must leave sentinel55 intact, including after trusted native completion.
- Filling all 64 continuation slots causes a successfully enqueued new closure to be dropped.
  Its Prepared request becomes Cancelled, and the actual guest consumer observes that result.

The host extraction suite adds mutations 82-90/92-94. In particular, 93 removes Pending-to-
Unknown native retention, while 94 removes acceptance-time identity retention before a
transaction-allocation exception. Unknown cannot be completed/cancelled or replayed in either
case. These mutations complement rather than replace the native executable.

## Evidence and remaining activation scope

Final lifecycle hardening passed both Android and Linux at `02610e1766` in
[38864622309](https://github.com/Splaser/emulator/actions/runs/38864622309). The downloaded
CTest log records 1,696 assertions in the native acceptance executable (0.82 seconds) and
3,878,778 assertions across 130 production test cases (6.34 seconds). Configured integration
regressions also passed. The earlier full native run passed at `202eb74500` in
[38861935399](https://github.com/Splaser/emulator/actions/runs/38861935399).

The [producer inventory](M3B_F2_PRODUCER_INVENTORY.md) records the paths that still require
exclusion or enrollment before restricted application activation.
The subsequent [restricted eligibility record](M3B_F2_RESTRICTED_ELIGIBILITY.md) documents the
blocked ordinary-entry acceptance and the added readback/fencing-worker isolation slice.

This acceptance covers the explicit controlled endpoint in the tested multicore topology.
Automatic ARM/NCE game-memory routing, complete legacy producer exclusion, all alias-process
lifetime qualification, trusted Unknown recovery and physical Android-device evidence remain
separate activation gates. The unbound ordinary F2 witness remains Open.
