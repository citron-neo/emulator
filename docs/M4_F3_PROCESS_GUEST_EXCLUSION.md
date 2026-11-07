# M4 process guest exclusion: implementation sequence

This is the active follow-up to the
[whole-guest feasibility decision](M4_F3_GUEST_EXCLUSION_FEASIBILITY_AUDIT.md).
The ownership boundary is KProcess, its PageTable and physical backing, including
other Memory contexts, CPU aliases and independent producers of that backing.
Per-Memory retirement and a private arena are not certificates for this boundary.
The remaining roadmap has three phases, listed at the end. Lifecycle fixes and
the implementation slices below are prerequisites within those phases, not
additional M4 milestones. None currently authorizes ordinary Guest admission.

## Required state and resource order

| State | Required real evidence | Resources that remain owned |
| --- | --- | --- |
| Closing | Close all execution, ordinary CPU/API, mapping and host-service ingress | Process/PageTable/Memory, backing owner, pending producer jobs |
| Backends stopped | Actual backend return AND UnlockThread, all cores still closed | Same owners; this alone cannot authorize access |
| Services joined | Actual Audio/VIC/service/debugger/callback/raw-pointer users closed and joined | Same owners and their callback/loan state |
| Mappings frozen | Complete alias census and CPU/DAddr/GPU mapping mutation exclusion | Actual storage pins and every alias/mapping guard, not epoch identity alone |
| GPU drained | Queued and unsubmitted work accounted for; actual producer stop/completion and visibility | Native resources, queue/worker owners, retained pre-admission obligations |
| Exclusive | All previous constraints held continuously and content/copy checks satisfied | All owners transferred to the exact checked operation before enqueue |
| Unknown retained | Partial effects, uncertain completion, remap, retirement or missing owner | No automatic reopening or release based on an old completion |
| Released | Exact operation completed successfully and all release validations passed | Reopen admission last; restore the previously existing pause state |

There is no public API that marks Services joined or Exclusive from caller booleans.
A future provider must assemble actual owned closure/join/freeze capabilities.
The first backend-stop component below has no GPU adoption or success-admission API.

## Implemented first component: process-bound actual backend stop

KProcess::TryStopGuestExecution retains a real KScopedAutoObject reference to the
target process and captures its current Common::PageTable identity and backing
epoch. It closes all PhysicalCore execution domains before requesting any halt.
Its ProcessExecutionStop polls the actual cores under try-lock, requests an actual
nonblocking backend interrupt and only acknowledges BackendsStopped once every
entered backend has returned, including UnlockThread. Poll-budget exhaustion,
unsupported interrupt, stale owner or retired backing is sticky Unknown.

The stop now captures a RetainedBackingStorage from the actual HostMemory Impl,
along with the allocation epoch. The epoch remains identity only; the separate
opaque owner retains the real allocation/reservation and existing or subsequently
materialized lazy chunks. Capture exposes no pointer/FD, materializes no chunk
and does not freeze aliases or close a different Memory object. It is declared
before the process owners so allocation release follows process finalization.
System, Kernel and the HostMemory facade still need external lifetime protection
during capture/validation; retaining storage is not permission to race facade
move/destruction. This precursor cannot transfer to asynchronous GPU work.
Unknown backend closure remains sealed; a durable resource-isolation registry
is still required before such work can be admitted.

PhysicalCore's previous count spanned the entire RunThread function, including
Svc::Call. A SVC can yield its fiber after the backend has already exited, leaving
the old count indefinitely active. The count now covers backend initialization
and each actual RunThread/StepThread visit through context unlock, with no count
held across the later SVC/debugger callbacks. Those callbacks remain independent
writers; they are not silently reclassified as stopped.

Dynarmic 32/64 explicitly implement the nonblocking request with HaltExecution,
whose backend implementation publishes the halt reason atomically. Request
success is not return acknowledgement. NCE does not opt in: its current
SignalInterrupt locks thread parameters and can wait for a signal handshake.
An active unsupported backend therefore refuses this path rather than pretending
its stores have stopped. No new fault handler or page-fault parking is installed.

Cancellation reopens only this backend-only, no-GPU-effects request after complete
acknowledgement and owner validation. Each request owns its own closure objects;
an old or moved-from request cannot reopen a new operation. ProducerDrain's
moved-from Closure operations now safely refuse instead of dereferencing moved
state. This does not add a mechanism for cancelling an uncertain GPU operation.

For the sustained cohort wrapper, cancellation reserves the roster mutex before
reopening any domain and validates its retained snapshot revision under that same
reservation. Contention or intervening retirement refuses with all those gates
still closed. The reservation spans only stopped-state validation and atomic gate
operations; it spans no callbacks, owner destruction, or roster re-entry. This
removes the fallible late creation-release lock. CPU, initialization and cold
publication cancellation now share the ResumeBatch protocol below. Durable Unknown
resource ownership is still required before any effect-bearing adoption.

## Atomic managed-domain cancellation (phase-one prerequisite)

ProducerDrain::ResumeBatch collects actual owned closures, each already closed and
drained. It publishes permanent shared-gate membership while those local domains
remain closed, reopens local count words behind the closed shared gate, then opens
that gate with one final CAS. All CPU cores, initialization and cold publication
participate in one cohort cancellation batch; creation commits afterward under
its previously acquired, revision-validated roster reservation, with no further
fallible step. Backend-only cancellation batches the cores and initialization.

