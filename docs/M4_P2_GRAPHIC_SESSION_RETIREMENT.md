# M4 P2: retained GraphicBuffer and session retirement

Date: 2026-10-23 (Asia/Singapore). Baseline: `4c1e8e0f05`.

Follow-up: [abandoned queue publication](M4_P2_ABANDONED_SLOT_PUBLICATION.md)
checks SetPreallocatedBuffer's local queue lifetime before effects and final
publication. Its refused candidate uses the prepared retirement described here.

## Owned lifecycle instead of destructor-time borrowed access

GraphicBuffer previously kept a raw NvMap pointer and directly unpinned/freed
its handle in its destructor. NVDRV destruction directly closed a Container
session outside the dispatch closure. Those paths could mutate owners after
dispatch had drained, and destruction could outlive the provider it borrowed.

BufferQueueProducer and GraphicBuffer now retain a shared Module provider.
GraphicBuffer creation prepares a unique retirement record BEFORE duplicate/pin
effects. Its callback captures Module, the actual shared nvmap handle, a checked
KProcess reference, and actual retained physical backing storage. Session
initialization likewise prepares its retirement record and owners BEFORE
OpenSession, then publishes the returned session id into that record. If
initialization fails after possible effects, an unknown id is retained rather
than guessed for cleanup.

The records use a preallocated intrusive OwnedRetirementQueue. Destruction can
retire immediately only through fresh dispatch admission. Closed retirement
enqueues without allocating or invoking cleanup; owner captures remain alive.
After healthy cancellation, a later admitted cold Module dispatch or resource
preparation pumps a bounded batch. Every replay needs fresh admission. Atomic
ResumeBatch itself still invokes no callback and grants no native authority.

The queue has independent shared lifetime. GraphicBuffer/NVDRV destructors drop
their extra Module facade reference before consuming the record. This lets the
record's counted final release include provider destruction when it is the last
owner, without destroying the queue facade while its method is running. Queue
callbacks and final capture destruction execute outside the queue mutex. A
BufferQueueProducer keeps its Module reference until after its core/slot fields.

## Disposition and Unknown

| State | Resource disposition |
| --- | --- |
| Closed before SetPreallocatedBuffer | WouldBlock; no duplicate/pin or old-slot replacement |
| Wrapper/record allocation fails before effects | No new nvmap effect; existing slot remains intact |
| Pin returns zero or throws after duplicate/possible mapping effects | Seal dispatch; retain Module/handle/Process/backing; no speculative rollback |
| Normal destructor while admission open | Count unpin/free or session close and final capture/provider release |
| Destructor while closed | Queue the preallocated record; keep owners and perform no cleanup |
| Unpin throws, Free refuses, or session close throws | Seal and keep the uncertain record; never replay its partial effects |
| An older retirement returns after a newer failure | Serialize release with quarantine; keep the older captures too |
| Session id was never published | Preserve the whole prepared owner record; do not guess a close target |
| Backing epoch has died | No callback into the retired Process/Kernel facade; quarantine retained identity/backing |

Records are uniquely owned, so destructor retirement cannot be submitted twice
through another shared record copy. Pending records deliberately retain their
Module/queue owner graph until a later legal pump. Unknown records retain that
graph permanently in this patch. There is no automatic recovery or terminal
release API which could accidentally free uncertain resources.

## Session ownership, destruction and final Process release

Container sessions now retain KProcess and actual backing, not only a raw process
address. RetainSession checks active state, positive session refs, backing epoch
and checked process acquisition before granting a GraphicBuffer owner snapshot.
CloseSession rejects invalid/already-retired ids before changing reference
accounting. Shutdown close still preserves mappings/ASIDs as before; inactive
does not imply those resources are safe to free.

Mapper/handle state is destroyed before Process, then backing. Container member
order preserves sessions while nvmap handle state is destroyed. Session move
assignment retires the old complete object before installing the replacement;
default member assignment would release old backing before its mapper. OpenSession
prepares a pending owner and failure slot before ASID publication, and keeps both
pending and reused-old sessions outside session_guard's destruction order. Final
Process release and mapper destruction therefore occur after unlocking, including
the failed-initialization path.

