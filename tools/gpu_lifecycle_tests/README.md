# GPU lifecycle correctness: F4/F5

Actual Audio service target closure runs
`../physical_coherency_tests/run_audio_service_owner_tests.py --repeat 3`.
Actual service/target declarations, unique impls, raw callback binds, factory
ownership and server/Audio joins cover 13 normal/four terminal paths and five
exact controls x3. Service work drains before Audio joins while target owners stay
alive. See [owner proof and limits](../../docs/M4_P2_AUDIO_SERVICE_TARGET_DRAIN.md).

Joint P2 owner composition runs
`../physical_coherency_tests/run_owner_closure_composition_tests.py --repeat 3`.
Full AudioManager and ThreadManager declarations join actual cohort/Memory address
translation bodies across nine schedules and six exact ownership/drain controls.
Kernel/process/backend/PTE/native collaborators remain explicit fixtures. See
[owner-chain evidence and limits](../../docs/M4_P2_OWNER_CLOSURE_COMPOSITION.md).

`run_presentation_pool_tests.py --repeat 3` exercises the fixed-address Frame pool
across swapchain shrink/grow, active presentation, fence reuse and shutdown.
Eight regular paths, seven terminal boundaries and five exact negative controls
use actual pool/worker/acquire/submit bodies with controlled native collaborators.
`--baseline` reproduces three historical failures; `--candidate` checks the patch
against that baseline before production edits. See
[bounded audit and limits](../../docs/M4_P2_PRESENTATION_POOL.md) and
[owner coverage / scope freeze](../../docs/M4_PRODUCER_COVERAGE_AND_ADMISSION.md).
Fixtures are not native Vulkan or whole-guest admission evidence.

Accelerated cache-native ownership runs `run_native_source_tests.py`; checked
presentation reuse runs `run_present_completion_tests.py`. See
[native handoff, split submissions and fallback/teardown limits](../../docs/M4_P2_ACCELERATED_NATIVE_SOURCE.md).

Retired session source re-admission runs `run_session_source_tests.py`: actual
session transitions, handle allocation and nvdisp publication with session
incarnations. See [shutdown/stale-session closure and limits](../../docs/M4_P2_SESSION_SOURCE_ADMISSION.md).

Delayed source mapping use runs `run_frame_mapping_tests.py`: bounded copy and
cache selection versus mapping intent, reuse and session retirement. See
[protocol, cancellation and native limits](../../docs/M4_P2_FRAME_MAPPING_USE.md).

Source-address/free/session interleavings use `run_frame_address_tests.py`.
See [snapshot synchronization and remaining mapping-use gap](../../docs/M4_P2_SOURCE_ADDRESS_TRANSLATION.md).

Per-frame source handoff uses `run_frame_source_tests.py`, with actual composer,
nvdisp and GPU request bodies linked to real guest syncpoints. See
[packet, partial failures and ownership limits](../../docs/M4_P2_FRAME_SOURCE_PACKET.md).

Deferred frame-owner prerequisites use `run_syncpoint_retirement_tests.py` for
actual syncpoint completion/cancellation capture release and destructor reentry.
See [scope, failures and the remaining frame packet](../../docs/M4_P2_SYNCPOINT_OWNER_RETIREMENT.md).

Vulkan resource recreation uses `run_blit_idle_tests.py` for final recording/
presentation publication and checked native idle, including exact failure
diagnostics. See [scope and remaining guest-buffer ownership](../../docs/M4_P2_BLIT_NATIVE_IDLE.md).

Warm slot/backing/cache ownership and escaped producer publication use
`../physical_coherency_tests/run_abandoned_slot_tests.py`. See
[local queue boundary and remaining native retirement](../../docs/M4_P2_ABANDONED_SLOT_PUBLICATION.md).

Warm VI Conductor/timing drainage and event borrow/unlink interleavings use
`../physical_coherency_tests/run_vi_conductor_drain_tests.py`. See
[CPU drain and remaining native slot boundary](../../docs/M4_P2_VI_CONDUCTOR_DRAIN.md).

VI stop/lazy-publication races and all eight nullable VI/AM caller bodies are
covered by `../physical_coherency_tests/run_vi_termination_tests.py`. See
[terminal-publication scope](../../docs/M4_P2_VI_TERMINAL_PUBLICATION.md).

