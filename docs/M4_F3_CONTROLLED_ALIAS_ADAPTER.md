# M4 F3: isolated admitted staged alias copy

This implements the first transfer slice of the
[authority plan](M4_F3_ALIAS_AUTHORITY_PLAN.md). It is an explicit Vulkan adapter,
not ordinary-game F3 repair. The original `[f3]` witness and fixed-mode negative
control remain unchanged. F2/F3 stay Open and restricted eligibility stays blocked.

## Supported contract

`RasterizerVulkan::BeginControlledAliasCopy(source, destination, size)` runs on the
existing GPU owner. Both ranges must already have warmed staged resources, stable
qualified producer provenance and external producer/mapping/frame-allocation
exclusion for the operation's lifetime. No eligibility certificate for those
external exclusions is issued by this API. There is no ordinary call-site routing.

The adapter captures immutable physical plans and native payload identities,
snapshots actual source GPU dirty intervals, and builds physical intersections
with the target's ordered segments. The initial bound is 256 KiB, page-aligned
plans, at most 64 copies, and four-byte-aligned transfers. Same-payload copies,
holes, no dirty overlap, CPU-dirty source/target ranges, queued readbacks/scatter,
unknown alias enumeration and competing overlapping GPU dirty aliases refuse.

`HasUniqueAliasSource` checks **all current DAddr aliases**, not only the nominated
target. A third alias's overlapping dirty bytes cannot silently lose to the chosen
source. This is a conservative BufferCache qualification, not proof against other
cache families, historical remap/ABA, untracked producers or NCE writes. Existing
write-site version/provenance integration remains necessary for general activation.

Target GPU dirty complements outside the copied physical intervals are allowed
and preserved. The adapter copies only the selected bytes; it does not upload a
whole stale page, copy into guest backing, consume upload bits, clear the source's
dirty ownership or relabel a replica as a newly completed logical GPU writer.
CPU consumers and subsequent overlapping writers still need their own authority
handoff. In particular, overlapping dirty owners are refused rather than ordered
using DAddr or cache slot identity.

## Existing admission and resource lifetime

Each physical copy interval requests Write access from the **existing backing
epoch's PhysicalRangeAdmission**. Physical pin preparation, admission metadata
allocation and native readback allocation happen outside cache/PTE/mapping locks. The immutable
admission resource owner retains both physical plans and the actual native source,
target and readback allocations. No coordinator, work queue or replacement cache
has been added. Polling uses the existing GPU owner.

| Phase | Behavior |
| --- | --- |
| Prepared / Parked | No copy commands. A predecessor must release all conflicting physical intervals. |
| Submitted | Source and target native identities, mapping equivalence and dirty-source qualification were rechecked before one-shot recording. Ordered Vulkan copies and source/target readbacks use one checked submission. |
| Complete | Checked submission/timeline progress, mapped-memory invalidation and matching source/target readback succeeded. Identities, physical correspondence and dirty qualification were rechecked before admission completion. |
| Refused | No commands recorded. Stale/retired representations or failed preflight cannot fall back to an ordinary copy. |
| Unknown | Recording/submission, visibility, content verification or post-effect qualification is uncertain. The original footprint/resources stay quarantined; subsequent readers park. Polling cannot replay or turn this into Complete. |

Unrelated mapping-generation changes may still qualify through exact physical
geometry. Remapping a participant to different physical pages or replacing its
native payload cannot. Pre-submit retirement refuses without touching a retained
old target. Post-submit remap/retirement becomes Unknown while the copy addresses
only its old pinned allocation, never the replacement mapping.

Renderer shutdown cancels only pending aliases. Submitted operations become Unknown
before pin-domain sealing. Native owners can be disarmed through the existing
`RetireControlledTransfersAfterDeviceIdle` boundary only after real producer joins
and trusted successful device idle. Idle does not by itself forgive Unknown or
release its physical quarantine. No host cache/mapping/PTE lock is retained over
a Vulkan wait.

## Validation cases

The new hidden `[f3-transfer]` case runs in its own CTest process, using the real
System/Kernel/process, current device alias enumeration, ordinary BufferCache and
software Vulkan. It covers exact partial/full transfers, every untouched byte,
disjoint reverse writes within one page, a parked predecessor, unrelated generation
change, competing target/third-alias writers, both source and target retirement,
retained old payloads, submitted remap, old-footprint reader quarantine and disjoint
reader progress. The original ordinary F3 witness still requires stale 10.

On the remote Linux build, run the adapter alone or all four isolated CTest processes:

```sh
build-integration/bin/controlled_cpu_scheduler_tests '[f3-transfer]'
ctest --test-dir build-integration --output-on-failure --timeout 180
```

The registration/pipeline suite additionally extracts the actual AliasTransfer,
factory, CopyBuffer and checked submission bodies into a separate fault fixture.
Its Vulkan dispatch/device execution are mocks. It tests delayed completion,
submit/counter/invalidate errors, unequal readback, domain seal, one-shot submission,
dropping cache buffer owners, Unknown retention and device-idle disarming. The
existing pipeline mutation controls retain their original source/failure sites.
The renderer shutdown extraction fixture checks pending cancellation versus active
Unknown, separately from native execution.

