# M4-F3 Producer Coverage / Exclusion Contract

## Scope and result

Planner and Copy expression are frozen at `c0fb4c4f68`. This stage adds a bounded,
Vulkan-neutral **contract verifier and pre-effect invalidation policy**, not a
production exclusion provider. No existing Scheduler, BufferCache, CPU or NCE
execution path is changed. No source election, physical authority, automatic
replica reconciliation or new content history/coordinator is introduced.

`ProducerCoverageContract::Check` returns `Eligible`, `Refused` or `Unknown`, with
a deterministic reason and offending producer where applicable. `Eligible`
means the supplied closure/admission assertions satisfy this contract **at this
synchronous check**. It is neither a lease nor evidence that the assertions are
true in the emulator. There is currently **no production provider** proving the
required exclusion, so production closure remains **Unknown**. A caller cannot
turn a Planner Candidate into production permission by constructing these values.

The first domain permits only exact controlled Transfer producers. Shader, CPU,
NCE and External entry paths must be independently excluded and drained. External
is the mandatory catch-all for unclassified DMA, mapped host writes, raw pointers,
texture/query/cache utility operations and third-party/driver writes. It is not
an assertion that an enumerated list of five labels exhaustively hooks the code.
Managed non-Transfer paths remain Unknown even with a notification claim.

## Evidence boundary and enforcement obligations

One domain contains one existing immutable `PhysicalWriteIdentity`, its original
native representation and whole contiguous physical interval under one retained
GuestBackingEpoch and mapping epoch. Geometry, owner liveness and captured
mapping generation are checked using the existing physical identity. There is no
new interval partitioner, identity registry or version comparison.

Each producer must supply exactly one scope-bound evidence entry: Managed,
Excluded or Unknown. Unknown is the default. Missing/duplicate producers and
unsupported values have deterministic results. For **every** entry a provider
must prove all possible entry paths participate, an exclusion/admission guard is
held, and prior writers are drained. Managed Transfer additionally requires
pre-effect invalidation. Actual-entry versus notified-entry mismatch rejects a
known gap; equality is deliberately insufficient without all-entry enforcement.

These booleans are an explicit provider trust boundary, not self-authenticating
capabilities. The verifier does not install hooks, pause guest cores, intercept
signals, block native queue access, lock raw host pointers or manufacture a
guard. A future integration must provide reviewed, owned enforcement rather than
copying successful test values into production. A list of observed writes, a
resource revision, an idle flag, a cache mutex alone, or zero active observations
does not establish exclusion. An omitted bypass that increments neither counter
cannot be detected by counter comparison; it is why the enforcement requirement
is mandatory and the current emulator has no positive closure claim.

The provider must retain real exclusion from admission through all possible GPU
effects and completion/quarantine, including exceptions and deferred workers.
Rechecking before recording alone does not close an asynchronous CPU/NCE race.
The verifier takes no locks and keeps no guard alive. Provider failures throw to
Unknown. No code may use a past `Eligible` result after guard release.

## Unified invalidation policy

`BeforeWrite` is a pure physical policy. A future adapter must translate its
result into the **existing** per-allocation Current Content Gate for every
affected representation before allowing effects. It does not apply that mutation
itself; the tests use an independent byte array to check the required effect.

- Exact known Transfer: revoke the intersecting physical bytes before recording.
  Independently proven disjoint bytes/holes may retain local current evidence.
- Exact unexpected CPU/NCE/Shader/External write: revoke its intersection and
  mark the closed domain Unknown; this does not authorize that producer.
- Missing physical identity, unknown footprint, bypassed notification, invalid
  footprint, remap or retirement: conservatively revoke the entire domain and
  reject closure. Unknown shader access cannot describe exact written bytes.
- Submission-domain uncertainty globally rejects the domain, including when an
  individual write appears to address another backing. A positively different
  backing is disjoint only when submission-domain evidence is still known.
- Every planned write interval must be invalidated **before the first command**.
  Partial recording, partial effects, failed submission and uncertain completion
  cannot restore old eligibility. No GPU transaction rollback is assumed.
