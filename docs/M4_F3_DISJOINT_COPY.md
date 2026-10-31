# M4/F3: explicit-source non-contiguous same-queue Copy

The final [mixed-root multi-hole acceptance](M4_F3_MIXED_HOLES_ACCEPTANCE.md)
closes the controlled Copy combination stage and sets the next stage to bounded
reconciliation planning. The disjoint partition contract below remains in force.

Baseline: `22438fd604` (Linux acceptance of `4bb3231040`). This is a bounded
extension of `RecordControlledReplicaCopies`, not a new content authority.

## Exact partition contract

Regions remain explicit, aligned, nonempty and canonically ordered, but need not
be adjacent. They must be non-overlapping and inside the declared window; the
last region ends at the window end. Every target byte outside the actual write
union, including internal holes, requires an ordered, exact complement partition.
The merge of writes and complement pieces covers the entire target exactly once.
Missing, duplicate, overlapping, Unknown or incorrectly projected pieces reject.
The existing immutable source and target roots, physical association, native pin
identity, mapping epoch and independently checked current qualification apply to
holes just as to outer complements. Receipt ancestry remains immutable.

The production change is confined to `ReplicaCopyProvenance::BuildBatch`.
The existing scheduler already qualifies every declared complement twice and
revokes each planned command destination before worker execution. No new buffer
selection, coordinator, content counter or submission mechanism is introduced.
Actual checked queue predecessor, barriers, completion and retained resources
remain mandatory. Cross queue stays Unknown. A successful query is not an
asynchronous admission lease.

## Failure boundary

Pre-recording refusal emits no new write or completion and does not revoke old
qualification. Once accepted, **all planned write intervals** lose earlier current
qualification, including a suffix that never gets recorded. Internal holes are
not part of that write union. A recording exception can leave a real prefix in
the command buffer; the resulting receipt stays Unknown even if that prefix
executes and its submission completes. There is no transaction rollback.

In this closed partial-recording case, independent old evidence for holes can
survive. A separate Fill into a hole revokes precisely its overlapping bytes;
the remaining hole bytes still need valid evidence. Submission failure or an
unknown execution outcome continues to poison the submission domain globally:
we do **not** claim that holes remain qualified after device-loss uncertainty.
No failed/unknown/unattempted submission manufactures successful completion.
Remap, retirement and later unknown producers continue to fail closed, while
historical root association remains unchanged.

## Deterministic and native acceptance design

The extracted-production-body fixture runs the previous 35 contiguous modes and
41 non-contiguous modes. New controls cover missing/Unknown/wrong-projection/
overlapping/stale hole partitions and a partial recording followed by a separately
flushed hole Fill. Independent per-byte value/root/offset labels check the entire
64-byte target, written prefix and unrecorded suffix. Simulated queue failures
include possible writes despite failure without advancing the timeline; these
are model evidence, not actual hardware device-loss evidence.

Mutation regression retains the prior controls and adds three faults: revoking
the whole bounding window, retaining the unrecorded suffix, and omitting internal
hole current qualification. Complement coverage and final window boundary
mutants have independent metadata probes; all mutants must fail by assertion,
never by a compiler error. Standalone tests add 512 deterministic disjoint mixed
target oracle prefixes over four immutable ancestry hops with multiple holes,
while retaining command/partition budget boundaries and prior oracles.

The real Linux Vulkan test runs both contiguous and disjoint variants using four
isolated staged replicas over physical aliases. Two source intervals [8,24) and
[32,48) leave [24,32) untouched. The first source crosses a nonidentity root
projection; outer complement [48,56) has a different root. The four-hop chain
batch -> single Copy -> batch -> batch imports both source and destination
receipts. All 64 root projections, wrong-projection rejection and complete
readback are compared with an independent oracle. Old hole evidence survives,
old copied-range evidence does not. A later hole Fill invalidates only [24,28),
retains [28,32), and leaves historical ancestry unchanged. A fifth hop rejects;
retained native storage is released only after trusted-idle retirement.

The existing Linux workflow already builds actual Scheduler/BufferCache objects,
runs all five CTest processes, repeats native provenance and extracted fixture
acceptance three times, and archives mutation/extraction/full test logs. No CI
scope reduction is needed. Android is skipped by default. Original fixed-required
F2/F3 negative witnesses remain mandatory and open. This controlled mechanism
neither selects ordinary BufferCache replicas nor publishes physical latest-writer
authority, and does not establish F2/F3 production correctness.

## Verification record

Source `d223b5ca33dce80283f0b44c6256c62394caedcc` passed
[Linux-only run 39111978882](https://github.com/Splaser/emulator/actions/runs/39111978882),
Linux job `117629864396`; Android job intentionally skipped. Acceptance includes:

- Actual Scheduler/BufferCache translation units, CLI/shader/test builds, all five
  CTest processes and all existing registration/coherency/backing/lifecycle suites.
- Native Vulkan provenance: 351 assertions, plus three independent passing runs;
  both contiguous and non-contiguous four-hop variants, complete readback, exact
  roots/projections, old hole qualification, later local cancellation and pin retirement.
- Extracted-production-body fixture: 35 contiguous and 41 non-contiguous modes,
  plus three independent repeats. All 70 mutation variants failed by assertion;
  all 71 fixture transcripts are retained (one positive plus 70 mutations).
- Standalone 512 disjoint mixed-target byte-oracle prefixes, existing content,
  eligibility, multi-hop and mixed-source/target oracles, and 32/64-piece bounds.
- Exact source file equals the accepted SHA; all 21 extracted input SHA-256 hashes
  match committed Git blobs. This checks the tested implementation, not just filenames.
- Original F3 full/partial aliases still report expected 99 / observed 10; its
  fixed-required run has two assertion failures and exit 42. F2 remains expected
  77 / observed 10 / backing 77. These are preserved defects, not acceptance of a fix.

Complete artifact files, generated sources/executables, extraction manifest,
CTest output and complete Linux job output are archived under
`.diagnostics/ci-39111978882/`. The independently checked index is
`verified-acceptance.json`. Local baseline red assertion
`bool(result)==!refused` is preserved in `m4-disjoint-red.log`; local focused
acceptance, all mutation results and three repeat logs are retained alongside
the CI archive. Initial focused checks exposed a fixture Fill that had not been
flushed, and a shadowed standalone-test local; both were fixed before the source
commit without relaxing assertions or compiler warnings. Final source CI passed
on its first run; no unchanged source validation was retriggered.

Changed source/test files:

- `src/video_core/renderer_vulkan/vk_replica_copy_provenance.h`: exact disjoint
  write/complement merge; existing submission/eligibility machinery is reused.
- `src/tests/core/controlled_cpu_scheduler.cpp`: real Vulkan contiguous/disjoint
  four-hop acceptance, oracle/readback and local hole invalidation.
- `tools/physical_coherency_tests/multi_region_fixture.cpp.in`: deterministic
  positive/negative and execution-uncertainty probes.
- `tools/physical_coherency_tests/run_sync_provenance_tests.py`: 70 expected
  assertion mutants, including hole cancellation and qualification faults.
- `tools/physical_coherency_tests/transfer_content_tests.cpp`: 512 independent
  disjoint mixed-target root-oracle prefixes.

This document defines the extension and acceptance; the prior multi-region
record and WIP handoff link it separately. No workflow change was needed because
the existing Linux scope already builds the affected real production units and
runs the complete suites. Actual hardware device-loss behavior, cross-queue
provenance, ordinary automatic selection and latest-writer authority remain
unproved. Submission-domain uncertainty may reject all holes conservatively.
Original F2/F3 and M3B restricted production eligibility remain Open/blocked.