VI create/destroy failure ownership uses
`../physical_coherency_tests/run_vi_session_owner_tests.py`: actual entry/getter,
pool-allocation and Close bodies, with prepared retention and precise partial-prefix
controls. See [protocol](../../docs/M4_P2_VI_SESSION_OWNERSHIP.md).

VI raw capture uses `../physical_coherency_tests/run_vi_capture_drain_tests.py` for
closed/active writes, publication, cancellation and partial effects. Its bounded
scope and remaining creation/retirement ownership are documented in
[VI owner audit](../../docs/M4_P2_VI_CAPTURE_CLOSURE.md).

The direct SurfaceFlinger/nvdisp synchronous entry has a separate actual-body
closure regression: `../physical_coherency_tests/run_surface_composition_drain_tests.py`.
See [scope and remaining deferred/raw producers](../../docs/M4_P2_SURFACE_COMPOSITION_CLOSURE.md).

Desktop Turbo native dispatch ownership: `run_turbo_dispatch_tests.py` extracts
the complete production Run/destructor bodies. It checks exact Vulkan results,
timeout retention, pending-work stop/idle disposal, and five mutation controls.
See [M4 P2 evidence](../../docs/M4_P2_TURBO_DISPATCH_LIFETIME.md).

Scope: explicit GPU emulation-thread teardown and ownership of CPU sync-request
arguments/results. This does not implement M3B admission, physical coherency,
Vulkan submission acknowledgements, device-loss recovery or producer leases.

## Deterministic regressions first

Before changing production, the new extracted-function tests were run against
`1ce96ad728` and both failed with exit code 1:

```
FAIL scheduler/renderer teardown before worker exit
FAIL shutdown leaves queued callbacks executable
```

Reproduce that negative control without editing production:

```sh
python tools/gpu_lifecycle_tests/run_tests.py --cxx g++ --revision 1ce96ad728 --case lifecycle --case cancel --repeat 1
```

The old destructor is actually executed against a latch-paused worker and
destructor sentinels. The test observes invalid lifetime order without dereferencing
freed objects. The old cancellation test checks executable queued work, stopping
before a dangling stack access is necessary. These are production-function
fixtures, not a historical game crash or a driver reproduction.

## Implementation contracts

- `GPU::Impl::~Impl` closes sync requests, explicitly calls `ThreadManager::Stop`,
  closes/drains detached presentation callbacks, then calls renderer Shutdown.
  The control scheduler, channels, renderer/context and sync metadata remain alive
  throughout the GPU thread's stop/join. No members were reordered as the fix.
- `Stop` is an owning-thread teardown operation. It closes command admission under
  the producer mutex, requests cancellation outside that mutex, wakes blocked
  producers, joins, then destroys queued captures. It is idempotent when invoked
  serially. `NotifyShutdown` remains callable on the GPU failure thread and never
  self-joins; it does not replace the owning frontend's eventual Stop.
- A full bounded queue uses a cancellable producer wait. Queue fences are allocated
  only at actual enqueue, so competing waiters cannot reorder fence assignment.
  Consumers notify space waiters under the same mutex used to check/wait; Stop
  cannot strand a producer in the old non-cancellable EmplaceWait. This adds a
  short producer-mutex notification scope per popped command; no performance claim
  is made and no Vulkan Finish/device wait is added to ordinary command submission.
- `IsGpuThread` uses a trivial thread-local manager pointer. It does not read a
  jthread object concurrently with join or mistake a recycled host thread ID for
  the old worker. Nested GPU-thread callbacks retain the inline, non-self-wait path.
- Sync-request insertion checks shutdown under the queue mutex. Closing seals
  admission and prevents pending requests from starting, then moves and destroys
  queued payloads one at a time outside locks. No temporary container is allocated
  on this failure path (even an empty list can allocate a sentinel on MSVC).
  Already-started callbacks may finish. Their bodies and owned arguments remain
  alive until thread join. Cancellation does not advance the executed-request fence.
- CPU read flush callbacks capture their immutable area by value. Applet captures
  use request-owned result storage. The caller reads it only after a successful
  completion wait, and returns empty after early cancellation. The completion load
  acquires TickWork's release publication even if shutdown woke the waiter before
  TickWork reacquired the queue mutex.
