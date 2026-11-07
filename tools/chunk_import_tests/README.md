# Phase 3: diagnostic per-chunk Vulkan import cache

This prepares import infrastructure only. No guest descriptor, DMA command,
shader, NCE pointer or ordinary buffer-cache allocation uses these imports.
`guest_chunk_import_diagnostics` defaults to **false**, independently of UMA.

## Architecture audit and scope

`DeviceMemory::GetPointer/GetPointerFromRaw`, CPU alias mapping and kernel
physical accesses continue to materialize lazy backing through
`HostMemory::EnsureBackingRange`. The arena publishes its fd only after CPU
initialization/mapping succeeds. Publication now also records an immutable export
description; the existing 32 MiB allocation strategy and CPU access interval do
not change.
Optional metadata allocation/query failure preserves the CPU-ready published
backing and suppresses diagnostic import, rather than failing Phase 2. Leases
report dma-buf publication separately from metadata/import capability.

`LazyGuestBackingExporter` retains one export-only device/library. It creates an
exportable buffer/allocation, exports an fd, opens the existing CPU access
interval and initializes the CPU payload. Its temporary VkBuffer and
VkDeviceMemory are destroyed before returning. **The exported dma-buf retains
the allocation payload**, not those original Vulkan handles. Phase 3 therefore
records size, buffer flags/usage, dedicated requirements, memory properties,
deviceUUID and driverUUID rather than assuming every imported buffer is compatible.
Unknown metadata is not import permission.

`HostMemory::GetPublishedChunk` and `FirstPublishedExportedChunk` query the
publication table under its lock. They never call EnsureBackingRange or create a
cold chunk. A lease copies immutable metadata and pins the low-level arena,
original fd and exporter/library. HostMemory destruction/replacement invalidates
its epoch immediately even if a lease keeps the arena alive. Move operations
transfer epoch ownership. This lease intentionally pins the **whole arena**, not
just its selected fd; old arenas must not be retained indefinitely by callers.

`DeviceMemoryManager::GetPublishedGuestChunk(DAddr)` holds the device mapping
lock and resolves the physical offset before querying that table. Distinct DAddr
aliases therefore have the same arena generation/chunk index. GPU VA callers
must first use their own MemoryManager translation and mapping snapshot. No GPU
VA lookup, dirty-range canonicalization or Physical Coherency Identity system
is implemented here.

Cache identity is `(renderer cache/device epoch, arena generation, chunk index)`;
fd numbers, GPU VA and DAddr are not cache identities. A renderer owns one cache
and uses its existing logical device. A mutex serializes all creation,
publication, generation changes and resource release. Successful and rejected
requests are cached. Even transient driver failures are sticky until renderer
or backing generation replacement, avoiding repeated diagnostic imports.

The cache exposes observations only, never VkBuffer/VkDeviceMemory handles.
Cache entries contain a backing lease, imported memory, then buffer; reverse
destruction destroys buffer before memory and finally drops the lease. The
renderer's diagnostic member is destroyed before Device, dispatch, instance and
library. Existing renderer WaitIdle remains unchanged; these private resources
have never been submitted and need no new GPU wait or 8-frame retirement.
Core shutdown resets GPU/renderer before Host1x/kernel shutdown and retains
DeviceMemory between normal game launches. A newly created renderer therefore
reimports its single diagnostic chunk, even if the arena generation is unchanged.

Close marks the cache closed before waiting for an in-flight import. Such an
import cannot publish afterward. Backing death during creation also prevents
publication. A stale request/stats snapshot releases inactive entries; generation
replacement clears both successful and rejected entries. Callers still obey the
normal C++ rule of not calling an object after its destructor has run.
Normal renderer teardown logs active imports/bytes zero and released-entry counts
after explicit cache Close, before Device destruction.
Publication allocation failure destroys the unpublished resource and disables
further imports for that generation. Optional diagnostic setup/probe exceptions
disable diagnostics while retaining staged rendering.

## Compatibility and ownership rules

The Linux backend accepts known 32 MiB dma-buf chunks only, with the exact
exporter's buffer usage and zero flags. It checks:

