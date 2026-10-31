# Native fence completion failure retains Unknown ownership

Date: 2026-10-23 (Asia/Singapore). Baseline: `5efdf1d5eb`.

## Actual completion chain and defect

Scheduler submits command buffers through MasterSemaphore::SubmitQueue. In the
fence fallback, successful queue submission puts a tick/fence in wait_queue.
WaitThread waits for that native fence, then publishes gpu_tick. ResourcePool
uses KnownGpuTick to decide reuse; InnerFence::Wait delegates to Scheduler::Wait.
This is distinct from a CPU dispatch callback returning Completed.

WaitThread previously ignored vk::Fence::Wait's VkResult. A failed native wait
could reach Reset, free_queue and gpu_tick publication. This incorrectly treated
an unknown completion as safe resource retirement. The timeline path uses a
different checked query/wait protocol and is not changed here.

The fallback now latches any non-successful native wait as a terminal completion
failure. It preserves the tick/fence in failed_wait, stops consuming queued
suffixes and wakes all completion waiters. Wait throws the original Vulkan result
for an uncompleted target; already confirmed ticks remain available. Later fence
submissions observe the failure before obtaining a reusable fence. A submission
already racing failure may remain queued and is not certified complete.

| State | Disposition |
| --- | --- |
| Wait succeeds | Reset/recycle fence; publish confirmed tick; wake waiters |
| Wait fails or times out | No reset, reuse or tick advance; retain failed fence and queued suffix; publish failure |
| Target tick already confirmed | Prior history remains usable despite later failure |
| Target tick not confirmed | Throw failure, never return a completion receipt |
| Subsequent fallback submit observes failure | Return failure before taking a free fence |
| Owner destruction | Still requires external submission-source/waiter shutdown and device idle/device teardown |

The failure latch is not a resource retirement registry. It owns fence handles,
not all guest backing, alias freezes, staging resources or native submissions.
Those owners must still survive Unknown; this change does not prove that all
existing callers do so. Fence reset errors, timeline helper errors, shutdown
without the documented external idle contract, and failed queue-submission
ownership remain separate audit items.

## Executed local validation

```powershell
python tools/gpu_lifecycle_tests/run_native_fence_failure_tests.py --repeat 3 --output-dir .diagnostics/m4-native-fence-final
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/video_core/renderer_vulkan/vk_master_semaphore.cpp --source src/video_core/renderer_vulkan/vk_scheduler.cpp --output-dir .diagnostics/m4-native-fence-production
```

The test extracts actual complete MasterSemaphore Wait and WaitThread bodies.
Vulkan fence outcomes and the timeline-disabled facade are fixtures. Success,
device loss and timeout run three times. Two concurrent waiters must wake, failed
completion must retain its original fence plus queued suffix, and no failed tick
or fence may be recycled. Historical tick zero remains valid.

Both mutation controls hit exact runtime assertions: ignoring the wait result
produces `failed native fence reported completion`; recycling the Unknown fence
produces `failed native fence advanced tick or recycled resource`. No timeout or
compile failure is accepted as mutation evidence. These are controlled native
result simulations, not execution against a physical Vulkan device.

Both complete production units generated Windows AMD64 COFF objects without
diagnostics. Source hashes, object digests and machine signatures were checked
before cleanup. Submit's new failure precheck is compiled, not executed by the
actual-body worker fixture. The existing workflow includes the new runner for
future execution; no workflow was dispatched for this change.

## Remaining M4 boundary

Ordinary CPU/NCE/host-service producers still need complete close-and-join.
Raw aliases and guest physical backing need sustained ownership. Native
submission completion must be combined with visibility, retained backing and
Unknown resource isolation before the first legal Guest-backed admission.
Callback Completed grants none of those facts. Terminal GMMU quarantine remains
fail-closed; Android runtime incidence and any recovery policy are not assessed.
All THREE M4 phases remain unfinished and GuestBackingAdmission stays Unknown.

No full application link, Android build or native device test was run. Main is
unchanged. Only this Markdown conclusion and reproducible test sources remain;
this round's temporary executables, objects, generated fixtures, manifests and
logs are removed after inspection. Older diagnostics/dependency caches remain.