- Independent holes may remain locally valid, but local validity cannot override
  domain Unknown. Historical RootSpan/receipt ancestry is never rewritten.

This phase defines these obligations and tests them; it does not claim existing
native-handle invalidation already reaches every physical alias. That fan-out is
an outstanding production integration requirement.

## Existing paths audited and remaining gaps

| Path | Existing evidence | Why closure is not yet proved |
| --- | --- | --- |
| `vk_scheduler.cpp` RecordBufferFill/Copy and controlled batch | pre-effect native-range revocation, checked submission, actual queue spine and barriers | native buffer identity revocation is not global physical alias fan-out or producer exclusion |
| `vk_buffer_cache.cpp` runtime Copy/Clear | delegates ordinary operations and some conversion/initialization commands | direct utility commands and all destinations must be classified/pinned or proven disjoint, not inferred safe by function name |
| `vk_query_cache.cpp`, turbo/utility buffers | direct Fill/Copy command sites | often private buffers, but isolation from the selected physical view needs ownership evidence |
| `vk_rasterizer.cpp` storage observation | actual command recording, immutable physical association, sticky incomplete coverage | access footprints are not exact content writes; observer sees a subset, not exclusion of all producers |
| `buffer_cache.h` MarkWrittenBuffer | bind-time write intent, gather notification | no execution or completion proof; skipped Draw cannot publish content |
| `core/memory.cpp` scalar/Block/Copy/Zero and Unsafe/raw pointer paths | existing prepared/registered subset, historical rasterizer invalidation | scalar registration is not all CPU writers, raw host stores may bypass notifications |
| `core/arm/nce/arm_nce.cpp` data abort / InvalidateNCE | historical fault/dirty path | no all-core/native-store exclusion; ordinary callbacks/cache locks are not signal-safe exclusion |
| Common PhysicalPageGates / PhysicalRangeAdmission | prepared-page drainage/publication and cooperating request ordering | only participating producers are governed; cannot prove absence of bypassing CPU/NCE/GPU writers |
| mapping/backing lifetime and resource pins | immutable owners, captured epochs, retirement checks | retain identities/storage, not a lock excluding content writes |

This is a boundary audit of named paths, not a complete write-site census. Before
restricted production F3 admission, an implementation must enumerate every alias
of the selected physical bytes, prove utility isolation, and enforce participation
or exclusion of every path that can reach them. An unclassified path invalidates
closure even if no write was observed in a particular test run.

The broader existing [producer inventory](M3B_F2_PRODUCER_INVENTORY.md) and
[restricted eligibility blockers](M3B_F2_RESTRICTED_ELIGIBILITY.md) remain in
force. In particular, the latter's prohibition on caller-supplied exclusion
booleans or fixture locks as production certificates is unchanged. This value
verifier is a schema/policy test facility, not an issuer of such certificates.

## Admission and ordering

Admission is the conjunction of closure and the original Copy qualification:
original pinned allocation/native identity; current physical mapping; current
content RootSpans and complements; bounded immutable receipts; checked successful
completion; actual same-queue dependency; known submission domain; original Copy
admission and its worker-side predecessor check. The contract accepts assertions
from that original admission, not substitute synthetic ticks or new root order.
PhysicalWriteCoverage must be sealed **and Complete**; overflow/capture failure
is sticky, so a valid prefix cannot satisfy full coverage.

Only existing exact transfer roots and actual same-queue synchronization can be
used. Missing barriers, stage/access mismatch, missing predecessor, pending or
unknown completion, and cross-queue evidence remain Unknown. Historical ancestry
is not a synchronization edge. No root ID, revision, tick, completion order or
counter magnitude ranks producers or establishes physical latest writer.

## Deterministic verification

`run_producer_coverage_tests.py` builds the real header under C++23 with
`-Wall -Wextra -Wshadow -Werror -pthread`. It first runs a weakened missing-producer
guard as a red negative, then the complete contract and three repeats. This red
is a deliberately weakened contract, **not a newly reproduced production bug**.

