# M4 F3: exact transfer content and replica copy provenance

This slice records historical byte origins inside one explicitly requested,
bounded transfer callback. It is not a physical latest-writer selector and does
not synchronize replicas automatically, publish backing bytes, enroll ordinary
Memory::Write/UMA or repair F2/F3. Shader writes and imported cross-submission/queue
roots remain Unknown. Ordinary BufferCache copy/fill routing is unchanged.

## Exact command coverage

TransferContent accepts only typed Fill and Copy commands. Sizes/offsets describe
actual native bytes relative to retained staged pins. Unsupported kinds, empty,
unaligned, out-of-bounds or overlapping same-buffer copies refuse before recording.
Duplicate native allocations in the buffer table refuse. Bounds are eight buffers,
32 commands, 64 spans per buffer, 256 ancestry nodes and 16 retained checked
transfer records per Scheduler until trusted retirement. Metadata overflow refuses
the controlled request before GPU commands are recorded.

Scheduler::RecordTransferBatch owns the explicit Vulkan commands, records them as
one worker callback and immediately uses the existing ordinary Flush with a checked
submission token. Ordinary queue failures still report/propagate; the matching
content record becomes Unknown.
It refuses an already-observed host batch. It does not accept arbitrary callbacks,
binding ranges or a caller assertion that a shader wrote every byte. Every command
has a real global memory barrier recorded alongside its journal evidence. The
first barrier covers preceding same-queue work; subsequent transfer barriers cover
read/write dependencies within the callback. No other Scheduler command can be
interleaved inside that callback.

These explicit barriers/copies belong only to this opt-in transfer request. They
are not inserted into normal rendering, nor selected as a coherency policy.

## Immutable byte ancestry and untouched complements

Every buffer starts entirely Unknown, even when it already contains GPU bytes or
was produced by an earlier completed transfer. Fill creates a root containing the
local command identity, exact destination interval and 32-bit fill pattern. Copy
records source/destination allocation indices and offsets, and derives ancestry
from the source state at that command. Copies of copies retain the old root;
overwriting the source later cannot rebase an earlier copy.
The root's allocation/offset names its historical origin, not a place from which
those bytes can still be read today. After A changes to 77, B may retain copied 99;
reading A's current storage would not recover that earlier root.

Overlay replaces only the exact written interval. Both untouched complements keep
their previous nodes, or Unknown when no source was established. Roots identify
commands within this callback only; their numeric indices are not content versions
or a global execution order. The graph and its terminal spans are immutable.

For A filled with 99 and B filled with 10 over [0,64), copying A[16,32) to B[16,32)
produces:

| B interval | Historical root |
| --- | --- |
| [0,16) | B's fill of 10 |
| [16,32) | A's earlier fill of 99 at source [16,32) |
| [32,64) | B's fill of 10 |

HasRoot(B,0,64,A_fill,0) must refuse. If A is then filled with 77, B's copied bytes
still refer to the earlier 99 fill. If B was never initialized in the callback,
its untouched complement remains Unknown. WrittenBy describes exact command
coverage independently of whether the copied bytes have a known root.

## Execution, mapping and resource gates

PlannedContent and HistoricalOrigins are metadata, inspectable before execution.
They are never completed content evidence. CompletedTransferContent requires the
same Scheduler owner, live retained native pins/backing identities, successful
checked submission/timeline/host-wait evidence and recorded inter-command memory
dependencies. Pending, failed, partially recorded, sealed, dead or retired records
refuse. Execution uncertainty is sticky Unknown. Resources remain retained until
trusted device idle, completed host callbacks and a resolved queue attempt. Both
an unrun callback and recorded native commands awaiting a Submit callback must
keep their pins. A host setup failure that never attempts a queue submission stays
quarantined until teardown; a later idle cannot manufacture its submission identity.

TransferContentForMapping additionally checks every immutable origin against the
current mapping, including the original mapping generation. Remap/restore cannot
relabel an old source. A historical completed graph can remain inspectable after
remap; it cannot pass the mapping gate. Epoch metadata does not own guest arena
storage or grant backing-byte access.

Even a mapping-qualified graph describes contents at the end of that callback,
not the allocation's current contents after subsequent commands. It is never a
freshness/authority certificate. Later ordinary writes can make the historical
snapshot stale. No publisher or ordinary coherency consumer uses this graph.