- Both external_memory_fd and external_memory_dma_buf extensions enabled.
- DMA_BUF handle type, compatibleHandleTypes and IMPORTABLE buffer properties.
- Nonzero, matching exporter/renderer deviceUUID and driverUUID.
- Export metadata, external properties and renderer memory requirements must
  **not require dedicated allocation**. Dedicated exports/imports are an explicit
  unsupported case in this first implementation, even if another implementation
  could legally support them.
- Positive fd payload size equal to recorded allocation size; buffer requirement
  size fits that allocation and alignment is nonzero. Binding offset is zero.
- A memory type in `buffer requirements.memoryTypeBits & fd memoryTypeBits`,
  using renderer memory types, preferring DEVICE_LOCAL. Protected, lazy and AMD
  device-coherent/uncached feature-dependent types are excluded.

HOST_VISIBLE/COHERENT flags are recorded, not assumed or required: imported
memory is never mapped or accessed by this diagnostic. Import success does not
establish CPU/GPU/NCE coherency, alias correctness or DirectGuestView eligibility.

Only a F_DUPFD_CLOEXEC duplicate is passed to Vulkan. The original borrowed fd
is never consumed. Allocate failure leaves the duplicate application-owned and
closes it; success transfers it to Vulkan. Bind failure destroys buffer then
imported memory; the consumed duplicate is not closed again by the application.
Partial creation and cache-publication exceptions use the same RAII cleanup.
No new dma-buf synchronization ioctls, queue submissions or ownership flips are
introduced. The arena's existing final ReleaseChunk ends CPU access and closes
the original fd after CPU aliases and all leases have been released.