The fixture injects evidence and admission results. It covers omitted/duplicated
producers; mismatched scope; unguarded/uninstrumented entry; known notification
bypass; missing prewrite revocation; active CPU/NCE; unknown Shader; sticky
observation overflow; missing synchronization/checked completion; unknown
submission; stale admission; native pin failure; remap; retirement; exception;
and release-after-query. A real two-thread mutex demonstrates exclusion only in
a **toy provider**, not a real guest CPU or NCE execution/signal handler.

400 independent per-byte oracle cases span all five producer classes and clipped,
overlapping/disjoint writes. Additional cases check unknown footprints, failure
union with internal holes, lifecycle loss and immutable historical data. These
are contract policy simulations, not extracted production GPU fault injection.

Twenty targeted assertion mutants remove membership, exclusion, drainage,
notification, shader restriction, coverage, sync, current/native/mapping gates or
physical overlap behavior. Compilation errors do not count as killed mutants.
Both redundant mapping-generation checks are removed together for that mutant;
removing only the first safely survives due to admission revalidation. Initial
local iterations exposed this redundant defense and a warning in the overlap
mutant; the latter explicitly consumes the unused bound without weakening Werror.

The existing Linux workflow runs this suite before native execution and retains
generated sources, transcripts and four input SHA256 hashes. It continues all
previous Planner/Copy mutations, native readback, CTest and original F2/F3 negative
witnesses. Android is not requested for this Vulkan-neutral, unconnected contract.

## Next gate

The next implementation question is an enforceable, owned provider for one small
isolated domain, with physical alias invalidation connected to existing current
gates and a guard retained through async effects/quarantine. Until that exists,
this stage proves rejection rules and policy consistency, not production closure.
Ordinary automatic selection, physical authority, cross queue, Draw/Dispatch
extension and F2/F3 repair remain out of scope and unproved.

## Linux acceptance

Accepted source: `3f7a3440e9a35afaf8fc9f22df201f7953133ee9`.
[Linux run 39127457695](https://github.com/Splaser/emulator/actions/runs/39127457695)
passed (job `117675489089`, 18m44s). Android job `117675490297` was skipped.

- New contract: 400 independent byte-oracle cases, successful initial execution
  plus three repeats, all 20 targeted mutants failing by assertion, and the
  separately retained weakened-contract red control.
- Existing verification: all 78 extracted-body and 12 pure Planner assertion
  mutants still fail as expected; all pure/fixture repeat groups pass. This adds
  no Planner or Copy expression forms.
- Actual production translation units, CLI/shader/test build, all five CTest
  processes, coherency/lifecycle/backing suites and native Vulkan pass. The
  unchanged native provenance case retains 398 assertions and three repeats,
  including actual planner-admitted four-hop Copy/readback. These native results
  validate regression safety, **not real producer exclusion by the new verifier**.
- All 24 existing extracted-body, 6 Planner and 4 new contract input SHA256 values
  match exact Git blobs at the accepted source (34 total inputs).
- Original F2 remains expected 77 / observed 10 / backing 77. Original F3 full and
  partial aliases remain expected 99 / observed 10; fixed-required has two failed
  assertions and exits 42. Restricted eligibility remains blocked; F2/F3 Open.

Complete artifact contents (generated source, executables, assertions, repeats,
manifests and CTest logs), complete Linux job output, local final contract log and
an independent verification script/index are retained under
`.diagnostics/ci-39127457695/`. The verifier checks exact inputs, 20+78+12 mutant
assertions, all repeat groups, CTest and both original negative witnesses.

Changed files are the new
`src/video_core/buffer_cache/producer_coverage_contract.h`,
`tools/physical_coherency_tests/producer_coverage_tests.cpp`,
`tools/physical_coherency_tests/run_producer_coverage_tests.py`,
the existing `.github/workflows/pr375-pr380-integration.yml`, this document and
`docs/M4_F3_WIP_HANDOFF.md`. The workflow only adds the pure runner and artifact
path; existing production/test scopes remain intact. Scheduler, Planner, receipts
and ordinary CPU/GPU execution files have no changes. Subsequent acceptance-only
documentation does not require rerunning unchanged source CI.
