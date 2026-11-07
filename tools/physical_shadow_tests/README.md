# M1: observation-only physical shadow registration

**Current policy:** M1.1 defaults to sampled observations (approximately 1/64
operations), exact within-operation dedup and span comparison. See [M11.md](M11.md)
for skip/writer accounting, timing scopes, denominators and validation. Producer
definitions/hooks below remain unchanged; this is never full producer coverage.

Baseline: M0 `3c1aae0f0f`. This is a bounded **historical range observation**
window, not a live producer registry or Physical Coherency Coordinator. Enabling
it does not fix alias bugs, authorize dispatch, select a fallback, add data
versions, copy bytes, create/import backing, issue GPU commands or change dirty
tracking. Phase 2/3 imports, UMA, scatter and eight-frame retirement are unchanged.

## Enable and read

Default off on every platform: renderer advanced setting
`physical_coherency_shadow=true`. Restart the renderer/application after changing
it; it is latched at renderer construction. Set log filter to include
`Render_Vulkan:Info` temporarily. No Android UI control is introduced. The existing
configuration serialization handles this registered setting.

Logs appear after every 4096 observed draw/compute preparation attempts and at
normal renderer shutdown. An exception during configuration ends the measurement
scope as a preparation attempt; counts are **not successful/submitted/completed
dispatch counts**. Abrupt Android process termination may lose final statistics.
Counters are cumulative per renderer, not per guest arena/game across renderers.

Metrics:

| Counter | Meaning |
| --- | --- |
| `queries`, `query_ns` | Resolver attempts and elapsed time including lock contention/errors |
| `max_dispatch_queries`, `max_dispatch_query_ns` | Largest query count and aggregate resolver time in one preparation |
| `observer_ns`, `max_dispatch_observer_ns` | Selected Observe bodies including lookup/dedup, provenance/validation/geometry; excludes input counters, sampling gate, skipped inputs and visitor/logging |
| `validation_ns` | Cache validation plus fresh DAddr provenance capture time |
| `hits`, `cache_invalidations` | Valid cached stamp reuse versus stale stamp; invalidation rate = invalidations / (hits + invalidations) |
| `snapshot_invalidations` | Fresh resolve became invalid before paired-lock provenance capture; denominator is queries |
| `outside_dispatch_queries` | Texture producer marks outside observed draw/compute configuration (e.g. copy/clear) |
| coverage by `staged_buffer/scratch/texel/texture/render_target/direct_buffer` | Observation/write-intent counts; successful full-range registration; unregistered write intents; unresolved observations including reads |
| coverage `bytes/max_range/size_buckets` | Descriptor/event-weighted requested range distribution, including unresolved attempts; bucket edges are 64 B, 1 KiB, 16 KiB, 64 KiB, 256 KiB, 1 MiB, 16 MiB (upper bucket includes edge) |
| coverage `segments/split_observations` | Registered physical segment sum and multi-segment observation count; multi-segment includes chunk boundaries, not only noncontiguous mappings |
| `aliases`, `different_daddr`, `different_manager` | Repeated overlap encounters in bounded historical window; not unique aliases/conflicts or live resource counts |
| `registered_observations/alias_observations`, read-only/potential-write pairs | Event-weighted encounter frequency; partial scans are lower bounds when comparison drops occur |
| distinct GPU/DAddr refs and `sample_epoch/offset/length` | Deduplicated mapping-token+address references to a sampled constant-membership physical interval, plus read-only/potential-write counts; not descriptor multiplicity |
| resolve/lock/walk/dispatch P50/P95/P99 | Fixed 65-bin log2 histograms, **bucket upper bounds** (`*_upper_ns`), not exact quantiles; empty histogram gives zero |
| `gpu_lock_wait_ns/device_lock_wait_ns/walk_ns` | Resolver lock acquisition time (includes uncontended overhead/scheduling) separated from locked traversal, stamp construction and metadata allocation |
| `validation_lock_wait_ns/provenance_walk_ns` | Cached-stamp/fresh provenance lock acquisition versus DAddr provenance walk/validation/allocation; excluded from resolver-only histograms |
| `generation_invalidations/generation_requeries/lifetime_invalidations` | Stale cache classifications and generation-triggered re-resolve attempts, including failed attempts |
| `entries/peak_entries`, `metadata_bytes/peak_metadata_bytes` | Current/peak entry count; object+retained vector capacity plus observed overlapping query/provenance temporary payload, not RSS/allocator or in-flight reallocation peak |
| `holes`, `provenance_mismatches` | Failed/hole-containing physical resolution; cached resource DAddr inconsistent with current GPU translation |
| `budget_drops`, `comparison_drops`, `evictions`, `errors` | Sampling/coverage loss; these must accompany any interpretation of alias counts |
| `sampled_out/deduplicated`, writer variants, `inspections/registrations` | Same input coverage definitions; skipped events are explicit unregistered/unknown subsets, not successful physical registrations |
| `sampled_dispatches/skipped_dispatches`, outside selected/skipped | Preparation/event sampling denominators; histogram quantiles only include selected preparations |

