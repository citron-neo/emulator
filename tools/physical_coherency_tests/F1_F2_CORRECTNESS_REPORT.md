# F1/F2 CPU/GPU write coherency investigation

Date: 2026-10-14. Production baseline: `3de1870c78babbdb934c875268fb49b43e5d3458`.
Branch: `codex/fix-cpu-gpu-write-coherency`. Main was clean and
`git pull --ff-only` reported already up to date before creating this branch.

**F2/F1 remain open. Follow-up prerequisite corrections are recorded below.**
The requested stop conditions apply: the existing production APIs do not provide
a mapping-use/access obligation spanning preservation, the real store and
publication. F1 additionally needs a native access epoch and signal-safe handoff.
No workaround claims to repair the full transaction or native lifetime. All
changes remain local; there is no push or merge. The lifecycle repair at the
baseline remains intact. Baseline investigation below is supplemented by the
follow-up section, which describes the subsequent production changes.

## Current production call chains

References below were checked against the baseline source, not inferred from
the older audit. `M3A5_ARCHITECTURE_AUDIT.md` is absent from the tracked directory;
`M3A_README.md`, `DESIGN.md` and the existing regression runners were reviewed.

### F2: known stores, notification precedes completion

- `src/core/memory.cpp:724`, `Impl::Write<T>`: cached pointer lookup calls
  `HandleRasterizerWrite`, returns the pointer, then executes `memcpy`. The
  split-page path retains the first pointer while looking up/notifying the
  second page, before either copy.
- `:289`, `WriteBlockImpl<false>`: `WalkBlock` acquires each host pointer before
  the cached callback; notification precedes that chunk's `memcpy`. The unsafe
  specialization bypasses notification. `ZeroBlock` (`:326`) has the same
  notification-before-`memset` ordering.
- `:771/:785`, exclusive stores: notification is inside pointer lookup and
  precedes `AtomicCompareAndSwap`. Thus a failed comparison can still dirty the
  cache. The compare itself is not a post-store notification.
- `:346`, `CopyBlock`: invokes read preservation and nested `WriteBlockImpl`
  per chunk; it has no owned whole-source snapshot. Per-chunk `memcpy` cannot
  establish overlapping physical source/destination semantics. The destination
  address is also supplied to the outer `WalkBlock`; this implementation must
  be examined explicitly before claiming any general source-copy contract.
- `:817`, `HandleRasterizerWrite`: obtains another pointer, fans physical
  backing out to DAddr aliases using `ApplyOpOnPointer`, and calls
  `GPU::OnCPUWrite` -> `RasterizerVulkan::OnCPUWrite` ->
  `RunBufferCacheCpuWrite(..., true)`. A true result enables deferred collection
  in `gpu_dirty_managers`; a false result does not mean a store completed.
- `src/video_core/buffer_cache/buffer_cache.h`, `OnCPUWrite`: staged GPU-dirty
  data can be downloaded before coarse CPU dirty tracking. `WriteMemory`
  resolves pending scatter, handles direct backing ownership, subtracts GPU
  dirty ranges and marks CPU modified. It does not execute the CPU instruction.
- `SynchronizeBuffer` -> `MemoryTrackerBase::ForEachUploadRange` consumes CPU
  dirty state and then `UploadMemory` obtains guest backing bytes. There is no
  obligation held by the CPU's earlier notification to block that consumption.
  The normal immediate upload path may borrow a device pointer; the mapped
  upload path is not a substitute for CPU-store exclusion either.

Dynarmic callback methods call `Memory::Write8/16/32/64` and exclusive wrappers
directly after access checks (`src/core/arm/dynarmic/arm_dynarmic_64.cpp`). No
common cache/mapping transaction is acquired there. A check/watchpoint is not a
mapping or GPU-access lease. Escaped fastmem/kernel accesses remain separate.

### Lock lifetimes and mapping gap

