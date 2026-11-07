# M4 phase two: GMMU entry audit and owned NV mapping lifetime

Date: 2026-10-22. Branch: `codex/issue-372-fixes`, baseline `c5f33fad9b`.
Main is unchanged. This is another phase-two lifecycle prerequisite. All THREE
M4 phases remain unfinished; GuestBackingAdmission remains Unknown.

## Audit: what still reaches live owners

| Entry family | Actual owner dependency | Current protection and remaining gap |
| --- | --- | --- |
| BindRasterizer / GPU InitAddressSpace / renderer bootstrap | Rasterizer identity and GPU publication owner | Previous detached terminal state protects confirmation; first enrollment stays private and retained. It does not cover subsequent reads/writes/maps. |
| Map / MapSparse / Unmap | GMMU tables plus renderer callbacks | mapping_update_guard and page_table_guard serialize mapping work; they do not retain the renderer. Direct public calls remain uncovered. |
| NotifyMappingChange | ModifyGPUMemory and texture-cache state | Called outside page-table read lock, while outer mapping update is serialized. No independent renderer lifetime guard. |
| ReadBlock / WriteBlock | Device memory plus FlushRegion / InvalidateRegion | Safe variants call the rasterizer before copies. Their void callback interfaces do not certify completion, preservation or ownership after shutdown. |
| CopyBlock | Scoped guest memory plus reads, writes and flush | Nested memory operations and the scoped object's eventual write must share an owned transaction. This patch grants neither snapshot/memmove nor partial-write success. |
| Read<T> / Write<T>, Unsafe block operations | GMMU mapping, device manager and backing | Avoid or bypass rasterizer calls; binding admission cannot cover their backing/alias lifetime. Scalar fast paths are unchanged. |
| GetPointer<T> / GetPointer / GetSpan | Borrowed host address and device/backing facade | Pointer escapes its resolver call. A function-entry guard alone cannot retain a later pointer use or exclude remap. |
| WriteBlockCached / FlushCaching | Accumulator and deferred InnerInvalidation | Deferred work can outlive the call that produced it. No complete retained callback/producer protocol is established. |
| FlushRegion / InvalidateRegion / IsMemoryDirty | Direct cache callbacks | Raw rasterizer access remains outside the detached binding check. Shutdown behavior and caller-visible cancellation need their own protocol. |
| ResolvePhysicalRanges / PinPhysicalRanges / PinPhysicalCopyRanges | GMMU/device snapshots, storage allocation and backing | Locks/stamps validate identity and returned plans retain storage. They do not freeze every alias, retain all facade lifetimes, or grant content authority. |
| NV AllocAsEx / BindChannel | Service metadata, GMMU enrollment and channel association | Existing retained publication APIs protect final owner publication. Caller still retains the service/System/GPU facade. |
| NV AllocateSpace / FreeSpace / Remap / MapBufferEx / UnmapBuffer | Allocator, nvmap pins, GMMU callbacks and service metadata | **The next reproducible gap, fixed here:** no terminal Entry covered these five operations. Remap also lacked the declared AS mutex. |
| GetVARegions1/3 | VM allocator metadata | AS lock serializes queries; these do not call the rasterizer. Escaped service/facade lifetime remains the caller's obligation. |

This inventory classifies source paths; it is not an acceptance claim for the
uncovered families. In particular HostMemory virtual remap, allocation pins,
GPU worker/native completion, mappings and exports remain separate dependencies.

## Failure first and repair

Before production changes, the new runner compiled actual NV methods, GPU terminal
methods/forwarding, and GMMU Map/MapSparse/Unmap/NotifyMappingChange bodies. The
closed-entry witness failed precisely with
`FAIL closed NV mapping reported success` after NotifyShutdown. The first sparse
allocation could still change allocator state and page tables and call the live
rasterizer. The tests also defined teardown pauses before applying the fix.

Each of the five methods now acquires the existing GPU::AcquireChannelBinding
terminal publication Entry **before** taking the AS lock or touching allocator,
handles, pins, GMMU or mapping metadata. Closed entry returns InvalidState before
all those effects. The Entry remains alive through early returns, exceptions,
helper calls, synchronous callback return, metadata and service acknowledgement.
The AS lock is destroyed before the Entry on return. FreeMappingLocked is private
and is reached within the retained FreeSpace operation. Remap now holds the same
AS mutex as the other mapping operations while reading VM/maps and updating them.

