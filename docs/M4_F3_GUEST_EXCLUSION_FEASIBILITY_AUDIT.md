# M4-F3 guest-backed exclusion feasibility audit

2026-10-19 continuation: see the
[CPU preparation lifetime audit](M4_F3_CPU_PREPARATION_LIFETIME.md) for the current
owner matrix and minimal repair of an empty readiness visit in the controlled
scalar request API. Exact source `9d50526fd5` passed Linux-only
[39251597638](https://github.com/Splaser/emulator/actions/runs/39251597638), with
independently verified artifact hashes, three preparation mutation controls/repeats
and 479 assertions in each native provenance process; Android was skipped.
It does not close the outer PTE/service/backend/GPU gates or
authorize ordinary guest admission; F2/F3 remain Open.

Audited 2026-10-19 at `0ea2b9a5c85197753861d34341e755fbf386e055`
(provider source `3d271fc313322403a451cf6e4fd2409504530ff9`). This is a static
source/contract audit, not new runtime acceptance or an implemented guest provider.

## Decision

**Existing arbitrary guest backing cannot currently acquire a truthful exclusion
capability. Keep admission Unknown.** The accepted private allocation provider
cannot be relabelled or extended by supplying a guest epoch or a list of VMAs.

The selected first guest-backed mechanism is **acknowledged whole-guest quiescence,
host/device producer admission closure and drainage, then an all-alias mapping
freeze held through checked GPU completion**. Page protections are supplementary
enforcement, applied to every owned CPU alias in normal host context. They are not
the permission certificate. NCE/JIT remain stopped for the entire operation.

This deliberately chooses a coarse, slow, explicit operation rather than live
fault-based writer parking. It is a proposed implementable mechanism with missing
integration prerequisites below, not an assertion that today's Pause API suffices.
No ordinary BufferCache source selection, authority, shader expansion or cross-queue
admission is enabled. No additional Copy/Planner expression is needed.

## Actual ingress and present gaps

Paths below are relative to the repository; line numbers refer to the audited
commit. These are confirmed ingress families and counterexamples to closure,
not a claim that every producer has already been enumerated.

| Family | Actual source entry | Consequence for exclusion |
| --- | --- | --- |
| Scalar CPU writes | `src/core/memory.cpp:784` TryBoundCpuWrite / `:793` Write | Only an explicit ScopedCpuWrite routes through the controlled transaction. Ordinary stores resolve pointers and memcpy; cached notification is not admission held through the store. |
| CPU bulk / atomics | `src/core/memory.cpp:315` WriteBlockImpl, `:352` ZeroBlock, `:372` CopyBlock, `:841` WriteExclusive, `:855` WriteExclusive128 | Separate entry paths, cross-page and possible partial effects. They do not automatically participate in PhysicalPageGates. Unsafe variants are also producers. |
| NCE native store | `src/core/arm/nce/arm_nce.cpp:401` RunThread; `src/core/arm/nce/arm_nce.s:179` GuestAccessFaultSignalHandler | Native guest instructions run directly on guest VA mappings; ordinary Memory::Write and its gate counters are bypassed. Access fault is a reactive recovery path, not continuous writer exclusion. |
| NCE fault fallback / alignment | `src/core/arm/nce/arm_nce.cpp:256`, `:346`, `:365` | Fallback can emulate a store through Core::Memory or skip an undecodable instruction. Alignment emulation is another write ingress. Neither is an exclusion retry protocol. |
| Dynarmic fast paths | `src/core/arm/dynarmic/arm_dynarmic_64.cpp:263`, `arm_dynarmic_32.cpp:199` | Fastmem and exclusive configuration can bypass Memory callbacks; disabling NCE alone does not close CPU writes. Both JIT and NCE must be stopped in the first domain. |
| Borrowed CPU pointers | `src/core/memory.cpp:688` GetPointer / GetPointerSilent, `src/core/device_memory.h:53` GetPointer, `src/common/host_memory.h:174` BackingBasePointer | Mutable pointers carry no scoped write-use lease. A pointer can outlive resolution, cache notification or mapping snapshots. Pointer acquisition count is not active writer count. |
| Device/GPU address helpers | `src/core/device_memory_manager.h:279` GetPointerFromRaw; `.inc:453` Write, `:531` WriteBlock, `:589` WriteBlockUnsafe; `src/video_core/memory_manager.cpp:900` GetSpan | Direct memcpy and mutable spans reach canonical physical backing. These must be included even when guest CPU execution is paused. |
| Host service examples | `src/audio_core/device/device_session.cpp:112` WriteBlockUnsafe; `src/video_core/host1x/vic.cpp:171` and `:241` GMMU WriteBlock | Audio/VIC are concrete non-NCE producer families. Guest scheduling suspension alone is not their stop/join certificate. Remaining service/debugger/plugin writers require enumeration or exclusion. |
| Physical clearing | `src/common/host_memory.cpp:1430` ClearBackingRegion | Direct memset of backing_base; protecting a guest VA does not intercept this alias. |
| Mapping / permission changes | `src/core/memory.cpp:93` MapMemoryRegion / `:116` UnmapRegion / `:130` ProtectRegion; `src/common/host_memory.cpp:1346`, `:1382`, `:1404`; `src/core/device_memory_manager.inc:218`, `:283` | CPU PTE, native VMA, DAddr and GPU VA changes have separate publication boundaries. An epoch detects staleness; it does not forbid remap or reopening a writable mapping. |
| GPU transfer / internal copies | `src/video_core/buffer_cache/buffer_cache.h:1080` DMACopyImpl, `:1342` DMAClear, `:4075` ScatterGatherEntry; upload, merge, download and readback copies elsewhere in the same file | Internal producer/copy/writeback work must be drained and new entry closed. Restricting only the public controlled Copy API is insufficient. |
| GPU shader / image producers | `src/video_core/renderer_vulkan/vk_rasterizer.cpp:867` Draw, `:1142` DispatchCompute; storage intent `buffer_cache.h:2751` MarkWrittenBuffer | Bind revision is intent, observation is not exclusion, and shader footprint may be Unknown. Texture/image aliases and host writeback must be excluded too, not only staged VkBuffers. |

## Why the present NCE fault chain cannot issue a guard

Current access-fault chain:

```text
GuestAccessFaultSignalHandler (assembly, still a signal handler)
  -> ArmNce::HandleGuestAccessFault
  -> Memory::InvalidateNCE
     -> GetPointerImpl: mapped pointer may succeed without rasterizer invalidation
     -> InvalidateGPUMemory (if rasterizer-cached)
        -> DeviceMemoryManager::ApplyOpOnPointer
        -> GPU::InvalidateRegion -> ThreadManager::InvalidateRegion
        -> RasterizerVulkan::OnCacheInvalidation
           -> texture_cache.mutex
           -> RunBufferCacheCpuWrite -> buffer_cache.mutex / possible GPU handoff
```

Evidence: `memory.cpp:930`, `:1314`; `gpu_thread.cpp:238` calls the Rasterizer
directly; `vk_rasterizer.cpp:1434`, `:1570` contain the lock-bearing work.
Changing TLS back to host TLS in assembly does not leave signal context. A
successful InvalidateNCE returns true for a mapped pointer; the access-fault path
then retries the original instruction. A separately read-protected ordinary page
can therefore re-fault without a guard-aware resolution. The fallback interpreter
must not be used to perform a conflicting store, nor may it skip the instruction
as though the exclusion request completed successfully.

The existing chain is **not an async-signal-safe foundation for the new provider**.
This is a source-level risk finding, not a reproduced deadlock. The audit does not
claim to repair all legacy signal handlers. Mutex acquisition, allocation, logging,
renderer callbacks, range admission and GPU waits must stay outside any new signal
path. POSIX/Linux signal-safety rules do not grant arbitrary C++ mutex/cache work
safe reentrancy; mprotect itself is not a POSIX async-signal-safety certificate.
See [signal-safety(7)](https://man7.org/linux/man-pages/man7/signal-safety.7.html).

PhysicalPageGates can be reused **in normal context**, with narrower semantics:

- `physical_page_gate.h:44` TryEnter increments only participating writers;
  `:66` Drained proves only their count has reached zero after Closing.
- `:110` Prepare locks a map and allocates. It must never run in a signal handler.
  Range tickets/registration and StoreCached are not signal-handler admission APIs.
- `physical_cpu_page.h:34` TryStore publishes after the actual store and before
  writer exit. Registration closes fast entry. `physical_registration_bundle.h:195`
  StoreCached uses a granted range ticket and an observation guard. These are
  useful local drainage mechanisms, not evidence that native/raw writers enrolled.
- Node CAS counters and atomics are not by themselves a proven signal ABI. A
  future signal descriptor would require target lock-free guarantees, bounded
  operations, stable preallocated storage and reclamation after all handler users
  quiesce. No shared_ptr construction/destruction or registry lookup in the handler.

A later live-NCE design would need a separate, early classification path for both
access and alignment faults: record into a preallocated per-core slot, preserve the
instruction PC/state, and use a verified assembly/ucontext exit to normal host
dispatch. Host context may then park the thread and resolve the request. Returning
true to retry while the lease is active, emulating the write, advancing PC, blocking
on a mutex/futex/GPU in the handler, or overflowing the slot must never admit the
store. Existing prefetch/interrupt context-save mechanisms are study points, not
proof that a new data-abort exit/resume is correct. Host raw-pointer faults and
Dynarmic's distinct fault machinery would still need separate treatment.
**This is not the selected first implementation.**

## Required physical coverage certificate

For one pinned `(GuestBackingEpoch, physical page interval)`, admission needs all
of the following, acquired atomically with respect to new producer/mapping entry:

1. A closed set of **all processes and owners**, CPU PTE aliases, every fastmem or
   direct-address VMA, canonical backing_base mapping, separately mapped chunk/view,
   mutable span/pin holders, and native GPU representations. DAddr aliases alone
   are not the complete CPU VMA set. ReadPhysicalAliases and mapping snapshots
   (`device_memory_manager.h:202`, `:217`) hold cold metadata locks, not writer leases.
2. A lifetime-scoped ledger or a fully stopped, audited owner set that can prove
   the ledger exhaustive. `/proc/self/maps` may independently check known VMAs but
   cannot discover outstanding pointer users, fd pwrite/truncate operations,
   external processes, dma-buf/driver bindings or queued jobs. Scan success is not
   a capability. Unknown export/owner history refuses adoption.
3. Mapping creation, unmap/remap, Protect, cache/debug permission restoration,
   backing chunk replacement and import/export publication must obey one freeze
   obligation for this interval. HostMemory's layout_mutex currently uses shared
   locks for Map/Unmap/Protect (`host_memory.cpp:796`, `:857`, `:910`) and chiefly
   excludes merging; it is not this ownership obligation. CPU MappingMutation and
   device mapping_guard likewise protect snapshots, not asynchronous use.
4. Close new writes **before** draining old ones. Counted gate writers exit with
   their publication; NCE/JIT acknowledge leaving native execution; service jobs
   complete/join; borrowed pointers are released or every owner is parked after
   its last store. A currently writable pointer has no such certificate today.
   Include outstanding kernel I/O that can write user memory, fd writes and device
   DMA/imports; non-drainable or unobservable paths refuse the whole domain.
5. Establish actual host memory ordering at acknowledged safe points, not merely
   an interrupt-sent bit. Validate ARM64 and JIT stop/resume and store visibility
   separately. A returned mprotect is not proof that every producer's multi-store
   operation ended; a thread may have completed only a prefix before fault/stop.
6. Cover full **host protection pages**, including adjacent logical subranges.
   First implementation requires 4096-byte host pages and no huge-page/partial-host-
   page ambiguity. Larger page granularity requires expanding ownership or refusal.
   Never hide alias/producer-capacity overflow by truncating an enumeration.

Linux mprotect changes protection of the specified virtual pages, not the backing
object or other aliases; GPU/DMA access is not fenced by CPU PTE protection.
[mprotect(2)](https://man7.org/linux/man-pages/man2/mprotect.2.html) and
[mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html) define those mechanisms.

## Minimal first domain and executable sequence

Initial budget proposal: one full 4096-byte physical page in a known, internally
owned ordinary memfd backing epoch; at most two guest CPU VA aliases plus the
canonical CPU view; 2-3 explicitly named staged allocations; one actual Vulkan
queue; one outstanding operation. Bounds are refusal limits, not a reason to omit
another alias. No dma-buf/direct import/UMA, shared external memory, debugger/raw
export, image producer or unacknowledged service owner. Existing backing is allowed
only if its complete owner/export history and aliases can actually be certified.

Sequence below describes obligations, not newly added API names:

1. From an ordinary non-GPU orchestration context, reserve an exclusive operation
   lifetime and close CPU/service, mapping and GPU producer admission. No signal
   handler, cache/PTE/layout lock or GPU worker may wait for the other actors.
   Preflight the dependency graph: a CPU/service job waiting for GPU work must be
   allowed to drain that already-admitted work; a parked owner must not hold a lock
   needed by the completion path. A cycle refuses admission, not a longer timeout.
2. Use existing System suspension serialization as a foundation. Importantly,
   `KernelCore::SuspendEmulation(true)` (`kernel.cpp:1285`) requests suspension
   **and waits until scheduler current threads no longer belong to the processes**;
   `System::StallApplication` (`core.cpp:301`) retains suspend_guard. This is real
   guest execution waiting, not just an asynchronous request. It does not prove
   host worker drainage, all guest-entry/host callback ordering or bounded liveness.
   Its current busy wait has no bounded timeout; a bounded attempt/ack protocol
   and lock-order validation are needed before borrowing it for this provider.
3. Complete/join all other enumerated writers and all pre-admission GPU work with
   real worker drainage and checked completion/visibility (initially a trusted
   device-idle boundary across participating queues). Queued-but-unsubmitted chunks
   are obligations too. Device loss/unknown old work refuses usable admission.
   The tested operation itself remains same-queue; no cross-queue source proof.
4. With producers stopped, freeze the complete alias/mapping set, pin the backing
   and native resources, verify identities/epochs and close/drain existing tracked
   range gates. Install protection on every CPU view in ordinary host context.
   Restore permissions later only from saved effective permission intersections,
   not unconditional ReadWrite (guest/debug/cache restrictions still apply).
   New alias/export/protect/remap requests are refused/deferred for the lease.
5. Re-run existing current-content/admission checks against this frozen state.
   Source bytes are not automatically authoritative merely because execution is
   excluded. Perform physical fan-out **before any possible write**, including
   controlled host stores, and use existing exact RootSpan/receipt/same-queue
   dependencies for the explicit transfer. Fan-out covers retained controlled
   gates only; ordinary representations must remain inaccessible or be invalidated
   separately. No latest-writer inference from this certificate.
6. Transfer ownership of the whole exclusion obligation to the existing checked
   operation before enqueue. Keep guest threads, service writers, mapping changes
   and unrelated GPU producer admission closed until actual checked completion
   and required host visibility. Keep a dedicated completion path runnable; a
   caller scope ending cannot resume owners. The private guard currently cannot
   adopt such a guest obligation; it must not be reused via a fabricated State.
7. Release only after successful completion plus epoch/native identity checks.
   Release gates/protections in a defined order, reopen admission last and restore
   the pre-existing pause state. Do not call UnstallApplication unconditionally
   while an operation still owns isolation. Physical content authority remains
   absent; metadata queries do not become admission leases.

This allows an existing guest-backed **controlled** operation while the rest of
the guest stays paused. It does not promise useful game throughput or live NCE
execution. A deadline expiry before admission cancels only after proving no GPU
effects; it never forces resumption after possible effects.

## Unknown and isolation

Before any GPU effect, a rejected attempt can release only after all stopped-owner
and protection changes have a known, safe disposition. Partial recording, possible
submission or uncertain execution transfers the affected allocation/page set into
a retained isolation obligation. All potentially written intervals lose old current
eligibility; historical receipts remain unchanged. Submission-domain Unknown
continues to reject all current claims, including otherwise untouched complements.

For guest backing, sealing an epoch or protecting one VMA is **not** isolation.
Keep all CPU/host producers and conflicting consumers stopped, all aliases frozen
and backing/native pins retained. Uncontrolled ordinary reads must not expose
uncertain guest content. A guard destructor cannot silently reopen this domain.
Remap/unmap must not free/reuse old backing or attach old completion to a new epoch.
Real trusted idle can establish that GPU access has ceased, but cannot establish
correct contents or automatically authorize guest resume. Recovery needs explicit
validated restore/resynchronization or controlled session termination. On device
loss without trustworthy cessation, retain inaccessible resources under existing
device-lifetime teardown; do not pretend timeout is completion.

If a platform or owner cannot maintain that isolation, refuse **before** admission.
Unexpected write/fault during the frozen interval is an invariant breach requiring
session stop/escalation; routing it into ordinary NCE invalidation is not recovery.

## Alternatives considered

| Mechanism | Decision |
| --- | --- |
| Check coverage then record | Reject: no continuous exclusion after Check. |
| PhysicalPageGates alone | Reject: only enrolled writes drain; no native/raw admission interception. |
| mprotect one/all known VMAs alone | Reject: unknown aliases, permission reopening, host fd writes and GPU effects remain. Even all known VMAs need owner drainage and mapping closure. |
| Resume NCE, park faults inside current handler | Reject first stage: current path invokes caches and can retry/emulate; blocking introduces lock inversion and no verified resume protocol. |
| userfaultfd WP + dedicated resolver | Possible later Linux experiment, not selected: requires real feature/memory-type/permission probing and registration of every alias, no fd/device bypass, mapping events and teardown/overflow protocol. It does not grant physical-object exclusion or GPU ordering. |
| Stop/ack producers + freeze mapping/alias entry | Selected first design: slow and restrictive, but enforcement and drainage are explicit and do not depend on a signal handler waiting on GPU. Missing joins/coverage still yield Unknown. |

Linux's [userfaultfd documentation](https://docs.kernel.org/admin-guide/mm/userfaultfd.html)
specifies feature negotiation, memory-type-dependent ioctls, access controls and
WP resolution by another thread. No deployed Android/kernel support was checked
here; userfaultfd is not a portable availability assumption.

## Next proof gates; what is not proved

Before implementing a guest lease, require deterministic negatives/mutations for:
omitted canonical/second-process aliases; newly created RW alias or cache-unprotect;
retained raw pointer store after coverage check; a writer paused between pointer
resolution and memcpy; audio/VIC/kernel-I/O work still running; NCE/JIT still in
native code; remap immediately after snapshot; queued old GPU commands; early caller
drop/resume; partial recording, device loss and Unknown; alias-budget overflow;
protection-restore failure; a CPU-held lock needed by GPU completion. Each must fail
before recording or preserve real isolation after possible effects. Include three
repeats of real multi-VMA stores and real Vulkan completion/readback when that
implementation exists, plus ARM64 target tests for any NCE participation.

Current blockers are **missing exhaustive owner/alias enrollment**, **host/device
producer close-and-join**, **mapping/permission admission freeze**, **bounded safe
guest quiescence integration**, and **retained guest-wide Unknown isolation**.
The audit cannot prove these conditions for arbitrary already-running guest pages.
No production code was changed or runtime tests/builds dispatched for this document.
The preceding private-provider Linux CI is historical evidence for private memory,
not evidence for any of these missing guest-backed properties. F2/F3 remain Open;
restricted production admission remains blocked.

## Subsequent bounded components

[Managed drainage](M4_F3_MANAGED_PRODUCER_DRAIN.md) now closes the four real
PhysicalCore entry gates and one controlled Memory owner; this is only a managed
subset, with Linux acceptance recorded separately. The
[HostMemory alias/mapping component](M4_F3_HOST_MAPPING_FREEZE.md) subsequently
adds actual owner-created mapping records and a cold mapping-entry freeze. Its
final design is constructor-only opt-in for fresh controlled arenas; ordinary
Core::DeviceMemory/NCE arenas stay untracked and acquire no new mapping mutex.
It cannot adopt an already-used guest arena. Its
bounded Linux acceptance passed at `a080cc5a2a` in
[run 39205308506](https://github.com/Splaser/emulator/actions/runs/39205308506).
It does not close outer CPU PTE/device/GPU changes,
escaped pointers or import/export publication. Those prerequisites above remain
open; neither component is a truthful general guest exclusion capability.

The next [controlled owner lifecycle substage](M4_F3_CONTROLLED_OWNER_LIFECYCLE.md)
adds a sealed fresh CPU-only backing cohort with actual admission/drain and retained
lifetimes. It reuses the accepted HostMappingDomain after drainage. Linux-only
[39212903431](https://github.com/Splaser/emulator/actions/runs/39212903431) passed at
`a36bae493c`, with verified artifact/source hashes, eleven owner and nineteen
mapping assertion mutants, three real owner repeats, five CTest processes and
three extra 467-assertion native provenance processes. The resolved-owner GPU
progress and separate opt-in metrics controls passed; Android was skipped.
See the owner document for exact counters and artifact digest. This accepts the
CPU-only cohort, not a general guest exclusion capability. It does not supply
outer CPU PTE, NCE/Dynarmic, raw/service
or GPU owner completeness. The documented publication/lifecycle blockers require
stopping at this owner substage, rather than asserting a general guest lease.
