# M4 P2: per-frame source owner across deferred guest fences and CPU reads

2026-10-24 (Asia/Taipei). Baseline `4e765475888fcc6afa8d1ba2d85193ecd80b33d4`;
local/fork/PR375 heads agreed before work. Branch `codex/issue-372-fixes`;
main remains `b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`. This connects the
source-object handoff requested after the syncpoint capture-retirement repair.
It is not mapping-use exclusion, native completion, or completion of M4 P2.

Follow-up: [source translation versus mapping use](M4_P2_SOURCE_ADDRESS_TRANSLATION.md)
now synchronizes nvmap address snapshots with free/unmap and refuses zero
translations. The sustained epoch/read boundary remains open.

## Defect and packet

HardwareComposer's BufferItem owned a GraphicBuffer, but HwcLayer and the
nvdisp-produced FramebufferConfig transferred only numeric descriptor/address
fields. GPU::RequestComposite retained that configuration until guest fences
fired, without retaining the GraphicBuffer's prepared retirement owner. A final
layer/slot/cache release could consequently end source-object lifetime before
the deferred renderer callback. The actual-body baseline reproduced precise
exit 91: `deferred request lost original source owner`, before production edits.

GraphicBufferSource now contains a const shared GraphicBuffer owner and a const
NvGraphicBuffer descriptor snapshot. HardwareComposer creates an immutable shared
packet for each visible composition layer, builds descriptor fields from that
snapshot, and attaches the packet to HwcLayer. Nvdisp forwards the same packet in
FramebufferConfig after checking a nonempty owner and matching descriptor handle.
Missing/mismatched packets are refused before their address lookup and before
request publication. Earlier valid layers in the batch may already have had
address lookups; this is not an all-or-nothing mapping transaction.
The existing void composition API does not turn that refusal into a completion
or retirement acknowledgement. The only production builder/caller chain found
is HardwareComposer -> nvdisp -> GPU; the public GPU facade by itself is not a
new validated capability for arbitrary fabricated configurations.

For positive-ID buffers, retaining the existing GraphicBuffer also retains its
prepared Module/handle/Process/backing retirement graph. Empty descriptors retain
their existing wrapper behavior and do not gain physical backing authority.
The snapshot freezes copied descriptor values against later wrapper aliases;
it does not freeze content, GMMU/HostMemory mappings, guest handles or epochs.
No new duplicate/pin is minted per frame: the packet shares the existing wrapper.
Packet allocation can fail after a framebuffer acquire; existing composition
exception/quarantine policy still applies. It is not a new creation rollback.

## Request, registration failure and shutdown protocol

The GPU's shared Request vector owns all layer packets. Guest actions share that
Request, so an earlier fence's retirement cannot release the source while later
fences still own the request. Normal WaitForSyncOperation still acknowledges
the registration callback; it does not wait for later guest-fence presentation.
For ready/no-fence requests the existing immediate source callback still runs.

Request gains a cancelled bit guarded by CompositeState::mutex. A registration
exception marks the request cancelled and propagates. Already-published prefix
actions keep their source packets but refuse subsequent presentation; they are
not guessed away or deregistered via unproved IDs. A source callback exception
also marks cancelled before propagating, preventing a retained failing action
from repeating partial read/present effects on a later syncpoint increment.
The existing syncpoint policy retains the throwing action until it is removed
or subsequently consumed; no speculative rollback or cleanup replay is added.

Failure is not synonymous with no effects. If all fences are already ready, the
last registration can invoke the source callback synchronously before a failure
reported after that registration's effect. Cancellation cannot undo that read
or any native work it may have submitted. Tests require that observed prefix
and forbid replay; no Success/no-effect authority is fabricated. Before-effect
and after-effect registration faults are separate fixture hooks. Actual
SyncpointManager insertion allocation failure ordinarily occurs before that
insert publishes; the after-effect hook deliberately exercises the stronger
partial-side-effect contract, not a claim about a specific STL allocator trace.

CloseCompositeRequests already serializes with active renderer Composite through
the state mutex. An active source read keeps Request/packet alive until callback
return; close waits for that CPU access and then detaches the renderer. A pending
action after close does not read the source. Pending actions can retain their
packet until fence firing, cancellation or syncpoint-manager destruction. This
is deliberate retention; shutdown does not pretend to have completed guest
fences or native work. Manager/provider/System lifetimes remain owning preconditions.

The preceding syncpoint fix releases completed/cancelled action captures outside
its guard. The new tests exercise retirement re-entering real guest registration.
No source owner is explicitly cleared under the state lock. Other resource
destruction/lock dependencies are not certified as a whole-System lease.

## Actual source read versus native use

Vulkan's raw Layer::UpdateRawImage reads/unswizzles device memory synchronously
into its own upload buffer, flushes that buffer, then records native copy work
against the upload/native image. The packet survives the enclosing synchronous
renderer Composite call. Accelerated texture/image lifetime, scheduler ticks,
recording work, presentation and native completion remain separate owners and
boundaries. No packet is claimed to pin the entire guest mapping or replace
those native resource lifetimes.

