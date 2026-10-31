# M4 phase two: checked UnmapMemory and retained failure ownership

Date: 2026-10-22. Branch: `codex/issue-372-fixes`, baseline `e170622029`.
Main is unchanged. This closes the callback-result gap from the
[GMMU mapping audit](M4_F3_GMMU_MAPPING_LIFETIME.md), not all three M4 phases.
GuestBackingAdmission remains Unknown and ordinary F2 routing remains disabled.

## Execution boundary

| Observation | What is proved | Required action |
| --- | --- | --- |
| Completed | The entire host callback returned normally, including buffer-cache work and pipeline invalidation | Continue the GMMU/NV unmap; this is not a universal native GPU or guest physical publication certificate |
| Cancelled, no earlier effects | The acknowledged action never started, including queue rejection, Stop cancellation and failed-worker fence acknowledgement | Keep mappings, pins and allocation owners; fail the operation |
| Failed | Dispatch, an inline callback or an executing worker callback failed; partial effects are possible | Keep the remaining owners, quarantine mapping mutation and request shutdown |
| Cancelled after a completed range or batch prefix | The cancelled action itself did not run, but previous work already completed | Preserve the known prefix, retain the failed suffix and allocation record; do not roll back or retry the batch |
| Metadata/queue allocation failure after callback completion | Callback completion does not make cleanup atomic | Quarantine the address space and retain remaining owners; no success result |

UnmapMemory now uses the existing RunAcknowledgedCallback. Queue acknowledgement
or a shutdown wakeup cannot manufacture completion. A running callback is waited
to its actual return; cancellation wins the Pending-to-Cancelled transition and
prevents a retained late closure from starting the action. Worker failures keep
their existing failure handling. Inline dispatch exceptions are translated into
the same checked UnmapFailure. The worker also handles that typed failure through
its normal failure-shutdown path.

Inside the GPU-owner callback, TryRunBufferCacheCpuWrite is consumed as an
optional: false is a legitimate completed WriteMemory result; nullopt refuses.
Failure before pipeline invalidation cannot be reported as completed unmap.
No queue type, queue scheduling, scheduler continuation, normal synchronous
ordering or teardown order has changed.

## GMMU and NV ownership

MemoryManager::Unmap only clears PTEs after all backend unmap callbacks return
successfully. A typed cancellation/failure preserves its outcome and includes
whether earlier physical ranges had completed. Generic failures conservatively
mean possible effects. Scratch ranges are cleared separately from live mappings.
The GMMU sets an atomic terminal unmap-failure latch under the mapping-update
mutex. Map, MapSparse and later nonempty Unmap check it under that same mutex;
there is no reset/reopen API. Even a first-range no-effects cancellation uses
this conservative terminal policy, rather than inventing a retry permission.

FreeMappingLocked and UnmapBuffer release allocator/pin ownership only after
checked GMMU unmap. FreeSpace may already have completed earlier child mappings:
a failed suffix does not release the remaining child or the allocation record.
NV returns InvalidState, rejects future mapping operations and refuses channel
binding to a quarantined GMMU. Callback-independent cleanup exceptions also mark
the GMMU quarantined. Shutdown/capture destruction occurs after releasing the AS
mutex, while the previously admitted publication Entry still retains lifetime.

The second GMMU unmap caller, NvMap::UnmapHandle, now returns checked success.
It keeps its pending queue entry, Host1x reservation and SMMU mapping when GPU
unmap fails. FreeHandle restores the guest duplicate on failure instead of
removing the handle or reporting it unlockable. PinHandle reclamation stops
without popping the failed queue owner or pinning the new handle. Both paths
release handle and queue locks before notifying shutdown.

A final pin is dropped only after allocating its queue's retained owner. If that
allocation fails, the original pin survives. A quarantine racing low-area Map
is also checked before publishing a new pin; its cold virtual reservation stays
with the terminal allocator until owner teardown. These are retained failure
states, not atomic rollback or native completion authority.

## Tests before fixes

The actual ThreadManager/RunThread/queue plus actual Vulkan UnmapMemory body
compiled before the repair and failed precisely with
`FAIL cancelled unmap reported callback completion` on pre-start rejection.
Additional focused witnesses were written before their corresponding follow-up
repairs and compiled to these exact failures:

- `FAIL failed queue allocation dropped last pin`
- `FAIL post-callback cleanup failure escaped quarantine`
- `FAIL quarantine race escaped low-area pin`

Syntax/compiler failures during fixture development were rejected as evidence.
None of the above accepted witnesses depends on a crash or timeout.

## Windows validation

