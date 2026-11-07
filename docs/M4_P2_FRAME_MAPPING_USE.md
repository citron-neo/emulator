# M4 P2: delayed frame-source mapping use

Follow-up: [session-incarnation source admission](M4_P2_SESSION_SOURCE_ADMISSION.md)
closes the escaped-producer recapture gap identified below. Native image lifetime
and generic raw session/GMMU admission remain separate obligations.

2026-10-24 (Asia/Taipei). Baseline `96d993457dd0cf2078b9538854b566ceec3b9146`,
branch `codex/issue-372-fixes`. Personal main remains
`b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`; no merge or manual workflow dispatch.

## Defect and bounded repair

The preceding address accessor fixed a mismatched mutex, but a saved DAddr could
be unmapped/reused before `Layer::UpdateRawImage` called GetPointer. That method
also treated the returned pointer as one contiguous tiled extent. The test-first
baseline executes its actual source-read prefix and actual DeviceMemoryManager
GetPointer against controlled PTEs: remapping the saved address before the read
selects another physical source. It fails with the exact exit-91 witness
`retired mapping reached source read`; no UAF, crash or timeout is acceptance.

The candidate protocol was tested before production integration. Its final
production implementation is `Common::FrameMappingDomain`, independent of the
existing controlled coherency admission/generation. Each device manager owns one
domain, and a frame carries a value/range stamp retaining only domain state.

1. NvMap::GetDisplayMapping retains the handle-table snapshot, releases the table
   lock, then captures address, aligned extent and domain generation under the
   handle lock. Capture is nonblocking and refuses a busy, pending, failed or
   closed domain. It does not grant guest data authority or retain the manager.
2. Device Map and Unmap begin a source mutation **before** cache/direct-buffer
   retirement callbacks. This waits for an active bounded CPU copy/cache selection,
   increments the generation and pending count, then releases the source mutex.
   No source/PTE lock spans those callbacks or any GPU waits. Nested mutations
   cannot reopen capture. Existing PTE mapping_generation behavior is unchanged.
3. UnregisterProcess also invalidates source stamps. Final Container::CloseSession
   starts an outer mutation before ordinary mapper/ASID retirement, and also in its
   shutdown shortcut. An old deferred packet is therefore revoked even where
   shutdown deliberately retains the mappings. Nonfinal session ref drops do not
   retire the session. Device-manager destruction closes the domain first.
4. At the raw read, CopyFrameMapping validates the stamp/range and holds the source
   mutex through a bounded owned byte copy. It takes mapping_guard inside that
   section, verifies every page first, then copies page by page from canonical
   physical backing. Missing pages leave output unchanged; physical continuity is
   never inferred from DAddr continuity. No guest pointer escapes. Allocation is
   outside the section; unswizzle, buffer flush, scheduler recording/waits and
   native image use happen after it unlocks, using the owned byte vector.
5. AccelerateDisplay holds the texture-cache mutex first, then validates/uses the
   source domain while selecting its image view. The audited selection chain
   performs cache lookup and optional native image-view construction, with no
   guest source read, recording/submission or explicit GPU wait. Query notification
   and subsequent rendering occur after source unlock. A rejected selection falls
   through to the independently checked raw copy, which rejects the same old stamp.

This is a lightweight CPU mapping-use boundary plus an owned byte snapshot at the
actual read. It needs neither a scheduler continuation nor an immutable physical
transfer plan for this CPU copy. It is not a Vulkan completion registry, an
all-producer range transaction, or an owner of future native accesses.

## Outcomes and partial effects

| Interleaving | Result |
| --- | --- |
| No mapping change, valid pages | Existing fence/request behavior; real source copy, unswizzle and flush still execute |
| Unmap/remap/reuse wins before source use | Old generation refuses; no guest copy/cache selection for that stamp |
| Raw copy wins first | Mapping intent waits for that CPU copy; later unmap/reuse cannot change its owned bytes |
| Mutation in a callback, including nested session teardown | Old use and new capture both refuse while pending |
| Callback/PTE/retirement exception | Mutation destructor makes source failure sticky; future successful maps do not clear it |
| Mapping hole/out-of-extent source | Raw read refuses; no unswizzle or flush from stale staging contents |
| Retirement after owned copy, before unswizzle | CPU processing uses the copied original bytes without a source lock |

UpdateRawImage and ConfigureDraw now return whether the layer has drawable data;
WindowAdaptPass omits rejected layers when recording its final draw. Other valid
layers and the background can still render. This is **not** an atomic whole-frame
cancellation or a no-effects result: resource refresh/allocation, prior cache view
creation, renderpass closure and resource-tick waits can already have occurred.
Allocation, cache and unswizzle exceptions retain the existing GPU request's
cancel-on-exception behavior; no uncertain work is retried here.

The domain is deliberately device-wide. Unrelated maps can cancel queued frames;
contention can refuse capture/use rather than wait under cache ownership. A failed
mutation seals subsequent source access for that manager lifetime. These conservative
presentation costs need device/game evaluation before performance acceptance.
Normal OnCPURead/RequestComposite wait/callback semantics and ThreadManager dispatch
are not changed; layer refusal is not converted into a claim of completed guest
publication or native work. Fence-registration completion still differs from a
later deferred fence callback.

## Deterministic Windows evidence