The existing Impl shutdown sequence closes admission, stops/joins the emulation
worker, joins owner publications, joins detached binding entries and shuts down
the renderer. This patch changes no production scheduling or shutdown order.
An operation admitted before Close may finish after Close, including a multi-entry
Remap batch. A new operation refuses immediately, even if an old operation is
holding the AS lock. The protocol protects object lifetime through that admitted
operation, not reversible process exclusion or content publication.

## Windows deterministic validation

```powershell
python tools/gpu_lifecycle_tests/run_address_space_mapping_tests.py --case closed --repeat 1 --output-dir .diagnostics/m4-nv-mapping-baseline
# The above baseline was run before the production patch and exited 1 at the named oracle.
python tools/gpu_lifecycle_tests/run_address_space_mapping_tests.py --repeat 3 --output-dir .diagnostics/m4-nv-mapping-acceptance
python tools/physical_coherency_tests/run_windows_production_checks.py --output-dir .diagnostics/m4-nv-mapping-production-final
```

Ten operation variants cover sparse/ordinary allocation, FreeSpace with a child,
mapped/sparse Remap, dynamic/fixed/subregion MapBufferEx and ordinary/sparse unmap.
Snapshots check page state/generation, allocator counters, pin/unpin/lookup
counters, mapping records and allocation children. Healthy operations retain
results and effects; an uninitialized VM still returns BadValue while open.

Each variant pauses after Entry acquisition and at the final acknowledgement.
Nine variants pause in actual GMMU callback call sites, six pause at nvmap calls,
and an explicit Remap lock waiter verifies both serialization and retained
admission during the wait. A two-entry batch crosses Close without dropping its
admitted second entry; a newcomer refuses before waiting for its AS lock.
Injected callback/nvmap exceptions prove Entry release and join progress, not
rollback of effects already performed.

Twelve controls remove either refusal or final retention from each of the five
methods, remove Remap's AS lock, or remove GPU's owner-publication Join. Each must
fail with its named runtime oracle. Compiler failures, crashes and timeouts do
not count. Ten lifecycle cases and the cancelled-read coverage control run as
prerequisites. The final positives repeat three times.

Actual production bodies and terminal guards execute with controlled
renderer/worker, allocator/nvmap and page-table data/operation fixtures. Ioctl and
metadata declarations are extracted, but padding/allocator/platform details are
fixtures: this is not a binary-layout, backing/coherency or native Vulkan test.
Six complete production units compile with real headers to Windows AMD64 COFF:
gpu.cpp, memory_manager.cpp, nvhost_as_gpu.cpp, nvhost_gpu.cpp, nvdrv.cpp and
channel_state.cpp. This is object compilation, not an application link.

Final results: all positives and ten lifecycle cases passed three repetitions,
and all twelve mapping controls plus the cancelled-read coverage control passed.
The six final production objects had zero compiler diagnostics. All 24 input-hash
entries across the mapping/production manifests (16 unique inputs), all object
digests and AMD64 signatures were independently matched.
See the committed [result summary](M4_F3_GMMU_MAPPING_RESULTS.json).

Final detailed logs/manifests remain locally in
`.diagnostics/m4-nv-mapping-results/`. Cleanup removed 206 temporary files from
this turn, including all 33 generated exe files and twelve objects from the two
production compilation passes. The final six objects were verified before deletion.
Prior result bundles and shared dependency caches are unchanged.
The existing Linux workflow now includes this runner for future dispatches;
no new remote workflow or Android/device run is claimed.

## Next explicit boundary: cancellation is not completed unmap

RasterizerVulkan::UnmapMemory calls GPU::RunOnGpuThread. ThreadManager dispatches
the callback through PushCommand(..., true); Stop can refuse or cancel a queued
command and wake a waiter. The void return path does not tell its caller whether
the callback executed. RunBufferCacheCpuWrite also has a boolean whose meaning
is not a preservation-completion certificate. Thus an NV operation can have an
owned, memory-safe lifetime without proving that unmap readback actually ran.
The new terminal Entry must never be used as that missing success certificate.

The next candidate protocol needs a checked executed/cancelled/failed result,
retained callback inputs/owners and a defined policy for earlier allocator/pin or
partial mapping effects. It must account for worker failure and cancellation,
not just reorder teardown or wait for enqueue success. Direct GMMU methods and
raw pointer loans also still require owned admission over their complete uses.
No ordinary BufferCache/F2 route or guest authority is enabled by this change.
