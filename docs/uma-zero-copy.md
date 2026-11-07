# Experimental shared guest buffers

The experimental `use_uma_zero_copy` setting defaults to true on Android for
real-device APK testing, and false on other platforms. An explicit saved value
still overrides the default. Enablement does not bypass backing eligibility or
synchronization checks. It builds on patches
1–9 and preserves gather/scatter/readback. It does not change the existing
8-frame buffer retirement mechanism. Android builds and real-device validation
remain the remote workflow's responsibility.

## Call paths

* Patch 6: `PrepareGatherBindings` / `ResolveGatherChunks` prepare overlapping
  views; graphics/compute uniform and storage binds call `GatherBuffer`.
  Previously it allocated and cleared a scratch `Buffer`, copied each cached
  device segment into it, and tracked shader writes with `FlushGatherWrites`.
  `ResolveScatter` / `ScatterGatherEntry` copied those writes back before DMA,
  downloads, rebinding, eviction or invalidation. `ResolveCachedBinding` now
  applies that ordering to cached index, vertex, uniform, storage, texel,
  transform-feedback and indirect bindings too, refreshing invalidated ids.
* Patch 7: `DMACopy` previously delegated split mappings to `DMACopySplit`,
  then copied cached buffer segments and maintained a CPU mirror. The new
  `TryDirectDMACopy` resolves and validates the entire source/destination plan
  first, stabilizes all buffer ids, and copies shared buffers without a CPU
  mirror. It checks overlap in **backing** coordinates, including guest aliases.
* Patch 8: `FenceManager` commits/pops buffer-cache flushes.
  `CommitAsyncFlushesHigh` previously allocated a download staging buffer,
  recorded cached-buffer-to-staging copies, and `PopAsyncBuffers` copied them
  into guest memory after the fence. `DownloadBufferMemory` did the same
  synchronously; GC had a separate bounded download staging reservation.
  Direct buffers use GPU→host barriers, GPU completion and dma-buf CPU-access
  handoff instead of these copies. Scratch buffers retain their existing paths.

## Backing and eligibility

Android's existing guest-memory exporter can allocate driver-owned dma-buf
chunks with device-local, host-visible, host-coherent, host-cached properties.
It remains functional for dedicated/padded exports, but records them as
ineligible for direct buffer views. Ordinary Linux memfd and Windows file
mapping backing are never advertised as driver-exported memory. Existing UMA
cache upload buffers are independent VMA allocations: host visibility alone
does not make them shared guest backing.

`GuestBackingRange` describes a known non-dedicated, unpadded exported payload.
`ZeroCopyEligibility` describes the direct/scratch choice using explicit memory,
usage, lifetime, alignment, completeness and continuity facts.
`DirectGuestMapping` owns the imported Vulkan memory and buffer; a duplicated
fd transfers ownership to Vulkan only after successful allocation. The import
keeps the allocation alive through buffer retirement independently of the
original fd. Guest mmap access uses the original fd while guest memory and the
renderer are alive.

The runtime queries external buffer support for its exact usage, memory
requirements (including dedicated requirements and binding alignment),
fd-compatible memory types and actual `VkMemoryPropertyFlags`. It requires an
integrated device **and** compatible device-local/host-visible/coherent backing.
No Android or vendor check selects zero-copy. Image backing is not imported.

For a split shader binding, all mapped segments must cover the entire view,
already have shared cached backing, and occupy consecutive offsets in the same
payload. Vulkan buffers cannot concatenate arbitrary physical pages without
additional sparse-binding support. DMA does support arbitrary split source and
destination layouts by issuing copies between individually eligible buffers.

The GPU `MemoryManager` pointer, address-space id and even mapping generation,
plus the device page-table generation, are checked before retaining a view or
executing DMA. Device mapping changes invalidate direct views **before** the
old translation disappears. Guest VA alone is never a direct-mapping identity.
Binding growth/shrink/layout changes preserve prior shared writes and retire
the old imported view; they do not clear guest bytes.

## Synchronization and fallback

Coherence does not replace ordering. GPU use ends dma-buf CPU access; CPU
access/reuse waits for outstanding GPU reads and writes and starts CPU access
again. CPU write/fault and unmap callbacks dispatch shared-backing operations
through a blocking GPU-thread command, releasing the cache mutex before waiting.
GPU-thread callers execute directly. Staged-to-direct cache joins copy only
GPU-modified intervals; sub-word dirty intervals retain the staged path so a
Vulkan copy never expands over newer CPU bytes. The runtime retains Vulkan
host/device memory barriers, completion
waits and `DMA_BUF_IOCTL_SYNC` START/END semantics for the original mmap aliases.
Interrupted sync ioctls are retried. An unsupported sync operation rejects a
candidate import. A sync failure after submitting shared work is surfaced as
an error rather than silently discarding pending writes.

Non-coherent imports deliberately remain **ineligible**. Vulkan
`vkFlushMappedMemoryRanges` / `vkInvalidateMappedMemoryRanges` apply to a Vulkan
mapping; they do not by themselves establish coherence for the original guest
mmap aliases. Supporting those imports requires a complete platform ownership
and alias-cache protocol, including atom alignment. Their current scratch path
continues using allocator-managed staging and readback cache synchronization.

Fallback conditions include disabled opt-in, discrete GPUs, ordinary backing,
incompatible external usage or memory types, missing import extensions,
non-coherent memory, dedicated/padded exported allocations, mapping holes,
misalignment, cross-chunk/nonconsecutive shader backing, unstable/remapped
translations, and failed import/sync support checks. Mixed-eligibility DMA
falls back as a whole; overlapping DMA uses the guest-memory snapshot path.
Uniform zero-padding still needs a temporary padded buffer.

Direct fence handling currently waits conservatively at commit time. This
avoids readback bandwidth but may stall more than the existing async path.
Real-device performance and skip-frame behavior need validation before enabling
the option broadly or removing fallback. No retirement-ring tuning is included.

## Validation

Run the existing and extended deterministic gather tests, the production Vulkan
importer with a fake driver, and extraction regressions without building Android:

```sh
python tools/gather_buffer_tests/run_tests.py --cxx c++
python tools/gather_buffer_tests/run_vulkan_import_tests.py --cxx c++
python tools/gather_buffer_tests/run_gpu_write_tests.py --cxx c++
python tools/gather_buffer_tests/test_extraction.py
python tools/openssl_tool_tests/run_tests.py
```

Coverage includes split mappings, binding changes, shared GPU write preservation,
manager/address-space identity, generations, all-or-nothing and overlapping DMA,
cached-binding scatter ordering, direct readback handoff, coherence/discrete
fallback, binding/allocation alignment, dedicated restrictions, fd ownership
and import failure cleanup. Catch2 eligibility tests are also registered in
`src/tests/CMakeLists.txt`. These are CPU/fake-driver checks; they cannot establish
Adreno driver correctness or actual GPU/CPU cache visibility.

The existing standalone GC staging reservation tests remain applicable.

Memory-property and alias semantics follow the
[Vulkan memory specification](https://docs.vulkan.org/spec/latest/chapters/memory.html).
