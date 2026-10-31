# Mapping ownership transactions and teardown

## MapGroup resource disposition (before implementation)

Every group operation opens its own mapping references (Open or OpenFirst).
Allocation references held by the caller are separate and can close on unwind.
A descriptor copy must complete before the first mapping effect. Allocation
failure while making that copy releases the operation references through the
existing scope guard, including OpenFirst allocations.

| Outcome | PTE / VMA disposition | Operation references | Recovery record / permission |
| --- | --- | --- | --- |
| All nodes succeed | Entire range published | Transfer to mappings | Discard temporary descriptors; normal metadata commit follows |
| First node explicitly refuses entry | No effects | Close all operation references | Discard descriptors; no quarantine |
| Prefix succeeds, suffix explicitly refuses | Prefix published, suffix untouched | Retain all, including suffix | PageTable owns copied group, range and heap mode; reject subsequent Operate calls |
| Any attempted node throws with Unknown effects | Attempted range may remain accessible | Retain all | Same retained record; never infer rollback or safe resume |
| Descriptor allocation fails before mapping | No effects | Close all | No pending record |
| Teardown unmap fails | Unknown accessibility | Retain all | Keep record and PageTable; do not close physical references |
| Teardown unmap returns successfully | Entire recorded range detached | Close once, then discard record | Normal block finalization can follow without rediscovering these PTEs |

Retaining the untouched suffix is conservative, bounded by the failed group and
explicitly owned; it is not an unreachable reference leak. Records are prepared
before effects and linked into the table without allocation during unwind.
Known refusal uses a distinct exception type. Other exceptions are Unknown even
if a particular injected collaborator happened to throw before effects.

The pending range has not received the caller's post-success memory-block
update. Teardown detaches it before walking that metadata, preventing double
physical Close. A quarantined table rejects further Operate calls so its pending
range cannot be silently replaced through those paths. Raw PTE users and other
Memory instances remain outside this component's coverage.

## Process teardown order (design before implementation)

1. The process owner has already stopped execution and retains Process, TLS,
   PageTable, Memory, System and HostMemory. This is not a new all-producer census.
2. Close controlled CPU writes. Close normal mapping admission and join existing
   publication entries before taking page-table / scheduler / HostMemory locks.
3. Obtain an exclusive, owner-bound teardown entry. It keeps admission closed;
   it authorizes cleanup unmaps only, never new maps, protections or context
   publication. It retains the mapping domain's lifetime lock until table cleanup
   completes. Foreign, moved-from or expired entries must be refused before effects.
4. Delete TLS using that explicit entry. The TLS page remains reachable in its
   process tree until its page finalizer succeeds; descriptor Free follows unmap
   and backing release. Propagate a failed Result instead of treating it as success.
5. With the same entry, detach pending group ranges, then finalize ordinary blocks
   and reset the PageTable. Failed cleanup retains its record and owner resources;
   it does not reopen normal admission or claim arbitrary callback retry safety.
6. Release the teardown entry before Memory::Finalize retires/disposes its Impl.
   System resources and backing managers must remain alive through all page closes.

The entry is issued only to the process owner through Memory's private interface.
UnmapPages and TLS finalizers carry it explicitly; no thread-local bypass or
global cleanup flag is permitted. HostMemory's own admission remains independent:
this entry cannot override a frozen HostMemory domain. Its refusal keeps resources
retained and propagates failure. Full process retry after partial external effects
is not promised by this protocol.

Ordinary processes remain untracked. F2/F3 and general guest backing admission
remain Open/Unknown. Validation is Linux only.

## MapGroup implementation evidence

The PageTable now prepares a copied group record before effects. Known first-entry
refusal closes the scoped references; a published prefix or Unknown transfers
them into that retained record without allocating on unwind. Operate refuses
further mutations while a record is pending. Finalization unmaps the retained
range before CloseAndReset and before the ordinary block walk. Failure retains
both descriptors and references. Controlled cleanup still needs the explicit
teardown entry described above; normal admission is never reopened.

The extracted production-body Linux fixture passes three repeats for all three
group operations, normal/first-refusal/prefix-refusal/Unknown/untracked cases,
descriptor failure before effects and successful untracked pending cleanup.
Restoring reference Close during retained unwind is rejected by its ownership
assertion. These are injected reference/PTE collaborators, not a native allocator
or process teardown witness.

## Teardown implementation evidence

Memory issues the teardown entry through a private KProcess friend interface.
The entry seals normal admission and retains the exclusive publication lifetime
lock. Validation checks domain identity, thread identity and moved-from state.
The explicit UnmapRegionForTeardown wrapper validates before Impl effects;
Map/Protect/context publication have no teardown overload. Controlled PageTable
finalization requires this entry, including when no incomplete group exists.

KProcess carries the entry through DeleteThreadLocalRegion, TLS Finalize,
UnmapPages and PageTable Finalize, then destroys it before Memory::Finalize.
Failed TLS cleanup remains reachable in a separate finalizing tree, excluded from
allocation. A failed Result is propagated; a second cleanup of that quarantined
TLS page refuses until a separate recovery protocol exists. The PLR address is
cleared only after success. The slab Destroy path cannot proceed to Free when
Finalize throws, so its Memory/PageTable owner resources are not recycled.
The object-list unregister earlier in Destroy is not rolled back; this is a
fail-stop retention protocol, not restartable full-process destruction.

The Linux extracted-body fixture executes the actual TLS finalizer, process TLS
deletion, Memory cleanup wrapper and the initial KProcess::Finalize owner sequence
(through PageTable finalization). Its remaining owner-resource cleanup is an
injected tail, not the full native destructor. It checks normal/controlled paths,
partial/full TLS allocation trees, Result/exception/Unknown cleanup failures,
foreign entry refusal before TLS and unmap effects, moved/thread identity, closed
normal admission, lifetime joining and Memory disposal after token release.
Three positive repeats and two assertion-specific mutants pass. The group fixture
also checks failed cleanup retains its record and references, followed by an
explicitly idempotent injected recovery; no general Unknown retry is inferred.

The preceding Unmap-only Linux CI run 39384043830 has completed successfully at
74aea98af7. It does not validate these new transactions. A fresh Linux-only native
run is required for this source; Android remains excluded.
