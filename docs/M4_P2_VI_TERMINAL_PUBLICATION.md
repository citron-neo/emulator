# M4 P2: VI OnTerminate, retained owners and terminal publication

2026-10-23; baseline `dc5d532840e4d99d5cfa3ecec26cd9a42449c366`.
Fork/PR375/local heads agreed before work. Main remains
`b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`. This repairs a terminal
publication gap; it does not complete M4 P2 or certify GPU/native retirement.

Follow-up: [warm Conductor/event-borrow drainage](M4_P2_VI_CONDUCTOR_DRAIN.md)
adds an explicit CPU callback/worker stop boundary and terminal unlink. The
limits below describe this earlier terminal-publication slice; native slot and
escaped-producer retirement remain separate.

## Actual shutdown/owner interaction

System::ShutdownMainProcess first calls GPU::NotifyShutdown, then
stop_event.request_stop, execution suspension and GPU::Shutdown. CloseServices,
Process/PageTable retirement and backing reset follow the checked GPU drain.
VI::LoopProcess registers a synchronous stop_callback calling Container::OnTerminate.
Consequently OnTerminate can run before the native drain, not only during the
later service destructor. The owning sequence is unchanged by this repair.

Unknown VI session records deliberately retain Container/Module/Process/backing
and required slot providers. That keeps objects alive, but does not authorize
those surviving facades to publish new workers or grant new entries after stop.
Explicit OnTerminate also mutates metadata despite shared provider retention;
reference counting alone is not a sustained producer closure.

The concrete defect is the cold/in-flight initializer. OnTerminate marks shutdown
and returns immediately when not initialized. EnsureInitialized formerly checked
only initialization, did unlocked service lookups, and checked only initialization
again before publishing manager/displays/Conductor. A lookup paused before stop
could finish after stop/GPU drain and start those resources. A cold or retained
terminated Container could likewise initialize or lend SharedBufferManager.

Before production edits, actual bodies reproduced three exact exit-91 witnesses:
cold post-termination publication; late lookup publishing after real stop callback
and a following GPU-drain marker; retained terminated facade lending a manager.
Historical --revision replay preserves them without modifying the checkout.
The marker models the permitted owning sequence; it is not a native Vulkan drain.

## Repair and linearization boundary

EnsureInitialized now returns bool and checks shutdown under m_lock **before**
the initialized fast path. It releases the mutex for service lookups, checks
missing providers without dereference, then reacquires the same mutex and checks
shutdown before any publication. OnTerminate and final publication therefore have
one lock order: termination winning prevents all subsequent new publication;
publication winning completes under the lock before OnTerminate can proceed.
No container lock spans service lookup or GPU draining.

All 17 EnsureInitialized caller sites handle refusal with ResultOperationFailed,
false, nullptr or an explicit void return. The entries which subsequently acquire
m_lock check shutdown again under that lock. This closes the second race where
EnsureInitialized succeeds but OnTerminate wins before the caller's own access.
GetBinderDriver now also locks before lending its provider. Read-only cold display
name operations and private teardown helpers are not broadened into this gate.

GetSharedBufferManager becomes a nullable terminal entry. All eight existing
VI/AM call sites are adapted: create, destroy, handle lookup, acquire, present,
acquirable-event, cancel and capture. Result routes propagate OperationFailed
before effects/output writes; the void destroy route returns without claiming a
completed retirement. A missing provider before any publication remains a normal
no-effect refusal, so an open Container can retry when it becomes available.
Normal initialization still publishes once, including competing initializers.

OnTerminate's existing teardown body is unchanged. The repair does not reset the
optional manager, session map or existing retained owner records on refusal.
Unknown anchors and default healthy terminal teardown retain their previous
contracts. The LoopProcess/service owners normally span GPU drain until later
CloseServices; arbitrary caller use after facade/System destruction remains
unsupported. Returning nullptr is not a lease on an already escaped raw pointer.

## Limits that remain explicit

