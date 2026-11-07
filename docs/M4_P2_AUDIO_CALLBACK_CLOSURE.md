# M4 P2: retain and drain AudioManager callback sweeps

Date: 2026-10-23 (Asia/Singapore). Baseline: `92d02eb3d5`.

## Production boundary and ordering

The AudioManager worker independently invokes the AudioIn/AudioOut managers'
buffer release/register callbacks. The input path can reach
DeviceSession::ReleaseBuffer and WriteBlockUnsafe. Individual Memory call
admission does not retain the surrounding callback or prevent another callback
from starting. The earlier SystemManager render-cycle gate does not cover this
worker.

AudioCore now passes the Kernel's shared AudioCallbackDomain to AudioManager.
The worker retains one ManagedHostProducer entry across a complete callback
sweep and its event clearing. It first waits for the existing event/timeout,
then releases the event mutex BEFORE entering the producer boundary. A closed
domain pauses there without borrowing new session buffers, consuming the event
bits, or invoking any callback. Resume retries use the existing host-only 1 ms
wait; pending events remain available. After admission, the worker reacquires
the event mutex and checks shutdown before invoking callbacks.

Shutdown requests the jthread stop token before signalling the ordinary shutdown
event and joining. This handles both event wait and domain pause. Initial event
clearing and running publication now happen before the jthread starts, so a late
worker startup cannot erase the shutdown event or overwrite running=false.
The pause does not require the event mutex; the normal admitted callback sweep
keeps the existing event-lock behavior.

If a callback throws, the callback domain is sealed BEFORE its counted entry
unwinds. That worker exits without clearing the failed callback's event. This
does not undo partial writes or recover the session: it prevents callback
failure from becoming a false drained receipt. Existing event bits from earlier
successful callbacks in the same sweep may already have been cleared.

| State | Disposition |
| --- | --- |
| Closure before callback admission | Preserve event and pause outside event lock |
| Callback already entered | Closure stays Pending through callback return and event clearing |
| Callback throws after possible partial effects | Seal domain before entry exit; leave failed event uncertain |
| Shutdown while paused | Stop token wakes entry wait; join requires no callback or event-lock acquisition |
| Healthy no-effects cancellation | Reopen callback domain in the same ResumeBatch as the other cohort domains |
| Late Seal or Seal during cancellation | No stale stopped receipt or partial write/CPU reopening |

ProcessCohortStop now composes backend stop, Audio render-cycle drainage,
AudioManager callback drainage, THEN managed Memory write drainage. The managed
write domain stays open for an admitted callback's remaining writes. The new
closure is retained, rechecked on stopped Poll, sealed on Unknown, and included
in the shared atomic ResumeBatch. Poll adds no blocking callback join itself.

## Executed local validation

```powershell
python tools/physical_coherency_tests/run_audio_callback_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-callback-cycle
python tools/physical_coherency_tests/run_process_execution_stop_tests.py --output-dir .diagnostics/m4-callback-execution
python tools/physical_coherency_tests/run_process_cohort_stop_tests.py --prerequisite-dir .diagnostics/m4-callback-execution --output-dir .diagnostics/m4-callback-cohort
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/audio_core/audio_manager.cpp --source src/audio_core/audio_core.cpp --source src/core/hle/kernel/kernel.cpp --source src/core/hle/kernel/process_cohort_stop.cpp --source src/core/hle/kernel/k_process.cpp --output-dir .diagnostics/m4-callback-production
```

The Audio callback suite executes extracted complete production constructor,
ThreadFunc and Shutdown bodies plus the real Event methods and shared producer
helper. AudioManager's member layout, callback registration and callback/session
work are fixtures. Three positive repetitions cover pause, event preservation,
resume exactly once, shutdown while the event mutex is held, in-flight callback
drain, thrown callback quarantine, and immediate startup/shutdown.

Five exact runtime mutants fail at their named assertions: absent admission,
pause while retaining the event mutex, entry release before callback completion,
omitted shutdown wakeup, and omitted exception sealing. The execution-stop and
cohort suites each pass three positive repetitions, with five and fourteen
exact controls respectively (24 controls total across the three suites).
New cohort cases cover callback-before-Memory drainage, restored callback
admission, late Seal, and Seal during local resume or before group commit.

Five actual complete Windows production sources compile to AMD64 COFF objects
without diagnostics. Input hashes, object digests and machine signatures are
checked before deleting this round's generated sources, manifests, binaries,
objects and logs. Reproducible runners and this Markdown record remain. The
existing Linux workflow includes the new runner; no workflow is dispatched here.

## Remaining P2 gates

This covers only callbacks invoked by AudioManager's worker, not all Audio
operations. Direct HLE buffer registration/release calls, generic service
publication, sink/backend callbacks, Opus/DSP workers, VIC/host1x, NCE/fastmem,
escaped raw aliases and GPU writers remain independent. Event notification by
another thread is not proof that thread stopped. No backend device stop, session
lifetime/backing retention, or native GPU completion follows from this gate.
The shared domain retains its bookkeeping, not Audio sessions or guest buffers.
HAS_NCE managed Memory closure remains unsupported as before.

P2 and M4 remain incomplete. GuestBackingAdmission stays Unknown. No full
application link, actual audio-device/DSP execution, native Vulkan run, Android
build, main merge or fresh remote CI success is claimed. Runtime pause latency
and device behavior still need separate validation.
