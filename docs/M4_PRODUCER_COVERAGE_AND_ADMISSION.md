# M4 owner coverage, scope freeze and admission gates

Date: 2026-10-25. Source audit starts at `c3fa0f0d4b`, with the bounded
[Presentation Pool repair](M4_P2_PRESENTATION_POOL.md). This is the consolidated
coverage ledger, not a certificate that all producers are closed. Existing
per-feature reports provide controlled evidence; the owner chain below defines
what must remain held together. GuestBackingAdmission stays Unknown in code.

The [2026-10-26 active NVDRV/cohort/GPU composition](M4_P2_NVDRV_COHORT_GPU_COMPOSITION.md)
now places actual Module/session/retirement, Host1x increment/callback and
ProcessCohortStop/ThreadManager in one live fixture graph. Eight schedules and
eight exact controls pass x5, with native collaborators explicitly bounded.
BackendsStopped with active Host1x/GPU work and a pre-resolved alias store remain
coverage-negative witnesses. This supersedes the previously pending named
combined-fixture milestone; native KProcess/System/service/mapping closure and
GPU completion/visibility are still open. No admission or production change.

The [nvhost_ctrl review and named owner composition](M4_P2_NVHOST_CTRL_OWNER_COMPOSITION.md)
adds repeated/concurrent cancellation, final facade capture release on Host1x
completion/cancellation, and dispatch closure during final owner destruction.
Sixteen schedules/one terminal case/nine exact controls pass x10. The active
callback with an otherwise drained dispatch domain remains an explicit
counterexample; GuestBackingAdmission stays Unknown. This is an active named
NVDRV/Host1x owner graph, separate from the earlier Audio/Memory/GPU fixture;
that separate test did not establish the combined graph now exercised above.

The [nvhost_ctrl lifetime repair](M4_P2_NVHOST_CTRL_CALLBACK_LIFETIME.md) now
tests the actual Module/session retirement -> raw Host1x callback chain with
controlled KEvent/kernel collaborators. Device destruction cancels/joins all its
handles before freeing events; Clear also joins an active Signalling callback.
Ten schedules pass x10 with four assertion controls and an injected terminal
join failure. This closes the named raw-target lifetime window only; native
KEvent runtime, escaped direct device owners and whole Host1x/P2 closure remain
unproven. Dispatch drainage alone still does not drain the Host1x action queue.

The [actual Audio service target drain](M4_P2_AUDIO_SERVICE_TARGET_DRAIN.md) now
reproduces and repairs last-owner release during raw AudioIn/Out callbacks.
The Audio server joins IPC work, retains factories/sessions, drains/joins Audio,
then releases service targets. Actual service unique impl ownership and raw binds
participate; candidate/fixed 13 normal/four terminal/five controls pass x3, with
nine real Windows production units. This is a terminal named-target closure,
not whole Audio/service/native-device or reversible guest closure.

The [joint Audio/Memory/GPU owner-chain test](M4_P2_OWNER_CLOSURE_COMPOSITION.md)
now combines nine schedules with six exact controls. Full AudioManager and
ThreadManager declarations and actual cohort/Memory translation bodies participate;
System/KProcess/backend/PTE/native collaborators remain fixtures. This upgrades
the prepared callback packet's composition evidence, not whole-family coverage.
Production AudioOut/In raw-target teardown now has the separate terminal owner
proof linked above. A live escaped-pointer store
after managed closure remains a mandatory negative witness, not admission success.
NVDRV/Host1x active device ownership, complete service graphs and real KProcess
runtime composition remain pending. No production scheduling code changes.

## 1. Presentation Pool checkpoint

Fixed-address Frame pool survives swapchain shrink/grow without resizing; named
acquire/retirement/surface/replay defects have deterministic witnesses and bounded
repairs. Scope now freezes: additional local work requires a reproduced critical
owner failure. The next milestone is owner closure composition, not counting more
destructor fixes. No main merge or F2 transaction integration is part of this slice.

## 2. Producer coverage from actual owners

`ManagedSubset` means only the named admitted bodies drain. `TerminalOnly` means
shutdown joins, not a reversible quiescence capability. `Unknown` is a missing
proof and blocks admission. None of the following rows alone grants permission.

