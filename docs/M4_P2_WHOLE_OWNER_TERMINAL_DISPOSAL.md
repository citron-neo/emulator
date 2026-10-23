# M4 P2: whole-owner terminal disposal

2026-10-23; baseline `27a56f4006`. This is the narrow terminal protocol approved
for PR375: safe native disposal or process-level Fail-Stop. It adds no owner
queue, quarantine manager, scheduler redesign or ordinary execution-entry work.
P2 remains incomplete; ordinary GuestBackingAdmission remains Unknown.

## Owning entry and normal path

The previous explicit checked-retirement guard did not prevent later Renderer,
Scheduler or allocator member destruction. Further, System called CloseServices
and ShutdownCores before GPU destruction, so a late renderer-only failure guard
could not protect all still-needed service/Process/PageTable owners.

`GPU::Shutdown` now exposes the existing terminal GPU drain as an idempotent
owner-thread operation. `System::Impl::ShutdownMainProcess` invokes it immediately
after execution suspension, before CloseServices, ShutdownCores, service resets,
GPU reset, Host1x reset, Kernel shutdown or backing-page reset. System's destructor
also enters this owning shutdown if a GPU remains: Impl's reverse member order
would otherwise destroy backing before GPU. These are small entry/order changes;
System ownership and scheduler behavior are not redesigned.

Normal order:

1. Close existing GPU publication/binding/composite/sync ingress; request execution
   and service-stop signals; suspend emulated execution.
2. GPU terminal entry stops/joins GPUThread and direct invalidations, joins owner
   publications and rasterizer bindings, and closes Composite requests while the
   entire System-owned graph remains alive.
3. Renderer closes rasterizer/pin producers, drains pipelines/fence callbacks,
   recording and presentation, joins the scheduler worker, detaches on-submit and
   stops the separate Turbo worker/device. Turbo now checks its own device-idle
   return before freeing its allocations.
4. Under the submission mutex, require real main-device idle success and accepted
   all-owner checked disposal, then retire the completion helpers. Worker joins
   stay outside the submission lock.
5. Only after successful return may ordinary System/GPU/renderer/member teardown
   proceed. GPU and Renderer destructor fallbacks repeat no native work after a
   completed drain. Reentrant terminal entry is a fatal protocol error.

The closures here are existing **terminal native GPU** ingress/worker boundaries.
This does not establish a reversible whole-guest admission gate or resolve all
ordinary NCE/host-service/raw-alias coverage. A terminal idle/disposal result is
not evidence of coherent guest content.

## Failure protocol

| Failure | Diagnostic stage/result | Action before owning member destruction |
| --- | --- | --- |
| System closure/execution-stop exception | `system.producer_close` / `system.execution_stop`; Vulkan unavailable | Fail-Stop |
| GPU worker/publication/binding/composite join exception | `gpu.worker_join` / `gpu.owner_join`; Vulkan unavailable | Fail-Stop |
| Renderer drain/join exception | Component phase; exact Vulkan result for `vk::Exception`, unavailable for an untyped host exception | Fail-Stop |
| Turbo native idle unsuccessful | `turbo.native_idle`; exact Vulkan return | Fail-Stop before Turbo cleanup |
| Main device idle unsuccessful | `renderer.native_idle`; exact Vulkan return | Fail-Stop before checked disposal or parent teardown |
| Checked host ownership unsettled | `renderer.checked_disposal`; last successful native-idle result `0` plus `unsettled_host_ownership` reason | Fail-Stop |
| Reentrant terminal entry | `gpu.entry` / `renderer.entry`; Vulkan unavailable | Fail-Stop |

`Common::TerminalFailStop` formats one small diagnostic in a fixed stack buffer,
writes directly to stderr (also debugger output on Windows), and calls
`std::_Exit(86)`. No C++ unwind, atexit handlers, static destruction, GPU worker
drain, heap allocation or global owner registration is used by that function.
Vulkan result unavailable is written explicitly rather than fabricated as success.
Direct output is best effort if stderr is unavailable; it does not call the
ordinary logger or try to flush backend locks.

This deliberately differs from the existing assertion path's logger flush and
Crash()/DebugBreak: a debugger can resume a breakpoint, and the generic Crash
fallback uses normal exit. The approved failure policy requires an unconditional
no-unwind process boundary. Exit code 86 and the `CITRON TERMINAL FAIL-STOP`
stage/reason/result record identify this outcome; no crash dump is claimed.

No uncertain return permits fallback destruction, owner requeue, recovery or a
new game session. Successful independent Turbo disposal may precede a later
main-device failure; Turbo owns a separate device and releases only after its own
successful idle. Main-device/renderer/GPU/System owners remain protected.

## Verification

`run_terminal_disposal_tests.py` extracts actual System owning shutdown/destructor,
GPU shutdown/destructor/forwarding, Renderer shutdown/destructor and Turbo
destructor, and links the actual Fail-Stop implementation. The only System
platform adaptation replaces its final OS backing-page reset with a fixture
marker. Components/drivers/workers and resource providers are fixtures with
explicit protected-owner destruction markers.

