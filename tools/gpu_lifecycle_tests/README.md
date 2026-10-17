# GPU lifecycle correctness: F4/F5

Scope: explicit GPU emulation-thread teardown and ownership of CPU sync-request
arguments/results. This does not implement M3B admission, physical coherency,
Vulkan submission acknowledgements, device-loss recovery or producer leases.

## Deterministic regressions first

Before changing production, the new extracted-function tests were run against
`1ce96ad728` and both failed with exit code 1:

```
FAIL scheduler/renderer teardown before worker exit
FAIL shutdown leaves queued callbacks executable
```

Reproduce that negative control without editing production:

```sh
python tools/gpu_lifecycle_tests/run_tests.py --cxx g++ --revision 1ce96ad728 --case lifecycle --case cancel --repeat 1
```

The old destructor is actually executed against a latch-paused worker and
destructor sentinels. The test observes invalid lifetime order without dereferencing
freed objects. The old cancellation test checks executable queued work, stopping
before a dangling stack access is necessary. These are production-function
fixtures, not a historical game crash or a driver reproduction.

## Implementation contracts

- `GPU::Impl::~Impl` closes sync requests, explicitly calls `ThreadManager::Stop`,
  closes/drains detached presentation callbacks, then calls renderer Shutdown.
  The control scheduler, channels, renderer/context and sync metadata remain alive
  throughout the GPU thread's stop/join. No members were reordered as the fix.
- `Stop` is an owning-thread teardown operation. It closes command admission under
  the producer mutex, requests cancellation outside that mutex, wakes blocked
  producers, joins, then destroys queued captures. It is idempotent when invoked
  serially. `NotifyShutdown` remains callable on the GPU failure thread and never
  self-joins; it does not replace the owning frontend's eventual Stop.
- A full bounded queue uses a cancellable producer wait. Queue fences are allocated
  only at actual enqueue, so competing waiters cannot reorder fence assignment.
  Consumers notify space waiters under the same mutex used to check/wait; Stop
  cannot strand a producer in the old non-cancellable EmplaceWait. This adds a
  short producer-mutex notification scope per popped command; no performance claim
  is made and no Vulkan Finish/device wait is added to ordinary command submission.
- `IsGpuThread` uses a trivial thread-local manager pointer. It does not read a
  jthread object concurrently with join or mistake a recycled host thread ID for
  the old worker. Nested GPU-thread callbacks retain the inline, non-self-wait path.
- Sync-request insertion checks shutdown under the queue mutex. Closing seals
  admission and prevents pending requests from starting, then moves and destroys
  queued payloads one at a time outside locks. No temporary container is allocated
  on this failure path (even an empty list can allocate a sentinel on MSVC).
  Already-started callbacks may finish. Their bodies and owned arguments remain
  alive until thread join. Cancellation does not advance the executed-request fence.
- CPU read flush callbacks capture their immutable area by value. Applet captures
  use request-owned result storage. The caller reads it only after a successful
  completion wait, and returns empty after early cancellation. The completion load
  acquires TickWork's release publication even if shutdown woke the waiter before
  TickWork reacquired the queue mutex.
- Presentation requests own moved layers/fences and a per-request countdown.
  Registered guest-syncpoint actions retain detached shared state, never Impl or
  caller stack references. Closing that state clears its renderer pointer under
  its mutex and waits for a presentation already using it. Late actions become
  inert. A newly bound renderer gets a fresh state; this does not promise live
  renderer replacement while the GPU worker is still using the old renderer.

There is still an important semantic boundary: an early shutdown return is not
successful GPU execution/visibility. Public legacy void wait/callback APIs retain
their cancellation semantics. A future M3B adapter needs explicit operation outcomes
and resource completion; this patch does not promote queue acknowledgement to that
contract. Device loss and a permanently blocked driver wait remain separate work.

## Run on Windows or Linux

Only Python 3 and a GCC-compatible C++23 compiler are needed. There are no emulator
build, Vulkan SDK, Boost, submodule, display, device or Android requirements.

