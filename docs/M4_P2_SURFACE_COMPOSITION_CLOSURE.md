# M4 P2: direct SurfaceFlinger composition and remaining raw producers

2026-10-23; baseline `b791777696d02bb88d7112b787a0315466edc08f`.
The fork branch/PR375 head agreed before this work. Main is unchanged at
`b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`. This is one synchronous producer
closure slice, not completion of M4 P2 or a raw-pointer revocation protocol.

## Actual gap and counterexample first

The live route is Conductor::ProcessVsync -> VI::Container::ComposeOnDisplay ->
SurfaceFlinger::ComposeDisplay -> HardwareComposer::ComposeLocked -> nvdisp::Composite.
It directly borrows the display device with Module::GetDevice, bypassing the six
counted Module dispatch methods. The body can acquire/release framebuffers,
resolve nvmap handle addresses, enter RequestComposite and update frame metadata
after the Kernel's NVDRV domain has reported DrainedSubset. A shared_ptr to the
device neither enters that domain nor carries permission to perform those effects.

Before changing production, the new actual-body fixture failed deterministically
in two cases: calling after closure still reached the composer; and pausing inside
the device lookup after entry let close report DrainedSubset. Both required exact
exit 91 and their defect marker, rather than accepting a timeout or crash.
The historical revision can be reproduced without changing the checkout.

## Bounded production repair

ComposeDisplay retains its Module and enters its existing dispatch domain before
display/device borrowing. A closed call returns false before lookups, buffer
consumption or output changes. It retains its entry through ComposeLocked's return,
the output assignment, and local device release. An already admitted call can
finish its synchronous body while close polls Pending; there is no wait while
closed and no newly added service/GPU lock or scheduler behavior.

Missing display, empty stack and missing device are no-effect false returns and
do not seal a healthy domain. A missing device is now checked before dereference.
An exception after possible composer prefix effects propagates unchanged but
seals the same dispatch domain before its counted entry exits. The small public
QuarantineDispatch method does only that seal; it adds no retirement queue or retry.
An active closure consequently becomes Unknown, refuses cancellation/reentry,
and cannot certify partial effects as a clean drain. Normal completion keeps its
existing result and outputs.

Module's production constructor obtains this domain from KernelCore::NvdrvDispatchDomain.
ProcessCohortStop already closes/polls it before inner host1x/managed-write domains
and composes cancellation with ResumeBatch. This repair reuses that identity and
ordering; cohort code is unchanged. ProducerDrain is not an FD-table mutex or
whole-System/facade lifetime owner. Existing caller serialization and facade
lifetime obligations remain; concurrent destruction of SurfaceFlinger is not
made legal by taking a local Module reference.

## Completion boundary: synchronous return is not all presentation

The entry counts only this synchronous call. With no guest acquire fences,
RequestComposite's existing synchronous request executes Composite inline in
TickWork. With guest fences it can instead register guest actions and return
before the later `present` closure runs. Those delayed actions, renderer recording,
submission/presentation workers and native completion remain separate producers.
Closing this domain alone therefore does not certify complete GPU exclusion or
visibility, nor does its successful bool prove every presentation callback ran.
Shutdown cancellation also retains RequestComposite's existing semantics.

The new callback case deliberately uses the real RunAcknowledgedCallback helper
with a fixture callee: dispatch returns while the action is Running, and the
composition entry must remain Pending until that callee actually returns. It
proves the entry cannot be released around a blocking callee; it does **not**
model production RequestComposite's guest-fence registration as full presentation
completion. The existing actual GPU-method suite independently regresses normal
OnCPURead/RequestComposite waiting and shutdown/cancellation behavior.

## Fresh remaining-route inventory

This is a source census of the examined families, not a complete producer list.