In particular, final packet release after a successful CPU callback is not
native completion. Guest force-unmap/handle/session transitions can still bypass
what an object reference alone proves. Do not use this packet to authorize
ordinary guest-backed upload/coherency or final physical pool retirement.

## Deterministic Windows evidence

`run_frame_source_tests.py` extracts actual HardwareComposer compose/cache/remove,
nvdisp conversion, GPU RequestComposite/CloseCompositeRequests and GraphicBuffer
construction/destruction, uses the actual descriptor/packet headers, and links
the actual SyncpointManager implementation with its real mutexes and atomics.
Historical headers are copied into an isolated diagnostic include tree for
baseline replay, without changing the checkout.

Acquisition/display layouts, the small-vector/map storage types, Module retirement
preparation/backing tokens and nvmap address lookup are fixtures. GPU registration
dispatch is immediate in the fixture; separate existing request tests cover the
actual sync wait/queue bodies. The renderer borrows a real byte-array pointer
without an extra owning reference across a deterministic gate, checks its source
owner before access, and copies bytes into a separate upload stand-in. It is not
actual DeviceMemory/Memory/GetPointerImpl, Vulkan unswizzle/upload or native driver
execution. CompositeState's mutex wraps a real std::mutex with a current-thread
observer, and Close has an observation/assertion probe. No timed-out or crashed
fixture counts as a defect witness. An early destructive control reached a
reentrant-lock timeout; its owner-release observation was strengthened so the
final control fails immediately with its specific lifetime witness.

Fourteen cases run three times: all original owners removed; two shared fences;
already-ready fences; no fences; later descriptor mutation; registration failure
before/after a pending second action; ready prefix failure before publication;
ready final registration failing after source effects; shutdown pending/active
source access; throwing source callback with explicit cancellation; throwing
source callback followed by another increment; absent/mismatched owner refusal.
The tests require final source retirement separately from a still-inflight native
marker, expressly demonstrating the protocol's bounded scope.

Eight exact controls run three times: omit either handoff hop, clear packets after
registration, omit registration cancellation, omit source-exception cancellation,
drop owner before actual source access, omit owner validation, omit close locking.
Each requires its particular exit-91 message.

Regressions pass: ten actual GPU request paths repeated three times (including
normal OnCPURead/RequestComposite waiting), plus cancelled-read coverage control;
eight syncpoint owner cases/seven controls repeated three times; actual
GraphicBuffer/session retirement production repeated three times and 21 controls
once. Eight actual Windows AMD64 COFF objects compile with GCC 13.2 and pinned
real headers, zero diagnostics: HardwareComposer, nvdisp, GPU, GraphicBuffer,
SurfaceFlinger, RendererVulkan, Vulkan present Layer and framebuffer_config.
See [recorded results/input hashes](M4_P2_FRAME_SOURCE_RESULTS.json).

No full application link, real game/native device, Linux/Android or current-head
CI acceptance is claimed. Existing workflow gains this suite/log artifacts; no
manual run is dispatched.

```powershell
$env:PYTHONUTF8='1'
$env:CXX='C:/Strawberry/c/bin/g++.exe'
python tools/gpu_lifecycle_tests/run_frame_source_tests.py --baseline --revision 4e76547588 --output-dir .diagnostics/m4-frame-baseline
python tools/gpu_lifecycle_tests/run_frame_source_tests.py --repeat 3 --output-dir .diagnostics/m4-frame-fixed
python tools/gpu_lifecycle_tests/run_tests.py --repeat 3 --output-dir .diagnostics/m4-frame-requests
python tools/gpu_lifecycle_tests/run_syncpoint_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-frame-actions
python tools/physical_coherency_tests/run_nvdrv_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-frame-retirement
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/nvnflinger/hardware_composer.cpp --source src/core/hle/service/nvdrv/devices/nvdisp_disp0.cpp --source src/video_core/gpu.cpp --source src/core/hle/service/nvnflinger/ui/graphic_buffer.cpp --source src/core/hle/service/nvnflinger/surface_flinger.cpp --source src/video_core/renderer_vulkan/renderer_vulkan.cpp --source src/video_core/renderer_vulkan/present/layer.cpp --source src/video_core/framebuffer_config.cpp --output-dir .diagnostics/m4-frame-production
```

## Remaining boundary / next work

The source-object packet is now connected on the actual producer chain. Next
audit stable source translation/mapping-use epochs against concurrent unmap,
guest free/session retirement and source reads, including accelerated display.
Keep descriptor immutability, backing object retention, mapping validity and
native upload/image/presentation completion as distinct requirements. No native
registry, complete HostMemory use lease, whole producer/alias exclusion, F2 content
authority or physical backing transaction is implemented here. All M4 phases
remain incomplete, F2/F3 open and GuestBackingAdmission Unknown.

Current-run executables/objects/generated fixture headers and sources are cleaned
after preserving JSON/logs and checking current input/COFF hashes. The committed
results and `.diagnostics/m4-frame-results` retain evidence; main is not merged.
