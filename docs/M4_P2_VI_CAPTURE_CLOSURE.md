# M4 P2: VI SharedBufferManager owners and raw capture entry

2026-10-23; baseline `cbf9c2599083d014145ead009e2b6503c57cbce7`.
The local issue branch, fork and PR375 agreed before work. Main stays
`b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`. This is a bounded synchronous
capture producer repair following an audit of creation/raw write/destruction.
It is not a complete SharedBufferManager ownership transaction or M4 acceptance.

Follow-up: [VI session owner protocol](M4_P2_VI_SESSION_OWNERSHIP.md) now counts
creation/raw initialization and handles partial create/Close refusal with prepared
retained owners and explicit Unknown records. It supersedes the create/destroy
open-transition descriptions below, while preserving this report's content/native
limits and historical capture evidence. M4 P2 is still incomplete.

## Source ownership audit

Live call sites are IManagerDisplayService::Create/DestroySharedLayerSession and
AM::DisplayLayerManager::WriteAppletCaptureBuffer. Container owns the manager in
an optional; GetSharedBufferManager returns a borrowed pointer after lazy init.
The manager holds a shared Module provider but borrows System/VI Container.
Its session records contain numeric IDs, not an explicit complete owner bundle.

| Step / actual source | Ownership and effects observed | Remaining obligation |
| --- | --- | --- |
| AllocateSharedBufferMemory | AllocateAndOpen creates secure physical-page references; a local unique KPageGroup describes the blocks; raw DeviceMemory pointers fill red before pool publication | Count initialization and cover allocation/fill exceptions with a proven page/backing disposition; this initializer remains outside managed facade stores and the new capture entry |
| Pool publication in CreateSession | m_guard covers assigning the pool, buffer/display IDs and later session setup; once assigned the pool descriptor is not replaced/reset until manager destruction | Publication locking does not close every producer or pin host virtual mappings |
| MapSharedBufferIntoProcessAddressSpace | Maps the page group into the borrowed Process before NvCore::OpenSession retains its own Process/backing owners | The manager does not retain map_address in SharedBufferSession; mapping teardown and Process owner lifetime must be explicitly composed |
| CreateSession session insertion | Inserts SharedBufferSession before OpenSession, Module::Open, handle allocation, layer setup and preallocated-buffer publication finish | No committed/partial state is represented; early errors/exceptions can leave a numeric record and earlier effects. Do not mistake map membership for successful creation or replay partial effects |
| Nested NV calls during create | Container::OpenSession and direct nvmap helper calls are not ordinary Module dispatch; Module::Open and GraphicBuffer publication have their own admission | An outer NVDRV entry cannot make later nested admission succeed after close. Need a continuable protocol/outer-before-inner closure or an explicit admitted operation capability before wrapping creation |
| WriteAppletCaptureBuffer | GPU capture result is copied through raw physical pointers into the manager's pool; neither the Memory facade nor Module::Ioctl counts it | The synchronous capture subset is fixed below; raw initializer, parallel raw aliases, VMA lifetime and full content coherency remain open |
| DestroySession | Under m_guard: destroy layer, direct IocFree, call Module::Close, raw Container::CloseSession, erase record | Module::Close can return Busy while closed, but its result is ignored before the later steps. Direct cleanup can have partial effects; need prepared continuous owners and checked execution/cancellation/Unknown disposition, not a void early-return or retry |
| Manager destruction | Default destructor retires member storage; KPageGroup destructor calls Finalize, which frees block descriptors rather than closing physical-page references | Do not add Close or reorder members as a speculative leak fix: outstanding mappings/aliases/native users need a separately proven retirement protocol first |
| NvCore session lifetime | OpenSession retains Process/backing; CloseSession mutates refcounts, mappings, mapper/ASID and reuse state; inactive session storage can still hold owners until reuse/destruction | This local provider retention does not automatically own the manager's entire pool/alias/native graph or validate a partially initialized numeric session |

The next concrete creation/retirement investigation should inject failure at
OpenSession/Open/layer setup and closure at Module::Close, then verify exact
record/handle/Process/mapping ownership through any completed prefix. These
source findings are not claimed as newly executed CreateSession/DestroySession
fault witnesses. Those methods and pool allocator cleanup remain unchanged here.

## Capture defects reproduced before repair

The actual capture body ran against the starting production implementation before
the production edit. Three exact witnesses failed with exit 91: closed admission
still performed capture/raw writes; close during a blocked capture reported a
drained domain; and an empty capture cleared the destination and returned success.
GPU::GetAppletCaptureBuffer explicitly returns an empty vector on cancelled sync
wait, so the last case represents an actual cancellation result boundary, not a
preservation success. An ordinary unavailable image can also be empty.

A fourth historical replay holds the manager publication mutex after publishing
the pool but before finishing initialization. The old capture body escapes it.
The deterministic publisher is a fixture for the audited CreateSession critical
section; no actual CreateSession production body or real Process is simulated as
passing acceptance by this test. Historical --revision reproduces all four cases
without changing the checkout. Crashes/timeouts/unrelated errors are not witnesses.