The change prevents **new** terminal publication/lending. It does not join lookup
threads, revoke previously borrowed managers, drain active SharedBufferManager
operations, stop Conductor/CoreTiming, or prove every existing layer teardown is
safe before native completion. The warm OnTerminate cleanup and its interaction
with live/Unknown slot retirement remain an independent proof obligation.

In-flight service lookup still relies on normal service/facade lifetime and can
block as before. Construction exceptions after partial publication retain their
existing contracts; this is not a new initialization transaction or rollback.
Existing native drain/fail-stop behavior is regressed separately, not replaced by
a shutdown bool. No scheduling change, new owner queue or quarantine is added.
F2 content publication, checked-free ambiguity, HostMemory mapping-use lifetime,
complete producer/alias exclusion and ordinary Guest backing remain unproved.
All M4 phases stay incomplete; GuestBackingAdmission remains Unknown.

## Deterministic Windows validation

The new suite extracts actual OnTerminate, EnsureInitialized, manager/binder
accessors and ComposeOnDisplay, eight VI/AM caller bodies, and the production
stop_callback registration line. It uses real threads, promises, stop_source and
an instrumented mutex wrapping std::mutex. Lookup hooks and before-lock rendezvous
select the exact interleavings; an atomic lock observer catches locking over a
lookup without relying on a deadlock or recursively try-locking std::mutex.

Providers, display/layer lists, manager/Conductor construction, CMIF argument types
and the GPU-drain marker are fixtures. The retained payload check preserves the
manager/Module marker, not a fabricated real KProcess/native acceptance test.
The existing actual session-owner suite independently checks the actual owner
structures and quarantine behavior. The existing terminal-disposal suite separately
extracts owning System/GPU/renderer shutdown bodies and requires exact failure
diagnostics and absence of forbidden teardown.

Nine new paths run three times: cold stop, paused lookup across stop/drain,
retained terminated facade, normal single publication, concurrent init winner,
three second-lock races, unavailable-provider retry, all closed callers, and all
open callers. Nine exact controls run three times: omitted shutdown gates,
omitted late gate, omitted manager/binder/compose rechecks, initialized fast path
before terminal check, lock over lookup, fabricated Success on caller refusal,
and discarding the retained manager. Each requires its exact exit-91 witness;
timeouts/crashes/unrelated errors are not accepted.

Regression: 29 session-owner cases/ten controls (three repetitions); five success
and 23 failure terminal-disposal paths (three repetitions) plus five controls.
Six real Windows x64 production COFF objects compile with pinned actual headers
and zero diagnostics: VI Container, manager/system display services, AM display
layer manager, VI service factory and SharedBufferManager. No full emulator link,
game, native driver, Android/Linux or current-head CI success is claimed.
See [recorded results](M4_P2_VI_TERMINAL_RESULTS.json). The existing workflow gains
the suite and log/manifest artifacts; no manual workflow is dispatched.

```powershell
$env:PYTHONUTF8='1'
python tools/physical_coherency_tests/run_vi_termination_tests.py --baseline --revision dc5d532840e4d99d5cfa3ecec26cd9a42449c366 --output-dir .diagnostics/m4-vi-term-baseline
python tools/physical_coherency_tests/run_vi_termination_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-term-fixed
python tools/physical_coherency_tests/run_vi_session_owner_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-term-session
python tools/gpu_lifecycle_tests/run_terminal_disposal_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-term-disposal
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/vi/container.cpp --source src/core/hle/service/vi/manager_display_service.cpp --source src/core/hle/service/vi/system_display_service.cpp --source src/core/hle/service/am/display_layer_manager.cpp --source src/core/hle/service/vi/vi.cpp --source src/core/hle/service/vi/shared_buffer_manager.cpp --output-dir .diagnostics/m4-vi-term-production
```

Next: audit warm OnTerminate layer/slot effects and Conductor/active-operation
drain ordering against real GPU shutdown. Do not infer them from this terminal
publication gate. Main is unchanged. Current-run temporary executables/objects/
fixture sources are cleaned after preserving logs/manifests, without touching old
result bundles or dependency caches.
