# M2 pure-memory model: scope and reference oracle

Status: design only. No production state machine, Buffer Cache integration or
large interval coordinator is introduced with M1. Existing DESIGN.md remains the
full contract; this document narrows the first executable model's proof surface.
Use M1 device observations to choose implementation limits later, without letting
sampling determine correctness semantics.

## Separate address geometry from authority

Geometry input is immutable `(backing epoch, physical byte range)` plus mapping
stamp/provenance. Two GPU VAs/DAddrs in different managers can refer to that same
range. Mapping generations invalidate addresses, not physical data versions.
The model consumes resolved ranges; it does not emulate GMMU, resolver locking,
Vulkan/NCE or authorize real dispatch from a validated stamp.

Start with a small byte array and per-byte reference metadata. No production
interval tree until this oracle passes. Replicas contain bytes and version tags;
producers have distinct lifetime IDs even when cache slots are reused. Versions
are causally ordered by accepted operations, not global submit/completion ticks.

For each byte retain committed authority and any reserved/pending GPU producer.
Track reservation, recorded, submitted, completed and CPU-visible separately.
A later completion cannot supersede a newer causally accepted write. Chunk
CPU/GPU access state is separate from per-byte authority; memory type/coherency
may affect visibility obligations, never which bytes are latest.

## Commands and preconditions

1. Resolve/register alias: geometry only; no authority update.
2. Begin GPU read/write: controlled-access reservation; select latest producer,
   forbid stale CPU replica upload. Record transfer plan without committing data.
3. Submit/complete: completion satisfies only its operation's dependency, not
   every earlier/later range or a different producer.
4. CPU write begin/commit: pre-store arbitration is separate from post-store
   publication. Commit only actual written bytes. Unsupported uncontrolled raw
   access rejects admission; don't pretend a pre-store fault observes final data.
5. CPU read/download: wait for relevant producer; commit only successfully copied
   bytes and revalidate the authority version before dirty-clear/publication.
6. DMA: snapshot the whole latest logical source before any destination update.
   Stable-mapping owned source semantics from M0 are distinct from establishing
   latest-producer freshness.
7. Remap/unmap: invalidate address permissions; do not fabricate a CPU write or
   reset physical authority. Preserve producer lifetime for surviving aliases.
8. Evict/renderer drain: retain latest producer until ordered materialization or
   safe ownership transfer completes. Old-device producers cannot survive device
   destruction; inert geometry stamps don't pin usable data/resources.

Failure injection must distinguish admission failure (no work/no data update),
partial completed transfers (publish only those bytes), and unknown GPU completion
(quarantine/drain or stop; never silently use stale CPU bytes). Unsupported same-
dispatch independent writable aliases reject before execution. Sparse/repeated
page shader storage is not made safe by metadata alone.

## Minimum deterministic tests

| Scenario | Required oracle result |
| --- | --- |
| CPU10, GPU99, surviving alias after unmap | latest remains 99; mapping removal isn't CPU dirtiness |
| GPU99 on full range, actual CPU77 on subrange | CPU subrange=77, untouched GPU bytes=99 after reconciliation |
| CPU77 committed before later GPU99 | dependency-obeying later GPU write=99, no stale download resurrects 77 |
| Two DAddrs/managers, one physical byte | all admitted reads observe same latest authority |
| GPU A99 completed late after later B77 | A completion does not erase B authority; B submission must depend on required A work |
| Partial readback while other GPU write pending | only downloaded current-version bytes become CPU-visible/clean |
| Producer eviction during pending GPU write | lease remains until completion plus transfer, or operation stops safely |
| Old renderer drain, same backing epoch next renderer | no dead-device producer; preserved CPU bytes remain authoritative |
| DMA physical overlap, split/repeated source pages | byte-array whole-source snapshot oracle; destination stores occur afterward |
| Same-dispatch scratch/image independent writers | reject before either writes; never manufacture fallback by skipping scatter |
| Noncoherent host-visible chunk | completion, invalidate/flush and START/END obligations modeled explicitly |
| CPU/NCE uncontrolled raw store | fail coverage/admission gate; no claim of universal coherent direct mapping |
| Remap between validation and admission | stamp validation alone cannot authorize operation |
| Cache slot/FD reuse, different allocation epoch | different producer/backing identity; never resurrect old authority |

Add seeded random sequences against the byte-array reference, asserting the
12 DESIGN.md invariants after every transition. Preserve deterministic seeds and
minimal failing sequences. Tests should exercise reservations and failures, not
merely mirror an interval implementation. No performance claims from this model.

## Use M1 evidence without conflating domains

Collect resolver queries/preparation, total/max timing, stamp invalidation rates,
range/alias coverage loss and cross-manager/DAddr witnesses. M1's current bounded
window is historical and excludes CPU/NCE/raw producers; lack of witnesses cannot
justify eliding reservations, lifetime/coverage gates or correctness fallbacks.
First decide whether cached resolver queries and a simple small-range model are
adequate. Optimize indexing only after measured distributions justify it. M2
alone cannot enable per-chunk DirectGuestView or integrate Buffer Cache behavior.
