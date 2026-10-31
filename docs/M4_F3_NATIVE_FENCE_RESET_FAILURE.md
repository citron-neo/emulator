# Native fence reset failure preserves completion ownership

Date: 2026-10-23 (Asia/Singapore). Baseline: `040ee8c66d`.

The previous fence-wait repair handled non-successful vkWaitForFences results.
An additional actual-body witness reproduced a successful wait followed by a
throwing fence Reset. The exception escaped WaitThread, which would terminate
its helper and strand completion waiters. The witness reported
`native fence reset exception escaped completion worker` before the repair.

Reset now runs inside a vk::Exception boundary before fence reuse. On failure,
its original VkResult enters the existing terminal completion-failure path:
retain the failed fence and queued suffix, publish the failure under the waiter
predicate mutex, wake all waiters, and stop the completion worker. No completion
tick or free fence is published. Already confirmed history remains usable.
The native wait may have completed, but fence reuse failed; this conservative
path does not roll back effects or infer new visibility/backing authority.

Local validation commands:

```powershell
python tools/gpu_lifecycle_tests/run_native_fence_failure_tests.py --repeat 3 --output-dir .diagnostics/m4-reset-final
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/video_core/renderer_vulkan/vk_master_semaphore.cpp --source src/video_core/renderer_vulkan/vk_scheduler.cpp --output-dir .diagnostics/m4-reset-production
```

Actual complete Wait and WaitThread bodies cover success, native wait device loss,
native wait timeout and Reset device loss, with two concurrent waiters. All pass
three repetitions. Three precise runtime controls reject ignored wait results,
Unknown fence recycling and escaping reset exceptions. The latter rethrows from
the catch and must reproduce the exact witness failure. Controlled Vulkan results
are fixtures, not a physical-device test. Both complete production units generate
Windows AMD64 COFF objects without diagnostics; final source/object hashes and
machine signatures are verified before cleanup.

## Remaining submission/retirement audit

SubmitQueueFence currently calls the native Submit before wait_queue.emplace.
If that host allocation throws after native acceptance, the local fence can be
destroyed without a retained completion record. A non-successful native Submit
also currently returns with its local fence; this is not a complete submission
Unknown ownership policy. The next transaction must reserve a durable record
before acceptance and retain its handle on uncertainty. It must account for the
completion worker, later submits and terminal teardown without waiting on an
unsignalled fence from a rejected submission. This change does not repair those
paths. Allocation failure while returning fences to free_queue is also open.

Producer close-and-join, escaped aliases, guest backing ownership, native
visibility and a durable Unknown resource registry still require composition.
All THREE M4 phases remain unfinished; ordinary GuestBackingAdmission stays
Unknown. Terminal GMMU quarantine recovery remains deferred to Android runtime
evidence. No Android build, native device execution, remote workflow, full app
link or main merge is claimed. This round retains this Markdown conclusion and
test source; temporary binaries, objects, generated fixtures, manifests and logs
are removed after inspection. Earlier diagnostics and dependency caches remain.
