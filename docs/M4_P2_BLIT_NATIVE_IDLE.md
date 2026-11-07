# M4 P2: framebuffer retirement audit and checked blit/presentation idle

2026-10-23 (Asia/Taipei). Baseline `73a0461d43e153371cebe50ddbb66d694cbe9f81`;
local/fork/PR375 heads agreed before work. Branch `codex/issue-372-fixes`;
main `b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76` is unchanged. This repairs
one Vulkan resource-recreation completion boundary. It does not certify guest
backing retirement or complete M4 P2.

Follow-up: [deferred frame/action-owner retirement](M4_P2_SYNCPOINT_OWNER_RETIREMENT.md)
establishes lock-safe capture release before connecting a source-owner packet.
It preserves the native/source lifetime limits described here.

## From guest buffer owners to native presentation

| Boundary | Actual source fact | Remaining distinction |
| --- | --- | --- |
| Uncached slot's final GraphicBuffer reference | Prepared retirement unpins/frees under a fresh admitted CPU dispatch, or retains on refusal | CPU dispatch admission supplies no native completion identity |
| NvMap::UnpinHandle | Final pin is put on the retained unmap queue before its count drops | Pin count dropping is not necessarily immediate physical unmap/free |
| NvMap::FreeHandle | Decrements one internal/guest duplicate; returned FreeInfo can have can_unlock=false while other owners remain | has_value is duplicate-retirement progress, not permission to release every guest allocation |
| NvMap::UnmapHandle | SynchronizeGPUThread precedes mapping removal in the powered asynchronous path; GMMU unmap failures retain owners | GPU emulation-thread synchronization is not native timeline/presentation completion |
| HardwareComposer cache | Cached BufferItem independently retains its GraphicBuffer; RemoveLayerLocked erases it even when the old display/layer is gone | Some buffers were never cached; one retained object is not a whole-pool lease |
| GPU::RequestComposite | GPU callback registers guest-fence actions; WaitForSyncOperation waits that registration callback | Guest-fence presentation may occur later; FramebufferConfig carries addresses, not the GraphicBuffer's prepared owner |
| Vulkan raw Layer | UpdateRawImage synchronously reads/unswizzles device memory into its own upload buffer and flushes it; recorded native copy uses that buffer | Guest source-read lifetime and later native upload/image lifetime are different |
| Accelerated Layer | Uses rasterizer-provided texture image/view, with scheduler resource ticks | A texture cache hit alone is not proof of guest backing/mapping-use completion |
| Presentation | Present records a host closure that enqueues a frame; present worker submits copy/blit and calls swapchain Present | Rendering tick, recording callback return, presentation-thread return and device idle are separate events |

The shared Module/Process/backing retirement record and cached framebuffer keep
their own owners alive. Neither reference counting nor FreeHandle's optional
result proves that all guest-fence actions, native accesses or mapping aliases
are settled. This audit does not change unpin/free or drop any uncertain record.

## Concrete defect: recreation's WaitIdle could be a false boundary

BlitScreen::DrawToFrame calls WaitIdle before SetWindowAdaptPass clears old layers
and before recreating a presentation frame. CreateFramebuffer similarly waits
before changing the adaptation pass. These are normal-runtime resource changes,
separate from RendererVulkan::Shutdown's existing checked terminal sequence.

Previously BlitScreen::WaitIdle called WaitPresent, then scheduler.Finish, then
ignored device.WaitIdle's VkResult. Present's host closure could still be in the
scheduler chunk when the first WaitPresent observed an empty queue. Finish then
published that closure; rendering completion need not imply that the present
worker has submitted its final copy. Device idle could consequently be observed
before that submission. Independently, a non-success device idle result allowed
the caller to continue clearing/recreating resources.

Before production edits, actual-method extraction reproduced two precise exit-91
witnesses: `idle preceded final recording or presentation work`, and
`native idle failure authorized resource release`. The latter uses a disposal
marker corresponding to the audited caller boundary, not a real Vulkan handle
destruction. Revision replay does not switch or modify the checkout.

## Repair

BlitScreen::WaitIdle now runs scheduler.Finish, scheduler.WaitWorker,
present_manager.WaitPresent, and finally a checked logical-device WaitIdle under
scheduler.submit_mutex. Finish submits the current rendering work and waits its
tick. WaitWorker dispatches remaining host closures and waits execution before
WaitPresent checks the queue and its active swapchain operation. Only after both
host drains is the submission mutex held across native idle, so worker submission
can progress and cannot race the checked idle call.

No worker is stopped or permanently closed during ordinary recreation; normal
rendering resumes after successful idle. Existing caller/Composite serialization
is the owning boundary here, not a new whole-device admission protocol. Arbitrary
independent producers are not revoked by the submission mutex. Queue/worker/native
progress remain conditional on the real backend returning; no bounded driver
recovery or timeout is introduced.

