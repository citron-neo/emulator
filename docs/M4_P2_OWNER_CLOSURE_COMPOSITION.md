# M4 P2: joint callback, Memory and GPU owner closure

The [2026-10-26 NVDRV/Host1x/cohort/GPU follow-up](M4_P2_NVDRV_COHORT_GPU_COMPOSITION.md)
now runs its concrete Module/session/retirement graph in the same active cohort
and ThreadManager scenario. This is a separate combined fixture from the Audio
scenario below; it does not claim every service family runs together or establish
native guest exclusion. The original suite still passes after the emitter reuse.

Follow-up: [nvhost_ctrl review and named owner composition](M4_P2_NVHOST_CTRL_OWNER_COMPOSITION.md)
now exercises the actual Module/session/retirement queue and Host1x callbacks
while their final-owner release is counted or deferred. It also records a live
callback after dispatch DrainedSubset. These are separate named schedules;
the idle NVDRV/Host1x domains in the joint fixture below are not upgraded to
active participants by this follow-up. Joining both owner graphs in one actual
cohort-close scenario was pending at that checkpoint; the follow-up above now
supplies that bounded same-graph proof, without widening guest admission.

Follow-up: [actual Audio service target drain](M4_P2_AUDIO_SERVICE_TARGET_DRAIN.md)
now supplies separate terminal raw-target owner evidence and repairs the reproduced
service-release gap. The prepared-packet evidence below keeps its original limits.

Date: 2026-10-25 (Asia/Taipei). Baseline: `d7c6489eadc00cbff9cb89c2fbd1712f2a850d1d`.

## Outcome and scope freeze

One joint, bounded owner chain is now exercised rather than inferring composition
from separate domain tests: cohort closure -> actual AudioManager registered
callback -> managed Memory facade / pointer translation -> actual GPU ThreadManager
queued or active callback -> worker join -> retained Session/process/backing release.
Nine interleavings and six named negative controls pass three times each on
Windows. No production lifecycle, scheduling, cancellation or admission code changes.
No new independently suspected destructor problem is patched. The local repair
scope remains frozen at the Presentation Pool checkpoint.

This is a joined **subset fixture**, not full production KProcess acceptance.
AudioManager and ThreadManager use their complete production owner declarations;
AudioManager registration, event, worker and Shutdown bodies and GPU queue/worker/
Stop bodies are actual code. ProcessCohortStop/ProcessExecutionStop and roster
types/bodies, actual KProcess stop factories, Memory::Write8, GetPointerImpl and
PageInfo decoding are extracted. KernelCore/System, KProcess lifetime/refcount,
backend, fixed PTE, scalar Impl store and HostMemory allocation remain explicit
collaborators. Native rendering is a fixture. The runtime does not instantiate a
full production KProcess, run Dynarmic/NCE or submit Vulkan commands.

The Session capture is a **prepared test packet**, not the current production
AudioOut/In callback target. Actual AudioOut/In::Manager::LinkToManager registers
`std::bind(&Manager::BufferReleaseAndRegister, this)`. ServerManager owns the
Service::Audio IAudioOut/InManager service, whose unique_ptr impl owns that raw
target. AudioManager's std::function does not itself retain that target. The
runner checks and hashes these actual registration/owner sources, but does not
prove their service teardown sequence safe or reproduce a defect there. The
whole Audio row therefore stays Unknown. Do not promote prepared shared captures
to evidence for the production raw-target lifetime.

## Owner chain and sustained obligations

| Owner or capability | Before close and while active | Join/retention observation |
| --- | --- | --- |
| System/Kernel facade | One fixture System definition is shared by actual core/GPU bodies; caller keeps it alive throughout | Deliberate precondition, not a new System lifetime capability or an ODR-incompatible pair of Systems |
| Process cohort | Actual factory holds creation/init/publication closures, roster, requester and backing; real core RunThread/stop body executes with a controllable backend | UnlockThread can remain blocked after halt request: Audio closure must not advance before counted backend exit. Idle owner references remain captured too. KProcess/refcount is a collaborator |
| AudioManager | Full owner owns its jthread, Event and registered std::function capturing Session | Active callback keeps AudioCallbackDomain pending. New event after drainage reaches actual ManagedHostProducer closed-entry pause without invoking another callback. Shutdown wakes/joins it |
| Managed Memory | Actual Write8 admission spans pointer translation and the fixture store | An ordinary writer can stop after real GetPointerImpl resolves an address; cohort remains Pending until its store and thread join. Closed ingress refuses before translation/effects |
| GPU ThreadManager | Full production owner/queue/callback path captures the same Session while its body is blocked | Stop joins active work; queued cancellation executes no callback. Execution and completion observations are separate; shutdown wakeup of RunOnGpuThread is not a completion receipt |
| Session | Registered callback and queued/active GPU function retain the fixture Session, whose members hold Process reference, RetainedBackingStorage, bytes and Memory view | Dropping the original Session handle cannot retire it. Worker join does not destroy the registered std::function. AudioManager destruction after joins releases that capture |
| Backing and Process | Cohort's actual retained storage type and roster stay alive beyond Session release | After Session/manager and external allocation roots disappear, backing and Process references remain held by the cohort; only its later destruction releases them. No pointer/mapping permission follows from this lifetime test |

The tested close sequence is actual ProcessCohortStop::Poll: process publications
and backend acknowledgement, NVDRV dispatch, Audio render, Audio callback,
Host1x ioctl, then managed writes. This joint chain runs the Audio callback,
ordinary Memory store and GPU worker; NVDRV/render/Host1x domains are idle here.
Their active real worker composition is **not** inferred from the empty domains.
The earlier cohort regression separately holds their entries, which proves only
the managed-domain order, not their complete owner graphs.

