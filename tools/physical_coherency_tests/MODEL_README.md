# M2 — standalone physical coherency reference model

Baseline `03b8ded878` (validated M1.1). C++20, standard library only, CPU-only;
not linked into the emulator or its build. DESIGN.md and M2_MODEL_SPEC.md remain
unchanged. Stop here before M3/M4 integration.

## State domains and public API

`model/coherency_model.h/.cpp` owns a small backing epoch, sparse authority cells,
simulated representations, causal operations, CPU admission leases and separate
chunk access states. `reference_memory.h/.cpp` is an independent test oracle.

| Domain | Identity / contents |
| --- | --- |
| Physical data | `(epoch, physical byte interval)`; never GPU VA/DAddr/manager |
| Mapping | View ID, generation, live/mutating state, ordered logical segments; manager/VA/DAddr are provenance |
| Authority cell | Current version, producer operation, retained holder, prior version, Unknown flag |
| Representation | Unique lifetime ID, physical-to-logical spans, private bytes and per-byte replica version tags |
| Operation | Monotonic operation ID, optional data version, causal predecessors, phase, retained predecessor operands/read resources |
| Chunk access | CPUReady/ClosingCPU/GPUPending/GPUActive/VisibilityPending/StartingCPU/Unknown, modeled uses and coherency obligation |

An initial CPU-backed cell spans the arena; actual write/restore boundaries split
it. Holes in logical views do not invent physical cells. Adjacent cells coalesce
only with equal authority/prior/error state and equivalent replica presence,
linear provenance and version tags. Replica version tags are byte arrays in this
bounded reference implementation; **authority cells are not one per byte**.

API groups:

- `RegisterView`, `GetStamp`, `RegisterRepresentation`, `Preflight`.
- `AcquireCPU(write/read, after, controlled)`, `CommitCPU(actual bytes)`,
  `ReleaseCPU` for a finished read or a provably unexecuted write.
- `ReserveDispatch(all bindings, after, queue)` returns tickets atomically after
  whole-group preflight. `ReserveGPU` is a one-writer dispatch shorthand.
  `Record`, `Submit`, `Complete`, `MakeVisible`, `Cancel` advance tickets.
- `Read` obtains a latest-version logical byte snapshot through a representation;
  `Synchronize` refreshes it, `Gather` preserves ordered repeated read segments,
  `Scatter` publishes only still-authoritative producer/version slices to CPU.
- `BeginMutation`, `EndMutation`, `Unmap`, `Remap`, `Evict`, `DrainRenderer`,
  `ReplaceBacking`, and whole-source `DMA`.
- `BeginGPUAccess`, `EndCPUAccess`, `UseChunk`, `BeginCPUAccess`,
  `PrepareCPUStart`, `StartCPUAccess`, `SetNonCoherent` model access obligations.
- `Inject`, `Status`, `ChunkStatus`, `VersionClock`, replica/cell introspection,
  `Statistics`, `Fingerprint`, `CheckInvariants` support executable assertions.

`Result.error` is typed; rejection is never successful fallback. A failed
admission/copy/allocation/budget request has no semantic state effects. Metrics
and fault consumption are diagnostic and excluded from `Fingerprint`.
`Unknown` deliberately changes quarantine state and retains bytes and leases;
there is no unproven automatic recovery. This is a serialized logical model,
not a mutex/thread scheduling implementation.

## Ordering, versions and publication

Operation IDs and data versions are distinct monotonic counters within an epoch.
Accepted writable reservations, completed CPU stores and DMA modifications
allocate data versions. A cancelled accepted write consumes an ID/version but
restores prior authority; identifiers are never reused. Read-only GPU dispatches
allocate operation tickets and retain operands without changing data versions.
Transfers, mapping changes, visibility and chunk handoffs create no data version.

Every physical write admission requires explicit `after` evidence covering the
current producer and conflicting outstanding GPU readers. Ancestor edges prove
transitive order. Queue numbers alone establish no ordering. GPU reservation can
depend on pending work; submission waits for predecessor submission, completion
waits for the actual declared predecessors. Unrelated disjoint work can complete
in either order. CPU/DMA cannot finish a store while an explicit dependency is
unfinished, even if it has an unrelated footprint. A synchronous `Read` is a
request for the current accepted frontier, returns Pending until its producers
complete, and needs visibility/CPU admission before returning CPU bytes.

Phases are Accepted -> Recorded -> Submitted -> Completed -> Visible; CPU/DMA
publish directly as Visible after their actual simulated store. Record stores a
private patch, not executed data. At completion, the declared writable footprint
is initialized from captured predecessor versions before applying the patch.
The whole footprint receives the conservative accepted write version, including
initialized unchanged bytes. No source bytes are taken from a stale CPU replica.