Every non-VK_SUCCESS result invokes the existing TerminalFailStop before caller
resource clearing. Vulkan/host exceptions are classified at blit.finish,
blit.recording_drain, blit.presentation_drain or blit.native_idle. This deliberately
terminates without C++ owner unwinding/atexit, following the existing terminal
failure contract instead of proceeding with unproved resource retirement. It is
not device-loss recovery. Normal RequestComposite/OnCPURead synchronous semantics,
guest buffer retirement and renderer shutdown order are unchanged.

## Deterministic Windows evidence

`run_blit_idle_tests.py` extracts actual BlitScreen::WaitIdle and
PresentManager::Present/WaitPresent/PresentThread, links actual TerminalFailStop,
and checks the two production recreation call sites. Real recording and present
threads, stop tokens, queue/swapchain mutexes and gates select the interleaving.
The actual WaitPresent body gets a fixture observation/fault probe at entry.

Scheduler Finish/timeline/recording and native device results are fixtures, as
are frame copy/submission/resource-disposal markers. A recording-tail gate models
host work that remains after rendering completion; a present-copy gate holds
the real presentation worker before its fixture native submission. This proves
the selected protocol/order and failure refusal, not actual Vulkan execution,
vkDeviceWaitIdle behavior on a device, guest Memory/GetPointerImpl, KProcess or
physical backing exclusion. It does not execute DrawToFrame's complete caller
body or physically destroy real cached/native resources.

Four success paths (late asynchronous Present closure, empty async queue,
synchronous Present, repeated healthy idle) and eleven failure paths each pass
three repetitions. Failures include -4/-1 and a synthetic positive non-success
native result, plus Vulkan and host exceptions at each of four stages. Each
requires exact exit 86, stage/reason/result, and no disposal/destructor/atexit
marker. Six controls each pass three repetitions: omit recording/presentation
drain, restore old drain order, ignore native result, hold submission mutex over
drains, omit the idle submission mutex. Each requires its particular exit-91
witness; crashes and timeouts are not accepted.

Regressions: actual GPU request suite's ten paths run three times, including
normal OnCPURead/RequestComposite waiting, deferred/active presentation and
cancellation, plus its cancelled-read coverage control. Whole-owner terminal
disposal five success/23 failure paths run three times and five controls once.
Abandoned-slot suite twelve paths/ten controls runs three times.

Six actual Windows AMD64 COFF objects compile with GCC 13.2, pinned real headers
and zero diagnostics: vk_blit_screen, vk_present_manager, Vulkan present Layer,
renderer_vulkan, nvmap and nvdrv. See [result bundle and hashes](M4_P2_BLIT_IDLE_RESULTS.json).
No full emulator link, game/native driver, Linux/Android or current-head CI
acceptance is claimed. Existing workflow gains the suite/log artifacts; no manual
run is dispatched.

```powershell
$env:PYTHONUTF8='1'
$env:CXX='C:/Strawberry/c/bin/g++.exe'
python tools/gpu_lifecycle_tests/run_blit_idle_tests.py --baseline --revision 73a0461d43 --output-dir .diagnostics/m4-blit-baseline
python tools/gpu_lifecycle_tests/run_blit_idle_tests.py --repeat 3 --output-dir .diagnostics/m4-blit-fixed
python tools/gpu_lifecycle_tests/run_tests.py --repeat 3 --output-dir .diagnostics/m4-blit-requests
python tools/gpu_lifecycle_tests/run_terminal_disposal_tests.py --repeat 3 --output-dir .diagnostics/m4-blit-terminal
python tools/physical_coherency_tests/run_abandoned_slot_tests.py --repeat 3 --output-dir .diagnostics/m4-blit-slots
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/video_core/renderer_vulkan/vk_blit_screen.cpp --source src/video_core/renderer_vulkan/vk_present_manager.cpp --source src/video_core/renderer_vulkan/present/layer.cpp --source src/video_core/renderer_vulkan/renderer_vulkan.cpp --source src/core/hle/service/nvdrv/core/nvmap.cpp --source src/core/hle/service/nvdrv/nvdrv.cpp --output-dir .diagnostics/m4-blit-production
```

## Still open / next handoff

This idle check is reached for Vulkan resource recreation, not every VI slot's
final unpin/free. Do not call it native retirement authority for those slots.
The next ownership gap to specify is a per-frame handoff spanning deferred guest
fence registration, the actual guest source read, mapping/session ownership and
the relevant native use. Framebuffer addresses and the registration callback's
completion cannot supply that bundle. Native upload/image/presentation resources
then need their own checked completion/reuse boundary.

Do not globally wait-idle on every GraphicBuffer destructor or use a cached tick
to release the whole pool. Preserve uncertain owners and determine whether the
minimum required protocol is an immutable per-frame source owner, a deferred
action cancellation/drain boundary, or both. Terminal VI cleanup still precedes
full GPU/native shutdown. F2/F3 stay open, all M4 phases incomplete and
GuestBackingAdmission Unknown.

Current-run temporary exe/object/generated sources are cleaned after preserving
JSON/logs and verifying input/COFF hashes. The committed results and
`.diagnostics/m4-blit-results` retain evidence; main is not merged.
