# M4/F3: bounded reconciliation candidate planner

Baseline `a68c5ac635`; the controlled Copy combination stage stays closed. This
slice adds a stateless candidate generator and a read-only Scheduler evidence
adapter. It reuses `RootSpan`, `ReplicaCopyProvenance::BuildBatch`, `HasRoot`,
Current Content Gate and existing admission. It adds no authority/coordinator,
producer discovery, new Copy form or ordinary BufferCache selection.

## Explicit input contract

A request names one live `GuestBackingEpoch`, one aligned contiguous physical
interval, a single immutable transfer anchor, a designated target and 2-3 staged
replicas including that target. All selected saved physical views must describe
that whole interval contiguously; logical offsets start at zero. The planner does not slice an already checked view or accept scattered physical
geometry. Such a checked view can itself be a subrange of a larger allocation
(the native acceptance uses 64-byte views of 4096-byte staged allocations).
Native allocations must be distinct. An offered source must also have a distinct
DAddr from the target, preserving existing controlled admission restrictions.

Each source supplies only explicitly **offered** `RootSpan` intervals from that
anchor or its existing copy receipt. The target supplies a structurally complete logical
historical partition; only retained pieces require historical/current proof. Offers are the caller's requested patch coverage, not a
registry of all physical producers or a claim that unlisted replicas are older.
The planner never guesses desired physical contents. Target bytes outside the
union of offered intervals must have independently known and current content.
The target is not entered into an election against the explicitly offered sources.

This distinction is necessary: two differently initialized staged aliases have
no implicit physical latest-writer relationship. A recipe to copy one explicitly
offered source over the designated target is a candidate recipe, not reconciliation
of global physical authority. Missing or unlisted producers are not certified as
excluded by this API; ordinary producer exclusion remains blocked elsewhere.

The total declared root-piece budget across all selected replicas is 64. Limits
remain 32 generated Copy commands, 64 source / complement output pieces, four
receipt hops, at most 64 physical pages, and the existing downstream admission
limits (eight anchor resources, 16 retained transfers). Selected replica count
is 2-3; immutable ancestry may retain other historical anchor resources. Budget
exhaustion refuses without widening eligibility or truncating a valid prefix.

## Decision and reason contract

`Result` carries `Candidate`, `Refused` or `Unknown`, a deterministic enum reason
and an owned existing immutable Copy metadata object only for Candidate.

| Condition at a spatial partition piece | Result |
| --- | --- |
| Exactly one offered, historically verified and current source | Generate that source's region |
| More than one verified source, different root/projection | Refused / ConflictingSources |
| More than one verified source, identical root/projection | Unknown / AmbiguousSources; no native-ID tie break |
| An offered source lacks current/submission/mapping evidence | Unknown; never silently drop it to create a unique winner |
| No offered source | Retain target only with exact historical and current complement evidence |
| Unknown retained content | Unknown / MissingContent |
| Malformed root/projection, partition, native alias, scope or budget | Refused with the relevant structural reason |
| No Copy writes required | Refused / NoWrites (not an execution receipt) |

Root equality is meaningful only within the same immutable anchor namespace and
with its exact offset projection. Neither root magnitude, resource revision,
submission tick nor completion order elects a writer. Refusal/Unknown results
contain no partial candidate. Validation is ordered and deterministic for the
same input snapshot; it is not an aggregate diagnosis of every invalid field.

The pure builder sorts bounded spatial endpoints, verifies each intersecting
offer, identifies a unique source or retains a qualified target piece, and merges
adjacent pieces only when source identity and exact projection allow it. It then
calls the existing `BuildBatch` to own and validate the resulting receipt recipe.
It neither records commands nor changes eligibility, mappings or prior ancestry.
Its evidence callback is a read-only test/adapter contract, not an independent
runtime authority system. Evidence exceptions return Unknown / EvidenceUnavailable.

## Scheduler evidence adapter and execution boundary

`PlanControlledReconciliation` is a GPU/cache-thread **query**. It checks selected
native pin geometry, retained storage/current eligibility, immutable mapping
identity, actual checked completion and recorded synchronization evidence. Prior
copy receipts must share the original anchor and an existing actual submission
spine. Structural depth locates a possible common predecessor only; completed
receipt checks validate actual synchronization/queue identities. Cross queue
remains Unknown. No render-pass transition, Flush, GPU recording or submission
occurs during planning.

