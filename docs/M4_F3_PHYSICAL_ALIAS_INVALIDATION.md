# M4-F3 controlled physical alias invalidation fan-out

## Scope

This stage implements **revocation**, using the existing retained Scheduler
transfer/resource list and Current Content Gates. It does not implement a producer
exclusion provider, connect ProducerCoverageContract::Check to admission, rank
writers or reconcile/copy replicas. Planner and Copy expression remain frozen.

The production `Scheduler::InvalidatePhysicalWrite(writer, offset, size)` takes a
saved immutable PhysicalWriteIdentity and logical write interval. On the GPU/cache
owner, before effects, it visits all retained current-eligible transfer resources,
matches GuestBackingEpoch identity, intersects every saved physical segment, and
projects each physical intersection back into the affected resource's logical
Current Content Gate. This includes multiple DAddr/native allocations, partial
views with different native offsets, reordered pages and multiple logical aliases
of the same physical page. It neither re-resolves current DAddr to reassign the
event nor changes immutable history.

Writer bounds/lifetime/generation uncertainty cancels all retained current
qualification and returns false. Invalid target physical/native membership,
retirement or remap cancels that target transfer conservatively. A positively
different live backing is disjoint; intersecting targets preserve independently
qualified disjoint byte ranges. There is no new registry or unbounded lookup:
the existing 16 retained transfers, 8 resource slots and 64 physical segments bound
the scan. TransferEligibility retains its existing fragmentation budget and
fail-closed behavior.

## Actual controlled write integration and lifetime

`RecordControlledPhysicalFill` is an explicit isolated endpoint, **not ordinary
production admission**. It requires a retained, completed transfer resource,
usable unexposed native allocation, exact captured pin binding, current physical
mapping and a valid aligned Fill range. It invokes physical fan-out before any
context/command recording, then reuses **RecordTransferBatch** for the real Fill.
That new transfer has its own pin retention, worker-pending state, checked
submission, completion and retirement/quarantine rules. The earlier receipt alone
would not protect a newly queued command from premature retirement.

The new transfer describes the actual controlled Fill using the existing exact
content metadata. Revocation alone does not arm or publish a new root, select a
physical writer or copy any bytes into peers. The Fill's own metadata is not
physical latest-writer authority; its untouched bytes have no imported roots.
Old receipts keep their exact historical roots, while overlapping old current
qualification disappears in every tracked representation.

The endpoint does **not** retain producer exclusion. The isolated caller must
already control CPU/NCE/host/GPU/mapping access through possible effects and
completion/quarantine. Mapping/current checks are rejection predicates, not an
asynchronous guard. CPU/NCE must not call the GPU-thread-only invalidator directly
from a signal handler. A real future provider must marshal/prevent writes before
effects while holding owned physical/alias exclusion; no new ordinary store hook
or fictitious certificate is added here.

For that future provider, merely setting eligibility or submission state Unknown
is **not** safe quarantine and does not justify releasing exclusion. Quarantine
must retain storage/lifetime ownership and enforce inaccessibility to conflicting
CPU/NCE/GPU/raw-pointer users and frame reuse until possible effects are resolved
or the affected mappings/storage are demonstrably isolated. This stage supplies
neither that enforcement nor permission to release a guard after a failed query.

## Boundaries and evidence

This fan-out reaches **all existing retained Current Content Gates**, not every
possible buffer in the emulator. Untracked allocations have no such gate to revoke
here. Ordinary native Fill/Copy hooks retain their earlier native-allocation
invalidation; their physical producer integration is still required. Controlled
replica Copy admission/recording is unchanged. Neither the new primitive nor a
quiet callback sequence proves all producers have enrolled.

Accordingly, production closure remains Unknown and restricted eligibility remains
blocked. The [coverage contract](M4_F3_PRODUCER_COVERAGE_CONTRACT.md), existing
[producer inventory](M3B_F2_PRODUCER_INVENTORY.md) and
[restricted blockers](M3B_F2_RESTRICTED_ELIGIBILITY.md) continue to apply. Cross
queue remains Unknown, no ordinary automatic source selection is enabled, and
F2/F3 remain Open.

