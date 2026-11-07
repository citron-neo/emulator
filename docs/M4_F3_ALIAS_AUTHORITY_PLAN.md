# M4 F3: staged alias content authority

This is the bounded follow-up design to the native witness, not a production fix
or an activation certificate. F2 remains Open, restricted eligibility remains
blocked, ordinary scalar routing and selective UMA zero-copy remain inactive.

The [isolated admitted alias adapter](M4_F3_CONTROLLED_ALIAS_ADAPTER.md) is now
implemented and verified at production source `642a71fb07` (full Linux/Android
run `38903097142`), with test-only weak-owner follow-up `a415dea618` (Linux run
`38903908981`). It captures per-operation physical/native association snapshots,
rejects competing current dirty aliases, and performs checked exact staged copies.
Persistent write-site provenance/latest-writer ownership and ordinary routing
are still absent; the adapter does not close the original ordinary F3 witness.

Follow-up `d8764c0cf0` starts write-site metadata with a resource-local GPU intent
revision, rejecting same-dirty-shape rewrites between preparation and completion.
It does not establish a queue/submission content version or physical latest-writer
ownership. See the [revision boundary](M4_F3_CONTROLLED_ALIAS_ADAPTER.md#resource-write-intent-revisions).

The [submission-provenance slice](M4_F3_SUBMISSION_PROVENANCE.md) then links explicit
isolated staged fills to a real CheckedSubmission, native owner/footprint and
intent revision. It distinguishes queue acceptance from device completion and
skipped worker commands from completed writes. Ordinary shader enrollment and
physical latest-writer authority remain separate, unimplemented gates.

## Observed defect and validation boundary

At checkpoint `3a95a1e4fa`, a native Vulkan write of 99 through device address A
left independent staged aliases B (full-page overlap) and C (partial overlap)
at 10. Same-address reads returned 99; an explicitly retired/remapped B returned
55 with a different native payload identity. The original whole-executable run
timed out, so its observations alone were not clean test acceptance.

Diagnostic run [38896969297](https://github.com/Splaser/emulator/actions/runs/38896969297)
compiled the real production libraries and native tests. Its standalone GDB
`[f3]` invocation completed normally with 28 passing assertions. The combined
CTest process failed when creating a second complete System lifetime: the kernel
host dummy thread is initialized once in `KernelCore::Impl::GetHostDummyThread`
and retains its first Kernel reference in thread-local storage. CTest now runs
the original scheduler suite and explicit hidden F3 case in separate processes.
This preserves both native paths rather than resetting kernel internals in tests.

The repeat driver requires three normal standalone exits plus an explicit
`CITRON_REQUIRE_F3_FIXED=1` run whose only two failures are the full/partial alias
coherency assertions. A timeout, crash, failed positive control or teardown
failure is not an accepted negative control. The strengthened witness checks
whole pages, all 256 written bytes, both untouched complements, the disjoint
page and the retired/remapped payload. Native queries additionally reject B's
old physical plan after remap while accepting A's physically unchanged association
despite a global mapping-generation change. Final validation uses
`48d95a8ba3`, run
[38898475861](https://github.com/Splaser/emulator/actions/runs/38898475861);
it passed all CTest/integration/lifecycle gates. The 38-assertion F3 case passed
in CTest and three additional standalone processes. Fixed mode exited 42 with
only its two intended alias assertions failing, after normal guest teardown.
This is accepted defect reproduction, not fixed-mode acceptance.

This uses real process/device mappings, BufferCache and software Vulkan transfers.
Manual admission enrollment and the isolated test producer sequence are harness
operations. They do not enroll game shaders, NCE stores or mapping/allocator
mutations in production admission.

## Source-level cause

- `BufferCache::MarkWrittenBuffer` records GPU dirty intervals using DAddr in
  `memory_tracker`, `gpu_modified_ranges` and pending flush ranges. It does not
  publish the physical byte identity of the resource containing the new content.
- `FindBuffer` and overlap joining locate resources by DAddr. Disjoint DAddr
  allocations remain separate resources even if their physical pages overlap.
- `SynchronizeBuffer` consumes the target DAddr's CPU upload bits. A warmed alias
  has no new upload bits, so it can retain 10 after another alias has GPU value 99.
- Completing the existing physical range writer releases its parked reader but
  does not copy or select staged content. Ordering alone cannot repair F3.
- `PinExistingDmaRepresentation` checks current mapping geometry and retains the
  actual allocation. That establishes neither a historical content association
  nor which staged representation has the latest physical subrange.

## Smallest next implementation slice

Keep existing DAddr lookup and the existing epoch/range admission domains. Add
representation association metadata to the existing staged resource owner and
physical subrange authority to its existing GPU/cache owner, not another queue,
coordinator or replacement BufferCache.

1. Capture an immutable physical association for the exact staged footprint,
   using values/tokens from mapping snapshots. Backing materialization and
   resource pinning stay outside mapping locks. Bind it to the native payload
   identity, not a reusable slot id or bare VkBuffer handle.
2. Publish exact GPU-written subranges through the existing write sites with
   queue/submission identity. A bind-time write intent is not trusted completion.
   Later consumers need queue ordering or checked completion before content use.
   In particular, `RasterizerVulkan::Draw/DrawIndirect` can suppress a draw on the
   scheduler worker when pipeline construction failed after binding. A revision
   increment at binding cannot establish that a shader write was even emitted.
   `Scheduler::CurrentTick` alone also lacks the checked submit result/owner
   carried by `CheckedSubmission`; do not promote that tick to content authority.
3. Under existing admitted access, intersect producer and consumer physical
   segments, then build ordered staged-copy ranges with exact source/target
   offsets. Transfer only authoritative dirty bytes; preserve target complements
   and unrelated GPU dirty ranges. Read-only aliases also need this resolution.
4. Validate both retained associations and cache/resource identities before
   recording effects. Mapping equivalence must compare physical geometry, not
   reject every unrelated global generation increment. Remap to different
   backing, dead epochs and retired identities must reject the old authority.
5. Keep source payloads and authority through trusted completion. On failed or
   indeterminate submission, retain Unknown and refuse dependent use; do not
   relabel stale content, clear dirty ranges or replay as ordinary work.
6. First exercise this slice in the already isolated native footprint. A helper
   that manually copies A into B is only adapter validation, not a fix for the
   original ordinary-entry witness. Preserve that negative witness until actual
   ordinary producer/consumer paths are integrated and all exclusion gates hold.

## Required lifetime and integration checks

| Path | Obligation before activation |
| --- | --- |
| `HeapMapper::Map/Unmap` and destructor; NvMap handle unmap; device Map/Unmap | Serialize association mutation with admitted content effects. Device Unmap invalidates while old mappings still exist, but invalidation alone does not certify resource retirement or content transfer. |
| `JoinOverlap`, `DeleteBuffer`, GC, cache slot reuse | Transfer or discharge live dirty authority before retirement; retained old allocation ownership cannot qualify a new cache association. |
| `WriteMemory`, download/writeback, pending async flushes | Prevent old writeback from overwriting a newer physical alias producer. CPU changes must not discard untouched GPU complements. Existing controlled guards cover only a subset. |
| Gather/scatter, texture/render targets, direct imports | Audit actual write publication and consumers; unknown alias coverage is not disjointness. Do not silently treat these paths as enrolled. |
| Process teardown, frame return/reallocation, shared process mappings | Backing allocation lifetime is not frame-content lifetime. The allocator does not currently issue a general admission certificate; exact PAddr equality cannot rule out reuse/ABA. |

The adapter now covers byte-disjoint reverse writers within one page, exact
partial/full copies, third-alias/overlapping-writer refusal, pre-submit retirement,
post-submit remap with retained old pins, Unknown reader quarantine and unrelated
mapping-generation changes. Submission/counter/wait/invalidate/readback failures
have an actual-body fault fixture; they are not real device-loss execution.
Next native acceptance must cover choosing the correct latest version for
overlapping alternating A/B writers, merge/GC authority transfer and frame reuse
under actual production exclusion. Full/partial baseline stale observations and original F2 remain
separate regressions. No game, hardware Vulkan, Android or NCE execution is
claimed by the software Vulkan witness.
