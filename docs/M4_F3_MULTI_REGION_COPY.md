# M4/F3: explicit-source same-queue multi-region Copy

This is the contiguous-window acceptance record. The later
[non-contiguous extension](M4_F3_DISJOINT_COPY.md) supersedes its internal-hole
restriction while preserving this baseline evidence.

Baseline: `70fc6ae474`. This extends the existing controlled receipt/provenance
mechanism; it does not select replicas or establish physical content authority.

## Contract and bounds

`RecordControlledReplicaCopies` takes one original anchor, one destination slot,
one target window and explicitly ordered source regions. Each region names an
anchor source slot, exact source/destination offsets, length, optional source
receipt and an exact root partition. Source and target receipts must belong to
that anchor, retain the saved native identity/pin geometry/physical binding, and
lie on the same retained actual submission spine. Structural depth selects a
candidate predecessor; ancestry alone never proves execution order. The worker
still requires the actual immediately preceding accepted checked submission on
the owned graphics queue.

Regions must be aligned, nonempty, in canonical destination order, adjacent and
cover the declared window exactly once. Internal holes, overlap, unsorted or
out-of-bounds regions reject. Self-copy into the destination allocation rejects.
This slice does not admit disjoint windows with internal retained holes. Source
partitions must cover each exact source interval, including root boundaries and
nonidentity original root projections. Unknown, stale, missing or overlapping
pieces cannot qualify. Every untouched complement byte outside the window needs
known historical provenance and independent current qualification. Physical
source/destination slices must match captured backing/epoch/segments, through
distinct DAddr/native allocations. Mapping-query exceptions reject.

The immutable `ReplicaCopyProvenance` owns all region/root declarations and
separate source/destination ancestry. Each output byte projects through its
unique source region or old destination complement. The original root IDs remain
anchor command IDs. Later source writes cannot rebase captured downstream
history; later target writes cancel only intersecting current ranges.

Bounds remain explicit: four actual submission hops, 16 retained transfers,
eight anchor resources, 32 commands per batch, 64 aggregate source root pieces,
and 64 complement pieces. One batch consumes one submission hop, not one hop per
region. All anchor resources are retained and checked; an unused resource's
mapping/lifetime cancellation may conservatively reject more than necessary.
Partition/eligibility capacity exhaustion never widens qualification.

## Failure qualification

| Event | New receipt | Earlier target qualification |
| --- | --- | --- |
| Rejected admission/allocation before recording | No receipt/completion | No new cancellation; existing unrelated writes still count |
| Accepted recording, before queue attempt | Pending or Unknown; never Complete | Entire planned write union conservatively revoked |
| Partial recording, successful submission of a possible prefix | Unknown even after timeline progress | Entire planned union revoked, including the unrecorded suffix; independently proven complement may survive |
| Submit failure, exception or unknown execution outcome | Unknown, no invented successful completion | Planned union revoked; existing submission-domain uncertainty conservatively refuses all current qualification |
| Successful checked completion with complete synchronization evidence | Historical provenance available; current gate still required | Covered bytes cannot reuse old qualification |
| Later write/remap/retirement/unknown producer | Historical graph unchanged; current gate may refuse | Existing byte-range and storage/mapping exclusions apply |

The existing `RecordTransferBatchImpl` revokes planned destinations before any
worker command may run. Each region emits a real `vkCmdCopyBuffer`, entry/internal
barriers and one final release, under one actual checked submission. Completion
checks the dependency identities, actual queue/predecessor, emission, timeline,
internal command dependencies and release evidence for every command. Recording
failure does not discard possibly recorded GPU commands or pretend to roll back
their writes. Pins remain retained until host work has drained, a queue attempt
is accounted for and trusted device idle permits retirement. Query success is
not an asynchronous admission lease.

## Acceptance and evidence boundaries

The baseline red witness `.diagnostics/m4-multi-red.log` shows ordinary batch Copy
cannot import known roots from previously initialized external replicas. The
new extracted-production-body fixture adds 35 deterministic modes covering
adjacent regions, exact roots/projections, holes/overlap/bounds/Unknown/stale
sources and complements, budgets, physical mismatch, owned declarations,
pending work, partial recording, host failure, submit failure, unknown effects,
missing synchronization/queue linkage, remap, retirement and the four-hop bound.

The simulated queue can apply recorded writes despite returning a submission
error, without advancing the completion timeline. Another fault emits a first
copy then throws, allowing that prefix to execute on successful submission.
Independent per-byte value/root/offset labels verify these outcomes without
assuming transaction rollback. Old planned coverage must be unqualified even
where no suffix bytes were copied; old untouched complement must remain
qualified in the closed partial-recording case. These injections simulate device
execution and do not prove real device-loss recovery.

