# M4 F3: immutable physical provenance

This slice associates historical physical identity with the existing opt-in storage
observer. It does not implement latest-writer selection or content authority.
Ordinary Memory::Write/UMA routing remains inactive; F2/F3 remain Open and
restricted eligibility remains blocked.

## Identity before execution

A current DAddr translation alone cannot identify an older staged allocation's
physical source. While explicit observation is enabled, a newly created staged
Buffer captures a bounded allocation-origin identity: its native payload identity,
device address geometry, Device MappingEpoch, GuestBackingEpoch and physical byte
segments. The PTE snapshot uses the existing mapping lock and fixed 64-segment
storage. It does not allocate/materialize physical backing or wait for the GPU.
Only bounded host metadata is allocated under the existing cache owner.

At the actual writable storage binding, PhysicalIdentityForBinding slices that
saved origin to the exact native offset/length and checks the current mapping
against it. Revalidation can refuse the association; it cannot replace its physical
identity with today's mapping. The immutable slice is attached to the same
CheckedWrite that retains the actual native payload and actual submission.
Completion never resolves DAddr again.

Origin qualification also requires the original device mapping generation. Equal
current geometry alone cannot exclude an intervening remap/restore while staged
contents were initialized. An unrelated generation change is conservatively
incomplete too; this slice does not prove per-range mapping stability. The existing
mapping stamp is compared for equality only, never used as a content version or
write ordering. A geometry-only comparison is diagnostic and cannot qualify a
representation origin.

Allocations predating observation have no origin and stay unqualified. Scratch,
direct/imported buffers, holes, oversized/unknown footprints and representation
identity mismatches also make capture incomplete. Cache JoinOverlap explicitly
invalidates the new representation's origin: this slice does not prove inherited
representations through merge. Origins are not reconstructed after this refusal.
Native resource recreation captures a fresh identity; earlier events retain their
own immutable geometry.
Origin capture is one-shot, including disabled/failed capture attempts. Calling
the hook again cannot rebase an existing allocation or requalify a merged one.

PhysicalWriteIdentity is an identity plan, not the pinned PhysicalAccessPlan used
for actual data transfer. It retains epoch tokens and geometry, not HostMemory
arena/page ownership, and exposes no backing byte pointer. Epoch death disables
qualified completion access while historical values remain inspectable. Building
a future content publisher requires real storage ownership/admission separately;
identity metadata must never be used as permission to access backing data.

Two DAddr ranges can overlap the same GuestBackingEpoch/physical offsets while
using different staged allocations. OverlapsPhysical detects that relation without
comparing resource revisions or ordering submission IDs. This identifies aliases,
not the latest writer or an execution dependency.

## Coverage is shared and monotonic

All ordinary events in one explicit observation scope retain the same coverage
token. The token starts Open: a currently valid prefix is not complete coverage.
Disabling observation closes the scope and preserves its records for inspection.
Starting another scope does not complete an abandoned open scope.

Any unsupported/failed capture, metadata failure or capacity refusal permanently
invalidates that shared token, including the events retained before the failure.
Sealing cannot repair an incomplete scope. Retaining 200 successful records from
an overflowing interval therefore cannot produce complete producer coverage.
This remains a storage projection; closing it does not enroll images, transfers,
gather, attachments or other producers in the same Vulkan batch.

Scheduler::CompletedPhysicalIdentity is the checked historical access gate. It
requires actual Complete execution, a closed complete coverage scope, matching
native/range metadata and live identity epochs. Pending, Submitted, Skipped,
Unknown, retired, open or incomplete records cannot pass it. Its result still
describes the OLD association after remap and is never a current-authority
certificate. A future publisher must additionally revalidate the intended mapping,
retain/admit storage, account for every overlapping writer and establish actual
synchronization/visibility relations.

## Review regressions and evidence boundaries

The deterministic actual-body Draw/observer/Scheduler fixture explicitly injects
VK_ERROR_DEVICE_LOST from SubmitQueue. It checks the original exception result and
device-loss reporting, observed Unknown, no successful timeline progress or queued
device effects, and permanent refusal by CompletedPhysicalIdentity even after the
fake driver stops reporting errors. Ordinary error propagation is preserved.

The overflow fixture records 257 actual Draw worker callbacks: all execute exactly
once in the existing batch, the first 256 metadata records remain, and the shared
coverage token makes every retained record ineligible for completed physical
provenance. The fixture also covers late remap, replacement native identity,
pre-observation allocations, physical aliases, partial cross-page slices, holes,
origin invalidation and dead backing epochs. Vulkan/PTE dispatch in this layer is
simulated; the method bodies and identity implementation are production code.
An additional mutation removes the actual completed-provenance coverage check;
the overflowing-prefix assertion must fail. Its expected failure output and
generated C++ are retained with the workflow evidence.