| Lock or mechanism | What it actually covers | Missing boundary |
| --- | --- | --- |
| `Memory::Impl::sys_core_guard` | Only callback work for the shared system-core slot; other CPU cores do not take it | Released when `HandleRasterizerWrite` returns, before store; GPU upload does not participate |
| `buffer_cache.mutex` | One cache helper/preparation call | `RunBufferCacheCpuWrite` releases it before returning to CPU store; drops it before waiting on a GPU callback that reacquires it |
| Device `mapping_guard` | PTE mutations and pure snapshots; multi-alias list gathering | Alias callbacks run outside it; single-alias callback has no retained guard; neither pins the actual CPU access |
| GPU `mapping_update_guard` | Serializes GPU Map/MapSparse/Unmap, preservation callbacks then PTE mutation | Controlled CPU stores do not join this mutation/use protocol |
| GPU PTE -> device PTE snapshot locks | Translation and generation validation | No execution permission after unlock; no backing/representation pin for a retained pointer |
| Fastmem page protection | Reactive protection of process virtual aliases | Does not guard stores through physical backing pointers or all other host aliases |

Concrete nesting: `sys_core_guard -> alias fan-out (device lock released) ->
cache helper -> optional GPU-thread wait (cache lock released) -> cache lock ->
optional Finish`. Cache preparation can acquire GPU PTE then device PTE locks.
GPU mapping mutation takes its update guard before callbacks/cache work and
later updates PTEs. Device Map/Unmap callbacks precede the device PTE lock.

`DeviceMemoryManager::Map/Unmap` (`device_memory_manager.inc:207/:272`) changes
PTEs and reverse aliases under `mapping_guard`; preservation callbacks are
outside it, and stronger direct-view callbacks are conditional on zero-copy.
`Memory::Impl::MapPages` (`memory.cpp:572`) changes process translations without
an access reservation joined by the Memory write wrappers. A retained pointer
may remain allocated yet no longer be the intended mapping; allocation lifetime
alone does not make a stale destination valid. No use/drain handshake for these
stores was found in production. `MappingAdmission` and `ControlledCPU` exist in
`tools/physical_coherency_tests/m3a`, not as production participants.

### F1: page invalidation before native access

`arm_nce.s::GuestAccessFaultSignalHandler` checks guest TLS, restores host TLS,
calls `ArmNce::HandleGuestAccessFault`, restores guest TLS on success and returns
from the signal handler. This is still signal-context execution, not a
delegation onto the normal host scheduling loop.

`arm_nce.cpp:310` aligns `si_addr` down to one guest page and calls
`Memory::InvalidateNCE(page, CITRON_PAGESIZE)`. It does not classify read versus
write here and explicitly has a split-page TODO. `memory.cpp:1078` finds cached
backing -> `InvalidateGPUMemory` -> physical/DAddr alias fan-out ->
`GPU::InvalidateRegion` -> `ThreadManager::InvalidateRegion` ->
`RasterizerVulkan::OnCacheInvalidation` (`vk_rasterizer.cpp:1093`) ->
`RunBufferCacheCpuWrite` with default `check_gpu_writes=false` -> `WriteMemory`.
For ordinary staged dirty data without pending scatter, no download is required
by this helper before GPU authority is subtracted and CPU dirty is published.

`RasterizerMarkRegionCached` (`memory.cpp:480`) selects Read/Write permissions
from reactive flushing and cached state. Uncached pages become readable and
writable; cache tracking/upload can protect them again. There is no explicit
native-access epoch proving all resumed instructions finished before that
reprotection or a GPU producer/consumer. `HostMemory::Protect` targets virtual
aliases. It does not revoke `GetHeapVirtualPointer` or other physical aliases.
The registered access handler uses `SA_SIGINFO | SA_ONSTACK | SA_RESTART`; masking
signals on this thread does not exclude other guest cores.

## Deterministic defect witnesses

Test commit: `9f1b78c3a6caf8a8a46cecce958a9ac360fcd4d2`.
Only `run_write_coherency_witnesses.py` is added by that commit.

```powershell
python tools/physical_coherency_tests/run_write_coherency_witnesses.py --cxx g++ --repeat 10
python tools/physical_coherency_tests/run_write_coherency_witnesses.py --cxx g++ --case f2 --repeat 1 --require-fixed
```

The first command asserts that the defects still reproduce; success is **not**
a correctness pass. The second deliberately fails (exit 1) with current code.
All five cases were also run separately against the generated executable in
`require-fixed` mode, each returning 1. There is no passing repaired-code result.