An entire range must resolve before registration. Hole-containing ranges are
unresolved even if some segments resolve. No partial producer registration.
Physical segment coordinates remain `(backing epoch, physical offset, length)`;
GPU VA and DAddr are provenance, never the physical key. Chunk index is derived
from physical offset, not a new identity. Generations only validate observations.

## Production call chains and coverage limits

Graphics: `RasterizerVulkan::PrepareDraw` -> pipeline Configure -> existing buffer
preparation/binds -> `ObservePhysicalBindings(false)` before draw recording.
Compute: DispatchCompute -> Configure -> `ObservePhysicalBindings(true)` before
direct/indirect dispatch recording. Visitor only reads prepared bindings/masks;
it never calls FindBuffer, GatherBuffer, ResolveScatter or synchronization APIs.

- Enabled graphics/compute uniforms and SSBOs: resolved GPU logical ranges,
  staged and directly shared cached buffers reported separately.
- Gathered uniform/storage: whole prepared gather allocation including scatter
  padding. Classified as scratch unless existing allocation is directly shared.
  Shared allocation observations may repeat once per descriptor; not unique producers.
- Enabled image/texel buffer views: actual cached contiguous DAddr checked against
  the current resolved GPU footprint. Both share the texel coverage category.
- Ordinary texture producers: existing private `MarkModification(ImageBase&, kind)`,
  reached from storage-image descriptors, render-target PrepareImage, and texture
  copy/conversion preparation. Observes the entire guest allocation bounding
  range, **not an exact subresource/layout write mask**. Repeated marks are intents,
  not evidence a GPU write executed or completed.
- Texture read preparation also observes read-only intent. Render-target color/
  depth preparation passes an explicit diagnostic tag through PrepareImageView/
  PrepareImage; it is reported separately from texture/image descriptors and copies.
  No image flags, modification ticks, refresh or GPU commands change with the tag.
- Transform feedback: present bindings are counted as unresolved write intents
  because current binding metadata does not retain GPU VA. No guessed identity.

Texture identity uses the cached image's GPU VA only when its translation still
matches the image's actual contiguous DAddr extent. Sparse textures, image reuse
across channels, remaps or stale originating GPU VAs can be unresolved; the
observer does not invent a current address for these resources. Texture extent
overlap includes padding/mips/tiling and can overestimate actual write overlap.
GPU VA retained by descriptors does not by itself prove a cache resource's
contents are fresh. Identity registration never asserts that fact.

CPU/NCE/kernel raw pointer accesses, query cache producers, DMA/inline buffer
writes, upload staging and internal texture alias copies/scale operations that do
not pass MarkModification are **not instrumented**. They are explicit unregistered
producer categories, but cannot be numerically counted by these hooks. The
write-intent denominator covers only instrumented hooks; it is not a global
coverage percentage. Vertex/index/indirect read consumers are also not visited.
This stage cannot prove complete coherency coverage or absence of aliases.

## Snapshot, locking and lifetime

Use the existing 3.5A production `ResolvePhysicalRanges`. On a fresh result,
lock that snapshot's GPU page-table token, then device mapping token. Check both
generations and lifetime before walking pure GpuToCpuAddress into DAddr spans.
The physical result and DAddr provenance therefore existed together under one
matching stamp. Reject stale/odd generations; don't combine separate remap epochs.
On cache reuse validate the same paired tokens before using saved provenance.

Locks release before comparing geometry or returning. Remaps may occur afterward:
these are historical observations, not mapping-use leases/GPU execution admission.
Comparison intentionally includes historical mappings, even if no longer live;
different-DAddr witnesses are checked within overlapped physical page slices.
Different managers are distinguished by shared GPU mapping token identity, not VA.
Distinct backing epochs never alias. No page table/object pointer is retained.