| Family and actual owner | Close and real acknowledgement | Retention that must span use | Remaining untracked route / status |
| --- | --- | --- | --- |
| Process: KernelCore::Impl process_owners, KProcess, PhysicalCore; ProcessCohortStop owns retained roster and requester | creation/init/publication gates remain closed; ProcessExecutionStop polls actual backend stop, KernelCore::TryCloseGuestExecution holds core entry closures | KScopedAutoObject requester/roster, RetainedBackingStorage, System/Kernel facade and PageTable identity | BackendsStopped is a subset result; fastmem/NCE/host continuations and all independent services need joint proof. ManagedSubset / whole-guest Unknown |
| Memory: KProcess::m_memory and shared KernelCore::ManagedGuestWriteDomain | non-NCE ManagedGuestWrites entries span 21 real facade write bodies; cohort drains only after backend stop | Process + Memory/PageTable + backing, not just ProducerDrain state | GetPointer/GetSpan, direct PreparedCpuPage/physical pointers and stores after return escape the body; HAS_NCE explicitly unsupported. ManagedSubset / aliases Unknown |
| NVDRV: shared Nvidia::Module owns dispatch_domain, Container, open_files and OwnedRetirementQueue | common Open/Ioctl/Close/QueryEvent and Initialize count; ProcessCohortStop drains outer NVDRV before inner host1x/Memory | Module/session lifetimes, process backing, retained GraphicBuffer/session retirement records | GetContainer/direct device access, deferred operations and raw exports require their own owners through last use; a domain counter does not retain Module/System. ManagedSubset |
| Host1x syncpoint callbacks: Module-owned nvhost_ctrl plus System-owned SyncpointManager | guarded handle removal or join before KEvent release; actual retirement entry spans final Module destruction; closed retirement keeps the owner record | nvhost_ctrl internal events, KEvents, EventInterface/ServiceContext, Module/Container sessions and Process/backing through join | Raw Host1x callbacks are outside the NVDRV counter; active callback with DrainedSubset remains witnessed. Native scheduler/System lifetime and joint cohort/GPU composition are open. Named owner composition / whole set Unknown |
| Host1x: Module-owned nvhost_nvdec_common and System-owned Host1x/device graph | Submit/MapBuffer/UnmapBuffer enter KernelCore::Host1xSubmitDomain; cohort waits for admitted ioctl bodies | device/decoder/channel, mapping source/backing, any queued owner packet | direct PushEntries/device/GMMU access and native decoder work are not implied drained by ioctl exit. ManagedSubset / device completion Unknown |
| Audio: System AudioCore graph, AudioManager::callbacks and renderer::SystemManager::rendering | ManagedHostProducer pauses before cycle publication; cohort drains render then callback domains; terminal owners request_stop/join their jthreads | managers, sessions and callback buffers; caller retains System while paused | sink/backend callbacks, Opus, direct service and independent queued work outside the two cycles. ManagedSubset |
| GPU: GPU-owned GPUThread::ThreadManager and channel/scheduler graph | Stop closes direct_invalidations and accepting, joins GPU thread, cancels queued captures without inventing completion, then joins invalidations | GPU Impl/destructor keeps schedulers/caches/renderer until actual joins; acknowledged requests own captures | terminal order does not create reversible full GPU pause; queued shaders/transfers, suspended continuations and external producers need joint closure. TerminalOnly |
| Rasterizer: RendererVulkan-owned RasterizerVulkan, fence_manager, Scheduler and native source retainers | Shutdown drains fencing; renderer drains recording, joins presentation/scheduler, checks native idle, then retires checked holders | image/view owners plus source packet/backing, allocator and Device context until checked native completion | retained presentation image is not all shader/cache/GMMU writer exclusion; no full reversible rasterizer closure. TerminalOnly / whole set Unknown |
| Presentation: RendererVulkan owns PresentManager, Swapchain and surface | upstream stop + WaitWorker precede Shutdown/WaitPresent/join; checked device idle follows; borrowed Frame returns only after Copy | stable Frame pool, command pool, semaphores/fences, surface before child replacement, renderer native context | unbounded persistent WSI failure, external render-owner synchronization and borrowed getters remain unproven. Bounded pool evidence / TerminalOnly |
| HostMemory / backing: System::Impl::device_memory -> DeviceMemory::buffer | HostMappingDomain and layout_mutex cover named mutation entries; they do not revoke already returned pointers | backing storage/allocation pin plus a continuously held mapping-use/freeze capability and facade owner | MergeMappings remap/protection and external alias lifetimes remain HIGH RISK. File/offset equality and stamps are not use leases. Unknown |