| Witness | Required result | Current observed result |
| --- | --- | --- |
| F2, two host threads with explicit latches | After completed scalar store, later GPU cache sees 77 | Backing 77, cache 10 |
| F1 partial native-store simulation | 99,77,99 | 10,77,10; zero downloads |
| F1 read-fault simulation, no store | 99,99,99 | 10,10,10; zero downloads |
| F1 two stores after one simulated fault | 99,77,88 | 10,77,88; zero downloads |
| Failed CAS | No completed-write publication | Failed compare leaves backing 10 but CPU dirty is set |

F2 executes the extracted production scalar `Write<u8>`, pauses its pointer
adapter after pre-notification, performs production cache synchronization on a
worker, then permits the actual `memcpy`. A later separate synchronization still
sees 10. The rendezvous establishes ordering; no sleep guesses are used.

The runner regenerates the existing unmap fixture first. It reuses extracted
production `FindBuffer`, `OnCPUWrite`, `WriteMemory`, `SynchronizeBuffer`, upload
dispatch, download/scatter helpers and `RunBufferCacheCpuWrite`. It additionally
extracts `Memory::InvalidateNCE`, `RasterizerVulkan::OnCacheInvalidation`, scalar
Write and WriteExclusive. Only the class qualifier of InvalidateNCE is adapted.
Extraction hashes and generated C++ are saved under
`.diagnostics/write-coherency-witnesses` on each invocation.

**Fixture limits:** `GetPointerImpl`, physical alias fan-out, page table/tracker,
buffer allocation, copy execution and GPU dispatch are fixture adapters. A
four-byte device page stands for the invalidated page. Immediate upload copies
fixture RAM into byte storage. The CAS shim exercises only a failed comparison,
not target atomic semantics. No real signal handler, mprotect, GMMU mutation,
Vulkan submission or multi-core native guest executes. Tests establish the
production-function ordering/data-loss witnesses under those explicit adapters;
they do not demonstrate signal safety or a complete production mapping race.

## F2 blocking proof and required next mechanism

A correct controlled transaction must acquire stable source/destination and
alias scope before preservation, exclude conflicting GPU/CPU access, preserve
the surviving GPU complement, execute the actual store, publish exact completed
extents, and only then release that scope. Mapping mutation must announce intent
and drain accepted users before preservation/PTE changes. Physical allocation
and cache representation lifetimes must remain valid throughout use.

None of the existing locks spans that interval and all relevant mutators.
Adding post-notification alone leaves GPU upload free to observe a partial store
or a concurrent producer's bytes. Retaining only a cache mutex does not prevent
process/device/GPU remapping or stabilize alias enumeration. Retaining a PTE
mutex through preservation would introduce PTE-lock -> GPU wait while the GPU
callback can require PTE queries. Retaining the cache mutex through that wait
would block its reacquisition. Extending sys_core_guard does not enlist other
cores, mapping mutators or GPU consumers. These are missing participants, not
merely the wrong order of two callbacks.

A safe next implementation therefore needs a production mapping-use/access
boundary joined by controlled stores, cache consumers/producers, relevant
mapping mutators and shutdown. It must preserve without recursively acquiring
the obligation that its waiting CPU already owns. M3A's isolated prototype does
not establish these production adapters. Integrating them here would cross the
task's original boundary against general M3B integration; the F2 mapping-use
stop condition remains unresolved. The follow-up adds prerequisite corrections,
but no controlled-store transaction is committed.

Block APIs also need exact completed extents or an explicit unknown/failure
state. `WalkBlock` can write valid chunks, encounter a hole, continue, and return
false with no completed-range result. Scalar writes are void/noexcept, and
exclusive writes notify even on failed compare. Existing APIs cannot promise
transactional cancellation/commit by changing notification placement. Metadata,
copy snapshots and failure bookkeeping must be prepared before irreversible
stores; allocation/exception after a partial store must not become success.

`RunBufferCacheCpuWrite`'s shared boolean result is lifetime-safe after the recent
repair, but a default false can also follow an unexecuted shutdown callback.
`RunOnGpuThread` returns void and CPU queue acknowledgement is not a preservation
success/visibility result. An accepted controlled write needs an explicit
preservation outcome before storing, plus drain/cancel rules. This report does
not reinterpret the lifecycle fix as such an acknowledgement or alter it.

