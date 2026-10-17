# Split-buffer regression checks

Run with Python 3 and a GCC-compatible C++23 compiler:

```sh
python tools/gather_buffer_tests/run_tests.py --cxx g++
python tools/gather_buffer_tests/run_cpu_scatter_tests.py --cxx g++
python tools/gather_buffer_tests/run_gpu_stop_tests.py --cxx g++
python tools/gather_buffer_tests/run_mapping_snapshot_tests.py --cxx g++
python tools/gather_buffer_tests/run_unmap_alias_tests.py --cxx g++
python tools/gather_buffer_tests/run_binding_alias_tests.py --cxx g++
python tools/gather_buffer_tests/run_index_range_tests.py --cxx g++
```

The runner extracts current buffer-cache helpers and the Vulkan flush-area merge
from production source, then compiles them against fake GPU memory, buffers and
tracking. Generated files stay under `.diagnostics/gather-buffer-tests`; use
`--build-dir` to select another directory.

Checks cover split mappings, overlapping views, layout changes, CPU/GPU writes,
address-space cache isolation, scratch retirement, allocation/copy failures,
DMA fallback and reactive readback filtering. Four-byte fixture pages keep the
mapping cases small and deterministic.

Writable physical aliases at different GPU addresses cannot be represented by
dense scratch buffers. They are rejected before gather copies or shader dispatch,
including aliases across different bindings or device addresses. The GPU command
loop requests frontend shutdown and retires subsequent commands without executing
them. This is an explicit unsupported-mapping stop, not a rendering fallback.
The original 99-versus-10 regression also checks that rejecting an existing pending
scatter performs no copies and retains its GPU writes and pending state.

Uniforms and storage bindings marked read-only by shader metadata can safely read
duplicate bytes. Their aliases are allowed unless they overlap a GPU-written view.
Read-only alignment padding is excluded from the shader alias comparison; writable
scratch keeps checking the complete range because scatter copies whole chunks.
Changing a cached read-only view into a writable one revalidates it. Writable
validation remains keyed by address-space identity, mapping generation, ranges
and access modes. Errors include the conflicting physical/GPU bytes and write
flags so device failures can be investigated without disabling the guard.

The binding-alias runner extracts production texture binding construction,
graphics/compute image/texel host binds, split preparation, physical validation
and scatter. It reproduces SSBO=99 overwriting image-buffer=77 when preparation
validation is bypassed, then verifies that the mixed dispatch is rejected before
binding or GPU writes. It covers same GPU VA, different GPU VA with the same
DAddr, different DAddr with identical physical bytes, partial overlaps, disjoint
and read-only combinations, changed write masks and retained pending writes.
Image/texel buffers remain independent cached resources: a writable overlap with
scratch must be rejected even when both views have the same GPU address. A split
image/texel range accompanying gathered shader bindings is rejected because its
current host-bind path cannot represent it as a shared gathered view.

Coverage is dispatch-local uniform/storage/image-buffer/texel-buffer bindings,
across all enabled graphics stages or the compute channel. Physical comparison
uses MemoryManager GPU-to-DAddr translation and DeviceMemoryManager's stable
physical-page snapshot; descriptor category and DAddr equality are not identities.
The existing mapping-generation/address-space cache also includes texture ranges
and their write flags. Dispatches without gathered uniform/storage ranges retain
the old path. Ordinary texture images/render targets, geometry/transform-feedback
bindings, cross-dispatch independent cached aliases and cross-channel coherence
are not covered by this shader-buffer guard; it is not a Physical Coherency
Identity implementation. Validation adds no GPU wait or Vulkan import and does
not alter unmap/readback, CPU-write synchronization or retirement.

Alias validation locks a stable GPU page-table snapshot and queries all physical
pages under one device mapping lock. A temporary odd generation is not a reason
to stop gameplay: readers wait for actual table updates to finish. GPU mapping
invalidation callbacks run before the mutation lock and odd generation begin,
because they can wait for renderer cache locks held by the reader. Unmap still
preserves pending writes through its callbacks before changing either page table.
The snapshot runner exercises production map/big-map/sparse/unmap functions and
alias/device snapshot helpers with concurrent writers, a callback waiting for the
renderer lock, generation invalidation, and actual unsafe write-alias rejection.

The CPU scatter check runs the actual CPU dispatch and scatter helpers on a worker
thread, verifying GPU-thread execution and write preservation during CPU unmap.
The unmap-alias check additionally extracts the public Vulkan UnmapMemory callback,
FindBuffer, SynchronizeBuffer, UploadMemory and DownloadBufferMemory. It reproduces
the original 99-to-10 overwrite through a surviving alias: unmap now scatters and
reads staged GPU writes into guest backing before the existing CPU-modified flag
can trigger another upload. Scheduler copies and completion execute on the GPU
worker, including ordinary dirty buffers with no pending scatter. It also checks
that post-write CPU invalidation still wins without readback, unaffected dirty
ranges remain tracked, and direct UMA unmap keeps its original no-copy barrier/wait
path. Actual CPU pre-store notifications save staged GPU data first; this prevents
the production deferred dirty queue's 64-byte granularity from discarding adjacent
GPU bytes when the CPU overwrites only part of that region.

Unmap callbacks execute on the GPU worker and drain deferred CPU notifications
before acquiring cache locks. Readback is clipped per buffer, extended only to
complete tracked device pages, and copies the remaining exact GPU dirty intervals.
Unmap clears those intervals and page bits only after GPU completion and CPU
writeback succeed. Tests cover partial pages, mixed CPU/GPU writes, multiple
buffers, failed copies, GPU reentrancy and preservation of other dirty pages.

`HW.Memory:Info` samples `Unmap staged readback` (first eight and every 64th
completion), reporting cumulative count, actual bytes, time_ns and last_time_ns.
Time includes staging, GPU completion and CPU copy, excluding GPU callback queue
latency. `Staged CPU write preservation` separately reports count and duration of
pre-store preservation, including scatter/readback/wait. These are correctness
costs of staged memory, not chunk imports or Phase 3 ownership-transition metrics.
The stop check extracts the production GPU command loop and verifies shutdown,
skipping later commands, and signaling queued fences. The index check covers the
inclusive index limit and the last index crossing a tracking-page boundary.

`run_vulkan_import_tests.py` also checks that a small buffer over a 1 GiB dma-buf
falls back before importing, synchronizing or waiting. Whole-allocation CPU cache
maintenance is conservatively limited to 4 MiB; this is a cost limit, not a claim
that smaller allocations are coherent. All accepted dma-bufs still require their
CPU access protocol and GPU ordering. The current 1 GiB exported guest chunks
therefore retain gather/scatter until a bounded range or proven coherent alias
protocol is available. Repeated CPU writes in the same ownership interval must
not repeat GPU waits; GPU reuse must restore the wait requirement.

On 2026-10-12, an Odin3 running Turnip was sampled with `simpleperf` for five
seconds while Super Mario U stalled at startup. With zero-copy enabled, cache
invalidation and cleaning accounted for 47.10% and 46.17% of CPU cycles. Two
native backtraces placed the GPU thread in the dma-buf START/END ioctls. Disabling
only `use_uma_zero_copy` allowed the same game to start normally; the cache sweep
functions no longer appeared above the 3% report threshold. This validates the
cause and the existing fallback, not the patched APK or an FPS improvement.

These checks do not exercise Vulkan drivers, real GPU command scheduling, Android builds
or game rendering. Device testing is still needed for correctness and performance.
