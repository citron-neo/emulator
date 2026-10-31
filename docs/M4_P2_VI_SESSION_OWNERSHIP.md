# M4 P2: VI partial-session and refused-close ownership

2026-10-23; baseline `0a22468339bc023a8fb0621a201bfacd2ac79f04`.
The fork/PR375 head agreed before edits. Main remains
`b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`. This closes the two previously
identified local failure transitions conservatively; M4 P2/F2/F3 remain open.

Follow-up: [VI terminal publication](M4_P2_VI_TERMINAL_PUBLICATION.md) now stops
retained Containers and interrupted lazy initialization from publishing/lending
new entries after OnTerminate. It preserves the owner records here; existing
layer teardown, escaped managers and complete GPU drain remain separate gates.

## Defects and test-first witnesses

The original CreateSession inserted an unqualified numeric record before
OpenSession/Open/handle/layer/slot publication finished. A layer error returned
failure, but GetSharedBufferMemoryHandleId exposed that partial record as usable.
DestroySession later interpreted default/partial IDs as a complete session.
The original destroy path also ignored Module::Close's result before calling
Container::CloseSession and erasing the record. Close could refuse after the
layer/free prefix had already executed, losing the only local disposition record.

Before production edits, extracted actual create/destroy/getter and Module::Close
bodies reproduced both defects with exact exit-91 markers: failed create published
a partial session, and a close racing the handle-free prefix still released the
session owner. Historical --revision replay preserves both witnesses. No crash,
timeout or unrelated error counts as reproduction.

## Prepared owners and explicit states

Before effects, create acquires the existing Kernel NVDRV domain entry, retains
the shared VI Container, then allocates a SharedBufferOwners bundle and an existing
OwnedRetirementQueue record. Module::RetainProcessLifetime uses the established
KScopedAutoObject/RetainedBackingStorage/SessionLifetime mechanism to retain Process
and backing before mapping/session publication. Module stays owned too. These
preparations occur before taking m_guard. The sole production Container factory
already uses make_shared; Container now exposes enable_shared_from_this. Unowned
creation refuses before effects instead of inventing ownership from a raw this.

The map node is prepared under m_guard before pool/mapping/device effects. Its
state is Creating, and its failure guard is installed before the first such
effect. The pool descriptor is installed in the retained manager before allocation
and raw filling: a raw-fill exception must not destroy a temporary descriptor and
lose the retained physical-page description. It is not publicly usable at that
point. Failed allocation/initialization is conservative Unknown; this protocol
does not assume every allocator failure has no effects.

After obtaining a producer, the bundle also retains it before either preallocated
slot call. This matters even if a later stop path unregisters the binder: a shared
VI facade alone does not prevent explicit OnTerminate metadata mutations. Slot
provider retention protects the already published local slots without treating
OnTerminate as a closed producer. Normal committed sessions keep only a weak slot
provider and release their strong Container anchor, avoiding a healthy self-cycle.

| Boundary | Public state / result | Owner disposition |
| --- | --- | --- |
| Closed create, missing retainer or preparation allocation failure | Failure/exception before effects; no new map record or output | Local preparations release outside the manager lock; healthy domain can still cancel/resume |
| Failure or exception after effect boundary | Unknown tombstone, hidden from getter; error/exception preserved | Preallocated record is moved to existing queue quarantine before the operation entry exits; strong VI/pool/Module/Process/backing and any needed slot provider remain retained |
| Full successful create | Ready only after both slot calls succeed; outputs published last | Complete record retained locally; healthy VI/provider strong self-anchors removed |
| Destroy refused at outer admission | Ready transitions to hidden Unknown, no layer/free/Close effects | Re-anchor provider/VI and quarantine prepared record; void return is not a successful destroy acknowledgement |
| Destroy layer/free error, Close refusal, or exception | Unknown with the exact completed prefix preserved; no later suffix/erase | Retain the prepared graph, seal domain, never replay any prefix |
| Successful destroy | Remove record only after checked layer/free/Close and session close | Release settled slot prefix in original order; final Process/backing/provider captures release outside m_guard but inside the counted operation |

Unknown records deliberately remain tombstones rather than being silently erased
or exposed as Ready. Subsequent destroy returns without replay, and creation cannot
reopen a sealed domain. The existing queue's Unknown list never invokes these
records. The lambda is a refusal, not a deferred cleanup callback: no borrowed VI
or Host1x facade is scheduled to run later. There is no new queue/global registry,
owner resurrection, speculative unmap, rollback or in-process recovery.

Normal scalar VI errors after an effect boundary now conservatively seal the
shared NVDRV domain and retain resources. This can stop subsequent NVDRV work;
it is intentional failure isolation, not silent resource recovery. It replaces
partially usable records/assertion cleanup with an explicit retained Unknown.
Trusted disposal/recovery for that retained graph remains independent work.

## Creation and destroy implementation boundaries

Open's invalid FD is checked before GetDevice. Handle setup no longer performs a
speculative IocFree rollback on a partially effectful allocation failure. Missing
devices, blending errors and slot-publication refusal propagate Result failures
instead of assertions. Producer ownership is established before slot effects.
Getter refuses Creating/Unknown even when a buffer ID and numeric session exist.