Every test is a subprocess, including normal controls. Five success paths and
23 failure paths each run three times. This covers explicit System shutdown,
System/GPU/Renderer destructor fallbacks, idempotency, reentry, typed Vulkan
exceptions, host exceptions, native result `-4`, unknown result `-13`, checked
refusal and independent Turbo failure. Failure acceptance requires exact exit
86, exact stage/reason/result and absence of protected destruction, service or
Process/PageTable disposal, atexit and static-destruction markers. A crash, timeout
or unrelated nonzero exit cannot count as success.

Five precise mutation controls must expose forbidden lifetime transitions:
ignoring idle failure, ignoring checked refusal, closing services before GPU
drain, omitting System's owning destructor fallback, and replacing `_Exit` with
ordinary `exit`. All five are detected.

Additional checks:

- Existing GPU lifecycle: ten cases, three repetitions, plus its cancelled-read
  negative control.
- Existing renderer/worker teardown: nine cases, three repetitions; failure cases
  now require the exact Fail-Stop subprocess result.
- GPU owner publication and NV mapping compatibility: three repetitions, with
  selected close/join mutation controls. Their Vulkan/allocator providers remain
  fixtures. The publication fixture now supplies the existing dispatch/retirement
  dependencies required by the previously changed actual Module::Open body.
- Five real Windows x64 production COFF objects: terminal_fail_stop, core, gpu,
  renderer_vulkan and vk_turbo_mode. Turbo's actual pinned GLSL is compiled by
  glslang to its real generated header; no shader stub or Android build is used.
- `git diff --check`. No full emulator link/runtime, remote workflow, real driver
  loss or ordinary guest-backed admission is claimed by this round.

Reproduction:

```powershell
python tools/gpu_lifecycle_tests/run_terminal_disposal_tests.py --repeat 3 --output-dir .diagnostics/m4-whole-owner-protocol
python tools/gpu_lifecycle_tests/run_tests.py --repeat 3 --output-dir .diagnostics/m4-whole-owner-gpu
python tools/gpu_lifecycle_tests/run_renderer_tests.py --repeat 3 --output-dir .diagnostics/m4-whole-owner-renderer
python tools/gpu_lifecycle_tests/run_owner_publication_tests.py --repeat 3 --output-dir .diagnostics/m4-whole-owner-publication --control-prefix missing --control-prefix external-missing
python tools/gpu_lifecycle_tests/run_address_space_mapping_tests.py --repeat 3 --output-dir .diagnostics/m4-whole-owner-mapping --control-prefix missing-owner-join
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/common/terminal_fail_stop.cpp --source src/core/core.cpp --source src/video_core/gpu.cpp --source src/video_core/renderer_vulkan/renderer_vulkan.cpp --source src/video_core/renderer_vulkan/vk_turbo_mode.cpp --output-dir .diagnostics/m4-whole-owner-production
```

## Deliberate limits

This closes the owning terminal failure/disposal transition for the supported
System shutdown entry, with process termination as its only uncertain outcome.
It does not introduce in-process terminal recovery, a retained System epoch, a
driver-hang watchdog, or coverage of callers that violate the existing facade
lifetime/owner-thread contract. Existing joins and vkDeviceWaitIdle can still
block: no portable interruption of a stuck driver is claimed. Fault injection
proves the no-unwind protocol in child processes, not real driver-loss recovery.

Construction unwinding and asynchronous worker-internal failures retain their
existing contracts; this change handles detected owner-thread terminal failures
without redesigning worker scheduling. Ordinary M4 producer exclusion, raw alias
inventory/ownership and the first real Guest-backed admission remain independent
acceptance work. The previous RTX 4060 primitive witness is historical native
evidence; it is not rerun or promoted to full emulator acceptance here.

## Preserved production compilation evidence

AMD64 COFF objects; no diagnostics. Temporary objects and generated files are removed after recording this table.

| Unit | Bytes | Object SHA256 |
| --- | ---: | --- |
| terminal_fail_stop | 2357 | `0177e214fbc93b87321acd63f72cef16942209d95626fad6499b5c0549b670cb` |
| core | 177757 | `9897f01bec2044f671781a209637a95def0cc91268544d09166bd9644dffb6c0` |
| gpu | 145100 | `301c5fc4592e48d9ccde9953de81390ff0418bed450f175d3b3444bd9c7c6659` |
| renderer_vulkan | 102988 | `e68910508885f880622887d8d4f97a7d7377d2d30fbea4ba000ac80ad2d27ecd` |
| vk_turbo_mode | 46738 | `dae8917484b15f84d870a6b2c8c2fd5e2f094fa252affbade687e794e7d2b852` |

Turbo shader source SHA256: `f977b1154c1f2da930ae793e69573959c55a3352505294c52a956e8e169e0196`.
Generated SPIR-V header SHA256: `9012e2717898792636143f312817749796e0cedb6b26c22b256ef6f51d897b97`.
Shader compiler: glslang 16.2.0, target spirv1.3.

Publication mutation controls actually executed: `missing-close`, `missing-join`, `external-missing-close`, `external-missing-join`.
