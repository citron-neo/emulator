# M3B-F2 physical-backing producer inventory

Audited against production code at `02610e1766` (2026-10-16). This is an activation
inventory, not evidence that every listed producer has joined physical admission.
The [scheduler acceptance and lock audit](M3B_CPU_SCHEDULER_ACCEPTANCE.md) covers the
explicit controlled endpoint. **Ordinary-game F2 activation remains blocked.**

The subsequent [restricted eligibility record](M3B_F2_RESTRICTED_ELIGIBILITY.md) documents
that blocker and the first implemented readback-isolation slice. Queue cancellation before
acceptance and defensive download/async guards do not enroll the remaining forbidden rows.

## Scope and admission rule

The conflict identity is the backing epoch plus physical byte/page footprint, across
all CPU processes, device addresses, GPU address spaces and cache representations.
Different virtual addresses, different queues, GPU-thread serialization and a pinned
host allocation do not establish physical exclusion. Preserving a coarse buffer can
touch its entire footprint (up to 64 pages), not just the scalar's one to eight bytes.
Frame return/reallocation is an ownership mutation even if the host allocation stays
mapped. Read-only consumers also require a coherent representation and ordering.

In the tables, **controlled** means the actual execution participates in admission,
identity validation, completion and Unknown retention. **Legacy only** means existing
cache notifications, fences, locks, snapshots or mapping generations exist, but they
do not provide that complete contract. **Partial** identifies the precise implemented
guard. “Forbidden” is the required restricted-F2 policy; it is **not an assertion that
the current runtime enforces that policy**. Missing enforcement blocks activation.

Only the deliberately isolated native acceptance harness currently demonstrates a
qualified shared footprint. Its raw setup/fill/readback operations are ordered by the
test, and are not general permission for those production producers. There is no
runtime “restricted F2” switch that makes the forbidden rows safe. Provably disjoint
physical footprints can be candidates for independent work; unknown alias coverage
cannot be treated as disjoint. An unrelated GPU marker demonstrates queue progress,
not memory-access qualification.

## CPU and host producers

| Producer / concrete entry | Existing protection | Same-footprint access during restricted F2 |
| --- | --- | --- |
| Explicit request: `Memory::PrepareControlledWrite8/16/32/64`, Rasterizer controlled continuation, `CommitControlledCpuWriteRequest` | Controlled: immutable payload/pins, acceptance retention, range admission, checked preservation, context/PTE try-locks, short owner visits, retained tracker publication, one terminal identity | Allowed only through this endpoint with all external producers excluded; single page, scalar bounds, all aliases and full preservation footprint qualified |
| Standalone `PrepareControlledCpuPage`, `WritePrepared*` / prepared physical store | Physical page/range gates; does not itself preserve a dirty registered GPU representation | Forbidden as an alternate path around a cached request, Refused or Unknown; uncached use needs its own eligibility proof |
| Ordinary `Memory::Write8/16/32/64` / `Impl::Write<T>` | Partial: `TryBoundCpuWrite` catches a matching thread-local controlled binding before pointer lookup; otherwise legacy `HandleRasterizerWrite` and raw memcpy | Allowed only inside the accepted scoped store. Every unbound caller, including another CPU thread/process, is forbidden |
| Cross-page ordinary scalar stores | Legacy pointer qualification and two copies; no all-page transaction | Forbidden; the controlled scalar endpoint rejects crossing a page |
| `Memory::WriteBlock`, `ZeroBlock`, `CopyBlock` | Legacy block walk/notifications; CopyBlock snapshots its complete source and preflights holes, but does not pin mappings or enroll admission | Forbidden for both destination writes and conflicting source reads; snapshot safety is not exclusion |
| `Memory::WriteBlockUnsafe` | Direct block copies; intentionally skips rasterizer-write handling | Forbidden; “Unsafe” is not an F2 escape hatch |
| `WriteExclusive8/16/32/64/128` / CAS | Legacy pointer/write notification plus host atomic compare-and-swap; no controlled scoped-store/admission integration | Forbidden, including failed-CAS attempts unless an independently ordered read-only contract is proved; cannot replay an uncertain atomic |
| Dynarmic callback writes, vector128 and exclusive callbacks | A32/A64 callbacks use Memory scalar/block/exclusive APIs | Forbidden unless a supported scalar actually executes under the accepted binding. Callback availability does not qualify the other generated accesses |
| JIT page-table stores, fastmem, fastmem exclusives | JIT `config.page_table`, `fastmem_pointer`, `fastmem_exclusive_access`; page protection/fault fallback is legacy coherence | Forbidden. Turning off fastmem alone does not exclude page-table stores, callbacks, other cores or other producers |
| NCE native guest stores/atomics | Direct native execution; protection faults call `InvalidateNCE` and resume/reexecute guest code | Forbidden. A fault and resumed native instruction do not acquire a controlled execution identity |
| NCE alignment/failure interpreter stores | `InterpreterVisitor` uses `Memory::WriteBlock`; some instructions write multiple pieces | Forbidden; interpreting one instruction does not provide an all-effects transaction or safe replay |
| Raw `Memory::GetPointer[Silent]`, mutable `GetSpan`, guest-memory scoped/span wrappers | Pointer/backing lookup, sometimes legacy safe read/write callbacks or destructor writeback; no admission lease tied to pointer lifetime | Forbidden for every retained pointer and delayed writeback; include readers that can observe incomplete preservation |
| `DeviceMemory::GetPointer[FromRaw]`, linear/heap physical pointers | Ensures backing exists, then exposes host pointers; does not enroll physical admission | Forbidden for overlapping writes/reads, even if guest PTEs were protected |
| Kernel IPC/copy/user-buffer paths | `KPageTableBase` has Memory block copies and direct linear/heap `memcpy` paths under kernel mapping/resource locks | Forbidden for overlapping buffers; kernel origin and a PTE lock do not establish physical-range ownership |
| Kernel allocation, page clearing, release/reuse | `KMemoryManager` / page-table partial-page copies and memset; resource/refcount ownership | Forbidden while controlled visits could execute. Owner Close protects the originating process's tested exit; it does not certify every shared-frame/alias-process transition |
| Services, audio capture/ring buffers, filesystem/loader, debugger/cheats | Ultimately Memory block/scalar APIs, device-memory access or raw guest pointers; e.g. audio `device_session.cpp` uses `WriteBlockUnsafe` | Forbidden for overlapping guest buffers. These are separate host producers, not automatically covered by suspending a game CPU fiber |
| CPU reads and instruction/data consumers | Scalar/block reads, unsafe reads, JIT/NCE loads, pointer reads; legacy flush/download may run | Forbidden while conflicting controlled writes/preservation are active unless separately admitted/ordered. Unknown also excludes readers |