Current cell authority changes at writable reservation; prior producer operands
stay retained. Completion fills that operation's private representation and does
not roll back a newer cell owner. Duplicate completion is idempotent. Scatter
checks both producer and version against current cells; superseded cells belong
to proven newer operations, not to an arbitrarily skipped pending write. Partial
readback refreshes only the requested slices; other pending bytes remain stale.

Final publication/lifetime review:

| Successor outcome | Authority and preserved contents |
| --- | --- |
| Cancel before submission | Restore captured prior version/producer; prior GPU99 remains intact, never substitute CPU10 |
| Submission/completion uncertainty | Pending successor remains Unknown/unreadable; retain its resource and unexecuted predecessor operands/bytes |
| Visibility failure after known completion | Retain the completed successor's authoritative bytes/resource; completed predecessor operands may already have retired |

Focused tests start with stale CPU10, a completed GPU99 predecessor and a pending
GPU77 successor. They verify predecessor bytes/tags and both resource leases after
failed submission, and restoration of GPU99 after cancellation. A separate test
covers retired predecessor operands followed by successor visibility failure.

CPU pre-store leases block competing access without publishing a CPU version.
Commit clips to the actual byte range and releases the lease; abandoning a lease
without a store does not invent a modification. Overlapping unfinished GPU
writes must complete before controlled CPU overwrite. Disjoint CPU/GPU ranges
coexist. A later CPU overwrite cannot be reversed by a late duplicate completion
or an older full-footprint scatter.

## Independent byte oracle

The oracle includes **no model header** and has no authority cells, dirty flags,
data versions, producers, representations, mapping stamps, ownership or transfer
algorithm. Tests supply known physical ranges and guest-operation dependencies
separately to each side. The oracle stores initial bytes plus a chronological
guest store journal. Reads replay those literal byte stores in guest sequence;
unfinished latest stores make their bytes unobservable, completion only satisfies
declared dependencies, and cancelled unexecuted events contribute nothing.
Late completion does not change replay order. Read-only ordering events have no
stores. Host timestamps and model metadata never choose expected bytes.

Oracle DMA snapshots the entire logical source before creating destination store
events, with later logical destination occurrences winning on repeats. Model DMA
instead reconciles its own producer/replica state, privately snapshots sources,
then transactionally modifies destination cells. They share elementary input
ranges, not preservation logic. The oracle evaluates guest-valid programs;
invalid/unordered/unsupported programs are tested by checking typed rejection and
the model fingerprint before the oracle accepts any side effect.

`--mutation-check` compiles an isolated copy with authoritative Read refresh
removed. Deterministic byte comparison must kill this stale-replica mutant with
`byte oracle mismatch`; dirty-state flags alone cannot satisfy this control.

## Admission and lifetime boundaries

Whole-dispatch preflight checks every supplied binding before reserving any
version, copying any readonly replica, or recording simulated commands. It rejects:

- Independent allocations with overlapping physical bytes if either binding is
  writable (including mixed buffer/scratch/image roles and partial aliases).
- Repeated/overlapping physical segments within a writable linear binding.
- Unknown footprints, unknown producer coverage, holes or unsupported layout.
- Unordered conflicting writes/read operands, stale stamps or closed mutation admission.
- Uncontrolled CPU/NCE raw access, before exposure to simulated guest stores.

Read-only replicas can be initialized at the required completed frontier. Same
actual linear allocation/positions can share bindings under the supplied
guest-valid execution contract; non-overlapping writers are independent. These
are representation proofs, not descriptor-type shortcuts. The model does not
simulate shader instructions/atomics; it cannot repair intra-dispatch aliasing
by ordering post-dispatch copies.

Read-only and mixed dispatches retain every private read operand until completion
or proven cancellation. Pending producers retain their own representation and
captured predecessor operands. Executed/completed operand leases can retire
independently of visibility; a later visibility failure retains the completed
producer, without reviving already retired predecessor resources. A callback
requiring a retired producer representation is rejected. An authoritative representation cannot be evicted
until current contents have been preserved elsewhere. Unmap/remap can remove a
view while accepted work keeps immutable physical/resource values; that work
never re-translates the old address. This models a **proven pinned-use** alternative,
not a claim that current production mapping stamps supply such a lease.

Renderer drain rejects outstanding/unknown work, then preserves live authority
to CPU and drops device representations. It does not manufacture completion or
discard the backing. Backing replacement requires a quiescent, CPU-preserved
state and a fresh epoch; stale callbacks/tickets cannot publish into it. GPU
resource retirement is independent of the existing emulator eight-frame ring.

