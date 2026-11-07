# M4 Windows mapping owner checkpoint

Date: 2026-10-27 (Asia/Taipei). PR #375, `codex/issue-372-fixes`.
Initial baseline: `e32d9d5f1a9d2e5f77bd69883dad8e69a1b57666`.
Final validation base: `9f4dc72c91e77e73f5c114b029531b4eb41da7c3` plus the recorded candidate.
CI remains deferred; no main or FFR worktree changes.
[Commands, inputs, exact outcomes and cleanup](M4_WINDOWS_MAPPING_OWNER_RESULTS.json).

## Finding and narrow repair

Move a real HostMemory before exporting any pointer, then call the moved-from
facade's four mutable/const backing/virtual getters. On the baseline they return
addresses into the moved allocation. A write through that export reaches both
actual Windows mappings, although the facade has lost allocation ownership and
the mapping-domain ledger. This is a new export after transfer, not merely an
already-issued pointer surviving a move.

The four getters now return null when the implementation owner is absent.
Each getter has its own omitted-guard negative control linked with the complete
actual HostMemory.cpp and common library. A fifth counterfactual falsely enrolls
Windows in the existing ledger; the same test must reject that change.
No stronger freeze, pointer revocation or Windows mapping enrollment was added.

The fixture uses real Windows allocation granularity, HostMemory backing and
placeholder mappings, two aliases of the same physical bytes, a joined writer,
unmap/remap to different physical bytes, PAGE_READONLY observed by VirtualQuery,
restored write permission, retained allocation after facade retirement, and the
final native allocation release. The saved pointer still writes the retained
allocation; retaining storage does not revoke an escaped capability. All these
unproved freezes must refuse. GuestBackingAdmission remains Unknown.

## Synced owner integration

The branch already contains the FFR post-wake nvhost rearm repair and command
segment/compute descriptor snapshot changes. Their focused regressions are
retained. The two synchronized Maxwell CAS commits (`2c306bccd7`, `9f4dc72c91`)
are preserved unchanged; this checkpoint makes no shader/CAS runtime claim.

Upstream removed RendererNull, which the cold restricted owner still referenced.
The owner now uses a private cold backend: draw/compute/channel/query/fence/copy
requests fail-stop before accepting work. Empty cache/worker maintenance has no
queued work to complete. It cannot manufacture a GPU fence callback. Normal
renderer selection is unchanged. This is actual System/Kernel/KProcess/KEvent/
ProcessCohortStop linkage with an intentionally non-rendering backend, not native
GPU completion evidence. A direct fence request must terminate with exit 86 and
the renderer refusal marker before its callback can exit 92.

Integration compile errors in the existing M4 owner path are also repaired:
remove the second, byte-identical SlabHeapContainer definition left by the merge;
replace the removed ASSERT macros in the owner-held channel lookup and NVDRV
channel-owner constructor with explicit missing-owner/channel rejection. No renderer teardown or unrelated subsystem repair.

## Build limitations and diagnosis

The first fixed-header incremental run reused a stale MSVC test object: CMake's
localized include-prefix configuration does not match the emitted include text.
The same native fixture was rebuilt fresh, and final owner integration began
with clean objects after all header changes. Retried source-only changes compile
normally; the invalid incremental run is retained as a failed attempt, not
acceptance. VSLANG did not repair dependency scanning on this installation.

The full native link also encounters upstream RendererVulkan optional-copy
errors in its swapchain/present-manager initializers. Only a temporary acceptance
copy changes these two initializers into explicit-return lambdas. The unchanged
tracked Vulkan source still has this build limitation. The cold owner does not
instantiate this renderer. Exact generated copy, source hashes and overlay are
recorded; this accommodation does not establish an unmodified full application
build or Vulkan runtime success. The earlier local C4242/C4244 exception remains
restricted to vk_rasterizer.cpp, as in the preceding native-owner record.

Eight selected production translation units compile separately with strict
Windows g++ flags to AMD64 COFF objects, without either Vulkan accommodation:
HostMemory, DeviceMemory, RestrictedProcessOwner, its private cold renderer,
System, KernelCore, GPU and the NVDRV channel-owner constructor. This is compilation evidence, not Android validation.

## Acceptance and next gate

Final local results: baseline witness/refusal 6/6 processes; fixed Windows
mapping plus five one-change controls 18/18; real native owner positive, Unknown,
unpolled and terminal/missing-foundation controls 21/21. Each native positive
run checks 35 assertions, Unknown 8 and unpolled 5. The nvhost owner regression
records 16 normal cases, one cancellation fail-stop and nine exact controls;
FFR post-wake rearm passes three repeats. Eight production objects compile.

Cleanup verified each temporary leaf by size/SHA256 before unlinking: 1,470 files,
3,646,124,949 bytes removed, with no temporary binary/fixture leaves remaining in
this task directory. Logs, commands and manifests remain; dependency caches are
kept outside it.

See the result record for commands and exact outcomes. Baseline witnesses,
fixed runs, one-change negative controls, native owner positive/Unknown/unpolled
cases and terminal controls are distinct evidence classes. A timeout, missing
marker or unexpected exit is failure, never an accepted Unknown result.

Physical mapping/alias freeze remains open. Windows deliberately refuses its
unsupported mapping ledger; ordinary DeviceMemory is still untracked. Existing
writable aliases have not been revoked, active guest ownership is unproved, and
no native GPU completion or first guest-backed Vulkan Copy is accepted. Continue
with a complete alias/producer closure that can either prove exclusion or refuse;
do not expand already-passed controlled owner combinations.