## Validation and evidence boundaries

The placeholder builder initially refused all requests; the valid partial-copy
test failed. The implemented header passes exact-copy/complement, source overwrite,
copy-of-copy, Unknown-byte and malformed-footprint cases, plus 512 deterministic
command prefixes checked against an independent per-byte root-label oracle.

The actual-body fixture runs 23 cases through production transfer recording,
checked submission/completion, synchronization and current-mapping gates. Vulkan
dispatch, device execution, mapping snapshots and allocations are simulated. It
covers failed submission/counter/host wait, partial copy recording, remap/dead
backing, unqualified/aliased origins, invalid geometry/capacity, pending work,
host-submit failure, foreign owner, dropped cache owners, erased sync evidence and
different nonzero native pin offsets. Copying from an Unknown source erases the
destination's previous known origin for that exact interval; it cannot preserve
the overwritten root as a fallback.
The pre-submit retention negative first reproduced early retirement after native
recording but before the queued Submit callback. The queue-attempt/result guard
must prevent that retirement even though the recording callback has finished.
An already-observed ordinary batch refuses transfer enrollment while its original
command still executes exactly once.
A second completed submission copying real bytes from a prior submission still
has Unknown roots; it cannot import that prior graph as provenance.

Nine content mutations must fail: bypass completion, bypass mapping validation,
remove actual CopyBuffer, remove actual FillBuffer, bypass synchronization evidence,
release pending host resources, promote mixed roots as a whole replica, discard
the left complement, discard the right complement. Expected failure logs are
retained separately from positive results. The previous five synchronization
mutations and original F2/F3 require-fixed negatives remain required.

The native [f3-provenance] process adds real staged replicas, explicit Vulkan
Fill/Copy/barriers/checked completion and readback of 77 versus mixed 10/99/10.
It verifies historical roots despite source overwrite, refuses whole-replica
promotion, refuses cross-submission root imports and rejects the current-mapping
gate after remap. These are controlled transfers, not real shader/game execution
or target-device acceptance. Android is compiled only through the GitHub workflow.

## Verification record

Final production/test source `4a3f9a6aba` passed the complete
[workflow 39012415113](https://github.com/Splaser/emulator/actions/runs/39012415113).
Both Linux production and Android native jobs succeeded. All five Linux CTest
processes passed: 130 ordinary cases, 1708 scheduler assertions, 38 original F3
assertions, 88 isolated alias assertions and 149 native provenance assertions.
Three additional native provenance processes each passed all 149 assertions.

The actual-body fixture passed all 23 content cases, the earlier sync cases and
all 14 expected mutation failures (nine content, five synchronization). The
independent byte oracle passed all 512 command prefixes. The combined registration,
ordinary Draw/Dispatch, coherency/lifecycle and ownership regressions also passed.
F3 require-fixed still produced exactly two alias failures, 36 passing assertions
and exit 42. F2 still reported backing 77/cache 10 and require-fixed exited 1.

Android compiled actual arm64 core/video/shader libraries through CPM/NDK 29.
It is library compilation, not APK, shader/game or target-device acceptance. No
local Android compilation was performed.

The initial `f682e086e9` workflow
[39012197470](https://github.com/Splaser/emulator/actions/runs/39012197470) was
cancelled when the deterministic pre-submit retirement control exposed a resource
retention gap. It is superseded, not acceptance evidence. `4a3f9a6aba` adds the
queue-attempt/result guard and its regression before the final full validation.

Downloaded source stamp, full job logs, LastTest.log, generated actual-body inputs,
hash manifests and separate expected-failure logs are retained under
`.diagnostics/ci-39012415113/`. CI extraction input hashes match the committed Git
blobs (Windows working-tree CRLF bytes are not compared to Linux LF bytes).
The final handoff follow-up changes only documentation and does not repeat the
same production validation.

## Next boundary

The follow-up [current-content gate](M4_F3_CURRENT_CONTENT_GATE.md) adds separate,
conservative Scheduler-local cancellation without changing these historical APIs.

Do not start physical latest-writer publication from this slice. Broader producer
enrollment, shader exact coverage, cross-submission/queue synchronization, external
initial-content roots, ordinary transfer observation and broader current-content lifetime
validation remain separate work. No revision/tick ordering substitutes for them.
