# Retained channel creation through NV fd/session publication

Date: 2026-10-22 (Asia/Singapore).
Baseline: `7a82ddd79d` plus the implementation committed with this report.
Scope: terminal owner publication, not completion of an M4 phase.

## Behavior and ownership

GPU::AllocateChannel returns an optional ChannelAllocation containing both the
shared channel and an owned cold publication Entry. NV's GPU-device factory
retains it through device construction and fd-map insertion. Module::Open holds
it through OnOpen session acknowledgement and return value materialization.
NotifyShutdown closes this existing GPU domain; destruction joins entered
publications before renderer shutdown.

Closed allocation refuses before channel ID reservation or allocation. The
factory returns INVALID_NVDRV_FD without constructing a device or allocating its
events/syncpoint. Module may consume an unused fd number. Healthy cold creation
before GPU Start remains supported.

| Operation/result | Resource disposition |
| --- | --- |
| Refused Entry | No channel/scheduler/fd/device publication |
| Channel constructor throws | No map record; Entry releases on unwind |
| Scheduler declaration throws | GPU map record erased; candidate releases; exception propagates |
| Successful allocation | GPU and scheduler retain shared owner; caller retains publication through NV completion |
| Later NV construction/fd/session allocation throws | Entry releases on unwind; registered channel stays parent-owned until GPU teardown; no full Open rollback guarantee |
| Module destruction | Own device map cleared before event/syncpoint providers retire |

A creation mutex serializes ID reservation and registration. The short GPU map
lock is released before scheduler declaration: Scheduler::Push holds its scheduling
lock while binding a channel. BindChannel copies a shared owner under the map
lock, then releases it before rasterizer binding. The GPU creation lock does not
add concurrent-client safety to Module's fd/session maps.

Module's explicit fd-map clear fixes its own provider destruction order. External
shared device references must still retire before those providers. NV constructor
resource exception safety and rollback of later service effects remain separate
work; this change does not certify those paths or native Unknown retirement.

## Executed validation

Windows x64, MSYS2 MinGW GCC 15.2.0 (Rev8), Python 3.14.4:

```powershell
python tools/gpu_lifecycle_tests/run_owner_publication_tests.py --repeat 3 --output-dir .diagnostics/m4-channel-creation-windows
```

| Check | Result |
| --- | --- |
| Address-space, initialization and creation positives | PASS, 3 repetitions |
| Closed creation leaves channel ID, GPU/scheduler/fd maps and event/syncpoint allocation counters unchanged | PASS |
| Successful Open publishes both fd and session | PASS |
| Channel construction and scheduler declaration exceptions | PASS, no failed owner inventory |
| Concurrent GPU allocation (8 callers, 16 channels each) | PASS, 128 unique IDs and matching maps |
| Allocation waiting for scheduler does not block an existing-channel map lookup/bind | PASS |
| Teardown during channel construction, scheduler declaration, NV event construction, fd publication and session commit | PASS, waits at all five stages |
| Owned NV devices retire before provider destruction | PASS |
| Prior five address-space and six initialization mutation controls | PASS, precise rejections |
| Allocate through unrelated ingress | Rejected: `closed channel creation published device` |
| Omit registration rollback | Rejected: `failed channel declaration left owner inventory` |
| Hold map lock while declaring scheduler channel | Rejected: `channel map lock inverted scheduler binding` |
| Factory releases Entry before Module acknowledgement | Rejected: `teardown escaped retained channel creation` |
| Module releases Entry before OnOpen | Rejected: `teardown escaped retained channel creation` |
| Omit fd-map retirement before event provider | Rejected: `module destroyed event provider before owned devices` |
| Missing channel fd in address-space BindChannel | PASS, InvalidState without dereference |
| Existing valid channel address-space binding after Close | PASS, independent publication counterexample |
| Production GPU, Module, nvhost_gpu and nvhost_as_gpu units | PASS, local WSL syntax |

The fixture executes actual GPU allocation/binding/initialization, scheduler
declaration, NV GPU constructor/destructor, GPU-device builder, OpenResult,
Module::Open/GetDevice/destructor, OnOpen and address-space BindChannel bodies.
It also retains the prior GPU lifecycle, address-space and initialization checks.
The fresh fixture generator's ten lifecycle cases passed three repetitions plus
its cancelled-read mutation during this round's preliminary run. The final
publication run reused that unchanged lifecycle fixture.
Renderer, worker, engines, event/syncpoint providers, queue and full System lifetime
remain fixtures. Hooks pause immediately before actual effects; no failed compile,
timeout or unrelated crash counts as a passing mutation control.

Production syntax command:

```bash
g++ -std=c++23 -DNDEBUG -fsyntax-only -Isrc -Iexternals/dynarmic/externals/fmt/include -Iexternals/Vulkan-Headers/include src/video_core/gpu.cpp src/core/hle/service/nvdrv/nvdrv.cpp src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp
```

## Limits and retained outputs

The previous creation-after-Close counterexample is closed. Existing channel
address-space binding still succeeds after Close and is exercised independently.
Other service configuration, pre-SubmitGPFIFOImpl guest-buffer copies, escaped
aliases, actual queue acceptance, native GPU writers and checked completion remain
outside this proof. No reversible ProcessCohortStop coupling, full Module/System
owner join or durable GPU Unknown registry is established. All THREE M4 phases
remain unfinished; ordinary GuestBackingAdmission remains Unknown.

No new remote workflow, Android/full application build or native Vulkan test was
run. This round's temporary binaries, generated fixtures, manifests and logs are
removed after inspection; this Markdown conclusion and test sources remain.
Older diagnostics are untouched. Main waits for M7's final zero-copy merge.