- Presentation requests own moved layers/fences and a per-request countdown.
  Registered guest-syncpoint actions retain detached shared state, never Impl or
  caller stack references. Closing that state clears its renderer pointer under
  its mutex and waits for a presentation already using it. Late actions become
  inert. A newly bound renderer gets a fresh state; this does not promise live
  renderer replacement while the GPU worker is still using the old renderer.

There is still an important semantic boundary: an early shutdown return is not
successful GPU execution/visibility. Public legacy void wait/callback APIs retain
their cancellation semantics. A future M3B adapter needs explicit operation outcomes
and resource completion; this patch does not promote queue acknowledgement to that
contract. Device loss and a permanently blocked driver wait remain separate work.

## Run on Windows or Linux

Only Python 3 and a GCC-compatible C++23 compiler are needed. There are no emulator
build, Vulkan SDK, Boost, submodule, display, device or Android requirements.

```sh
python tools/gpu_lifecycle_tests/run_tests.py --cxx g++ --repeat 10
python tools/gpu_lifecycle_tests/run_thread_tests.py --cxx g++ --repeat 10
python tools/gpu_lifecycle_tests/run_renderer_tests.py --cxx g++ --repeat 10
python tools/gpu_lifecycle_tests/run_fence_tests.py --cxx g++ --repeat 10
# On supported Linux compiler/runtime installations:
python tools/gpu_lifecycle_tests/run_tests.py --cxx g++ --sanitize address
python tools/gpu_lifecycle_tests/run_thread_tests.py --cxx g++ --sanitize address
python tools/gpu_lifecycle_tests/run_renderer_tests.py --cxx g++ --sanitize address
python tools/gpu_lifecycle_tests/run_fence_tests.py --cxx g++ --sanitize address
python tools/gpu_lifecycle_tests/run_tests.py --cxx g++ --sanitize thread
python tools/gpu_lifecycle_tests/run_thread_tests.py --cxx g++ --sanitize thread
python tools/gpu_lifecycle_tests/run_renderer_tests.py --cxx g++ --sanitize thread
python tools/gpu_lifecycle_tests/run_fence_tests.py --cxx g++ --sanitize thread
```

All ordering uses condition-variable events and futures. Five-second waits and
process timeouts detect hangs; no sleep or elapsed-time threshold decides the
required interleaving. Zero-duration future checks only assert that a latch-blocked
join/drain has not already returned.

### Coverage and evidence limits

`run_tests.py` extracts current Impl destructor, request enqueue/wait/tick/shutdown,
CPU read, capture and composite methods, plus detached composite state. Core/System,
worker, renderer/Vulkan and syncpoints are fixtures. Cases cover:

1. Active worker joins before scheduler/renderer teardown.
2. Queued work is cancelled, admission closes, executed fence remains unchanged.
3. Cancelled payload destruction can reenter enqueue without locking deadlock.
4. Capture is paused inside renderer; shutdown returns the caller; callback resumes
   only after that caller's stack is gone, using owned result storage.
5. CPU read request is cancelled before callback invocation.
6. Guest-syncpoint actions outlive Impl without accessing its renderer.
7. An already-active presentation delays close until its lifetime ends.
8. Normal capture/read, multi-fence and zero-fence presentation still work.
9. With shutdown absent, OnCPURead and RequestComposite remain blocked both after
   enqueue and while their actual callback is paused. The fixture takes the real
   request mutex after a wait-entry signal and observes a return counter before
   releasing the callback, proving enqueue/start are not callback completion.

`run_thread_tests.py` compiles the actual ThreadManager class, complete production
RunThread/ThreadManager bodies, bounded MPSC/SPSC queue and stop-token wait.
Platform, System, engine/control scheduler and renderer are shimmed. It saturates
the actual 4096-entry queue behind an active callback, stops a producer paused
inside the real queue-space wait, verifies active join and cancellation, pauses
a suspended-channel retry, checks sync/async modes and nested callbacks, and rejects
pre-start/post-stop requests. Two test-only signals are inserted in copied source
immediately before the queue wait and after request_stop; the production wait and
cancellation logic are unmodified. No production instrumentation is added.

