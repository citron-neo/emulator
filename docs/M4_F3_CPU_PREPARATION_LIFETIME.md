# M4-F3 controlled CPU preparation lifetime boundary

Audited from `60ff97d12b`, 2026-10-19 (Asia/Taipei). **Ordinary guest eligibility
remains blocked; F2/F3 remain Open.** This slice does not expand DMA, NCE, Texture
Cache, physical content authority or automatic ordinary write routing.

## Owner and lifetime gaps before ordinary guest activation

| Boundary | Actual enforcement | Remaining gap |
| --- | --- | --- |
| Controlled CPU owner | `CpuWriteOwner::TryVisit` retains an entry and shared owner lock around actual bounded store/publication; Close seals entry and takes the exclusive lock before process PTE finalization. | Request preparation previously used an empty visit, then touched Memory/PTE/backing outside it. This slice repairs that narrower gap. |
| Initial API/object entry | Callers retain System/DeviceMemory and process/Memory during the borrowed API call and obtain its current controlled owner. | A raw `Memory*` and shared gate state are not a process or System lifetime pin. Concurrent object destruction/reset before initial owner acquisition remains unsupported. |
| Raw page preparation | `PrepareControlledCpuPage` qualifies context/PTE generation and retains physical storage. | Its public cold API still requires the caller to retain process/PTE owners. A prepared pin is not a blanket execution or guest exclusion lease. |
| CPU PTE/native mapping publication | Actual `MapMemoryRegion`/`UnmapRegion` call MapPages before HostMemory Map/Unmap; cache/debug permission restoration has separate paths. | Inner HostMemory refusal cannot roll back already-published outer PTEs. PageTable public entries/fastmem pointers lack an all-owner publication closure. No outer freeze is issued. |
| Host mapping cohort | Accepted fresh controlled arenas reuse their actual HostMappingDomain after owner drainage. | Ordinary DeviceMemory remains default-off; existing aliases, escaped raw pointers, mutable spans/fds and export history cannot be adopted as a closed cohort. |
| Process/backend execution | Fixed four-core managed entry closure waits for counted calls to exit. | It does not certify ARM/NCE/Dynarmic exit or join every process/service. These paths are unchanged. |
| Host services | Audio device-session WriteBlockUnsafe and VIC GMMU WriteBlock are concrete independent writers. | They and outstanding kernel I/O/debug/raw callbacks have no exhaustive lifetime-scoped close/join. Guest scheduling suspension is insufficient. |
| Device/GPU work | Existing controlled queue completion and retained native/physical owners have explicit scopes. | Queued ordinary transfers, shader/image writers, DAddr/GPU mapping publication and guest-wide retained Unknown isolation are not enrolled. No new GPU admission is enabled. |

This inventory identifies confirmed gaps, not a complete census of all guest
producers. Neither closing one CPU owner nor the new preparation boundary supplies
an ordinary guest exclusion capability. The broader
[feasibility audit](M4_F3_GUEST_EXCLUSION_FEASIBILITY_AUDIT.md) remains binding.

## Smallest implemented boundary

`Memory::PrepareControlledWrite` now captures its existing owner and delegates the
whole cold snapshot, physical pin, request construction and registry publication
to `CpuWriteOwner::TryPrepare`. The old empty `TryVisit` readiness probe is removed.
Only the existing explicit controlled scalar request API uses this path.

Ordering and lifetime:

1. Acquire one actual managed entry, then the owner shared lock with try-lock.
   Closed/retired/contended entry refuses before the factory accesses Memory.
2. Hold that lock through actual cold preparation and immutable request creation.
   Existing context/PTE snapshot locks still end before backing materialization;
   generation revalidation remains unchanged. No cache/sys-core guard, renderer
   dispatch, GPU polling/wait, CPU store or physical range admission is introduced.
3. Release the shared owner lock **before** Track tries its exclusive registry
   lock. The same managed entry remains held through Track and result construction.
   This avoids recursive shared/exclusive locking on the owner mutex.
4. If Close wins between those phases, Track refuses using the retained owner and
   request only; it does not access a destroyed Memory/PTE or replay a write.
   Before-effects exceptions unwind both locks/entry without publishing a request.