Chunk transitions require proven CPU coverage/exclusion. CPU accesses anywhere
in a GPU-owned chunk are denied; continuous GPU uses reuse one state. Noncoherent
chunks require explicit flush before END and invalidate plus host barrier before
START. Retry leaves the intermediate state closed; hard failure leaves Unknown.
There are no ioctls or Vulkan operations. These booleans model required evidence,
not proof that exporter/Vulkan/NCE implementations provide it.

## Acceptance traces and invariants

| DESIGN trace | Executable evidence / production limitation |
| --- | --- |
| 1 | CPU77 prefix / GPU99 middle in either disjoint completion order; superseding prefix and old scatter |
| 2 | Same/different DAddr and manager; stale CPU10 and replica10 never win over GPU99 |
| 3 | Partial overlaps, ordered noncontiguous/repeated read segments, holes; writable repeats rejected |
| 4 | Ordered scratch99/image77 keeps77; whole-group conflicting dispatch rejected before effects |
| 5 | Identity-codec texture overfetch publishes only matching cells; unknown real image layouts rejected |
| 6 | Pending scratch survives unmap, remap, resize/rebind replacement and eviction request |
| 7 | Recorded/submitted/Completed-but-not-Visible are distinct; dependent read remains blocked |
| 8 | Disjoint queues complete in either order; dependent premature completion and late duplicate cannot roll back authority |
| 9 | CPU pre-store lease creates no version, blocks readers; readonly/mixed GPU operand leases retained |
| 10 | Mutation admission and stale stamp reject dispatch; immutable accepted physical use survives view removal |
| 11 | Old epoch callback cannot reach replacement; representation IDs are not reused |
| 12 | Known byte layouts admitted, missing producer/footprint or opaque layout refused; no real descriptor discovery |
| 13 | Uncontrolled read/write/cross-boundary raw accesses rejected; no real NCE fault handling proved |
| 14 | Multiple chunk uses share state; unrelated CPU alias in chunk is excluded |
| 15 | START/END retry/hard failure, submission/completion/visibility uncertainty retain closed state and resources |
| 16 | Interval/replica/operation budgets reject atomically; equivalent cells coalesce without dropping authority |
| 17 | Only unexecuted tickets cancel; submitted/Unknown work cannot return false drain success |
| 18 | Owned DMA forward/backward overlap, split/repeated mappings/destination, latest GPU source, allocation/holes before writes |
| 19 | Existing gather, unmap, CPU-write, lazy/import regressions unchanged; external validation below |
| 20 | No runtime flags/hooks/build integration; unchanged M1 disabled-path regression and separate model metrics; no real CPU waits measured |
| 21 | Outstanding renderer work blocks teardown; successful drain preserves surviving arena; fresh renderer/epoch cannot reuse dead resource |

I1/I2: producer-selected refresh and conditional publication; I3/I9: physical
producer/read operand leases independent of views; I4: byte-exact cells/commit;
I5: epoch/physical key, provenance never freshness; I6: view changes no version;
I7: closed admission or captured pinned-use, not validation permission; I8:
quiescent epoch replacement; I10: whole-group preflight; I11: typed atomic errors
or retained Unknown; I12: separate chunk state/data versions. `CheckInvariants`
checks internal partitions, retained authority/operands, version tags, dependency
DAG, CPU exclusion and budgets. Independent byte comparisons check observable
contents after transitions; neither alone is a production proof.

## Random tests, budgets and complexity

Default seeds 372–379, 400 high-level commands each (3,200 commands plus lower-level
transitions); 64-byte storage, multiple aliases/replicas, random partial/split
remaps, repeated pages, queued dependencies, independent disjoint two-queue
batches, randomized completion attempts, read-only tickets and injected atomic
failures. Each seed also exercises three bounded outstanding-work quarantine
traces, giving 24 isolated Unknown tests. Unexpected random failures print seed
and full program, delta-reduce it to a replayable 1-minimal command subsequence,
and save both traces. Fixed small fault failures print their stage/range/value
and replay seed; those bounded fault traces are not claimed as delta-minimized.

Default structural budgets: 256 cells, 32 representations, 128 views, 1,024 retained
operations, 4 MiB metadata estimate, 1 MiB byte payload. State admission uses a
private candidate so allocation/invalid source/budget failure cannot partially
publish DMA destination or replicas. At budget exhaustion, reject with retained
authority; no dirty producer eviction. Historical operations intentionally stay
bounded and eventually cause Budget rather than unsafe history pruning.