These rules follow the [Vulkan fd import ownership rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryFdInfoKHR.html)
and [external memory allocation requirements](https://docs.vulkan.org/spec/latest/chapters/memory.html).

## Diagnostics and telemetry

The flag is sampled at renderer construction; restart the game/renderer to change
it. In `[Renderer]`, temporarily select:

```ini
use_lazy_guest_backing\default=false
use_lazy_guest_backing=true
guest_chunk_import_diagnostics\default=false
guest_chunk_import_diagnostics=true
```

Set `log_filter\default=false` and include `Render.Vulkan:Info` in the existing
log filter. Back up settings before editing, and restore only changed keys after
testing. UMA may retain the user's original setting; it does not enable Phase 3.

Every five seconds, Composite selects **one already published exportable chunk**
and requests it twice. The first candidate is retained for that backing generation,
even if other chunks appear later; rejection does not trigger a search/import sweep.
There is no arena import sweep, cold chunk creation or
probe data read/write. First success logs requests=2, created=1, hits=1,
active=1, logical_bytes=33554432. Repeated probes increase requests/hits while
created/active/bytes remain fixed. No candidate means no import is attempted.
Rejection logs a named reason, counts and last VkResult. Rendering continues on
the same staged path. Non-Linux import requests reject as platform unsupported.

Runtime snapshots are available via `RendererVulkan::GetChunkImportStats`:
requests, cache hits, created imports, failures, per-reason rejections, active/
peak imports, active logical bytes, total/max miss duration, dup failures,
released cache entries, device epoch and backing generation. Duration excludes
cache-lock queue latency and publication. Failed attempts also contribute time.
Failures count unsuccessful misses including compatibility rejection; named
rejections count every rejected request, and cache hits count successful reuse.
Explicit probes sample process fd/VMA counts; their peaks are sampled, not
guaranteed absolute peaks. GetStats itself performs no /proc sampling. Normal
gameplay with diagnostics off has no imports, probes, process sampling or logs.

## Validation

Changed modules/functions: common `published_guest_chunk.h` and
`HostMemory::{GetPublishedChunk,FirstPublishedExportedChunk,BackingGeneration}`;
exporter `ExportChunk`, `LazyGuestBackingExporter::{Initialize,DescribeChunk}`;
`DeviceMemoryManager::GetPublishedGuestChunk`; new `GuestChunkImportCache`,
`GuestChunkVulkanBackend::Create` and `GuestChunkImportDiagnostics`;
renderer constructor/Composite/teardown and `GetChunkImportStats`; the default-off
setting and common/video-core source lists. Gather/scatter, DMA, dirty/unmap,
GMMU mutations, import allocation for existing guest buffers and retirement are
unchanged. Diagnostic buffers mirror the export usage contract; texel/TFB/image
or other future binding usages require their own compatibility validation.

```sh
python tools/chunk_import_tests/run_tests.py --cxx g++
# Linux, real HostMemory/syscalls; add --boost-include if not installed system-wide:
python tools/chunk_import_tests/run_backing_tests.py --cxx g++
python tools/lazy_backing_tests/run_tests.py --cxx g++
python tools/lazy_backing_tests/run_export_tests.py --cxx g++
```

The cache template is production code. Vulkan backend and diagnostic service
functions are extracted from production and run with actual Vulkan core types,
owning wrapper handles, fake device callbacks and explicitly simulated fd
ownership. Tests cover miss/hit, distinct chunks, concurrent deduplication,
teardown during creation, arena/device replacement, expired backing, publication
OOM, rejected handle/usage/dedicated/UUID/requirements/type bits, non-coherent
properties, successful fd transfer and allocate/bind cleanup.

Linux backing tests compile production HostMemory and the production DAddr query
method with real mappings/files and a memfd-based **fake exporter**. GPU/DAddr
page translations are fixture inputs, not a full GMMU integration test. They
verify cold queries, physical aliases, stable metadata, lease fd retention,
destruction/replacement and move lifetimes. memfd stand-ins are not proof of real
Vulkan dma-buf compatibility.

Available validation on 2026-10-13:

- New cache/backend/service regressions pass on Windows MinGW and Linux GCC.
- Production publication/lease, existing lazy backing and exporter regressions
  pass on Linux (exporter uses fake Vulkan and real Linux mapping syscalls).
- All nine existing gather-buffer runners pass on Windows and Linux: gather,
  mixed binding aliases, CPU scatter, GPU-thread CPU writes, unmap preservation,
  mapping snapshots, GPU failure stop, legacy Vulkan import and index ranges.
- Linux backend and renderer translation units compile with isolated logging/
  platform-surface header shims. This is not a complete application build/link.
- Android full build is left to workflow as requested. No Phase 3 APK, real
  renderer Vulkan integration or Android/Turnip import has been tested yet.

## Odin3 verification procedure

1. Build this reviewed commit via workflow with diagnostics default-off. Verify
   APK commit and SMU/SMO gameplay with the user's ordinary settings first.
2. With the app closed, back up settings and enable lazy backing plus the Phase 3
   diagnostic flag and Vulkan Info logs. No probe/instrumentation installation.
3. Launch SMU. Confirm one existing 32 MiB chunk imports, or record the named
   compatibility rejection. On success, created=active=peak=1 and repeat probes
   produce hits without additional imports. Check the observation's size,
   alignment, selected renderer memory type and property flags.
4. Walk in SMU, then SMO New Donk City. Capture process fd/maps alongside cache
   logs. Existing chunk materialization may grow, but diagnostic import count
   must stay one per renderer epoch. VMA protection fragmentation can grow
   independently; do not equate arena sampled peaks with live process counts.
5. Normally exit and relaunch. New renderer epoch requires a new cache; old
   private imports must be released while arena lifetime follows existing System
   policy. Capture the baseline exit-associated guest PANIC separately if it
   recurs; do not assign it to imports without an A/B comparison.
6. Unsupported/driver failures must leave staged gameplay intact, active imports
   zero and one failure attempt per cached chunk/generation. Do not artificially
   exhaust phone fds or modify backing to force a failure: injected cases are
   covered by mocks; real driver rejection remains a device-validation item.
7. Close the app, preserve logs and restore only temporary keys. Do not enable
   DirectGuestView or change CPU/GPU ownership settings based on import success.

Outstanding assumptions are real driver fd-properties/UUID reporting, payload
size and non-dedicated buffer compatibility, import latency/driver resource
behavior while the existing CPU interval stays open, and clean real-device
teardown. Physical coherency/alias dirty identity and Phase 4 remain separate.