| Cancellation point | Producer entry | Seal/failure disposition |
| --- | --- | --- |
| Collect/validate | Every participating local closure remains closed | No reopening; quarantine owned closures |
| Publish membership | Shared gate closed; local words still closed | Failed publication CAS preserves local closure and seals the group |
| Reopen local words | Shared gate still closed | Even already reopened members remain unavailable |
| Final gate CAS | Entries can resume only after success | A preceding Seal prevents success and keeps the group closed |
| Successful cancellation | Normal managed entry resumes | A later Seal is a separate event, still propagated to the group |

The shared owner is assigned before the release CAS that first publishes Grouped
in the local word and is immutable afterward. Seal reads it only after acquiring
that bit. A Seal that wins before publication makes the publication CAS fail; the
batch then seals its gate before any local resume. For a participating member,
the local reopen CAS publishes the already-closed shared gate. A late successful
entry CAS acquires that publication and rechecks the shared gate before returning
an entry; refusal rolls the count back. This closes the delayed-entry/ABA window.

There is no raw coordinator pointer, atomic shared_ptr, callback, wait or retry in
the release protocol. A fixed-size batch holds at most sixteen borrowed closures
which must outlive it. First group formation allocates once, before any reopening;
allocation failure refuses. Ungrouped ordinary entry keeps its single CAS path;
grouped entry adds gate reads before and after that CAS. Member domains own the
shared gate directly, so dropping a failed batch cannot remove its protection.

Membership remains until domain destruction; subsequent cancellation reuses the
same gate and can extend it with previously ungrouped domains. Existing distinct
groups cannot be merged or detached. Seal/retirement of a joined member propagates
conservatively to the shared gate. A subset cancellation does not prove that other
members have drained, and the gate is not a whole-guest exclusion certificate.
Old released or moved-from closures cannot reopen or quarantine a new operation.

The stopped backend latch also revalidates initialization/core closures, so a
later Seal cannot remain hidden behind a previous BackendsStopped observation.
Standalone tests exercise normal and scheduled production bodies, with precise
mutants for early group opening, missing Seal propagation, destructive batch drop,
and omitted late-entry rollback. Actual cohort-body tests separately reject an
omitted publication member and the old sequential cancellation implementation.

This protocol covers no-effects cancellation only. Ordinary writes, cache/fault
PTE mutation, NCE, host-service/raw-pointer writers and GPU queues still require
their own closure/join/freeze facts. It adds no GPU adoption, checked completion
or durable backing/resource quarantine registry. Ordinary Guest admission remains
Unknown, and all three M4 phases remain unfinished.

## Cache publication owner routing and remaining ingress (phase one/two)

DeviceMemoryManager::UpdatePagesCachedCount now advances its current ASID together
with its Memory interface, after flushing pending spans to the previous interface.
Previously the discriminator remained the first ASID: for a contiguous A-B-A
device range, the last A span could be published into B's PageTable. The actual
counter/Maxwell forwarding/cache publication bodies now have a real HostMemory/PTE
regression for A-B-A, B-A-B, repeated B spans and a third owner, in both cache and
uncache directions. Every coalesced callback is expanded and compared to its exact
owner/address; unrelated PTEs must remain unchanged. Original ASID behavior,
switch-before-flush and omitted owner-boundary flush are separate negative controls.
Translation layout is injected; this proves routing, not KProcess lifetime pins.

The remaining cache/fault route cannot safely be enrolled by changing Begin to
BeginCold at the Memory wrapper. Actual callers have already changed state:

| Producer/call site | State preceding Memory publication | Required boundary |
| --- | --- | --- |
| ShaderCache::Register | Entry lists, invalidation cache and shader storage updated while cache locks are held | Close producer before registry effects and drain entered work |
| TextureCache::TrackImage/UntrackImage | Tracked flag changed; sparse segments may already have completed | Retain image/segments and account for partial publication |
| Buffer cache word tracking | Page tracking calls the device cache counter | Close tracker ingress before bookkeeping effects |
| DeviceMemoryManager::UpdatePagesCachedCount | Range lock held, page counts incremented/decremented, earlier spans possibly published | Exact owner retention plus transaction or pre-effect exclusion |
| NCE access fault -> Memory::InvalidateNCE -> GPU::InvalidateRegion | Direct rasterizer invalidation, including paths outside GPU command enqueue | Actual NCE pause and direct rasterizer ingress close-and-join |

ThreadManager::Synchronize only fences earlier queue dispatch. The owner loop can
still retain suspended channels and pending CPU-write continuations after a barrier;
it also does not prove Vulkan completion. InvalidateRegion and
FlushAndInvalidateRegion invoke the rasterizer directly outside PushCommand's
admission lock. Closing command enqueue alone therefore cannot freeze cache PTEs.

Keep the successful cache-bypass counterexample in the cohort suite. Next work
must close actual owner/direct-service ingress before its first bookkeeping effect,
join entered work (including parked continuations), and hold native GPU resources
through checked completion before publishing a cache/mapping freeze capability.
No new fault-handler wait, broad throwing gate, queue-barrier admission certificate,
or ordinary Guest admission is introduced by this routing correction.

## Terminal direct invalidation close-and-join (phase-two prerequisite)

ThreadManager::Stop now closes its two direct invalidation entry points as well
as command enqueue. External InvalidateRegion/FlushAndInvalidateRegion calls acquire
a TerminalIngress entry before touching the rasterizer and retain it through the
last callback effect/exception. This entry uses lock-free counter operations only:
no CAS retry, allocation, mutex, condition-variable notification, or waiting is
added to the calling/fault path. Healthy simultaneous readers are all admitted.

