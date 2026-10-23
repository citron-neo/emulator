# M4/F3: controlled partial copy into a mixed target

This slice composes existing transfer provenance and Current Content Gate receipts.
It does not implement physical latest-writer authority, cross-queue ordering,
ordinary BufferCache replica selection, or F2/F3 production acceptance.

## Contract

`ReplicaCopyRequest` can name an existing `destination_receipt` and an explicit
`destination_roots` partition of the untouched target complement. Source roots
still cover exactly the requested source interval. Complement roots cover exactly
`[0,destination_offset)` and `[destination_offset+size,target_size)`, in order.
Missing tails, duplicated/overlapping pieces, pieces inside the overwritten hole,
zero sizes, wrong root projections, Unknown and over-budget declarations reject.
Each side may contain several distinct original anchor Fill IDs and projections.

The builder validates immutable ancestry, then owns copies of both partitions in
fixed arrays (64 pieces each). Admission separately checks every source piece and
every retained target piece, before and after leaving the render-pass context.
Each target binding must match the saved native identity, pin offset/size and
physical identity of both the anchor slot and the old target receipt. Its current
mapping must still match the captured epoch. Mapping-query exceptions reject.
An allocation which exposed a writable host mapping or was retired cannot qualify.

The overwritten target bytes need no old content root/current byte qualification:
none of that old content is retained. Unknown or stale *retained* bytes reject.
For full replacement the complement is empty, but the supplied old target receipt
must still have real checked completion and a valid mapping/storage identity.
The original no-receipt/no-partition API keeps its conservative whole-target-root
requirement. Query success is not an asynchronous admission lease; controlled
qualification and recording remain on the existing GPU/cache thread.

## Immutable composition and actual submission evidence

A new immutable graph points separately to its source-copy ancestry and old
destination-copy ancestry. Covered byte queries project through the source;
uncovered queries project through the old destination. Neither old graph is
modified or compressed into a single root. Root IDs always refer to the original
anchor graph, not the new copy's local command index.

The two dependencies must lie on one existing bounded actual submission spine.
The newer dependency must contain the other as an ancestor; independent branches
are refused. Structural depth helps choose a candidate, but never proves GPU
execution order. The worker requires the actual immediately preceding accepted
checked submission on its owned graphics `VkQueue`. Completed qualification
checks every queue/predecessor, dependency identity, emitted entry/release barrier,
emission and completion edge back to the anchor. An intervening unchecked submit
or unsupported queue cannot qualify. No semaphore/cross-queue model is added.

One real `vkCmdCopyBuffer` performs the exact replacement, with the existing real
entry/release barriers and checked submission. Recording conservatively revokes
overlap on earlier snapshots of that allocation. The new receipt arms its target
range once, using the verified old complement plus copied bytes. Failure never
rearms an old receipt or manufactures successful completion. Native pins remain
retained through queued host work, real queue attempt and trusted device-idle
retirement; uncertain effects remain quarantined.

The four-hop and 16-retained-receipt limits remain. Source and destination may
both reference earlier receipts only when the actual submission-spine constraints
hold. This is bounded composition of existing records, not a new content authority
or coordinator.

## Example and cancellation

Start with A=99, B=10, C=20, D=30. A→B replaces `[16,32)`. B→C copies `[8,40)`,
so C contains 20/10/99/10/20. A later D→C replaces `[12,28)`, crossing two old
roots. C must retain:

| Interval | Original root/value |
| --- | --- |
| `[0,8)` | C / 20 |
| `[8,12)` | B / 10 |
| `[12,28)` | D / 30 |
| `[28,32)` | A / 99 |
| `[32,40)` | B / 10 |
| `[40,64)` | C / 20 |

Later source/intermediate writes do not rebase already copied downstream history.
A later target write cancels only its overlap; other current byte ranges survive.
Generic Unknown recording, uncertain submission, stale mapping, storage/domain
retirement and escaped host pointers remain conservative exclusions. Immutable
planned/history metadata remains inspectable after current eligibility is lost;
it is never a statement of global physical authority.

## Verification

The old implementation's expected refusal is retained in
`.diagnostics/m4-destination-red.log`. The actual-production-body simulated fixture
adds 51 mixed-target modes: distinct left/right roots, replacement across roots,
partial/full replacement, stale covered bytes versus stale/Unknown complements,
gaps/tails/overlaps, wrong projections or bindings, pending/failed submissions,
missing earlier/final barriers, remap, retirement, borrowed declarations, writes
after recording, source+target receipt composition, four hops and fifth-hop refusal.

An independent byte-label oracle adds 256 prefixes with shifted source/destination
positions, original root projections and immutable previous snapshots. Standalone
controls reject unknown left/right complements and test the 64-piece budget.
Mutation controls remove current-complement checks, partition guards, owned
declarations, target-history completion/identity and the target hop limit; each
must fail by assertion rather than compilation error.

The native Vulkan case uses five private unregistered staged allocations over
physical aliases. A separate per-byte value/root array applies copies independently
of production ancestry. Real C readback after mixed-target replacement and E
readback after a fourth-hop copy are compared with it. Later D/C writes test
partial current revocation and unchanged downstream bytes/history. Async target
retirement tests retained native lifetime through trusted idle.