```sh
python tools/gpu_lifecycle_tests/run_tests.py --cxx g++ --repeat 10
python tools/gpu_lifecycle_tests/run_thread_tests.py --cxx g++ --repeat 10
python tools/gpu_lifecycle_tests/run_renderer_tests.py --cxx g++ --repeat 10
python tools/gpu_lifecycle_tests/run_fence_tests.py --cxx g++ --repeat 10
# On supported Linux compiler/runtime installations:
python tools/gpu_lifecycle_tests/run_tests.py --cxx g++ --sanitize address
python tools/gpu_lifecycle_tests/run_thread_tests.py --cxx g++ --sanitize address
python tools/gpu_lifecycle_tests/run_renderer_tests.py --cxx g++ --sanitize address
python tools/gpu_lifecycle_tests/run_fence_tests.py --cxx g++ --sanitize address
python tools/gpu_lifecycle_tests/run_tests.py --cxx g++ --sanitize thread
python tools/gpu_lifecycle_tests/run_thread_tests.py --cxx g++ --sanitize thread
python tools/gpu_lifecycle_tests/run_renderer_tests.py --cxx g++ --sanitize thread
python tools/gpu_lifecycle_tests/run_fence_tests.py --cxx g++ --sanitize thread
```

All ordering uses condition-variable events and futures. Five-second waits and
process timeouts detect hangs; no sleep or elapsed-time threshold decides the
required interleaving. Zero-duration future checks only assert that a latch-blocked
join/drain has not already returned.

### Coverage and evidence limits

`run_tests.py` extracts current Impl destructor, request enqueue/wait/tick/shutdown,
CPU read, capture and composite methods, plus detached composite state. Core/System,
worker, renderer/Vulkan and syncpoints are fixtures. Cases cover:

1. Active worker joins before scheduler/renderer teardown.
2. Queued work is cancelled, admission closes, executed fence remains unchanged.
3. Cancelled payload destruction can reenter enqueue without locking deadlock.
4. Capture is paused inside renderer; shutdown returns the caller; callback resumes
   only after that caller's stack is gone, using owned result storage.
5. CPU read request is cancelled before callback invocation.
6. Guest-syncpoint actions outlive Impl without accessing its renderer.
7. An already-active presentation delays close until its lifetime ends.
8. Normal capture/read, multi-fence and zero-fence presentation still work.
9. With shutdown absent, OnCPURead and RequestComposite remain blocked both after
   enqueue and while their actual callback is paused. The fixture takes the real
   request mutex after a wait-entry signal and observes a return counter before
   releasing the callback, proving enqueue/start are not callback completion.

`run_thread_tests.py` compiles the actual ThreadManager class, complete production
RunThread/ThreadManager bodies, bounded MPSC/SPSC queue and stop-token wait.
Platform, System, engine/control scheduler and renderer are shimmed. It saturates
the actual 4096-entry queue behind an active callback, stops a producer paused
inside the real queue-space wait, verifies active join and cancellation, pauses
a suspended-channel retry, checks sync/async modes and nested callbacks, and rejects
pre-start/post-stop requests. Two test-only signals are inserted in copied source
immediately before the queue wait and after request_stop; the production wait and
cancellation logic are unmodified. No production instrumentation is added.

`run_renderer_tests.py` executes extracted Scheduler WorkerThread/WaitWorker/
DispatchWork/Shutdown and PresentManager PresentThread/WaitPresent/Shutdown with
real queues, mutexes and latch-paused work. It exercises an active and a still-
recorded callback, active/queued frames, and fencing cancellation. It additionally
executes the actual RasterizerVulkan/RendererVulkan Shutdown and destructor bodies
against collaborator spies to check owner order and release of shutdown_mutex
before joining callbacks. Actual TurboMode destructor and MasterSemaphore Shutdown
are run against latch-paused host helpers; device-idle and resource cleanup are
spies, not real Vulkan calls. Each case passes ten repetitions.
The Scheduler destructor's fallback is tested separately: during unwinding it
cancels/joins without dispatching a local chunk whose owners may already be dead.
The explicit renderer-owned Shutdown is the successful drain boundary.

`run_fence_tests.py` compiles and instantiates the **complete** production
FenceManager template (both class declarations and method bodies), with only its
Core/platform/cache surfaces shimmed. It proves active callback join, unstarted
payload release, post-shutdown publication rejection and repeated Shutdown safety.
The copied header contains one signal immediately after request_stop to establish
the interleaving, without changing the shutdown/worker logic.