## Deterministic controls

The extracted-production-body fixture has 14 modes: three native replicas under
one backing plus an independent backing, nonzero native/physical projections,
clipping/disjointness, invalid bounds, PTE remap, epoch remap, source retirement,
submission-domain uncertainty, failed submission, uncertain partial effects and
retirement while the new Fill worker is pending. Its independent byte oracle
checks every resource byte, and an enqueue hook asserts revocation **before the
first command is queued**. Peers' actual simulated bytes stay unchanged; the
writer changes only its Fill interval. Failure does not manufacture a successful
timeline or restore old qualification.

A further 24,576 byte checks cover a two-page writer, a page-reordered target and
a duplicate-page target. The oracle independently translates each target byte;
it does not reuse the intersection/projection algorithm. Nine assertion mutants
remove fan-out, move it after recording, restrict it to the writer's native
allocation, omit backing identity, break local/page projection, revoke whole
views, widen the writer footprint or accept a stale writer. Compilation errors
do not count as a killed mutant. Simulated dispatch/failure injection is labelled
separately from actual Vulkan evidence.

The native `[f3-provenance]` case now has three distinct VkBuffer allocations under
the same physical interval, with different byte values. A real controlled Fill to
one replica revokes old `[16,32)` qualification in all three before execution,
preserves both complements, and uses a 192-byte qualification oracle. Actual
readback confirms the written replica's expected interval and unchanged peer
bytes; historical roots remain intact. The existing four-hop Copy/Planner native
acceptance and all original negative witnesses remain in the workflow.

## Linux acceptance

Accepted source `fecb02c9a98c6441bf58356a06035b29135f5c0e` passed
[run 39134590517](https://github.com/Splaser/emulator/actions/runs/39134590517),
Linux job `117696419145` (31m19s). Android job `117696420232` was skipped.

- All nine new fan-out assertion mutants and the existing 78 extracted-body
  mutants pass their expected-failure checks (87 total). The existing 12 pure
  Planner and 20 coverage-contract mutants also remain assertion failures.
- The extracted fixture's 14 modes and 24,576 page-projection byte checks pass,
  including all three complete fixture repeats. Full local mutation acceptance
  and three independent local fixture repeats also pass.
- Actual native Vulkan passes **407 assertions**, with three repeats. Every
  repeat includes the new three-allocation/192-byte fan-out oracle and actual
  Fill/readback, unchanged peer bytes/history, plus all prior four-hop
  Planner/Copy layouts. The controlled harness does not prove CPU/NCE exclusion.
- Production Scheduler/BufferCache/native translation units, full CLI/shader/test
  build, all five CTest processes and complete coherency/lifecycle/backing
  regressions pass. Existing workflow coverage was sufficient; no Android build
  or extra workflow was added.
- All 25 extracted-body, 6 Planner and 4 coverage input SHA256 values match exact
  Git blobs at the accepted source (35 inputs).
- F2 remains expected 77 / observed 10 / backing 77. F3 full/partial aliases remain
  expected 99 / observed 10; fixed-required fails two assertions and exits 42.
  F2/F3 Open, restricted eligibility blocked, production closure Unknown.

Complete artifacts, generated source/executables, mutation/repeat transcripts,
manifests, CTest and full job output are archived in
`.diagnostics/ci-39134590517/`. The retained verification script/index independently
checks source hashes, all 87+12+20 assertion mutants, repeat markers, CTest and
original negative witnesses. Local final mutation/repeat logs are also archived.

Changed files: `src/video_core/renderer_vulkan/vk_scheduler.h` and `.cpp`;
`src/tests/core/controlled_cpu_scheduler.cpp`;
`tools/physical_coherency_tests/first_registration_pipeline.cpp.in`,
`run_registration_pipeline_tests.py`, `run_sync_provenance_tests.py` and the new
`physical_alias_invalidation_fixture.cpp.in`; this document and
`docs/M4_F3_WIP_HANDOFF.md`. Planner, Copy receipts/expression, ordinary write
entry points and the coverage verifier remain unchanged. Acceptance-only
documentation does not rerun unchanged source CI.
