# M3B production integration progress

## Controlled CPU preparation lifetime repair — 2026-10-19

Ordinary guest owner/lifecycle audit found an empty readiness visit before actual
scalar request preparation. `9d50526fd5` keeps the existing managed owner entry
through cold snapshot/pinning, immutable request construction and registry
publication, with the owner shared lock held through Memory/PTE access and released
before the exclusive registry lock. Close may refuse the registry-only tail;
pre-effect exceptions unwind without a published request. Initial System/process
lifetime remains the caller's obligation. No ordinary Write/bulk, DMA, NCE,
Texture Cache, mapping or automatic game routing was expanded.

[Linux-only 39251597638](https://github.com/Splaser/emulator/actions/runs/39251597638)
passed at exact source `9d50526fd500267fb1c1d7a81078903a28365010`: full production
build/regressions, five CTest processes, three extra 479-assertion provenance
processes with the real-process preparation/pin marker, three new preparation
assertion mutants and three positive repeats. Artifact `12187957923` has a verified
ZIP digest and 131 matching source hashes across ten manifests. Android was skipped.
Original F2 backing77/cache10 and F3's two fixed-mode alias failures remain.

The [audit and acceptance record](../../docs/M4_F3_CPU_PREPARATION_LIFETIME.md)
separates this CPU-only repair from the remaining outer PTE/permission publication,
full process/backend/service/raw/GPU owner exclusion and retained Unknown lifetime
gates. Ordinary guest admission remains blocked; **F2/F3 remain Open**.

## Isolated submission-linked write provenance

Full [Linux/Android run 38984914456](https://github.com/Splaser/emulator/actions/runs/38984914456)
passed at `102b0486f7`: five CTest processes, 53 provenance assertions in CTest and
each of three extra native processes, all configured integration/lifecycle/fault
gates, preserved F2/F3 fixed-mode negatives and real Android arm64 production
library compilation. The Draw guard case uses extracted actual worker code with
fake pipeline/Vulkan dispatch; it is not a native storage-binding/shader test.
Source stamps, generated fixtures and job output are retained under
`.diagnostics/ci-38984914456/`.

`102b0486f7` (initial implementation `64c59e9208`) links an explicit staged fill
to native resource/byte footprint,
resource-local intent and a real CheckedSubmission. It distinguishes worker
pending, actual queue acceptance, skipped/no-write commands, checked completion and
sticky Unknown. Completed old submissions never certify a remapped allocation or
order physical replicas. Scheduler retains accepted payloads until the existing
trusted device-idle retirement boundary. Ordinary shader/alias consumer routing
and physical latest-writer authority remain unimplemented. F2/F3 remain Open and
restricted eligibility remains blocked. See the
[provenance record](../../docs/M4_F3_SUBMISSION_PROVENANCE.md).

## Resource write-intent revision follow-up

Full Linux/Android run [38907387312](https://github.com/Splaser/emulator/actions/runs/38907387312)
passed at `d8764c0cf0`: 88 native adapter assertions in CTest and each of three
additional processes, all configured integration/lifecycle/fault regressions and
actual Android arm64 production library compilation. Original F3 fixed mode still
fails exactly its two alias checks; F2 still observes backing 77/cache 10. Baseline
Linux run [38906918308](https://github.com/Splaser/emulator/actions/runs/38906918308)
at `841047f2a3` independently reproduces the old adapter accepting a prepared source
rewrite in all three extra native processes (72 assertions each). Source stamps,
logs and generated fixtures are retained under `.diagnostics/ci-<run>/`.

`d8764c0cf0` advances resource-local GPU write intent at ordinary write marking,
staged DMA copy/clear and scatter destination publication. The isolated alias
adapter now compares those revisions alongside native identities and physical
geometry, rejecting repeated source/target writes before submission and retaining
Unknown/quarantine after submission. Same dirty geometry or identical-byte writes
cannot reuse the old intent. Resource-wide conservative invalidation is not
latest-writer subrange ownership or checked producer submission completion.
Historical physical provenance, CPU uploads, mapping/frame reuse and ordinary
consumer integration remain open; F2/F3 stay Open and eligibility stays blocked.
See the [adapter follow-up](../../docs/M4_F3_CONTROLLED_ALIAS_ADAPTER.md#resource-write-intent-revisions).

## M4 isolated admitted alias copy verified; ordinary F3 remains Open

Production source `642a71fb07` passed full Linux/Android run
[38903097142](https://github.com/Splaser/emulator/actions/runs/38903097142).
Test-only weak native-payload retention/release checks at `a415dea618` passed
complete Linux run [38903908981](https://github.com/Splaser/emulator/actions/runs/38903908981).
The production sources/workflow are identical between those heads. The four CTest
processes passed: 130 ordinary cases, 1708 scheduler assertions, 38 original F3
assertions and 67 admitted-adapter assertions; the adapter also passed three extra
native repetitions. All configured HostMemory/import, integration/lifecycle,
mutation and new seven-mode alias transfer fault fixtures passed. Android compiled
the actual arm64 production libraries remotely; no APK/device run is claimed.

The adapter snapshots physical/resource associations and actual GPU dirty ranges,
rejects overlapping dirty owners across all current DAddr aliases, parks under the
existing backing range admission and verifies exact staged copies with checked
Vulkan submission/readback. Dirty complements survive; pre-effect retirement
refuses and post-submit remap retains Unknown/quarantines readers. The source stays
dirty and no guest-backing copy or new ordinary route is introduced.

This completes an isolated transfer component, not persistent producer versioning
or ordinary F3 repair. F2/F3 remain Open and restricted eligibility remains blocked.
Continue with the [adapter record](../../docs/M4_F3_CONTROLLED_ALIAS_ADAPTER.md)
and [authority roadmap](../../docs/M4_F3_ALIAS_AUTHORITY_PLAN.md).

## M4 F3 native alias witness reproduced; production fix remains open

Remote Linux run [38898475861](https://github.com/Splaser/emulator/actions/runs/38898475861)
at `48d95a8ba3` passed all production/native/integration/lifecycle gates. CTest
passed 130 ordinary cases, 1708 scheduler assertions and the 38-assertion native
F3 witness. Three more independent F3 processes passed; fixed mode returned 42
with exactly two expected alias failures. Full-page and partial aliases retained
10 after an actual Vulkan write of 99; same-address reads, untouched complements,
disjoint backing, retired/remapped identities and physical-equivalence queries
passed their controls. F2 reproduced three times; its fixed-mode negative returned 1.

The new test setup isolates complete System lifetimes in separate processes because
the host TLS dummy KThread retains its original Kernel. No production coherency code
changed. F2/F3 remain Open, restricted eligibility remains blocked and ordinary
Memory::Write/UMA activation remains off. Continue from the
[M4 handoff](../../docs/M4_F3_WIP_HANDOFF.md) and
[bounded content-authority plan](../../docs/M4_F3_ALIAS_AUTHORITY_PLAN.md).

## Restricted eligibility: blocked; readback isolation verified

The requested ordinary scalar production-eligibility acceptance is **not achieved**. The
[restricted eligibility record](../../docs/M3B_F2_RESTRICTED_ELIGIBILITY.md) identifies the
missing CPU/native/raw-pointer/GPU/alias-lifetime exclusion authorities. No certificate or
automatic ordinary Write handoff is fabricated. The strict fallback is producer isolation.

This continuation rejects overlapping uncommitted/committed/pending BufferCache readback work
before request acceptance, through current physical aliases over the full preservation
footprint. Guarded ordinary download/writeback and async scheduling/retirement reject active
or Unknown conflicts before their data/bookkeeping effects. This is one family of isolation,
not full GC/scatter/mapping/Texture/CPU producer exclusion. Original unbound F2 was rerun three
times: backing77/cache10 each time; `--require-fixed` exits 1. F2 remains Open.

The native test queues a real completed Vulkan async download but keeps guest copyback pending,
then requires pre-accept Cancelled/zero stores from its guest consumer. It also probes actual
download/writeback/scheduling entries while an accepted request is parked. Vulkan readback
retirement runs on GPUFencingThread: that worker catches/latches conflicts and notifies GPU;
command dispatch and FenceManager owner/wait APIs observe failure without replay or false
completion. The complete production FenceManager template uses native fence/cache/platform
shims for its host topology tests, distinct from the native Vulkan executable.

Final remote evidence:

- Android production compilation passed at `3ebb655b97` in
  [38870993595](https://github.com/Splaser/emulator/actions/runs/38870993595).
- The complete Linux workflow passed at `46687f1bfa` in
  [38873324462](https://github.com/Splaser/emulator/actions/runs/38873324462): 130 production
  test cases, 1,708 native scheduler/readback assertions (0.81 seconds), real HostMemory/import
  lifetime and every configured integration regression. Downloaded CTest and job logs are
  retained locally. Registration/pipeline cases ran ten remote repetitions; all controls
  82-90/92-95 passed. Renderer fencing and complete FenceManager cases passed ten repetitions.
- `git diff 3ebb655b97 46687f1bfa -- src` is empty. The follow-up changes only the extraction
  fixture, workflow and documentation; Linux-only validation intentionally skipped Android
  rather than claiming a second Android build. The earlier full Linux job was red solely
  because the renderer-worker fixture omitted the new conflict type/state/GPU notifier.
- Local registration/pipeline cases passed three repetitions; controls passed individually.
  Real ThreadManager topology passed three repetitions plus the reverse-dependency control.
  GPU-stop tests cover both a direct typed exception and external fencing-worker publication.

These results certify this isolation slice, not ordinary-game Restricted Eligibility.

## Native guest scheduler acceptance: final lifecycle hardening passed

CI [38864622309](https://github.com/Splaser/emulator/actions/runs/38864622309) at `02610e1766`
passed both Android and Linux, including the native acceptance executable (1,696 assertions,
0.82 seconds), production unit tests (130 cases, 3,878,778 assertions) and all configured
integration regressions. Its downloaded native log confirms
64 process teardowns, real guest suspend/resume, all terminal observations, actual lock
contention/negative witnesses, GPU99/CPU77/GPU99 and accepted native teardown without queue
drain, including the final owner-retention/slab-cleanup and reused-frame sentinel assertions.
The full workflow is green. The earlier complete run at `202eb74500` also passed in
[38861935399](https://github.com/Splaser/emulator/actions/runs/38861935399).

`controlled_cpu_scheduler_tests` links the real CpuManager/KThread/KScheduler, kernel process
and page-table teardown, GPU ThreadManager, Vulkan Rasterizer/BufferCache and checked transfers.
Its headless transfer renderer omits swapchain/presentation; no scheduler or device-completion
body is extracted/replaced. CI installs software Vulkan and treats missing driver support as
failure. Test-only peers select real owners/locks and initialize the existing kernel/video
stages; a service fiber entrypoint supplies the CPU work rather than an ARM application image.

The suite exercises guest sleep/resume and all four terminal actions, 64 complete process
teardowns with pending Prepared requests, and an accepted native request whose submission is
blocked during process exit. It checks actual PTE-domain retirement before releasing delayed
GPU work, unrelated GPU progress during exit, and CPU77/GPU99 complement with a real Vulkan fill.
Actual cache/sys-core/counter/PTE contention and blocking legacy negative witnesses exercise
the reverse dependencies. The final stress assertions also require context retirement and
release of Cancelled owners: slab finalization skips ordinary C++ destructors, so KProcess
explicitly finalizes Memory after its inherited/PTE cleanup. Unknown retains a sealed gate.
The accepted-native teardown deadline includes the last thread references and actual PTE
retirement. Delayed GPU99 preservation must leave a test-reused physical frame's sentinel55
unchanged after exit and subsequent trusted device completion.

The execution lease no longer encloses device tracker callbacks or blocking range completion.
Controlled request publication retains existing page protection/cache references and updates
only pre-existing tracker managers. Store and bounded preservation backing copy have separate
short owner visits; final local metadata publication uses nonblocking admission completion.
Request mapping qualification and cache entry also use try-locks. Guest observation sleeps on
the kernel timer; GPU completion never has to acquire the guest scheduler lock to wake a CPU.
Termination interrupts observation without cancelling an accepted request or authorizing replay.
Host observers and dispatch-disabled/locked contexts refuse observation without waiting.
Acceptance now retains identity in the bounded owner registry before transaction allocation;
Unknown before allocation or before admission cannot lose its identity/native owners. Cold
preparation/Close reap ordinary terminal identities, but Unknown has no implicit reap/recovery.
Negative controls 93/94 exercise parked admission and construction failure respectively.
Final host validation passed three repetitions of registration/pipeline cases and all controls
82-90/92-94; the physical-admission suite includes Pending-to-Unknown retention and rejected
completion/cancellation, and the insertion candidate suite passed 24 cases x 3 repetitions.
Ordinary Memory writes are still not automatically routed here; global F2 remains Open.

The complete [producer inventory](../../docs/M3B_F2_PRODUCER_INVENTORY.md) records which CPU,
GPU/cache, deferred, raw-pointer and mapping/lifetime paths actually participate and which
must be excluded before restricted activation. Existing notifications, snapshots and pins
are not a global exclusion mechanism. The [native acceptance and lock audit](../../docs/M3B_CPU_SCHEDULER_ACCEPTANCE.md)
records the execution-lease boundaries and the CPU terminal-action contract.

Previous CI 38844806313 passed Android/Linux native compilation and unit tests, but failed the
older GPU-stop extraction harness because it omitted the new continuation/monostate variants.
The harness has been updated; do not record that previous workflow as an overall success.

## M3B-F2 CPU Scheduler Handoff: explicit owner bridge, activation still gated

This continuation implements the requested handoff boundaries without switching all ordinary
Memory writes. `QueueControlledCpuWriteRequest` goes through the existing GPU facade and actual
ThreadManager/RunThread. Its queue admission makes one try-lock/TryEmplace attempt: it never
waits for queue space, a GPU fence, preservation, admission or the original CPU producer.
Synchronous-mode/inline-owner submissions refuse. Up to 64 owner continuations poll once per
normal owner iteration, allowing normal queue work and suspended-channel retries to progress.
An already suspended channel refuses a new continuation before request acceptance. Stop/failure
destroys captures while the renderer remains alive: Prepared requests cancel and accepted
uncertainty quarantines. No second dispatcher or recursive queue pump was introduced.

Each Memory now has a bounded request registry and an independently retained execution
gate. Cold preparation registers its immutable request. Close seals only that Memory: it
cancels Prepared requests, marks non-completed Accepted requests Unknown, and joins only an
entered CPU store/publication visit. It does not drain the GPU queue or wait for native
preservation. Queued closures contain no Memory/process raw pointer; final commit obtains a
nonblocking execution visit. Actual KProcess::Finalize closes the gate BEFORE TLS/page-table
finalization; Memory destruction, reset and move also close the old execution identity. Reset/
move cannot redirect an old request to a replacement Memory. This is revocable execution
qualification, not an assertion that the request pins the entire KProcess. Accepted identities
have explicit strong retention; other terminal identities can be reaped cold or at Close.

Terminal request classification is explicit and cannot be downgraded by teardown:

| Request state | Scheduler action |
| --- | --- |
| Prepared / Accepted | Await owner; enqueue/callback return is not store completion |
| Stored | CPU store/publication completed; protected GPU refresh remains separate |
| Cancelled | Do not execute this request |
| Refused | Reprepare only with the explicit certified-no-effects flag; no legacy bypass |
| Unknown | Quarantine; retain physical/native/request identity, never replay |

Accepted cancellation still refuses without a certified no-effects cancellation operation.
Owner exit does not manufacture such certification: admitted uncertainty stays Unknown.
Quarantine checks now collect current physical aliases under a short try-guard, so a stale
mapping generation alone does not block an unrelated current range or another owner's tasks.
The same old physical footprint remains isolated; this is not recovery or revalidation for
replaying the old store.

The requested assertions are executable in the registration pipeline:

| Test | Assertion/evidence |
| --- | --- |
| Enqueue vs Close | `enqueue_close_and_teardown`: real owner queue races Close, destroys the Memory wrapper before delayed GPU completion, and never executes the queued store |
| CPU suspend vs GPU progress | `scheduler_handoff`: producer parks after releasing guards; real RunThread advances admission and checked preservation independently |
| Repeated poll/resume | Same test: additional ticks/queue attempts leave physical publication sequence exactly one |
| Process teardown | Execution gate Close joins the entered visitor before real PageTable::Resize destroys storage; subsequent visits refuse; production KProcess closes before Finalize |
| Accepted cancellation | Late Cancel refuses; native/owner-close failures retain Unknown, dirty obligations and accepted owners |

The handoff test parks a real conflicting physical admission, verifies that PTE/cache/sys-core
guard probes can enter while admission/preservation are pending, and releases the dependency
through later normal GPU-owner work. Close leaves unrelated owner ticks and current ranges
running. A mutation which inserts synchronous queue acknowledgement recreates the CPU/GPU
producer cycle and fails the topology gate. Omitting Close fails lifecycle control 92. Earlier
candidate insertion/whole-owner deadlock witnesses remain required and unchanged in meaning.

Earlier host evidence boundaries (superseded by the native acceptance section above):
ThreadManager, RunThread, bounded queue, CPU preparation, scoped ordinary
store, physical admission/pins, PageTable, tracker, cache/Vulkan advance and checked transfer
bodies are production code. Driver/device execution and engine/platform collaborators remain
fixtures. The sys-core probe and cache object surrounding extracted bodies are harness
collaborators; this is NOT full guest CpuManager/KThread fiber or complete KProcess destruction
execution. The production Finalize ordering is compiled/audited, and the execution gate/PTE
ordering is runtime-tested, but a complete kernel/process teardown run and alias-process lifetime
qualification remain activation gates. Do not claim those remaining tests passed merely from
the host topology suite. No automatic game-CPU routing is enabled.

Next acceptance: connect the actual guest CPU suspension/resume owner without retaining any
production sys-core/PTE/cache guard or required producer duty, validate complete kernel teardown
and all alias-process owners, and enforce exclusion of ordinary GPU/GC/download/mapping/unknown
producers. Real-device evidence and trusted Unknown recovery remain necessary. F2 remains Open.

Validation for this slice: registration pipeline `--repeat 10` passed the four-width
store/preservation cases and controls 82-90/92; its actual-owner handoff/lifecycle groups
passed (including eight enqueue/close races), together with the synchronous-acknowledgement
negative topology control. First-registration tests passed `--repeat 10` and controls 71/72/75;
insertion audit passed three repetitions and all rejected baselines. `git diff --check` passed.
The unchanged legacy F2 `--require-fixed` witness still fails as expected (`backing=77`,
`observed=10`), so this is not a global F2 closure. Previous-source CI
[38842256792](https://github.com/Splaser/emulator/actions/runs/38842256792) completed Android
and Linux successfully at `da5bf21a39`; current-source native compilation awaits remote CI.

## Owned CPU requests and completion handoff: 2026-10-16 — automatic dispatch pending

CI [38839879554](https://github.com/Splaser/emulator/actions/runs/38839879554) completed successfully
at `273bfb2f5d82a96565595ac160b558faef3861ac`: Android native compilation, Linux production/unit
tests and all configured backing/coherency/lifecycle regressions. Its earlier Android-only
observation is superseded by the completed two-job result. It does not validate this new source.

Actual CPU-owner `Memory::PrepareControlledWrite8/16/32/64` now creates an immutable single-page
cached write request before effects, outside sys-core/PTE/cache guards. It copies the typed
scalar, normalizes VA, rejects cross-page/uncached/unqualified views, and retains the existing
physical/context snapshot. No binding, data admission, CPU store, dispatcher or wait is created
by preparation. The request stores no raw Memory/process pointer; the actual commit still
requires a live Memory supplied by its owner. Keeping Memory/process alive remains the caller's
lifetime responsibility, not an implicit process pin.

CPU cancellation and GPU-owner acceptance race through one atomic Prepared transition. A
cancelled request cannot execute later; acceptance is single-use and late cancellation cannot
revoke its write/native obligations. `BeginControlledCpuWriteRequest` binds the immutable request
to the existing real BufferCache transaction and retains it in the same physical admission
owners as the registration and native preservation allocations. Caller drop cannot free an
accepted Unknown request. Pre-effect qualification failure reports Refused; uncertain effects,
store/publication failure and dropped active transactions report Unknown, never Stored.

The actual Vulkan `AdvanceControlledCpuWriteRequest` endpoint polls the checked preservation
continuation and commits only the request's owned scalar through actual ordinary Memory Write.
The older value-taking endpoint cannot substitute another scalar into a request-backed
transaction. Neither queued callback return nor completed readback acknowledges CPU success.
Only successful actual store, all-alias tracker publication and admission release publish
Stored with release/acquire visibility to the CPU observer. Stored acknowledges **CPU commit**;
the protected GPU representation remains dirty/inaccessible until checked refresh/upload.

The pipeline suite now composes the actual Memory preparation, private request state machine,
BufferCache acceptance/commit and actual Vulkan advance body. An independent CPU producer
prepares all four widths; changing its source variable cannot alter the queued payload. CPU
observers stay Accepted through submission/readback and post-store tracker publication, then
observe Stored and backing77. Both manual and request-backed four-width transactions retain
the GPU 99 complement and reach cache77/clean tracker after checked refresh. It also checks
pre-accept cancellation, cancellation/acceptance races, duplicate acceptance, stale queued
views, invalid target eligibility, wrong Memory context, native failure and retained Unknown
owners. Early readback-as-success fails 89; omitted accepted-request retention fails 90.

Windows first-registration and pipeline suites each passed `--repeat 10`, with controls
71/72/75 and 82-90. Original unbound F2 `--require-fixed` still fails as required with
`expected=77 observed=10 backing=77`; its unmap/preservation prerequisites passed. Native
compilation for this continuation needs its own exact-source workflow. Driver dispatch/device
execution, device-mapping/tag fanout and some cache collaborators remain fixtures in extraction.

**Automatic game-CPU routing and suspension remain pending.** The request is a nonblocking
handoff/completion object for the explicit isolated GPU-owner endpoint, not a new command queue,
GPU wait or automatic request lookup in ordinary Write. No locks/binding are retained across its
polling boundary. Next, connect the real CPU caller to existing owner scheduling/continuations
without sys-core/PTE/cache guards spanning suspension, and qualify all GPU/GC/download/mapping/
unknown producers before activation. Trusted recovery and exact-source device evidence are
still required. No DMA, Texture Cache, NCE or full-game safety expansion is included. F2 is Open.

## Ordinary scalar body binding: 2026-10-16 — automatic producer enrollment pending

Native [38835727709](https://github.com/Splaser/emulator/actions/runs/38835727709) passed Android
production-library compilation and the full Linux production/unit/coherency/lifecycle jobs at
`17f1e65977fb8a4573afb0aa87b24e903792bdde`. That validates the preceding preservation slice,
not device execution or the new continuation below.

The typed Vulkan transaction commits now call `Memory::WriteOrdinaryControlled8/16/32/64`.
Each installs a synchronous, thread-local, one-attempt binding and invokes the **real ordinary
public Write8/16/32/64**, including their existing little-endian conversion. The actual ordinary
`Memory::Impl::Write` checks the binding before any legacy pointer resolution or rasterizer
callback. The binding qualifies the Memory context, normalized VA, width and owned scalar bytes,
then uses the admitted physical store. Its explicit result returns to BufferCache, which still
owns preservation, admission and every post-store alias publication. Stored is not reported
until that complete outer transaction succeeds.

Every bound refusal returns through the ordinary branch without legacy fallback. The scope
does not survive a wait or callback; it owns expected scalar bytes and borrows owners which the
outer transaction retains. It cannot be copied/moved or accidentally bind another CPU thread.
Nested bindings refuse Busy without hiding the earlier scope; wrong context/VA/width/value,
cross-page/unsupported-width, stale mapping and busy mapping guards refuse. A second ordinary
call in one scope refuses rather than replaying a store. Scope teardown restores its predecessor.

The pipeline extraction now includes actual public ordinary Write wrappers and bound wrappers,
as well as the ordinary Impl body and real scoped binding. All four scalar widths run through
GPU work -> checked preservation -> ordinary Write -> real tracker publication -> checked refresh.
Backing/cache retain 99 around the exact 77 store bytes; physical publication is checked byte by
byte. The prior two 77 registration orderings and controls remain. Bypassing the ordinary routing
branch must fail explicitly with 88 instead of letting the fixture resolver silently write.
The fixture also checks refusals, cross-thread isolation, nested scope restoration and duplicate
attempts. Actual driver/device work and full producer exclusion remain outside its evidence.

Windows validation passed: first-registration and registration-pipeline suites each with
`--repeat 10`, all existing negative controls and new ordinary-routing control 88. The pipeline
executes all four widths per repetition. Candidate admission passed 24 cases x 3 with ordinary
binding deliberately inactive. The insertion audit reproduced its three rejected baselines
with `--repeat 3`. The unchanged original F2 `--require-fixed` run failed as required with
`expected=77 observed=10 backing=77`; its unmap/preservation prerequisites passed. Native
Android/Linux validation for this new source must use a new exact-source workflow run.

**Automatic ordinary CPU producer enrollment is still pending.** This is a real ordinary-body
integration on the existing isolated GPU-owner continuation, not a new CPU-to-GPU dispatcher.
No game CPU thread automatically creates a registration or waits for preservation. The legacy
HandleRasterizerWrite sys-core lock and command-queue dependencies cannot enclose that wait.
Unbound ordinary writes retain their legacy behavior (plus the inactive binding check), and the
disabled-binding original F2 witness must still report backing77/cache10. Candidate/insertion
witness collaborators explicitly keep binding inactive; they do not establish the new route.

Next: prepare immutable ordinary CPU write requests before effects, connect caller suspension/
completion ownership without retaining sys-core/PTE/cache guards or blocking required producers,
and enforce GPU/GC/download/mapping/unknown-producer exclusion before automatic activation.
Trusted Unknown recovery and exact-source real-device evidence remain necessary. F2 is **Open**.
No local Android build, DMA, Texture Cache or NCE expansion is included.

## Write-transaction ownership and GPU preservation slice: 2026-10-16 — F2 Open

Resumed from `89665bef71`, fast-forwarding the clean home checkout from `8ca6197c67`.
The next-stage target remains ordinary scalar CPU write enrollment. This slice implements its
cache-owner transaction/preservation boundary first; it does **not** install an ordinary
`Memory::Write` caller, replace a refused write with legacy memcpy, or close that target.

`BeginControlledCpuWrite` binds one cached prepared view, physical page, normalized VA and scalar
width to the registered immutable full-buffer plan. Its privately owned write admission retains
the registration, physical pins and a separate one-shot Vulkan readback. The registration is
Writing until commit; protected readiness, new writes and refresh refuse. Caller drop/uncertainty
quarantines it and the range retains the accepted owners. The GPU owner must poll outside cache/
PTE locks and continue excluding producers which do not participate in this explicit protocol.

The readback copies the retained GPU buffer to a dedicated mapped allocation, records a real
transfer-to-host visibility barrier, and uses exact checked queue submission, timeline query,
zero-timeout host wait and checked invalidation. Callback return cannot advance preservation.
After completion, a short device guard validates ALL physical destinations and aliases before
copying into pinned backing. Whole-buffer preservation requires a clean CPU footprint and one
authoritative GPU representation; mixed CPU/GPU data and GPU-dirty alternative aliases remain
ineligible. No ordinary device mapping is reread to choose a copy destination.

The transaction retains GPU dirty flags and exact download ranges through the actual prepared
store AND all post-store physical-alias tracker publication. Only then are GPU dirty/download
obligations cleared and admission released. A store/publication exception retains Unknown and
the GPU dirty obligation. Typed Rasterizer commit endpoints bind the stored width to the
transaction's declared extent. The old cached adapter also checks capability ownership.

Actual `SynchronizeBuffer` checks exclusion before `ForEachUploadRange` can consume real CPU
dirty state, including physical device aliases. An overlap or unqualified mapping guard throws
before consumption; the legacy bool cannot represent a deferred operation safely. This is a
fail-closed explicit-owner endpoint, **not** an ordinary-game suspension/retry policy. Other GPU
bindings, existing bound draws, GC/download/mapping producers and unknown CPU writers have not
all been enrolled. They must not run against the isolated footprint during this continuation;
this slice does not prove safe concurrent general game execution.

Also corrected the old controlled-path null check: `NULL_BUFFER_ID{0}` is a valid SlotId in a
boolean context. Ready/refresh/cached-write qualification now compares it explicitly, so a
reused/replaced slot cannot pass qualification through that null sentinel.

Windows validation: `run_first_registration_tests.py --repeat 10` and
`run_registration_pipeline_tests.py --repeat 10` passed with all negative controls (71/72/75
and 82-87), using the existing Boost headers under `.diagnostics/lazy-backing-tests/deps/usr/include`.
`run_write_coherency_witnesses.py --case f2 --repeat 1 --require-fixed` still failed as required,
reporting `F2 expected=77 observed=10 backing=77`; its unmap/preservation prerequisite regressions
passed. Native Android/Linux CI must be matched to the resulting source commit and is not yet
validation of this slice. No local full Android build was run.
The pipeline suite adds checked GPU work -> preservation -> actual CPU store ->
real tracker publication -> refresh, with `[99,77,99]` in backing and the observed cache. Upload
attempts through actual SynchronizeBuffer before/between store/publication must refuse, and an
admitted physical reader remains parked until commit. Missing preservation fails 85; bypassing
upload exclusion fails 86; omitting transaction tracker publication fails 87. Native API/remap/
slot failures, interrupted post-store publication, width mismatch and dropped transactions
retain their failure boundaries. Both earlier registration orderings and controls remain.
Vulkan dispatch/allocation/device execution, cache binding teardown and async download/range
containers in this extraction remain fixtures; HostMemory, prepared CPU stores, real tracker,
cache/transfer/checked Scheduler bodies are real. Driver/device acceptance is still required.

Next: connect ordinary scalar write ownership to a GPU-owner/deferred continuation without
holding CPU/PTE/cache locks across a wait; enforce eligibility and exclusion of every producer
which can touch the accepted footprint; add trusted recovery and exact-source device evidence.
The unchanged ordinary F2 witness still reports backing77/cache10 with `--require-fixed` failing.
No DMA, Texture Cache or NCE expansion is included. F2 remains **Open**.

## End-of-day checkpoint: 2026-10-15 — F2 Open

Development is stopped for today; this checkpoint is documentation only. The next stage is
**M3B-F2 Production Write Transaction**, defined in the
[roadmap](../../docs/PR375_PR380_INTEGRATION.md#next-stage-m3b-f2-production-write-transaction).
Stop expanding DMA/peripheral features. Restrict the next implementation to ordinary scalar
`Memory::Write<u8/u16/u32/u64>` with a single stable page, proven physical identity and actual
BufferCache admission/preservation. NCE, JIT fastmem, cross-page and unknown producers retain
their original paths and have no newly claimed coherency guarantee.

Prove the complete `preserve -> store -> publish` transaction against concurrent real GPU work:
GPU-dirty untouched bytes survive (`[99,99,99] -> [99,77,99]`, not `[10,77,10]`); exact successful
dirty publication cannot be prematurely consumed by upload; exceptions/partial failures retain
dirty state, admission and resource ownership until certified completion or trusted recovery.
Entering a prepared-write API alone is not acceptance. Keep both registration orderings, actual
cached continuation after NeedSlow, real tracker dirty/clean checks, and backing77/cache10 as the
disabled-handshake negative control. Add the GPU-dirty complement case as a production-entry gate.

CI [38741531093](https://github.com/Splaser/emulator/actions/runs/38741531093) succeeded at
`f00768b0e6cd988db2b3c411e04b48c35f9c7601`: Android arm64 production libraries, Linux production
build/unit tests, actual Linux HostMemory/backing lifetime tests, and full coherency/lifecycle
regressions. Both registration orderings passed ten repetitions, with backing77/upload77 and
the real tracker sequence; disabled handshake still produced backing77/upload10. The extracted
native pipeline and its completion/alias/visibility negative controls also passed ten positive
repetitions and all negative controls; Vulkan dispatch/device execution remain fixtures.

Available: physical pins, joint range admission, page writer gates, actual controlled cache
registration/upload, checked completion and deterministic controls. Still open: ordinary CPU
production callers, GPU-dirty preservation, failure recovery, exclusion of uncontrolled
producers and real-device validation. This is descriptive progress, not a completion percentage.
Green CI does not prove ordinary-write safety in every game and does not close F2.

## Actual BufferCache registration, immutable upload and checked completion: 2026-10-15

Native run [38737867745](https://github.com/Splaser/emulator/actions/runs/38737867745) at
`08ea7c0a98` exposed two compile-only issues: Linux X11/Result declaration order and Android's
uncaptured-local shadow check on the new plan lambda. Follow-up corrects include order and the
parameter name without disabling warnings or changing the admission/transfer behavior. The
corrected source `f00768b0e6` passed replacement CI
[38741531093](https://github.com/Splaser/emulator/actions/runs/38741531093); F2 remains open.

CI [38727690858](https://github.com/Splaser/emulator/actions/runs/38727690858) passed both native
jobs for `dc1a839308`. Continued on PR375 with an explicit real BufferCache/Rasterizer endpoint;
ordinary game access remains unenrolled and F2 remains OPEN.

The real controller prepares the complete aligned device-only physical plan, real unregistered
slot/payload pin and dedicated Vulkan upload/readback buffers. One all-page bundle precedes gate
closure and entered writers must drain. Initial real tracker/protection publication and final
Register/readiness both qualify the device generation under short mapping guards. The transfer
reads physical pins only, records actual ordered copies/barriers and polls a private submission
identity: exact queue-submit success, checked native timeline progress, checked zero-timeout
host semaphore wait, explicit transfer-to-host read visibility and checked mapped-memory
flush/invalidate and matching real readback are mandatory. No callback/Flush/Finish or cached tick
is completion evidence. Pending content has no page-table/LRU membership. Dirty controlled content
cannot be returned to a protected reader.

The registration-first cached continuation invokes actual Memory prepared cached Write8/16/32/64
and publishes to every bounded physical device alias while the device mapping remains qualified.
Actual CPU/PTE execution guards end before tracker callbacks. GPU-dirty aliases refuse pending a
preservation adapter. Refresh uses another hidden allocation and only replaces the old buffer
after checked completion/readback. Errors and uncertain effects retain owners as Unknown. A
renderer idle success after all producer joins retires native resources, never converts Unknown
to successful data. Failed idle retains them. Cancelled gates and trusted Unknown recovery are
still fail-closed/incomplete; do not enable general routing on that basis.

The new pipeline suite composes actual production cache/native-transfer/checked-submit bodies
with real HostMemory/PageTable/CPU stores/WordManager. Both orderings end at backing77/upload77
with consistent real tracker state; the prior disabled-handshake control remains backing77/
upload10. Delayed worker and device completion, cold/queued/submitted remap, busy mapping metadata,
dirty physical alias, slot reuse, submit/counter/invalidate failures and bounded alias capacity
are exercised. Removing target completion fails 82; losing post-store alias publication fails 83;
omitting explicit host-read visibility fails 84.
Vulkan dispatch, allocator/device execution and binding teardown collaborators remain fixtures;
native CI compiles the actual Vulkan/Rasterizer endpoints. Real hardware evidence and actual
normal CPU caller/deferred-write enrollment are still required before F2 can be closed.

## Controlled first-registration ordering and cached continuation: 2026-10-15 evening

Continued from `8ca6197c67` on `codex/issue-372-fixes`. The user's step-3 gate explicitly requires
both orderings, final backing77/upload77, real tracker consistency, and a disabled-handshake
backing77/upload10 control. Returning NeedSlow alone is not a passing registered-first case.

The new physical registration bundle deduplicates aliases and enqueues one atomic bounded range
request before closing every covered page. Entered writers on the second/last coarse page must
drain before registration can run. Completed registration can issue a separate cached-write
request, retained through the actual prepared Memory cached store and BufferCache post-store
tracker publication. Snapshot and explicit tracked-store publication exclude each other without
spinning; the ordinary prepared fast store retains its existing no-allocation/page-local path.

Windows `run_first_registration_tests.py --repeat 10` passes both orderings with actual Memory
Write/WriteCached, HostMemory/PageTable, BufferCache registration/upload bodies and real
MemoryTrackerBase/WordManager. Registered-first proceeds through actual cached store77, real
dirty publication for two device aliases, subsequent upload77 and clean tracker. The actual
ordinary Write body with handshake disabled repeatedly produces backing77/cache10 with clean
tracker. Removing the post-store tracker update fails (72), closing only the start page fails
(71), and removing tracked-store/snapshot exclusion fails (75). Stale prepared permission and
GPU-dirty refusal are checked before effects. The new suite is included in integration CI.
Tracker-publication exceptions after an actual store explicitly mark the cached request Unknown;
even a caller catching the exception cannot complete it, and conflicting readers remain parked.

The existing GPU-plan, representation-pin, cache-representation, range-admission, page-writer
and CPU-mapping suites also passed locally during this continuation. Full Android compilation
and Linux native validation remain GitHub Workflow work, not local Android builds.

**Scope remains controlled groundwork.** Complete footprint/owner discovery, qualified stable
device aliases, physical-plan upload, GPU-dirty preservation and real checked Vulkan completion
are not installed into ordinary production registration. The new test uses fixture device alias
fanout, allocation and synchronous upload/completion; it is not native driver/device proof. No
DMA/NCE/JIT/raw-pointer/image routing is enabled. F2 and the full end-to-end stage remain OPEN.

## Resumed baseline: 2026-10-15

The user authorized merging the validated #380 integration into personal main and the #375
branch, and continuing yesterday's M3B work there. Both remote branches were fast-forwarded to
`105779d2c4`. The author of #380 supplied its Android device evidence; the user declined an
additional local device run. That decision does not provide device evidence for new M3B changes.

The prior F2/M3B branch at `80c4f4d7f7` was merged as `3e5b3228c3`. Its CPU CopyBlock snapshot,
host callback acknowledgement and candidate/negative-control tests remain intact. The sole
test conflict retained the #380 logging stub and acknowledgement include. Windows tests passed:
CopyBlock, preservation acknowledgement (three repetitions), 24 candidate cases (three repetitions)
and unmap alias preservation. None is a passing production F2 fixed-code witness.

## First production boundary: DMA method continuation

`DmaPusher::SetMethodAdmission` supplies a nonblocking GPU-thread preflight before scalar, inline
or non-incrementing batch effects, including `ConsumeSink`. False copies the unexecuted words
and parks the channel without consuming the rejected method. Its decoder state and original
command position survive retry. A callback is installed before the first submission and cannot
be replaced under queued or executing work. No real admission adapter is installed yet.

Existing `Control::Scheduler::RetrySuspended` retries the saved method on its original GPU
thread. The existing ThreadManager suspended-channel polling keeps servicing queue callbacks
and other channels. An unready admission does not wait under the channel scheduler guard.
This is a decoder boundary, not permission to replay an effectful draw or an entire command list.

`run_dma_admission_tests.py` compiles the actual DMA class/header/implementation and actual
channel scheduler, with fixture engines, guest command reads, Puller dependencies and Core/GPU
shells. Windows positive cases cover prefix retirement, blocked same-channel tail, independent
channel progress, increasing/non-increasing/increase-once/inline commands, immutable callback
installation and semaphore continuation compatibility. A control bypassing admission must fail
because the blocked method executes. The extracted-source manifest records the real inputs.
No actual driver, real physical admission, transfer visibility or production mapping pin is tested.

## Physical backing views and immutable plans

`HostMemory::PinPhysicalRange` materializes a complete bounded physical extent and returns a
`PinnedPhysicalMemory` retaining the actual shared Impl and backing epoch. The immutable extent
uses `backing_base`, never a remappable virtual alias. A retired epoch rejects new obligations
elsewhere, while already accepted views keep storage allocated; `OwnerAlive` and storage lifetime
are deliberately distinct. Moves/replacement of the HostMemory object must be excluded during
resolution by its owner; this API does not make concurrent destruction of a borrowed object safe.
Invalid/empty/overflowing extents return no view, and materialization failure precedes exposure.

Windows GCC standalone execution of the actual HostMemory implementation passes alias remap,
cross-page extent, moved owner/view, epoch retirement, backing replacement and final release
checks using real OS mappings. Only logging/assert formatting and unused dynamic-library filename
formatting are stubbed. The same runner additionally checks real lazy cross-chunk creation and
FD cleanup on Linux. These Linux checks subsequently passed in run `38674435729`. No CPU lookup
epoch, device/GPU representation pin or physical admission is connected by this storage API.

The prepared `PhysicalAccessPlan::Build` composes existing resolver snapshots with these real
pins. It validates exact ordered coverage, holes, extent bounds and backing identity before
materialization, then rechecks the snapshot under GPU-then-device mapping locks. Pin acquisition
runs outside those locks. If a mapping generation changed, the plan is rejected; an accepted
plan retains its original physical targets rather than re-resolving after a remap. Plans contain
no mapping-use lock. Resource owners retire after temporary validation locks, including rejection.
Windows tests use fixture mapping stamps with real allocations and exercise stale/retired stamps,
backing mismatch, invalid coverage/hole/overflow and retained split views. Engine footprint and
CPU first-registration adapters remain separate from this allocation ownership boundary.

The post-pin validation checkpoint also permits a controlled mapping change after pin acquisition.
Its Windows test rejects that stale plan. A negative control validates only before acquiring pins
and must fail by accepting the remap race. Linux additionally rejects a malformed suffix before
materializing any cold chunk; these Linux checks passed in the next remote run below.

Run `38672294519` on `41c6ce4ac3` finished with the complete Linux production/regression job
successful, including actual DMA/scheduler positive/negative controls and 24 candidate cases
(three repetitions). Android's complete compile error list contained one strict Clang lambda
capture shadow in the rejoined acknowledgement helper. Renamed its capture without changing
ownership/cancellation semantics; this fix accompanies the new storage/plan slice.

Run `38674435729` at `a6bc459af014e9b34542dd95df932c70b6a3567c` succeeded in both jobs:
real Android arm64 production libraries and complete Linux production/unit/HostMemory/coherency/
lifecycle validation. The Linux physical-pin tests include lazy cross-chunk publication, FD
cleanup, malformed-plan rejection before cold publication and the post-pin race negative control.

## GPU resolver adapter

`MemoryManager::PinPhysicalRanges` now composes the actual GMMU snapshot resolver with
`DeviceMemoryManager::PinGuestPhysicalRange` and `PhysicalAccessPlan::BuildWithPins`. The
device provider checks the exact backing epoch before obtaining a real HostMemory pin; the plan
builder checks returned epoch identity, physical offset and length, then validates the mapping
snapshot after all pins. No remappable guest virtual pointer is retained in the result.

`run_gpu_plan_tests.py` first runs the real pin/plan suite, then compiles the actual GPU map/unmap,
translation, resolver and pin methods plus actual device snapshot/translation/pin methods against
real HostMemory allocations. GPU/device PTE storage, manager shells, rasterizer notifications
and submapped enumeration remain fixtures. Windows checks pass noncontiguous and partial ranges,
real GMMU big-page translation, holes/empty requests, GPU remap at the post-pin checkpoint, device
remap, and old physical storage surviving both manager/arena exits. Input hashes identify seams.
This adapter has no active engine/cache consumer or data-admission privilege. Run `38676646541`
at `d6c593e4dfce3ae88f25b0e6d5a187caf3c314bb` passed both real Android arm64 production compilation
and the complete Linux production/unit/HostMemory/coherency/lifecycle suite, including the new
actual GPU-plan resolver tests and the pin/plan positive/negative controls.

## Engine register projection

`EngineInterface::ProjectPendingRegisters` validates the whole pending sink against register
bounds and the execution mask, then applies ordered writes to a register-array copy. It never
calls CallMethod/ConsumeSink, mutates committed state or drains the pending queue. MaxwellDMA's
`ProjectPendingState` exposes this projection for future footprint calculation; it does not
authorize the launch or infer page kind, memory layout, semaphore or remap effects.

Windows `run_engine_projection_tests.py` compiles the actual DMA register layout, constructor,
CallMethod/CallMultiMethod/ConsumeSinkImpl and projection. Launch is a side-effect spy; System
and MemoryManager are shells. Last-write ordering, idempotence, unchanged committed registers,
retained sink, equivalence with later real sink consumption, and execution/out-of-bounds rejection
pass. Omitting the execution mask produces an explicit failing negative control. Run `38678613933`
at `0eaedb5eb2e3f8d7b0aaa94f90400493cd61cd77` passed Android arm64 production compilation and the
complete Linux production/regression job, including this projection test and its negative control.
No active engine admission callback is installed, and production F2 remains unresolved.

## Restricted single-line DMA description

`MaxwellDMA::DescribeSingleLineCopy` projects pending registers without consuming the sink and
accepts exactly one non-pipelined, virtual-addressed, single-line launch encoding. Additional
semaphore, interrupt, remap, physical-address, multiline, reduction and reserved bits are rejected.
Zero lengths, extents above 32 MiB, upper address bits beyond 40 bits and overflowing extents are
ineligible. The limit bounds future page-plan construction; it does not limit existing execution.

The description carries logical addresses/length and conservative 512-byte group coverage for
both plain copies and Launch's pitch/block-linear 16-byte block permutation. This coverage is
for synchronization only, never exact dirty publication. PageKind/layout eligibility, mapping
stability and GPU representation ownership still need separate proof. This API neither installs
an admission callback nor changes Launch behavior, and does not describe a whole NI batch.

Windows extraction tests compile the actual descriptor with the actual DMA register layout and
pure projection. Tests pass flag rejection, source/destination bounds, exact 40-bit endpoint,
32 MiB boundary, unchanged sink/Launch spy, and exhaustive offsets with cross-group permutation
coverage. Launch remains a spy, not real DMA or driver execution. The execution-mask negative
control still fails as required. Run `38680939033` at
`5a072182e71107de384a38249ca0ebf8b7791fba` passed both Android arm64 production compilation and
the complete Linux production/regression job, including descriptor/projection positive and
negative controls. F2 remains unresolved.

## Joint source/destination storage preparation

`MemoryManager::PinPhysicalCopyRanges` resolves both extents, requires matching GPU/device/
backing identities, builds real storage pins outside mapping locks, then revalidates BOTH
snapshots under one GPU-then-device lock pair after all pins. A mapping change after either
individual build rejects the entire pair. Accepted plans retain their original storage after
later remaps; no mapping-use lease is retained, and their future use still requires admission.

`MaxwellDMA::PrepareSingleLineStorage` composes the pure restricted description with this real
pair adapter for its conservative source/destination coverage. It does not consume the sink,
call Launch, preserve cache contents, acquire GPU representation ownership or authorize access.
The returned logical length is distinct from synchronization coverage. PageKind/layout
eligibility and exact publication remain unresolved; no execution callback is installed.

The Windows real GMMU/HostMemory runner checks paired split/alias coverage, empty/hole rejection,
GPU and device remaps after both individual pin builds, and old physical view retention. Removing
the final joint validation produces the explicit `stale paired mapping accepted` negative control.
The combined runner additionally compiles the actual DMA projection/description/storage adapter
against those real extracted GPU/device resolver methods and real OS allocations; Launch is
still a spy and PTE storage/manager shells are fixtures. The smaller engine test checks exact
coverage arguments, unsupported-encoding rejection before resolution, failure propagation and
unchanged sink/Launch count. This preparation is not an executable DMA plan or an F2 repair.
Run `38683254478` at `8a0914f6ec054115b422e02753fbd4932b407dbd` passed both Android arm64
production compilation and the full Linux production/regression job, including combined DMA
storage preparation and the joint-validation negative control. This does not close F2.

## Next focus: DMA admission activation prerequisites

The user's 2026-10-15 review narrows the next activation milestone to the restricted single-line
DMA path. These are blockers for **activating admission**, not automatic merge blockers for inert
or test-only infrastructure. Current production execution remains unchanged for every unproved
path; no broader DMA eligibility is implied by storage-plan success.

| Risk | Current assessment | Priority |
| --- | --- | --- |
| Joint source/destination snapshot validation | Reasonable point-in-time design; not execution permission | Low |
| Remap after plan return, before execution | Requires old-plan/mutator admission contract | High |
| Physical coverage differs from representation footprint | No production representation admission established | High |
| Direct JIT/NCE bypasses controlled access | Still explicitly excluded | High |
| F2 first-cache-registration race | Not repaired in production | High |
| Shutdown lifecycle | Existing lock-order fixes retained; new pin/cancellation lifetime still needs proof | Medium |

### Representation ownership audit and required evidence

At `8a0914f6ec`, the actual Vulkan `Buffer` owns staged storage (`vk::Buffer`, views) and an optional
`unique_ptr<DirectGuestMapping>` containing the imported VkBuffer, memory and guest backing.
`BufferCache::DeleteBuffer` unregisters the representation, moves it from `slot_buffers` into
`DelayedDestructionRing`, and erases its slot. `DelayedDestructionRing::Tick` clears a ring slot;
`Flush` clears every slot. `BufferCacheRuntime::CopyBuffer` records raw source/destination
VkBuffer handles and copy geometry. Neither a BufferId, a captured raw VkBuffer, nor the current
HostMemory pin establishes ownership of a parked DMA representation. The existing destruction
ring is not evidence for an arbitrarily long parked-plan lifetime. This is a gap in the new
activation proof, not a claim that existing submitted-work lifetime management is broken.

The next implementation must provide real source/destination payload ownership across cache
replacement/unregistration, slot reuse, queued recording, submit, checked device completion and
Partial/Unknown/exit cancellation. Resource identity, physical-to-representation footprint and
generation must be immutable. A parked plan cannot keep a cache mutex, mapping-use lease or
whole-owner reservation; a reference-counted wrapper alone does not prove footprint/visibility.
Unknown completion must not release resources as if an exact successful tick had been observed.

### Staged Vulkan payload retention slice

The next production slice moves the real staged `vk::Buffer`/VMA allocation into
`StagedBufferPayload`; `Buffer` retains its ordinary owner through a shared pointer. A bounded
`StagedBufferPin` independently holds that same payload and immutable buffer-relative offset/
length. Pin construction rejects empty, absent and out-of-bounds extents. Live payload identity
is independent of a cache slot id. Moving a cache Buffer into the existing destruction ring,
erasing/reusing its slot, or clearing the ring cannot destroy that allocation while a pin exists.
The final owner runs the existing actual `vk::Buffer::Release` exactly once.

Only non-direct buffers expose this pin; direct/imported representations remain ineligible.
Conversion to direct storage clears old views and releases the cache's staged owner, while an
existing staged pin keeps its original VkBuffer. No texel-view ownership is promised: this pin
is scoped to DMA's VkBuffer payload, not arbitrary shader/image views. Existing Buffer lookup,
execution, cache registration, delayed retirement and submission policy stay in place; there
is no DMA activation or new pin consumer yet.

Windows `run_representation_pin_tests.py` passes actual owning Buffer fields/pin/implicit moves,
actual SlotVector and DelayedDestructionRing, actual payload and actual Vulkan RAII/Release,
with fake creation/device services and a fake VMA destruction callback. It checks slot reuse,
Tick/Flush, replacement, copies/moves, bounds and exactly-once final resource release. A negative
control retaining only the raw payload identity fails explicitly at ring retirement. This is
resource-owner retention evidence with a fake driver, not device execution/completion evidence.
The real production importer extraction also passes its success/failure/FD-ownership tests.

**Lifecycle proof remains partial.** Device/VMA allocator domains must outlive every pin; this
slice does not establish shutdown sealing or retain those domains. Physical/representation
footprint matching, cache-content preservation, immutable submit ownership, checked completion/
Unknown handling and cancellation remain activation blockers. A shared resource owner alone
does not close those contracts. The original F2 gate still reports backing 77/cache 10 and exit 1.
Run `38692337709` at `cc324dff81303649e9385c2dd271424be3604c31` passed Android arm64 production
compilation and the full Linux production/regression job, including staged payload ownership
and raw-identity negative controls. This is not a full device-domain lifetime proof or F2 repair.

### Shutdown seal (after cc324dff81)

Each runtime now prepares a monotonic `StagedBufferPinDomain` shared by its staged payloads.
`RasterizerVulkan::Shutdown` seals that domain after taking exclusive producer exclusion and
before releasing the shutdown guard for pipeline/fence/recording drains. Runtime destruction
seals it again as a fallback. Seal performs no wait and releases no Vulkan resources. New pin
acquisitions observing the seal reject; existing pins stop exposing a usable VkBuffer through
Handle/IsUsable while retaining their payload until the final owner releases it. A creation
racing seal may finish with an unusable pin if its acquisition check preceded seal.

Windows tests pass the actual runtime seal method and payload domain, rejection after repeated
seal, handle invalidation without early VMA destruction, and exactly-once final release. A
missing-seal negative control explicitly accepts a post-shutdown pin and fails. Actual extracted
renderer shutdown orchestration also passes its producer-exclusion/order assertion and existing
recording, presentation, fence, turbo and completion-worker drain tests. Importer tests still pass.

This is cancellation of future handle lookup, **not a complete device-domain lifetime proof**.
Previously extracted handles and submitted commands are not revoked. No outstanding resource
may outlive the real Device/VMA allocator; this slice does not enforce final-pin drainage or
connect parked-plan cancellation/Unknown completion to that boundary. It deliberately does not
force-free old payloads at seal or reinterpret callback return/unchecked WaitIdle as completion.
There is still no active DMA admission consumer and the original F2 failure remains open.
The cc324dff81 native run finished successfully before this slice was submitted. Run
`38694742732` at `be4eb320115a9d4699d198270de8d3cc60b12344` passed both Android arm64 production
compilation and complete Linux production/regressions, including seal/ownership negative controls
and actual renderer shutdown/drain tests. The remaining activation gaps above are unchanged.

### Existing-cache physical correspondence

`BufferCache::PinExistingDmaRepresentation` now exposes a pure, existing-buffer-only preparation
query for pin-capable backends. The caller retains the cache mutex and device/backing/renderer
owners on the GPU/cache thread. It rejects empty/out-of-range requests, missing/null/stale cache
ids, partial buffer coverage, inconsistent cache-page ids, active scatter and any pending gather
scatter. It never invokes FindBuffer, creates/merges buffers, synchronizes contents, resolves
scatter, publishes dirty state or issues GPU work. Direct/imported pins remain rejected by the
actual Vulkan Buffer pin API; unsupported backend pin types are not instantiated.

The pure `MatchesDevicePhysicalPlanLocked` compares EVERY relevant device page with the ordered
physical spans retained in the supplied plan, including exact backing epoch identity and range
bounds. Empty/moved-from plans cannot pass. After this device snapshot check, the query obtains
the actual buffer-relative Vulkan resource pin outside the mapping lock, then checks the device
epoch/generation again. A remap during resource acquisition rejects the query. Cache locking
and mapping locks are not retained in the returned resource pin. An old GVA plan is compared
against its original physical targets, not redirected by a later GVA retranslation.

`run_cache_representation_tests.py` composes the preceding suites' hash-checked generated actual
GMMU/device/HostMemory and Vulkan Buffer/payload/RAII inputs with the actual cache lookup/pin
method and page-id checks. Cache/PTE table storage, registration, VMA/device services and the
post-resource-pin rendezvous remain fixtures. Windows checks pass split-page physical matching,
changed interior PTEs, different backing epochs, resource-acquisition remap, old GVA targets,
missing/stale/reused cache slots, partial coverage, pending-scatter refusal, moved-from rejection,
and domain seal. Removing the physical comparison explicitly accepts mismatched storage; removing
the final generation check explicitly accepts the post-pin remap. Both negative controls fail
as required. The workflow runs this suite after both input generators; stale source hashes reject.

**This proves current address correspondence, not historical registration/content authority.**
It does not prove the buffer was populated from this physical allocation, join first-registration
epochs with CPU stores, preserve old contents across a mutator, establish layout eligibility,
atomically admit source/destination, execute the pinned resource path, or drain the final pin at
shutdown. The supplied physical plan and returned representation pin must remain paired by a
future adapter with those contracts. No production admission consumer is installed, all ordinary
DMA execution remains unchanged, and the original F2 gate still fails with backing 77/cache 10.
Run `38698088610` at `f1cd4d870169ad865820ad1e9c31353626ec9f2e` passed both Android arm64
production compilation and the full Linux production/regression job, including correspondence
and post-resource-pin remap positive/negative controls. The content/registration gap remains open.

### Backing-owned range ordering groundwork

The next slice gives each real `GuestBackingEpoch` one `PhysicalRangeAdmission` metadata domain.
Arena retirement seals that domain before marking the epoch dead. Ordinary CPU/GPU access does
not enqueue requests yet, so this does not change existing access eligibility or close F2.
The domain is shared by the actual allocation identity, not a cache slot, VA or test dispatcher.

Ready logical segments enqueue bounded immutable read/write intervals and retained resources
outside cache/PTE locks. `TryStart` grants ALL intervals atomically or parks with NONE granted.
Earlier conflicting pending requests order later ones; shared readers and disjoint scopes can
proceed. Inline conflicts are rejected through an explicit execution-scope chain, including an
outer same-domain scope hidden beneath a different nested domain. Tickets retain domain identity
to avoid stale-owner ABA. There are no worker threads, queue pumps, GPU waits or owner-wide leases.
Only ready segments may enqueue: pending requests must not conceal unresolved external dependencies.
Preparation/capacity/seal failure is never authorization to fall through to an unguarded access.

`EnqueuePhysicalCopyAdmission` prepares a real physical-copy bundle on its exact backing domain:
source segments read, destination segments write, with actual source/destination storage and the
supplied GPU representation owners retained together. It rejects empty/moved/mixed-backing plans,
dead epochs, missing representation ownership and unsupported fragmentation (64 intervals maximum).
The metadata queue is bounded to 256 requests. It does not certify representation contents or
match that opaque owner to its physical footprint; the real adapter must supply that proof.

Pending requests can cancel; seal cancels them without releasing resource owners under the
metadata mutex. Running/Unknown obligations retain resources. Unknown conservatively quarantines
the request's physical footprint, including readers, while permitting disjoint work. No recovery
API pretends to supply device quiescence: Unknown cannot be completed/cancelled by this slice,
and its unresolved resources intentionally remain retained. `Complete` is an explicit caller
obligation after real completion/visibility or proof of no effects, not a Vulkan completion signal.

Windows `run_physical_admission_tests.py` composes the actual domain, actual physical-copy builder,
HostMemory and preceding GMMU/cache/Vulkan-owner fixture inputs. It passes atomic source/destination
parking, opposite-copy concurrent TryStart, ordered readers/writers, real storage bytes with an
independently released CPU producer, aliases, nested reentry, foreign identities, bounds/capacity,
resource-destructor reentry after unlock, arena retirement and Unknown quarantine. CPU store,
registration and memcpy executor participation in this suite remain fixtures, not Memory::Write,
BufferCache first-registration or a live scheduler adapter. Bypassing pending-writer order and
bypassing conflict checks each produce explicit failing negative controls. Cross-platform actual
HostMemory/GPU-plan regressions also pass. The additional chunk-backing runner requires Linux;
its Windows attempt cannot compile Linux mmap headers, so its evidence awaits the remote Linux job.

**No activation:** initially uncached Memory::Write/WriteBlock/CAS still do not join this domain;
first-cache-registration/upload also does not join. CPU lookup can still bypass notification after
cached marking. Before activation, implement stable controlled CPU resolution, preservation-before-
store and exact successful-store publication, registration epoch/content provenance, mapping-mutator
ordering, real scheduler park/resume and checked completion/Unknown/final-pin drainage. The original
F2 correctness gate still reports backing 77/cache 10 and exits 1. Native evidence for this new
range-ordering slice is pending.

### Required creation-to-execution order

1. Purely classify one supported launch and project pending registers without consuming effects.
2. Resolve/pin both physical storage ranges and validate them jointly; establish immutable,
   matching representation ownership and footprints. No data-access authority exists yet.
3. Order mapping-mutator intent and the old-plan request in the same admission domain. A mutator
   winning before plan acceptance must reject/reprepare the unexecuted plan; after acceptance,
   execution must use the accepted pinned targets with the required preservation ordering.
   Re-resolving current VA inside the existing Launch is not execution of the old plan.
4. Atomically TryStart source-read and destination-write ranges as one request, including physical
   aliases/overlap. Never retain a granted source while parking for its destination. CPU first
   registration and controlled stores must share this ordering domain to close the F2 gap.
5. On conflict park the unexecuted single-line method in the actual channel scheduler and return
   the GPU worker; independent preservation/release work must run. Retry the retained immutable
   request without replaying earlier effects or silently granting admission to legacy Launch.
6. Execute only the proved representation path, publish exact successful write extents, and
   retain resources/reservations until the defined visibility/completion or cancellation outcome.

Required deterministic production-adapter tests cover mutator-before-accept, remap-after-return,
mutator-during-park, representation replacement/slot reuse while parked, same-allocation
source/destination overlap, other-channel CPU dependency progress, no effected-prefix replay,
and shutdown/Unknown while parked or submitted. Existing real DmaPusher/Control::Scheduler
boundary tests are a prerequisite, not proof that this physical adapter is active. Use actual
mapping mutator methods and independently released CPU stores; keep explicit negative controls.

Keep the original F2 `backing=77`, `cache=10` failure assertion and its failing `--require-fixed`
gate. New infrastructure, candidate or scheduler-boundary passes cannot close that witness.

## Prepared physical-page writer and registration boundary (inert)

Native run [38700231751](https://github.com/Splaser/emulator/actions/runs/38700231751)
passed Android production and the complete Linux production/regression jobs for `80bf2b85f3`.
The completed PR380 integration branch was subsequently deleted locally and remotely after
checking ancestry; development now continues on `codex/issue-372-fixes`, synchronized with main.

`HostMemory::PreparePhysicalCpuPage` now prepares a real pinned physical page and its canonical
`GuestBackingEpoch::pages` gate. Different GPU/device virtual aliases of the same backing page
obtain the same gate; different physical pages or backing epochs do not. Cold preparation uses
a bounded registry (65,536 pages); it can allocate. Prepared scalar stores do not query that
registry, allocate a range ticket, copy a shared owner, or acquire cache/PTE locks.

The prepared physical interface executes actual bounded memcpy or aligned unsigned CAS and
publishes cumulative exact modified-byte bits and a release sequence AFTER a successful store,
before releasing its page-local writer count. Failed CAS publishes nothing. Closing or tracked
pages return `NeedSlow` without storing; no fallback is authorized. This is not CPU-VA resolution,
a cache dirty-bit consumer, or support for cross-page stores, u128 CAS or registered-page writes.
Concurrent conflicting CPU operations still need their normal language/memory-access contract;
the writer count orders registration, not overlapping scalar writes against each other.

First-registration preparation enqueues its whole-page write intent before atomically closing
fast writer entry. Nonblocking `TryStart` reports `Draining` until already entered writers have
published and exited, then attempts the backing range ticket. The returned registration retains
actual storage and the supplied representation owner. `PublishTracked` changes only gate state;
it neither certifies an upload nor completes the ticket. `CompleteWithExternalEvidence` still
requires the caller's real visibility/completion or no-effect proof. Destruction cancels pending
work or marks running work Unknown; cancellation never silently reopens the gate.

Windows `run_page_writer_tests.py` composes actual HostMemory, GPU/device translation, existing
cache representation lookup and Vulkan payload owners (creation/PTE/cache storage remain fixtures).
It checks aliases, a deterministically paused real store versus registration, exact byte publication,
failed/successful CAS, bounds, retirement and fail-closed cancellation. An independent releaser
unblocks the writer. 2,000 prepared byte stores perform zero measured hot allocations and do not
advance range-domain enqueue progress. Negative controls explicitly fail for ignoring writer drain
(49) and publishing before the store (50). This is bounded correctness evidence, not a performance
benchmark or normal Memory::Write/BufferCache::Register production routing.

**Activation remains blocked.** Normal CPU VA stores and actual first cache creation/upload do
not call these adapters. Preservation, stable CPU resolution, cache-content provenance, registered
slow writes and DMA participation must join this SAME handshake before use. Range admission alone
does not exclude these fast writers. Unknown retains resources intentionally; a trusted quiescence/
recovery owner and final-pin drainage before device/VMA destruction remain required. Destroying
the admission object is not recovery. The original F2 gate was rechecked and still reports
`expected=77 observed=10 backing=77`, with `require-fixed` exit 1. No DMA activation or eligibility
expansion is included in this slice.

### Publication consumption and failed registration activation gates

The user's next implementation priority is the controlled CPU VA store adapter; DMA admission
must remain disabled. Its required chain is `Memory::Write -> stable VA/physical resolution ->
page writer qualification -> actual store -> exact publication`. Preparation must occur cold
and hot accesses must reuse an owned stable page view, with explicit invalidation on CPU mapping,
address-space and backing changes. Calling `PreparePhysicalCpuPage` for each scalar access is
not an acceptable implementation. Writer qualification must precede first cache registration
even for an initially uncached page; cached-PTE checks alone cannot provide that ordering.

Current source audit: `GetPointerImpl` returns a nonzero PTE pointer without the rasterizer
callback; `MapPages` writes the atomic tagged pointer and plain `backing_addr` separately.
Thus pointer/tag sampling or a generation retry around unsynchronized `backing_addr` does not
yet provide a legal stable physical snapshot. Map/unmap, debug/cached tag changes, table resize,
current-process switch and destruction must be included in the prepared-view lifetime audit.
The existing `Write` scalar/cross-page branches, `WriteExclusive`/128, `WriteBlockImpl`,
`HandleRasterizerWrite` device-alias fanout/OnCPUWrite/dirty collection and uncached behavior
must retain their semantics until their individual controlled counterparts are proved. The
physical memcpy tests are not evidence for replacing those ordinary paths.

`Published()` is cumulative observation, not a consumable dirty queue. There is no clear or
incremental acknowledgement API. The current snapshot requires fast entry closed and writers
drained. A future consumer or tracked-writer extension must prove concurrent snapshot/sequence
validation and incremental acknowledgement without dropping writes during cleanup; it may not
clear the observed bitmap and assume it owns those modifications.

`PublishTracked()` means only that fast entry remains disabled. It is not registration-ready,
content-valid or upload-success state. The future adapter must separately track representation
validity and real upload visibility/completion. After uncertain/partial upload failure it must
mark Unknown and retain the ticket/resources, preventing dependent CPU/GPU access. A certified
no-effect failure may release its reservation only while the failed/uninitialized representation
remains inaccessible; such release never turns Tracked into valid content. Callback return,
Tracked state and a readable cumulative bitmap are insufficient completion evidence. These
failure transitions and their deterministic negative controls remain activation prerequisites.

## Remaining production adapters and acceptance gates

### Explicit prepared CPU Write execution (not ordinary-path activation)

`ecf9bb31e8` adds an explicit prepared-view overload of actual `Memory::Impl::Write`,
exposed through result-returning `WritePrepared8/16/32/64`. Ordinary Write wrappers and their
original body remain unchanged. Cold preparation captures a Memory-owned active-context epoch;
actual `SetCurrentPageTable` advances it even when switching away and back to the same table.
The view retains immutable mapping/physical/context fields. A prepared write checks caller
context identity, canonical 48-bit VA/page bounds, then takes nonblocking shared context/PTE
try-locks, validates the epochs/tag, enters the physical page writer, performs the actual store
and publishes before releasing that writer and the short execution guards. No callbacks,
suspension, registry lookup, physical pin creation or shared-owner copy occurs in this hot call.

Busy, stale, retired, wrong-context/cross-page, tracked/closing and cached/debug targets refuse
with a result and no effects; there is no fallback to legacy memcpy. In particular cached/debug
rejection does NOT replace preservation/device-alias/OnCPUWrite handling. This API is an inert
explicit entry with no ordinary caller installed, not a repair of the normal CPU path. Two
shared try-locks and byte publication are correctness groundwork, not a throughput claim.

The actual extracted prepared Write, SetCurrentPageTable, cold factory and actual page/physical
implementations pass Windows tests for context switch away/back, top-bit VA masking, wrong-page
and cross-page refusal, remap/tag invalidation, Busy under a mapping mutator and shared physical
alias gates. An independently released actual prepared writer races first-registration gate
closure: registration reports Draining until its exact store publication, and a later alias
writer refuses. Resource ownership supplied to this registration is a fixture, not a real
BufferCache upload. 2,000 prepared Memory writes perform zero measured hot allocations and no
range enqueue. Removing hot active-context validation fails explicitly (54); missing cold
post-pin validation remains control 51. The full Memory::Write -> real BufferCache first
registration/preservation/cached-slow-path integration is STILL REQUIRED. DMA remains disabled.

Native run 38708541040 passed both jobs for its parent `06408b9ce5`. New native
[38711873776](https://github.com/Splaser/emulator/actions/runs/38711873776) verifies
`ecf9bb31e8`; its result is pending at this handoff. The next stage is **M3B - Controlled First
Registration Integration**, whose scope, real source edges, whole-coarse-range coverage,
completion/Unknown gates and home-machine commands are saved in
[`docs/M3B_CONTROLLED_FIRST_REGISTRATION_HANDOFF.md`](../../docs/M3B_CONTROLLED_FIRST_REGISTRATION_HANDOFF.md).
That controlled BufferCache end-to-end stage is not implemented yet; none of the prepared
Write passes closes the original backing77/cache10 failure.

### CPU VA cold-resolution dependency

Native [38704560296](https://github.com/Splaser/emulator/actions/runs/38704560296) passed both
Android production and the complete Linux production/regressions for `0ed92c3efd`, including
the real page writer suite. This does not close F2 or activate any new ordinary access path.

`PageTable::SnapshotMapping` now samples the tag and plain backing address together under a
short shared metadata guard. The actual `Memory::Impl::MapPages` and debug/cached tag mutators
take the corresponding exclusive guard; `Resize` invalidates snapshots under that same guard.
The domain generation invalidates prior snapshots conservatively for the entire table. Owner
retirement marks retained tokens dead even when their domain remains owned by old snapshots.
Existing raw/JIT/page-table readers are not converted into this snapshot protocol.

Actual `Memory::PrepareControlledCpuPage` takes this cold snapshot, drops the PTE guard, prepares
the real HostMemory physical page, then rechecks the table and mapping generation. The returned
view pairs the mapping token with owned physical storage and canonical page gate. It performs
no CPU store, callbacks, registration or data admission. Callers must keep the process/PageTable/
HostMemory alive and use the memory-owner thread. The token does not own the process or detect
an address-space switch away and back; future hot access needs an explicit active-context
qualification and bounded mapping-use protocol. `IsCurrent` alone is not execution permission.
Table-wide invalidation is correctness groundwork, not a hot-path performance claim. No physical
pin, shared view creation or registry lookup has been inserted into scalar `Memory::Write`.

Windows `run_cpu_mapping_tests.py` uses the actual PageTable implementation/VirtualBuffer and
HostMemory plus extracted actual Memory cold-preparation and MapPages bodies. System/device
collaborators, logging/formatting and controlled rendezvous remain fixtures. It checks same-page
physical aliases, remap during cold preparation, tag/unmap/resize/destruction invalidation and
mutator exclusion of snapshot reads between field writes. Removing the post-pin validation
accepts the remap and fails explicitly (51). Candidate and unsafe-insertion tests retain their
fixture PTEs and original failure semantics; their new metadata guards do not certify production
CPU writes. Next work must connect reusable views and full writer qualification to actual
Memory::Write and real first registration, preserving OnCPUWrite/device alias checks and all
unconverted paths. DMA admission stays disabled.

1. Physical backing plans and the GPU resolver adapter are implemented. Add representation pins
   and the controlled CPU resolution adapter. Resolution may not hold a mapping-use lease across a parked
   channel. Revalidation cannot silently redirect an already accepted old plan to a new mapping.
2. Derive supported engine footprints without effectful decode. Pending `method_sink` register
   writes must be projected by pure resolution or otherwise bounded safely; running ConsumeSink
   to discover the footprint would precede this boundary and invalidate its pre-effect contract.
   Unsupported macro/NI batches, image layouts and unbounded footprints remain ineligible.
3. Join initially uncached controlled CPU stores and first cache registration with the same
   linearizable epoch handshake. Acquire a stable view before a store and publish exact successful
   extents afterwards; failed CAS publishes nothing. CopyBlock stays outside the transaction.
4. Install range admission outside cache/PTE locks, compose old-plan preservation and mapping
   mutation in a consistent owner order, and reject conflicting inline reentry without self-wait.
5. Connect real transfer visibility, checked completion/failure acknowledgement, resource retention
   for Partial/Unknown and cancellation/sealing before manager/renderer destruction. Callback
   completion alone is not Vulkan completion; suspended external dependencies cannot retain
   broad data admission that blocks their required CPU producer.
6. Run protected-path F2 tests with independent CPU-store release and GPU-attempt rendezvous,
   retaining stale-byte assertions and old-code controls. Existing five F1/F2 witnesses still
   characterize the unprotected production path. Candidate passes are not fixed-code passes.

F1 native/NCE access, direct JIT fastmem, raw/kernel pointers, F3 and image/buffer authority are
still outside this controlled-path work. Do not activate a cache-local blocking mutex to bypass
the queue-cycle counterexamples in `M3B_F2_IMPLEMENTATION_REPORT.md`.
