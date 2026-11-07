# M4 P2: native drain and terminal disposal

Follow-up: the [whole-owner terminal protocol](M4_P2_WHOLE_OWNER_TERMINAL_DISPOSAL.md)
addresses the owning shutdown/failure gap below with an early System drain and
process-level Fail-Stop. This document records the earlier baseline and native
primitive evidence; it does not certify full P2 or Guest admission.

2026-10-23; baseline `8fb32cf53d`. P2 is incomplete. Ordinary
GuestBackingAdmission remains Unknown. No retirement queue or retained-record
type is added by this change.

## Production boundary and repair

The terminal path in `GPU::Impl::~Impl` closes ingress, stops GPUThread, joins
owner publications/rasterizer bindings and closes Composite requests before
calling renderer shutdown. Vulkan then shuts down rasterizer/pipeline/fence
workers, waits recording callbacks, joins presentation and the recording worker,
detaches on-submit callbacks and stops TurboMode. Only after those joins does it
take `scheduler.submit_mutex` and call the real `vkDeviceWaitIdle` wrapper.
The mutex must never span worker joins: those workers may need it for final
submissions. This is a terminal path; it is not a reversible whole-guest gate.

An actual ordering defect existed after successful device idle. Rasterizer first
retired BufferCacheRuntime controlled/alias transfers (pins, upload/readback
allocations), then asked Scheduler to preflight checked work. A refusal could
therefore arrive after dependent holders had already been released.

Scheduler now returns an explicit disposal result and preflights all writes and
transfers before releasing any checked holder. Missing transfer submission is
also a refusal rather than a null dereference. Rasterizer propagates refusal
before touching runtime holders. Renderer logs device-idle failure separately
from unsettled host ownership. A successful disposal is terminal cleanup only;
it does not promote failed submissions or their contents to Complete.

| Native/host state | Explicit disposal result | Resource action |
| --- | --- | --- |
| Native idle not successful | Not invoked | No explicit checked/runtime retirement |
| Observed host work, missing/unattempted submission, worker pending or result Pending | Refused | Preserve both checked and runtime holders during this call |
| Native idle successful; all host submission attempts settled | Accepted | Retire checked holders, keep exclusion quarantined, then retire runtime transfers |
| Submission failed but independently successful whole-device idle | Accepted for disposal | Release terminal native-use holders; never claim successful content |

## Native Windows evidence

`tools/gpu_lifecycle_tests/run_native_terminal_drain.py` compiles a standalone
native Vulkan witness using the pinned headers and the system Vulkan loader.
It allocates two real native buffers, records two fill/copy submissions with
transfer dependencies and a transfer-write to host-read barrier, joins its only
publisher thread, checks `vkDeviceWaitIdle`, checks mapped-memory invalidate,
verifies every readback word, and only then destroys command buffers, buffers,
memory and the device. Loader/device absence is an unavailable failure, not a
pass. Unexpected Vulkan results never enter the successful cleanup branch.

On NVIDIA GeForce RTX 4060, all three runs passed: two submissions, 1024 words
equal to the last submission's `0x99999999`, followed by owned native disposal.
The selected memory was host coherent. Explicit invalidate was checked, but a
noncoherent allocation path was not exercised on this machine. Omitting the
tail submission failed precisely at `Tail copy content mismatch`; completion of
the earlier work cannot supply the expected final content.

This proves a native primitive sequence for dedicated allocations. It does not
execute the production Scheduler, ordinary guest backing, arbitrary shader/image
writers, multiple publishers/queues or an actual device-loss cleanup case.
Successful idle alone is not a host content-visibility certificate: the witness
also has the explicit barrier, invalidate and content check.

## Local verification

- Actual Scheduler disposal and Rasterizer inline coordinator: three repetitions,
  four exact negative controls (omitted write/transfer preflight, ignored refusal,
  runtime release before preflight). Covers mixed settled/pending owners and
  missing transfer submission without partial release.
- Renderer/worker teardown extraction: nine cases, each three repetitions,
  including failed native idle and refused checked disposal. Device and native
  dispatch in this suite remain fixtures.
- Synchronization provenance suite: positive-only compatibility run. Its driver
  remains simulated; mutation acceptance was not rerun here.
- Real Windows x64 production compilation: Scheduler, RendererVulkan and
  RasterizerVulkan; three AMD64 COFF objects, no compiler diagnostics. No full
  application link, Android build or remote workflow was run.
- Native Windows witness: three passes and one precise omitted-tail negative
  control, as described above.

Commands:

```powershell
python tools/gpu_lifecycle_tests/run_checked_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-terminal-disposal-checks
python tools/gpu_lifecycle_tests/run_renderer_tests.py --repeat 3 --output-dir .diagnostics/m4-terminal-disposal-renderer
python tools/gpu_lifecycle_tests/run_native_terminal_drain.py --repeat 3 --output-dir .diagnostics/m4-terminal-native
python tools/physical_coherency_tests/run_sync_provenance_tests.py --positive-only
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/video_core/renderer_vulkan/vk_scheduler.cpp --source src/video_core/renderer_vulkan/renderer_vulkan.cpp --source src/video_core/renderer_vulkan/vk_rasterizer.cpp --output-dir .diagnostics/m4-terminal-production
```

Production object SHA256, retained here before deleting temporary output:

| Object | Bytes | SHA256 |
| --- | ---: | --- |
| vk_scheduler | 219578 | `8e9205a33a2642b8cab91bddd6d8ccb8dc1f1522a6a7b588df5c773c1e658e6c` |
| renderer_vulkan | 102627 | `5d9444bf8ba444ce1da9a4982db1688844b392346381f9a2250ec9c0a2188c74` |
| vk_rasterizer | 1121196 | `e492b55306a11f71960508967a38d7f8d019ccfb98c8e6f9be4a8e251c345e38` |

## Remaining global disposal gate

Refusing explicit retirement does **not** stop subsequent C++ member destruction.
Renderer shutdown still finishes, and Rasterizer destruction later calls
`NukeAllAllocations`; Scheduler/member containers also eventually destruct.
Logging refusal is observability, not a durable whole-owner quarantine. Likewise,
an earlier Finish/fence/worker exception may prevent reaching the final device
wait at all. This change does not claim these failure paths are safe globally.

Before terminal Unknown disposal can be accepted, the actual owning teardown
entry must propagate a drain outcome and retain the entire necessary lifetime
through failure: renderer/device/allocator, GPU owners, Process/PageTable,
physical backing and aliases. GPU Impl currently owns some of that graph but
borrows System/Host1x/device-memory owners; simply leaking another record or the
renderer cannot establish the complete boundary. Detached workers must also be
proved absent before any retained graph can outlive borrowed facades.

The next implementation target is this owning teardown outcome and lifetime
boundary, with an independently successful native drain as the sole terminal
native-use release route. Content visibility remains separately checked; no
Guest admission or reopening follows from terminal disposal. The standalone
witness and local preflight repair are prerequisites, not P2 completion.