## F1 blocking proof and required next mechanism

Preservation must precede resuming a native access, but preservation can lock,
allocate, queue work and wait for Vulkan completion. The present signal stack
and host TLS restoration do not make those operations async-signal-safe.
Existing complex handler calls are not evidence that more are safe. Changing
`check_gpu_writes` to true is therefore not a proven correction.

Even a safe one-time readback would not control subsequent stores once a page
is writable. A complete fix needs a signal-safe capture/handoff to normal host
execution, a physical-page/alias native access epoch, other-core quiescence or
exclusion, preserved data before retry, and an explicit close/reprotect boundary
joined by GPU use. Read/write/atomic/split-page classification and alternate
aliases must be accounted for. A newer CPU producer must not be overwritten by
an old page readback. None of those obligations is discharged by returning true
from InvalidateNCE. The mandatory F1 stop condition is reached; default NCE
behavior is unchanged and remains defective in these witnesses.

## Coverage and exclusions

| Requested case | Evidence/status in this work |
| --- | --- |
| CPU write racing upload | Deterministic F2 two-thread witness, defect retained |
| Partial page; read-only fault; repeated stores after fault | Three F1 byte witnesses, simulated only, defects retained |
| Failed exclusive/CAS | Extracted pre-notification ordering and failed-compare witness; no target atomic proof |
| GPU dirty complement | Existing unmap suite mixed-page CPU77/GPU99 preservation case passes when its controlled ordering is respected; no racing-store guarantee |
| Split-page stores | Current scalar two-pointer/two-copy path inspected; no protected span; unsupported transaction, no new passing claim |
| Copy overlap / partial block failure | Existing per-chunk source/boolean failure interfaces inspected above; no transactional contract; unsupported by a repair here |
| Concurrent CPU writers | No common physical range exclusion across cores; unsupported |
| Device remap / GPU VA remap during preparation | Pure snapshot tests pass, but no store/use lifetime handshake; unsupported execution interleaving |
| Preservation failure / shutdown cancellation | Existing unmap failure and lifecycle cancellation tests pass in their domains; no controlled-write success acknowledgement; unsupported transaction |
| NCE reprotection / cross-core native access | Permission code inspected; no native epoch or actual device execution; unsupported |
| Unknown physical aliases / different DAddr replicas | Resolver enumerates translations, not authority/access rights; no new coherency guarantee |
| Kernel/raw/unsafe pointers | `GetPointer`, `GetSpan`, unsafe writes and kernel heap copies are uncontrolled; explicitly excluded |

In particular, kernel heap-to-heap copies use direct pointers and memcpy
(`k_page_table_base.cpp:4234/:4368`); kernel page locks do not constitute GPU
access exclusion. Buffer/texture and distinct DAddr replicas also do not share
a newly implemented authority domain in this branch.

## Validation

Windows x86-64 Strawberry MinGW GCC 13.2.0. No full emulator or Android build.

| Suite | Result |
| --- | --- |
| GPU lifecycle: run_tests, run_thread_tests, run_renderer_tests, run_fence_tests | All pass, defaults (10 repetitions) |
| M3A protocol | Pass, 10 deterministic concurrency repetitions |
| M2 model | Pass deterministic suite and seeds 372-379, 400 commands each |
| Phase 3.5A physical resolver `run_tests.py` | Pass |
| M0 DMA snapshot | Pass |
| M1/M1.1 physical shadow | Pass after supplying real pinned fmt headers |
| Nine gather runners: main, binding_alias, cpu_scatter, gpu_stop, gpu_write, index_range, mapping_snapshot, unmap_alias, vulkan_import | All pass |
| New F1/F2 witnesses | Five defects reproduce on every one of 10 repetitions; all five correctness gates fail as expected |
| ASan/UBSan and TSan | Unavailable: minimal link probes fail with missing -lasan/-lubsan and -ltsan; not passes |
| Linux GCC | Unavailable: WSL not installed and no docker/podman runtime; Linux-only cold HostMemory resolver test not run |
| Android ARM64 NCE / actual driver | Not run; no device correctness claim |

