# M4-F3 staged byte-range current eligibility

This extends the [whole-snapshot gate](M4_F3_CURRENT_CONTENT_GATE.md) with bounded
per-pin eligibility intervals. It does not change historical ancestry, import
cross-submission content roots, reconcile replicas or publish physical authority.
F2/F3 and M3B restricted production eligibility remain Open/blocked respectively.

## Cancellation contract

`CurrentTransferContentRange(transfer, memory, buffer, offset, size)` uses a buffer
index and offsets relative to that snapshot's retained staged pin. The nonempty
interval must be wholly in bounds and wholly eligible. It retains the old checked
completion, intra-batch dependency, immutable mapping and live membership gates.
An eligible interval can still contain Unknown provenance; callers must separately
prove its exact content origin. Returned pointers remain immutable historical
metadata across subsequent operations, not concurrent admission leases.

A non-null range query qualifies only the supplied bytes, never other intervals
in the returned historical graph. `CurrentTransferHasRoot` combines current range
qualification and exact historical root/projection checks for the same buffer,
offset and size, returning a boolean instead of a wider historical pointer.
Mixed roots, Unknown bytes, wrong root offsets and stale middle bytes all reject.
The root's command index is local to this immutable transfer graph, not a data
version or an ordering relationship. This point-in-time check is not a lease or
physical latest-writer publication.

The whole-snapshot query requires the full interval of every retained pin. Both
queries return the original graph; revocation never edits roots, copy parents,
physical origins, fill values or historical coverage.

Typed `RecordBufferFill` and `RecordBufferCopy` bind destination cancellation to
the actual command and its captured region list. BufferCache's ordinary clear,
copy, upload and readback route through these typed emitters, preserving their
existing barriers and command-buffer choice. Small copies of up to eight regions
retain inline storage. Larger lists own their regions until worker recording.
There is no public generic callback that can claim an arbitrary exact footprint.

Before enqueueing, the destination VkBuffer is matched against retained live
staged pins. Native offsets are intersected with the pin and translated to pin
offsets. Only the destination intersection is removed. Copy source ranges,
untouched complements, disjoint native offsets and other staged allocations
remain eligible even if their physical backing aliases. This qualifies individual
representations, never decides which alias is latest. Controlled transfer batches
use the same exact cancellation before recording and arm only their new snapshot.
Worker completion never rearms an older snapshot.

The interval set has at most 64 valid spans per pin. Fragmentation overflow fails
closed for that entire pin; it never retains a falsely complete prefix. Zero or
overflowing write footprints cancel conservatively. Zero-size, out-of-bounds and
overflowing query ranges are rejected.

Typed memory barriers (including WFI and copy pre/post barriers) retain eligibility:
they record only the actual barrier and its synchronization observation, no buffer
write or content-root import. Their real Vulkan stage/access scopes are unchanged.
Generic `Record`/`RecordWithUploadBuffer`, unknown shader paths, unclassified state
commands and deferred render-pass work still cancel the whole snapshot.
Their effects are not inferred from shader bindings or intent revisions. Typed
readback only changes its staging destination, so it preserves the source snapshot
unless surrounding unclassified work intervenes. Retirement/remap retain their
strict conservative gates, including original mapping generation and membership.

An actual queue error or uncertain submission callback latches a separate atomic
failure-domain rejection. It cannot be cleared by a later successful status or
completion. Ordinary device-loss reporting and exception propagation remain
unchanged; ranges are never mutated by the worker. Historical graph storage and
the original completed-event checks retain their prior meanings.

## Evidence and boundaries

The initial range query delegated to the old whole-snapshot check. A real-body
controlled Fill of `[16,32)` made its untouched `[0,16)` query fail; the expected
red assertion is retained in `.diagnostics/m4-range-gate-red.log`.
An additional negative control exposed typed memory/copy barriers going through
generic recording and incorrectly cancelling complements; it is retained in
`.diagnostics/m4-range-barrier-red.log` before the typed barrier enqueue fix.