Observer is GPU-thread confined and hooks execute with existing cache locks.
Final shutdown report acquires both cache locks, detaches the borrowed texture
diagnostic pointer, and the diagnostic member outlives cache destruction. It owns
only vectors and inert shared mapping/backing tokens, no guest arena/import leases.
Caught observation/logging exceptions only reduce telemetry coverage; they never
escape into GPU execution. No CPU read, scheduler wait or nested GPU callback.

## Bounded cost and validation

128 range entries; at most 64 MiB/query, 256 physical/DAddr spans per retained
entry, 4096 pair geometry/page comparisons and separately 4096 cardinality segment
scans per observation; at most eight sampled intervals and 256 deduplicated GPU/
DAddr references per sample. Reference deduplication/provenance lookup also have
fixed bounds. Eviction is a ring, not a
live resource retirement decision. Cache lookup is O(128); compare cost is bounded
but can be significant when enabled. Resolver plus DAddr page walks are O(pages),
including every mapped segment. Cache hits still acquire both mapping locks.
Whole-address-space generations may invalidate otherwise unchanged ranges; M1
measures that cost before any per-range generation optimization. Resolve can
temporarily allocate more than 256 segments before this observer applies its cap.
The renderer owns the diagnostic through a unique_ptr allocated **only when
enabled**, avoiding a large inline metadata reservation in the disabled renderer.
With it disabled all binding visitors, observations, clocks and paired mapping
locks for shadow work are bypassed. Ordinary resolver/validation callers use null
timer sinks: no timing clock reads, no additional mapping locks; original GPU->
device lock order remains unchanged. Startup diagnostic OOM leaves it disabled.
Only pointer branches and explicit render-target kind arguments remain on the
ordinary paths. No per-dispatch process-wide fd/VMA scanning or GPU stall.

Every runtime report starts with **coverage=unknown** and the uninstrumented
categories. Zero observed overlaps must never be read as global "no conflicts".
References retain conservative potential-write intent until their cache entry is
replaced/evicted, not actual data authority. Cardinality samples include partial
overlaps and repeated mappings and clip their interval at membership/provenance
boundaries. They are bounded historical samples, not a complete interval index.
Peak metadata measures payload at explicit observation points; errors can hide a
temporary allocator peak. Fixed window/histograms and capped ranges establish the
growth bound independently of the measured peak. Cardinality and cache updates
may be a significant enabled CPU cost; observer timing includes these, resolver
timing does not. Read-only vs potential-write data cannot prove the frequency of
the actual A=99/B=10 overwrite without GPU/data/version observations (out of M1).

```sh
python tools/physical_shadow_tests/run_tests.py --cxx g++
# If real fmt 9+ headers are not on the compiler's system include path:
python tools/physical_shadow_tests/run_tests.py --cxx g++ --fmt-include /path/to/include
```

Tests use the production observer and extracted production binding visitor,
texture producer hook, GMMU/resolver/device snapshot functions. Tables/cache
objects are fixtures. They cover different-DAddr/manager physical aliases,
partial ranges, mixed buffer/scratch/texel/texture coverage, scatter padding,
whole-AS/device generation invalidation, mixed-snapshot rejection, stale texture
provenance, holes, bounds, OOM containment and fixed metadata capacity. They do
not execute Vulkan/NCE or constitute device cost/coverage measurements.

Available-platform validation: new suite passes Windows MinGW and Linux GCC;
Linux ASan/UBSan passes. All nine existing gather/scatter runners pass on both
platforms, including CPU-write, unmap preservation, GPU-stop and mixed binding
alias tests. Production resolver and M0 suites pass Linux (M0 also Windows).
Actual vk_rasterizer.cpp compiles on Linux with real manager/cache headers and
isolated logging/assert/platform shims; not a full Android link/build. The
separately extracted production report is compiled/exercised with real fmt headers
to check formatting, explicit unknown coverage and null diagnostic bypass. No
local Android full build.

Workflow APK and SMU/SMO diagnostic sampling are the next validation step. Record
all counters above and an off/on same-scene comparison before choosing M2 data
structures. In particular don't interpret import reuse or successfully registered
geometry as GPU/NCE concurrent access safety.