`run_renderer_tests.py` executes extracted Scheduler WorkerThread/WaitWorker/
DispatchWork/Shutdown and PresentManager PresentThread/WaitPresent/Shutdown with
real queues, mutexes and latch-paused work. It exercises an active and a still-
recorded callback, active/queued frames, and fencing cancellation. It additionally
executes the actual RasterizerVulkan/RendererVulkan Shutdown and destructor bodies
against collaborator spies to check owner order and release of shutdown_mutex
before joining callbacks. Actual TurboMode destructor and MasterSemaphore Shutdown
are run against latch-paused host helpers; device-idle and resource cleanup are
spies, not real Vulkan calls. Each case passes ten repetitions.
The Scheduler destructor's fallback is tested separately: during unwinding it
cancels/joins without dispatching a local chunk whose owners may already be dead.
The explicit renderer-owned Shutdown is the successful drain boundary.

`run_fence_tests.py` compiles and instantiates the **complete** production
FenceManager template (both class declarations and method bodies), with only its
Core/platform/cache surfaces shimmed. It proves active callback join, unstarted
payload release, post-shutdown publication rejection and repeated Shutdown safety.
The copied header contains one signal immediately after request_stop to establish
the interleaving, without changing the shutdown/worker logic.

Generated sources, source hashes and per-case logs are under
`.diagnostics/gpu-lifecycle/{windows,linux}`. The baseline control has its own
subdirectory. Neither suite executes Vulkan, native NCE or a real full System
shutdown; no full executable link or Android validation is claimed.

## Validation in this change

Windows MinGW GCC 13.2.0: all four lifecycle suites pass ten repetitions per case; the
baseline negative controls fail as expected. Existing GPU stop, CPU-write and
unmap/scatter regressions pass. M3A passes three repeated protocol runs after
updating its queue fixture: stopped admission now belongs to PushCommand, which
the legacy fixture previously did not model. The dedicated ThreadManager suite
executes the actual new admission/stop code rather than that fixture.

Linux execution and sanitizers are **not** claimed: this Windows host has no
installed WSL or container runtime, and its MinGW lacks ASan. The same dependency-free
runners and Linux sanitizer commands are ready for a Linux/GitHub workflow host.
No Android local build was attempted.

## Follow-up boundary audit: complete renderer shutdown

The first F4/F5 patch alone did **not** close the backend worker lifetimes. A
follow-up test ran the old RendererVulkan destructor and failed:

```
FAIL renderer destroys resources or waits idle before host workers are joined
```

The old device WaitIdle preceded presentation-worker join, so presentation could
submit again after WaitIdle. The old fencing thread survived GPU Impl destructor
body and could access GPU fields destroyed before the renderer member. Scheduler's
late automatic join did not protect renderer/cache captures. The on_submit callback
was also overwritten without first excluding its worker. These gaps were repaired
before merging the lifecycle change.

The terminal normal-shutdown order is now explicit:

```
GPU Impl: close sync requests -> GPU emulation Stop/join (including direct invalidations)
  -> close CompositeState
  -> RendererVulkan::Shutdown
       -> RasterizerVulkan::Shutdown
            close render entries and release shutdown_mutex
            PipelineCache::DrainWorkers (compile, then serialize)
            Scheduler::Finish (device completion for existing render work)
            FenceManager::Shutdown (join active callback; cancel unstarted callbacks)
            final buffer/texture runtime Finish
       -> Scheduler::WaitWorker (including recorded Present enqueue callbacks)
       -> PresentManager::Shutdown (WaitPresent includes active frame; stop/join)
       -> Scheduler::Shutdown (drain and stop/join recording/submission worker)
       -> detach on_submit after its worker has stopped
       -> TurboMode destruction (join thread, desktop device idle, resource cleanup)
       -> renderer device WaitIdle after all host submission sources stop
       -> MasterSemaphore::Shutdown (join timeline-debug / fallback-fence helpers)
  -> later normal member destruction: imports, caches, frames, scheduler, device
```

Compilation tasks are drained while GPU::ShaderNotify still exists. Fencing is
joined while the derived fence manager, caches, GPU and Host1x are all alive,
avoiding base-destructor virtual calls from an active worker. Its plain-CV stop
predicate is changed under its guard, preventing a stop notification from being
lost between the worker's predicate check and sleep. Callback payload destruction
runs after join and outside that guard. Presentation is drained only after the
recording worker has published all queued enqueue closures; device idle occurs
after the final presentation submission. Completion helpers reference only their
own semaphore/fence state and the still-live device, and now join explicitly too.