## Bounded repair and linearization

WriteAppletCaptureBuffer now takes the existing Module dispatch entry before any
GPU capture, pool borrow or physical effect. Closed admission returns
VI::ResultOperationFailed with outputs untouched. There are no nested NVDRV
dispatch calls inside this capture body; the same shortcut is not valid for create
or destroy. The entry remains alive through the synchronous GPU result, stores,
existing projection calls and successful output publication.

Under m_guard, it requires a published pool/buffer ID and takes the stable pool
descriptor pointer. It releases the guard before waiting for GPU capture. The
mutex pairs with CreateSession publication; the descriptor is thereafter never
replaced/reset by live manager methods. This snapshot relies on the existing
caller-held manager/System lifetime. It is not a shared facade owner, host-mapping
lease, page pin, Process owner or revocation of all aliases. DestroySession does
not reset the manager's pool descriptor; that fact alone does not certify its
mapping/session/native retirement.

Empty/cancelled capture returns failure before GetPointer or any destination store,
without changing success outputs or sealing a healthy domain. Nonempty capture
keeps the previous byte placement, padding and projection behavior. Exceptions
propagate but seal the dispatch domain before entry exit. A completed first block
plus failed second block stays partial and Unknown: no rollback, success output,
automatic retry or falsely clean cancellation is claimed.

The domain is the same Kernel NVDRV domain already closed/polled before inner
managed-write/host1x domains by ProcessCohortStop. No new producer domain, scheduler
policy, global owner queue or in-process recovery is added. The new entry counts
one synchronous writer subset; creation, retirement, other raw aliases and native
GPU work still prevent whole-guest admission.

## Unchanged content and native boundaries

The pre-existing pointer loop advances start to end before ApplyOpOnPointer and
passes `end - start` to InvalidateRegion. This remains a zero-length invalidation
concern. The new tail test proves entry retention around the actual call boundary
using a projection fixture; it does not prove the production projection finds an
alias or a useful invalidation runs. Do not promote callback return, nonempty
capture or this admission entry to F1/F2 preservation/publication success.

No immutable physical access plan, remap protection, BufferCache first-registration
handshake, native visibility or complete pool/Process/PageTable/allocator lifetime
is established here. Ordinary parallel captures and arbitrary facade destruction
are not newly certified. HostMemory remains a separate high-risk remap/lifetime
module; retaining a descriptor alone cannot keep a borrowed host pointer safe.

## Windows verification

The suite extracts the unmodified production WriteAppletCaptureBuffer body plus
actual Module admission/seal methods. ProducerDrain/ScopeGuard are real headers.
GPU result/projection, physical GetPointer/byte storage and page-group block layout
are fixtures; the publisher wrapper exposes a rendezvous around a real mutex.
It does not use real Memory/GetPointerImpl, ThreadManager, KProcess or native GPU
completion. The production code is separately compiled with the real headers.

Eight paths run three times: closed/resume, active GPU call, first raw-pointer use,
tail projection, empty result, missing pool, publication interleaving and a
first-block-written/second-block-exception prefix. The active GPU case checks the
publication mutex can be acquired while capture is blocked. Eight exact mutation
controls run three times: unrelated domain, release before GPU/raw/projection,
ignored empty result, omitted exception seal, omitted publication mutex and holding
the mutex across GPU waiting. Each requires its exact exit-91 witness.

The existing actual GPU-method lifecycle suite passes ten cases three times and
its cancelled CPU-read control, including capture cancellation and ordinary sync
behavior. Three real Windows x64 production objects compile with real pinned
headers, zero diagnostics: shared_buffer_manager, manager_display_service and
AM display_layer_manager. Source/object hashes and results are preserved in
[results](M4_P2_VI_CAPTURE_RESULTS.json). No full app link/game, native GPU proof or
Android/Linux validation is claimed. The existing workflow gains the new suite;
no manual workflow is dispatched or current-head CI success claimed.

```powershell
$env:PYTHONUTF8='1'
python tools/physical_coherency_tests/run_vi_capture_drain_tests.py --baseline --revision cbf9c2599083d014145ead009e2b6503c57cbce7 --output-dir .diagnostics/m4-vi-capture-baseline
python tools/physical_coherency_tests/run_vi_capture_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-capture-fixed
python tools/gpu_lifecycle_tests/run_tests.py --repeat 3 --output-dir .diagnostics/m4-vi-capture-gpu
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/vi/shared_buffer_manager.cpp --source src/core/hle/service/vi/manager_display_service.cpp --source src/core/hle/service/am/display_layer_manager.cpp --output-dir .diagnostics/m4-vi-capture-production
```

Next: deterministic CreateSession partial-state and DestroySession refused-Close
owner tests before designing continuable admitted operations and prepared retirement.
Keep create/raw-init/capture/destroy effects distinct and do not infer complete
ownership from one counter. All three M4 phases remain incomplete; F2/F3 open and
GuestBackingAdmission Unknown. Main is unchanged. Only current-run temporary
executables/objects/fixtures are cleaned after preserving final logs/manifests;
old result bundles and dependency caches remain.