Candidate owns metadata, not resource pins or an asynchronous admission lease.
It proves availability of an explicitly offered content recipe at observation
time, not that a future operation will be admitted or completed. Callers translate
the existing metadata regions/complements into `RecordControlledReplicaCopies`
with the original checked parent/receipt handles. Existing admission rechecks
mapping, native identity, source ranges and untouched complements. The worker
still verifies the actual immediately preceding accepted submission and emits
real barriers, Copy commands and checked submission. No new execution path is added.

A source or complement write, remap, retirement or synchronization invalidation
between query and admission refuses. An intervening queue submission can instead
cause worker rejection after admission: the immutable Candidate remains a
historical recipe while the actual operation is Unknown. All possibly written
target intervals have already lost old qualification; independently proven holes
can survive a closed partial recording. Submission-domain uncertainty still
refuses all current qualification, including untouched holes. No rollback,
completion or physical authority is fabricated by the planner.

## Tests and evidence boundaries

Pure C++ logic tests use 128 independent per-byte source/target label and value
oracles. They include mixed roots/projections, complete complements, conflicts,
equivalent-source ambiguity, stale/Unknown evidence, backing/mapping/native
mismatch, invalid geometry/alignment, exact 2-3 replicas, valid 32 / invalid 33
commands, 64 / 65 total declarations, owned output history and four-hop refusal.
The callback's synthetic evidence is not actual Vulkan synchronization evidence.
Twelve pure logic mutations must fail by assertion, including weakened evidence,
conflict/ambiguity classification, namespace backing/projection, budgets and hops.

The extracted-production-body fixture has 24 query/admission modes: conflicts,
ambiguity, stale sources/complements, remap (including a PTE mismatch without an
epoch advance), retirement, missing synchronization, wrong queue, pending work,
unknown producer and wrong receipts. It checks zero query commands/submissions,
then invalidates sources, complements, mapping, retirement or sync after a valid
query to prove there is no admission lease. Real extracted recording/submission
bodies simulate one/two-command prefixes, failure with/without possible effects,
intervening queue work and missing output synchronization. All old target bytes
are checked individually. Four new adapter mutants join the prior 74 assertion
mutants; all 78 are required. These injections are model evidence, not hardware
failure recovery or ordinary Draw/Dispatch validation.

The existing Linux Vulkan mixed-hole layout now obtains its first and fourth
three-region Copies from the planner and passes them through existing admission.
The fourth query consumes a completed legacy source receipt and a completed batch
target receipt on the actual shared queue spine. A separate extracted-body query
checks an existing target receipt without recording another Copy. The same accepted
Copy combination, four-hop receipts, independent root/value oracle and actual
readback remain in use; no new Copy expression has been introduced. The workflow
adds pure planner tests/mutations and their full artifact directory, while keeping
actual Scheduler/BufferCache compilation, all unit tests, three fixture/native
repeats, original F2/F3 negative witnesses and existing coherency/lifecycle suites.
Android stays skipped. No ordinary BufferCache auto-selection, cross queue,
latest-writer authority or new Draw/Dispatch producer is enabled. F2/F3 stay Open.

## Verification record