```powershell
python tools/gpu_lifecycle_tests/run_checked_unmap_tests.py --repeat 3 --output-dir .diagnostics/m4-unmap-worker-final
python tools/gpu_lifecycle_tests/run_unmap_ownership_tests.py --repeat 3 --output-dir .diagnostics/m4-unmap-ownership-verified
python tools/gpu_lifecycle_tests/run_address_space_mapping_tests.py --repeat 3 --output-dir .diagnostics/m4-unmap-mapping-regression
python tools/gpu_lifecycle_tests/run_channel_rebind_tests.py --repeat 3 --output-dir .diagnostics/m4-unmap-channel-regression
python tools/gather_buffer_tests/run_unmap_alias_tests.py --cxx g++ --output-dir .diagnostics/m4-unmap-alias
python tools/physical_resolver_tests/run_tests.py --cxx g++ --output-dir .diagnostics/m4-unmap-resolver
python tools/physical_coherency_tests/run_windows_production_checks.py --output-dir .diagnostics/m4-unmap-production-verified
```

The worker suite runs real ThreadManager, RunThread, bounded queues and the
acknowledged callback helper. It tests normal synchronous/asynchronous execution,
nested owner execution, pre/post-start rejection, queued cancellation, Stop during
an active callback, failed-worker acknowledgement, inline failure, each cache
stage and optional false/nullopt. Five controls require exact runtime failures.
Renderer/cache/Core/platform/GPU-facade objects are controlled fixtures.

The ownership suite executes real GMMU mapping/unmap bodies, five NV mapping
operations, FreeMappingLocked, and NvMap UnmapHandle, PinHandle, UnpinHandle and
FreeHandle bodies. It covers cancelled/failed first ranges, completed physical
and child prefixes, retained low-area queue/SMMU/handle owners, lock availability
at shutdown, last-pin allocation failure, post-callback cleanup failure, and a
low-area quarantine race. Nine precise controls reject missing ownership,
quarantine, result propagation and lock-release boundaries. Tables, allocator,
handles, nvmap services and SMMU are fixtures; this is not native backing proof.

The existing mapping suite repeats with twelve controls. Channel ownership
repeats with ten controls, including a new quarantined-GMMU binding control.
Ten lifecycle cases and cancelled-read coverage remain prerequisites. The alias
readback/scatter/surviving-upload and production resolver regressions also pass;
the isolated cache fixture's local retry does not reopen a real quarantined GMMU.

The Windows compiler helper now covers nine complete production files, including
vk_rasterizer.cpp, gpu_thread.cpp and nvmap.cpp. Real pinned Vulkan/SPIRV/Sirit/
VMA/Utility/unordered_dense headers were initialized, without changing gitlinks.
No fixture headers are used for these objects. All final results and object/input
hashes are verified before deleting binaries. This is object compilation, not a
full application link or native Vulkan/device execution. The existing remote
workflow includes the new suites for a future dispatch; no new remote run is claimed.

All core positive suites passed three repetitions. The 36 precise controls
(five worker, nine ownership, twelve NV mapping and ten channel binding) passed,
as did cancelled-read coverage and the alias/resolver regressions. All nine final
production files generated AMD64 COFF objects with zero compiler diagnostics.
All 57 recorded source-hash entries from five manifests (29 unique inputs),
object digests and machine signatures were independently matched.
See the committed [result summary](M4_F3_CHECKED_UNMAP_RESULTS.json).

Final detailed logs/manifests and pinned-dependency identities remain locally in
`.diagnostics/m4-unmap-results/`. Cleanup removed 554 temporary files from this
turn, including all 84 generated exe files and 28 objects across discovery and
revalidation passes. The final nine objects were checked before deletion.
Previous result bundles and the shared/pinned dependency caches are retained.

## Remaining scope

Completed means this host callback completed. WriteBackDownload's existing
unmapped-device-page skip policy is unchanged; callback completion does not
certify full guest physical publication. Submission identity, device progress,
visibility, mapped-memory synchronization, immutable destinations and checked
readback still need their own end-to-end proof. Native failure retirement is
not replaced by the GMMU latch.

Direct raw GMMU reads/writes and pointer loans still require facade/backing/alias
lifetime; HostMemory VMA remap remains high risk. Independent SMMU/mapping/export
producers are not covered by the controlled NvMap fixtures. Generic creation/Map
transactions and unrelated SMMU cleanup failures are not proved atomic here.
An escaped System/GPU/service facade is still the caller's lifetime obligation.
No whole-guest stop, all-producer drain or ordinary BufferCache admission is granted.