Close/Reset/Finalize already seal the old owner before replacing Memory state or
finalizing process mappings. Their exclusive owner lock now joins the cold
factory's Memory/PTE accesses as well as entered store callbacks. The registry-only
tail may subsequently finish by refusing against the closed retained owner; it
does not keep using Memory/PTEs. Only the managed closure's zero-entry Poll proves
that the full preparation/registration entry has exited. This is a CPU-only
lifecycle join, not a bounded
wall-clock/preemption guarantee or GPU completion join. Close must not be invoked
reentrantly from the factory. Callers still keep the original object alive at
initial entry; the factory is trusted implementation code, not an arbitrary guest
callback or a new general preparation/export interface.
Call Close outside context/PTE/HostMemory mutation locks. Current production
mutators do not invoke it while holding those metadata guards, and KProcess closes
the owner before PTE finalization. Future outer freeze orchestration must enforce
that same order rather than close an owner while holding a lock its preparation
needs. No general all-caller lock-order proof is inferred from this narrow audit.

## Validation and exact limits

`run_cpu_preparation_owner_tests.py` requires the existing first-registration
generation. It executes extracted actual Memory preparation methods with the
actual owner/header and real HostMemory/PageTable implementations; System/device
services remain fixtures. A deterministic pause after the actual physical pin
checks all four scalar widths: collective close stays Pending, early resume/new
entry refuses, the real exclusive owner mutex cannot pass the active preparation,
and drainage follows request registration. It also checks pre-effect exception
cleanup, subsequent preparation, and prepared-request cancellation at owner close.

Three separately compiled assertion controls remove counted preparation, unlock
before the cold factory, or restore the exact old empty-visit design. Compiler
errors, timeouts and unrelated crashes never count as successful controls.
The positive case repeats three times.

The native scheduler peer separately runs this actual helper against a real
process Memory and actual cold physical pin, pausing its factory across managed
closure and checking exception cleanup. That peer is test orchestration; it does
not prove the public scalar request API is automatically entered by a game CPU.
The native provenance processes must print the new preparation marker. Existing
ordinary F2/F3 defect/fixed-mode controls, complement preservation, tracker,
checked completion, owner/mapping mutations and lifecycle suites remain required.

Local Windows checks have passed the focused three assertion controls and three
positive repeats, plus ten repetitions of both existing first-registration
orderings and their original negative controls. The existing managed-drain
unit/body fixtures and all assertion controls also passed. Linux native/full
regression validation for this changed source is verified below; earlier
`a36bae493c` acceptance alone was not used to accept these changes.
Android is on demand and no local Android build is used.

## Verified Linux acceptance

[Run 39251597638](https://github.com/Splaser/emulator/actions/runs/39251597638)
passed at exact production/test source
`9d50526fd500267fb1c1d7a81078903a28365010`. Linux job `118056473944` passed in full;
Android job `118056475633` was skipped. Subsequent evidence updates are docs only.

Independent artifact verification confirms:

- Artifact `12187957923` matches its published ZIP SHA-256
  `b5e8874b1c24500019bdcbbfa8d27523f903bc0e47890316ccf7308fdd340825`.
  Its source stamp and all 131 hashes across ten manifests match Git at the exact
  source and resumed HEAD. Windows worktree comparisons match after configured
  CRLF normalization, with no source-content exception.
- All three new preparation mutants failed assertions, and the positive actual
  preparation scenario passed three repeats. These execute extracted actual Memory
  preparation bodies and real HostMemory/PageTable with fixture System services.
- All five CTest processes passed: 130 ordinary cases (3,966,703 assertions),
  scheduler 1,708 assertions, F3 witness 38, admitted transfer 88 and provenance
  **479**. Each of three additional provenance processes also passed 479 assertions
  with the new real-process preparation/pin closure and exception-cleanup marker.
  This native pause is a test-peer factory, not automatic game-CPU enrollment.
- Existing eleven owner and nineteen mapping assertion mutants, their three real
  repeats, full synchronization/content boundary suite, production builds,
  HostMemory/import, coherency/lifecycle/fault regressions all passed. Both 77
  registration interleavings and the original GPU-dirty complement checks remain.
- F3's fixed-mode control retains exactly two alias failures, 36 passing
  assertions and exit 42; F2 still produces backing77/cache10 and fixed-mode exit 1.
  No repaired ordinary-game coherency witness is claimed.

Downloaded metadata/artifact, full job log, independent verifier and acceptance
summary are retained in ignored `.diagnostics/ci-39251597638/`. This accepts the
controlled preparation lifecycle fix only. **F2/F3 remain Open.**

Next ordinary-guest gate is still outer PTE/process/host-service producer closure
with proven lifetime retention and publication ordering, followed by device/GPU
owner completeness. Do not turn this CPU preparation fix into a guest lease,
frame-path instrumentation or automatic coherency activation.
