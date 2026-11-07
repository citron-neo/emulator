# M4 P2: warm slot retirement and an escaped producer's publication boundary

2026-10-23 (Asia/Taipei). Baseline `1c1348d777ea2e08cd8691844ad6db8b890a56ca`;
local, fork and PR375 heads agreed before work. Branch `codex/issue-372-fixes`;
main remains `b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`. This closes one
abandoned-queue publication hole. Native retirement and all M4 phases remain open.

Follow-up: [backing/cache/native audit and checked recreation idle](M4_P2_BLIT_NATIVE_IDLE.md)
repairs a separate Vulkan presentation completion boundary. It grants no blanket
native retirement permission to this queue's buffers.

## Ownership and ordering audit

The preceding [Conductor repair](M4_P2_VI_CONDUCTOR_DRAIN.md) stops timing/VSync
CPU accesses before warm OnTerminate destroys layers/queues/displays. System
still calls that stop callback before its checked GPU::Shutdown native drain.
The following owners are distinct:

| Owner | Warm cleanup effect | What it does not prove |
| --- | --- | --- |
| SurfaceFlinger layer list | DestroyLayer removes its shared Layer reference | A display stack or other holder can still retain the Layer |
| Display stack | RemoveDisplay erases the stack; final Layer destruction calls consumer Abandon | Native submissions are not drained by erasing this reference |
| ConsumerBase slots | AbandonLocked clears cached GraphicBuffers/fences, then disconnects | This is CPU slot teardown, not native completion |
| BufferQueueCore slots/queue | Disconnect sets is_abandoned under core->mutex, clears the queue and frees all slots | A previously borrowed producer remains callable |
| HardwareComposer::m_framebuffers | BufferItem keeps a separate strong GraphicBuffer reference; warm RemoveDisplay does not clear this map | Not all buffers enter that cache; holding one cached buffer is not a lease for the pool or other buffers |
| GraphicBuffer prepared record | Captures shared Module/nvmap handle, Process and retained backing; destruction retires via fresh dispatch admission or defers | An open CPU domain is not native completion, mapping-use safety or physical content authority |
| Escaped BufferQueueProducer | Shared facade retains Module/core; unregistering binder IDs does not revoke the existing shared pointer | Module admission does not grant permission to publish into an abandoned queue; its raw ServiceContext borrow also needs the service owner |

HardwareComposer's cache can reuse an already acquired framebuffer without a
fresh consumer acquire. RemoveLayerLocked releases/erases its cached BufferItem;
when the display/layer is already gone it skips consumer ReleaseBuffer and still
erases the cache entry. The last GraphicBuffer reference can consequently retire
at a different time from core/consumer slot clearing. No cache invalidation or
native scheduling change is made here.

## Proven defect and test-first witnesses

SetPreallocatedBuffer validated the slot, entered the global NVDRV domain,
prepared/duplicated/pinned a GraphicBuffer and unconditionally reset/published
the slot under core->mutex. It never checked is_abandoned, unlike the ordinary
RequestBuffer/QueueBuffer/Connect paths. A producer obtained before warm cleanup
could therefore refill a slot, change queue dimensions/count, signal dequeue and
return NoError after its consumer disconnected. The same result occurred when
the candidate was paused during pin preparation and warm cleanup won before
final publication. Removing binder registry ownership cannot prevent either.

Before production edits, both actual-body cases reproduced precise exit-91
witnesses: `borrowed producer republished abandoned slot` and
`prepared candidate published after abandonment`. Replay against the baseline
does not change the checkout. Crashes/timeouts are not accepted as witnesses.

## Local protocol and effects boundary

SetPreallocatedBuffer now checks is_abandoned under core->mutex twice: before
NVDRV admission/backing preparation, and at final slot publication. Disconnect
uses that same mutex. If publication wins, the original synchronous publication
and notification completes under the lock; later disconnect clears it. If
disconnect wins, the producer cannot resurrect a slot.

The first check is a no-new-effect refusal (`NoInit`): no fresh dispatch,
duplicate/pin, slot mutation or publication notification. Invalid slot validation
still precedes it and returns BadValue. An open queue refused by the global
domain still returns WouldBlock without replacing the old slot.

The second check occurs after a successfully prepared candidate. It refuses
with NoInit before slot reset, dimension/count/fence/frame metadata mutation or
notification. It is **not** a claim that no effects occurred: the candidate may
already own one duplicate/pin. Declaration order releases the scoped core lock
before the unpublished candidate's destructor, and its retirement stays inside
the outer dispatch entry. No core lock spans preparation, queue draining or
this refused candidate's retirement.

Existing prepared retirement handles the possible outcomes. An open, healthy
domain can retire that unsubmitted candidate once. A sealed domain retains the
record/Module/Process/backing without invoking unpin/free. Failed pin preparation
returns WouldBlock and quarantines uncertain owners. A partial unpin/free refusal
seals and retains the record without replay; NoInit describes queue publication
refusal, not successful cleanup. No new rollback, retry, completion registry,
range admission or scheduler continuation is introduced.

Normal positive-ID, replacement and empty-descriptor support stay intact. The
change does not route ordinary Memory writes, alter callback completion helpers,
or treat NeedSlow/enqueue as success. Existing retirement ownership remains the
authority for uncertain effects.

## Deterministic Windows validation