`run_frame_mapping_tests.py` passes three repetitions of ten protocol paths and
six exact controls, plus seventeen actual-body raw/mutation/cache-selection paths
and four exact controls. The protocol uses real mutexes/threads with an observed
mutex wait, not sleeping as the interleaving decision. Its active-use test waits
until mutation is blocked at the real lock. The actual-copy test gates memcpy,
starts the extracted real Unmap on another thread, observes its source-domain
wait, then releases the copy and checks the retained bytes and unmapped PTE.

Actual bodies: const GetPointer; CaptureFrameMapping, CopyFrameMapping and
WithFrameMapping; complete Map/Unmap/UnregisterProcess; UpdateRawImage through
the pre-native-copy boundary; complete AccelerateDisplay. The copy observation
wraps memcpy without changing its copy semantics. Test double PTE storage,
reverse-map helpers, process GetPointerSilent, physical byte arrays, rasterizer
callbacks, cache/native views, unswizzle and flush are explicit fixtures. No real
process Memory/GetPointerImpl, HostMemory remapping or Vulkan driver executes in
this new suite. The raw prefix's return only tests the source boundary, not the
omitted VkBufferImageCopy recording tail.

Cases cover normal, unmapped and reused addresses; final session-shaped mutation;
actual map/unmap callback admission and callback failure; ASID unregister; holes,
partial holes with unchanged destination, extent checks and noncontiguous backing;
normal/retired/pending accelerated selection; active actual copy versus Unmap;
and reuse after copy but before unswizzle. Protocol cases additionally cover
foreign/closed domains and nested mutation. Controls remove generation, pending,
failure poison, bounds, closure or identity checks; actual-body controls bypass
copy/cache use guards and move map/unmap intent after the first callback. Each
requires its exact exit-91 diagnostic; timeout/crash/nonmatching failure is rejected.

Regressions: packet fourteen paths/eight controls three times; address ten
paths/six controls three times (including actual GetDisplayMapping against free
and session unmap); actual GraphicBuffer/session retirement three
times plus 22 exact controls once. The latter executes actual CloseSession in
ordinary and shutdown modes with a real source domain, checking old-stamp
revocation and refusal of capture from the ordinary unmap callback. Its nvmap,
mapper, Process, device and backing operations remain fixtures.

Nine real Windows AMD64 COFF objects compile with GCC 13.2, pinned dependencies
and zero diagnostics: NvMap, nvdisp, Container, MaxwellDeviceMemoryManager,
Vulkan Layer, rasterizer, WindowAdaptPass, GPU and HardwareComposer. The window
pass uses a real glslang-generated present vertex shader header. No application
link, native/game, Linux/Android or current-head CI acceptance is claimed.
See [hashes, manifests and cleanup](M4_P2_FRAME_MAPPING_RESULTS.json).

## Remaining obligations / next audit

This closes mapping continuity for the covered queued stamp through its CPU copy
and guarded cache selection. An epoch recheck alone would not have done that.
The retained packet/manager/System lifecycle remains an external precondition;
the domain state deliberately contains no manager pointer or Process authority.
Canonical physical allocation lifetime still depends on the existing retained
source-owner graph and shutdown order. Concurrent guest/CPU/GPU writers are not
excluded, so the owned byte copy is not a coherency/content transaction.

The accelerated view returned from selection still needs its existing cache/native
owner through recording, submission and presentation completion. This repair does
not add that per-frame native lease or prove final VI unpin/free. HostMemory,
especially concurrent VMA merge/protection versus borrowed aliases, remains a
high-risk module requiring dedicated validation; copying canonical physical pages
here is not general HostMemory mapping-use acceptance.

Final CloseSession revokes **previously captured** packets. Permanent per-session
source admission is separate: its shutdown shortcut retains mappings, while the
existing stale-PID branch intentionally retains resources. A future new capture
through an escaped producer after such retirement requires a valid session/producer
entry protocol; this global generation is not a permanently closed session token.
Audit that re-admission together with accelerated native-owner handoff next, without
holding handle/mapping/source locks across renderer or GPU waits. All M4 phases
remain incomplete; F2/F3 open and GuestBackingAdmission Unknown.

```powershell
$env:PYTHONUTF8='1'
$env:CXX='C:/Strawberry/c/bin/g++.exe'
python tools/gpu_lifecycle_tests/run_frame_mapping_tests.py --baseline --revision 96d993457d --output-dir .diagnostics/m4-use-baseline
python tools/gpu_lifecycle_tests/run_frame_mapping_tests.py --candidate --output-dir .diagnostics/m4-use-candidate
python tools/gpu_lifecycle_tests/run_frame_mapping_tests.py --repeat 3 --output-dir .diagnostics/m4-use-fixed
python tools/gpu_lifecycle_tests/run_frame_source_tests.py --repeat 3 --output-dir .diagnostics/m4-use-packet
python tools/gpu_lifecycle_tests/run_frame_address_tests.py --repeat 3 --output-dir .diagnostics/m4-use-address
python tools/physical_coherency_tests/run_nvdrv_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-use-retirement
```

The committed result records the complete nine-source Windows compile command.
Only this run's generated cpp/header/exe/object files are removed, after verifying
current or baseline input hashes, COFF markers/object hashes and preserved logs.
Local log/JSON copies remain under `.diagnostics/m4-use-results`.