CPU source anchors: [memory.cpp](../src/core/memory.cpp),
[controlled_cpu_page.h](../src/core/controlled_cpu_page.h),
[A32 JIT](../src/core/arm/dynarmic/arm_dynarmic_32.cpp),
[A64 JIT](../src/core/arm/dynarmic/arm_dynarmic_64.cpp),
[NCE](../src/core/arm/nce/arm_nce.cpp),
[NCE interpreter](../src/core/arm/nce/interpreter_visitor.cpp),
[physical pointers](../src/core/device_memory.h),
[kernel copy paths](../src/core/hle/kernel/k_page_table_base.cpp),
[allocator](../src/core/hle/kernel/k_memory_manager.cpp),
[audio capture](../src/audio_core/device/device_session.cpp).

## GPU, cache and deferred producers

| Producer / concrete entry | Existing protection | Same-footprint access during restricted F2 |
| --- | --- | --- |
| Controlled first registration / controlled refresh / preservation | Controlled registration bundle, immutable physical plan, checked Vulkan transfer and retained resources; refresh excludes stale CPU/GPU aliases | Allowed only through the qualified owner continuation; no ordinary fallback after acceptance or Unknown |
| Normal buffer upload: `SynchronizeBuffer` | Partial: `CheckControlledSynchronization` checks current physical aliases against Writing/Unknown registrations before `ForEachUploadRange` consumes CPU dirty state | Explicitly rejects overlapping active/Unknown operations through this entry. Not a global permission for other upload paths, Ready bindings or mapping races |
| Immediate/inline upload, `InlineMemory`, gather materialization/scatter and buffer overlap merge/copy | Legacy synchronization/dirty ranges, staging and GPU usage ticks; some routes call guarded SynchronizeBuffer, others mutate directly | Forbidden unless each complete route is certified. Reaching a guarded helper once does not cover later writes/merges |
| Already bound vertex/index/uniform/storage/transform-feedback/indirect/query buffers; draw/compute writes | Cache binding and command ordering, usage stamps, ordinary dirty tracking | Forbidden. Existing bindings can consume or modify a representation without another upload check; shader outputs are producers too |
| Normal synchronous buffer download: `DownloadBufferMemory`, `WriteBackDownload` | Partial: ordinary download/writeback entries reject physical-alias Writing/Unknown conflicts before data/dirty effects; legacy Finish and unsafe writeback remain outside a full execution lease | Overlap is rejected by these guarded entries. This does not qualify other download/GC/mapping routes or certify the destination physical identity after arbitrary remaps |
| Buffer async readback: `CommitAsyncFlushesHigh`, `PopAsyncBuffers` | Partial: candidate batch scheduling and front-batch retirement reject active/Unknown conflicts before effects; ControlledReadbacksIdle rejects queued/uncommitted/committed alias conflicts before request acceptance. Vulkan retirement runs on GPUFencingThread with cache locks; typed failures propagate to the command owner and fence waiters | Conflicting pending callbacks must retire before acceptance; no global queue drain for disjoint work. Remaining producer/mapping exclusion is still required |
| Buffer GC, eviction, dirty-batch flush, unmap preservation | Legacy download/copyback and cache-container retirement; unmap retains GPU dirty complement before clearing tracker state | Forbidden while conflicting accepted/Unknown work exists. Neither ordinary cache destruction nor a whole-queue drain is a certified recovery action |
| UMA/direct guest buffers and native imported backing | Vulkan import eligibility, guest barriers/CPU-access tracking, native completion/lifetime references | Forbidden. Device writes can inhabit backing directly; CPU page protection alone cannot exclude them. Acceptance tests disable UMA zero-copy |
| Maxwell GPU DMA linear/split/remap/block-linear, clear, semaphore write | `Launch`, BufferCache DMA acceleration and MemoryManager fallback; snapshots/overlap handling and GPU order are legacy only | Forbidden for source and destination footprints, semaphore addresses and all remapped coverage |
| DMA physical preflight/admission helpers | `PrepareSingleLineStorage`, `PinPhysicalCopyRanges`, `EnqueuePhysicalCopyAdmission` exist, but production Launch does not call them | Not protected execution; forbidden. A prepared physical plan or helper-only unit test is not DMA enrollment |
| Inline-to-memory / Kepler memory / Fermi2D / buffer-image DMA | Upload engine, `AccelerateInlineToMemory`, `AccelerateSurfaceCopy`, `AccelerateDMA::DmaBufferImageCopy`; buffers, images or scoped writebacks | Forbidden. Buffer/image transfer can bypass the normal buffer upload guard |
| Query/result/timestamp and semaphore/fence writeback | QueryCache retains raw device pointers and writes results in completion callbacks; DMA/GPU engines also write semaphore payloads | Forbidden for all result/timestamp bytes and delayed callbacks. A queue fence is not range admission |
| Presentation framebuffer fallback, screenshot and shader/pipeline memory consumers | Presentation layer can read a raw DeviceMemoryManager framebuffer pointer; other consumers read guest snapshots/cache payloads | Forbidden for overlapping live backing without coherent read ordering. Downloading a separate completed presentation image does not itself write guest backing |
| Texture upload, accelerated upload and async decode | Texture image tracking, UnswizzleImage/ReadBlock[Unsafe], staging and later upload; worker decode retains a snapshot | Forbidden; snapshot/upload validity must be tied to physical generation and admission before allowing overlap |
| Texture render targets, image stores, copies, clears, resolves, scaling | Image representations, image dirty state, barriers and modification ticks | Forbidden even when no host copy happens immediately; they create competing authoritative payloads |
| Texture synchronous download / swizzle | `DownloadMemory`, `SwizzleImage`, GPU completion and MemoryManager unsafe block writes | Forbidden; no shared controlled CPU-write admission or Memory execution lease |
| Texture async download, preemptive download, image-to-buffer copyback | `CommitAsyncFlushes`, `PopAsyncFlushes`, delayed SwizzleImage or WriteBlockUnsafe | Forbidden through final callback and writeback; prior submission must be accounted for before activation |
| Texture invalidation, unmap, image GC/alias consolidation | Image tracking/cache locks, deferred resource destruction and possible download | Forbidden as an independent overlapping transition; dropping a representation must not erase accepted/Unknown obligations |
| Host1x NVDEC/VIC and other engines | Separate GMMU/device memory and engine queues; VIC writes output surfaces with GMMU.WriteBlock | Forbidden when physical backing overlaps. Different engine/address-space ownership is not physical separation |

