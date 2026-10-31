# M4 P2: source address resolution versus sustained mapping use

Follow-up: [bounded use at the actual source copy/cache selection](M4_P2_FRAME_MAPPING_USE.md)
repairs the queued-stamp window characterized below. Native completion and
permanent per-session re-admission remain separate obligations.

2026-10-24 (Asia/Taipei). Baseline `0412393e94aa75a936c48ab41cfec3c5598d4838`;
local/fork/PR375 heads agreed before work. Branch `codex/issue-372-fixes`;
main remains `b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`. This repairs a
source-address synchronization defect and already-invalid address publication.
It deliberately does not claim a mapping-use epoch/lease for the later read.

## Audit findings

The per-frame packet retains GraphicBuffer and its existing prepared owner graph,
plus an immutable descriptor. Nvdisp still translates its numeric handle into a
device address before GPU registration. The following are independent boundaries:

| Route | Source fact | Consequence |
| --- | --- | --- |
| NvMap::GetHandleAddress | Previously read d_address under handles_lock alone | Free/unmap/pin write d_address under Handle::mutex, so the snapshot had no matching synchronization |
| NvMap::FreeHandle, last guest dupe | Force-unmaps and clears pins even with positive internal_dupes | A packet's wrapper/internal duplicate does not by itself prevent mapping removal |
| Container::CloseSession | Its ordinary path calls file.UnmapAllHandles before mapper/process registration retirement | A retained Process/backing object does not keep the session's mapping metadata active |
| NvMap::UnmapAllHandles | Snapshots the handle table, filters session/guest dupes under per-handle locks, then calls FreeHandle | Session unmap reaches the same mapping mutation lock as guest free |
| DeviceMemoryManager::GetPointer | Reads the compressed physical entry and returns a raw pointer; no sustained mapping guard is returned | A previously valid numeric DAddr is not a physical/mapping-use plan |
| DeviceMemoryManager::Unmap | Performs invalidation callbacks then updates entries/generation under mapping_guard | The source packet currently owns no mapping epoch spanning these effects and the delayed access |
| Existing controlled epoch helpers | Require matching epoch/generation and nonblocking guarded bounded operations | They cannot simply enclose a renderer callback that waits for GPU/cache work |

In particular, retaining a wrapper/handle/Process/backing object cannot freeze
the meaning of its device address. The audit's actual GetHandleAddress caller
inventory finds nvdisp's composition translation; renderer callbacks use the
already copied FramebufferConfig address rather than calling this accessor again.
No lock is added around the whole renderer callback or GPU drain.

## Test-first defects and repair

Before production edits, actual-body tests reproduced two precise exit-91
witnesses: address lookup bypassing a mapping mutation's handle lock, and nvdisp
publishing a deferred framebuffer after its translated address became zero.
The writer is paused before changing d_address, so the baseline does not rely
on executing a C++ data race, stale pointer access or timeout as its witness.

GetHandleAddress now obtains a shared handle snapshot through GetHandle (briefly
holding handles_lock), releases that table lock, then locks Handle::mutex before
reading d_address. Final free can take handle -> table, so holding the table lock
while waiting on the handle would create an inversion. An exact negative control
observes that inversion before allowing an actual deadlock. The shared snapshot
also spans removal of the table entry while resolution is waiting.

If a completed guest free/session unmap wins that handle lock, lookup observes
zero. Nvdisp now resolves into a local address, refuses zero before publishing
the GPU request and uses that single snapshot for FramebufferConfig. A missing
handle still resolves to zero. Positive live-address publication remains normal.
Other earlier valid layers in a batch may already have had address lookups and
local packet copies; refusal publishes no GPU request, renderer metadata update
or presentation. The existing void API is not a completed-retirement status.
Creating/replacing an empty GraphicBuffer descriptor is unchanged; a source with
no mapped address can no longer be submitted as a valid frame address.

FreeHandle failure semantics are unchanged: failed unmap restores the guest dupe,
preserves pin/address ownership state, unlocks before NotifyShutdown and returns
nullopt. The accessor can observe the retained numeric address after such a
failure; that observation supplies **no** read authority for an uncertain or
partially modified mapping. A retained number is not verified mapping validity.
No failure is converted to Success and no uncertain free is replayed here.

## What this intentionally does not close

The after-snapshot characterization first resolves a positive address, then
executes actual guest FreeHandle with an internal duplicate still retained. The
saved address remains positive while the handle's current mapping becomes zero
and pins drop to zero. The test never dereferences that saved address. This is
an explicit witness of the remaining continuity gap, not acceptance of mapping
admission. A multi-layer batch can likewise have an earlier address invalidated
before the complete request is published.

