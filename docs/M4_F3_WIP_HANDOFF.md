# M4 physical alias coherency: WIP handoff

Initial checkpoint date: 2026-10-16, at the user's requested 5-hour usage
threshold. Evening continuation established a stable native F3 reproduction.
This is a continuation checkpoint, not M4 acceptance or a production F3 fix.

## Status and constraints

- Submission-provenance source `102b0486f7` (initial implementation `64c59e9208`)
  introduces only an explicit isolated
  staged fill linked to actual native owner/footprint, local intent and checked
  submission. Pending, Submitted, Skipped, Complete and Unknown are distinct;
  historical completion does not certify a still-current physical representation.
  See the [provenance boundary and tests](M4_F3_SUBMISSION_PROVENANCE.md). Ordinary
  shaders/alias consumers are not switched to this API and no latest-writer
  authority is published. Verification results are recorded in that document.
  Full [Linux/Android run 38984914456](https://github.com/Splaser/emulator/actions/runs/38984914456)
  passed at `102b0486f7`: five CTest processes, 53 provenance assertions in CTest
  and each of three extra processes, unchanged original F2/F3 negative controls,
  all configured integration/lifecycle/fault gates and actual Android arm64
  core/video/shader compilation. The failed-pipeline Draw guard remains an
  actual-body fixture, not native shader/binding execution. Evidence is retained
  under `.diagnostics/ci-38984914456/`. No physical content authority, general
  draw/dispatch provenance or ordinary F3 repair is certified.

- Write-intent follow-up `d8764c0cf0` records resource-local GPU write revisions at
  ordinary write marking, staged DMA copy/clear and scatter destinations. The
  isolated adapter rejects pre-submit source/target rewrites even when dirty
  geometry is unchanged; post-submit rewrites retain Unknown and quarantine.
  This starts producer metadata but is not submission completion, historical
  physical provenance or latest-writer subrange authority. See the
  [revision record](M4_F3_CONTROLLED_ALIAS_ADAPTER.md#resource-write-intent-revisions).
  Full Linux/Android run [38907387312](https://github.com/Splaser/emulator/actions/runs/38907387312)
  passed at that source: 88 native adapter assertions in CTest and each of three
  extra processes, all configured integration/fault/lifecycle gates and actual
  Android arm64 core/video/shader compilation. Original F3/F2 negative controls
  remain unchanged. Baseline run `38906918308` separately confirms the repeated
  source write was accepted by the old adapter. Both runs' evidence is retained
  under `.diagnostics/ci-<run>/`; no Android/device execution is claimed.

- M3B-F2 remains **Open**. Restricted eligibility remains **blocked**; see
  [eligibility blockers](M3B_F2_RESTRICTED_ELIGIBILITY.md) and
  [producer inventory](M3B_F2_PRODUCER_INVENTORY.md).
- Ordinary `Memory::Write` routing and selective UMA zero-copy remain inactive.
- No new coordinator or broad BufferCache rewrite has been introduced.
- The native F3 witness and test observation helpers have compiled remotely. The
  standalone `[f3]` processes reproduce stale full/partial aliases with whole-byte
  controls, remap rejection and physically equivalent representation qualification.
  Final remote validation is tracked below. **F3 is reproduced, not fixed.**
- No local Android build was attempted; validation continues through remote workflow.

## Initial verified baseline

The checkpoint starts from `3462bbadcf`, previously pushed to both `main` and
`codex/issue-372-fixes` (PR 375 source branch).

- Production source at `3ebb655b97`: Android job passed in run `38870993595`.
- Linux workflow `38873324462` at `46687f1bfa`: passed, including 1708 native
  assertions, 130 production unit cases, integration/mutation checks, renderer
  and FenceManager fixtures. These are baseline results, not validation of this WIP.
- Original F2 witness was reproduced three times: expected CPU value 77,
  observed cache value 10, backing 77. Its `--require-fixed` invocation failed as
  expected. This defect remains open and must remain a regression witness.

## Source audit and current hypothesis

| Area | Existing source and finding |
| --- | --- |
| Physical backing lifetime | `src/common/published_guest_chunk.h`: backing epoch owns generation/alive state, range admission and page gates; lifetime invalidation closes authority even while storage remains pinned. |
| Immutable physical plans | `src/video_core/physical_access_plan.h`: snapshots device mappings, pins physical storage outside mapping locks, then revalidates. `MatchesCurrentDevice` checks captured epoch/generation and physical pages. A storage pin is not content authority. |
| DAddr aliases | `src/core/device_memory_manager.inc`: odd/even mapping generation, reverse alias collection, bounded alias enumeration and inverse-translation verification. Staged-mode Map itself does not perform the UMA unmap path. Higher-level mapping callers still require audit. |
| Staged ownership | `src/video_core/buffer_cache/buffer_cache.h`: ordinary lookup, overlap joining, upload tracking and GPU dirty publication are keyed by DAddr. Physically overlapping but disjoint DAddr windows can own independent staged buffers. |
| Native resource pin | `src/video_core/renderer_vulkan/vk_buffer_payload.h`: pins retain allocation and byte bounds. They do not prove cache membership, current physical association, authoritative content, admission or completion. |
| Representation qualification | `PinExistingDmaRepresentation` validates physical geometry and takes/revalidates a fresh mapping snapshot around pin acquisition. It does not simply reject every historical plan generation change; existing physical-equivalence behavior must be preserved. |

Observed F3 sequence: warm two independent staged buffers A/B from physical
backing 10, then complete a GPU write of 99 through A. Ordinary synchronization
through B can still consult B's DAddr-local tracking and serve its old staged
value 10. Completing physical range admission establishes ordering but does not
itself transfer authoritative content between staged aliases. This explanation
matches the standalone native result. See
[source cause and bounded authority plan](M4_F3_ALIAS_AUTHORITY_PLAN.md).

## Native witness and process-isolation correction

`src/tests/core/controlled_cpu_scheduler.cpp` adds the `[f3]` case to the existing
`controlled_cpu_scheduler_tests` target. `src/core/scheduler_test_access.h` adds
test helpers for ordinary buffer acquisition, Vulkan fill/readback, retained
staged pins and cache retirement.

| Case | Required observation |
| --- | --- |
| Full-page alias | A/B map identical physical pages; GPU writes A=99; same A reads 99; B currently expected to reproduce stale 10. |
| Partial alias | C starts one physical page into A; a 256-byte GPU fill within that page reads 99 through A, currently expected stale 10 through C. Adjacent untouched bytes stay 10. |
| Disjoint control | Separate physical backing D remains 10. |
| Admission control | Existing physical range admission parks the B reader behind A's writer and grants it after completion; no new coordinator is used. |
| Retirement/remap | Retire B before remapping it to backing 55; a retained old pin remains an old allocation, the old physical plan and representation query reject the new mapping, and a fresh representation has a new identity and reads 55. A's unchanged physical association still qualifies despite the global mapping-generation change. |

The driver uses actual System/Kernel/process objects, device mappings, ordinary
BufferCache code and native Vulkan transfer execution through the existing
software-device test renderer. The sequence and manual admission enrollment are
test drivers. This is not a game image, an ARM scalar store, a shader draw, a
GPUVAddr translation witness, or proof that ordinary producers participate in
production admission. Existing extracted cache/pin and renderer fixtures remain
fixtures; do not report their results as this native F3 reproduction.

Commands on the remote Linux build:

```sh
build-integration/bin/controlled_cpu_scheduler_tests '[f3]'
CITRON_REQUIRE_F3_FIXED=1 build-integration/bin/controlled_cpu_scheduler_tests '[f3]'
```

The F3 case is hidden from an unfiltered Catch2 invocation and explicitly selected
by its own CTest process. KernelCore's host dummy KThread is thread-local and
retains the first Kernel; creating two complete System instances sequentially
in the same host test thread reused stale kernel state. Run `ctest`, the original
unfiltered scheduler executable, or the explicit `[f3]` process; do not select both
System lifetimes in one executable invocation.

Default mode intentionally requires stale alias values 10 and prints
`F3 REPRODUCED (not fixed)` only after positive controls pass. The second command
requires coherent values 99 and should fail until F3 is fixed. The repeat driver
`python tools/physical_coherency_tests/run_native_f3_tests.py --repeat 3` retains
each normal log and explicitly rejects crashes, timeouts, failed positive controls
and unexpected negative-mode failures. Catch assertions outside the GPU callback
receive captured setup/observation exceptions rather than terminating the GPU
worker on an uncaught test exception.

## Resume roadmap

1. Extend the [bounded authority implementation plan](M4_F3_ALIAS_AUTHORITY_PLAN.md)
   with persistent write-site physical/provenance metadata and latest-writer
   subrange ownership. Reuse the now verified
   [isolated alias adapter](M4_F3_CONTROLLED_ALIAS_ADAPTER.md) for qualified exact
   copies. Its per-operation snapshots are not a general write-site version issuer.
   Preserve DAddr lookup and GPU dirty complements; do not add a coordinator.
2. Integrate overlapping alternating writers (currently refused), merge/GC
   authority transfer and frame-reuse exclusion. The adapter already covers
   disjoint reverse writes, source/target retirement and submitted remap Unknown;
   native adapter success is not ordinary-path or allocator-lifetime acceptance.
3. Preserve the original ordinary F3 negative witness and F2 defect. A manually
   driven copy adapter cannot count as an ordinary-path coherency fix. Only after
   the implementation exists may fixed-mode passing controls replace this gate.
4. Run the complete `pr375-pr380-integration.yml` gates after source changes.
   Preserve M3B native/integration/mutation regressions; Android production source
   changes require the remote full scope, without a local Android build.

## Continuation evidence: 2026-10-16 evening

- Initial run [38891421101](https://github.com/Splaser/emulator/actions/runs/38891421101)
  compiled production and native targets, passed the 130-case ordinary test suite,
  and printed full/partial stale 10, same-DAddr 99 and remap 55. Its combined native
  executable timed out; this was not clean witness acceptance.
- Diagnostic run [38896969297](https://github.com/Splaser/emulator/actions/runs/38896969297)
  confirmed standalone F3 normal exit under GDB (28 assertions). Both native
  cases in one process failed in whichever complete System lifetime ran second.
  Test process isolation is implemented at `0fd0f17c9e`.
- Run [38897769357](https://github.com/Splaser/emulator/actions/runs/38897769357)
  at `f6ab101715` passed all three CTest processes (130 ordinary cases, 1708
  scheduler assertions, 36 F3 assertions) and three additional standalone F3
  repetitions. Fixed mode produced precisely the two expected alias failures;
  the wrapper incorrectly assumed the older Catch2 exit convention. `62259c5dbd`
  recognizes Catch2 3.16's test-failure status 42 and records process exit codes.
- Final validation of `48d95a8ba3`, with two additional native mapping qualification
  controls, passed in run
  [38898475861](https://github.com/Splaser/emulator/actions/runs/38898475861).
  All three CTest processes passed: 130 ordinary cases, 1708 scheduler assertions,
  and 38 F3 assertions. Three additional F3 processes each passed 38 assertions
  and exited 0. Fixed mode exited 42 with exactly the two expected alias failures
  (36 passing/2 failing assertions), after normal guest teardown. F2 reproduced
  three times and its negative mode exited 1 as required. Real Linux HostMemory/
  import lifetime and every configured coherency/lifecycle/mutation regression
  passed. Android was intentionally skipped because production code is unchanged.
  Downloaded source stamp, CTest logs, each F3 process log, F2 negative log and
  complete job output are retained in `.diagnostics/ci-38898475861/`.
- Local Windows extracted F2 witness ran three repetitions: backing 77, cache 10;
  invoking its compiled `f2 require-fixed` returned 1 as expected. This is fixture
  regression evidence, not native Vulkan or Android acceptance.

The witness-only continuation through `425de3055b` did not change production code.

## Subsequent continuation: isolated staged alias copy verified

Production source `642a71fb07` implements the explicit admitted alias-copy adapter;
full run [38903097142](https://github.com/Splaser/emulator/actions/runs/38903097142)
passed Linux and Android compilation. Linux passed the original 130 ordinary cases,
1708 scheduler assertions, 38 F3 assertions and 67 new native adapter assertions,
plus three extra adapter repetitions. Test-only weak-payload-owner checks at
`a415dea618` passed full Linux run
[38903908981](https://github.com/Splaser/emulator/actions/runs/38903908981), including
the new exact-copy/failure/retention fixture and every existing integration/mutation
regression. Production source/workflow differences between those heads are empty.

See [adapter contract and evidence](M4_F3_CONTROLLED_ALIAS_ADAPTER.md) for exact
scope, commands/cases, retained resources and Unknown behavior. The original ordinary
F3/F2 negatives still reproduce; automatic ordinary routing, latest-writer authority
and general producer eligibility remain blocked. Next development starts at the
resume roadmap above, not by redoing the verified adapter.

## Subsequent continuation: ordinary storage submission observation

Source `be3847045e` adds an opt-in ordinary Draw/indirect Draw/Dispatch storage
observer. It captures the actual descriptor allocation/range and write intent,
then joins the existing batch's checked submission without a per-draw flush.
Default rendering remains unobserved; unsupported capture is incomplete and does
not suppress/replay the command. Shader footprints are conservative access ranges,
not proof of per-byte stores or physical content versions.

See the [observer contract and current validation record](M4_F3_STORAGE_WRITE_OBSERVER.md)
and [initial checked provenance slice](M4_F3_SUBMISSION_PROVENANCE.md). Native
provenance now has 70 assertions, including two real fills sharing an ordinary
submission and a subsequent batch with a different identity. The Linux job of
[38989337199](https://github.com/Splaser/emulator/actions/runs/38989337199) passed
all five CTest processes, repeated native controls, original F2/F3 negatives and
all configured integration/mutation regressions. Full platform status is recorded
in the observer document.

Next gate: retain immutable GuestBackingEpoch/physical geometry and prove actual
representation association plus synchronization relationships before any physical
latest-writer publisher. Do not infer authority from Complete, maximum revision or
submission identity. Actual shader/game execution, merge/GC and mapping/frame reuse
remain separate acceptance gates. F2/F3 remain Open, restricted eligibility remains
blocked, and ordinary Memory::Write/UMA routing remains inactive.

## Subsequent continuation: immutable physical identity before authority

The [immutable physical provenance contract](M4_F3_IMMUTABLE_PHYSICAL_PROVENANCE.md)
supersedes the earlier observer's lack of physical identity. Allocation origins
are captured once from real device PTE snapshots; exact binding slices retain
GuestBackingEpoch/physical geometry and native identity. Existing, merged,
ambiguous or mapping-generation-changed resources remain incomplete. Completion
never re-resolves a current DAddr or chooses a writer by revision/submission ID.

Shared coverage is Open until the GPU owner explicitly closes the observation
scope. Any capture failure/overflow invalidates every retained event in the scope;
sealing cannot repair it. CompletedPhysicalIdentity requires checked Complete,
closed complete coverage and live identity epochs. It exposes historical identity
only, with no backing pointer/storage ownership or content authority.

Production source is `84d004b270`; the native test setup follow-up `52560e5ec9`
changes only pre-enqueue retry behavior in the existing scheduler fixture. Native
physical provenance now has 105 assertions. Validation/failures and exact source
relationships are recorded in the contract; use those results rather than inferring
acceptance from an intermediate run's partial success.

Next stage remains real synchronization relationships: same-queue execution/memory
dependencies, cross-queue semaphore relationships, transfer/shader dependencies and
partial intervals. Do not start latest-writer publication from these identity
records. Real storage pins/admission, complete producer enrollment, mapping/frame
reuse and merge/GC ownership are still required before content authority. F2/F3
remain Open; restricted eligibility and ordinary Memory::Write/UMA stay blocked.

### M4 F3 synchronization provenance extension

See [the synchronization contract](M4_F3_SYNCHRONIZATION_PROVENANCE.md). Direct
same-submission graphics-queue WFI memory-barrier dependencies are now recorded
alongside actual Vulkan emission and gated by checked completion, closed coverage,
stage/access scopes and complete same-native-byte physical projection. Independent
replicas, different submissions and cross-queue relations remain Unknown. No data
copy or latest-writer publication is implemented. Source `a5dba62536` passed full
[workflow 39004334780](https://github.com/Splaser/emulator/actions/runs/39004334780):
all five Linux CTest processes, native provenance 119 assertions plus three repeat
processes, focused actual-body controls and five expected mutation failures, full
coherency/lifecycle regressions and Android arm64 production library compilation.
F3 require-fixed still fails exactly two aliases (exit 42); F2 remains backing
77/cache 10. Detailed boundaries and retained artifacts are in the contract.

### M4 F3 exact transfer content extension

See [the transfer content contract](M4_F3_TRANSFER_CONTENT_PROVENANCE.md). Explicit
bounded Scheduler Fill/Copy requests now retain exact command coverage, immutable
copy ancestry and both untouched complements inside one checked callback. Initial
bytes are Unknown; previous submissions and shader bindings cannot seed roots.
The mapping gate refuses remap/dead origins. No ordinary caller routing, automatic
replica synchronization or physical latest-writer publication is added.

Final source `4a3f9a6aba` passed complete
[workflow 39012415113](https://github.com/Splaser/emulator/actions/runs/39012415113):
all five Linux CTest processes, native provenance 149 assertions plus three repeat
processes, 23 actual-body content cases, nine content/five sync mutation failures,
512 byte-oracle prefixes, full coherency/lifecycle regressions and actual Android
arm64 production library compilation. No APK/device/game acceptance is claimed.

The pre-submit negative reproduced early pin retirement after command recording
but before its queued Submit callback. The final source guards queue attempt/result
as well as host callback completion; unattempted host failures remain quarantined.
The initial `f682e086e9` run was cancelled/superseded, not accepted. Artifacts and
source-hash correspondence are recorded in the transfer contract. Original F3
require-fixed still fails exactly two aliases (exit 42); F2 remains backing 77/cache
10. Cross-submission/queue roots stay Unknown; no latest-writer authority is added.

### Current-content cancellation follow-up (2026-10-17)

See [M4_F3_CURRENT_CONTENT_GATE.md](M4_F3_CURRENT_CONTENT_GATE.md). Historical
ancestry remains immutable. A separate current query requires checked completion,
strict physical mapping, live cache membership and no subsequent unclassified
Scheduler recording. Ordinary Fill/Copy, observer-off shader recording and upload
commands cancel before device execution; reads/disjoint commands conservatively
cancel too. Unregister cancels staged payload membership without releasing pins.
Worker completion cannot restore eligibility. Cross-submission/queue ancestry
remains Unknown and no authority or replica synchronization is published.

The actual-body red control is retained in `.diagnostics/m4-current-gate-red.log`.
Final source `95ab388bd9` passed complete
[workflow 39015687982](https://github.com/Splaser/emulator/actions/runs/39015687982):
all five Linux CTest processes, native provenance 159 assertions plus three repeat
processes, 31 actual-body content cases, 17 expected mutation failures, full
coherency/lifecycle regressions, and actual Android arm64 production library
compilation. Eleven extraction input hashes match committed Git blobs. Logs and
artifacts are retained in `.diagnostics/ci-39015687982/`.

The first run at `e7ee346913` exposed a native `-Werror=shadow` diagnostic and was
superseded after the local variable rename, without changing warning policy.
No APK/device/game acceptance is claimed. Next work must preserve this separate
cancellation contract: returned graph pointers are historical metadata across
subsequent work, read/disjoint commands conservatively cancel, and no current
query constitutes physical authority or a concurrent data-admission lease.

### Staged byte-range eligibility follow-up (2026-10-17)

See [M4_F3_RANGE_CURRENT_CONTENT_GATE.md](M4_F3_RANGE_CURRENT_CONTENT_GATE.md).
The new range query checks a pin-relative nonempty byte interval. Typed actual
Fill/Copy remove only overlapping destination ranges on the same live staged
allocation; source ranges, untouched complements and independent allocations
remain eligible. Ordinary runtime clear/copy/upload/readback use the typed
emitters and preserve their prior barriers. Unknown/general recording still
rejects the whole snapshot. Native offsets, bounded fragmentation, device-loss
failure domains, mapping and retirement must all fail closed.

Historical roots and origins remain immutable; preserved current ranges do not
import any cross-submission content source. No latest-writer authority, replica
reconciliation, or ordinary Memory::Write/UMA activation is added. The three
development stages are recorded explicitly: close proven content provenance,
then real replica reconciliation with complement preservation, then the original
ordinary F3 witness subject to M3B producer-exclusion eligibility. Controlled M4
acceptance and production F3 acceptance remain distinct.

The old whole-snapshot behavior failed the new untouched-range control as expected
(`.diagnostics/m4-range-gate-red.log`). The typed-barrier control additionally
exposed copy pre/post and WFI barriers cancelling complements; their typed enqueue
now preserves content eligibility while retaining the actual barriers and scopes.
The source-only current-root query also failed a stale-interval control
(`.diagnostics/m4-current-root-red.log`); `CurrentTransferHasRoot` now combines
current byte eligibility and the exact historical root/projection without
promoting other bytes in a retained graph pointer.

Final source `3bf13ca261` passed
[Linux-only workflow 39019720456](https://github.com/Splaser/emulator/actions/runs/39019720456):
all five CTest processes, native provenance 177 assertions plus three repeat
processes, 46 actual-body transfer-content cases, 27 expected mutation failures,
512 content/2,048 eligibility oracle prefixes and full coherency/lifecycle tests.
All 12 extraction input hashes match committed blobs; artifacts are retained in
`.diagnostics/ci-39019720456/`. Original F3 still rejects two aliases (exit 42),
and F2 remains backing 77/cache 10. No authority or reconciliation is published.

Validation policy: default to Linux compilation, unit tests and relevant native
regressions. Run Android builds only for Android-specific changes or risks Linux
cannot cover; manual workflow dispatch now defaults to `linux-only`, with `full`
available when needed. Android was intentionally skipped for `3bf13ca261`; do not
claim an unrun Android or device/game acceptance. The previous `cc055d3426` full
Linux job passed independently (170 native assertions). Its already running
Android job did not block this follow-up. This documentation-only handoff does
not repeat the same source validation.

### Controlled same-queue submission handoff (2026-10-17)

See [M4_F3_SAME_QUEUE_REPLICA_COPY.md](M4_F3_SAME_QUEUE_REPLICA_COPY.md).
This follow-up copies one exact current interval between different private staged
allocations over the same saved physical alias, preserving explicit parent roots
for copied bytes and untouched target complements. Actual accepted graphics-queue
predecessor identity, entry/release barriers and checked completion are required;
ticks/revisions do not rank producers. Writable host pointer exposure disqualifies
the payload. Missing synchronization, stale source/target intervals, failures,
remap, unknown recording and retirement fail closed. Source writes after a
completed copy do not rewrite its historical ancestry.

Scope remains one hop from one controlled parent batch, with no intervening
unchecked submission. Cross-queue and unsupported chains remain Unknown. There
is no physical latest-writer publication, ordinary game selection or F3/F2 closure.
Use Linux-only CI and relevant unit/native tests; Android remains on demand.

Source `d783250b0d` passed
[Linux-only workflow 39022879191](https://github.com/Splaser/emulator/actions/runs/39022879191):
all five CTest processes, native Vulkan provenance 211 assertions plus three
independent repeats, 49 new replica modes alongside the existing 46 transfer
modes, 42 expected assertion-failure mutations, byte oracles and the full
registration/coherency/lifecycle regression suite. Seventeen extracted
input hashes match committed blobs; all 43 fixture logs are retained under
`.diagnostics/ci-39022879191/`. Original F3 fixed-required mode still fails two
alias checks (exit 42); F2 remains backing 77/cache 10. Android was intentionally
skipped. This evidence update changes documentation only and does not rerun CI.

### Bounded multi-hop same-queue handoff (2026-10-17)

The follow-up in [M4_F3_SAME_QUEUE_REPLICA_COPY.md](M4_F3_SAME_QUEUE_REPLICA_COPY.md)
extends explicit receipt ancestry to at most four copies over fresh target slots
from one root anchor. Admission combines current source bytes, immutable root
projection, exact saved binding and known whole-target complements. Completed
qualification verifies every actual submission/queue/barrier edge, not just the
last copy. Ancestor storage/remap/host-exposure cancellation fails current
qualification; later intermediate byte writes do not rewrite already captured
downstream content. Cross-queue remains Unknown;
ordinary automatic selection, latest-writer publication, F3 and F2 closure remain
outside this slice. Android remains on demand.

Source `715d1097b9` passed
[Linux-only workflow 39025579635](https://github.com/Splaser/emulator/actions/runs/39025579635):
all five CTest processes, native provenance 233 assertions plus three independent
repeats, 44 multi-hop modes, 46 expected assertion-failure mutations, 256 new
multi-hop oracle prefixes and full existing content/registration/coherency/backing/
lifecycle regressions. All 17 extracted input hashes match committed blobs;
47 fixture logs and downloaded artifacts are retained in
`.diagnostics/ci-39025579635/`. Original F3 fixed-required mode still fails two
aliases (exit 42); F2 remains backing 77/cache 10. Android was intentionally
skipped. This documentation-only evidence update does not repeat source CI.