All these are **terminal owner-thread** APIs: producers must already have stopped;
they are not concurrent live-renderer replacement or M3B admission APIs. A driver
wait which never returns or device-loss recovery is still outside this patch's
proof domain. No frame age is used as device completion evidence.

Normal synchronization is unchanged: non-preemptive OnCPURead returns after its
flush callback completes (that path already includes the staged download wait).
RequestComposite returns after its GPU callback finishes registering guest-fence
actions, or performs a zero-fence composite. With unresolved guest fences it has
**never** promised that presentation/hardware work is already complete; deferred
actions still present only when all required fences arrive. The new normal-wait
tests distinguish all three boundaries. Only shutdown permits early cancellation;
cancellation is not reported as successful execution internally.

ThreadManager Stop also closes external InvalidateRegion/FlushAndInvalidateRegion
before worker cancellation. It joins the worker, destroys unexecuted queue captures
outside write_lock, then joins entered external invalidations while the rasterizer
is still alive. Already executing GPU-owner calls are covered by the worker join
and remain able to finish. The entry guard never waits, retries a CAS, allocates,
or refuses ordinary reader-count contention; only the cold terminal owner waits.
Caller/facade lifetime remains externally owned. The actual-thread suite includes
five precise negative controls for this boundary. GPU::OnCPUWrite now forwards
through the same counter; four additional controls catch the old facade bypass,
early entry release, false dirty-collection success on closed entry, and refusal
of an entered GPU-owner callback. Its true/false return and exception propagate
unchanged while healthy. Closed entry returns false, which only skips collection
and is not CPU-store authorization or quiescence.

GPU::OnCPURead now queries optional geometry through ThreadManager::TryGetFlushArea
under that same terminal counter. Query entry ends before any queued readback
wait. Four query mutation controls catch the old facade bypass, early entry
release, closed-interval invention and entered-owner refusal. The facade returns
an empty area on closed query or cancelled flush, preventing new cached coverage
for unperformed work. The actual sync-request suite also rejects the old ignored
WaitForSyncOperation result while preserving normal completed readback behavior.
Existing cached geometry and underlying guest loads are outside this change.

FlushCommands, InvalidateGPUCache and OnCommandListEnd now also forward through
ThreadManager's terminal counter. InvalidateGPUCache enters before consuming any
dirty records and holds one entry over the entire gather, including later owners
after external close. Command end preserves ReleaseFences(false), then accuracy
update. Backend affinity/serialization requirements remain unchanged.
Six command-tail controls reject the three old facade bypasses, early entry
release, refusal of an entered owner, and callback-only dirty admission after
metadata consumption. Actual System gather and GPUDirtyMemoryManager Collect/
Gather run against two fixture owners. Actual dirty Collect remains successful
after Stop as an independent producer scope counterexample. Neither this counter
nor backend FlushCommands proves native completion or live M4 admission.

For isolated local validation, both run_thread_tests.py and run_tests.py accept
`--output-dir <temporary-directory>`. Their normal CI output paths are unchanged.
After reviewing results, remove temporary binaries/fixtures and retain the
Markdown validation conclusion when requested; the test sources reproduce them.

GPU address-space owner publication has a separate cold terminal counter: it may
start before GPU Start, closes with NotifyShutdown, and joins before renderer
teardown. InitAddressSpace returns an optional owned Entry, retained by AllocAsEx
through its final VM/GMMU commit. The NV caller stages both owners locally and
returns InvalidState on refusal without publishing candidate state. This does
not close channel address-space binding, existing mappings, or other raw aliases.

Run `python tools/gpu_lifecycle_tests/run_owner_publication_tests.py --repeat 3`
for actual GPU close/join/API/forwarding, MemoryManager binding assignment, VM
layout and AllocAsEx tests. The suite uses the lifecycle fixture, then covers
pre-Start success, shutdown refusal, candidate allocation failures, and teardown
while either binding or the caller's final commit is paused. Five controls reject
missing Close/Join, early caller Entry release, ignored refusal and unstaged VM.
Its optional `--output-dir` isolates local binaries for subsequent cleanup.
Allocator/GMMU construction, renderer, worker and complete System lifetime remain
fixtures; this terminal lease is not reversible Guest admission or native GPU
completion. Candidate construction before Entry acquisition remains outside it.

