# M4 P2: warm VI termination and Conductor/event-borrow drainage

2026-10-23 (Asia/Taipei). Baseline `31d980ce84ec0ada96252593c4f877070a88c5da`;
local/fork/PR375 heads agreed before work. Work remains on
`codex/issue-372-fixes`; main `b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`
is unchanged. This is a CPU callback/worker lifetime repair, not native GPU
retirement or completion of M4 P2.

Follow-up: [warm slot/escaped producer publication](M4_P2_ABANDONED_SLOT_PUBLICATION.md)
closes the abandoned-queue SetPreallocatedBuffer hole. It preserves the separate
cached framebuffer/GraphicBuffer owners; it supplies no native completion proof.

## Actual ordering and the concrete gap

The real System shutdown sequence is GPU NotifyShutdown, synchronous
stop_event.request_stop, execution suspension, checked GPU Shutdown, then
CloseServices and provider/Process/backing destruction. VI LoopProcess registers
a stop_callback calling Container::OnTerminate. Its warm cleanup thus precedes
the owning native GPU drain.

OnTerminate's Container lock already excludes an active ComposeOnDisplay body:
that method holds the same lock through SurfaceFlinger::ComposeDisplay and the
synchronous HardwareComposer/nvdisp call. That protects that particular CPU
metadata borrow; it does not account for native work submitted by that call.
After ComposeOnDisplay returns, Conductor::ProcessVsync still signals raw Event
pointers outside the Container lock. Neither the timing event nor the VSync
thread was stopped by warm OnTerminate. A retained Container could leave those
sources live indefinitely. The prior terminal-publication gate prevents new
composition but does not prevent these subsequent event signals.

IApplicationDisplayService's destructor calls Container::UnlinkVsyncEvent before
destroying its map of Event objects. Previously that unlink returned on shutdown,
leaving the raw pointer in VsyncManager. In addition, the manager's set was
mutated under the Container lock but iterated without that lock or its own lock.
That is an ordinary-runtime signal/unlink race as well as a terminal borrow gap.

Conductor's destructor only unscheduled timing and requested/woke stop. It relied
on jthread member destruction for join. Cadence fields are declared after that
thread, so their lifetimes end before the implicit join; an active worker can
still access them. We repair this with an explicit body-level join, without
reordering members.

Before production edits, three actual-body witnesses reproduced exact exit 91:
termination returned with live Conductor, destructor-body return before explicit
join, and terminal unlink retaining a borrowed event. Revision replay is supported
without switching the checkout. No timeout or crash is accepted as a witness.

## Close, wait and revoke

OnTerminate now serializes its entire sequence with a separate termination mutex.
Under m_lock it publishes terminal state, prevents new entries, and obtains the
stable optional Conductor. It releases m_lock before calling Conductor::Stop,
then reacquires m_lock for the existing layer/display cleanup. A second
OnTerminate cannot return past a first caller's incomplete drain; repeated
completed termination is a no-op. Cold termination does not initialize anything.

Stop uses CoreTiming::UnscheduleEvent's default Wait mode. Source inspection
confirms that mode removes queued events/increments their sequence under
basic_lock and waits for advance_lock; Advance holds advance_lock through the
callback and rejects rescheduling after a sequence change. Then Stop requests
the worker stop token, wakes its signal, and explicitly joins. The wake checks
the stop token before doing another composition, including direct destruction
when System::IsShuttingDown is false. Stop is idempotent for serial owner calls;
Container serializes its own calls and destructor. Calling Stop concurrently
through arbitrary raw Conductor pointers, or from its own callback/worker, is not
supported. No callback takes the termination mutex.

No Container lock spans UnscheduleEvent or join: single-core callbacks and the
multicore worker can need m_lock to leave ComposeOnDisplay. Existing in-lock
composition finishes before closure; a waiting later composition is refused.
The normal open callback still performs synchronous composition followed by
vsync signaling, including a display with no newly composed frame. This repair
does not reinterpret enqueue/cancellation as callback success, or change
RequestComposite/OnCPURead completion helpers.

Unlink no longer lazily initializes or refuses merely because shutdown started.
It removes a subscription from an initialized Conductor during or after Stop.
VsyncManager owns a mutex spanning Signal's entire raw event access and each
link/unlink mutation. Once unlink returns, an existing signal borrow has finished
and subsequent iteration cannot find that pointer. Event::Signal calls kernel
event signaling and does not re-enter Container or VsyncManager. The map is
populated with try_emplace because a mutex-bearing manager is nonmovable.
Stop does not clear manager/session/provider ownership or discard Unknown records.

## Layer/slot and remaining active-operation audit

DestroyLayerLocked removes the SurfaceFlinger layer record and unregisters its
producer/consumer binder IDs. A display stack may retain the Layer until the
following RemoveDisplay. Final Layer destruction calls BufferItemConsumer::Abandon;
ConsumerBase clears slot GraphicBuffers/fences and disconnects the consumer.
HardwareComposer also has a separate cached framebuffer table. References in
that table, an escaped producer and existing prepared retention records have
different lifetimes; retaining the VI facade does not freeze this metadata.