Source `0777f7f3273d02555dd1e91d6ed149c653dd7efd` passed
[Linux-only workflow 39095898124](https://github.com/Splaser/emulator/actions/runs/39095898124)
on 2026-10-18. CLI, shader tool and production test targets built, and all five
CTest processes passed. Native Vulkan provenance passed 280 assertions in CTest
and three additional independent runs, including the real mixed-target/fourth-hop
readbacks and asynchronous target-retirement case above.

The Linux extracted-production-body suite passed all 51 new mixed-target modes
alongside the 49 one-hop, 44 multi-hop and 46 mixed-source modes. All 59 mutation
controls failed by assertion as required; their logs plus the positive fixture
log total 60 files. The 19 extracted input SHA-256 hashes match the committed Git
blobs. The standalone independent oracle passed 512 content, 2048 eligibility,
256 multi-hop, 256 mixed-source and 256 new mixed-target prefixes. The full
registration/coherency/backing/lifecycle suite also passed. The deterministic
failure/synchronization/remap mutations simulate dispatch/device execution; they
are distinct from the successful real Linux Vulkan readback evidence.

Downloaded workflow artifacts, exact-source manifest, mutation logs, native
repeat logs and complete Linux job output are retained under
`.diagnostics/ci-39095898124/`. Local red and positive/mutation logs remain under
`.diagnostics/m4-destination-*.log`.

The original ordinary F3 fixed-required witness still fails both aliases
(`expected=99 observed=10`, exit 42); F2 still reports
`expected=77 observed=10 backing=77`. Android was intentionally skipped. F2/F3
remain Open; controlled success does not close those production defects. The
following evidence-only documentation commit does not repeat source CI.

## Simultaneously mixed source and target (2026-10-18)

This acceptance slice uses the existing implementation unchanged. The native
test now exercises both receipt inputs on its fourth actual same-queue copy:

1. The anchor fills A/B/C/D/E with 99/10/20/30/40, then copies A `[0,16)`
   into its disjoint `[16,32)`. Those bytes now project to original root offsets
   `[0,16)`, rather than allocation offsets `[16,32)`.
2. A to B `[16,32)` creates a mixed B; B to C `[8,40)` creates a mixed C.
3. D to C `[12,28)` changes real C bytes while preserving its mixed complements.
4. Mixed B to mixed C `[12,36)` uses the first source receipt and third target
   receipt. Their exact three-piece source and four-piece complement partitions
   are verified on the same retained submission spine. The new receipt is depth
   four; a fifth hop must refuse before recording.

An independent per-byte value/root/offset oracle applies the seed and copies
without querying production provenance to construct expectations. All 64 bytes
must have the expected current root/projection; deliberately wrong roots and
offsets must reject. Actual Vulkan C readback must match all copied bytes and
both untouched complements. Later B writes leave captured C history/content
unchanged; later C writes revoke only overlap. Previous ancestry remains intact.
The asynchronous native target-retirement case also supplies both mixed source
and target receipts, retaining the actual allocation until trusted device idle
while refusing current qualification after retirement.

The extracted-production-body suite adds 40 simultaneous-mixed modes. Both source
and target partitions have independent tail/gap/overlap/Unknown/projection
controls, including stale first/middle/last source pieces and stale left/right
complements. Stale overwritten target bytes may be replaced. Failed or pending
target history, failed new submission, missing source/target/final barriers,
broken queue linkage/identity, remap before admission/worker/after completion,
retired source/target/ancestor, and borrowed declaration lifetime are checked.
Two extra mutations flatten nonidentity source projection or bypass old-target
ancestry; both must fail by assertion. These failure injections simulate Vulkan
dispatch/device execution; native success uses real Vulkan copy and readback.

Source `9bf69055a71b9c73cefa267d8ead66184520bac9` passed
[Linux-only workflow 39101068176](https://github.com/Splaser/emulator/actions/runs/39101068176)
on 2026-10-18. Production targets built and all five CTest processes passed.
Native Vulkan provenance passed 285 assertions in CTest and all three independent
repeat runs. This includes the real double-mixed fourth-hop Copy/readback,
nonidentity root projections, untouched complements, fifth-hop rejection, later
writes and asynchronous mixed-target retirement described above.

All 40 new simultaneous-mixed deterministic modes and all prior fixtures passed;
all 61 mutations failed by assertion as required. All 20 extracted input SHA-256
hashes match committed blobs, and the positive/mutation logs total 62 files.
Existing standalone byte oracles and the complete registration/coherency/backing/
lifecycle regressions passed. Downloaded artifacts, extraction manifests,
mutation logs, native repeat logs and complete job output are retained in
`.diagnostics/ci-39101068176/`; local focused and extra-mutation logs remain in
`.diagnostics/m4-combined-*.log`.

Original F3 fixed-required mode still fails both ordinary aliases (expected 99,
observed 10, exit 42); F2 still reports expected 77, observed 10, backing 77.
Android was intentionally skipped. Cross-queue remains Unknown, no ordinary
BufferCache selection or physical latest-writer authority is added, and F2/F3
remain Open. The following documentation-only evidence commit does not rerun
unchanged source CI.