| Terminal transition | Ownership fact |
| --- | --- |
| Before StartThread | Counter closed; no external callback can use the unpublished rasterizer |
| External entry | Its admission RMW must precede Close; even a delayed initial read cannot bypass closure |
| Stop begins | Close direct entry and queue ingress before requesting worker cancellation |
| Worker join | Already executing owner-thread invalidations may finish; the worker join covers them |
| Queue cancellation | Destroy unexecuted captures outside write_lock, allowing entered callers awaiting their retirement to exit |
| Direct join | Cold owner waits for counted external calls, with no queue/cache/PTE lock held |
| Parent renderer shutdown | May proceed after these two entry families have exited |

Only the cold terminal owner sleeps while joining. Entry release is just an atomic
decrement; it does not notify through a library waiter pool from signal context.
Open/start and terminal teardown are owner-serialized, with caller/facade lifetime
still protected externally, including refused calls. Join is not a bounded live
stop poll and must not be called from an entry, fault, backend or worker callback.
This guard does not make the existing rasterizer/fault implementation signal-safe.

The actual ThreadManager/RunThread/queue fixture now covers both direct methods,
pre/post-start refusal, active external join after worker exit, 8,000 healthy
entries across eight concurrent callers, exception exit, queued-capture retirement before direct join,
delayed entry RMW, and already-entered worker continuation after external closure.
Five precise controls reject premature guard release, legacy Stop without ingress,
join-before-capture-cancellation, a stale prefix read bypassing Close, and refusal
of an already executing GPU-owner callback. The older blocking-handoff dependency
counterexample remains separate.

A successful bypass counterexample executes the actual GPU::Impl::OnCPUWrite body
after ThreadManager Stop against a live fixture rasterizer: this sibling entry is
still independent. Other raw rasterizer calls, services, callbacks, and native GPU
work also remain outside this guard. Stop cancels parked CPU-write captures;
suspended channels remain owned by the scheduler. Cancellation and worker join
are not checked device completion. No reversible
GPU freeze, Guest admission, alias/backing pin or durable Unknown registry is added.
This is one closed terminal ingress family within phase two, not a new milestone
or completion of any of the three M4 phases.

### Follow-up: terminal CPU-write cache callbacks

GPU::Impl::OnCPUWrite now delegates to ThreadManager::OnCPUWrite, using the same
terminal counter as both direct invalidation methods. An external entry lives
through the rasterizer's result or exception. Stop's existing worker join, queued
capture retirement and external join order therefore also covers these cache
callbacks. The already executing GPU owner may finish inline after external close.
No new wait, allocation or contention refusal is introduced on the calling path.

The original true/false rasterizer return passes through unchanged. Closed entry
returns false before touching rasterizer: in Memory::HandleRasterizerWrite this
only skips dirty collection for that call. It neither stops nor grants permission
to perform the CPU store. A cache callback may also leave asynchronous native work
pending; its return is not checked completion or release authority for backing.

Tests use the actual GPU forwarding body as well as actual ThreadManager code,
covering pre-start/post-stop refusal, both results, sync/async modes, exception
exit, external calls surviving worker exit, and entered GPU-owner work after
close. Four precise mutation controls restore the old facade bypass, release the
entry early, invent a true result on closure, or reject entered-owner work.
The old successful OnCPUWrite bypass described above is superseded by that mutation
control. A successful actual-body preemptive OnCPURead/GetFlushArea bypass remains
independent: not all rasterizer ingress is closed. See
[Windows validation](M4_F3_CPU_WRITE_TERMINAL_VALIDATION.md) for the test boundary.
None of the three M4 phases is completed by this terminal lifetime extension.

### Follow-up: terminal readback geometry queries and cancelled coverage

GPU::Impl::OnCPURead now asks ThreadManager::TryGetFlushArea for an optional
geometry result. Query uses the same terminal entry counter, retaining the
rasterizer through its synchronous result/exception. The value returned to the
facade owns no Entry: a non-preemptive read must release this query guard before
queuing or waiting for its separate flush. Worker join and sync-request shutdown
still own flush execution and cancellation. Already executing GPU-owner queries
may finish inline after external closure. Healthy geometry and the preemptive
flag are preserved; closed query returns no area before touching the rasterizer.
Facade lifetime remains externally owned, including work before/after the query;
joining this callback alone does not join the entire OnCPURead caller.

The facade returns a zero-initialized area on closed query or unsuccessful
WaitForSyncOperation, rather than publishing a reusable requested interval for a
cancelled flush. Memory::HandleRasterizerDownload caches only the returned
geometry; an empty result cannot cover a later nonempty access. This is neither
CPU-read cancellation nor checked device completion: the underlying load and
other producers remain independently owned. Existing cached geometry is not
revoked by this change.

Actual-body fixtures cover sync/async query ownership, preserved geometry and
preemptive results, exceptions, external callbacks surviving worker exit, entered
GPU-owner queries after close, and closed facade calls. Four query mutations
restore the facade bypass, release its entry early, invent closed coverage, or
reject an entered owner. A separate actual sync-request mutation ignores failed
flush completion and must fail the cancelled-read coverage assertion. Normal
readback still waits for completed flush execution.

The previous successful OnCPURead bypass is superseded by those rejection
controls. A successful actual GPU::Impl::FlushCommands bypass against a live
fixture rasterizer remains after Stop. Direct services/raw pointers, other
rasterizer routes, suspended work and device submissions remain independent.
No reversible owner stop, backing/alias freeze, or durable Unknown retirement
authority is created. All THREE M4 phases remain unfinished.

### Follow-up: terminal command tail and whole dirty gather

