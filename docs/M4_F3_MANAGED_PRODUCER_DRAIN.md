# M4-F3 bounded managed entry close-and-drain prototype

This implements the first execution-entry component of the
[guest exclusion design](M4_F3_GUEST_EXCLUSION_FEASIBILITY_AUDIT.md), not a full
guest-backed exclusion provider. Ordinary guest admission remains **Unknown**.
There is no GPU recording/admission endpoint, authority publication or BufferCache
automatic routing. No signal handler, NCE fault emulation or legacy pause code is
changed. Cross queue remains Unknown; F2/F3 remain Open.

## Real enforcement points and stop evidence

`PhysicalCore::RunThread` obtains an owned `ProducerDrain::Entry` **before** backend
initialization/execution. Entry persists until the complete function returns,
including backend execution and its host handling. Closing atomically rejects
subsequent entries; already entered execution must return before its release count
can acknowledge drainage. Neither an interrupt request nor a caller-provided bool
is a stop acknowledgement.

`KernelCore::TryCloseGuestExecution` enumerates the fixed four actual PhysicalCore
objects, never a caller-selected subset. Missing/uninitialized cores or failed
close return no operation; already acquired closures become sticky Unknown and
stay closed. A complete request polls each core and returns Pending until every
entry has exited. Any Unknown seals all held core closures. The API does not
suspend service fibers or certify all guest activity has stopped.

Existing `CpuWriteOwner::TryVisit` similarly holds an entry around its real bounded
controlled-store callback. `Memory::TryCloseControlledStores` closes that particular
existing owner's visits. Memory teardown seals the gate before its old execution
mutex/teardown. A queued callback reaching TryVisit after closure is refused; no
legacy fallback is added. Ordinary Memory::Write, bulk/exclusive calls outside the
controlled owner, raw pointer users, audio, VIC and mapping mutators are not newly
enrolled. Closing one Memory owner never implies all process/host owners closed.

The shared gate owns a packed atomic entry count/Closed/Unknown state. Entry uses
one CAS attempt; contention refuses entry. Close and each Poll have finite work,
without taking cache/PTE/scheduler/core-context locks or waiting for a writer.
Poll budgets are 1-4096 calls (default 64), **not wall-clock or preemption claims**.
Remaining active entries at budget exhaustion make the closure sticky Unknown.
If the caller itself owns an active entry, it gets Pending/Unknown rather than
waiting on itself. The gates require lock-free 64-bit atomics.

No new forced interruption or host parking is introduced. Backend exit is
cooperative: a running NCE/JIT backend can remain active indefinitely, and a
blocked SVC can prevent drainage. That is a refusal/Unknown outcome. Closed
entry does not erase in-flight execution. In current CPU loops, refusal returns
without executing another backend attempt; this prototype does not add a sleep
or promise low CPU use while held. Keep diagnostics brief. Real ARM64 stop/resume
and integration with an acknowledged scheduler park are still required before
enabling this for live gameplay.

## Lifetime, cancellation and Unknown

Closures are move-only, single-orchestration-thread objects. Callers keep Kernel
and Memory lifetimes valid; shared gate state is not a pin on those objects or
guest physical memory. A closure cannot resume a counted in-flight writer.
`ResumeNoEffects` / guest `CancelNoEffects` release only a known drained subset;
these are cancellation endpoints for this **CPU-only prototype**, not completion
certificates for GPU effects. There is deliberately no API to attach a submission,
claim effects finished, resume after Unknown or manufacture an asynchronous lease.
After no-effects cancellation an old closure cannot release another close.

Dropping an unfinished closure, owner retirement, or poll-budget exhaustion leaves
entry permanently closed and Unknown; later writer exit does not reopen it. This
enforces *no new managed entry*. It does **not** mean already-running code has
stopped, nor that backing is isolated from unmanaged aliases. Do not label it
safe guest quarantine. No arbitrary guest operation may be recorded based on this
result. Session lifecycle remains responsible for joining/terminating execution;
no resource can be freed merely because the poll budget expired. A future GPU
provider still needs backing/native pins and retained all-owner isolation through
checked completion or safe teardown.

Cancel only after a fully successful collective Poll and while owner lifecycle is
stable. This is not an atomic multi-owner effects transaction: unsupported
concurrent teardown/partial close is Unknown, not a rollback or GPU recovery path.
Do not use legacy Run/Unstall to bypass an active closure: those calls do not clear
these execution gates. Normal prototype cancellation does not alter the existing
user pause state, System suspension flags or saved guest permissions.

## Owner/alias coverage is deliberately incomplete

| Covered actual entry | Acknowledgement | Not covered |
| --- | --- | --- |
| All four PhysicalCore::RunThread gates | Last counted call returned | Host service fibers; backend not yet returned; other writer threads |
| One actual CpuWriteOwner::TryVisit gate | Last counted callback returned | Other Memory owners; ordinary writes and escaped pointers |
| Shared gate ownership/lifetime | Closed/Unknown state retained | Backing/native pin, VMA permission or mapping lease |

Both APIs expose only `DrainedSubset`; `GuestBackingAdmission()` remains Unknown
unconditionally. This is a type boundary, not a successful alias census.
There is no owner/alias list accepted as a certificate and no second producer
authority system. Existing coverage/current-content machinery is not relaxed.