The same suite now covers channel initialization and both actual NV Setup/Submit
callers. InitChannel returns an Entry held through caller acknowledgement, even
for a ready-channel Submit. Deferred Setup is allowed; unbound Submit refuses.
Ready is published after backend initialization; partially effected owners keep
initialization_failed and cannot retry. Setup/Submit share the channel mutex.
Six channel controls reject early ready, missing retry isolation, either ignored
refusal, or either caller releasing protection early. Four teardown pauses cover
engine construction, backend initialization, Setup metadata and ready Submit
acknowledgement. Engines/backend/queue/syncpoints are fixtures. Guest-buffer reads
before SubmitGPFIFOImpl and actual queue acceptance/completion remain independent.

Channel allocation now returns its channel and publication Entry together. The
actual NV GPU factory and Module::Open retain it through construction, fd insertion
and OnOpen session acknowledgement. Closed allocation refuses before ID/resource
allocation. Scheduler registration failure removes the GPU map record; creation
serializes IDs while map locks are released before scheduler/backend calls.
Module's destructor releases its own device map before event providers retire;
escaped device references still require external lifetime coordination.

Creation tests cover concurrent IDs, allocation/registration failure, map/scheduler
lock order and five teardown pauses from construction through session commit.
Six controls reject an unrelated gate, missing rollback, inverted locks, early
factory/Open release and missing fd retirement. Real constructor/destructor,
factory/Open/session methods execute against fake event/syncpoint providers.
External NV channel/address-space binding now obtains the same owned Entry before
fd lookup, serializes the GMMU snapshot and Setup/Submit association, and retires
the previous owner outside the service locks before releasing the Entry. The
suite covers post-Close refusal, normal cold/null binding, lookup/acknowledgement/
retirement teardown pauses and both service-lock waits. Seven additional controls
require precise refusal/lifetime/lock/retirement failures. The retained independent
scope witness now calls actual BindRasterizer through an external GMMU after
Close. Safe live-engine rebind, raw/mapping/native producers, reversible cohort
stop and durable Unknown retirement are unproved. Guest admission stays Unknown.
See [the Windows report](../../docs/M4_F3_CHANNEL_BINDING_VALIDATION.md).

Materialized channel GMMU ownership is checked separately with
`python tools/gpu_lifecycle_tests/run_channel_rebind_tests.py --repeat 3`.
It executes real engine-construction/initialization and NV binding/Setup bodies
against fixture engines retaining manager references. Ready or partially initialized
channels reject different/null managers; idempotent binding requires equal object
address and shared control-block identity. Cold/deferred Setup/bind is preserved.
Nine controls cover refusal, ownership aliasing, failure
isolation and real shared-mutex serialization across initialization and terminal
Close. This is metadata/reference ownership evidence, not native engine execution
or real guest-backed admission. See [validation](../../docs/M4_F3_CHANNEL_GMMU_OWNERSHIP.md).

For focused real Windows production object compilation after these changes, use
`python tools/physical_coherency_tests/run_windows_production_checks.py --fetch-boost`
once to populate the SHA256-verified shared Boost cache, then omit `--fetch-boost`
for subsequent runs. No app linking/runtime/device acceptance is inferred.

External GMMU rasterizer binding now uses detached terminal state. The publication
runner executes actual public/private binding and renderer bootstrap bodies, with
before-entry, entered-call and Host1x enrollment teardown gates. Unregistered,
wrong/null target, closed and post-GPU calls refuse. An open same-target
confirmation never stores a pointer. Six further controls check Close, Join,
Entry lifetime, target/owner identity and renderer publication retention.
This supersedes the historical successful raw-binding bypass witness above.
See [current evidence and limits](../../docs/M4_F3_EXTERNAL_GMMU_BINDING.md).

Run `python tools/gpu_lifecycle_tests/run_address_space_mapping_tests.py --repeat 3`
for actual NV mapping lifetime across Close and GPU teardown. Five actual NV
methods, FreeMappingLocked and GMMU mapping/callback bodies run with allocator,
pin and table fixtures. Ten operation variants cover normal/closed results,
entry/acknowledgement retention, callback/pin pauses, exception release, Remap AS
serialization and a batch admitted before Close. Twelve precise controls reject
missing refusal, early release, missing Remap lock or missing GPU Join. This is
NV service lifetime evidence, not a certificate for direct GMMU calls, raw loans,
callback execution after cancellation or native completion. See the
[audit and validation report](../../docs/M4_F3_GMMU_MAPPING_LIFETIME.md).