The final Process::Close may itself enter Finalize and throw. Preallocated
ProcessReleaseFailure slots detach that one close attempt from ordinary noexcept
KScopedAutoObject member destruction. On failure they keep backing, seal dispatch
and publish opaque identity into a process-lifetime intrusive quarantine ledger.
They never reopen or retry a zero-reference object. A dead backing epoch is
handled without even dereferencing the old Process. The identity is diagnostic
only; this is not a new valid Process reference or permission to retry cleanup.
These slots also intentionally have no automatic terminal release in this patch.

## Executed Windows validation

```powershell
python tools/physical_coherency_tests/run_owned_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-owner-retirement-queue
python tools/physical_coherency_tests/run_nvdrv_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-owner-retirement-lifecycle
python tools/physical_coherency_tests/run_nvdrv_dispatch_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-owner-retirement-dispatch
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/nvdrv/nvdrv.cpp --source src/core/hle/service/nvdrv/nvdrv_interface.cpp --source src/core/hle/service/nvdrv/core/container.cpp --source src/core/hle/service/nvnflinger/ui/graphic_buffer.cpp --source src/core/hle/service/nvnflinger/buffer_queue_producer.cpp --source src/core/hle/service/nvnflinger/surface_flinger.cpp --source src/core/hle/service/nvdrv/core/nvmap.cpp --source src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp --output-dir .diagnostics/m4-owner-retirement-production
```

All three suites pass three positive repetitions. Seven queue controls, twenty-one
lifecycle controls and twenty-two dispatch controls hit their named runtime
failures (50 total). Queue witnesses reject allocations after preparation on
closed/quarantine paths, count final captured-owner release, and test older
completion racing newer quarantine. Actual extracted factories/destructors,
SetPreallocatedBuffer, Session lifetime/release methods, CloseSession and
RetainSession run with fixture Process/backing/nvmap/native collaborators.
OpenSession's exact ownership/reuse prefix is exercised; the application heap
preallocation optimization is excluded from that fixture.

Cases include closed destructor deferral/resume, first/partial pin failures,
unpin/free/session-close failures without replay, unknown session preservation,
old-slot preservation, empty-descriptor compatibility, duplicate-close refusal,
retired-session borrow rejection, reused-owner release outside session_guard,
Process Close throwing after zero, stale epoch protection, and retained failure
ledger bookkeeping. Kernel reference counting/finalization, real device mappings
and GPU work are fixtures here, not native execution acceptance.

Eight complete Windows production units (Module, interface, Container,
GraphicBuffer, BufferQueueProducer, SurfaceFlinger, nvmap and nvhost-as-gpu)
compile to AMD64 COFF objects without diagnostics. Source hashes, object digests
and machine signatures are checked before cleanup. Generated fixtures, manifests,
logs, binaries and objects from this round are removed; reproducible sources and
this Markdown evidence remain. The existing workflow includes both new runners;
no workflow is dispatched. No local Android build or full application link runs.

## Remaining boundary

This closes these ordinary local publication/retirement paths; it does not
close every display reader/writer, escaped GetContainer/GetDevice alias, NCE/raw
store or queued/native GPU producer. Unpin/free and session metadata return do
not establish GPU completion or visibility. The owner bundle here retains local
Module/handle/Process/backing state, not independent ownership of System,
Host1x, native device, allocator and all GPU aliases. Those facades still need
their existing caller lifetime and a separately proven terminal sequence.

Unknown intentionally retains resources, potentially an entire backing
reservation and Process resources; native/Android availability and memory impact
have not been measured. No forced release is introduced to make teardown appear
successful. Full native owner retirement and trusted disposal of the quarantine
ledger remain explicit P2 prerequisites. P2/M4 are incomplete and ordinary
GuestBackingAdmission stays Unknown; original F2/F3 controls are not weakened.
