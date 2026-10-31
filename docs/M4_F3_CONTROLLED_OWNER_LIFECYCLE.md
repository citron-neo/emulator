# M4-F3 controlled owner lifecycle substage

Follow-up: the [CPU preparation lifetime audit](M4_F3_CPU_PREPARATION_LIFETIME.md)
identifies ordinary-guest owner/publication blockers and closes the empty-visit
gap in explicit scalar request preparation. Source `9d50526fd5` passed separate
[Linux validation 39251597638](https://github.com/Splaser/emulator/actions/runs/39251597638),
including three preparation assertion mutants/repeats and 479 assertions in each
native provenance process. Exact artifact/source checks are in the linked record.
The earlier CPU-only arena evidence below remains scoped to `a36bae493c`.
No ordinary guest admission is enabled.

## Scope and enforcement

`Common::ControlledGuestArena` constructs fresh, private HostMemory with two fixed
4096-byte virtual aliases of one physical page. It accepts no existing backing,
caller owner census, callbacks, raw view, fd, physical pin, renderer or PageTable.
Only its private factory creates move-only owner handles. A fixed eight-slot
registry records enrollment; slots are never reused. Losing any owner seals the
entire cohort, including an idle cohort. Registry size is not a drainage proof.

Every public byte Store/Load obtains an actual ProducerDrain entry before pointer
resolution and retains it through the access and release of the byte data lock.
Admission is the owned atomic gate, not the ordinary mutex. Closing the gate also
closes registration. A pre-resolved operation keeps the gate occupied and retains
the State/HostMemory allocation independently of owner/arena/close handles.
Only an acquire observation of the owned gate's zero in-flight entries after
closure yields DrainedSubset. This means this closed CPU-only cohort only.

The byte mutex protects C++ accesses from races; it is try-only. Closure/Poll hold
no registry, byte, PTE, cache, renderer or GPU completion lock. Failed acquisition
after entry, abandoned access, registration contention/overflow, invalid range,
owner loss and retirement seal the cohort. Exceptions after admission seal before
propagating. Exhausted polling budget becomes sticky Unknown, even when the
writer later exits. A preexisting operation may finish after retirement/timeout;
its backing stays alive and the cohort cannot reopen. No forced interruption or
rollback is claimed. Failed or abandoned closure never silently resumes.

After real drainage, Close can hold the **same HostMappingDomain** owned by its
private HostMemory. Actual Map/Unmap/Protect ingress then refuses mutation before
effects. There is no second mapping authority. Cancellation is explicit and
requires a fully drained, non-Unknown CPU-only closure. This type has no API that
can submit asynchronous GPU effects; it must not be extended with an export or
device operation without replacing its cancellation/lifetime protocol.

## Explicit stop before outer CPU PTE freeze

This is a usable controlled backing owner substage, **not a guest lease**. No
KProcess/PageTable consumes this arena. It cannot enroll existing guest owners.
The following remain Unknown and block outer CPU PTE integration:

- Guest CPU/NCE and Dynarmic execution do not use these owner handles. Existing
  core entry gates alone do not prove backend exit or full all-process joins.
- Existing raw pointers, audio/VIC/I/O/kernel/host services and physical/native
  exports have no exhaustive owner lifecycle closure.
- Core::Memory publishes MapPages before the HostMemory mapping operation;
  refusal of an inner Map cannot roll back that earlier PTE side effect.
- PageTable public entries, fastmem pointers and independent permission updates
  have no enforced, lifetime-bound owner admission at every publication point.
- GPU/device mappings, imported resources and queued asynchronous effects have
  no cohort admission, completion join or retained guest-wide Unknown isolation.

Accordingly this change does not touch ordinary Core::Memory, PageTable, NCE fault
paths or BufferCache admission. GuestBackingAdmission always returns Unknown.
No CPU PTE freeze token, fabricated certificate or guest resume API is provided.
Planner, Copy forms/provenance, physical fan-out/private provider and physical
latest-writer authority stay at their previously accepted boundaries. Original
F2/F3 negative witnesses must continue to reproduce.

## Controls and acceptance

`run_controlled_guest_arena_tests.py` compiles actual Linux HostMemory/syscalls;
only logging/assert infrastructure is stubbed. It first requires assertion kills
for eleven source mutants, then three positive repeats. Compiler failures,
timeouts or unrelated crashes do not count as mutation kills. Inputs are hashed
in its artifact manifest. The privileged test peer pauses the exact private
Operation used by public Store after real pointer resolution; production users
receive neither this operation nor a raw pointer.

Controls cover a raw writer resolved before old mapping closure (still writes),
new owner/store/read refusal, in-flight drainage, frozen writable alias creation,
remap/unmap/unprotect, explicit cancellation, early owner release, retirement,
abandoned operation/exception/closure, timeout, fixed-capacity overflow, invalid
access and backing retention after early handle destruction. A separate native
Vulkan control executes checked fill/completion/readback on unrelated ordinary
backing while a controlled owner holds its resolved pointer and byte mutex;
the GPU callback must refuse arena access before trying that mutex. This proves
that dependency's progress, **not GPU ownership of the arena**.

Linux acceptance for this owner substage is now verified in run `39212903431`,
as recorded below. Earlier HostMemory/private-provider runs are not its evidence.
Android was skipped this round.

The first Linux-only attempt [39211644236](https://github.com/Splaser/emulator/actions/runs/39211644236)
at `603a606f56f3ebf7a79a813796e815c41497881c` stopped in mutation validation:
ten owner mutants failed assertions, but `closure-drop-reopens` survived. The
in-flight drop case alone was insufficient: its later entry decrement masked the
reopening. A separate idle-closure drop control now directly checks Store,
registration and re-close refusal. This failed attempt supplies no new owner or
native acceptance. The production implementation is unchanged by this test fix.

## Opt-in diagnostics and ordinary fast-path boundary

Enrollment and this arena's measured accesses are explicit slow-path prototype
operations. They are not selected during normal frames. Whole-guest quiescence
remains a separate, blocked slow-path design, never an automatic production route.
Ordinary guest/NCE accesses gain no per-access mutex, registry lookup, allocation,
clock/counter operation or synchronous GPU wait. The default ProducerDrain entry
instantiates an unmeasured implementation; the diagnostic overload alone records
the actual failed-CAS reason. A local Clang `-O2` comparison against the pre-metrics
entry implementation produced identical assembly, with no clock/metrics symbols;
this is not an ARM64/NCE runtime benchmark.

`SlowPathMetrics` keeps separate Admission, Drainage, MappingFreeze and
GpuCompletion snapshots: invocation count, total host nanoseconds, contention,
refusal and Unknown observations. Snapshots are nontransactional relaxed counters,
diagnostic only; they are never admission, exclusion or completion evidence.
Admission contention counts an actual failed CAS or byte/registry try-lock.
Closed-entry refusal is recorded separately. Drainage contention counts Pending
with existing in-flight entries. Mapping contention counts the existing
HostMappingDomain try-lock failure, not a second mapping authority. GPU completion
contention counts actual checked Pending/Submitted polls; it does not claim to
observe a Vulkan driver's internal lock. Timing covers host API call cost, not
device execution duration or total stop-to-drain latency; nested stages can overlap
and their times must not be added as independent frame cost.

Scheduler exposes explicit `PollCheckedWriteMeasured` and
`PollCheckedTransferMeasured` wrappers. Existing frame routes and ordinary checked
polls remain unmeasured and do not call them. The wrappers poll once, preserve the
existing result/Unknown quarantine, and add no synchronous wait, renderer callback
or resource allocation. Tests record real mapping-lock/data-lock contention and
native Vulkan worker-pending completion counters independently of successful
completion. Numeric observations are saved in the CI logs, with no timing limit
used as a correctness certificate.

Linux-only [39212193213](https://github.com/Splaser/emulator/actions/runs/39212193213)
at `ebf84de7b4528eda91c7e2499c750f8e640495c1` was superseded and cancelled after
the request for separate opt-in diagnostics. It cannot accept the metrics source.

## Historical pause checkpoint — 2026-10-19 afternoon

Paused at the user's request to wait for the five-hour usage reset. All code is
pushed to PR #375's existing `codex/issue-372-fixes` branch. Exact code/test source:
`a36bae493c555dd3a6f50f7e48f6cd88cb9d67ef`; subsequent checkpoint changes are
documentation only. No Android build was dispatched.

Current Linux-only run: [39212903431](https://github.com/Splaser/emulator/actions/runs/39212903431),
Linux job `117930077501`, Android job `117930078993` **skipped**. At this
checkpoint the run is still building CLI/shader/tests. The following steps have
passed: real HostMemory mapping suite, real controlled owner suite (eleven
assertion mutants and three positive repeats, including idle closure drop and
real byte/mapping lock contention), early native integration translation units,
and the full synchronization/content boundary suite. This is **not full native
or regression acceptance**; do not infer final workflow success from this note.
The existing remote run is left running; no replacement run is needed merely
because the conversation paused.

On resume, first inspect this exact run's final result and first actual failure,
if any. If successful, download its artifact and verify source stamp, ZIP digest,
all manifest hashes against Git at the source above, eleven owner and nineteen
mapping assertion mutants, three real owner repeats, all five CTest processes,
three native provenance repeats with both new progress/metrics markers, and the
original F2/F3 negative witnesses. Record actual assertion counts and metrics;
do not reuse the preceding source's 447-assertion count. Then update this document
and the two linked audit/mapping documents with final evidence and push only this
branch. If it failed, repair that failure and dispatch Linux-only for the changed
source; compiler failures/timeouts are never successful mutation kills.

Local ignored continuation evidence is prepared in
`.diagnostics/ci-39212903431/verify_acceptance.py` (requires the final job log,
metadata, jobs JSON and downloaded/extracted artifact before it can run).
`.diagnostics/m4-fast-path-audit/` retains the identical ordinary-entry assembly
comparison; `.diagnostics/ci-39211644236/` retains the failed mutation-attempt log.
These local paths are helpers, not substitutes for final remote evidence.

The implementation stop remains deliberate: owner cohort and inner HostMemory
freeze only. Outer CPU PTE/process/backend/service/GPU enrollment and retained
guest-wide Unknown isolation are still blocked as listed above. No ordinary
guest fast-path instrumentation, automatic frame route, guest lease or general
resume authority has been enabled.

## Verified resume acceptance — 2026-10-19

Resumed from checkpoint `dc7a0a2971`. Linux-only
[39212903431](https://github.com/Splaser/emulator/actions/runs/39212903431)
completed successfully at exact code/test source
`a36bae493c555dd3a6f50f7e48f6cd88cb9d67ef`; Linux job `117930077501` passed,
Android job `117930078993` was skipped. Subsequent checkpoint/evidence commits
change documentation only. No replacement build was needed.

Independent verification downloaded artifact `12170850326`, checked its source
stamp, and matched the published ZIP SHA-256
`ed966c1776d3457d2544eaf457f7b828076082ab7bf96f837940591322c59ec5`.
All 125 SHA-256 entries in nine manifests match Git at that source and the resumed
HEAD. Windows worktree comparisons agree after its configured `core.autocrlf=true`
checkout normalization; no source rewrite was needed. The 57 normalized unique
files are an on-disk line-ending difference, not an exception to Git hash checks.

- Eleven controlled-owner mutants and nineteen mapping mutants failed assertions;
  none was accepted because of compiler failure, timeout or unrelated crash.
  `closure-drop-reopens` now fails the explicit idle-cohort Store/register/re-close
  assertion, covering the previously surviving mutant.
- Actual Linux owner, primitive mapping, production HostMemory and NDEBUG
  protection-failure controls each passed three repeats. The owner tests exercise
  resolved-pointer retention, real byte/mapping contention and sticky Unknown.
- Early native integration units, the complete synchronization/content boundary
  suite and Linux CLI/shader/test targets passed, followed by all configured
  HostMemory/import/coherency/lifecycle regressions.
- All five CTest processes passed: 130 ordinary test cases (3,922,962 assertions),
  scheduler 1,708 assertions, F3 witness 38, admitted adapter 88 and provenance
  **467**. The three extra provenance processes each passed 467 assertions and
  both the resolved-owner GPU-progress and opt-in completion-metrics markers.
  Do not reuse the preceding source's 447 count.
- The native GPU progress control completes unrelated real Vulkan work while a
  controlled owner retains a resolved pointer and byte mutex; attempted arena
  access refuses before that mutex. This does not enroll GPU ownership of the
  arena or prove ARM/NCE/backend exit.
- Original F3 repeats reproduce stale aliases; fixed mode has precisely 36 passing
  and two failing assertions, exit 42. Original F2 repeats retain backing77/cache10;
  its fixed-mode check exits 1 as required. **F2/F3 remain Open.**

Recorded diagnostics from the three real owner repeats:

| Measured operation | Calls per process | Total host ns, repeats 1/2/3 | Contention | Refused | Unknown |
| --- | --- | --- | --- | --- | --- |
| Owner admission | 10 | 611 / 614 / 601 | 0 | 5 | 0 |
| Owner drainage | 8 | 289 / 270 / 270 | 3 | 0 | 2 |
| Host mapping freeze | 2 | 340 / 350 / 391 | 0 | 1 | 0 |
| Separate real mapping-lock contention control | 1 | 3817 / 3767 / 3497 | 1 | 1 | 1 |

Checked GPU completion in the three extra native processes recorded respectively
9/9/8 calls, 6443/5371/10418 total host ns and 4/4/3 pending-contention observations.
The CTest process recorded 9 calls, 9658 host ns and 4 contention observations.
These are opt-in host-call diagnostics, not device execution duration, additive
frame cost, a performance threshold or new correctness authority.

Artifact, metadata, full Linux log, independent verifier and `acceptance.json`
are retained in the resumed machine's ignored `.diagnostics/ci-39212903431/`.
This closes Linux acceptance for the fresh CPU-only owner substage only.
The outer PTE/process/backend/service/raw/GPU enrollment and retained guest-wide
Unknown isolation blockers above remain; ordinary game routing stays off,
GuestBackingAdmission stays Unknown and no guest lease/resume API is issued.
Continue on `codex/issue-372-fixes`; push evidence there only, without promoting
personal `main` or merging upstream as part of this checkpoint.
