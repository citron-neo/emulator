# M4/F3: controlled same-queue replica copy

For the later mixed-target partial-copy extension, see
[M4_F3_MIXED_TARGET_COPY.md](M4_F3_MIXED_TARGET_COPY.md). The sections below retain
the scope and validation of the earlier increments.

This slice transfers exact content ancestry across two actual checked Vulkan
submissions. The original one-hop slice is recorded below; the bounded multi-hop
extension follows its validation record. It does not publish physical latest-writer authority, reconcile
ordinary game buffers, or close F3/F2.

## Admission and execution

The bounded case uses two different staged allocations initialized by one closed
controlled parent Fill/Copy batch. Two different DAddr bindings must name exactly
the same saved physical interval in the same `GuestBackingEpoch` and mapping
epoch. The source interval must have an exact completed, current parent root;
the entire target pin must have one exact completed, current parent root so both
untouched complements have explicit ancestry. Mixed/unknown complements fail.

Neither allocation may ever have exposed a writable mapped host pointer. This
sticky restriction closes a producer path that typed GPU range cancellation
cannot observe. The native witness uses private staged buffers outside ordinary
BufferCache registration/selection. Qualification and recording run on the
existing GPU/cache thread; these queries are not concurrent admission leases.

The consumer owns one actual `vkCmdCopyBuffer`, with an actual entry memory
barrier and release memory barrier. The worker requires the parent to be the
immediately preceding accepted checked submission on the actual owned graphics
`VkQueue`. An intervening unchecked submission clears that witness. This is
deliberately narrower than all legal same-queue dependency chains. Numeric ticks,
resource revisions and completion order do not establish the relation.