## Deterministic schedules and counterexamples

1. Normal nested callback: close while Audio is paused, finish its inner Memory
   writes and GPU callback, hold its tail, then confirm the subset only after the
   actual counted Audio scope exits. New callback ingress stays paused. Retain
   Session through manager lifetime and backing through cohort lifetime.
2. Backend active: actual PhysicalCore remains inside blocked UnlockThread after
   halt request. Host closure cannot start until that counted scope exits/join.
3. Ordinary store active: Write8 has entered and GetPointerImpl has returned a
   pointer. Even after Audio/GPU finish, managed-write drainage remains Pending
   until the store is released and joined; verify the byte and unchanged guard.
4. GPU active stop: Stop waits for the blocked GPU callback. An Audio caller can
   be woken by shutdown earlier; that wake does not retire the active captured
   owner or substitute for GPU join or outer callback drainage.
5. Queued cancellation: an older GPU callback is blocked while Audio enqueues its
   callback. Stop cancels the queued callback without executing it or publishing
   the post-success byte; captures retire only at the known joins/owner release.
6. Callback failure: first Memory byte is written, then callback throws. Actual
   Audio failure sealing makes the cohort Unknown; no success/replay/reopen.
7. Timeout: closure becomes sticky Unknown while Audio/GPU work is still active;
   later completion does not turn it into a success receipt.
8. Late seal: after the subset acknowledges, sealing a domain makes revalidation
   Unknown and no-effects cancellation refuses before reopening.
9. Escaped alias: a pointer obtained through actual GetPointerImpl before close
   can still store a witnessed byte after managed drainage. Backing remains owned
   throughout this deliberately negative fixture. Its success is a bypass witness,
   so GuestBackingAdmission must remain Unknown, not an accepted exclusion proof.

Six controls each require exit 91 with its exact assertion, three repetitions:
omit Audio callback drainage; close inner Memory before outer callback can finish;
replace both callback owner captures with a raw pointer; omit callback failure
sealing; bypass Write8 admission; replace actual RetainedBackingStorage strong
ownership with a weak reference. Raw-owner failure is checked before resuming the
paused callback, avoiding UAF as an accepted oracle. Redundant callback captures
are removed together; removing only one would not be a meaningful owner control.
Compile failure, timeout, generic crash or wrong assertion does not pass.

Rendezvous use real threads and bounded events/promises at the active callback,
GPU enqueue/stop/join and backend UnlockThread. Timing-only observers signal Audio
at its next event wait (after the prior counted scope has ended) and immediately
before actual closed-entry pause locking. Native completion is modeled by a
blocked function, not represented as a real GPU fence or visibility guarantee.

## Windows validation, regressions and reproducibility

Run:

```powershell
$env:CXX='C:/Strawberry/c/bin/g++.exe'
python tools/physical_coherency_tests/run_owner_closure_composition_tests.py --repeat 3
```

The runner prepares the existing CPU actual-body fixture and re-emits actual GPU
bodies; optional reuse verifies every CPU input hash first and never reuses stale
GPU emission. It assembles one System collaborator definition shared by both sets
of bodies, rather than linking incompatible class definitions. Full AudioManager
declaration is included directly; no private-member test access is added.

Eight real production files compile with Windows GCC 13.2, real dependencies and
zero diagnostics: process_cohort_stop, process_execution_stop, process_owner_roster,
physical_core, audio_manager, audio_event, gpu_thread and memory. Each output is
checked as AMD64 COFF. This is object compilation, not an emulator application
link, MSVC build or native device execution. Existing execution/cohort, all 21
managed-write routes including the unsupported-NCE compile branch, Audio callback
and full GPU thread/cancellation/direct-entry regressions also pass.

[Result manifests, input hashes, commands and bounded cleanup](M4_P2_OWNER_CLOSURE_COMPOSITION_RESULTS.json)
travel with Git. Full logs stay local in ignored diagnostics. Current-run generated
executables/objects/fixtures/shim headers are removed only after exact path/hash
checks: 187 generated files, including 82 executables, were cleaned. The workflow
now includes this joint suite; local results do not imply
current-head CI or Linux/Android/game validation.

## Next gate from the coverage ledger

The [coverage ledger](M4_PRODUCER_COVERAGE_AND_ADMISSION.md) remains incomplete.
This test does not make ProcessCohortStop GPU-adoptable and does not call its
no-effects reopening after ordinary Memory/GPU side effects. The test caller
explicitly retains System/Kernel/native collaborators through all joins. Unknown
does not magically retain that facade graph; durable resource/context quarantine
still needs its own proven owner contract.

Before marking the Audio family covered, combine the actual service target owner
and its raw registered callback with service close/join and AudioManager join;
the prepared packet result above cannot discharge that obligation. This is a
coverage gap, not a diagnosed or authorized speculative destructor repair.

Next device composition target: Module/session/OwnedRetirementQueue owner ->
actual NVDRV dispatch -> Host1x queued device operation -> last callback/read,
with the process/backing retained after caller retirement. Start with one
restricted route and real owners; direct GetContainer/device/decoder exports must
remain Unknown unless their actual last-use owner is accounted for. An idle
dispatch domain or completed ioctl cannot acknowledge downstream device work.
This is an existing coverage obligation, not a newly authorized local repair.

Rasterizer/Presentation terminal joins, physical mapping freeze, HostMemory
remap/protection, escaped CPU aliases, native NCE stores and independent services
remain separate blockers. Keep whole-guest quiescence first; no per-access policy
or signal-handler waiting. P2 is not globally complete, M4 admission remains
Unknown, F2/F3 stay open, and no main merge is performed.