Six new mutations remove current qualification, planned revocation, suffix
recording, unique destination coverage, final window coverage or original root
projection. All must fail by assertion. Standalone tests exercise the valid
32-command/64-source-piece/64-complement boundary and refusal at 33/65/65.

The native Linux Vulkan case uses four private, unregistered staged allocations
over aliases and an independent byte oracle. Anchor copies create a nonidentity
source projection and mixed target complement. Its four-hop sequence composes
batch -> single Copy -> batch -> batch, importing explicitly named source and
target receipts. Complete target readback and all 64 roots/offsets are compared
with the oracle; wrong projections and a fifth hop reject. Subsequent source and
target writes, retained target pins and trusted-idle retirement are checked.
No intermediate readback submission is inserted into the controlled spine.

Implementation commit `bb6b845784` and native-test naming correction
`4bb323104025682ea2bd06956ca111788007d202` passed
[Linux-only workflow 39104473255](https://github.com/Splaser/emulator/actions/runs/39104473255)
on 2026-10-18. All five CTest processes passed; native Vulkan provenance passed
315 assertions in CTest plus three independent repeats. All 35 deterministic
multi-region modes passed, including three additional fixture repeats. All 67
mutations failed by assertion; the positive/mutation log set contains 68 files.
All 21 extracted input SHA-256 hashes match the committed source blobs. The
standalone command/partition boundaries and existing byte-oracle suites passed.
CLI, shader tool, production unit tests and the full registration/coherency/
backing/lifecycle regressions also passed.

Because Scheduler production code/header changed, the early native translation-
unit check explicitly includes real `vk_scheduler.cpp` and `vk_buffer_cache.cpp`.
The first run, [39103686983](https://github.com/Splaser/emulator/actions/runs/39103686983),
passed its deterministic/mutation controls but stopped at a conflicting native
test variable declaration (`batch_first`, `batch_second`, line 1704). Commit
`4bb3231040` renamed the added test locals without changing production semantics
or weakening compiler checks. Its new source run above completed all validation.

Complete job logs and downloaded artifacts are retained in
`.diagnostics/ci-39104473255/` and `.diagnostics/ci-39103686983/`. The former contains
the exact-source/extraction manifests, all mutation logs, native and fixture
repeat logs, original negative controls and complete Linux job output. The
original F3 fixed-required witness still fails both aliases (expected 99,
observed 10, exit 42); F2 still reports expected 77, observed 10, backing 77.
Android was intentionally skipped; no Android-specific paths changed. The
following documentation-only evidence commit does not rerun unchanged source CI.

Cross-queue remains Unknown. There is no ordinary BufferCache automatic source
selection, global coordinator, physical latest-writer publication, Draw/Dispatch
scope expansion, or claim that F2/F3 are fixed.

## Changed files and remaining limitations

| Files | Change |
| --- | --- |
| `src/video_core/renderer_vulkan/vk_replica_copy_provenance.h` | Owned bounded multi-source partitions and immutable per-byte projection |
| `src/video_core/renderer_vulkan/vk_scheduler.h`, `vk_scheduler.cpp` | Explicit batch admission, retained dependencies, native target index, command coupling and completed synchronization checks |
| `src/tests/core/controlled_cpu_scheduler.cpp` | Real Vulkan batch/single interoperation, four hops, byte oracle, current cancellation and retained lifetime |
| `tools/physical_coherency_tests/multi_region_fixture.cpp.in` | Deterministic multi-region admission, failure and uncertainty controls |
| `tools/physical_coherency_tests/first_registration_pipeline.cpp.in` | Actual-body scaffold and non-rollback simulated submit fault |
| `tools/physical_coherency_tests/run_sync_provenance_tests.py`, `run_registration_pipeline_tests.py` | Extract real batch admission/types, run controls, add six mutations |
| `tools/physical_coherency_tests/transfer_content_tests.cpp` | Command/partition budget boundaries |
| `.github/workflows/pr375-pr380-integration.yml` | Early real Scheduler/BufferCache compilation and three fixture repeats |
| `docs/M4_F3_MULTI_REGION_COPY.md`, `docs/M4_F3_WIP_HANDOFF.md` | Contract, failure matrix and validation handoff |

This is one contiguous declared target window with explicitly named sources on
one anchor/spine. Current qualification is scoped to those staged bytes; it does
not prove which replica holds the newest physical data. Cancellation can be
conservative across the whole retained resource/submission domain. Host failures
without an accounted queue attempt remain quarantined; this slice adds no retry,
submission-domain reset or device-loss recovery. Android and real target-device
failure behavior remain unproved. Fault-injection results must not be read as
real GPU rollback or recovery evidence.
The native case proves two explicit source regions and their four-hop
composition; maximum 32-command/64-piece budgets are unit-tested, not a claim of
32-region execution on every Vulkan driver.