GPU source anchors: [buffer_cache.h](../src/video_core/buffer_cache/buffer_cache.h),
[DMA Launch](../src/video_core/engines/maxwell_dma.cpp),
[DMA helper](../src/video_core/physical_copy_admission.h),
[upload engine](../src/video_core/engines/engine_upload.cpp),
[Vulkan rasterizer](../src/video_core/renderer_vulkan/vk_rasterizer.cpp),
[query cache](../src/video_core/query_cache/query_cache.h),
[texture cache](../src/video_core/texture_cache/texture_cache.h),
[swizzle utilities](../src/video_core/texture_cache/util.cpp),
[VIC](../src/video_core/host1x/vic.cpp).

## Mapping and lifetime mutators

| Mutator / concrete entry | Existing protection | Same-footprint access during restricted F2 |
| --- | --- | --- |
| CPU PTE map/unmap/protect; host fastmem mapping/protection | PTE mutation generation/domain, mapping locks and HostMemory Map/Unmap/Protect; prepared stores revalidate through try-locks | Staleness can refuse/quarantine a request. This is not a general permit for remapping while other participants still own device access; exclude unqualified transitions |
| DeviceMemoryManager Map/Unmap/process ASID registration | `mapping_guard`, odd/even generation, alias bookkeeping; legacy invalidate/unmap notifications | Snapshot qualification detects changes; no shared physical admission for every mutation. Forbidden during a live overlapping transaction except the specifically tested owner teardown sequence |
| GPU MemoryManager Map/MapSparse/Unmap, kind/PTE changes | `mapping_update_guard`, mapping read guard, generation and rasterizer notifications | Not a physical execution lease. Forbidden during conflicting work unless old/new physical footprints and representation retirement are explicitly qualified |
| Originating KProcess teardown | Seal owner before TLS/PTE finalization; bounded visits; explicit Memory Finalize; Prepared Cancelled, Accepted Unknown | Allowed through this exact sequence. No GPU-wide drain; delayed controlled preservation/store cannot revisit released frames |
| Other alias-process teardown, shared-memory transfer/unmap and frame reuse | Their own process/PTE/refcount machinery; originating owner does not automatically pin all alias owners | Not yet qualified; forbidden until every alias process's execution/destruction ordering is proved |
| NVMap handles and external host aliases | Handle GetPointer exposes the mapped host address; external clients and retained mappings are outside a scalar request's scope | Forbidden unless all clients/aliases are excluded or enrolled. A handle reference protects lifetime, not store ordering |
| Backing retirement / HostMemory release / driver-import teardown | Backing epochs, physical pins and native allocation references keep storage/resources alive | Lifetime retention is necessary but does not prevent kernel frame reuse or uncontrolled stores. Unknown prohibits reuse by the controlled protocol; no implicit trusted recovery is implemented |