The GPU facade's FlushCommands, InvalidateGPUCache and OnCommandListEnd now forward
through ThreadManager under the same terminal counter. External entry is retained
through the complete callback or exception. An already executing GPU worker may
finish its command tail after external closure under worker-join ownership.
FlushCommands still calls the backend directly, and OnCommandListEnd preserves
ReleaseFences(false) followed by UpdateGPUAccuracy. No queue fence or new wait is
added to these paths. Backend thread-affinity and serialization requirements are
unchanged: an ingress counter does not serialize concurrent backend users.

InvalidateGPUCache closes admission before System::GatherGPUDirtyMemory consumes
dirty metadata. The entry covers the entire gather, including every owner and
synchronous callback, rather than entering separately within each callback.
Otherwise closed callbacks could refuse after the gather has already consumed
dirty records. An admitted gather may finish later owners after external Close.
The existing Stop worker-join/capture-retirement/external-join order remains.

Tests execute the actual facade/ThreadManager methods, System gather loop and
GPUDirtyMemoryManager Collect/Gather against two fixture owners. They cover both
GPU modes, pre-start/post-stop effects and metadata preservation, active callback
join, remaining-owner callbacks after Close, exceptions and entered GPU-worker
tail completion. Six precise controls restore each of the three facade bypasses,
release the command entry early, reject entered-worker tails, or guard only dirty
callbacks after metadata consumption. Existing cache/readback controls remain.

The former successful FlushCommands bypass is now a rejection control. Actual
dirty Collect still succeeds after Stop and is retained as a positive scope
counterexample: CPU producers are not closed by this terminal consumer guard.
Collection/gather concurrency assumptions and partial dirty-callback exception
semantics are unchanged; no durable Unknown owner or rollback is added. Backend
FlushCommands may merely submit native work. This is neither device completion
nor reversible whole-guest exclusion. All THREE M4 phases remain unfinished.

### Follow-up: retained terminal address-space owner publication

InitAddressSpace publishes a raw rasterizer alias into a new GPU MemoryManager.
Protecting just BindRasterizer would leave a gap before the NV service publishes
its GMMU and initialized VM. GPU therefore has a separate cold terminal counter:
open during GPU construction (initialization may precede Start), closed by
NotifyShutdown without waiting, and joined after GPU worker stop and before
renderer shutdown. This counter currently covers address-space publication only.

| Transition | Disposition |
| --- | --- |
| Candidate construction fails | Local staged VM/allocators/GMMU retire; service owner state remains unchanged |
| InitAddressSpace entry is closed | No rasterizer binding; NV InvalidState; unpublished candidates retire |
| Entry precedes Close | Binding and caller's final publication may finish while teardown waits |
| InitAddressSpace returns | Optional Entry transfers to caller; returning from the bind is not release authority |
| NV commit | Entry remains held through nothrow VM/GMMU moves and initialized state publication |
| Renderer teardown | Join must observe final Entry release before raw rasterizer ownership is retired |

The actual NV AllocAsEx implementation now builds pending VM/allocator/GMMU
owners locally. Refusal cannot leave an initialized service or replace its GMMU.
The optional Entry is held across the final commit; VM move assignment is checked
nothrow. No snapshot/IsCurrent result is converted into asynchronous authority.

Windows tests execute actual GPU destructor/NotifyShutdown/publication methods,
public forwarding, MemoryManager binding assignment, NV VM layout and AllocAsEx
against fixture allocators/GMMU/renderer/worker. They pause inside the bind and
after it returns but before service commit, verifying teardown waits in both
cases. Five precise controls detect omitted Close/Join, early caller release,
ignored refusal and VM state mutated before admission. Local WSL syntax checks
also cover the complete production GPU and nvhost_as_gpu translation units.

Candidate allocation before Entry acquisition still relies on external System
lifetime. Entries/facades require the existing external lifetime/serialization
contract; this counter does not serialize NV clients, close channel creation or
initialization, revoke existing mappings/raw aliases, retain renderer ownership
for later GMMU use, drain native writers, or provide durable Unknown retirement.
It is terminal and not attached to ProcessCohortStop. All THREE M4 phases remain
unfinished and ordinary GuestBackingAdmission stays Unknown.

### Follow-up: terminal channel initialization and NV acknowledgement

InitChannel now returns an owned publication Entry from the same cold GPU domain.
Both actual NV SetupGPFIFOChannel and SubmitGPFIFOImpl retain it through their
final metadata/fence/queue acknowledgement. Submit obtains an entry even for an
already initialized channel. Setup now shares Submit's channel mutex. Closed
entry refuses before Setup's program-id/fence output writes or Submit's fence
increment/queue calls. An already initialized duplicate Setup still reports
AlreadyAllocated without effects.

| Channel transition | Disposition |
| --- | --- |
| Unbound deferred Setup | Entry retained while recording program id/fence; no engine materialization |
| Unbound Submit | Refuse; no queue or fence-increment effects |
| Bound cold initialization | Mark potential partial effects before engine construction; ready remains false |
| Engine/binding/backend initialization throws | Keep initialization_failed true and ready false; preserve parent ownership and reject retry |
| Complete backend initialization | Publish initialized only now; clear failure marker; return Entry to caller |
| Ready-channel Submit | Reacquire publication Entry without reinitializing engines |
| Close before entry | No new initialization/metadata/submission effects on these two NV routes |
| Close after entry | Existing initialization and caller acknowledgement may finish; teardown joins them |

ChannelState::Init no longer publishes initialized before alias/backend-cache
initialization. The first exception still propagates as before; subsequent Setup/
Submit requests reject a partially effected owner instead of rebuilding engines
or replaying cache publication. This marker only isolates retries; it is not a
native resource quarantine/retirement certificate.