The first shadow attempt failed compiling `fmt/format.h`. To complete validation,
the exact repository pin `e8244777ee1c32df8233c215ac9ff626b2dd2c38` was fetched
from fmtlib/fmt into ignored `.diagnostics/f1-f2-validation/fmt`; rerunning with
`--fmt-include .diagnostics/f1-f2-validation/fmt/include` passed. No dependency
pin, submodule or build configuration was changed. Vulkan importer validation
used the checkout's existing Vulkan headers through process-local include flags.
Logs/results are local ignored artifacts in `.diagnostics/f1-f2-validation`.

Actual Android verification eventually needs read/write/atomic faults, two stores
after one fault, reprotection during upload, crossing pages, multiple NCE cores,
physical aliases, newer CPU producers, remapping and shutdown during preserved
access. Host extracted-function tests cannot replace this execution evidence.

## Changes, performance and disposition

The original investigation added only witnesses and this report. Follow-up
production changes and their costs are listed below. No overhead
estimate is claimed for the missing production mechanism: physical conflict
tracking, mapping use/drain, native quiescence and preservation waits will need
measurement if implemented under a separately authorized scope.

The report is committed separately after test commit `9f1b78c3a6`; its own SHA
is available from `git log -1 -- tools/physical_coherency_tests/F1_F2_CORRECTNESS_REPORT.md`
(a commit cannot embed its own hash). Main/issue branch remain unchanged. F2 and
F1 remain open, with reproducible failure gates and the missing integration
requirements recorded above.

## Follow-up: CopyBlock and preservation acknowledgement

The user's follow-up explicitly required separate source-snapshot/partial-copy
proof and a real preservation outcome, without requiring a Vulkan completion
registry. These prerequisites are now corrected independently on the same local
branch. This does not close the original F2 store/publication window or F1.

### CPU CopyBlock correction

Commit `518c99aaac` (plus the subsequent wording clarification in this follow-up)
changes `src/core/memory.cpp`, `src/core/memory.h`, the `ReadIoMemoryImpl` caller
in `src/core/hle/kernel/k_page_table_base.cpp`, and adds
`run_copy_block_tests.py`.

The old outer WalkBlock used **dest_addr**, passing destination bytes as the
source of nested writes. Even distinct non-overlapping source bytes were not
copied correctly. This is independently reproduced by extracting the old
production functions with `--revision 52ae351066`; the assertions fail (exit 1).

The corrected CopyBlock rejects out-of-range addresses and known destination
holes, allocates the entire owned source before destination copy stores, reads
the actual source with existing preservation, rejects an incomplete source,
then writes from the snapshot in logical destination order. Snapshot allocation
failure returns false. `ReadIoMemoryImpl` now checks that false result instead
of returning ResultSuccess unconditionally. Its write counterpart already
checked the result.

`run_copy_block_tests.py` extracts current WalkBlock, ReadBlockImpl,
WriteBlockImpl, ZeroBlock and CopyBlock. Its fixture supplies page translations,
stable byte backing and preservation callbacks. It verifies both physical
overlap directions through distinct VAs, cached/ordinary pages, unaligned ranges
crossing pages, repeated destination aliases, normal distinct-source copies,
source and destination holes, snapshot OOM via operator new failure, zero length
and bounds. A preservation exception on the second destination chunk propagates
after the first chunk changed; the test checks those exact fixture bytes.

**Explicit failure contract:** this copy is not atomic. Mapping preflight does
not pin mappings; callers must keep mappings stable. A later exception can
leave a completed prefix. A concurrent remap/hole during WriteBlock can leave
noncontiguous completed chunks because WalkBlock continues past holes. The
boolean reports failure, not completed byte extents or rollback. Therefore
**CopyBlock remains excluded from an F2 transaction** until that integration
supplies source/destination lifetime protection and an exact partial-write or
unknown-state contract. Snapshot correctness under stable mappings alone does
not establish concurrent CPU/GPU access correctness. Existing readback during
source preservation is also distinct from destination copy stores.

Performance: one source-sized allocation/copy, plus a destination preflight
walk. This is intentionally limited to the CPU CopyBlock path; scalar/block
writes, GPU MemoryManager::CopyBlock and DMA snapshot policy are not replaced.
No performance measurement or negligible-overhead claim is made.

