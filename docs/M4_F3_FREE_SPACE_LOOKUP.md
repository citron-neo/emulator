# FreeSpace refusal preserves owner inventory

Date: 2026-10-23 (Asia/Singapore). Baseline: `5a9bb1727c`.

The actual NV FreeSpace body used `allocation_map[params.offset]` to look up an
existing allocation. An unknown address inserted a default allocation before
parameter validation returned BadValue. Zero pages/page size could match that
default record and proceed to allocator Free and Success. A malformed request
therefore changed ownership bookkeeping even though no allocation belonged to it.

FreeSpace now uses `at()`. The existing out_of_range handler returns BadValue
before unmap_started, without quarantine or shutdown. Existing allocations and
checked-unmap failure handling retain their behavior. This is a narrow refusal
fix, not a complete mapping transaction or guest exclusion protocol.

The failure-first actual-body test reproduced
`FAIL missing FreeSpace allocation changed owner inventory` on the old code.
An initial test compilation warning about copied structured bindings was fixed;
that compiler failure was not counted as runtime evidence.

The new positive covers both ordinary and zero-sized requests to a missing
address, compares the complete fixture ownership/allocator snapshot before and
after each refusal, and then successfully frees an existing allocation. Its
mutation control restores the inserting lookup and must hit the exact original
inventory failure.

Local Windows commands:

```powershell
python tools/gpu_lifecycle_tests/run_address_space_mapping_tests.py --repeat 3 --control-prefix FreeSpace --output-dir .diagnostics/m4-free-space-final
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp --output-dir .diagnostics/m4-free-space-production
```

The complete mapping positive suite runs with the new refusal test. The selected
FreeSpace controls cover ignored closure, early Entry release and inserting
lookup. Other mapping controls are unchanged and are not claimed rerun here.
GPU/System, allocator and renderer resources remain fixtures in runtime tests;
the production compilation uses actual headers and emits an AMD64 COFF object.
No application link, Android/remote build or native Vulkan run is claimed.

Results: the ten prerequisite GPU lifecycle cases and the full mapping positive
suite passed three repetitions. The cancelled-read control and all three selected
FreeSpace controls hit their expected runtime failures. The production object
compiled without diagnostics; input hashes across both final manifests and the
object digest/AMD64 signature were independently checked before deletion.

All THREE M4 phases remain unfinished and ordinary GuestBackingAdmission stays
Unknown. Main is unchanged. Temporary executables, objects, generated fixtures,
manifests and logs are removed after verification; this conclusion and test
source remain.
