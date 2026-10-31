# M4-F3 cross-call ownership audit

The findings below describe the pre-repair source. The three follow-up commits
repair bookkeeping, retain incomplete MapGroup ownership and introduce an
explicit process teardown entry. Current disposition, recovery limits and test
evidence are in [mapping ownership transactions](M4_F3_MAPPING_OWNERSHIP_TRANSACTIONS.md).
This supersedes the original OPEN reproduction outcomes, not F2/F3 status.

## Implementation checkpoint: Finalize bookkeeping

The block-manager callback now runs while its current record remains linked.
Only a successful return commits erase and descriptor Free. Linux extracted-body
tests cover a throw before/after effects on the first/second block, exact remaining
tree and descriptor counts, and successful completion using an explicitly
idempotent retry collaborator. Restoring erase-before-callback must fail the
record-during-callback assertion. This repairs bookkeeping exception safety only;
unknown callback effects still require an owner-specific recovery protocol.

This source audit follows the per-Memory publication component and the Unmap
reference repair. It identifies remaining OPEN prerequisites; it does not enroll
ordinary processes or establish safe teardown, rollback or backing admission.

## MapGroup: published prefix loses its mapping references

The KPageTableBase::Operate group overload covers MapGroup, MapFirstGroup and
MapFirstGroupPhysical. KScopedPageGroup opens every group reference before the
loop, but only cancels automatic Close after every Memory::MapMemoryRegion call
returns. Each Memory call has a separate controlled entry.

If closure wins between two calls, the first node remains mapped and the second
call refuses before effects. Stack unwinding then closes every group reference,
including the published prefix. An exception after possible effects in the first
call has the same ownership problem. MapFirstGroup starts those references with
OpenFirst; it does not have an independent original reference as a fallback.

AllocateAndMapPagesImpl, MapInsecureMemory and SetHeapSize also close their
allocation references on scope exit. Those original references therefore do not
provide lasting ownership for an abandoned published prefix. Their memory-block
updates occur after successful mapping, so a prefix can also be absent from the
metadata that Finalize later walks. This is an ordering finding, not a witnessed
native allocator reuse or a claim that all exception paths reach a live guest.

KScopedPageTableUpdater only calls FinalizeUpdate to consume a page-update list;
it does not unmap this prefix. MapPageGroupImpl is a different, single-block
Operate loop. Its ON_RESULT_FAILURE reads the tracked Result, initially success;
an exception in the R_TRY expression does not assign a failure Result. That guard
is therefore not an exception rollback protocol. Even adding an unmap call to
an exception handler would be insufficient when the Memory entry is closed.

The single-block Map path also performs MemoryManager::Open after the Memory
wrapper returns. Draining that wrapper alone does not join the outer reference
publication or the later memory-block update.

Required repair boundary: retain the operation's references, PTE/table owner and
metadata disposition across all calls and ownership publication. A partial or
uncertain result must retain accessible backing, with explicit recovery or
quarantine ownership. Merely cancelling Close on every exception would leak
unmapped suffix references and lose the recovery record; merely reopening the
entry during cleanup would defeat closure.

## KProcess::Finalize: closure precedes its own TLS unmap

Finalize seals controlled CPU writes and mappings before DeleteThreadLocalRegion.
Deleting the final TLS allocation removes its KThreadLocalPage from the process
tree, then calls KThreadLocalPage::Finalize. That calls UnmapPages before freeing
KPageBuffer. For an enrolled Memory, the nested unmap refuses because retirement
is terminal. The exception interrupts process finalization after TLS bookkeeping
has already changed. The current Unmap repair retains backing references on this
refusal, but cannot complete TLS teardown or restore the tree.

DeleteThreadLocalRegion also ignores the Result returned by the TLS page's
Finalize before freeing its descriptor. KProcess::Finalize ignores the Result
from DeleteThreadLocalRegion. Those Result paths and exception paths must not be
confused: an exception stops unwinding before the following descriptor Free,
whereas a failed Result is currently ignored. No new native TLS failure witness
is claimed here.

All current KProcess constructors still use untracked Memory, so controlled
closure is not activated by ordinary processes. This is a blocker to future
enrollment, not evidence of a newly activated normal-process regression. Moving
the seal after TLS cleanup would leave cleanup racing an open entry domain;
teardown needs a retained owner and a cleanup protocol after producer closure.

## KPageTableBase::Finalize: separate mutation and bookkeeping lifetimes

The callback bypasses Memory's wrapper and calls System DeviceMemory HostMemory
Unmap directly. It then builds a heap page group and closes those physical
references. Finally the table resets its Common::PageTable. Neither raw table
users nor another Memory sharing the table are joined by this per-Memory gate.
HostMemory has its own mutation domain; bypassing Memory does not bypass that
domain or guarantee that this cleanup can enter it after a freeze.

KMemoryBlockManager::Finalize erases the current block before invoking the
callback and frees its descriptor only after the callback returns. If the
callback throws, the erased descriptor is not freed and is missing on a retry;
the later blocks remain in the tree. Later resource cleanup and m_impl.reset
are skipped by unwinding. This is not an atomic or retry-safe finalization.
MakePageGroup's ignored result is another unresolved completeness boundary:
non-heap mappings legitimately have no heap references, while descriptor
allocation failure can leave a partial group. A blanket R_TRY is not a repair.

Do not move or suppress these exceptions as a standalone fix. A teardown state
must retain the current block, table and physical ownership until the mutation's
disposition is known, and define how a partially processed block is recovered.

## Reproduction and validation scope

`tools/physical_coherency_tests/run_cross_call_ownership_audit.py` compiles the
actual extracted group Operate, KScopedPageGroup, Memory mapping wrapper and
KMemoryBlockManager::Finalize bodies. It uses the actual controlled entry domain
and injected mapping, reference and tree collaborators. It deterministically
retires the entry between completed node calls, without recursive shared-lock
acquisition. It covers all three group operations, first-node refusal,
between-call closure, injected uncertain first-node failure, successful mapping
and an untracked control. Finalize has successful and throwing callback controls.

Three Linux (WSL Ubuntu) audit executions reproduced both defects, with normal,
refusal and untracked controls passing. Two counterfactual sensitivity controls
retain prefix references or delay record erasure; each must fail the corresponding
defect assertion. These counterfactuals are incomplete and are not proposed fixes.
Compiler failures, timeouts or unrelated assertion failures do not qualify.

Three successful audit executions mean **the OPEN defects reproduced**, not
fixed acceptance. The process/TLS path above is a source trace, not an extracted
full-process test. No allocator reuse, native process teardown, complete owner
census, rollback or guest coherency is certified. Evidence and source hashes are
saved under ignored `.diagnostics/m4-cross-call-audit/`.

This audit changes no runtime behavior. F2/F3 remain Open. Validation remains
Linux-focused; no Android build or reset consumption is requested.