The fix linearizes address resolution only. It does not prevent unmap/free/session
retirement between resolution and a guest-fence callback, or between pointer
translation and its last CPU/native access. It adds no generation token, physical
source plan, sustained read capability or native completion registry. All such
uses remain Unknown. Numeric field locking is necessary but cannot replace those
remaining requirements.

## Deterministic Windows evidence

`run_frame_address_tests.py` extracts actual NvMap Handle/FreeInfo layouts,
Handle constructor, GetHandle/GetHandleAddress/TryRemoveHandle/FreeHandle/
UnmapAllHandles and nvdisp Composite conversion. Actual packet/config headers
are used. Real std::mutex wrappers, threads and gates select resolution versus
guest free/session unmap. Current-thread ownership observations reject the wrong
lock order without recursively try-locking an owned std::mutex.

UnmapHandle itself is a controlled mutation/failure fixture, not real GMMU/SMMU
unmapping. System/GPU publication and GraphicBuffer's public constructor/destructor
are stand-ins; the latter has no fabricated nvmap pin/Process/backing authority.
The session case runs actual UnmapAllHandles, not the whole Container::CloseSession.
No real DeviceMemory/Memory/GetPointerImpl, physical read, mapping remap, driver or
game executes in this new suite. Source and epoch conclusions beyond the selected
lock/refusal interleaving come from the audited production bodies, not invented
end-to-end runtime evidence.

Eight paths pass three repetitions: guest free; session unmap; failed unmap and
ownership restoration; final guest free removing the table entry; retirement
after shared table snapshot but before the handle lock; normal/missing lookup;
zero-address refusal; explicit after-snapshot invalidation characterization.
Five exact controls pass three repetitions: omit per-handle lock, hold table
lock across handle wait, omit zero refusal, discard every valid address, ignore
unmap failure. Each requires its particular exit-91 message.

Regressions: frame source packet fourteen cases/eight controls (three repetitions);
actual GraphicBuffer/session retirement production three times plus 21 controls
once; checked-unmap suite using actual ThreadManager/RunThread and Vulkan
UnmapMemory bodies three times plus its existing controls. These still do not
prove native completion or mapping continuity.

Four real Windows AMD64 COFF objects compile with GCC 13.2, existing pinned headers
and zero diagnostics: nvmap, nvdisp, HardwareComposer and GPU. See
[recorded results and input hashes](M4_P2_SOURCE_ADDRESS_RESULTS.json).
No application link, native driver/game, Linux/Android or current-head CI success
is claimed. Existing workflow gains the suite/artifacts; no manual run is dispatched.

```powershell
$env:PYTHONUTF8='1'
$env:CXX='C:/Strawberry/c/bin/g++.exe'
python tools/gpu_lifecycle_tests/run_frame_address_tests.py --baseline --revision 0412393e94 --output-dir .diagnostics/m4-address-baseline
python tools/gpu_lifecycle_tests/run_frame_address_tests.py --repeat 3 --output-dir .diagnostics/m4-address-fixed
python tools/gpu_lifecycle_tests/run_frame_source_tests.py --repeat 3 --output-dir .diagnostics/m4-address-packet
python tools/physical_coherency_tests/run_nvdrv_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-address-retirement
python tools/gpu_lifecycle_tests/run_checked_unmap_tests.py --repeat 3 --output-dir .diagnostics/m4-address-unmap
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/nvdrv/core/nvmap.cpp --source src/core/hle/service/nvdrv/devices/nvdisp_disp0.cpp --source src/core/hle/service/nvnflinger/hardware_composer.cpp --source src/video_core/gpu.cpp --output-dir .diagnostics/m4-address-production
```

## Next bounded protocol

Specify source translation/mapping use at the **actual** read, not just the first
numeric lookup. Test map-generation change after snapshot, remap/reuse to another
physical source, active CPU read versus unmap, failed partial unmap, session mapper
retirement and accelerated display. Determine whether an immutable physical read
plan plus a bounded guarded copy suffices for raw source reads, and which owners
or cancellation boundary must span accelerated/native use. Do not hold handle or
mapping locks over renderer/GPU waits, and do not promote an epoch recheck alone
to a lease over a subsequently escaped pointer. F2/F3 open; all M4 phases remain
incomplete and GuestBackingAdmission Unknown.

Current-run exe/object/generated fixture sources and headers are cleaned after
preserving JSON/logs and checking current input/COFF hashes. Committed results
and `.diagnostics/m4-address-results` preserve the handoff; main is not merged.