Windows actual GPU/NV method tests cover deferred setup, required address-space
binding, cold materialization, ready-channel reuse, closed no-effects results,
engine/backend partial failure and retained teardown at engine/backend/Setup/
Submit stages. Six precise controls detect premature ready, omitted failure
isolation, ignored Setup/Submit refusal, or either caller releasing Entry early.
Complete production GPU, nvhost_gpu and ChannelState units pass local WSL syntax.

The subsequent channel-creation change below closes that owner-creation witness.
Address-space bind
to channel, other configuration APIs, pre-SubmitGPFIFOImpl guest-buffer reads,
existing alias lifetime and native writers remain outside this closure. An
admitted Submit racing later queue shutdown may still see void PushGPUEntries
cancelled; its service acknowledgement is not queue acceptance or checked device
completion. All THREE M4 phases remain unfinished; ordinary admission stays
Unknown. No reversible ProcessCohortStop or durable GPU Unknown registry is added.

### Follow-up: terminal channel creation through fd/session publication

AllocateChannel returns an optional channel plus owned publication Entry from
the same cold GPU domain. NV's GPU-device builder retains it through device
construction and fd-map insertion; Module::Open retains it through OnOpen's
session acknowledgement. Refusal returns INVALID_NVDRV_FD before channel ID,
event or syncpoint allocation. Module may consume an unused fd number.

| Transition | Disposition |
| --- | --- |
| Closed allocation | No channel, scheduler or fd/device publication |
| Entered creation races Close | May complete; GPU teardown joins the retained Entry |
| Channel constructor throws | Entry released; no inventory record published |
| Scheduler declaration throws | GPU map record erased; exception propagates |
| NV construction/fd/session allocation throws | Entry released on unwind; completed channel registration remains parent-owned until teardown; no claim of a full Open transaction rollback |
| Module retirement | Own fd map cleared while event/syncpoint providers remain alive; escaped device refs still need external lifetime coordination |

Channel creation serializes IDs and inventory registration with a dedicated
mutex. BindChannel copies the shared owner under a short map lock before backend
binding. Allocation releases that lock before Scheduler::DeclareChannel because
Scheduler::Push holds its own lock when calling BindChannel. No Module-wide
concurrent-client safety is inferred from the GPU creation lock.

Windows actual-body tests cover three positive repetitions, constructor and
declaration failures, 128 concurrent unique IDs, scheduler/map lock ordering, and
five retained teardown pauses (channel construction, scheduler declaration, NV
event construction, fd publication, session commit). Six new precise controls
exercise wrong-domain entry, rollback omission, lock inversion, factory/Open early
release and provider retirement order. Four full production units pass local WSL
syntax. See [validation](M4_F3_CHANNEL_CREATION_VALIDATION.md).

The creation-only baseline retained an existing-fd address-space binding bypass.
The following extension addresses that NV entry family. No native GPU completion,
reversible ProcessCohortStop coupling or durable Unknown registry follows from it.

### Follow-up: terminal NV channel/address-space binding

AcquireChannelBinding returns an Entry from the same cold GPU publication domain.
The real NV BindChannel holds it before fd lookup, through both service-lock waits,
shared-owner replacement and method acknowledgement. Closed entry refuses without
publishing the GMMU owner. The address-space lock serializes the snapshot with
AllocAsEx; the existing channel lock serializes replacement with Setup/Submit.
The previous owner retires after both locks release, while Entry still protects
it against renderer teardown; mapping retirement may acquire its own mutex.

Windows actual-body positives passed three repeats and all 24 precise controls,
including seven binding-specific ones. They cover closed refusal, lookup/return/
old-owner-release teardown pauses, cold/null behavior and both lock waits through
Close. An independent thread must acquire both service locks during old-owner
release. ThreadManager regressions also passed three repeats and their terminal/
blocking-handoff controls. Full production Windows syntax remains blocked by a
missing Boost dependency; no current-source Linux/device validation is inferred.
See [the Windows validation boundary](M4_F3_CHANNEL_BINDING_VALIDATION.md).

Externally retained GMMU BindRasterizer remains a successful post-Close actual-body
scope counterexample. Initialized engines still hold old-manager references. The
follow-up guard rejects different/null GMMU replacement under the shared AS and
channel locks once initialized or initialization_failed is set. Idempotent binding requires equal addresses and shared control-block identity;
refused aliases cannot replace the true owner. Failure isolation and deferred/cold
binding behavior are preserved.
Actual engine-construction and Setup/Bind bodies pass controlled ownership and
serialized initialization tests; native engine rebind/retirement remains unproved.
See [the engine-owner validation](M4_F3_CHANNEL_GMMU_OWNERSHIP.md).
Complete facade/Module/service lifetime, output copying, mapping/alias producers,
native completion and durable Unknown retirement remain separate. This terminal
counter is not wired into reversible ProcessCohortStop and grants no guest backing
authority. All THREE M4 phases remain unfinished; ordinary admission stays Unknown.

## Evidence and limits

The actual-body Linux fixture covers Run and Step, a halt request followed by a
final simulated store, delayed UnlockThread, a still-active SVC writer after
backend acknowledgement, process reference retention, closed re-entry,
unsupported backend, contended core, exhausted budget, moved-from handle,
backing replacement and backend exception cleanup. It compiles the production
ProcessExecutionStop type and extracted factory/core/stop methods, with injected
backend and kernel collaborators. Three repeats and two assertion-specific mutants
are required. The SVC writer is a mandatory successful bypass counterexample:
GuestBackingAdmission remains Unknown even after BackendsStopped.