Destroy re-anchors VI/slot providers before effects. Successful layer destruction
releases the completed slot/provider prefix before the external handle and nvmap
session, preserving the previous normal order. It then checks the free helper and
Module::Close. A closure winning between handle-free and Close becomes Unknown;
CloseSession and erase are not run. Exceptions from device close or session close
also preserve the graph without retrying partially completed operations.

Final normal owner destruction can call Process lifecycle code; it is explicitly
moved after manager unlock and before the outer entry exits. A local shared
Container anchor spans that release as well. The existing SessionLifetime's
Process-close/backing failure retention is reused; no new final-close retry is
introduced. Default healthy manager teardown still relies on its existing owning
System terminal ordering; arbitrary concurrent facade destruction is not granted.

## Nested admission and limits

Creation and destroy now count their own raw/direct synchronous effects, but
Module::Open/Close and slot publication still have independent admission. A close
can win after an admitted outer prefix. This slice resolves that counterexample
with retained Unknown, not a successful continuation or fabricated no-effects
receipt. It does not change scheduler behavior or bypass closed inner admission.
A future continuable admitted-operation protocol needs its own proof/tests.

The owner bundle retains local Process/backing and providers. It is not an owner
of the entire System/Host1x/native device graph, nor a host-virtual mapping-use
lease or a complete alias census. Explicit VI::OnTerminate and other service
publishers still need sustained close/drain composition. CPU alias unmapping,
native completion/visibility and all unknown owner disposal are not certified.
Retaining a shared provider does not freeze its mutable metadata.

The capture byte/projection code is unchanged: the existing zero-length
invalidation concern, first-registration/F2 preservation, raw aliases and
HostMemory remapping remain open. An IocFree/Close Result alone is not a new proof
of whole-guest physical publication or native completion. All three M4 phases
remain incomplete and ordinary GuestBackingAdmission remains Unknown.

Existing IocFree can fold different internal FreeHandle outcomes into Success.
This slice does not upgrade that result into checked unmap/native completion;
the broader checked-free contract remains an independent audit boundary.

## Deterministic Windows evidence

The suite extracts actual CreateSession, DestroySession, getter, owner structures,
AllocateSharedBufferMemory and Module::Close, and uses real OwnedRetirementQueue,
ProducerDrain and ScopeGuard. Only the allocator helper's System namespace spelling
is adapted to the fixture. Process/backing retention, physical allocation/store,
map/nvmap/layer/slot providers remain fixtures. It does not use real KProcess,
Memory/GetPointerImpl, scheduler or a native GPU as successful guest admission.

29 paths run three times. These include normal cleanup, healthy cycle absence,
closed create/destroy, missing Process retainer and a real injected preparation
bad_alloc before effects, close during create Open, allocation/raw-fill/mapping/
session/Open/handle/layer/blending/producer/slot0/slot1 failures, representative
exceptions, close after the handle-free prefix, and destroy layer/free/device-close/
session-close failure prefixes. Binder-removal simulation confirms uncertain slot
providers remain retained after outside registration and VI/Module roots drop.
The release hook checks both manager unlock and Pending counted closure during
final normal Process-owner release.

Ten precise mutation controls each run three times: expose partial getter state,
ignore Close refusal, ignore closed retirement, omit create quarantine, omit VI
anchor, release outside entry, release under manager lock, keep healthy VI cycle,
omit slot-provider ownership, and publish pool descriptor only after raw fill.
Each requires its exact exit-91 witness. The allocation injector is fixture-only;
it does not alter production allocation behavior.

Additional validation: prior capture suite (eight paths/eight controls, three
repetitions), existing actual retirement queue suite (three repetitions/seven
controls), and six real Windows x64 production COFF objects with zero diagnostics:
shared_buffer_manager, nvdrv Module, VI Container, manager_display_service,
AM display_layer_manager, and VI service factory. No full app link/game, real
native GPU, Android/Linux or current-head CI acceptance is claimed. Hashes and
manifests are in [results](M4_P2_VI_SESSION_RESULTS.json). The existing integration
workflow gains this suite/log manifests; no manual workflow is dispatched.

```powershell
$env:PYTHONUTF8='1'
python tools/physical_coherency_tests/run_vi_session_owner_tests.py --baseline --revision 0a22468339bc023a8fb0621a201bfacd2ac79f04 --output-dir .diagnostics/m4-vi-session-baseline
python tools/physical_coherency_tests/run_vi_session_owner_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-session-fixed
python tools/physical_coherency_tests/run_vi_capture_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-session-capture
python tools/physical_coherency_tests/run_owned_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-session-retirement
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/vi/shared_buffer_manager.cpp --source src/core/hle/service/nvdrv/nvdrv.cpp --source src/core/hle/service/vi/container.cpp --source src/core/hle/service/vi/manager_display_service.cpp --source src/core/hle/service/am/display_layer_manager.cpp --source src/core/hle/service/vi/vi.cpp --output-dir .diagnostics/m4-vi-session-production
```

Next work should audit explicit VI OnTerminate/Container publication with these
owners and the existing GPU terminal ordering, then select a deterministic gap.
Do not grow quarantine as a replacement for a reversible producer/native drain.
Main is unchanged. Only current-run temporary executables, objects and generated
fixture files are cleaned after preserving logs/manifests; dependency caches and
older result bundles remain untouched.