The new `run_abandoned_slot_tests.py` extracts actual SetPreallocatedBuffer;
ConsumerBase Abandon/AbandonLocked/FreeBufferLocked; BufferQueueConsumer
Disconnect; BufferQueueCore FreeBuffer/FreeAll/GetPreallocatedBufferCount;
SurfaceFlinger DestroyLayer/DestroyBufferQueue/RemoveDisplay; Layer destructor;
GraphicBuffer factories/destructor and Module::PrepareGraphicBufferRetirement;
HardwareComposer cache/acquire/remove methods. It uses the actual GraphicBuffer
header, ProducerDrain and OwnedRetirementQueue, real threads/mutexes and gated
pin preparation. An observer detects waiting over a held core lock without
accepting a deadlock as evidence.

Queue/provider layouts and binder registry are fixtures. Cache storage is a
std::map fixture rather than the real boost::flat_map, with fixture acquisition
and swap-interval normalization. Nvmap effects and Process/backing references are
counters/shared tokens; they are not actual KProcess, Memory/GetPointerImpl,
HostMemory mappings or a driver. The native-drain marker deliberately remains
false in cache/uncached characterization: observing CPU reference retirement
before that marker demonstrates the local protocol boundary, not actual device
use-after-free or successful native completion. OnTerminate/System owning bodies
were audited; the new suite executes their downstream warm removal chain, not
the full System shutdown or actual VI Container. The existing Conductor suite
separately exercises its extracted OnTerminate/stop callback.

Twelve paths pass three repetitions: already abandoned; pin paused across warm
cleanup; closed-domain candidate deferral; zero/throwing pin; partial free refusal;
normal/replacement/empty descriptor; invalid slot; global refusal preserving old
slot; cached original owner; uncached original owner; cached original plus a
refused late candidate. The last case proves the two GraphicBuffer wrapper owners
retire separately even when they name the same handle; candidate refusal does
not discard the original cache reference.

Ten exact controls pass three repetitions: omit both/early/final abandonment
checks; fabricate NoError; hold core lock across preparation; discard the candidate
retirement record; ignore free refusal; drop cached framebuffer ownership; retire
the refused candidate under core lock; clear slot before the final check. Every
control requires its particular exit-91 diagnostic.

Regression suites pass: existing actual GraphicBuffer/session retirement
(production repeated three times and 21 controls once); Conductor drain
(ten paths/eleven controls, three repetitions); VI session ownership
(29 paths/ten controls, three repetitions). Seven actual Windows AMD64 COFF
objects compile with GCC 13.2, existing pinned real headers and zero diagnostics:
producer, consumer, core, ConsumerBase, HardwareComposer, SurfaceFlinger and
GraphicBuffer. No full application link, game, native-driver, Linux/Android or
current-head CI acceptance is claimed. See [results and hashes](M4_P2_ABANDONED_SLOT_RESULTS.json).

```powershell
$env:PYTHONUTF8='1'
$env:CXX='C:/Strawberry/c/bin/g++.exe'
python tools/physical_coherency_tests/run_abandoned_slot_tests.py --baseline --revision 1c1348d777 --output-dir .diagnostics/m4-abslot-baseline
python tools/physical_coherency_tests/run_abandoned_slot_tests.py --repeat 3 --output-dir .diagnostics/m4-abslot-fixed
python tools/physical_coherency_tests/run_nvdrv_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-abslot-retirement
python tools/physical_coherency_tests/run_vi_conductor_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-abslot-conductor
python tools/physical_coherency_tests/run_vi_session_owner_tests.py --repeat 3 --output-dir .diagnostics/m4-abslot-session
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/nvnflinger/buffer_queue_producer.cpp --source src/core/hle/service/nvnflinger/buffer_queue_consumer.cpp --source src/core/hle/service/nvnflinger/buffer_queue_core.cpp --source src/core/hle/service/nvnflinger/consumer_base.cpp --source src/core/hle/service/nvnflinger/hardware_composer.cpp --source src/core/hle/service/nvnflinger/surface_flinger.cpp --source src/core/hle/service/nvnflinger/ui/graphic_buffer.cpp --output-dir .diagnostics/m4-abslot-production
```

## Remaining next boundary

Warm slot/cache owner retirement can still precede native drain. Cached strong
references are uneven, not a complete native use lease. Existing deferred
retirement protects closed CPU domains, while an open domain may unpin/free;
neither fact proves device completion or sustained mapping use. Further producer
routes, blocked dequeue/listener operations, raw ServiceContext/provider borrows
and complete System lifetimes remain separate obligations. A terminal queue bit
does not revoke a shared pointer or join all active producer operations.

Next: trace the uncached GraphicBuffer's final unpin/free and the cached
framebuffer release against checked GPU/presentation completion, and identify
the owning closure that must span both. Require a deterministic native-use/owner
release witness before changing shutdown ordering or retirement admission. Do not
paper over that distinction by clearing all caches earlier or retaining only one
representative buffer. F2/F3 remain open; GuestBackingAdmission remains Unknown.

Current-run exe/object/generated fixture files are cleaned after preserving
JSON/logs and checking current input/COFF hashes. The committed result bundle and
`.diagnostics/m4-abslot-results` preserve the handoff. Existing workflow gains the
new suite/artifacts; no manual run is dispatched and main is not merged.
