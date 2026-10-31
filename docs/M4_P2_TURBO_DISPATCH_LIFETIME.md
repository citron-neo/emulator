# M4 P2: desktop Turbo dispatch lifetime

2026-10-23; synchronized baseline `664136bb763d5fe6dc25f9316cfafb7e88ba4f61`
(16 afternoon commits fast-forwarded from `b6d1f03b65`). PR375 and the fork
issue branch agreed at synchronization. Main remains `b6a2ce9488`.

## Proven defect and bounded repair

The native wrapper's Fence::Wait returns VkResult, with VK_SUCCESS equal to zero.
TurboMode::Run treated that value as bool: success entered timeout handling,
while timeout/device loss entered success accounting. It also reset the fence,
modified descriptors and rerecorded the command buffer on the next iteration
without proving completion of the previous dispatch. Submit's VkResult was ignored.
The afternoon destructor idle repair protected final disposal, but did not protect
this earlier running reuse boundary.

Before modifying production, the actual Run/destructor body fixture reproduced
five separate defects on the synchronized baseline: incorrect success accounting,
pending fence reset after timeout, false success when stopping during timeout,
waiting after rejected Submit, and returning normally after native wait failure.
All five exited with the exact witness code 91; a crash/timeout is not acceptance.

The desktop worker now tracks its single pending dispatch locally. Only confirmed
VK_SUCCESS clears it and permits the next reset/descriptor update/record/submit.
VK_TIMEOUT retains the same fence, command buffer, descriptors and resources;
the next enabled iteration waits that dispatch again. The existing adaptive wait,
timeout throttling, auto-disable and submission pacing remain in place. Elapsed
success time starts at the original submission, including intervening timeouts.
If Turbo pauses or auto-disables, the pending resources remain owned; stopping
the worker transfers final settlement to the existing destructor join/device-idle
boundary before CleanupResources. A timeout itself never authorizes reuse.

Unsuccessful Submit or non-timeout/non-success Wait results invoke the existing
no-unwind TerminalFailStop protocol with `turbo.submit` or `turbo.wait`, the exact
Vulkan result, and exit 86. No cleanup, speculative retry or owner destruction is
allowed after that uncertainty. This adds no queue or ownership registry.
Android's driver-control branch and ordinary GPU scheduling are unchanged.

## Deterministic acceptance

`run_turbo_dispatch_tests.py` extracts the complete, unmodified desktop Run and
destructor methods and links the actual TerminalFailStop implementation. Vulkan
resources/calls/results, performance hooks and submission pacing are fixtures.
The cancellation case uses a real jthread and promise rendezvous: the native wait
is entered, stop is requested, then the injected timeout returns. The worker must
exit with its dispatch still owned, and destructor native idle must precede cleanup.

Five paths run three times: two ordinary completions; two timeouts then two
completions with exactly two submissions; stop during pending wait; rejected
Submit; failed Wait. The last two require exact exit 86, diagnostic stage/result,
and no cleanup. Five precise controls also run three times: inverted success,
unconditional pending reset, timeout clearing pending, ignored Submit failure,
and ignored Wait failure. Each must produce its exact expected defect marker or
false-success Fail-Stop diagnostic; unrelated failure cannot pass a control.

The existing whole-owner suite passes five success and 23 failure paths three
times, plus its five controls. A real Windows x64 production Turbo object compiles
with pinned actual Vulkan/Boost headers and actual GLSL-generated SPIR-V header,
without diagnostic output. Source/object hashes and shader evidence are preserved
in [results](M4_P2_TURBO_DISPATCH_RESULTS.json).

```powershell
$env:PYTHONUTF8='1'
python tools/gpu_lifecycle_tests/run_turbo_dispatch_tests.py --baseline --revision 664136bb763d5fe6dc25f9316cfafb7e88ba4f61 --output-dir .diagnostics/m4-turbo-baseline
python tools/gpu_lifecycle_tests/run_turbo_dispatch_tests.py --repeat 3 --output-dir .diagnostics/m4-turbo-fixed
python tools/gpu_lifecycle_tests/run_terminal_disposal_tests.py --repeat 3 --output-dir .diagnostics/m4-turbo-terminal
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/video_core/renderer_vulkan/vk_turbo_mode.cpp --output-dir .diagnostics/m4-turbo-production
```

The suite is added to the existing integration workflow; no manual workflow is
dispatched. This round makes no full emulator link/game/runtime, actual driver
timeout/device-loss, Android/Linux or ordinary Guest-backed coherency claim.
Other worker exceptions/construction unwinding retain their existing contracts.
P2/M4 remain incomplete and GuestBackingAdmission stays Unknown. Next work should
return to the remaining producer/escaped-alias census and reversible closure
composition; this independent desktop device repair supplies no Guest admission.

Only this round's generated executables, objects and fixture sources are removed
after preserving results; prior result bundles and dependency caches are retained.
