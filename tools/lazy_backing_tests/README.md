# Lazy guest physical backing: Phase 2

The default-off `use_lazy_guest_backing` experiment now tries 32 MiB
GPU-driver-exported dma-buf chunks on Android, with Phase 1 memfd fallback.
It prepares physical backing only. Renderer imports/direct bindings,
gather/scatter, DMA, fence/readback, ownership flips, GC/recovery and 8-frame
retirement remain unchanged. `use_uma_zero_copy` is independent.

## Odin3 configuration and telemetry

Close the app, set these values in the existing config.ini sections, and restart:

```ini
[Renderer]
use_lazy_guest_backing\default=false
use_lazy_guest_backing=true

[Debugging]
log_filter\default=false
log_filter="*:Warning Core:Info HW.Memory:Info Render.Vulkan:Info"
```

Do not duplicate section headers. Android Vulkan backend and a loaded compatible
driver are needed for exports. Other backends/hosts and unsupported drivers use
CPU-only chunks. A restart is required to change backing modes. Build this branch
remotely; Phase 1 Odin3 results do not validate the new dma-buf path.

HW.Memory:Info emits `Lazy backing runtime:` every five seconds while the arena
exists, without needing normal destruction. Collect from the app log:

```sh
adb shell grep 'Lazy backing runtime:' /sdcard/Android/data/org.citron.citron_emu/files/log/citron_log.txt
```

Counters: `chunks_created`, `chunk_cache_hits`, `chunks_exported`,
`export_failures`, `cpu_only_fallback_chunks`, `active_chunks`, `peak_fd_count`,
`peak_vma_count`, `export_duration_ns`, `peak_export_duration_ns`. The log labels
CPU-only memfd, GPU-driver-exported dma-buf, or mixed backing. Core logs exporter
availability; Vulkan logs context startup and the first chunk failure.

`chunks_exported` counts published GPU chunks. `export_failures` counts declined
requests or failed final physical mappings; a disabled exporter can decline
without driver calls. CPU-only fallback counts include unavailable exporters.
Duration is cumulative/peak wall time of exporter calls including mapping checks
and full zeroing. Fd peak counts retained arena fds, excluding driver-internal and
temporary fds. VMA peak is sampled process-wide. Relaxed atomic snapshots may
transiently differ during publication. Restore temporary log filtering after tests.

## Changed functions and allocation lifecycle

1. System::Impl::UseGuestMemoryFromGpu creates the exporter before kernel
   initialization, after the Android window has the selected driver library.
   VideoCore::CreateGuestBackingExporter only opens the Android Vulkan route.
2. Vulkan::CreateGuestBackingExporter / LazyGuestBackingExporter::Initialize retain
   one library, instance and export-only device for the arena. No chunks are
   allocated at context startup. InitializeExportDevice is shared with the old
   eager exporter; its default path remains available.
3. HostMemory::Impl::EnsureBackingRange serializes first creation. The exporter
   has a separate device mutex and never calls back into HostMemory, renderer
   caches, schedulers or guest memory. Allocation happens in normal kernel/CPU
   materialization, not an NCE signal handler. Phase 1 full-range CPU page-table
   publication, fastmem mappings and kernel bulk-pointer coverage are preserved.
4. LazyGuestBackingExporter::CreateChunk creates a buffer/allocation, honors
   external-buffer and per-buffer dedicated requirements, binds at offset zero
   and exports one close-on-exec fd. It requires an integrated device and allowed
   device-local/host-visible/coherent/cached memory. Flags alone do not establish
   CPU mmap compatibility. There are no queue submissions or renderer imports.
5. One DMA_BUF_SYNC_START CPU access interval begins. InitializeCpuPayload checks
   fd extent, maps the full chunk, probes an RX alias and shared CPU visibility,
   and zeros all bytes. Vulkan does not guarantee zeroed allocations. Temporary
   VkBuffer/VkDeviceMemory objects are freed; the fd retains the payload reference.
   The exporter context remains alive for future chunks.
6. HostMemory maps the fd at the stable physical address with MAP_FIXED and
   publishes with release ordering. Full ranges precede pointer/alias publication.
   Slots remain valid until arena destruction. Guest frees/remaps never replace
   a published chunk, including one that fell back to memfd.
7. Export, probe or final-map failure cleans temporary resources and falls back
   to memfd before publication. If CPU allocation/mapping also fails, the error
   propagates. Completed earlier slots stay valid; the failed slot is retryable.
8. ReleaseChunk ends CPU access and closes the fd after all CPU aliases are
   unmapped. One CPU interval spans its lifetime; no per-access ownership flips
   are introduced. See the [Linux dma-buf CPU access requirements](https://kernel.org/doc/html/next/driver-api/dma-buf.html)
   and [Vulkan exported fd ownership](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetMemoryFdKHR.html).

Common::GuestBackingExporter, LazyBackingParams and BackingStats provide the
interface in src/common/host_memory.h. DeviceMemory::DeviceMemory(LazyBackingParams)
forwards the context lifetime to HostMemory. Driver metadata stays empty, so
DriverBackingRange rejects all experimental chunks regardless of the UMA flag.

Large heap mappings can materialize many slots before gameplay. This is lazy by
range/mapping, not page faults. Driver allocations are resident and fully zeroed,
unlike sparsely committed memfd backing; commitment and startup latency may differ.
No published memfd payload is migrated. Allocation/GC recovery policy is unchanged.

## Validation and limitations

Run on Linux with Python 3, C++23 and Boost headers:

```sh
python3 tools/lazy_backing_tests/run_tests.py --boost-include /path/to/boost/include
python3 tools/lazy_backing_tests/run_export_tests.py --boost-include /path/to/boost/include
```

Phase 1 tests compile production HostMemory and extract physical-pointer helpers.
They cover cold reservations, split ranges, aliases/RX mappings, concurrent
creation, fd exhaustion/retry, cleanup/moves, 4 GiB/32 MiB geometry and legacy
external-fd/ordinary paths. Export tests compile production HostMemory/exporter
against real Vulkan headers and fake dispatch. Vulkan driver, dma-buf ioctl,
logging/formatting and unused surface headers are mocked; Linux memfd, mmap,
aliases and fd cleanup are real. Fault injection covers final MAP_FIXED failure
and a partially completed range with both export and CPU fallback failure.
Periodic telemetry is checked before teardown. These are not Android device tests.

Untested on Android/Turnip: exported fd extent/RX support, long-lived CPU access,
CPU alias/cache semantics, resident allocations under memory pressure, actual NCE
execution and gameplay on exported backing. Rebuild remotely, test SMU/SMO on
Odin3, and confirm chunks_exported > 0 with stable counts and acceptable export
latency. An all-memfd run validates fallback only. Phase 2 does not validate
zero-copy performance or future per-chunk imports/ownership.