Verified production change: `642a71fb07`. Full Linux/Android workflow
[38903097142](https://github.com/Splaser/emulator/actions/runs/38903097142) passed.
Android compiled the real arm64 core/video/shader libraries through CPM; this is
not an APK or device-execution result. Linux passed all four CTest processes:
130 ordinary cases, 1708 scheduler assertions, 38 original F3 assertions and 67
native adapter assertions. Three extra adapter processes each passed 67 assertions.
The original F3 fixed-mode negative returned 42 with its two expected failures;
F2 reproduced three times and its fixed-mode negative returned 1.

Test-only follow-up `a415dea618` directly checks weak source/target payload owners
before and after trusted-idle retirement. Its complete Linux run
[38903908981](https://github.com/Splaser/emulator/actions/runs/38903908981) also
passed, including all existing HostMemory/import, integration/lifecycle and
mutation gates, the new seven-mode alias fault fixture and repeated native cases.
`git diff 642a71fb07 a415dea618 -- src .github/workflows` is empty: this final
test follow-up does not need a second Android compilation. Source stamps, CTest,
per-process adapter logs, generated fault fixtures and full job output are retained
under `.diagnostics/ci-38903097142/` and `.diagnostics/ci-38903908981/`.
No local Android build was attempted. Local Windows renderer-shutdown extraction
passed three repetitions; that fixture result remains separate from native CI.

## Resource write-intent revisions

Follow-up `d8764c0cf0` adds a resource-local, saturating write-intent revision to
the existing BufferBase owner. `MarkWrittenBuffer`, staged DMA copy/clear and
scatter destination publication advance it without allocating or taking a new
lock. Rewriting exactly the same dirty interval (even the same bytes) therefore
cannot reuse a prepared snapshot merely because its RangeSet geometry matches.
The adapter captures source and target revisions under the cache mutex alongside
their dirty snapshots and retained physical/native associations. A change before
submission refuses without commands; after submission it preserves Unknown and
the original physical quarantine. A saturated revision cannot qualify again.

This is deliberately resource-wide: a disjoint new write in the same allocation
also invalidates the operation. Cache retirement/merge still requires the existing
native-identity check; revision values from different allocations are never
compared as a latest-writer ordering. The counter records producer intent, not
submission identity/completion, historical physical provenance, CPU upload/content
versions, replica authority, or mapping/frame-reuse exclusion. No new producer is
enrolled in admission by incrementing it. External isolation remains required.

The native `[f3-transfer]` case now checks a same-interval source rewrite before
submission, an identical-byte target rewrite in a disjoint dirty complement, and
a source rewrite after checked copy/readbacks have been queued. The last case
keeps old copied 44 versus new source 77 and parks a dependent physical reader;
trusted idle cannot turn it into Complete. A separate actual scatter-body fixture
checks revision invalidation before an injected copy failure. That dispatch is
mocked. Baseline `841047f2a3` retains the original adapter and explicitly observes
its acceptance of a prepared source rewritten from 66 to 55 with unchanged dirty
geometry. The earlier verified adapter source/results above remain separate.

Baseline Linux run [38906918308](https://github.com/Splaser/emulator/actions/runs/38906918308)
at `841047f2a3` passed the full configured suite. Its three additional isolated
adapter processes each passed 72 assertions and printed the same-dirty-geometry
rewrite reproduction. Source stamp, standalone logs, generated fixtures and full
job output are retained under `.diagnostics/ci-38906918308/`.

Follow-up run [38907387312](https://github.com/Splaser/emulator/actions/runs/38907387312)
checks exact source `d8764c0cf0`. Linux passed all four CTest processes: 130
ordinary cases, 1708 scheduler assertions, 38 original F3 assertions and 88
adapter assertions. Three additional native adapter processes each passed 88
assertions. HostMemory/import, integration/lifecycle and fault/mutation gates all
passed, including the new actual scatter-body failure check. Original F3 still
has exactly two fixed-mode failures, and F2 still observes backing 77/cache 10.
Downloaded source stamps, standalone logs and generated fixtures are retained
under `.diagnostics/ci-38907387312/`. Local Windows gather/scatter fixtures and
the original F2 reproduction also passed; these remain separate from native CI.
The full run completed successfully, including Android's actual arm64
core/video/shader production libraries built remotely through CPM. This is compile
evidence, not an APK or Android/device execution result. No local Android build
was attempted. The final documentation follow-up changes no production/test
sources or workflow and does not require rerunning these same validations.

## Remaining work

The next increment provides [isolated submission-linked write provenance](M4_F3_SUBMISSION_PROVENANCE.md)
for explicit staged fills, with distinct Pending/Submitted/Skipped/Complete/Unknown
states and actual native resource/intent/footprint identity. It does not switch
this adapter or ordinary shaders to that evidence, publish physical authority or
close any exclusion blocker. General draw/dispatch integration remains a gate.

Attach physical/content provenance at actual staged producer publication, integrate
latest-writer subrange ownership across overlapping aliases, merge/GC and mapping
changes, then integrate ordinary consumers. A snapshot taken by this isolated adapter
is not a substitute for those write-site versions. Keep the original F3 negative
witness until the actual ordinary path becomes coherent. Producer exclusion outside
this isolated footprint remains governed by the existing restricted-eligibility
blockers, not by a successful adapter copy.