This patch makes those existing effects occur after Conductor/timing CPU
accesses finish. They still occur before System's checked GPU Shutdown. It does
not move native drain earlier, move layer disposal later, certify that all slot
resources can retire, or drain every service operation holding an escaped
manager/producer. Existing NVDRV admission and checked retirement/quarantine
remain necessary; a Conductor join supplies no Vulkan submission identity,
visibility or completion evidence. The composition regression separately checks
its existing synchronous dispatch/acknowledged-callback boundary.

Next scoped work: trace warm slot release and HardwareComposer cached framebuffer
owners through GraphicBuffer retirement versus native/presentation completion;
include a producer borrowed before terminal closure. Require a deterministic
owner-release witness before changing the layer cleanup/GPU sequence. Do not
replace Unknown retention with a shutdown bool or a reference-count assumption.
In-flight lazy lookups, partially throwing construction, arbitrary raw aliases,
whole System/kernel lifetime and cross-service metadata access remain outside
this repair. F2/F3 remain open; GuestBackingAdmission remains Unknown and all M4
phases remain incomplete.

## Windows evidence

`run_vi_conductor_drain_tests.py` extracts the actual Conductor class/constructor/
destructor/Stop/VsyncThread/ProcessVsync/cadence, VsyncManager class/methods,
Container OnTerminate/ComposeOnDisplay/Unlink and VI stop callback registration.
Real std::threads, jthreads, mutexes, stop_source/callback, condition variables
and gates select interleavings. The destructor probe observes body return before
member teardown, avoiding an intentional use-after-lifetime execution.

System, display/layer/SurfaceFlinger providers, Event kernel effects and the
timing scheduler are fixtures. The scheduler models cancellation and waiting
with a real callback mutex; it does not execute actual CoreTiming::Advance or
UnscheduleEvent. Their waiting/reschedule contract was inspected separately.
The GPU marker is only an order marker. This suite is not real GPU/native driver,
KProcess/Memory/GetPointerImpl, full emulator link, game, Linux or Android proof.

Ten paths each pass three repetitions: warm signal paused across the real stop
callback; destructor join; terminal unlink; normal active/idle stop wake; single
core; active timing callback; competing terminal calls; signal/unlink borrow;
cold termination; active ComposeOnDisplay. Eleven exact negative controls each
pass three repetitions: omit Stop, explicit join, unschedule, unlink, signal
lock, wake stop-token check, callback waiting, termination serialization or unlink
lock; wait under Container lock; clean layers before Stop. Each requires its
specific exit-91 message. An initial wake-check control survived the active-worker
case; an independent idle-wake gate was added and the complete final suite rerun.

Regressions: terminal publication nine paths/nine controls; session ownership
29 paths/ten controls; terminal disposal five success/23 failure paths/five
controls; synchronous composition six paths/five controls. Path repetitions are
three; controls repeat three except the pre-existing terminal-disposal controls
which run once. All passed.

Six actual Windows AMD64 COFF objects compiled with GCC 13.2 and the existing
pinned real dependency headers, zero diagnostics: Container, Conductor,
VsyncManager, IApplicationDisplayService, SharedBufferManager and VI LoopProcess.
See [recorded results and input hashes](M4_P2_VI_CONDUCTOR_RESULTS.json).
No current-head CI result or native application run is claimed. The existing
integration workflow gains this suite/artifacts; no manual run is dispatched.

```powershell
$env:PYTHONUTF8='1'
$env:CXX='C:/Strawberry/c/bin/g++.exe'
python tools/physical_coherency_tests/run_vi_conductor_drain_tests.py --baseline --revision 31d980ce84 --output-dir .diagnostics/m4-vi-cond-baseline
python tools/physical_coherency_tests/run_vi_conductor_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-cond-fixed
python tools/physical_coherency_tests/run_vi_termination_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-cond-terminal
python tools/physical_coherency_tests/run_vi_session_owner_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-cond-session
python tools/gpu_lifecycle_tests/run_terminal_disposal_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-cond-disposal
python tools/physical_coherency_tests/run_surface_composition_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-cond-surface
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/vi/container.cpp --source src/core/hle/service/vi/conductor.cpp --source src/core/hle/service/vi/vsync_manager.cpp --source src/core/hle/service/vi/application_display_service.cpp --source src/core/hle/service/vi/shared_buffer_manager.cpp --source src/core/hle/service/vi/vi.cpp --output-dir .diagnostics/m4-vi-cond-production
```

Current-run temporary binaries/objects/fixtures are removed only after preserving
all JSON/log files and checking input and COFF hashes. The committed result bundle
and local `.diagnostics/m4-vi-cond-results` retain evidence. Old diagnostics and
dependency caches are untouched; main is not merged.