`Statistics` exposes cells, replicas, active non-CPU holders, pending operations
(including retained Unknown), attempted splits/coalesces, current/peak structural
metadata, byte capacity, rejection/Unknown results and mutation-call elapsed/max
time. Pure preflight/introspection are not timed. Estimated metadata includes
object/vector capacity and a 64-byte map-node allowance, not allocator/RSS or
temporary reallocation peaks. Transactional candidate copying can temporarily
double state payload; the resident budget is not a process memory cap. Split and
coalesce counters include attempts rolled back after rejection.

This model deliberately scans bounded cells/segments/operations and clones state.
Authority storage is O(cells), replica tags/bytes O(total representation bytes),
history O(retained operations and operands). Transfers iterate bytes and relevant
spans; dependency checks traverse the bounded DAG. This is a correctness reference,
not the final production interval index or an emulator performance benchmark.

**Do not transplant candidate cloning into production Buffer Cache.** It is a
reference-only strong exception guarantee for private CPU data: budget rejection
can occur after candidate bytes/versions changed, with the original fingerprint
unchanged. Publication has a compile-time nonthrowing State move-assignment check.
Real GPU submission/store/ownership side effects cannot be rolled back by copying
metadata and require the staged transaction/acknowledgement protocol in DESIGN.md.
Neither 36–55 us / 24–40 us nor the approximately 118 KiB estimate predicts M4
latency, memory footprint or performance; they include reference state cloning and
its bounded fixtures/history. M2 prioritizes logical correctness over hot paths.
Immutable physical values and simulated leases also do not prove actual Vulkan
object lifetime, mapping-use admission or CPU/NCE exclusion.

## Reproduce and validation

```sh
python tools/physical_coherency_tests/run_model_tests.py --cxx g++ --mutation-check
python tools/physical_coherency_tests/run_model_tests.py --cxx g++ --sanitize
python tools/physical_coherency_tests/run_model_tests.py --cxx g++ --seed 372 --steps 400 --seeds 8
python tools/physical_coherency_tests/run_model_tests.py --cxx g++ --replay /absolute/path/failing-minimal-372.txt
# Windows: --cxx C:/msys64/mingw64/bin/g++.exe
```

Runner performs isolated compilation of both model translation units, then
deterministic and randomized tests. It uses only standard C++20, no production
headers or dependencies. Logs, binaries and minimized failure files are under
ignored `.diagnostics/m2-model/{windows,linux}/{normal,sanitized}`.

Validated on Windows MinGW and Linux GCC with `-std=c++20 -Wall -Wextra -Werror`:
both isolated model translation units, 16 deterministic groups, default eight
seeds / 3,200 commands plus 24 isolated quarantine trials, stale-read mutation
control, and a six-command saved replay exercising independent queue completion,
readonly admission, renderer drain and CPU store. Linux ASan/UBSan passes the
same deterministic/random suite with no reported sanitizer failures.

Existing resolver, M0 DMA, M1/M1.1 shadow and all nine gather-buffer runners pass
on Windows and Linux. Linux lazy backing/export, chunk import cache/backend and
published backing/lifetime suites also pass. Production source, build files,
DESIGN.md and M2_MODEL_SPEC.md are byte-identical to the starting branch.

Default random results on both hosts: 22–28 final cells, 11 replicas, zero remaining
active/pending GPU producers after preservation, 95–120 attempted coalesces;
peak estimated resident metadata 98,672–120,816 bytes (maximum about 118 KiB).
70–92 mutation-call rejections per seed and three explicitly retained isolated
Unknown trials per seed. Windows normal mean mutation-call time roughly 36–55 us,
Linux normal roughly 24–40 us in this run, including candidate cloning/metadata
budget accounting, excluding test assertions/fingerprints/oracle. Host load and
compiler matter; these are structural reference-model measurements, not a target
GPU/emulator benchmark or a choice of production budgets. Sanitizer timings are
not comparable. Logs retain per-seed exact counts/times and sanitizer results.

No Android APK build is needed; production Vulkan, real NCE faults, actual
dma-buf coherency, thread races and shader behavior are not tested.

## Remaining integration blockers

M3/M4 still need real protected mapping-use admission, allocation lifetime pins,
controlled CPU post-store publication and complete NCE/kernel alias coverage,
producer inventory beyond sampled M1, real queue/semaphore completion/error
acknowledgements, exact texture layout adapters, publication revalidation around
asynchronous transfers, renderer/device drain and exporter/mmap cache guarantees.
One model owns one active backing epoch; replacement rejects undrained work.
Empty views are rejected. No multi-arena coordinator or automatic recovery from
Unknown is attempted. Nothing here enables general DirectGuestView or changes
Phase 2, imports, UMA, GPU cache semantics or eight-frame retirement.
