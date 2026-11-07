# Fence pool allocation failure retains the completion owner

Date: 2026-10-23 (Asia/Singapore). Baseline: `aa58136d59`.

After native wait and Reset succeeded, WaitThread pushed its local fence into
free_queue before publishing gpu_tick. A deque allocation failure escaped the
worker, destroying the local owner and leaving completion waiters stranded.
The new actual-body witness reproduced
`native fence recycling allocation escaped completion worker` on the old body.

Reset, pool insertion and successful tick publication now share one exception
boundary. std::bad_alloc becomes VK_ERROR_OUT_OF_HOST_MEMORY and enters the
existing terminal retention path. vk::Fence has a noexcept move; a deque storage
allocation failure leaves the local fence available for failed_wait. The original
handle and queued suffix remain owned, no reusable fence or tick is published,
all waiters wake with failure, and a prior terminal error cannot be overwritten.
This conservatively fails retirement bookkeeping even though the native wait
itself succeeded; it is not a rollback of native work or a new visibility proof.

```powershell
python tools/gpu_lifecycle_tests/run_native_fence_failure_tests.py --repeat 3 --output-dir .diagnostics/m4-retirement-final
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/video_core/renderer_vulkan/vk_master_semaphore.cpp --source src/video_core/renderer_vulkan/vk_scheduler.cpp --output-dir .diagnostics/m4-retirement-production
```

The suite executes full production Wait, WaitThread and SubmitQueueFence bodies
with controlled native results and allocation providers. Positive tests cover
native success/wait failures/reset failure/pool allocation failure, two waiters,
submission reservation failure/retry, failed-submission retention and older
completion races. All pass three repetitions. Seven exact runtime mutation
controls pass, including rethrowing the pool allocation exception to reproduce
the original witness. The prior six controls retain their exact assertions.

Two full production units generate Windows AMD64 COFF objects without diagnostics.
Source hashes, object digests and machine signatures are independently matched
before cleanup. These are simulated failure results and production object
compilation, not native Vulkan execution or a full application link.

The fence pool and failed_wait retain handles only. All backing/alias owners,
native visibility, complete producer close-and-join and durable global Unknown
resource isolation still need composition. Higher-level retirement and timeline
helper failure remain separate audit items. All THREE M4 phases are incomplete;
ordinary GuestBackingAdmission stays Unknown. See the
[roadmap snapshot](M4_ROADMAP_STATUS_2026_10_23.md).

This round retains Markdown conclusions and test source. Temporary executables,
objects, fixtures, manifests and logs are removed after inspection. No Android,
remote workflow, device run or main merge is claimed.
