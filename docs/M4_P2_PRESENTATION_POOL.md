# M4 P2: bounded Presentation Pool audit and scope freeze

Date: 2026-10-25. Historical source: `c3fa0f0d4bc791234f64a2d2c84f3dd78d6ede8e`.

## Pool contract

PresentManager owns `frames`, command pool and each Frame's image/view/framebuffer,
render_ready semaphore and present_done fence. SetImageCount runs only during
construction: min(initial swapchain image count, 7). Only construction resizes
frames and publishes its addresses to free_queue. RecreateSwapchain changes the
swapchain's image arrays and pipeline limit, not this pool. Frame count therefore
need not equal the recreated swapchain count. Dynamically resizing frames would
invalidate pointers in the recording callback, present_queue and active worker;
this change deliberately keeps the pool fixed.

Under the existing serialized render-owner contract, free_queue contains unique
pool addresses and free_queue.size() + frames_in_flight == frames.size().
GetRenderFrame removes one under free_mutex and increments the count before
checked fence wait/reset. ReleaseRenderFrame reverses that bookkeeping under
the same mutex. Queue return is host-copy completion; native completion is still
required at the next borrow. The source Frame and acquired swapchain image use
different indices and different synchronization objects.

PresentThread takes swapchain_mutex before dropping queue_mutex, then copies
before release. Thus WaitPresent's empty-queue wait followed by swapchain_mutex
also includes the last active copy. Owning GPU teardown first stops emulation and
capture callbacks. RendererVulkan drains recording before PresentManager::Shutdown, joins presentation,
joins scheduler, then checks device idle before retained native-source disposal.
PresentManager alone is not an ingress-close or native-idle capability. External
producers must already have stopped before invoking this teardown contract.

## Reproduced failures and repair

Three historical production-body witnesses reproduce three times each before
the patch: failed acquire uses a stale index after shrink; failed device idle
still destroys a pending swapchain; surface recovery destroys a parent surface
before its old child swapchain. A separate negative control proves that replay
after copy starts can consume render_ready twice.

Acquire now returns immediately for OUT_OF_DATE and never touches image ticks
without successful acquisition. It bounds successful indices and refuses other
unexpected statuses. Destroy clears image/tick/semaphore arrays and both indices.
Create checks device idle before old resource destruction, publishes the new
surface metadata only afterward, and requires complete new arrays. Zero surface
extent returns false without replacing the old publication. Every caller checks
that receipt; an already borrowed Frame has no cancellation receipt, so inability
to recreate it is terminal rather than recycled as completed.

Surface recovery builds a candidate surface, retires/replaces the old swapchain
while its parent is alive, then replaces the surface owner. Surface loss can retry
only before command recording begins. After that boundary, any exception is
terminal with no stack unwinding of potentially referenced owners. Destructive
creation failures likewise terminate; no rollback or partial-success claim is
made. This uses the existing terminal failure policy, not a recoverable device
loss or minimized-window protocol. Normal scheduling and pool size are unchanged.

These boundaries follow Vulkan's acquisition result contract, checked
[device idle](https://docs.vulkan.org/refpages/latest/refpages/source/vkDeviceWaitIdle.html),
[surface child lifetime](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroySurfaceKHR.html)
and [presentation wait effects](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html).
See also [WSI acquisition](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html).

## Deterministic evidence and limits

Candidate tests passed before production edits. Candidate and fixed suites each
pass eight regular cases, seven terminal cases and five exact negative controls,
each three repetitions. Cases cover synchronous/asynchronous shrink and growth,
stable Frame addresses, pending fence reuse, failed acquisition with valid/stale
index, pre-copy surface loss, active rebuild, shutdown waiting for the active
worker, failed idle, post-copy surface loss, unexpected acquire status/index,
zero extent and native swapchain/semaphore factory failures. Controls deliberately
use a failed-acquire index, ignore idle, release the parent first, replay a used
Frame and recycle an active Frame. Each must reach its own exit-91 assertion;
terminal cases require exit 86, the exact stage and no owner/atexit release.

Extracted bodies are Swapchain Create/Acquire/Present/Destroy and PresentManager
Get/Present/Release/PresentThread/WaitPresent/Shutdown/SetImageCount/RecreateSwapchain/
CopyToSwapchain. CopyToSwapchainImpl preserves actual acquisition and submit/present
code; its command recording middle is an observer. Factories, driver queue/native
completion and constructor allocation setup are fixtures. Real std::jthread and
semaphore gates orchestrate worker interleavings. A WaitPresent observer checks
contention on the actual swapchain mutex before allowing the paused copy to
continue; shutdown must not already have returned. Pending-fence reuse requires
an outstanding native-use probe before entering its wait. There is no real Vulkan device,
pixel, game, KProcess backing or global producer-closure acceptance here.

Four real Windows AMD64 production units compile with real dependencies:
Swapchain, PresentManager, RendererVulkan and BlitScreen. No application link,
MSVC, Linux/Android/device run or current-head CI acceptance is claimed.
[Results and exact inputs](M4_P2_PRESENTATION_POOL_RESULTS.json) record regressions,
hashes and cleanup: 111 generated files, including 50 executables, were removed
after exact bounds/hash checks. Ignored full logs remain local; committed manifests travel.

## Freeze and unresolved composition

This completes the bounded pool slice, not all P2. From this checkpoint, new local
repairs require a deterministic reproduction of a critical owner failure. A new
destructor suspicion, individual green test or broader error-path inventory is
not sufficient to expand scope. Work returns to the
[producer coverage and four-step integration ledger](M4_PRODUCER_COVERAGE_AND_ADMISSION.md).

Repeated pre-copy surface-loss/OUT_OF_DATE retries have no bounded progress proof;
WaitPresent/join can therefore remain pending under persistent driver failure.
Cross-thread reads of swapchain count/format, concurrent external render owners
and scheduler tick/wait access during recreation have not been proven by this
fixture. They remain explicit synchronization/closure obligations, not an assertion
that shutdown always progresses. Failure of such obligations must refuse guest
admission. No speculative follow-on local fix is included. HostMemory remains
high risk; M4 incomplete, F2/F3 open, GuestBackingAdmission Unknown.