Native run `39427948982` compiled all production targets, but the original
unconditional B -8 store-loop failed to acknowledge and timed out during cleanup.
The pinned frontend uses LinkBlockFast here; its linked terminal has no halt check.
The operation correctly becomes Unknown instead of certifying backend exit, but
ordinary arbitrary-program stop capability is not established.

The original loop now remains an explicit native refusal witness in a separate
process: continued writes, sticky Unknown, refused cancellation/new execution,
and retained owners until OS termination. A checked TBNZ loop separately tests
successful actual Dynarmic/KProcess/KThread/PhysicalCore exit and stable backing.
The counter stays odd so that loop cannot exit naturally. No global optimization
change or forced in-process cleanup disguises the original counterexample.
Both native results passed in run 39432258674 at 51266b64ef, with all seven
CTest logs and exact-source artifact hashes independently recovered. That run
later failed a stale extraction fixture, so it is not full-workflow acceptance.
Replacement run 39518766603 at 89f034d20b passed build/CTest, repeated native
witnesses and HostMemory checks, then failed the shared resolver fixture's missing
ReserveRange adapter. Its exact-source artifact (12310269482) is independently
hash-verified, including all seven CTests and the stop positives/mutants. The
resolver and assertion-stub fixture corrections still require a full Linux run;
the cancelled interim run 39527273477 supplies no additional acceptance.
The corrected baseline at `9fdec2afa1` subsequently passed the complete Linux
workflow 39528346917. Artifact 12316026001 is independently verified: 170 source
hash entries in 15 manifests and all seven CTest markers, with the pinned
Dynarmic gitlink unchanged. This accepts the existing prerequisite and negative
controls only; subsequent running-owner changes require a new run.
Neither result closes services/GPU or proves a complete alias census; the harness drives the real backend on a dedicated
host thread, not as an ARM application dispatched by the scheduler.

## Running-owner retention follow-up

The stop requester is not necessarily the process executing on any PhysicalCore.
After all execution entries close, each core now captures its actual running
KProcess under the same try-locked context that issues the halt request. The
stop handle retains those strong references and their PageTable identities,
including when a backend refuses the nonblocking request. A late exit does not
discard them. Moving the handle transfers the references; the execution closures
are sealed before references are destroyed. An occupied output slot refuses
before interrupt effects so no unrelated last reference is dropped under m_guard.

| Observation | Retention and disposition |
| --- | --- |
| Core lock contended | No owner snapshot or halt; retry within the existing budget |
| Core idle | No running owner to retain; still wait for the closed execution count |
| Running owner captured | Open its real reference before invoking backend halt; retain requester separately |
| Unsupported or uncertain backend | Keep captured owners for the handle lifetime and leave closure Unknown |
| Backend and UnlockThread exit | Validate retained PageTable identities; still no guest admission |
| Known cancellation | Reopen only owned execution closures; retain references until handle destruction |

The actual-body fixture covers two different running processes plus a third
requester, pre-halt reference capture, delayed UnlockThread, move/release exactly
once, occupied-slot refusal and foreign-owner retention after unsupported stop
and late exit. A third assertion mutant removes the capture and must fail at
the pre-halt retention assertion. A new isolated native CTest requests from A
while real Dynarmic executes a checked store loop in B, then checks retained
ownership through exit and exact reference release. Linux run 39533283269 at
`d98272e36a` passed the complete workflow. Artifact 12317661714 was independently
verified against that exact source: 170 source hash entries across 15 manifests,
98 unique files and all eight CTests. The archive digest is
`9d5cfe1feb2ccd7f3b3d5119d68474d67c29cc519a21aca5eb14cab082eea65b`.
This accepts the running-owner prerequisite, not later storage/initialization work.

This is the running-backend cohort, not a complete process/backing owner roster.
KernelCore::GetProcessList is populated by the OS process wrapper and is not an
exhaustive allocation/publication census. Constructor/initialization, registration,
all Memory views, teardown and independent service ownership must be closed and
joined before a full cohort can be certified. No process-list mutex is held across
halt/drain: those locks can be needed by lifecycle work. A separate backing
storage owner now retains Impl; it does not retain native GPU resources or freeze
aliases, and the handle cannot transfer to async GPU work. Callers retain
System/Kernel/HostMemory and an uncertain handle; a future durable Unknown registry
is still required. Original unchecked-loop/NCE/service/raw-alias/GPU blockers remain.

## Actual backing storage retention follow-up

RetainedBackingStorage is privately constructed by HostMemory from its real Impl
and epoch. Unlike an epoch reference it keeps OS allocation/reservation resources
alive; unlike PinnedPhysicalMemory it exposes no byte range, pointer or FD. Capture
is a cold owner operation, not a signal/ordinary-write path, and never materializes
lazy chunks, enrolls ordinary mappings or enters a mapping mutation. A moved-from
HostMemory refuses capture before touching transferred state. Moving the token
leaves its old OwnerAlive false and Size zero.

KProcess acquires this owner before opening process references or closing cores.
A capture failure has no core-closure effects. ProcessExecutionStop holds the
storage before its process members in declaration order: execution closures seal,
captured processes release/finalize, then storage releases. Replacement or epoch
retirement makes Poll Unknown while the old allocation remains retained for the
handle lifetime. System/Kernel/facade protection and durable Unknown retirement
remain separate requirements; this cannot keep a destroyed GPU device usable.