### Preservation callback boundary

Commit: `80eb1e8f40f91bbcf935edef3809cba39305596c`.

`src/video_core/acknowledged_callback.h` adds host callback outcomes
`Completed`, `Cancelled`, and `Failed`. A shared request has a CAS-protected
Pending -> Running or Pending -> Cancelled transition. After a blocking dispatch
returns without starting the action, cancellation prevents even a retained late
closure from executing the action. After Running, an early stop-token wakeup
must still wait on the callback's actual promise completion. Callback exceptions
are transported to the waiting request and rethrown on the executing thread,
preserving existing worker failure handling. Inline exceptions still propagate.
No cache/PTE/queue mutex is retained by the waiting helper.

`RasterizerVulkan::TryRunBufferCacheCpuWrite` exposes `optional<bool>`:
nullopt means callback preservation was not confirmed; a contained false is a
successful callback with no deferred-dirty requirement. This separates the two
meanings that the old boolean conflated. The contained bool keeps its existing
dirty-tracking meaning. Completed establishes only completion of the existing
cache action; any required device visibility still comes from that action's
existing readback/Finish, and does not reconcile unknown physical replicas.

Legacy preservation callers (`check_gpu_writes` or `unmap`) with NCE disabled use the checked path,
with an explicit legacy `.value_or(false)` adapter. **The Memory store wrappers
still consume the old boolean and are not repaired transactions.** Future F2
integration must consume the optional result and reject failure before stores;
this adapter must not be reused as proof of preservation success.

Default cache invalidation and all NCE-mode calls retain their original dispatch
body. Further review found that NCE alignment-fault instruction emulation calls
Memory::WriteBlock, so guarding only default invalidation was insufficient.
The adapter explicitly excludes NCE mode using the existing IsNceEnabled flag.
Neither the access fault nor alignment fault acquires the new promise/wait machinery. The
acknowledged path is not signal-safe and is not inserted as an F1 workaround.
ThreadManager shutdown/queue/fence behavior itself is unchanged.

`run_preservation_callback_tests.py` compiles the production helper, adding only
a copied-header signal immediately before its future wait. It covers inline
completion, rejection, retained/skipped late closure, owned payload lifetime,
active shutdown wait, callback failure, inline exception propagation and 200
start/cancel races per run. Ten repetitions pass. These are host callback tests.

The actual ThreadManager runner also uses the helper with the full production
class/RunThread/queues. Ten repetitions pass for pre-start/post-stop rejection,
normal completion, Stop waking an active request before its callback finishes,
callback exception and the failed worker acknowledging a skipped command.
The existing cache extraction runners now extract both helper methods; they
also distinguish successful false from cancellation/nullopt.

Performance: off-thread preservation adds a shared request/promise/future and
waits for actual active callback completion on shutdown. It adds no Finish or
Vulkan completion registry. Clean writes acquire no new allocation or wait;
the legacy adapter adds a backend-flag/dispatch branch. NCE mode and default
invalidation retain the original path. Costs are not benchmarked.

### Follow-up Windows validation and remaining scope

Windows MinGW GCC 13.2.0: the four lifecycle runners, M3A, M2, resolver, M0 DMA,
M1/M1.1 shadow with the pinned real fmt headers, nine gather runners, new CPU
CopyBlock tests and new acknowledged callback tests pass. Logs are in
`.diagnostics/f1-f2-followup`. The final native-dispatch/backend separation was followed
by rerunning the affected cache extraction runners and defect witnesses.

The five F1/F2 defect witnesses still reproduce; they are **not fixed-code
passes**. No full emulator link, Linux, sanitizer or Android/device validation
is claimed in this follow-up. Windows fixtures cannot discharge F1 signal
safety, native cross-core exclusion or reprotection obligations.

The remaining F2 work is a production mapping/access reservation joined by CPU
stores, GPU consumers/producers and mapping mutations, with post-store exact
publication and explicit failure. CopyBlock must remain separately scoped as
above. Remaining F1 work is signal-safe handoff plus native access epochs across
all supported cores/aliases and close/reprotect before GPU reuse. These were
not replaced with a global hot-path mutex or speculative signal-handler waits.
