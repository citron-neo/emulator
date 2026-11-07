# M4 P2: Audio render cycles pause and drain before managed writes close

Date: 2026-10-23 (Asia/Singapore). Baseline: `c0257a7c40`.

## Real producer boundary

The Audio Renderer SystemManager worker generates command buffers, signals the
ADSP renderer and waits for a RenderResponse before the next cycle. DSP commands
can write guest bytes via Memory::WriteBlockUnsafe and can also hold command-list
and pool pointers. Closing individual Memory calls does not stop this producer
or drain an in-flight DSP cycle.

SystemManager now owns a ManagedHostProducer bound to a Kernel-shared Audio render
domain. It enters at the beginning of each complete cycle, BEFORE mutex1 or any
command generation, and retains that entry through Signal, checked Wait and
PostDSPClearCommandBuffer. Open-cycle entry has no pause-mutex acquisition or
per-cycle wait-object allocation. Cold pause machinery is allocated when the
producer object is constructed, before its worker is published.

When the domain closes, a new cycle pauses at that outer boundary: it holds no
service/list locks or newly borrowed command pointers and does not signal DSP or
fabricate a RenderResponse. No queued render cycle is dropped. The host-only wait
retries admission at 1 ms intervals because ResumeBatch has no notification API;
stop-token cancellation wakes it. SystemManager::Stop can therefore join a paused
manager even while mutex1 is held elsewhere, then invoke the existing DSP stop.
This is a cooperative cycle pause/drain, not termination of every audio thread.

## Acknowledgement and Unknown

AudioRenderer::Wait previously logged an unexpected message and still cleared
the command buffers. It now returns false BEFORE clearing on an unexpected
message. SystemManager quarantines its render domain and exits that worker path.
Only the correct RenderResponse clears the buffers and completes the counted
cycle. A wrong acknowledgement cannot be mistaken for a drained DSP producer.
This retains uncertainty; it is not recovery or proof that an unexpected DSP
operation has stopped. Terminal DSP shutdown and its mailbox protocol remain
external owner responsibilities.

| Cycle state | Disposition |
| --- | --- |
| Admission open | Generate commands, Signal and wait under one retained cycle entry |
| Closure wins before entry | Pause outside list/service locks; publish no commands/ack |
| Closure races admitted generation/DSP work | Remain Pending until correct response and buffer clear finish |
| Unexpected response | Do not clear borrowed command metadata; seal domain; no drained result |
| No-effects cancellation | Shared ResumeBatch reopens the Audio domain with CPU/init/publication/managed writes |
| Stop while paused | Stop token exits entry wait without guest work; actual manager Stop joins before DSP Stop |

## Cohort composition

ProcessCohortStop now orders these subsets as follows:

1. Retain creation/init/publication closures and capture the Process cohort.
2. Obtain the existing actual backend-stop acknowledgement.
3. Close and drain SystemManager-owned Audio render cycles.
4. Only then close and drain ordinary managed Memory facade writes.

This order keeps managed Memory open for an already admitted DSP cycle's remaining
writes. The new cycle closure is retained through the stop handle's lifetime,
rechecked after a successful stop, quarantined on Unknown, and added to the SAME
atomic ResumeBatch. A late Audio Seal cannot leave a stale BackendsStopped receipt
or reopen managed writes. Poll performs no new thread join or DSP wait itself;
it remains bounded and fails closed on unresolved dependencies.

The Audio domain is independent of HAS_NCE and adds no signal-handler policy.
The existing managed Memory domain still explicitly refuses closure in HAS_NCE
builds, and this change does not make NCE execution stoppable or grant admission.

## Executed local validation

```powershell
python tools/physical_coherency_tests/run_audio_render_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-audio-cycle-final
python tools/physical_coherency_tests/run_process_execution_stop_tests.py --output-dir .diagnostics/m4-audio-execution
python tools/physical_coherency_tests/run_process_cohort_stop_tests.py --prerequisite-dir .diagnostics/m4-audio-execution --output-dir .diagnostics/m4-audio-cohort
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/audio_core/renderer/system_manager.cpp --source src/audio_core/adsp/apps/audio_renderer/audio_renderer.cpp --source src/core/hle/kernel/kernel.cpp --source src/core/hle/kernel/process_cohort_stop.cpp --source src/core/hle/kernel/k_process.cpp --output-dir .diagnostics/m4-audio-production
```

The Audio suite runs actual full SystemManager ThreadFunc and Stop, AudioRenderer
Wait, and the real host admission helper/ProducerDrain. Three repetitions cover
closed-cycle pause and resume, stop/join while the list mutex is held, admitted
DSP-response delay, and unexpected-response isolation. DSP mailbox, command
generation and DSP Stop are fixtures; real audio devices and the actual DSP Main
thread are not executed. Four exact runtime controls reject missing cycle entry,
early entry release, ignored response failure and buffer clear on a wrong response.

The process/core and cohort suites pass three positive repetitions. Five execution
controls and twelve cohort controls hit their named assertions. New cohort tests
retain an admitted render cycle, require managed writes to stay open while it
drains, compose cancellation, and reject a late Audio Seal. Two new controls omit
Audio drainage or ResumeBatch membership. Existing cross-domain cancellation
controls remain present; their sequential-release model reopens Audio separately
too so it continues testing atomicity rather than only a missing new member.

Five complete production source files generate Windows AMD64 COFF objects without
diagnostics. Final source hashes, object digests and machine signatures are checked
before cleanup. The existing workflow includes the new runner for future runs;
no workflow is dispatched here.

## P2 limits

This closes the SystemManager-owned render-cycle subset only. Audio input/output
manager callbacks, Opus workers, generic HLE Update/publication, VIC/host1x, arbitrary
public AudioRenderer Send callers, raw/escaped aliases, native GPU writers and
durable complete Unknown resource ownership remain independent. The shared gate
does not retain an Audio System, KProcess, command buffer, mapped pool or device:
their caller/Process ownership and borrow lifetime must still be proved separately.
Per-cycle pause alone cannot certify all asynchronous audio or all guest writes.

Thus P2 remains incomplete, as do the other M4 phases. GuestBackingAdmission stays
Unknown. No Android local build, real NCE/DSP/Vulkan execution, full application
link or main merge is claimed. Device performance and resume latency remain
unvalidated. Temporary sources, manifests, logs, objects and executables from this
round are removed; Markdown conclusions and reproducible test sources remain.