Implementation `374977121d`, receipt-input/mutant fix `6a652ed561` and naming/
fail-fast fix `4b856d0921522233bd361e2505bae5ba779b83c1` passed
[Linux-only run 39123951599](https://github.com/Splaser/emulator/actions/runs/39123951599),
Linux job `117665075606`. Android was intentionally skipped. Verified acceptance:

- Real Scheduler/BufferCache/native test translation units compile before the
  slow mutation loop. CLI/shader/test builds, all five CTest processes and the
  full registration/coherency/backing/lifecycle suites pass.
- Native Vulkan: **398 assertions**, plus three independent repeats. The existing
  mixed-hole layout consumes planner candidates on the first and fourth actual
  hops; the latter verifies real legacy source and batch target receipt inputs.
  Existing admission, barriers, checked completion, full byte readback and all
  original root/current/retirement oracle assertions remain mandatory.
- Pure planner: 128 independent byte oracles, boundary/reason/ancestry tests and
  three repeats. All **12 pure mutations** fail by assertion. These test injected
  evidence semantics, not actual hardware synchronization or failure.
- Extracted production bodies: all **24 planner query/admission modes**, positive
  target-receipt reuse without new commands, prior fixture modes and three full
  repeats pass. All **78 mutations** fail by assertion, including four new adapter
  faults. Query command counts remain unchanged. Failure/prefix/uncertain-effect
  cases are simulated dispatch, strictly separate from native Copy/readback.
- Exact source file equals the accepted SHA. All **24 extracted-body input hashes**
  and **6 pure-planner input hashes** match committed Git blobs.
- Original F3 fixed-required aliases remain expected 99 / observed 10, two failed
  assertions, exit 42. F2 remains expected 77 / observed 10 / backing 77. Neither
  production defect is closed by this controlled candidate acceptance.

Complete Linux job output, all CI artifacts (including generated mutation source/
executables), 78 extracted-body and 12 pure assertion transcripts, positive/repeat
logs, extraction manifests and CTest output are archived under
`.diagnostics/ci-39123951599/`. Independent checks are in `verified-acceptance.json`
and its retained verification script. The local API-stub red assertion is retained
as `reconciliation-baseline-red.txt`; it documents a missing new generator, not a
prior production F3 defect. Local pure/focused/full-mutation logs and repeats are
also retained; `local-evidence.json` identifies pre-naming-fix versus final-source
scope. Final-source Linux evidence is the acceptance record.

Changed implementation and validation files:

- `src/video_core/renderer_vulkan/vk_reconciliation_planner.h`: stateless bounded
  spatial candidate builder, existing immutable Copy metadata output and reasons.
- `src/video_core/renderer_vulkan/vk_scheduler.h`: typed explicit replica request
  and actual pin/mapping/current/submission/synchronization evidence query.
- `src/tests/core/controlled_cpu_scheduler.cpp`: existing first/fourth Copy hops
  consume generated candidates through original admission and native readback.
- `tools/physical_coherency_tests/reconciliation_planner_tests.cpp` and
  `run_reconciliation_planner_tests.py`: pure oracle, reasons/budgets and 12 mutants.
- `tools/physical_coherency_tests/reconciliation_planner_fixture.cpp.in`: query,
  receipt-input and failure/no-lease tests with extracted production methods.
- `first_registration_pipeline.cpp.in`, `run_registration_pipeline_tests.py` and
  `run_sync_provenance_tests.py` in the same directory: extract the real adapter,
  reuse the fixture and add four assertion mutants. The explicit `--positive-only`
  option is an iterative check, not acceptance; CI always runs the full default.
- `.github/workflows/pr375-pr380-integration.yml`: pure planner execution/artifacts,
  plus real compilation before slow mutants, preserving complete Linux scope.
- This document and WIP handoff describe the contract and evidence boundaries.

Residual boundaries: Candidate uniqueness is among explicitly offered source
intervals only, not an enumeration/exclusion of all physical producers. A caller
must retain original request/receipt context and re-enter existing admission;
Candidate metadata is not a resource or execution lease. Whole checked views,
one anchor and 2-3 selected replicas remain the minimal scope. General view slicing,
cross queue, ordinary automatic selection, physical latest-writer authority,
hardware device-loss recovery and ordinary F2/F3 correctness are not proved here.
No further Copy expression combinations were added; that acceptance stage remains
closed. This evidence-only documentation does not retrigger unchanged source CI.


The first Linux run
[39122121531](https://github.com/Splaser/emulator/actions/runs/39122121531)
rejected the `without-input-budget` mutant at compile time: GCC diagnosed `pieces`
as set-but-unused after the guard was removed. That result is **not** accepted as
a mutation assertion. The mutant now reads the counter explicitly while bypassing
the condition; `-Werror` remains intact. The failed job/artifacts are retained in
`.diagnostics/ci-39122121531/`. Receipt-input acceptance was also strengthened
before the corrected source validation; production planner semantics are unchanged.


[Run 39122571156](https://github.com/Splaser/emulator/actions/runs/39122571156)
completed all 12 pure and 78 extracted-body assertion mutants, then production
compilation rejected adapter-local `State` and `state` names shadowing Scheduler
members. They were renamed to `DecisionState` / `decision` without semantic
changes or warning suppression. The extracted fixture does not contain the whole
private Scheduler layout, so this error needed real production translation-unit
compilation. The workflow now compiles those units **before** the slow mutation
loop; pure tests still precede any actual GPU execution. Complete failed evidence
is retained in `.diagnostics/ci-39122571156/`.
