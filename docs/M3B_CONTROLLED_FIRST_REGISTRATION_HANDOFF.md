# M3B - Controlled First Registration Integration

The [restricted eligibility gate](M3B_F2_RESTRICTED_ELIGIBILITY.md) remains blocked before
ordinary-game activation. The current continuation first isolates legacy buffer readbacks;
the earlier explicit scheduler acceptance below is not an ordinary-entry exclusion proof.

Current scope is **M3B-F2 CPU Scheduler Handoff**: explicit nonblocking existing-owner queue
continuations, per-Memory execution closure before process PTE finalization, distinct
Cancelled/Refused/Unknown actions, and the five concurrency/lifecycle assertion groups.
See [current progress](../tools/physical_coherency_tests/M3B_PROGRESS.md) for production-code and
fixture boundaries. Close isolates this Memory's requests, not the entire GPU queue. Final
revision `02610e1766` passed both Android and Linux in
[38864622309](https://github.com/Splaser/emulator/actions/runs/38864622309): real guest service-fiber
sleep/resume, 64 full process teardowns, actual lock contention, software-Vulkan complement
preservation, Cancelled-owner release and delayed preservation/frame-reuse safety. The test
renderer omits presentation and the CPU entrypoint is a service fiber rather than an ARM game
image. Alias-process qualification,
full producer exclusion and Android hardware acceptance remain pending. Automatic game routing
stays off; F2 remains Open. The older notes below are historical.

Current review references: [producer inventory](M3B_F2_PRODUCER_INVENTORY.md) and
[native acceptance / execution-lease audit](M3B_CPU_SCHEDULER_ACCEPTANCE.md). The inventory
distinguishes enforced guards from the exclusion policy still required before activation.

Current continuation: [38839879554](https://github.com/Splaser/emulator/actions/runs/38839879554)
passed Android and Linux at `273bfb2f5d`. CPU-owner scalar preparation now supplies an immutable
request to the actual cache/Vulkan continuation, with cancellation/acceptance ownership and
CPU completion only after store/publication/admission release. Accepted Unknown requests remain
retained. Automatic game-CPU dispatch/suspension, full producer exclusion, recovery and device
proof remain pending; this adds no blocking wait or second dispatcher. Stored is CPU-commit
evidence, not GPU refresh completion. See [current progress](../tools/physical_coherency_tests/M3B_PROGRESS.md).
**F2 remains Open.** Earlier continuation notes below are historical.

Latest continuation: native CI [38835727709](https://github.com/Splaser/emulator/actions/runs/38835727709)
passed at `17f1e65977`. Typed cache transactions now invoke actual ordinary Memory Write8/16/32/64
through a one-attempt thread-local binding, with explicit refusal and no legacy fallback. This
connects the ordinary store body on the isolated GPU-owner continuation, not automatic CPU caller
admission/dispatch. Caller suspension, complete producer exclusion, trusted recovery and device
evidence are still required; unbound F2 remains backing77/cache10. See
[current progress](../tools/physical_coherency_tests/M3B_PROGRESS.md). **F2 remains Open.**

Continuation 2026-10-16 (Asia/Singapore): the cache-owner write transaction now has a separate
checked Vulkan GPU preservation readback and holds admission through actual store/all-alias
publication. Actual upload tracker consumption excludes active/Unknown transactions. See the
[current progress](../tools/physical_coherency_tests/M3B_PROGRESS.md) for eligibility, fixture
boundaries and failure tests. Ordinary scalar caller/deferred ownership, complete producer
exclusion, trusted recovery and real-device proof remain open. **F2 remains Open.**
The end-of-day handoff below is historical; do not interpret the new explicit transaction as
ordinary Memory::Write enrollment or safe concurrent whole-game execution.

Handoff updated 2026-10-15 (Asia/Taipei). **The explicit controlled BufferCache/Vulkan path is
implemented; end-to-end acceptance and ordinary-game F2 repair are NOT complete. F2 remains open.**
Native compilation and dispatch fixtures do not replace real driver/device evidence.

## End-of-day handoff and next scope

Stop development for today; this handoff update changes documentation only. The next stage is
**M3B-F2 Production Write Transaction**. Its authoritative scope and acceptance gates are at the
top of the [integration roadmap](PR375_PR380_INTEGRATION.md#next-stage-m3b-f2-production-write-transaction).
Focus only on restricted ordinary `Memory::Write<u8/u16/u32/u64>`: single page, stable/provable
physical identity, real BufferCache admission and GPU-dirty preservation. Do not expand DMA,
Texture Cache, NCE or other peripheral work. NCE/JIT fastmem/cross-page/unknown producers remain
on their original paths without new coherency claims; do not replace all writes.

The complete transaction must prove GPU-dirty complement preservation before the store
(`[99,99,99] -> [99,77,99]`, never `[10,77,10]`), exact post-store tracker publication that cannot
be prematurely consumed by concurrent upload, and failure/partial-effect handling without dirty
loss or early admission release. Reaching `WritePrepared*` or returning `NeedSlow` is insufficient.
Retain both 77 interleavings, real tracker dirty/clean checks and the disabled-handshake 10 control.
**F2 remains Open** pending ordinary caller integration, preservation/recovery, exclusion of
uncontrolled producers and real-device evidence. Progress is descriptive, not a percentage.

Latest tested source is `f00768b0e6cd988db2b3c411e04b48c35f9c7601`:
[38741531093](https://github.com/Splaser/emulator/actions/runs/38741531093) passed both native
jobs and all configured Linux regressions. This confirms compilation/regression evidence only;
it does not establish ordinary-write safety across games. Personal `main` and
`codex/issue-372-fixes` share that source before this documentation-only update.

## Actual BufferCache and Vulkan continuation from `dc1a839308`

Native [38727690858](https://github.com/Splaser/emulator/actions/runs/38727690858) passed Android
and Linux at `dc1a839308`. The next implementation adds explicit GPU-owner Rasterizer endpoints
for first registration, polling, cached Write8/16/32/64 and refresh. Ordinary game callers are
still unenrolled; no NCE/JIT/raw-pointer/DMA/image activation is included.

The first native continuation run
[38737867745](https://github.com/Splaser/emulator/actions/runs/38737867745) at `08ea7c0a98`
failed compilation: Linux diagnosed Result enum names after X11 declarations; Android diagnosed
the new plan lambda's `size` parameter shadowing a field. The follow-up moves Memory's include
before Vulkan/X11 and renames that parameter. Compiler warnings remain enabled. This failed run
is not validation of the corrected source. The corrected source is `f00768b0e6`, validated by
successful replacement run [38741531093](https://github.com/Splaser/emulator/actions/runs/38741531093).

- Cold `PhysicalAccessPlan::BuildDeviceRange` covers the actual complete rounded cache allocation,
  bounded to 64 physical pages. It snapshots device mappings, pins real HostMemory outside locks,
  then rejects a post-pin remap. Holes, overlaps/overlap expansion, retired backing, missing CPU
  prepared views and unsupported backends refuse before registration/protection/upload effects.
- BufferCache allocates a real hidden slot and a real staged payload pin plus dedicated upload
  and readback allocations BEFORE reserving the one all-page registration bundle. Hidden slots
  have no cache page-table/LRU membership. Entered CPU writers drain before initial real tracker
  marking. Mapping qualification and tracker publication share a short nonblocking device guard;
  neither that guard nor a cache/PTE guard survives a retry or a GPU operation.
- The actual Vulkan transfer copies exclusively from immutable physical pins, records ordered
  destination upload and readback copies with existing real visibility barriers, then flushes
  with a private checked submission token. Actual queue-submit status must succeed for THAT tick.
  A checked native timeline query and checked zero-timeout host semaphore wait must reach it;
  cached KnownGpuTick, callback/Flush return and
  unchecked Finish/WaitIdle are not accepted. Checked VMA flush/invalidate and a full readback byte
  comparison must succeed. This controlled backend requires timeline support; it never treats an
  unsupported fence fallback as success.
  The zero-timeout wait supplies host synchronization before invalidation, following the
  [mapped-memory dependency chain](https://docs.vulkan.org/refpages/latest/refpages/source/vkInvalidateMappedMemoryRanges.html)
  and [nonblocking wait rule](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitSemaphores.html).
- Only after that proof does a second device-mapping guard permit actual BufferCache Register,
  Tracked/readiness publication and admission release. A queued stale CPU view or device plan
  refuses before effects; a remap after submission or slot/allocation identity replacement goes
  Unknown. A CPU-dirty controlled representation cannot be returned by ControlledReadyBuffer.
- Registration-first continues beyond NeedSlow through actual prepared cached Memory Write.
  DeviceMemoryManager walks up to 64 real physical device aliases without allocation while holding
  the mapping guard; the store's short CPU guards end BEFORE real tracker publication for every
  alias, including an alias with no current buffer. GPU-dirty aliases require preservation and
  refuse. Cached-store success must become dirty before its admission can be released. Refresh
  uploads into another hidden allocation, verifies it the same way, then replaces the old buffer.
- Submit/query/flush/invalidate/readback/publication failures retain resources and quarantine the
  range as Unknown. Native allocations are retired only after producer joins and checked successful
  device idle; failed idle is not retirement evidence. Unknown does not become successful upload
  merely because shutdown reached idle. Cancelled closing gates are fail-closed: automatic gate
  recovery and a complete trusted Unknown recovery policy remain future work.

`run_registration_pipeline_tests.py --repeat 10` reuses the real prepared CPU, HostMemory,
PageTable and tracker, and extracts the actual cache state machine, Vulkan transfer/CopyBuffer,
checked Scheduler, timeline-submit and completion-query bodies. It preserves both 77 orderings
and the earlier suite's stable disabled-handshake 10 control. Worker callback/submit return is
separated from device completion. Bypassing target completion fails with 82; omitting a
physical device alias's tracker publication fails with 83; omitting explicit host read visibility
fails with 84. Cold/queued/submitted remaps, a busy
metadata guard, dirty physical aliases, slot reuse and native API failures are checked.

**Its Vulkan dispatch/allocator/device execution and alias page-table collaborators are fixtures.**
The normal Android/Linux jobs compile the real endpoint and Vulkan bodies, but no passing
fixture result closes F2. Before general routing or F2 closure, obtain exact-source real driver/
device evidence, connect actual CPU caller/deferred-write ownership, prove GPU-dirty preservation,
overlap/general alias eligibility, cancellation/recovery and final device-owner retirement. The
unchanged original F2 witness must continue reporting backing77/cache10 until that route is fixed.

### Evening continuation from `8ca6197c67`: roadmap step 3 ordering gate

`PhysicalRegistrationBundle` reserves one bounded physical range request before closing every
deduplicated page gate. A writer on the second/last page (including a CPU physical alias) keeps
the entire registration Draining. It supplies owned pages and an external representation owner;
the controlled caller still has to derive and qualify the actual complete cache footprint.

After externally certified registration completion, a separate admitted `CachedWrite` can use
actual `Memory::WritePreparedCached8/16/32/64`. `BufferCache::CommitControlledCpuWrite` invokes the
store first and updates the real tracker for every supplied qualified device alias afterwards,
while admission remains held. Stale CPU views and GPU-dirty/pending-scatter targets refuse without
a legacy fallback. The alias list/stability and absence of required preservation are explicit
caller prerequisites, NOT automatically proved by this adapter. No ordinary caller is installed.

`run_first_registration_tests.py --repeat 10` composes actual Memory Write/WriteCached, HostMemory,
PageTable, BufferCache Register/ChangeRegister and SynchronizeBuffer/UploadMemory/
ImmediateUploadMemory with actual MemoryTrackerBase/WordManager. Its acceptance gates are:

| Interleaving | Final backing | Final cache upload | Real tracker |
| --- | --- | --- | --- |
| CPU writer enters first; full registration waits for its actual store | 77 | 77 at both device aliases | clean after upload |
| Registration closes first; NeedSlow is followed by an actual protected cached store | 77 | 77 at both device aliases | dirty after store, clean after upload |
| Ordinary legacy Write with handshake disabled | 77 | 10 at both device aliases | clean, exposing the stale-cache defect |

Removing post-store tracker publication fails with 72; closing only the first page fails with
71; omitting snapshot/store exclusion fails with 75. Snapshot exclusion is nonblocking and does
not add a range lookup, cold pin or allocation to the prepared fast store. Cumulative publication
is still not a consumable dirty batch. Device mapping/tag fanout, cache allocation and synchronous
upload completion in this suite are fixtures. This is controlled step-3 evidence, not Vulkan
driver evidence or completion of the end-to-end stage. The original F2 witness remains required
and failing; steps 1/2 footprint/owner preparation and steps 4-6 immutable upload, quarantine,
checked real completion and production caller integration remain open.

The earlier native run [38711873776](https://github.com/Splaser/emulator/actions/runs/38711873776)
is now confirmed successful for both Android and Linux at `ecf9bb31e8`. That result predates
this continuation. Current-slice Android/Linux validation must be matched to its own source SHA.

## Checkout and branch policy

Work on `codex/issue-372-fixes` in the personal fork `Splaser/emulator`. Personal `main` is
fast-forwarded to the same commits; push both atomically. The completed PR380 integration
branch was deleted locally/remotely. Do not recreate it, close/merge upstream PR375, or modify
the historical F2 branch. No local full Android build is required.

On the home machine, preserve its local work before switching/pulling:

```powershell
git status --short --branch
git diff
git fetch origin --prune
git switch codex/issue-372-fixes
git merge --ff-only origin/codex/issue-372-fixes
gh run list --repo Splaser/emulator --workflow pr375-pr380-integration.yml --branch main --limit 3
```

If that branch is absent, create it tracking `origin/codex/issue-372-fixes`. Do not reset a
divergent home checkout or discard pending work. Match native evidence to the exact production
source SHA; read all failed jobs' full logs only for the latest applicable run.

## Saved implementation and evidence

Latest prepared-write production source: `ecf9bb31e8`. Remote validation was dispatched as
[38711873776](https://github.com/Splaser/emulator/actions/runs/38711873776); its result was
not yet available at handoff. Any subsequent evidence-only commit changes documentation,
not the production source tested by that run.

- `0ed92c3efd`: real HostMemory physical-page storage, backing-canonical page writer gates,
  successful post-store byte publication, failed CAS without publication, nonblocking
  first-registration page drain. Native run 38704560296 passed Android and full Linux jobs.
- `06408b9ce5`: actual PageTable consistent tag/backing snapshots, real CPU map/debug/cached
  mutator guards, resize/retirement invalidation, cold `Memory::PrepareControlledCpuPage`.
  Native run 38708541040 passed Android and full Linux production/regressions.
- The subsequent prepared-write implementation adds explicit public
  `Memory::WritePrepared8/16/32/64` and an actual `Memory::Impl::Write(prepared, VA, T)` overload.
  Ordinary Write wrappers/body remain legacy. Cold preparation captures an active-context epoch;
  actual SetCurrentPageTable invalidates views even on switch-away/switch-back. Hot execution
  uses context/PTE try-locks, checks immutable view identity/epochs/page bounds/tag, then page
  writer entry, actual store, exact publication and writer exit. Refusals never fall back.
- Windows actual CPU mapping/Write tests pass alias gates, 48-bit normalization, remap/tag
  invalidation, stale active context, Busy under mutator, cross-page refusal, independent
  writer release versus registration drain, exact bytes and 2,000 writes with zero hot heap
  allocations or range enqueue. Negative controls 51/54 remove cold/hot validation and fail.
  System/device services, registration representation owner and driver services remain fixtures.
- Original F2 remains `expected=77 observed=10 backing=77`; `require-fixed` exits 1. Keep this
  failure, original stale-byte assertions, queue-cycle witnesses and unsafe-insertion controls.

**The earlier handoff had no controlled BufferCache first-registration path. The continuation
above installs the explicit path, but still has no normal-game enrollment, GPU-dirty cached-store
preservation adapter, DMA admission or new Vulkan device evidence.** The current
prepared Write result is explicit: Stored, NeedSlow, Stale, Busy, Retired or Invalid. Callers
must not drop a refused write or bypass admission with legacy memcpy. No per-store cold pin,
shared-owner creation, physical registry lookup or range ticket is allowed.

## Next controlled end-to-end contract

1. Prepare the complete stable CPU VA views and immutable physical registration plan cold.
   Include the actual coarse BufferCache allocation, cache/protection and upload coverage,
   not just the requested byte or starting page. Deduplicate physical aliases by backing
   epoch/page. Reject holes, mixed/retired identities and unsupported layouts before effects.
2. On the actual BufferCache side, prepare a bounded all-page registration request and retain
   real representation/storage owners. Reserve ONE atomic physical range bundle before
   Closing every affected fast-entry gate. Never hold partial page admission while parked.
   Unknown/capacity/failure is not permission to bypass. No cache/PTE locks across suspension.
3. While an independently released prepared CPU writer is paused before its actual store,
   registration must return Draining/Park without CPU cached marking or backing upload.
   The writer stores 77, publishes exact bytes and exits. Admission can then grant the whole
   footprint. A queued view invalidated by remap or process switch must refuse/reprepare
   before acceptance; pinned storage alone is not current-address-space CPU permission.
4. Only after qualification may the controlled path call the actual cache registration and
   initial CPU cached/protection marking and upload. Upload from retained immutable physical
   targets; do not retranslate a queued old plan through live DAddr/VA. Keep newly allocated
   cache content unavailable to ordinary readers until registration readiness is certified.
5. Record/submit actual transfer work and retain its exact completion identity. Callback return,
   slot registration, `Flush` return, `PublishTracked` and unchecked `Finish`/WaitIdle are not
   completion or visibility proofs. Use checked transfer completion and required Vulkan
   visibility barriers; poll without blocking/recursively pumping the sole GPU worker.
6. After trustworthy completion/data visibility and a controlled read of 77, publish Tracked
   and registration-ready state, then release admission. Partial/uncertain upload/submit or
   completion failure goes Unknown with owners retained and dependent access isolated.
   Certified no-effect cancellation must keep failed/uninitialized cache content inaccessible.

The prior generic PublishTracked helper permits early closure tracking; that is not the
controlled stage's success sequence or permission to treat content as valid. Published byte
bits are cumulative observations, not a clearable dirty batch. Do not consume/clear them without
a separate sequence/incremental acknowledgement proof.

## Real source edges to integrate

- `src/core/memory.cpp`: ordinary and explicit prepared Write, cold preparation,
  SetCurrentPageTable, HandleRasterizerWrite, RasterizerMarkRegionCached and MapPages.
- `src/core/controlled_cpu_page.h`: immutable prepared view/context qualification and bounded
  actual store; cached/debug targets currently return NeedSlow without callbacks.
- `src/common/physical_cpu_page.h`, `physical_page_gate.h`, `physical_range_admission.h`:
  current single-page registration boundary and backing-domain request ownership. A complete
  coarse-range/all-page registration bundle is still required.
- `src/video_core/buffer_cache/buffer_cache.h`: CreateBuffer aligns to CACHING_PAGESIZE and
  can expand through ResolveOverlaps. Register/ChangeRegister installs cache slots; it does
  not alone prove upload completion. SynchronizeBuffer calls tracker ForEachUploadRange then
  UploadMemory. Existing upload paths reread device mappings and cannot execute an immutable
  accepted plan unchanged.
- `memory_tracker_base.h` / `word_manager.h`: ForEachUploadRange can clear CPU-modified state
  and update tracked pages. WordManager's NotifyRasterizer path calls UpdatePagesCachedCount.
  Guard its full rounded coverage before its first marking, not after RasterizerMarkRegionCached.
- `src/core/device_memory_manager.inc`: UpdatePagesCachedCount fans out CPU marking through
  registered process mappings. These aliases and coarse coverage must join the same handshake.
- `vk_buffer_cache.*`, `vk_scheduler.*`, `vk_master_semaphore.*`: real upload/staging,
  submit tick, completion/error and visibility evidence. Refresh/GetCounter errors and submit
  failure must not become success. The existing renderer shutdown seal does not prove final-pin
  drainage before Device/VMA destruction or trusted Unknown recovery.

## Required positive and negative gates

Use the real prepared Memory Write and actual BufferCache registration/upload bodies. Keep
fixture scope visible; fixture GPU bytes are not driver/device execution evidence.

- Protected original race: backing starts 10; writer independently stores 77 while registration
  is attempted; cache/GPU observation must be 77. Old-code/order-bypass control must still show
  backing77/cache10, not merely a missing callback or counter mismatch.
- Coarse multi-page registration: pause a writer on the SECOND/LAST covered page, plus a
  physical alias. Start-page-only gating must fail explicitly.
- Pre-mark/pre-upload ordering: spies reject any initial CPU cached marking/upload before the
  complete gate bundle is Granted and all entered writers have published.
- Queued stale VA/context and device remap: refuse before effects, retain old pinned storage,
  and never redirect an accepted transfer to a new physical target.
- Callback return before delayed transfer completion: ticket remains held, dependent writes
  refuse, and uninitialized content remains inaccessible. Early-Complete control must fail.
- Partial/upload/submit/completion failure, Unknown, shutdown and slot replacement while
  pending/submitted: no success publication, no owner release before trusted retirement.

Do not enable DMA admission, NCE F1, direct JIT/raw pointers, F3, images, or whole-game routing.
Do not use a second GPU dispatcher, recursive pump, cache-local blocking lock, whole-owner
lease, or effected-command replay to make the controlled scenario pass.

## Reproduction and remote validation

Windows pin-related scripts use existing Boost headers:

```powershell
python tools/physical_coherency_tests/run_gpu_plan_tests.py --boost-include .diagnostics/lazy-backing-tests/deps/usr/include
python tools/physical_coherency_tests/run_representation_pin_tests.py
python tools/physical_coherency_tests/run_cache_representation_tests.py --boost-include .diagnostics/lazy-backing-tests/deps/usr/include
python tools/physical_coherency_tests/run_physical_admission_tests.py --boost-include .diagnostics/lazy-backing-tests/deps/usr/include
python tools/physical_coherency_tests/run_page_writer_tests.py --boost-include .diagnostics/lazy-backing-tests/deps/usr/include
python tools/physical_coherency_tests/run_cpu_mapping_tests.py --boost-include .diagnostics/lazy-backing-tests/deps/usr/include
python tools/physical_coherency_tests/run_write_coherency_witnesses.py --cxx g++ --case f2 --repeat 1 --require-fixed
```

The last command MUST still fail until the real production witness is repaired. Home-machine
toolchain/dependencies may need setup; generated .diagnostics files are not transferred by Git.
Run generators in the order above; downstream runners reject stale source hashes. Linux-only
lazy backing/FD tests run in the remote workflow, not Windows. After meaningful changes, update
M3B_PROGRESS, commit English on PR375, fast-forward main, push both atomically, then dispatch:

```powershell
gh workflow run pr375-pr380-integration.yml --repo Splaser/emulator --ref main
```

This handoff separates explicit pipeline/dispatch evidence from real-device acceptance and
ordinary-game F2 repair. Do not mark the stage or F2 complete based on green CI alone.