Focused actual-body controls exercise partial Fill/Copy, independent aliases,
nonzero native pin offsets, upload regions, multi-region copy, source preservation,
pending snapshots with later writes, partial recording failures, device-loss
rejection, bounds and immutable history. Vulkan dispatch and execution in these
fixtures are simulated. The fake queue now retains all accepted submissions in
order until completion; it previously replaced earlier uncompleted work, which
was exposed by the new pending-snapshot case.

The standalone independent byte oracle verifies 2,048 eligibility prefixes in
addition to 512 content-origin prefixes, plus explicit fragmentation exhaustion.
Mutation controls remove overlap, native allocation, pin offset, submission
failure, actual typed Fill/Copy and left/right complement guards separately.
Native Vulkan tests perform actual partial Fill/Copy/readback and compare both
untouched bytes and query outcomes while retaining historical origins.

The source-only initial implementation of `CurrentTransferHasRoot` failed the
new stale-interval control despite retaining the historical root. The expected
red assertion is retained in `.diagnostics/m4-current-root-red.log`. The final
implementation requires both current eligibility and that exact root/projection
on the same interval. Its separate mutations remove each gate independently.

Final source `3bf13ca26195e0507af12942f2b6560c45c3f19d` passed
[Linux-only workflow 39019720456](https://github.com/Splaser/emulator/actions/runs/39019720456)
([Linux job](https://github.com/Splaser/emulator/actions/runs/39019720456/job/117358949052)):

- All five CTest processes; native provenance 177 assertions and three repeated
  processes using actual ordinary runtime clear, pre/copy/post barriers and
  readback. Mixed sources, historical-but-overwritten bytes, Unknown roots and
  attempted whole-range promotion reject; qualifying complements pass.
- 46 actual-body transfer-content cases, 27 expected mutation failures, 512
  content/2,048 eligibility oracle prefixes, and full coherency/lifecycle tests.
- Original F3 require-fixed still rejects two stale aliases (exit 42); original
  F2 still exposes backing 77/cache 10. Neither production defect is closed.
- Android was skipped intentionally. This commit is validated on Linux and makes
  no Android compilation or device/game acceptance claim.

All 12 extraction input hashes match committed Git blobs. Source stamp, complete
job log, native repeat results, generated fixture source, hash manifest and 28
fixture logs (one positive, 27 expected failures) are retained under
`.diagnostics/ci-39019720456/`.

The interval/barrier baseline `cc055d3426` had already passed the full Linux job
in [39018695234](https://github.com/Splaser/emulator/actions/runs/39018695234),
including 170 native assertions and 25 mutation failures; retained logs/hashes
are under `.diagnostics/ci-39018695234/`. Its separately running Android job was
not used as a gate for the current-root follow-up. The initial `34736ad618` run
[39018402186](https://github.com/Splaser/emulator/actions/runs/39018402186) was
cancelled/superseded after the typed-barrier negative exposed complement loss.
It is not acceptance evidence. The final documentation-only update does not
repeat identical production-source validation.

Cross-submission/queue roots remain Unknown in this slice. These controlled
checks do not constitute reconciliation or ordinary production F3 acceptance.

Follow-up validation defaults to Linux compilation, unit tests and relevant
native regressions. Android compilation is requested only for Android-specific
changes or risks Linux cannot cover. Manual workflow dispatch defaults to
`linux-only`; `full` remains an explicit option when Android evidence is needed.
The prior running Android job does not block subsequent work and is never treated
as successful while pending.

## Development stages

1. Close provable content provenance: exact transfer coverage, current byte
   eligibility, and eventually explicit cross-submission source transfer under
   checked execution and real synchronization evidence. Unsupported shader and
   queue relationships remain Unknown. This range slice alone does not close
   this stage or infer cross-submission roots from preserved current ranges.
2. Implement replica reconciliation: perform actual buffer copies with proven
   dependencies and dirty-complement preservation. For A writing `[0,64)` and B
   writing `[16,32)`, preserve A's complements and apply B only where proved.
   Remap, retirement and failed submissions must prevent stale publication.
   A derived graph alone is not reconciliation acceptance.
3. Close the original ordinary F3 witness. Controlled M4 mechanism acceptance and
   production F3 repair must be reported separately. Missing producer exclusion
   in M3B restricted eligibility remains an explicit production blocker; no
   ordinary Memory::Write/UMA routing is enabled to bypass it.