Mapping source anchors: [CPU mapping](../src/core/memory.cpp),
[device mapping](../src/core/device_memory_manager.inc),
[GPU mapping](../src/video_core/memory_manager.cpp),
[process finalization](../src/core/hle/kernel/k_process.cpp),
[host backing](../src/common/host_memory.cpp).

Raw-pointer and wrapper follow-up anchors: [guest-memory wrappers](../src/core/guest_memory.h),
[presentation fallback](../src/video_core/renderer_vulkan/present/layer.cpp),
[NVMap handles](../src/core/hle/service/nvdrv/core/nvmap.h),
[debugger writes](../src/core/debugger/gdbstub.cpp).

## What the current tests establish

The native target proves the explicit request's real guest-fiber observation, 64 full
process exits, per-owner isolation, actual lock-context progress, single store,
GPU99/CPU77/GPU99 preservation, dropped-continuation cancellation and accepted-native
exit with a frame-reuse sentinel. Host mutations additionally prove Unknown identity
retention before transaction allocation and while admission is parked.

It does **not** prove exclusion of the forbidden rows. In particular, the known ordinary
unbound F2 witness still produces backing77/cache10. No settings change, disabled fastmem,
successful Android compilation or software-Vulkan acceptance closes that activation gate.

Before any application activation, an eligibility owner must establish all physical aliases,
exclude or enroll every overlapping producer/reader and mapping transition for the complete
coarse footprint, account for already submitted GPU work and deferred callbacks, and preserve
Unknown obligations through teardown and trusted recovery. It must fail before acceptance
when those facts cannot be proved; afterwards uncertainty is quarantine, never a legacy
fallback or replay. This is substantial producer integration work, not a small guard fix.

## Reproducing the source audit

Run from the repository root; inspect both implementations and callers, not merely hits:

```text
rg -n 'WriteBlock|ZeroBlock|CopyBlock|WriteExclusive|TryBoundCpuWrite|InvalidateNCE' src/core
rg -n 'fastmem|page_table|MemoryWrite' src/core/arm/dynarmic
rg -n 'EnqueuePhysicalCopyAdmission|PrepareSingleLineStorage' src
rg -n 'CheckControlledSynchronization|ForEachUploadRange|WriteBackDownload|PopAsyncBuffers|DMACopy|DMAClear|InlineMemory|ObtainBuffer' src/video_core/buffer_cache
rg -n 'DownloadMemory|UploadImageContents|SwizzleImage|PopAsyncFlushes|UnmapMemory|GarbageCollect' src/video_core/texture_cache
rg -n 'GetPointer|GetSpan|WriteBlockUnsafe|memcpy|memset|AtomicCompareAndSwap' src/core src/video_core src/audio_core
rg -n 'MapSparse|NotifyMappingChange|mapping_generation|Finalize|SealControlledCpuWrites' src/core src/video_core
```

Local staging-only memcpy does not itself write guest backing; its eventual GPU copy or
guest writeback is classified above. Any new raw-pointer producer, engine or deferred
callback without an established classification defaults to forbidden for overlap.