| Route / source | Observed current boundary | Still required |
| --- | --- | --- |
| SurfaceFlinger::ComposeDisplay, `nvnflinger/surface_flinger.cpp` | Synchronous direct device borrow/effects now counted in the Kernel NVDRV domain | Delayed guest actions/native GPU work and the facade lifetime contract remain independent |
| VI::AllocateSharedBufferMemory, `vi/shared_buffer_manager.cpp` | Fresh physical page-group initialization fills through DeviceMemory::GetPointer, outside managed Memory facade stores | Account for page allocation, raw fill and publication together; fresh allocation does not by itself authorize all later aliases |
| VI::WriteAppletCaptureBuffer, same file | Copies capture bytes directly through physical pointers into retained shared-buffer pages; no managed write entry | A bounded owned create/write/map/retire protocol, partial-effect disposition, and coherent invalidation; do not wrap it in an unproven F2 transaction |
| VI::AllocateHandleForBuffer/FreeHandle/CreateSession/DestroySession, same file | Live GetDevice and Container calls bypass ordinary Module dispatch at these call sites | Continuous Process/page-group/session/handle ownership and closure through creation and retirement; nested dispatch cannot be assumed safe after close |
| HID::Hidbus::UpdateHidbus/Initialize/Finalize, `hid/hidbus.cpp` | Direct shared-memory memcpy; timing callback and direct service operations have distinct lifecycles | Compose callback and service publication/retirement boundaries before revoking the physical shared view |
| IRS constructor and processor references, `hid/irs.cpp` plus `hid_core/irsensor` | Shared-memory pointer becomes persistent typed device entries handed to processors | Inventory and retain/revoke every processor/backend callback use; a pointer-acquisition counter cannot drain subsequent use |
| HID SharedMemoryHolder, `hid_core/resources/shared_memory_holder.cpp` | Persistent constructed SharedMemoryFormat pointer | Complete users/owner lifetime and publication closure, not just GetPointer interception |
| PSC Time::SharedMemory, `psc/time/shared_memory.cpp` | Constructor stores a persistent pointer; setters/UpdateBaseTime write it directly | Track actual ongoing users and two-slot counter/value publication; no lifetime proof follows from its lock-free format |
| Host1x::PushEntries, `video_core/host1x/host1x.h` | Public inline direct route; no call site found in current src census | Keep it unqualified, but do not invent a live producer or change scheduling to repair a dormant route |
| CDmaPusher::ProcessEntries / GPU::PushCommandBuffer | Current live host1x submit route is already counted at nvhost_nvdec_common::Submit; public/direct facades remain unqualified | Independent direct-caller, raw-view and native owner contracts; one audited caller does not certify every possible caller |
| nvhost_as_gpu::BindChannel GetDevice | Existing dispatch when reached via Module ioctl, plus its separate GPU binding entry | Do not treat the raw accessor itself as admission or assume every external caller uses Module dispatch |

An additional source-only warning in WriteAppletCaptureBuffer: its pointer loop
advances `start` to `end` before ApplyOpOnPointer and computes `end - start` there.
The resulting zero-length invalidation is not a coherency proof. This turn does
not claim runtime causality or fix that F1/F2/content-publication boundary.

Conductor still signals vsync after ComposeOnDisplay, including a false return;
that surrounding event producer is not closed by this synchronous entry repair.
Container::EnsureInitialized runs before the entry too. No claim is made that the
whole Conductor, service construction or all VI publication is drained.

## Windows verification and continuation

The new suite extracts unmodified ComposeDisplay and actual Module GetDevice,
TryEnterDispatch and QuarantineDispatch methods. ProducerDrain, ScopeGuard and
RunAcknowledgedCallback are real headers. Module layout/table, display/composer
and callback receiver are fixtures; no guest Memory/ThreadManager/native queue
completion is invented by them. Real threads/promises hold lookup, composer and
callback boundaries. The short future wait tests absence of early acknowledgement;
timeouts/crashes/unrelated errors are never accepted as defect controls.

Six paths run three times: closed/resume, close during device lookup, close during
composer with an active synchronous callback, exception with no closure, exception
with active closure, and missing display/stack/device. Five exact controls also
run three times: unrelated admission domain, release before device lookup, release
before composer, omitted exception seal, and enqueue-as-completion in the shared
callback helper. The old defect witnesses remain available via --revision.

The existing GPU lifecycle suite passes ten cases three times plus its cancelled
CPU-read control. Three actual Windows x64 production objects compile with real
pinned headers and zero diagnostics: SurfaceFlinger, NVDRV Module and nvdisp.
Hashes, cases and production evidence are recorded in
[results](M4_P2_SURFACE_COMPOSITION_RESULTS.json). No emulator link, real game,
native Vulkan failure/visibility or Android/Linux execution is claimed.

```powershell
$env:PYTHONUTF8='1'
python tools/physical_coherency_tests/run_surface_composition_drain_tests.py --baseline --revision b791777696d02bb88d7112b787a0315466edc08f --output-dir .diagnostics/m4-compose-baseline
python tools/physical_coherency_tests/run_surface_composition_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-compose-fixed
python tools/gpu_lifecycle_tests/run_tests.py --repeat 3 --output-dir .diagnostics/m4-compose-gpu
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/nvnflinger/surface_flinger.cpp --source src/core/hle/service/nvdrv/nvdrv.cpp --source src/core/hle/service/nvdrv/devices/nvdisp_disp0.cpp --output-dir .diagnostics/m4-compose-production
```

Next priority: isolate VI SharedBufferManager create/raw-fill/capture-write/destroy
ownership and interleavings before changing it. Keep admission nesting, partial
side effects, Process/backing and provider retirement explicit; simply adding an
outer counter around nested dispatch is insufficient. HID/time/IRS escaped views
and deferred GPU producers remain independent blockers. P2/M4 are incomplete,
F2/F3 stay open and ordinary GuestBackingAdmission remains Unknown. No main merge.
Only this round's temporary binaries/objects/fixture files are cleaned; final
documents, result manifests/logs, old result bundles and dependency caches remain.