The Linux negative creates **two real MAP_SHARED views of one memfd**. After the
managed subset drains and one view becomes read-only, another thread writes
through the omitted writable view, and the first view observes 99. Thus a truthful
managed drain plus one mprotect still permits an omitted-alias store. This is a
counterexample which must retain Unknown, not a successful guest exclusion test.
All-alias mapping/export closure, audio/VIC/I/O joins, CPU/guest backing pins,
mapping freeze, device/GPU pre-work drainage and retained guest isolation remain
missing. Remap/native retirement do not become eligible through this prototype.

## Tests and evidence classes

Deterministic controls precede acceptance:

- Managed writer holds a real entry while a competing caller closes it; the store
  completes before post-join/acquire drainage succeeds. New entry and early resume
  are refused. Self-dependency hits a finite poll budget without waiting. Drop,
  timeout, owner retirement, duplicate/stale closure and Unknown remain closed.
- Ten gate/type-boundary assertion mutants cover those checks, including the
  false guest certificate. A deliberately false-drained implementation is tested
  red before the real implementation. Compile errors/timeouts are not killed mutants.
- Extracted actual kernel factory, actual PhysicalCore entry prefix and complete
  CpuWriteOwner visitor run against a **mock backend**. Five assertion mutants
  bypass backend/visitor closure, omit a core, bypass collective early-cancel
  validation or omit Unknown fan-out. These do not execute ARM/NCE or the rest of
  PhysicalCore::RunThread.
- The native Linux integration uses real Kernel/PhysicalCore/Memory owners and
  real host threads, with privileged held-entry injection to model an already
  running backend/visit. It checks all-core closure, Pending versus drained,
  early cancellation and real controlled-owner drainage. It is **not** an ARM64
  execution, live NCE interruption or complete guest suspension test. The existing
  real Vulkan provenance/readback tests continue separately.

The new unit/integration fixtures each run three repeats. Linux CI now compiles
the changed kernel.cpp and physical_core.cpp early, then builds the full normal
Linux targets and runs the previous native Vulkan, scheduler, backing, coherency
and lifecycle suites. Original F2/F3 negative witnesses remain intact. Android is
not requested. Exact source/CI acceptance is recorded after the run completes.

## Checkpoint for continuation on another workstation (historical)

Implementation `827073eab4`, followed by test-peer access fix
`7e6996daa317835d76f0b5d4d5ce6a3d884d88bb`, is pushed on
`codex/issue-372-fixes`. Local Windows g++ compiled the real managed-thread unit
and extracted-body fixture; both passed three repeats and all fifteen mutants
failed assertions. Linux-only memfd alias probes were not executed locally.
These local results are not Linux/ARM/NCE or complete guest-quiescence acceptance.

[Linux-only run 39179992784](https://github.com/Splaser/emulator/actions/runs/39179992784)
targets exact source `7e6996daa317835d76f0b5d4d5ce6a3d884d88bb`; it was **pending**
when the user requested immediate handoff rather than waiting. Acceptance remains
unfinished. The preceding run 39179956771 targets the superseded source lacking
the Memory test peer's explicit friendship and must not be used for acceptance.
Read the current run's first real compiler/assertion error before changing code;
do not dispatch another identical run if this one already supplies the evidence.
Download its artifact after completion to obtain kernel/fixture/mutation logs.

Next verify all fifteen new mutants fail assertions, both new fixtures repeat
three times, the real Linux alias-bypass counterexample retains Unknown, the
native four-core/Memory-owner marker appears in each provenance repeat, and all
original F2/F3 and regression checks remain intact. Match source hashes and record
the final CI result in this document and WIP handoff. No Android run is requested.

## 2026-10-19: Linux acceptance recovered and verified

Run [39179992784](https://github.com/Splaser/emulator/actions/runs/39179992784)
completed successfully for exact source
`7e6996daa317835d76f0b5d4d5ce6a3d884d88bb`. The Linux production job passed;
Android was skipped. No duplicate workflow or local build was needed to recover
this result. This supersedes the pending status at the historical checkpoint.

Artifact `12157085513` contains the source stamp, fixture/mutation logs, native
repeat logs and CTest log. Independent verification compared all 108 SHA-256
entries across seven extraction manifests against both Git at that source and
the resumed checkout, including all eleven managed-drain inputs. All matched.

- All fifteen new mutants failed assertions; the separate false-drained baseline
  also failed its intended assertion. Compiler failures/timeouts were not counted.
- Both new fixtures passed their initial run and three extra repeats. Every Linux
  logic repeat observed the real omitted writable memfd alias store and retained
  Unknown; this proves the coverage counterexample, not complete exclusion.
- Each of three native Vulkan provenance repeats passed 447 assertions and printed
  the real Kernel four-core/controlled Memory-owner drainage marker. Held entries
  remain privileged test injection, not ARM/NCE execution or interruption.
- All five CTest processes and the configured backing/coherency/lifecycle
  regressions passed. Original F3 repeats still reproduce stale aliases; fixed
  mode has exactly two failed alias assertions, 36 passing assertions and exit 42.
  Original F2 remains backing 77/cache 10 with its expected failing fixed mode.

Downloaded evidence, full Linux job log, independent verifier and acceptance
index are retained under `.diagnostics/ci-39179992784/` (locally ignored).
Acceptance is restricted to managed entry close-and-drain. Ordinary guest backing
admission, unmanaged writers, mapping freeze and GPU-lifetime isolation remain
Unknown; F2/F3 remain Open. The next implementation must address the remaining
owner/alias and mapping obligations in the feasibility audit without extending
Planner/Copy or enabling ordinary BufferCache routing on this subset result.