Checked unmap is validated by `run_checked_unmap_tests.py --repeat 3` (real
ThreadManager/RunThread/queue and actual Vulkan unmap body) and
`run_unmap_ownership_tests.py --repeat 3` (actual GMMU/NV/NvMap bodies with controlled
resource bookkeeping). They distinguish completion, queued cancellation, active
shutdown, worker/inline failure, partial physical/batch prefixes, low-area owner
retention, failed last-pin allocation, cleanup failure and quarantine races.
Five worker and nine ownership mutation controls require named runtime failures.
The mapping/channel suites also cover failure quarantine. Fixtures do not prove
native Vulkan completion, guest backing authority or independent SMMU lifetimes.
See [the checked-unmap report](../../docs/M4_F3_CHECKED_UNMAP.md).

The Windows object compiler now defaults to nine units, including the Vulkan
rasterizer, GPU thread and NvMap. Initialize the repository's pinned Vulkan-Headers,
VulkanMemoryAllocator, Vulkan-Utility-Libraries, SPIRV-Headers, sirit and
unordered_dense submodules before this check. Existing Boost/fmt requirements remain.

Run `run_dispatch_exception_tests.py --repeat 3` to check exceptional dispatch
return against the real acknowledged-callback helper. Retained Pending closures
must never start later; Running actions must finish before Failed is returned.
The fixture also checks inline failure, exception after completion and unchanged
normal outcomes, with three precise controls. This is host callback lifetime
and result evidence, not a native completion or guest-admission certificate.
See [the morning checkpoint](../../docs/M4_F3_DISPATCH_EXCEPTION.md).

Capture/readback and local helper uploads are checked by
`python tools/gpu_lifecycle_tests/run_capture_borrow_tests.py --repeat 3`.
`--baseline` and `--candidate` use pinned pre-fix production source plus current
fixtures/candidate transformations. The suite extracts RenderToBuffer,
GetAppletCaptureBuffer, UploadImage, Finish and the checked native-borrow adapter.
It distinguishes allocation/pre-borrow failures, queued and active host borrows,
partial native effects, checked completion and post-completion invalidation errors.
Native driver work and Scheduler collaborators are fixtures; a semaphore-gated
worker models an entered callback before its delayed read. See
[scope, audit and Windows results](../../docs/M4_P2_CAPTURE_NATIVE_BORROW.md).

`run_aa_retirement_tests.py --repeat 3` extracts actual Layer refresh/AA replacement,
clear/wait helpers, ConfigureDraw through AA Draw and its scope-exit tick update,
plus Scheduler Finish/native-borrow methods. Pinned `--baseline` and `--candidate`
modes retain the old late-final-use witness and the pre-production candidate.
Three slots, internal Finish, later final composite, paused native/host boundaries,
partial failed waits, checked failures and constructor retry are controlled.
Ten exact controls must fail at their named runtime assertions. Native helpers,
recording/native progress and constructor allocation are fixtures. See
[scope and Windows validation](../../docs/M4_P2_AA_HELPER_RETIREMENT.md).

`run_applet_frame_tests.py --repeat 3` extracts actual applet rendering/readback,
Frame declaration and both BlitScreen framebuffer-creation overloads. Pinned
`--baseline`/`--candidate` modes preserve construction/publication/retry witnesses.
Image/view/framebuffer and pass failures leave member state empty; factory gates
inspect publication before commit, failed prefixes release children first, and
retry/reuse/readback follow the original serialized owner route. Resource/driver,
Draw/recording/native progress and bytes are fixtures. See
[scope and Windows results](../../docs/M4_P2_APPLET_FRAME_PUBLICATION.md).

`run_applet_content_tests.py --repeat 3` distinguishes applet Empty/Recording/
Recorded/Failed/Closed state. It extracts the actual renderer draw/readback/shutdown,
Scheduler completion and terminal retirement/release methods, and Layer destructor.
Partial recording, submitted/internal-Finish prefixes, failed retries, previous
good content, native readback and shutdown gates are controlled. Failed applet
work stays quarantined; terminal release requires the existing joined checked-idle
boundary and never flushes a cached current tick after worker join. Native jobs,
bytes and driver/worker collaborators are fixtures. See
[protocol and Windows validation](../../docs/M4_P2_APPLET_CONTENT_READBACK.md).