The dedicated Linux suite links actual HostMemory and uses process_vm_readv to
probe lifetime without dereferencing a potentially freed address. It checks
facade/token moves, moved-from refusal, retirement, replacement isolation, final
unmap/FD release, lazy capture with zero chunk creation, a later materialized chunk,
and capture during a controlled mapping freeze without escape or reopening.
Three repeats pass. A fake-owner mutant retaining only an unrelated allocation
and the real epoch fails at the precise premature-storage-release witness.
The stop actual-body fixture separately checks failed capture before closure,
old allocation retention after replacement and storage surviving process Close.
Production/native syntax and the existing full HostMemory mapping-freeze controls
pass locally. The later combined source a09bf27bb4 passed Linux run 39540116589,
including this storage owner and initialization closure. Its independently
verified artifact 12320539901 contains 180 source hash entries, 103 unique files,
16 manifests and all eight CTests, with archive SHA256
`6267209c50becfca1166b8f15b3025fca0e7386157ed79f0bb61e45a3f3fb5a7`.

## Guest process initialization closure follow-up

KernelCore now owns a session-specific guest initialization domain, independent
of the OS wrapper process list. Both PageTable-building KProcess::Initialize
overloads enter before resource reservation or initial mapping. Their entry
outlives initial TLS creation, the initialized flag and all local rollback guards.
The public real-process common initializer also enters; the two outer initializers
call a private InitializeCommon under their existing entry, so a close cannot
cause a spurious nested refusal halfway through an admitted initialization.
Closed, contended or exhausted entry refuses with ResultBusy before effects.

ProcessExecutionStop closes this domain before closing core execution. Poll waits
for admitted initializers/rollback to exit before issuing backend halt requests.
Budget exhaustion seals both domains Unknown; a later initializer exit does not
reopen them. Known no-effects cancellation reopens initialization last. An occupied
initialization closure refuses the factory before any core closure. Kernel session
initialization creates a fresh domain; an old closure cannot reopen the new one.
No process-list, scheduler or PTE lock is held while polling.

The actual-body stop fixture tests a pending initializer with a running backend,
no early halt request, closed re-entry, successful release/cancellation, stale
cancellation, timeout followed by late exit and occupied-closure refusal without
core effects. A fourth assertion mutant bypasses initialization drainage and must
fail specifically at the early halt request. The native checked/unchecked stop
witnesses now call all three real-process Initialize overloads while closed and
require ResultBusy, no initialized publication and unchanged resource-limit
references. Checked witnesses also create and tear down a real mapped process
after known cancellation. Linux run 39540116589 at a09bf27bb4 passed the full
workflow; the exact-source artifact and eight CTest markers were independently verified.

The initial implementation closed PageTable/TLS construction only. Its later
cohort extension below also closes is_real=false initialization and cold setup.
Service callbacks, independent cache/GPU publication, publication of
an already initialized process, Run/ordinary mapping mutation, existing Memory
contexts, service callbacks and teardown remain independent obligations. In
particular, initialization drainage supplies neither an exhaustive retained owner
roster nor proof that all users of previously published backing have stopped.
All GuestBackingAdmission results remain Unknown.

## Lifecycle inventory prerequisite (implemented, not admission)

KernelCore now has a separate ProcessOwnerRoster. KProcess registers after its
constructor-owned Memory is ready and before any caller can initialize/publish
its initial PageTable. This includes kernel service processes and initialized
processes that never enter the OS wrapper list. Normal C++ destruction removes
the exact registration only after all PageTable/Memory/member destruction and
unregisters the object's kernel bookkeeping; slab
Destroy marks it retiring before inherited Finalize/Free. If Finalize throws,
the retiring record remains and future captures refuse without dereferencing it.
Completion uses a monotonic registration ID rather than an address, because
another process may reuse the slab slot before the old Destroy returns.

TryRetainProcessOwners captures actual KScopedAutoObject references using a new
checked TryOpen: a constructor with reference count zero or an owner whose final
Close already won cannot be adopted/resurrected. Snapshot capture/validation use
one mutex try-lock. Partial pins release after that lock, including when their
release invokes finalization. Snapshot moves transfer pins. Its revision becomes
stale on registration/retirement; stale validation does not release those pins.
Counter overflow poisons validation instead of permitting identity/revision reuse.
Cold reference opening/allocation is not a bounded stop or signal-handler API.

Kernel bookkeeping unregister has explicit ownership: a direct C++ destructor
unregisters only while its registration is still present. Slab Destroy consumes
that registration and leaves unregister to KAutoObject::Close. The production
slab Free does not invoke the C++ destructor, so the earlier implementation did
not double-unregister on that ordinary path; the guard clarifies responsibility
and prevents a consumed registration from taking it again in later C++ cleanup.
The lifecycle witness now compiles the actual inherited slab Destroy body, counts
unregister calls through actual Close/UnregisterWithKernel, checks direct C++
cleanup separately and rejects unconditional or omitted destructor unregister.
These two additional mutants bring the roster suite to six precise negatives.

The standalone Linux witness compiles the actual roster, scoped reference type,
KAutoObject Open/Close and extracted KProcess Destroy/destructor, with injected
slab/Finalize collaborators. Three repeats cover zero refs, real final-reference
retention, partial capture racing the caller's last Close, reentrant cleanup,
Finalize failure, lock contention, stale/moved snapshots and address reuse.
Four precise mutants omit pinning, ignore retiring owners, omit registration
revision and erase by reused address. Native checked/unchecked execution witnesses
also retain an actual unpublished KProcess/PageTable/Memory through termination
and final release, reject a zero-ref constructor, and reacquire the roster after
real finalization. Full native runtime acceptance for this component is pending.

