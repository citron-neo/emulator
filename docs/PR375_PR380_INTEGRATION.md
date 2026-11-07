# PR #375 / #380 integration roadmap

## Current continuation: 2026-10-16 — Restricted Eligibility blocked; readback isolation

The next [restricted eligibility acceptance](M3B_F2_RESTRICTED_ELIGIBILITY.md) is blocked:
ordinary scalar callers cannot yet prove complete physical producer exclusion. Work advances
readback isolation first; the original unbound F2 witness still reproduces backing77/cache10.
See current progress for this continuation's validation, distinct from the earlier green run.

## Verified earlier continuation: CPU Scheduler Handoff

An explicit nonblocking bridge now runs request continuations on the existing GPU ThreadManager.
Per-Memory Close cancels/quarantines only its requests and joins bounded execution visits before
production process page-table finalization, without draining unrelated GPU work. Cancelled,
certified Refused and Unknown have distinct scheduler actions; Unknown never permits replay.
The five requested assertion groups, queue dependency-cycle mutation and lifetime-close mutation
are in the [current progress entry](../tools/physical_coherency_tests/M3B_PROGRESS.md).

The native executable now uses CpuManager/KThread/KScheduler service fibers, complete initialized
KProcess teardown and real cache/sys-core/PTE/counter locks with Vulkan checked transfers.
Its final lifecycle-hardened revision `02610e1766` passed Android and Linux in
[38864622309](https://github.com/Splaser/emulator/actions/runs/38864622309), including 64 process
teardowns, GPU99/CPU77/GPU99, Cancelled-owner release and delayed preservation/frame-reuse safety.
The native executable passed 1,696 assertions; production tests passed 130 cases.
The no-presentation renderer uses software Vulkan; it does not execute an ARM game image.
Automatic ordinary-game routing, alias-process qualification, full producer exclusion, trusted
Unknown recovery and Android hardware evidence remain gates. F2 stays Open.

The [producer inventory](M3B_F2_PRODUCER_INVENTORY.md) classifies ordinary CPU writes,
CAS/NCE/JIT, DMA, buffer/texture transfers, deferred callbacks, raw pointers and mapping/lifetime
mutations. Its forbidden rows are activation requirements, not currently enforced exclusions.
See also the [native acceptance and execution-lease lock audit](M3B_CPU_SCHEDULER_ACCEPTANCE.md).

### Earlier continuation: CPU request/completion owned, automatic dispatch pending

CI [38839879554](https://github.com/Splaser/emulator/actions/runs/38839879554) passed both Android
and Linux at `273bfb2f5d`. The next slice adds actual CPU-owner immutable scalar request
preparation, single-use GPU acceptance/cancellation, and completion publication only after the
actual ordinary store, alias tracker publication and admission release. The actual Vulkan
advance endpoint consumes the request's owned value; callback/readback return cannot acknowledge
CPU success. Accepted Unknown requests remain retained with physical/native admission owners.
See the [current progress entry](../tools/physical_coherency_tests/M3B_PROGRESS.md).

The handoff is nonblocking and explicit. Automatic game-CPU scheduling/suspension and full
producer exclusion remain pending, along with trusted recovery/device proof. Stored means CPU
commit; GPU readiness still requires checked refresh. Unbound F2 remains backing77/cache10.
F2 stays Open, and the next-stage acceptance gates below remain binding.

### Earlier continuation: ordinary body bound, automatic enrollment pending

Native [38835727709](https://github.com/Splaser/emulator/actions/runs/38835727709) passed both
jobs and all configured regressions at `17f1e65977`. The continuation now makes typed transaction
commits invoke the actual ordinary Memory::Write8/16/32/64 through a single-attempt thread-local
binding. Context/VA/width/value qualification and every refusal precede legacy pointer lookup;
the enclosing cache transaction still owns preservation and post-store tracker publication.
See the [current progress entry](../tools/physical_coherency_tests/M3B_PROGRESS.md).

This installs an ordinary-body participant on the explicit isolated GPU-owner continuation.
It does **not** automatically enroll game CPU callers, create registrations for them or add a
CPU-to-GPU wait. Caller suspension, full producer exclusion, recovery and device evidence remain
open. Unbound ordinary writes and their backing77/cache10 defect control remain unchanged in
behavior. F2 stays Open; the next-stage gates below are not satisfied by this binding alone.

### Earlier continuation: transaction boundary implemented, ordinary caller pending

The home checkout has resumed from `89665bef71`. The first slice of the next stage below adds
an owned cache-side scalar write transaction, checked Vulkan GPU-dirty preservation, exact
post-store alias publication and exclusion before actual upload tracker consumption. It retains
GPU dirty obligations and owners through failures, and corrects controlled null-buffer sentinel
checks. See the [progress entry](../tools/physical_coherency_tests/M3B_PROGRESS.md#write-transaction-ownership-and-gpu-preservation-slice-2026-10-16--f2-open).

**This is preparatory integration, not completion of the ordinary production write stage.**
Ordinary Memory::Write has no transaction caller yet; other bound GPU work/GC/download and
uncontrolled producers still require enrollment/exclusion, and trusted recovery/device evidence
remain open. The explicit GPU-owner continuation requires its footprint isolated from those
producers. The unchanged ordinary F2 witness still exposes backing77/cache10. F2 remains Open.
The 2026-10-15 checkpoint below is historical; its next-stage acceptance gates remain binding.

## Current handoff: 2026-10-15 end of day

Development stops here for today. This update records progress and the next scope only;
do not start implementation as part of this handoff. The current continuation branch is
`codex/issue-372-fixes` in `Splaser/emulator`; personal `main` is kept at the same commit.
The older integration-branch resume instructions below are historical and superseded.

Latest tested production source: `f00768b0e6cd988db2b3c411e04b48c35f9c7601`.
[CI 38741531093](https://github.com/Splaser/emulator/actions/runs/38741531093) passed Android
arm64 production-library compilation, Linux CLI/shader/unit-test builds, production tests,
Linux HostMemory/backing lifetime tests and coherency/lifecycle regressions. This establishes
compilation and those regression results, not that ordinary writes are safe in every game or
that the new protocol has been validated on real Android hardware.
This documentation-only checkpoint does not change that production source and does not require
another native CI dispatch.

**F2 — CPU/GPU write coherency: Open.** Progress is an engineering description, not a measured
completion percentage. Available groundwork includes physical pins, joint range admission,
page writer gates, actual controlled BufferCache registration/upload, checked completion and
deterministic positive/negative controls. Still open: ordinary CPU production callers,
GPU-dirty preservation, failure recovery, exclusion of uncontrolled producers and device proof.
Implementation/evidence details remain in
[`M3B_CONTROLLED_FIRST_REGISTRATION_HANDOFF.md`](M3B_CONTROLLED_FIRST_REGISTRATION_HANDOFF.md)
and [`M3B_PROGRESS.md`](../tools/physical_coherency_tests/M3B_PROGRESS.md).

### Next stage: M3B-F2 Production Write Transaction

Concentrate on actual ordinary CPU writes as one `preserve -> store -> publish` transaction
under concurrent real GPU work. Stop expanding DMA and peripheral features. Do not use this
stage to refactor DMA, Texture Cache or NCE, or to replace every write path at once. Controlling
the change surface takes priority over adding more protocol-only tests.

Initial eligibility is limited to ordinary `Memory::Write<u8/u16/u32/u64>` on a single page,
with a stable mapping and provable physical identity, where the real BufferCache participates
in both admission and preservation. Prove and enforce that eligibility before routing a write.
Entering `WritePrepared*` alone is not transaction acceptance. NCE, JIT fastmem, cross-page
writes and unknown producers keep their original paths and receive no new coherency guarantee.
Uncontrolled producers must be excluded from any new guarantee, not assumed cooperative.

Three mandatory proof boundaries:

1. Preserve GPU-dirty contents **before** the CPU store, including untouched bytes. For GPU
   contents `[99,99,99]`, a one-byte middle CPU store of 77 must finish as `[99,77,99]`;
   `[10,77,10]` is a failure. Rejecting GPU-dirty pages is not proof of this preservation path.
2. Publish the exact successful store extent to the real tracker while admission remains held.
   A concurrent GPU upload must not consume/clear that publication prematurely. Prove the
   transaction's ownership and ordering, not merely that a dirty bit was set once.
3. Exceptions and partial failures during preservation, store or commit must not incorrectly
   clear dirty state or release admission before the effects are understood. Retain the accepted
   physical/resource owners and quarantine uncertain effects until trusted recovery; neither a
   callback return nor failed preservation permits success publication or an unsafe fallback.

Retain both registration interleavings and real tracker acceptance:

- CPU writer wins the gate: registration waits for actual store77, then uploads77.
- Registration closes first: fast `NeedSlow` is followed by an actual protected cached store77,
  real tracker dirty publication, upload77 and clean tracker state after verified completion.
- Protected accepted transactions finish with CPU backing77/cache77; a protected reader must
  not observe stale10 after the store. Preserve the disabled-handshake ordinary-path control
  `backing77/cache10` and its failing `--require-fixed` baseline until that route is repaired;
  keep an explicit bypass control after introducing the protected production route.
- Add the GPU-dirty complement acceptance above, with real GPU work, and failure/interleaving
  evidence for all three boundaries. Existing controlled fixtures, compilation and green CI
  remain necessary evidence but cannot alone close F2 or justify whole-game safety claims.

Resume on `codex/issue-372-fixes`: inspect local changes, fetch, use `git pull --ff-only`, and
match CI to its exact source SHA. Preserve unrelated work, active PR branches and the historical
F2 branch. Tomorrow's implementation should start with the transaction/ownership boundary and
the partial-write preservation case, rather than broadening producers.

## Baseline and scope

- Integration branch: `codex/pr375-pr380-integration`.
- Start: PR #375 head `3de1870c78babbdb934c875268fb49b43e5d3458`.
- Upstream: PR #380 merge `57b1925f51f450759439603410333c85abf7becc`.
- Keep coherency, source snapshots, physical-shadow diagnostics and explicit GPU shutdown
  boundaries while incorporating upstream translation and synchronization changes.
- Keep the eight newer local F1/F2 commits, including candidate admission, out of this branch.
  Production F1/F2 are not solved by this integration.

## Ordered work

| Order | Module | Required result |
| --- | --- | --- |
| 1 | `host_translate_info.h` | Keep independent bindless and passthrough fields. |
| 2 | `maxwell_dma.cpp` | Keep upstream remap support and local M0 source snapshots. |
| 3 | `buffer_cache.h` | Combine submission ticks/barriers with `EndGuestCpuAccess`; preserve scatter readback. |
| 4 | `vk_rasterizer.cpp/.h` | Keep physical-shadow scope and deferred WAIT_FOR_IDLE. |
| 5 | `renderer_vulkan.cpp` | Keep drain/join ordering; scope `submit_mutex` away from worker joins. |
| 6 | `host_memory.cpp` | Reconcile lazy sparse fd publication and VMA merging with an explicit validation boundary. |

## High-risk module: HostMemory

HostMemory requires dedicated Linux/Android validation before claiming concurrent dma-buf
remapping safe. A successful Windows compile cannot establish that property.

Required invariants and checks:

- Discover lazy fd identity when a chunk is published, including out-of-order sparse slots and
  mixed exported/memfd fallback backing. Do not assume a fixed array or stop at the first hole.
- Distinguish the stable physical `backing_base` view from remapped guest virtual aliases.
  An allocation pin alone does not protect a borrowed virtual alias from remapping.
- Make layout/fd metadata lock ordering explicit. Do not perform GPU waits under these locks.
- Stop and join the mapping sampler before freeing its state, backing or file descriptors.
- Exercise concurrent Map/Unmap/Protect, merge pressure, borrowed pointer accesses, failure
  fallback and shutdown on an appropriate target. Until proved, do not enable background
  MAP_FIXED merging over active guest aliases in the lazy path.

## Other semantic gates

- Retain the exact old NCE null-wrapper recovery before upstream's new generic Core::Memory
  instruction emulation. It is a separate compatibility path from the host ARM64 register
  guards, which also remain. NCE signal behavior requires target validation.
- `WriteBlockUnsafe` hole reporting is not a preservation transaction: do not interpret it as
  callback completion, immutable destination identity or complete physical publication.
- Preserve GPU emulation, recording, fencing, pipeline and presentation shutdown boundaries.
- Do not overwrite main, PR #375 or the F2 branch merely to clear GitHub conflicts.

## Acceptance evidence

Record compilation, focused regression results, source revisions and environment limitations
here after integration. A clean merge is only the first check; distinguish a real production
build from extracted-source tests and from Linux/Android/device execution.

## Implemented integration boundaries

- All seven textual conflicts reconciled in the order above.
- HostMemory records file identity at lazy exported-fd publication, uses dynamically sized
  optional slots, and scans past cold holes. Lookup uses `backing_mutex`, with lock order
  `layout_mutex` then `backing_mutex`; chunk publication never acquires `layout_mutex`.
- Lazy VMA merging is explicitly disabled (also checked inside `MergeMappings`); the upstream
  eager supplied-fd path remains. This retains the lazy baseline's behavior and VMA pressure
  limitation rather than claiming the new concurrent remapping contract is proved.
- Final renderer WaitIdle takes `submit_mutex` only after all submission workers join. The
  regression fixture verifies worker drains can acquire it and WaitIdle holds it.
- Submission-tick, sparse registry/parser and the exact NCE recovery block have extracted-source
  regression checks. These do not emulate hardware or prove the excluded fault/remap paths.
- The integration workflow compiles real Linux CLI/shader/tests, runs Linux HostMemory and
  existing coherency/lifecycle regressions, and cross-compiles real arm64 production libraries
  with NDK r29 through CPM. It does not publish a release.

Status (2026-10-15): conflict resolution, the complete Linux production/unit-test/HostMemory/
coherency/lifecycle CI job and Android arm64 production library compilation passed at
`4fc3f7e768`. The user accepted upstream device validation and promoted the result to main/#375;
M3B continuation and its separate validation boundaries are recorded below.

## Handoff: 2026-10-14 (Asia/Taipei)

The integration is committed and pushed to the personal fork, on
`Splaser/emulator:codex/pr375-pr380-integration`. The roadmap commit is `6476a0e906`,
and the upstream merge/integration commit is `eabdc59cd4`.

### Completed locally

- All seven conflicts resolved; upstream #380 is an ancestor of the integration branch.
- Windows GCC 13.2: all 17 runnable existing regression entry points passed: four GPU lifecycle,
  nine gather-buffer, DMA snapshot, physical resolver, physical shadow, and chunk-import mocks.
- New `tools/pr375_pr380_tests/run_tests.py` also passed on Windows. It compiles extracted actual
  submission usage, mapping parser/sparse registry, and the narrow NCE recovery code.
- Renderer orchestration checks cover submission-lock availability while draining and lock
  ownership during final WaitIdle. Normal CPU-read/composite completion regressions pass.
- Linux-only HostMemory/export/cold resolver runners cannot run on this Windows host. The
  Windows imported-buffer test uses mocked fstat/driver behavior, not a real dma-buf driver.

### CI state at handoff

- Initial source revision: `eabdc59cd4e3f8a573c08aa9665764427f822201`.
- Initial run `38575916030`: Android setup failed before CMake because `sdkmanager` was absent
  from PATH. The workflow now installs the Android SDK tools explicitly with setup-android.
- A duplicate manual dispatch (`38575965148`) interacted with cancel-in-progress; both initial
  runs ended cancelled. Neither is evidence of a successful production build.
- The handoff commit push starts a fresh single push run. Use the latest integration-branch run
  for its actual status; do not treat the local extraction tests as full compilation proof.
- Do not claim the integration ready for main/#375 until native builds and regressions pass.

CI follow-up: run `38576377755` failed before compilation. The Linux shell rejected checkout
ownership at `Record exact build inputs`; that step now trusts only `$GITHUB_WORKSPACE` via
`safe.directory` before invoking Git. Android setup tried to install the obsolete SDK `tools`
package; its explicit package list now requests `platform-tools`. Both fixes are workflow-only;
production compilation remains unverified until a subsequent run passes.

### Resume: 2026-10-15 (Asia/Singapore)

- Synced the personal fork and resumed from `a4a7462944` on the integration branch.
- Run `38577756839` reached Linux production compilation, then GCC rejected the GC-local
  `PendingDownload` because it shadows the async download member added by #380. Renamed only
  the GC-local type to `PendingGCDownload`; readback ownership and eviction behavior are unchanged.
- The same run stopped Android configuration in Oaknut's `find_package(Catch2 3 REQUIRED)`.
  The top-level CTest option defaults on even with `CITRON_TESTS=OFF`; CPM now passes
  `BUILD_TESTING OFF` specifically to Oaknut, without disabling the project's Linux unit tests.
- The extracted integration contracts pass again on Windows. No local Android production build
  was attempted. Linux/Android production build and Linux runtime regression results remain
  pending the next remote workflow; the failed run never reached those regression steps.
- PR #375, personal main and F2 remain unchanged by these integration fixes.

Remote follow-up on `583cef2834`, run `38664784880`:

- Android configuration passed, including Oaknut; real arm64 compilation then exposed a missing
  `<cassert>` include in `src/common/fs/file.cpp` with precompiled headers disabled. Added the
  direct include for its content-URI assertion.
- Both Ninja build steps now use `-k 0` to collect independent compilation errors in one run.
  They still fail on compiler errors; this does not relax warnings or skip production targets.
- Linux passed the previous GC compilation failure, then GCC rejected possibly uninitialized
  `KMemoryInfo` in `FindFreeArea`. `QueryInfoImpl` can fail without filling its outputs; the
  debug-only success assertion did not guard their use in Release. Failed random candidates
  are now skipped explicitly, retaining the assertion and successful-query path.
- Windows DMA snapshot, renderer orchestration and GPU-write coordination regressions passed
  again while waiting for Linux CI. These remain distinct from native Linux and device results.
- `tools/pr375_pr380_tests/run_free_area_tests.py` compiles the actual `FindFreeArea` body and
  `R_ASSERT` macro with `-O3 -DNDEBUG`. Mocked queries verify success, retry after failure,
  all-failed fallback and ASLR disabled; a negative control removes the guard and must fail.
  The Windows run passed. This covers candidate selection, not a real kernel memory-block tree.

Remote follow-up on `463702d541`, run `38666031080`:

- Real Linux CLI, shader tool and unit-test compilation passed; CTest passed its production
  test target. All four Linux HostMemory/export/backing-lifetime/cold-resolution runners passed.
  Export tests use a fake Vulkan driver with real Linux mappings, not a real dma-buf device.
- Integration contracts, DMA snapshot, all nine gather-buffer runners and physical resolver
  regressions passed on Linux. The physical-shadow runner then failed before its tests because
  explicitly adding `/usr/include` as `-isystem` reordered GCC 16's system search and broke
  `#include_next <stdlib.h>`. CI now uses the installed fmt headers via default compiler search.
  Chunk-import mocks and four lifecycle runners after that step did not run in this revision.
- Android's complete error list identified missing `<cassert>` in `path_util.cpp`, `<span>` in
  the interpreter header, `<atomic>`/`core/memory.h` in the interpreter implementation, and
  complete thread/memory definitions in the patcher. Added those direct includes for no-PCH builds.
- Renamed the moved fence lambda capture in GPU composite requests to avoid Clang's
  `-Wshadow-uncaptured-local`, preserving callback ownership and fence ordering.
- The Release ASLR regression runner and its CI invocation are included with these fixes.
  Android production compilation, remaining Linux regression results and device validation
  remain pending; these Linux successes do not establish NCE signal or Vulkan-driver behavior.

Remote follow-up on `4fc3f7e768867e6cc5a8474843ad32e68ec73dc0`, run `38668008146`:

- Linux job `116204276049` succeeded: real CLI/shader/unit-test builds, production CTest target,
  all four HostMemory/export/backing-lifetime/cold-resolution runners, integration contracts,
  Release ASLR positive/negative controls, DMA snapshots, nine gather-buffer runners, physical
  resolver/shadow, chunk-import mocks and all four GPU lifecycle runners passed.
- Android job `116204275783` succeeded: all 1547 Ninja actions completed through CPM with NDK
  `29.0.14206865`, arm64-v8a/API 30, Release and precompiled headers disabled. The real `core`,
  `video_core` and `shader_recompiler` static libraries linked, including NCE/interpreter/patcher
  and HostMemory compilation. This is not an APK build, device execution or driver validation.
- Both jobs and the complete workflow succeeded:
  https://github.com/Splaser/emulator/actions/runs/38668008146 . The final evidence update changes
  documentation only and uses `[skip ci]`; the tested production source remains `4fc3f7e768`.
- Rechecked the seven conflict resolutions against the merge parents and the subsequent CI
  fixes. Both #375 head `3de1870c78` and #380 merge `57b1925f51` are ancestors; F2 snapshot
  `80c4f4d7f7` is not. No conflict markers remain in project sources/docs/tests, and the diff
  passes whitespace checks. Preserved DMA snapshots, CPU-access handoff, physical-shadow scope,
  shutdown lock ordering, independent translation fields and the lazy-remapping validation gate.

### Promotion and M3B continuation: 2026-10-15

The user accepted #380's author's Android device validation for this integration and explicitly
requested promotion. Personal `main` and PR #375's `codex/issue-372-fixes` were fast-forwarded
and pushed to `105779d2c4`. No additional local Android build or device run is required here.
This accepts upstream #380 evidence; it does not certify the new M3B/F2 protocol on a device.

To resume yesterday's M3B work, the previous `80c4f4d7f7` branch is being rejoined on main:
CPU CopyBlock snapshots, explicit host callback acknowledgement, negative insertion audits and
the candidate protocol remain distinct from a completed production F2 fix. The one test-fixture
merge conflict keeps both the #380 logging stub and acknowledged-callback header. CopyBlock,
preservation acknowledgement, 24 candidate cases (three repetitions) and unmap preservation
regressions passed after the merge. Production admission remains disabled pending its adapters.

### Resume on another computer

```powershell
git fetch origin
git switch --track origin/codex/pr375-pr380-integration
git pull --ff-only
gh run list --repo Splaser/emulator --branch codex/pr375-pr380-integration --limit 5
```

If that local branch already exists, use `git switch codex/pr375-pr380-integration` instead of
creating it again. The successful validation source is `4fc3f7e768`, run `38668008146`; compare
any later changes against it. Keep Linux HostMemory tests and actual Android/NCE/device validation distinct.

Remaining work, in order:

1. Remote production compilation and Linux regression gates are complete at the revision above.
   Do not repeat them for this documentation-only evidence commit.
2. Keep HostMemory high risk: lazy alias remapping stays disabled pending target validation.
   Run NCE compatibility cases on a device; Windows instruction fixtures are not signal tests.
3. The conflict-resolution diff review is complete. Synchronizing PR #375 or personal main is a
   separate next step; neither branch has been updated by this integration work.
4. Only then return to F2. Its separate snapshot is `codex/fix-cpu-gpu-write-coherency` at
   `80c4f4d7f7` (eight commits beyond #375). No F2 commits were merged here. Candidate admission
   remains test-only, and production F1/F2 remain unresolved.

Local detailed logs live under `.diagnostics/pr375-pr380/` and are ignored by Git. The tracked
tests/workflow reproduce them elsewhere; diagnostics from this Windows computer are not
required inputs for the production build. The physical-shadow runner can use installed fmt
headers via `--fmt-include`; on this computer the real headers were supplied from the existing
ignored `.diagnostics/f1-f2-validation/fmt/include` fixture dependency.
