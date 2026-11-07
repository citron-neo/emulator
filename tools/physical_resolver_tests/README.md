# Phase 3.5A: canonical physical-range resolver

Address identity only. No gameplay caller invokes this resolver; diagnostics are
test-only/on-demand, with no setting, periodic GMMU scan, import, memory write or
GPU command. Buffer Cache, dirty tracking, gather/scatter, DMA execution, shader
bindings, backing allocation, Phase 3 imports and retirement are unchanged.

## Translation audit and implementation

`MemoryManager::ResolvePhysicalRanges` (`src/video_core/memory_manager.cpp:63`)
takes the existing GPU page-table read lock and calls the new
`DeviceMemoryManager::ReadPhysicalMappingSnapshot` (device header, near line 144),
which takes the existing device mapping lock. All GPU and device lookups and
generation captures run with **both locks held**. The callback must not escape,
invoke Vulkan, allocate backing, invalidate caches, or wait for GPU work.

The GPU step uses the unchanged production `GpuToCpuAddress`: GMMU mapped entries
yield DAddr; reserved/free entries are holes. The device step inspects
`compressed_physical_ptr` and calls the existing
`GetPhysicalRawAddressFromDAddr`, whose stored page number is encoded as physical
page index + 1, with zero representing unmapped. It never calls `GetPointer`,
`GetSpan`, `EnsureBackingRange`, a published-chunk lookup or an exporter.

Existing scopes inspected:

- GPU Map/MapSparse/Unmap serialize updates via `mapping_update_guard`; PTE
  mutations execute under `AcquireMappingRead`'s mutex. Small/big `PageTableOp`
  increment `map_generation` before/after updates. Both contribute to the same
  whole-GPU-address-space counter; remapping Mapped to Mapped still increments it.
- GPU Unmap invokes preservation/invalidation callbacks before the PTE changes;
  no callback runs with that page-table lock held. Callback-side notifications
  are not a new data/ownership generation and are outside this resolver's scope.
- Device Map/Unmap change `compressed_physical_ptr` under `mapping_guard` and
  bracket changes with the existing `mapping_generation`. Allocation/free of
  DAddr ranges alone does not translate pages. TrackContinuity modifies only the
  pointer fast-path continuity hint; this resolver does not consult that hint.
- `ReadPhysicalAliases` already couples device alias queries under its lock.
  Buffer Cache's `ValidateGatherAliases` establishes GPU -> device lock order.
  This resolver and validation follow that same order. Existing mutation
  callbacks happen before acquiring the relevant PTE lock, not while holding
  device then requesting the GPU page-table lock.
- Backing has an independent `GuestBackingEpoch`: destruction/replacement marks
  the old epoch dead. Device managers capture the token and extent at construction,
  never query a potentially replaced HostMemory object during resolution.

There is **no additional mapping-generation counter**. The existing mutex and
atomic counter for each manager now live in one shared `MappingEpoch`; reference
members preserve the existing field names and all mutation sites. The owning
`MappingLifetime` invalidates its token in the manager destructor body, before
page tables are freed. Stamps retain tokens, not managers/page tables.

Existing GPU `GetID()` remains unchanged. Device mapping identity is the shared
lifetime token, with no new numeric ID counter or global address-space registry.

## Representation

`PhysicalRange` in `src/video_core/physical_memory.h` contains:

```cpp
u64 backing_epoch;
u64 physical_offset;
u64 length;
```

Offsets refer to canonical guest physical arena storage, not GPU VA, DAddr, fd,
host pointer, BufferId, VkBuffer, or renderer epoch. Equal offsets in different
backing epochs do not alias.

`MappingStamp` independently contains shared const GPU/device mapping lifetime
tokens, their captured generation values, the existing GPU ID for diagnostics,
and a shared const backing epoch token plus backing extent. The backing token
is **identity/invalidation only**: it does not pin arena allocations or a chunk
lease. No allocation availability or CPU/GPU ownership is inferred.

Each `PhysicalSegment` contains logical offset, length, an optional PhysicalRange,
and a hole reason (`GpuUnmapped` or `DeviceUnmapped`). ChunkIndex/ChunkOffset return
optional values, empty for holes. `PhysicalResolveResult` adds status, stamp,
original GPU address/size and the ordered segment vector.

## Algorithm and validity contract

1. Reject overflowing/out-of-address-space input. Zero size is valid, including
   an empty range at the address-space end, while still requiring live identities.
2. Reject dead mapping/backing epochs or odd generations; never publish partial
   success. Invalid physical bounds produce `InvalidPhysicalRange` and no segments.
3. Walk logical GPU bytes at page granularity. Limit every piece by GPU page edge,
   DAddr page edge, request end and, for mapped bytes, a 32 MiB physical chunk edge.
4. Preserve holes and repeated physical bytes in logical order. Never deduplicate.
5. Coalesce neighboring pieces only for proven physical continuity, equal backing
   epoch and the same chunk; holes coalesce only when their hole reasons agree.
   Different DAddrs can coalesce if their actual physical bytes are contiguous.
6. Recheck captured identities/generations/backing liveness while both mapping
   locks are still held. Only then return Success.

The 32 MiB boundary is a **logical partition** of physical storage on every
platform. It does not assert that a chunk exists, is dma-buf backed, importable
or eligible for a single-chunk GPU view. Materialization remains a separate API.

`ValidatePhysicalSnapshot(result)` locks retained GPU token then device token,
checks owner liveness and both generations, then checks backing liveness. It
never dereferences a manager or accesses page tables, and is safe after either
manager/backing owner has been destroyed. Destruction marks a token dead under
the same mutex before freeing manager data. Backing liveness is monotonic for
an epoch; the last successful check supplies the validation linearization point.