The entry barrier covers earlier transfer writes and the consumer's transfer
read/write; the release covers the copy before subsequent shader/transfer
accesses. Both emitted barriers are journaled and checked separately. Vulkan
pipeline barriers can scope commands across submissions in same-queue submission
order; submission order alone does not establish the required memory dependency.
See the [Vulkan synchronization specification](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
and [`vkCmdPipelineBarrier`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPipelineBarrier.html).
Different queues, absent/wrong scopes, incomplete journals and failed submissions
remain unproved/Unknown. Pending completion does not qualify as completed content.

## Immutable ancestry and current qualification

`ReplicaCopyProvenance` holds the immutable parent graph and exact copy geometry.
Root command IDs retain their parent namespace. The new local copy graph is not
relabeled as a new Fill root. For A = 99 and B = 10 on `[0,64)`, copying only
`[16,32)` proves B's left/right complements still originate from its old 10 root,
while the middle originates from A's 99 root. It cannot prove B's whole range
originates from A.

Admission rechecks current roots after leaving a render pass. Before emission,
the worker checks payload membership/retirement, escaped host pointers and the
captured epoch generation. The copy receipt retains pins through the real queue
attempt and trusted device-idle retirement. Remap never resolves the historical
event against a new backing. Current queries additionally check current mapping,
the submission failure domain and destination byte eligibility.

A later source overwrite does not change bytes already copied or their history.
A later target write revokes only overlapping current destination bytes; generic
unknown recording cancels current eligibility conservatively. Remap, dead backing,
retired allocation and newly exposed host pointers fail current qualification.
Historical metadata remains inspectable. A completed historical receipt is not a
statement about current physical authority.

At `d783250b0d`, only one hop from a local parent graph was admitted. Chained imported receipts,
nonadjacent unknown submissions, mixed-root whole targets, concurrent producers
and cross-queue semaphore/ownership chains are outside this slice.

## Verification

The retained initial negative witness is
`.diagnostics/m4-replica-sync-red.log`: history/bytes alone incorrectly accepted a
copy after entry dependency evidence was removed. The implemented gate rejects
that case even when simulated bytes look correct.

The actual-production-body simulated fixture has 49 replica modes. Controls cover
missing/wrong entry/release barriers, broken actual queue/predecessor evidence,
pending completion, failed parent/consumer submissions (including ordinary error
propagation), stale source intervals, unknown or overwritten complements,
geometry/resource substitution, remap before/after emission, escaped mapped host
pointers, post-copy source/target writes, and asynchronous retirement. Mutation
controls independently remove each critical guard or command and must fail by
assertion rather than compilation error. These are simulated-driver evidence.

The native Vulkan witness separately creates two private staged allocations over
physical aliases, submits parent Fills and a second checked Copy, waits for real
completion and reads back the middle plus both complements. It also checks later
source/target writes, remap and retained allocation lifetime.

Source `d783250b0d7cb1f79558566f00d544cb84248178` passed
[Linux-only workflow 39022879191](https://github.com/Splaser/emulator/actions/runs/39022879191),
Linux job `117368038043`:

- Full CLI/shader/unit build and all five CTest processes passed. The native
  provenance case passed 211 assertions, followed by three independent repeated
  processes with the same 211 assertions each.
- The 49 new replica modes passed alongside the previous 46 transfer-content
  modes. All 42 mutation controls failed by assertion as required. The 43 fixture
  logs and all 17 extracted input hashes were checked against the
  committed Git blobs.
- Standalone content/eligibility tests passed, including 512 content and 2,048
  eligibility oracle prefixes and the replica overlay cases. Full registration,
  physical coherency, backing-import and lifecycle regressions passed.
- Original F3 fixed-required mode still failed two alias checks with process exit
  42; F2 still reported `expected=77 observed=10 backing=77`. Controlled copy
  acceptance is separate from those open production defects.

Logs and downloaded artifacts are retained in `.diagnostics/ci-39022879191/`.
Android was intentionally skipped. Device/game acceptance is untested. The
following documentation-only evidence update does not repeat source validation.

## Bounded multi-hop extension

The next controlled slice accepts an explicit `source_receipt` together with the
original parent anchor. Root command IDs continue to refer to that anchor's
immutable local Fill/Copy graph. A receipt stores its actual immediate producer
separately from its root anchor. The immutable metadata retains the preceding
copy's ancestry; it never replaces an imported root with a hop-local command ID.

Every hop must copy from the preceding receipt's destination allocation and
exact saved physical binding. That receipt must prove the requested current
source bytes and original root projection. Each new target must still have a
whole-pin exact current root in the original anchor, including its untouched
complements. At `715d1097b9`, mixed-root source intervals and previously modified
whole targets remained unsupported. Model construction also rejects different anchor graphs,
incorrect source slots and a fifth hop. Four hops bound ancestry/storage walks;
the existing 16-receipt retention limit remains unchanged.

Each actual copy owns a checked submission with the existing entry/release
barriers. The worker still requires the immediately preceding accepted submission
on its actual graphics queue. Completed queries validate every edge back to the
anchor: actual queue/predecessor identity, successful completed emission and both
barrier scopes. Good evidence on the final edge cannot repair missing evidence
on an earlier edge. Cross-queue and intervening unchecked submissions remain
unproved; neither ticks nor completion order ranks content.

For A = 99, B = 10, C = 20 on `[0,64)`, A→B copies `[16,32)` and B→C copies
`[20,28)`. B retains 10 outside its copied interval; C retains 20 outside its
smaller interval. C's middle retains A's original Fill root, not B's local Copy
command. Copying B's untouched bytes can instead retain B's 10 root. No whole
replica becomes A's content merely because one interval was copied.

An overlapping B write before recording the second copy cancels source admission. An
overlapping B write after the second copy leaves C's captured history/current
bytes intact. Later C writes cancel only the corresponding current destination
interval; Unknown recording cancels all current eligibility. Historical graph
pointers remain unchanged. The current gate additionally walks ancestor storage
and mappings, conservatively rejecting retirement, escaped host mappings or
invalid epochs anywhere in the retained chain without rebasing byte ancestry.
Mapping-query exceptions reject eligibility. No ordinary BufferCache selection,
replica reconciliation policy or physical latest-writer authority is added.

The retained red control `.diagnostics/m4-chain-red.log` proves the old one-hop
implementation refused the required B→C handoff. The new deterministic fixture
adds 44 multi-hop modes covering exact roots/complements, prior versus subsequent
overwrites, missing earlier/final dependencies, both failed copy submissions,
pending completion, remap, async retirement, host exposure, Unknown recording and
the four-hop bound. An independent byte-label oracle checks 256 multi-hop prefixes
with interval projections and roots from copied or untouched source bytes.

The native Vulkan witness now uses three private staged allocations, reads back
B and C, checks later intermediate/destination writes and retains an asynchronously
retired intermediate allocation until trusted device idle. These controlled
mechanism tests remain separate from production F3 acceptance.

Source `715d1097b9c7fdeae269be11aa2bb7163f4572c2` passed
[Linux-only workflow 39025579635](https://github.com/Splaser/emulator/actions/runs/39025579635),
Linux job `117375791932`:

- Full CLI/shader/unit build and all five CTest processes passed. The native
  provenance case passed 233 assertions, plus three independent repeated
  processes with 233 assertions each.
- All 44 multi-hop modes, the previous 49 single-hop modes and 46 local transfer
  modes passed. All 46 mutation controls failed by assertion as required,
  including removed ancestor dependency/storage gates and the hop bound.
- The independent oracle passed 256 multi-hop prefixes, alongside the prior
  512 content and 2,048 eligibility prefixes. Full registration, physical
  coherency, HostMemory/backing-import and GPU lifecycle regressions passed.
- All 17 extracted input hashes match committed Git blobs; all 47 fixture logs
  are retained in `.diagnostics/ci-39025579635/`. The artifact's source SHA matches
  the validated commit. Original F3 still fails two fixed-required alias checks
  with exit 42; F2 still reports `expected=77 observed=10 backing=77`.

Android was intentionally skipped; no device/game acceptance is claimed. The
evidence update changes documentation only and does not repeat this source run.

## Mixed-source interval follow-up

A copy may now carry an explicit ordered partition of its source interval. Every
`RootSpan` names source-pin-relative bytes, an original anchor Fill ID and that
Fill's byte projection. The partition must cover the whole requested source
exactly: no holes, overlaps, empty pieces or outside suffixes. The bound is 64
pieces; absent declarations preserve the original single-root request semantics.
Unknown bytes cannot become known by supplying a declaration.

`ReplicaCopyProvenance::Build` validates every piece against the immutable local
graph or preceding copy's ancestry, then copies declarations into its own fixed
array. Admission checks the current qualification of every saved piece, both
before and after leaving the render-pass context. It cannot accept a valid prefix
while ignoring an overwritten suffix. Later caller mutation cannot change saved
declarations. The actual command remains one exact `vkCmdCopyBuffer`, tied to the
same geometry/native bindings; every previous queue/barrier/completion edge must
still qualify. The four-hop limit and ancestor storage/mapping cancellation stay.

For A = 99, B = 10 and C = 20, A→B copies `[16,32)`. B→C then copies `[8,40)`,
whose declared roots are B's 10 on `[8,16)`, A's 99 on `[16,32)`, and B's 10 on
`[32,40)`. C keeps its own 20 on `[0,8)` and `[40,64)`. Queries retain all five
intervals and cannot call the entire mixed range A's or B's single projected
root. Copying the resulting mixed C into further fresh targets preserves these
roots and the next target's untouched bytes.

Any source piece overwritten before admission rejects the whole copy. Known
source writes recorded after it capture later source state without rebasing the
earlier destination receipt. Later target writes revoke only overlap; Unknown,
failed/unconfirmed submissions, remap, retired storage and escaped host pointers
remain conservative exclusions. History remains metadata, not current/authority
publication. A target still requires one exact whole-pin current anchor root;
mixed/modified target complements and automatic reconciliation are not enabled.

The initial old-code refusal is retained in `.diagnostics/m4-mixed-red.log`.
The actual-body simulated fixture adds 46 modes, including overwrites in each
copied/untouched source part, overwritten undeclared tails and gaps, overlapping
declarations, incorrect root projections, pending/failed submissions, earlier/final
missing barriers, remap, retirement, escaped host pointers, borrowed declaration
mutation and mixed overlays through four hops. Mutation controls remove partition
and per-piece guards, and must fail by assertion. The independent byte-label
oracle adds 256 mixed-source prefixes with shifted interval/root projections,
Unknown-source and 64-piece capacity controls.

The native Vulkan witness copies the mixed interval into a third private staged
allocation, checks all 64 byte roots and the full readback, then overwrites copied
and untouched portions of B and a partial region of C. It also retains the mixed
copy's intermediate allocation across asynchronous retirement until trusted idle.
Cross-queue remains Unknown; ordinary automatic
selection, latest-writer publication and production F3/F2 closure remain excluded.

Validation (2026-10-18): source `c3cb37cfb18e79931e359c8dc362131ad7728bf3` passed
[Linux-only workflow 39093574539](https://github.com/Splaser/emulator/actions/runs/39093574539),
Linux job `117575230493`:

- Full CLI/shader/unit build and all five CTest processes passed. Native Vulkan
  provenance passed 241 assertions plus three independent repeated processes,
  each with 241 assertions, including the mixed readback and 64 byte-root checks.
- All 46 new mixed-source modes passed alongside 49 single-hop, 44 multi-hop and
  46 local transfer modes. All 51 mutation controls failed by assertion as required.
- The byte oracle passed 256 new mixed-source prefixes, 256 preceding multi-hop
  prefixes and the existing 512 content/2,048 eligibility prefixes. Full physical
  coherency, registration, backing import and GPU lifecycle regressions passed.
- All 18 extraction input hashes match committed Git blobs; the artifact source
  SHA matches this commit. All 52 fixture logs and downloaded artifacts are retained
  in `.diagnostics/ci-39093574539/`. Original F3 fixed-required mode still fails
  two alias checks (exit 42); F2 remains `expected=77 observed=10 backing=77`.

Android was intentionally skipped. No device/game acceptance is claimed. The
documentation-only evidence update does not repeat the same source validation.
