# M0: owned source snapshot for CPU DMA fallback

Baseline: `175b737c13`. Design and original borrowed-span audit remain under
`tools/physical_coherency_tests`; that audit is historical and still demonstrates
generic GuestMemory span behavior, not a failure of this corrected call site.

## Corrected path

`MaxwellDMA::Launch`'s single-line pitch/pitch fallback after accelerated BufferCopy
returns false now calls `CopyDmaSourceSnapshot`. The helper:

1. Returns immediately for zero length, leaving Launch's normal semaphore release.
2. Uses the read-only physical resolver to reject known invalid/overflowing or
   hole-containing source/destination ranges before DMA writes or source reads.
3. Resizes the engine's reusable owned ScratchBuffer.
4. Calls the existing safe ReadBlock to flush/read the entire logical source into
   that owned storage, including split mappings.
5. Only after the read completes calls existing WriteBlockCached for destination.

Initial pages 10/20/30, source pages 0/1 and destination pages 1/2 now produce
10/20, rather than clobbering the later source and producing 10/10. Independent
per-piece memmove would not fix that original topology; full-source ownership does.
Different DAddrs referencing the same physical bytes are equally protected once
the source bytes are available to ReadBlock. Repeated destination pages receive
the existing CPU write path's logical-order stores from the fixed source image.

The accelerated path is unchanged and does not allocate/resolve a CPU snapshot on
success. Common GuestMemory::Read/GetSpan behavior is unchanged for other users.
The engine retains reusable storage rather than allocating a new array every copy.

## Failure and validity boundaries

Known holes/invalid ranges reject through the existing UnsupportedGatherMapping
GPU resource-failure stop mechanism, with a DMA-specific message; there is no
new scheduler catch or silent skip/success path. This deliberately does not
zero-fill an unmapped source or partially write a hole-containing destination in
this fallback. Such DMA operations require explicit semantics before support.

Owned allocation or source ReadBlock failure occurs before any DMA destination
store. Existing source cache flushing may itself reconcile earlier GPU writes
into guest backing; that preservation is not a DMA destination write. Tests use
already-current CPU bytes and no-op cache callbacks when comparing RAM unchanged.
Destination-write failures are not promised transactional rollback; current block
write/dirty publication semantics remain unchanged. The existing GPU failure path
stops emulation on supported allocation/resource exceptions.

**Resolver preflight is not a mapping-use lease.** The two range snapshots and
the later source/destination operations are not one locked mapping transaction.
Concurrent remap, arbitrary concurrent CPU stores and cross-DAddr latest-producer
freshness remain Phase 3.5B/M3 integration work. No mapping lock is held across
GPU flush/wait. This commit establishes source-before-DMA-destination semantics
for stable mappings using bytes provided by the existing read/flush path; it does
not claim a Physical Coherency Coordinator or universal DMA correctness.

Multi-line copies, pitch/block-linear 16-byte conversion loops, tiled/swizzled
DMA, accelerated buffer copies and separate MM/address-space DMA command semantics
are unchanged. This is the independent fix for the confirmed single-line fallback,
not a redesign of every DMA operation. There are no Buffer Cache, dirty tracking,
NCE, ownership, import, eight-frame retirement or mapping mutation changes.

## Tests and integration evidence

```sh
python tools/dma_snapshot_tests/run_tests.py --cxx g++
```

The runner compiles the production snapshot helper and extracts the actual
MaxwellDMA fallback branch, MemoryManager GPU translation/resolver wrapper,
MemoryOperation, ReadBlockImpl/ReadBlock and WriteBlockImpl/WriteBlockCached, plus
device physical decoding/snapshot functions. Page-entry storage and entry-type
selection, device maps, cache callbacks and accelerator result are fixtures;
there is no Core/System/GPU execution or full MaxwellDMA register-engine test.

Coverage:

- Exact original 10/20 overwrite case, overlap both directions, same/different DAddr
  aliases, partial byte ranges and first/last pages.
- Split source/destination, physical swap, repeated source/destination pages, disjoint
  mappings and source data preserved for the entire copy.
- Production big-page translation and block read/write paths on contiguous physical fixtures.
- Production acceleration-success branch skips the helper and leaves snapshot
  storage/read/write callbacks untouched.
- Owned capacity reuse, zero-length no-op, GPU holes, device holes, invalid bounds
  and overflow, with no DMA output on rejection.
- Injected owned-array allocation failure and second-page source-read failure:
  no destructor-driven destination copy and no DMA write on failure.

Validation on available Windows/Linux tools:

- New suite passes Windows MinGW and Linux GCC; Linux ASan/UBSan passes.
- Existing nine gather-buffer runners pass Windows/Linux, including CPU-write,
  scatter, unmap preservation, binding aliases, mapping snapshots and index ranges.
- Physical resolver suite passes Linux; no resolver production behavior changes.
- Actual maxwell_dma.cpp translation unit compiles on Linux with isolated
  logging/assert/platform shims and actual manager headers; not a full link/build.
- No local Android full build; workflow build and real-device retest remain
  pending. No FPS/performance claim.

## Cost and rollback

Only CPU fallback incurs two resolver walks/metadata vectors and a whole-source
copy. ScratchBuffer storage is proportional to requested bytes and reused across
copies. An already-large engine buffer can retain its capacity as before. The
change introduces no new global GPU stall; safe ReadBlock uses existing per-range
cache flushes, which may wait for prior GPU writes. Measure large fallback traffic
before any optimization that would reintroduce borrowed overlapping source spans.

Review before pushing/merging. Rollback is this independent commit; it restores
the known borrowed-source bug and is not a new correctness fallback. Next work
on physical freshness, mapping-use lifetime and raw CPU access needs separate
approval and regression/device evidence.