**Limits:** caller must keep the actual GPU/device managers alive for the duration
of Resolve (the ordinary reference-based API contract). Retained stamp validation
needs no owner lifetime protection. Simultaneously starting a raw-reference Resolve
on an object being deleted is unsupported; this is not an object reclamation
system. True validation means valid at that point, not a mapping-use lease after
locks release. A later consumer must re-resolve/revalidate and separately prevent
remaps during actual use. Do not invoke validation recursively while holding
either PTE lock or with the inverse lock order. No retries are hidden in this API.

Generation changes invalidate whole-address-space stamps even if the requested
range itself was unaffected. This conservative existing granularity is retained.
The resolver observes an actual pair of mappings at one instant, not necessarily
an application's intended multi-table update transaction. It does not merge such
transactions or change GMMU mapping semantics.

Replacing backing makes an old device manager's captured epoch permanently stale;
reconstruct that manager with the new backing instead of refreshing an old stamp
or trusting its old physical-base pointer. Physical offsets surviving a remap
retain the same physical identity if the backing epoch remains unchanged.

## Alias helpers and diagnostics

- `PhysicalRangesOverlap`: byte-range intersection, same backing epoch required,
  subtraction-based arithmetic avoids endpoint overflow.
- `PhysicalSnapshotsOverlap`: sorts mapped intervals and performs a two-way walk.
  Repeats remain in the resolver output. Holes do not intersect physical storage;
  a false result describes mapped portions only, not an ownership proof.
- `SameOrderedPhysicalStorage`: same length and ordered canonical segments, same
  backing epoch, no holes. GPU/DAddr/manager identities deliberately do not affect
  this physical equality test. Empty ranges compare only within the same epoch.
- `AnalyzePhysicalSnapshot`: segment/hole counts, exact repeated canonical segment
  count, count of segments overlapping earlier sorted physical coverage, contiguous
  chunk boundary crossings, GPU ID/generation, device generation and backing epoch.
  Overlap count also detects repeated pages inside differently coalesced spans;
  exact-repeat count is not a count of all physical pages or all overlap pairs.

These are historical geometry comparisons, not automatic validation or a claim
that any copy is latest. Test diagnostics demonstrate selected range resolution
and distinct GPU addresses resolving to the same physical identity. There is no
normal-gameplay invocation or logging.

Resolution costs O(P) page lookups and O(S) output memory for P translation pieces
and S coalesced segments. It allocates only metadata vectors/tokens. Overlap and
diagnostic analysis cost O(S log S), with per-call vectors, no per-byte iteration
and no global alias table. Validation is O(1) with two mutex acquisitions. There
is one shared mapping-state allocation per manager; existing mapping operations
use the same locks/counters, not additional hot-path resolver work.

## Validation

```sh
python tools/physical_resolver_tests/run_tests.py --cxx g++
# Linux, actual HostMemory; optional external boost header location:
python tools/physical_resolver_tests/run_cold_tests.py --boost-include /path/to/boost
```

The main runner compiles the actual production algorithm, lifetime state and
validation/alias/diagnostic helpers. It extracts production GPU Map/Unmap,
small/big PTE operations, entry access, address translation and resolver wrapper,
plus device physical decoding and read-snapshot function. Page-table containers,
device mapping mutations, GPU unmap subrange enumeration and rasterizer callbacks
are fixtures. It is not a full Core/System/GMMU integration or GPU execution test.

Coverage: single/multi-page coalescing, non-contiguous/repeated pages, GPU/device
holes, partial page endpoints, big pages, same/different GPU VA and DAddr,
different GPU and device managers sharing backing, partial/disjoint/epoch-mismatch
overlaps, cross-chunk splitting, GPU unmap/remap, device remap, owner destruction
including concurrent stamp validation, backing replacement, zero/overflow/range
errors, concurrent GPU remap/device mutation and deterministic blocking during
an incomplete device update. No independent generation samples substitute for
the paired-lock snapshot.

The Linux cold test uses production HostMemory with actual lazy anonymous
reservations, production resolver/device query and fixture device PTEs pointing
to a cold chunk. Chunk creation stays zero before/after, published lookup stays
empty, and actual HostMemory destruction invalidates the retained backing token.
Device fixture mapping does not prove the real CPU-process mapping path can
install a cold PTE without materializing memory; it tests resolver non-allocation.

Verified 2026-10-13:

- New resolver suite passes on Windows MinGW and Linux GCC; Linux cold tests pass.
- Linux resolver suite passes AddressSanitizer + UndefinedBehaviorSanitizer.
- All nine existing gather-buffer runners pass on both Windows and Linux,
  including binding aliases, CPU scatter/write, mapping snapshots, GPU stop,
  unmap write preservation, index ranges and legacy Vulkan import.
- Linux lazy backing/export suites and Phase 3 import/publication/lifetime suites
  pass. Phase 3 cache/backend suite also passes on Windows.
- Actual `memory_manager.cpp` and `host1x/gpu_device_memory_manager.cpp` translation
  units compile on Linux with isolated logging/assert/platform surface shims and
  actual manager headers. This is not a full build or application link.
- No local Android full build (user uses workflow). No Phase 3.5A device runtime
  test or Android compilation claimed; earlier APK validation was Phase 3 only.

No physical coherency state, DirectGuestView, DMA snapshot fix or Buffer Cache
integration is included. Stop at this infrastructure layer until reviewed.