The roster itself retains neither arbitrary Memory contexts nor GPU/device
resources. It does not freeze PageTables, close raw pointer loans, stop service
creation/callbacks or join a process whose failed initialization left effects.
ProcessExecutionStop does not yet adopt its snapshot: integrating a cohort needs
continuous initialization/publication closure and a defined response to new
service owners, plus ordinary writes and teardown exclusion. An IsCurrent check
alone cannot authorize an asynchronous operation; creation could follow it.
Keep the captured cohort owned through all future drains/Unknown isolation rather
than returning raw pointers or enumerating GetProcessList under a long-held lock.

## Sustained cohort and execution integration (phase one, in progress)

KProcess::TryStopProcessCohort now composes continuously owned capabilities:

1. Retain actual backing storage and the requester, refusing a requester without
   a real Guest PageTable. Close creation at the same mutex-protected point that
   registers every KProcess, including service processes. A constructor not yet
   registered must refuse and unwind; its kernel bookkeeping and popped slab slot
   are restored. Already registered zero-ref constructors are retried/refused,
   never omitted or resurrected.
2. Close all initialization, including is_real=false common service setup, and
   the shared cold publication domain. Wait for admitted entries to leave before
   closing CPU execution; an admitted loader must not be parked behind its own
   CPU-dependent completion. No scheduler/PTE/roster lock spans the drain.
3. Capture the real owner roster while creation remains closed. Keep every strong
   owner reference, then close actual PhysicalCore entry and issue actual backend
   halt requests. IsCurrent is an additional consistency check under those owned
   closures, not a substitute for keeping creation/publication closed.
4. Retain the closures and cohort through polling, move and uncertainty. Known
   cancellation reopens creation last. Unknown and late producer exit do not
   reopen acquired gates. Backing outlives closure destruction and all retained
   process finalization. This type has no asynchronous GPU adoption interface.

Every Memory constructor binds a shared kernel cold-publication domain, including
locally untracked Memory. BeginCold covers context bind, map, unmap, protect and
debug publication. Reset/move use a parent-only entry before their local join to
avoid acquiring a shared lifetime lock and then waiting on themselves. Local
controlled-arena enrollment remains separate. Run, metadata/module/interface
setup, the OS loader wrapper, process-list publication and application-process
publication also participate. Refusal precedes effects; the OS wrapper propagates
loading/running refusal rather than claiming success. Cold entry retries count
contention so normal concurrent mapping is not rejected spuriously. That retry
API is forbidden in a fault handler, backend entry or bounded stop poll.

Cache/fault-related RasterizerMarkRegionCached still uses its original independent
Begin path. It remains a required successful bypass control; this component
certifies neither all PTE publishers nor full alias/producer coverage. The NCE
fault path has no new wait or parking mechanism. Ordinary writes, raw pointers,
service callbacks, GPU resources and partial-initialization remnants remain
separate proof obligations. Manual destruction outside the intrusive owner
protocol is not authorized by a snapshot. BackendsStopped is a prerequisite only;
GuestBackingAdmission remains Unknown.

The Linux actual-body integration fixture retains idle/unpublished owners before
the halt callback, delays initialization and publication independently, proves
CPU entry is not closed during those drains, refuses creation and cold mapping,
and tests move, stale cancel, zero refs, timeout/late exit and a post-stop seal.
Four mutants move capture after halt, omit publication drainage, omit creation
closure or close an unrelated publication domain. The ninth native CTest uses
real KProcess/Memory/roster and a real checked Dynarmic store loop. It tests actual
constructor/slab rollback, mapping/reset/move/interface/module/OS publication
refusal and resumed mapped-process initialization after cancellation. Native
runtime acceptance for this integration is pending its exact-source Linux CI.

## Remaining roadmap: three phases

The [2026-10-23 P2 coverage update](M4_P2_PRODUCER_CLOSURE.md) adds a shared
non-NCE managed Memory write closure to the cohort. Prepared entries remain
bounded. NCE/service/raw/GPU-owner coverage is still incomplete; these phases
retain their original acceptance gates.

1. **Roster plus real execution drainage — in progress.** Finish the continuous
   creation/initialization/publication ownership boundary and scheduler/lifecycle
   protocol. The sustained cold-route integration above does not close independent
   cache/GPU PTE publishers or prove all arbitrary Memory views. Never promote a
   single IsCurrent observation to permission for subsequent asynchronous work.
2. **Close every independent producer — Open.** Ordinary scalar/bulk/atomic CPU
   writes; NCE native stores and unchecked Dynarmic fastmem; Audio/VIC/service
   callbacks; raw pointer loans, external mappings and aliases; queued/unsubmitted
   GPU transfers, shader/image writes and cache/DAddr/GPU mapping publication.
   Require real stop and close-and-join facts, preserve pause state, and avoid
   parking an owner while it holds a lock needed for completion. Unknown must
   keep backing, alias protection and native resources owned under an explicit
   durable isolation/retirement protocol; an old completion cannot release a new
   operation. No fine-grained page-fault parking is required for this first version.
3. **Real Guest-backed Admission — Open.** With real KProcess and ordinary Guest
   physical backing, drain all relevant producers, freeze mapping/permission
   publication and retain physical storage, PageTables and GPU owners. Execute one
   restricted real Vulkan Copy, wait for exact checked completion, then release
   safely or keep isolation/resources on Unknown. Cover success/refusal/partial
   failure/remap/teardown/retirement and preserve independent original F2/F3 negatives.

No normal guest route or physical latest-writer authority is enabled. F2/F3
remain Open. The current iteration uses focused Windows production-object and
deterministic regression checks; Linux/native-device acceptance remains a separate
exact-source obligation. Android is not requested.
