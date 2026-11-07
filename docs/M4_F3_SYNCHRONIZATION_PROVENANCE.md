# M4 F3: synchronization provenance

This bounded slice proves a direct historical access dependency. It does not sort
all producers, choose a latest writer, copy replicas, publish physical contents or
enable ordinary Memory::Write/UMA routing. F2/F3 remain Open and restricted
eligibility remains blocked.

## Accepted evidence

Scheduler::HasConfirmedMemoryDependency(before, after, physical_offset, size)
accepts only two completed observed writes in the SAME checked submission of this
Scheduler's graphics queue and main command buffer. Both must retain closed,
complete physical coverage and live historical identity owners. Pending, skipped,
failed, incomplete and retired observations refuse. False means Unknown, not a
claim that the device executed the accesses without synchronization.

Each emitted observed command receives one worker-recorded position. All of its
footprints share that position; they cannot order each other. Positions are local
command placement, never content versions, timestamps or cross-submission ranks.
The fixed journal retains at most 256 global memory barriers. Overflow invalidates
the entire journal, including earlier prefixes.

RecordProvenanceBarrier issues the actual VkMemoryBarrier and records its masks
and position in the same worker callback, after the Vulkan call. The existing
deferred WFI path uses it with exactly its previous Vulkan stages/accesses. No
barrier, flush or GPU wait is added for observation. Uninstrumented barriers,
buffer/image barriers, events and render-pass implicit dependencies currently
provide no positive evidence.

A direct edge requires A < barrier < B, supported stage coverage, source write
availability and destination read/write access coverage. MEMORY_READ/WRITE and
ALL_COMMANDS/ALL_GRAPHICS are expanded conservatively within supported legacy
stages/access types. Storage bindings declare all potentially used shader stages
and shader read/write accesses; transfer fixtures declare transfer writes. Unknown
metadata or unsupported stages refuse. This is not a general Vulkan dependency
chain solver and does not certify shader-internal ordering or read-only WAR edges.

## Physical and representation bounds

Every requested physical byte must be covered by both historical footprints and
project to the same native byte of the same retained allocation. Ambiguous repeated
physical segments refuse. A partial intersection can qualify only its intersection;
it cannot certify the complete binding/allocation. Binding geometry describes
possible accesses, not guaranteed shader write coverage.

Independent staged replicas refuse even when their GuestBackingEpoch/physical
segments overlap and both submissions complete. Synchronization does not transfer
contents between allocations. This gate therefore cannot certify a stale replica
as a data source. An old historical dependency can remain valid after remap but
does not apply its contents to the replacement mapping.

Different submissions, different Scheduler owners and all cross-queue relations
remain Unknown. A larger timeline value, completion of both events, or equal local
positions cannot bypass that boundary. No semaphore signal/wait edge is enrolled
yet, including across submissions of the same queue.

## Validation

The initial journal placeholder refused everything: the valid transfer-to-shader
edge failed deterministically. The implemented journal accepts that edge and
rejects missing/misplaced barriers, wrong source/destination stages or access
masks, reversed/same-command positions, unknown metadata and overflow.

run_sync_provenance_tests.py extracts actual Scheduler observation, checked
submission/poll, barrier recorder and physical dependency gate bodies, with the
actual physical identity and journal headers. Vulkan dispatch, allocations,
mapping snapshots and device execution are fixtures. Its 13 cases cover valid
edges, missing barriers, wrong stages/access, partial ranges, independent replicas,
separate submissions without a linked semaphore wait, pending/skipped/device-loss
writes, incomplete scope coverage and journal overflow. Matching local positions
across submissions are deliberately constructed. Removing native identity, range
coverage, submission identity, journal recording or the actual Vulkan barrier must
make an assertion fail. These expected mutation failures are retained separately.

The existing registration pipeline runs the same dependency fixture alongside its
ordinary Draw/Dispatch, immutable identity, device-loss and coverage regressions.
The workflow runs the focused controls before native production compilation and
preserves their generated sources, hashes and logs.

The native [f3-provenance] process additionally tests actual deferred WFI recording,
transfer-stage coverage, a qualified 512-byte intersection, full-page refusal,
independent staged aliases and separate submissions. Unobserved helper barriers
keep its Vulkan writes synchronized: native negative cases remove proof coverage,
not the synchronization required for valid GPU execution. Deterministic mocked
dispatch covers genuinely missing barrier/wrong destination conditions without
deliberately submitting a Vulkan data race. Native transfer/readback evidence is
not actual shader/game execution or Android device validation.

## Verified source and results

Production/test source `a5dba62536` passed the complete
[workflow 39004334780](https://github.com/Splaser/emulator/actions/runs/39004334780).
Both Linux production and Android native jobs succeeded. Linux passed all five
CTest processes: 130 ordinary cases, 1708 scheduler assertions, 38 F3 assertions,
88 isolated alias assertions and 119 provenance assertions. Three additional native
provenance processes each passed all 119 assertions. Focused dependency controls,
all five expected mutation failures, the combined actual-body registration/Draw
fixture and every configured coherency/lifecycle regression passed.

Original F3 require-fixed mode still produced exactly two known alias failures,
36 passing assertions and exit 42. F2 still reported backing 77/cache 10, with its
require-fixed control exiting 1. These remain required defect witnesses, not repair
acceptance. Android compiled actual arm64 core/video/shader libraries through
CPM/NDK 29; no local Android build, APK acceptance or target-device run was made.

Downloaded source stamp, LastTest.log, generated actual-body sources, input hashes,
positive logs and separate expected-failure logs are retained under
`.diagnostics/ci-39004334780/`. The documentation follow-up changes no production,
test or workflow source and does not require repeating the same validation.

## Next boundary

The bounded [transfer content extension](M4_F3_TRANSFER_CONTENT_PROVENANCE.md)
establishes exact coverage and copy ancestry for explicitly requested Fill/Copy
callbacks. It does not enroll ordinary transfers/shaders or introduce authority.

Before physical latest-writer authority, separately establish actual content
coverage and replica-copy provenance. Cross-submission/queue edges require linked
signal/wait identities, stage scopes and applicable ownership transfer. Preserve
Unknown until supported. No maximum tick/revision/content counter may substitute
for those proofs.