Generated sources, source hashes and per-case logs are under
`.diagnostics/gpu-lifecycle/{windows,linux}`. The baseline control has its own
subdirectory. Neither suite executes Vulkan, native NCE or a real full System
shutdown; no full executable link or Android validation is claimed.

## Validation in this change

Windows MinGW GCC 13.2.0: all four lifecycle suites pass ten repetitions per case; the
baseline negative controls fail as expected. Existing GPU stop, CPU-write and
unmap/scatter regressions pass. M3A passes three repeated protocol runs after
updating its queue fixture: stopped admission now belongs to PushCommand, which
the legacy fixture previously did not model. The dedicated ThreadManager suite
executes the actual new admission/stop code rather than that fixture.

Linux execution and sanitizers are **not** claimed: this Windows host has no
installed WSL or container runtime, and its MinGW lacks ASan. The same dependency-free
runners and Linux sanitizer commands are ready for a Linux/GitHub workflow host.
No Android local build was attempted.

## Follow-up boundary audit: complete renderer shutdown

The first F4/F5 patch alone did **not** close the backend worker lifetimes. A
follow-up test ran the old RendererVulkan destructor and failed:

```
FAIL renderer destroys resources or waits idle before host workers are joined
```

The old device WaitIdle preceded presentation-worker join, so presentation could
submit again after WaitIdle. The old fencing thread survived GPU Impl destructor
body and could access GPU fields destroyed before the renderer member. Scheduler's
late automatic join did not protect renderer/cache captures. The on_submit callback
was also overwritten without first excluding its worker. These gaps were repaired
before merging the lifecycle change.

The terminal normal-shutdown order is now explicit:

```
GPU Impl: close sync requests -> GPU emulation Stop/join -> close CompositeState
  -> RendererVulkan::Shutdown
       -> RasterizerVulkan::Shutdown
            close render entries and release shutdown_mutex
            PipelineCache::DrainWorkers (compile, then serialize)
            Scheduler::Finish (device completion for existing render work)
            FenceManager::Shutdown (join active callback; cancel unstarted callbacks)
            final buffer/texture runtime Finish
       -> Scheduler::WaitWorker (including recorded Present enqueue callbacks)
       -> PresentManager::Shutdown (WaitPresent includes active frame; stop/join)
       -> Scheduler::Shutdown (drain and stop/join recording/submission worker)
       -> detach on_submit after its worker has stopped
       -> TurboMode destruction (join thread, desktop device idle, resource cleanup)
       -> renderer device WaitIdle after all host submission sources stop
       -> MasterSemaphore::Shutdown (join timeline-debug / fallback-fence helpers)
  -> later normal member destruction: imports, caches, frames, scheduler, device
```

Compilation tasks are drained while GPU::ShaderNotify still exists. Fencing is
joined while the derived fence manager, caches, GPU and Host1x are all alive,
avoiding base-destructor virtual calls from an active worker. Its plain-CV stop
predicate is changed under its guard, preventing a stop notification from being
lost between the worker's predicate check and sleep. Callback payload destruction
runs after join and outside that guard. Presentation is drained only after the
recording worker has published all queued enqueue closures; device idle occurs
after the final presentation submission. Completion helpers reference only their
own semaphore/fence state and the still-live device, and now join explicitly too.

All these are **terminal owner-thread** APIs: producers must already have stopped;
they are not concurrent live-renderer replacement or M3B admission APIs. A driver
wait which never returns or device-loss recovery is still outside this patch's
proof domain. No frame age is used as device completion evidence.

Normal synchronization is unchanged: non-preemptive OnCPURead returns after its
flush callback completes (that path already includes the staged download wait).
RequestComposite returns after its GPU callback finishes registering guest-fence
actions, or performs a zero-fence composite. With unresolved guest fences it has
**never** promised that presentation/hardware work is already complete; deferred
actions still present only when all required fences arrive. The new normal-wait
tests distinguish all three boundaries. Only shutdown permits early cancellation;
cancellation is not reported as successful execution internally.