Source anchors: `src/core/hle/kernel/process_cohort_stop.{h,cpp}`,
`process_execution_stop.*`, `process_owner_roster.*`, `kernel.*`, `k_process.*`;
`src/core/managed_guest_writes.h`, `src/core/memory.cpp`,
`src/core/managed_host_producer.h`; `src/core/hle/service/nvdrv/nvdrv.*` and
`devices/nvhost_nvdec_common.cpp`; `src/audio_core/audio_manager.*` and
`renderer/system_manager.*`; `src/video_core/gpu_thread.cpp`;
`src/video_core/renderer_vulkan/renderer_vulkan.cpp`, `vk_rasterizer.cpp`,
`vk_present_manager.cpp`, `vk_swapchain.cpp`; `src/common/host_memory.cpp`,
`src/core/device_memory.h`, `src/core/core.cpp`.

The actual ProcessCohortStop::Poll order is retained creation/init/publication,
roster capture, core entry closure + backend acknowledgement, NVDRV dispatch,
Audio render, Audio callbacks, Host1x ioctl, managed Memory. Outer operations may
need inner writers to finish, so closing an inner writer earlier can prevent
progress. All gates remain owned through the interval. CancelNoEffects batches
reopening only after confirmed drainage and before asynchronous GPU effects;
Quarantine keeps Unknown sticky. This type explicitly forbids GPU work from
adopting BackendsStopped. No new admission implementation changes that rule.

Next composition acceptance must enumerate concrete retained objects, active
operations, callbacks and aliases in one restricted session, then exercise a
paused operation in each family while closure is attempted. The negative control
must omit each required owner/closure and fail the joint oracle. Timeout, missing
entry, stale roster, owner loss, publication side effect or uncertain native wait
means Unknown; completed callbacks and successful enqueue are not equivalent.
This table exposes the remaining gaps rather than declaring global P2 complete.

## 3. Whole-guest quiescence first

Do not add per-access checks or waits in a signal handler to fill this ledger.
Keep process creation/publication closed, request and observe genuine backend
exit for the entire retained cohort, then drain supported outer host producers
before inner managed writes. Retain acknowledgements until the whole operation
ends. Windows tests of the non-NCE managed subset cannot certify native NCE stores.

HostMemory requires all remap/unmap/protection/publication routes frozen before
borrowing a pointer and throughout its final CPU/native use. Physical backing
pinning alone does not freeze virtual mappings. Escaped pointers require a real
last-use owner or a verified whole-owner join; independent services need equivalent
drain receipts. If any producer cannot actually be stopped, or its alias census
is incomplete, reject while keeping Unknown. The current repository does not yet
compose all of these conditions; controlled arenas do not enroll existing guests.

## 4. Restricted real KProcess-backed Vulkan Copy gate

The first integration target is one retained initialized KProcess, a small
explicit physical range, a fixed immutable source/destination plan and one Vulkan
Copy. No complex game, ordinary fast path or F2 mutation authority is required.
Before enabling it, require all of the following continuously, not a snapshot:

1. Complete retained process cohort with creation/publication frozen and actual
   backend acknowledgements; closed/drained concrete host/GPU producer owners.
2. Mapping freeze acquired before translation, covering every relevant alias and
   protection/remap route; physical backing pin plus owning System/Kernel/Device.
3. Immutable range/epoch plan, no unowned raw pointer; exact byte coverage and
   defined partial-side-effect receipt. No admission on owner/census uncertainty.
4. Native source/destination holders survive recording, internal Finish splits,
   submission and presentation if used; exact submission identity, checked wait,
   visibility and host readback verification establish completion.
5. Success verifies expected bytes and unchanged guards, then releases native use
   before mapping/backing/process capabilities in dependency order. Failure before
   effects may cancel only through checked no-effects reopening. Failure after
   possible effects retains the entire resource/context bundle under Unknown;
   it cannot recycle a Frame, replay a store or resume guests by destructor.

Integration tests must use actual KProcess/Memory/GetPointerImpl and the real
ThreadManager for deterministic closure interleavings; native Vulkan Copy evidence
must be labeled separately from fixtures and object compilation. Missing genuine
backend/service/mapping closure safely refuses the request. Current milestone:
bounded Presentation Pool complete; final P2 composition, phase 3 and this phase-4
acceptance are pending. F2/F3 remain open.