`run_raw_resources_tests.py --repeat 3` extracts Layer raw staging/image/view
creation, complete candidate publication, size/offset and wait/release/destructor
methods. Pinned `--baseline` and `--candidate` modes preserve failed-publication,
retry and stride witnesses. Cold/warm allocation faults, each of three slots,
semaphore factory gates, checked retirement and shutdown are controlled. Native
allocation, pending users and mapped bytes are fixtures; real Windows compilation
checks the actual no-throw commit. See
[scope and results](../../docs/M4_P2_RAW_RESOURCE_PUBLICATION.md).


`run_raw_upload_tests.py --repeat 3` extracts the full UpdateRawImage callback,
ConfigureDraw through AA Draw, raw recording state/bundle declarations and actual
refresh/wait/release methods. Historical `--baseline` witnesses and a saved
`--candidate` transformation precede production changes. Each of three slots
covers first and warm upload enqueue/partial submission faults, pre-record retry,
failed-slot quarantine, independent-slot progress, checked generation replacement,
internal Finish and semaphore recording/worker/shutdown gates. Recorded does not
mean native completion. Eight named controls distinguish seven production mutations
from one queue fixture control. Source copy, unswizzle, native queues,
pixels and candidate allocation are controlled collaborators; applet quarantine
and actual renderer shutdown are separate regressions. See
[scope and Windows results](../../docs/M4_P2_RAW_UPLOAD_RETRY.md).


`run_post_upload_tests.py --repeat 3` extracts complete Layer ConfigureDraw/raw
upload/descriptor/retirement bodies and complete FXAA/FSR/FSR2 Draw/UploadImages.
It preserves `--baseline` and saved `--candidate` modes. Shared initialization,
internal Finish, partial enqueue/submission effects, all slots, sticky Layer
quarantine, output publication and active-worker shutdown use deterministic gates.
The actual applet Draw handler and readback refusal prefix are connected to the
actual Layer exception, then tested with a fresh healthy Layer. Ten named controls
reject weakened ownership, admission, output and applet isolation. Native wrappers,
queues, pixels, math, source copy and helper descriptor allocations are fixtures.
Descriptor API update itself is noexcept void; before/after host injections never
claim a recoverable native failure receipt. See
[scope and Windows results](../../docs/M4_P2_POST_UPLOAD_HELPER_QUARANTINE.md).


`run_composition_tests.py --repeat 3` extracts complete WindowAdaptPass Draw,
BlitScreen DrawToFrame, Frame/PresentPushConstants declarations, the actual
FinishNativeBorrow adapter, Composite's draw/flush/present guard and applet
failure/refusal bodies. Historical `--baseline` and saved `--candidate` modes
retain the low-level borrowed-Frame witness. Host enqueue errors retire the checked
prefix before unwinding; callback/uncertain retirement and dequeued presentation
errors are terminal. Original applet Failed isolation remains effective even after
composition retirement succeeds. Three slots, internal Finish splits, command and
join gates, partial native effects and eleven exact controls are deterministic.
Resources, source ConfigureDraw, native queues/progress and preparation/presentation
collaborators are controlled fixtures. See
[scope and Windows results](../../docs/M4_P2_COMPOSITION_FRAME_RETIREMENT.md).


`run_frame_recreation_tests.py --repeat 3` extracts actual PresentManager
RecreateFrame, the Frame declaration, complete Blit pass factory/DrawToFrame/both
CreateFramebuffer overloads and checked WaitIdle, plus actual applet failure/
refusal bodies. Pinned `--baseline` and `--candidate` retain publication, parent
order and shrink witnesses. Complete tuples and cached metadata publish only
after candidate creation; sync object identities remain unchanged. Six shrink
combinations, three pooled Frame formats and reverse reuse, zero count, native/
host allocation faults, semaphore observation, retries, shutdown and eleven exact
controls are covered. Native factories/use graphs and Layer/Window Draw are
collaborators; no native Vulkan/game/content-readiness acceptance. See
[scope and Windows results](../../docs/M4_P2_FRAME_RECREATION_PUBLICATION.md).
