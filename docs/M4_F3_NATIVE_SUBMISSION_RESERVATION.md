# Fence submission reserves ownership before native acceptance

Date: 2026-10-23 (Asia/Singapore). Baseline: `09145ea15b`.

## Resource transaction

SubmitQueueFence previously called native queue Submit and only then inserted its
tick/fence into wait_queue. An allocation failure in that insertion could destroy
the local fence after native work had been accepted. A non-successful Submit also
returned without a retained fence record or terminal completion failure.

The method now obtains a fence and reserves its owning queue record before native
Submit. wait_mutex is held through reservation, native submission and result
publication. The completion worker takes the same mutex, so it cannot observe an
unsent record. No allocating queue insertion follows native acceptance. The real
vk::Queue::Submit wrapper is noexcept and returns VkResult; artificial dispatcher
exception handling is not added to this native path.

| Stage/result | Fence/record disposition | Completion authority |
| --- | --- | --- |
| Failure observed at entry | Refuse before obtaining a fence or issuing native work | Original terminal failure |
| Failure races fence acquisition | Recheck under wait_mutex; unused local fence can retire | No native call for this attempt |
| Record allocation throws | Unsubmitted local owner unwinds; native Submit was never called | No tick advance or new failure latch; retry remains possible |
| Submit returns success | Reserved record becomes worker-visible after mutex release | Worker must still establish native wait/reset success |
| Submit returns non-success | Keep attempted record and queued suffix; latch first completion failure | Never consume an unsignalled rejected fence or certify its tick |
| Old popped fence completes after new failure | Only its older confirmed tick may advance | New isolation and retained submission remain intact |
| Old popped fence fails after new failure | Retain old fence separately; preserve first error | No error replacement or new completion authority |

The worker now wakes on terminal failure even with an empty queue, and checks the
failure before consuming any further records. Failure publication holds the
waiter's free_mutex and notifies all waiters. First-error CAS prevents a delayed
older fence failure from replacing the new submission's failure. A successful
submission already racing an older worker failure may still be accepted, but its
record remains retained and it is not certified complete.

Lock order is wait_mutex then free_mutex where both are held. Fence acquisition
releases free_mutex before taking wait_mutex. The worker releases wait_mutex
before waiting/resetting/recycling its popped fence. No new wait for native fence
completion is performed while holding wait_mutex; native queue Submit itself is
inside that mutex and this change makes no bounded-driver-latency claim.

## Executed local validation

```powershell
python tools/gpu_lifecycle_tests/run_native_fence_failure_tests.py --repeat 3 --output-dir .diagnostics/m4-submit-final
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/video_core/renderer_vulkan/vk_master_semaphore.cpp --source src/video_core/renderer_vulkan/vk_scheduler.cpp --output-dir .diagnostics/m4-submit-production
```

The suite executes the actual full SubmitQueueFence, Wait and WaitThread bodies.
The controlled queue requires the owning record to exist at the native call.
Allocation failure proves zero native calls and successful later retry. Successful
submits complete normally; non-successful results retain their record, wake/fail
waiters, prevent new native calls and never invoke native Fence::Wait. A paused
submit proves wait_mutex remains held until acceptance. Delayed older successful
and failed fence waits cannot clear or overwrite a newer operation's isolation.

The existing native wait/reset cases also run. All positives pass three repetitions.
Six precise controls cover ignored wait results, recycling Unknown, reset exception
escape, absent pre-submit record, omitted submit failure latch and an older error
overwriting new isolation. An initial absent-record control produced an unused
parameter compiler warning; it was corrected and only its exact runtime assertion
was accepted as evidence. Timeouts and unrelated crashes are never passing controls.

Both complete production units generate Windows AMD64 COFF objects without
diagnostics. Source hashes, object digests and machine signatures are checked
before cleanup. Runtime queue/fence results, resource allocation and the disabled
timeline facade are fixtures; this is not native Vulkan device execution.

## Remaining boundary

This transaction retains fence records only. Guest physical backing, command
resources, escaped aliases and a global Unknown resource registry are not proved
owned by those records. Teardown still requires external submission-source and
waiter shutdown plus device idle/device teardown. free_queue allocation failure,
timeline-path failure handling and complete higher-level resource retirement
remain separate audit items. Fail-closed handling of recoverable submission errors
is conservative; recovery policy awaits actual runtime evidence.

Ordinary CPU/NCE/host-service close-and-join, alias/backing lifetime, native
visibility and the first composed Guest-backed admission remain incomplete.
Callback Completed is never native completion. All THREE M4 phases remain
unfinished; GuestBackingAdmission stays Unknown. No Android build, remote workflow,
native device run, application link or main merge is claimed. This round retains
the Markdown conclusion and test source; temporary binaries, objects, generated
fixtures, manifests and logs are removed after verification.