The native [f3-provenance] process uses real System/Kernel device mappings,
ordinary staged allocations and checked Vulkan transfers. It verifies stable
association, independent staged aliases to the same physical storage, a remap
after actual queue acceptance, immutable old geometry/bytes and rejection of
pre-observation allocations. These are transfer commands, not actual shader/game
execution or Android device acceptance. Original F2/F3 negative witnesses remain
required.

## Next gate

The following bounded extension is now implemented in
[synchronization provenance](M4_F3_SYNCHRONIZATION_PROVENANCE.md): direct global
memory-barrier evidence within one checked graphics submission, with physical and
native byte coverage checks. Cross-submission/queue relationships and content
authority remain unimplemented. The paragraph below records the broader gate.

Real synchronization provenance is still unimplemented: same-queue execution and
memory dependencies, cross-queue semaphore relationships, transfer/shader access
dependencies and partial interval coverage must be established explicitly. Do not
replace those proofs with a global content_version, resource revision ordering or
the largest submission ID. Latest-writer authority is a later, separate step.

## Verification record

Final production source is `84d004b270`; `52560e5ec9` changes only the native
scheduler test's pre-enqueue setup. The complete
[Linux run 38998481678](https://github.com/Splaser/emulator/actions/runs/38998481678)
passed all five CTest processes: 130 ordinary cases, 1708 original scheduler
assertions, 38 original F3 assertions, 88 isolated alias assertions and 105 physical
provenance assertions. Three additional provenance processes each passed all 105
assertions. Original F3/alias repetitions and every configured HostMemory/import,
coherency/lifecycle and mutation gate passed. F3 fixed mode retained exactly two
known alias failures (36 passing assertions), exit 42; F2 remained backing 77/cache
10 and require-fixed exited 1.

The [Android native job](https://github.com/Splaser/emulator/actions/runs/38997972989/job/117294620935)
passed actual arm64 core/video/shader library compilation with CPM/NDK 29 on
`84d004b270`. It is library compilation, not APK or target-device/game execution.
That full run's Linux job failed the old scheduler fixture's single-attempt enqueue
assertion; it is not described as an entirely passing run. Production/workflow
sources are identical between the Android-tested source and the final Linux head.
The final documentation follow-up changes no production/test code or workflow.

| Required association | Evidence |
| --- | --- |
| Stable staged allocation / physical pages | Native actual device mappings, native identity/offset/length checks and checked fill/readback. |
| Remap after actual queue acceptance | Native old geometry and old native bytes retained; strict current-map validation rejects the changed mapping. Completion exposes only the old association. |
| Two DAddr aliases to one backing | Native shared backing/physical overlap with distinct staged identities and different retained bytes; no revision-based source choice. |
| Unknown mapping / representation | Actual-body pre-observation, hole, generation change/restore, mismatched and invalidated origins reject association and invalidate shared scope coverage. |

The deterministic device-loss fixture checks the actual ordinary exception result,
device-loss reporting, Unknown, absence of successful timeline/device effects and
refusal by the physical history gate. The overflow case executes all 257 Draw
callbacks once, retains 256 metadata records and refuses the entire prefix after
scope invalidation. The extra mutation removes the actual coverage check and
fails precisely the overflowing-prefix CompletedPhysicalIdentity assertion.

Validation corrections are retained, not counted as passing intermediate runs:

- [38996500093](https://github.com/Splaser/emulator/actions/runs/38996500093)
  passed native 104-assertion provenance at `6c639e4108`, then found the concrete
  gather Runtime's missing merge-invalidation hook (-Wtemplate-body). `63bfe2448d`
  adds that fixture hook and checks actual JoinOverlap invocation without weakening
  source warnings or the original data assertions.
- [38996898419](https://github.com/Splaser/emulator/actions/runs/38996898419)
  passed F2, then exposed the representation pin extraction's missing actual
  identity header. `a191a7d7c2` adds the include/hash; resource-retention,
  non-owning-pin and missing-seal controls passed locally and in final CI.
- Actual-header generation-change testing reproduced acceptance of equal final
  geometry with different mapping history. `84d004b270` requires the original
  generation; the same control and actual-body fixture then passed. Obsolete
  pending checks were cancelled and are not acceptance evidence.
- The original scheduler suite in the Android source run encountered a permitted
  TryQueueCpuWrite try-lock refusal while filling its 64 owner slots. `52560e5ec9`
  retries only the no-effect transport boundary within the existing Until deadline.
  It does not retry admitted CPU requests or alter production queue behavior, and
  retains the exact 64-slot cancellation assertion. Final native scheduler and
  physical provenance checks all passed.

Exact final Linux source stamp, per-process native logs, generated fixtures,
extraction hashes, expected coverage-negative log and full job output are retained
in `.diagnostics/ci-38998481678/`. Android configuration and complete job output are
in `.diagnostics/ci-38997972989/`, including its original scheduler failure.
Earlier first-error logs are in `.diagnostics/ci-38996500093/` and
`.diagnostics/ci-38996898419/`. Windows actual-body controls and the mapping-history
red/green control remain separate fixture evidence; remote native acceptance belongs
to the exact sources above. No local Android compilation was used.
