# M4 P2: remaining actual producer and ownership boundaries

The [Frame recreation follow-up](M4_P2_FRAME_RECREATION_PUBLICATION.md) prepares
complete image/view/framebuffer and Blit pass candidates before publication,
retains old parents/children and metadata on failure, preserves synchronization
identities, records per-Frame view format and bounds shrunk slot indices. Applet
Failed is independent. Fixed pool versus recreated swapchain count,
free/in-flight bookkeeping and presentation-worker failure remain separate from
this selected-Frame transaction; no runtime pool resizing is inferred.

The [final composition follow-up](M4_P2_COMPOSITION_FRAME_RETIREMENT.md) retires a
failed enqueue's checked prefix before target/source owners may unwind, preserves
applet Failed, and prevents an unsettled dequeued presentation Frame from being
reused as a cancellation success. Callback/unknown retirement stays terminal.
Pre-ConfigureDraw resource-change publication and in-place RecreateFrame failure
remain separate; a complete final composition is not a certificate for them.

The [post-upload helper follow-up](M4_P2_POST_UPLOAD_HELPER_QUARANTINE.md) adds
sticky whole-Layer isolation after uncertain helper/descriptor-boundary failure,
with retained owners and transactional output parameters. Raw generations cannot
reset shared helper failure; applet Failed remains independent. Descriptor API
update is noexcept void and has no recoverable native status. Final composition
recording and destination Frame failure are the next separate boundary.

The [raw upload follow-up](M4_P2_RAW_UPLOAD_RETRY.md) bounds pre-record retries,
per-slot uncertain-prefix quarantine, native callback failure and generation
replacement after old-use completion. It preserves applet Failed isolation and
never treats Recorded/enqueue as native content completion. Remaining helper,
descriptor and output failures after upload return still require independent audit.

Source audit: 2026-10-23 (Asia/Singapore), following the NVDRV dispatch update.
This is an acceptance inventory, not a certificate that all writers are known.

The [raw resource publication follow-up](M4_P2_RAW_RESOURCE_PUBLICATION.md) builds
staging and every slot locally, then commits the complete tuple after old-use
completion. Creation failures preserve old owners/metadata and stride participates
in sizing. Later raw upload/content and helper recreation remain separate.

The [applet content/readback follow-up](M4_P2_APPLET_CONTENT_READBACK.md) quarantines
failed prefixes and closes applet ingress before terminal drain. Confirmed native
retirement permits Layer disposal without submitting a stale current tick after
worker join. Device content evidence and partial raw recreation remain separate.

The [applet Frame publication follow-up](M4_P2_APPLET_FRAME_PUBLICATION.md) keeps
creation prefixes local and commits a complete native tuple without throwing.
Failed creation leaves empty state for retry/readback refusal. Pixel readiness,
post-publication Draw effects and partial raw recreation remain separate.

The [AA helper retirement follow-up](M4_P2_AA_HELPER_RETIREMENT.md) completes the
already-recorded graphics prefix and host recording before warm AA/raw replacement.
It covers later final sampling after internal Finish and failed wait retention;
partial applet/raw publication and other helper lifetimes remain separate.

The [capture/native helper follow-up](M4_P2_CAPTURE_NATIVE_BORROW.md) repairs local
capture/download and upload staging unwind while asynchronous users may survive.
Normal paths retain actual completion/host drain semantics; uncertain failures
stop without unwinding owners. Other helper replacement and partial applet frame
publication remain explicit open items; no complete guest admission follows.

The [accelerated native source follow-up](M4_P2_ACCELERATED_NATIVE_SOURCE.md)
retains exact image/view versions across cache metadata eviction and internal
Finish, using accepted submissions and checked timeline/fallback completion.
Presentation output keeps a separate checked fence boundary. Other native
helper/capture-output owners and all guest content/admission obligations remain open.

The [retired-session source admission follow-up](M4_P2_SESSION_SOURCE_ADMISSION.md)
closes new nvdisp source capture after shutdown/stale retirement, including slot
reuse, while preserving retained resources. Actual source stamps precede session
validation and are never refreshed after it. Native accelerated-view ownership,
generic raw session/GMMU admission and HostMemory alias lifetime remain separate.

The [delayed source mapping-use follow-up](M4_P2_FRAME_MAPPING_USE.md) now guards
bounded actual CPU copies and cache selection against mapping intent/reuse and
final session retirement. It does not close permanent session re-admission via
escaped producers, native image ownership after cache selection, or host-alias
mapping/protection lifetime. The domain is conservative and device-wide; F2/F3
and guest admission remain open/Unknown.

The [source-address translation follow-up](M4_P2_SOURCE_ADDRESS_TRANSLATION.md)
repairs lookup's mismatched mutex and rejects already-unmapped zero addresses.
After-snapshot unmap/free remains an explicit counterexample to mapping continuity;
neither object retention nor this linearized snapshot authorizes delayed reads.

The [per-frame source packet follow-up](M4_P2_FRAME_SOURCE_PACKET.md) now retains
the actual GraphicBuffer wrapper and immutable descriptor through composer,
nvdisp, guest-fence actions and synchronous source callback. Mapping-use epochs
and native completion remain separate; packet presence is not guest admission.

The [deferred-frame action-owner follow-up](M4_P2_SYNCPOINT_OWNER_RETIREMENT.md)
fixes captured-owner destruction under SyncpointManager guard, for completion
and cancellation, without moving callback execution. Per-frame source ownership
is still absent from the HwcLayer/FramebufferConfig handoff and remains next.

The [backing/cache/native completion audit](M4_P2_BLIT_NATIVE_IDLE.md) repairs
BlitScreen recreation's late-present/unchecked-native-idle boundary. It does not
authorize every VI final unpin/free. Deferred guest-fence payloads still require
an owner handoff spanning source reads/mappings and relevant native uses.

The [warm slot/escaped-producer follow-up](M4_P2_ABANDONED_SLOT_PUBLICATION.md)
prevents SetPreallocatedBuffer from resurrecting an abandoned queue, including
when Disconnect wins during backing preparation. Candidate retirement preserves
the existing effects/owner boundary; cached framebuffers are separate strong
owners. Native-use drainage and final backing retirement remain unproved.

The [warm VI Conductor follow-up](M4_P2_VI_CONDUCTOR_DRAIN.md) now closes and
drains timing/VSync CPU accesses before existing warm cleanup, with synchronized
event unlink that remains available after closure. This does not move layer/slot
retirement past native GPU drain or cover already escaped producer/manager uses.

The [VI terminal-publication follow-up](M4_P2_VI_TERMINAL_PUBLICATION.md) prevents
cold/paused lazy initialization and retained-facade access from reopening VI after
OnTerminate. Its same-mutex gate and nullable caller checks are terminal refusal,
not drainage of existing layers, Conductor, escaped managers or native work.

The [VI partial-session owner protocol](M4_P2_VI_SESSION_OWNERSHIP.md) follows the
capture audit: creation/raw initialization is counted; failed prefixes are hidden
Unknown records with prepared Process/backing/Module/VI/slot-provider retention.
Destroy checks Close and never erases/releases session ownership on refusal.
Nested admission can still lead conservatively to Unknown; no continuable success,
native completion or ordinary Guest-backed admission follows from this repair.

The [VI SharedBufferManager owner/capture follow-up](M4_P2_VI_CAPTURE_CLOSURE.md)
audits creation, physical pool ownership and destruction. Its raw capture entry
now joins the existing NVDRV producer domain, pairs pool publication with the
manager mutex and refuses empty/cancelled capture before stores. CreateSession's
partial records/nested admission and DestroySession's unchecked Close result
remain separate unresolved transitions. The pool descriptor is not a complete
Process/backing/alias/native lifetime bundle; Guest admission remains Unknown.

Follow-up: the [direct SurfaceFlinger composition audit and repair](M4_P2_SURFACE_COMPOSITION_CLOSURE.md)
counts its synchronous entry in the existing Kernel NVDRV domain. The fresh
caller/raw-alias table there distinguishes real callers from dormant public
routes and explicitly leaves deferred guest-fence callbacks, VI raw physical
stores, HID/IRS/time shared views and native GPU work open. GetDevice itself
continues to return an unqualified borrow; no accessor-wide capability is claimed.

Follow-up: [GraphicBuffer/session retirement](M4_P2_GRAPHIC_SESSION_RETIREMENT.md)
now supplies the prepared local owner and deferred-retirement protocol below.

| Actual route | Current fact | Required next capability |
| --- | --- | --- |
| NVDRV Module Open/Ioctl1/2/3/Close/QueryEvent | Shared sustained synchronous dispatch closure, with outer-before-inner cohort ordering | Real runtime and complete caller/owner lifetime; native work remains separate |
| NVDRV::Initialize -> Container::OpenSession | Ordinary Initialize now shares dispatch admission through local success reply; exceptions seal without fabricated Success | Direct Container use and full Process/session/backing lifetime still require continuous ownership; the raw accessor is not a capability |
| NVDRV::~NVDRV -> Container::CloseSession | Prepared session record now closes under admission or defers with Module/Process/backing retained; unknown id/failure never retried | Trusted terminal disposal and complete System/Host1x/native ownership; no Busy/skip cleanup shortcut |
| SurfaceFlinger::CreateBufferQueue -> BufferQueueProducer -> android::GraphicBuffer | Shared Module provider, counted slot publication and prepared handle/Process/backing record before duplicate/pin | Complete alias/native coverage beyond this buffer publication transaction |
| android::GraphicBuffer destructor | Counted or deferred unpin/free; uncertain owners retained and no partial-effect replay | Native completion/visibility and final allocator/device retirement, separate from CPU callback return |
| Direct Module GetDevice, Host1x::PushEntries, GPU::PushCommandBuffer and CDmaPusher::ProcessEntries | Public borrowed/direct routes are not automatically covered by Module dispatch | Inventory actual callers and require lifetime-bearing access/admission instead of an escaped raw/shared facade |
| Memory::GetPointer/GetPointerSilent/GetSpan and direct prepared/HostMemory access | Managed facade store counting does not revoke previously escaped borrows | Census plus borrow retirement/revocation or a proven whole-guest stop spanning every user |
| Native NCE and unchecked fastmem | HAS_NCE managed-write closure explicitly unsupported; no native/store signal parking proof | Actual backend halt/ack, retained running owners and validated absence of outstanding stores; do not add blocking signal-handler bookkeeping |
| Audio renderer and AudioManager | The two actual counted worker subsets exist; direct service/lifecycle operations remain separate | Prove all session/publication/retirement dependencies, plus native sink/backend access, before claiming complete Audio exclusion |
| GPUThread, scheduler continuations, recorded/native transfer and shader/image work | Terminal ingress and checked completion/failure retention slices exist | A sustained reversible GPU closure that accounts for suspended channels, deferred callbacks, native completion and visibility |
| Unknown backing/PageTable/alias/native owners | Local checked holders/fence and mapping-failure owners are retained in several paths | Durable whole-owner bundle, including allocator/device, backing, aliases and Process/PageTable, through completion or explicit terminal retirement |

Concrete source pointers:

- `src/core/hle/service/nvdrv/nvdrv_interface.cpp`: Initialize and ~NVDRV.
- `src/core/hle/service/nvdrv/core/container.cpp`: OpenSession and CloseSession.
- `src/core/hle/service/nvnflinger/surface_flinger.cpp`: CreateBufferQueue.
- `src/core/hle/service/nvnflinger/buffer_queue_producer.cpp`: creation of GraphicBuffer.
- `src/core/hle/service/nvnflinger/ui/graphic_buffer.cpp`: constructor and destructor directly mutate nvmap.
- `src/core/memory.h`, `src/core/managed_guest_writes.h`: escaped views and unsupported HAS_NCE gate.
- `src/video_core/gpu_thread.cpp`, `src/video_core/renderer_vulkan/vk_scheduler.cpp`: queued/continuation/native ownership.

## Audio/Opus classification found during this audit

Do not equate every Opus worker buffer with ordinary guest physical backing.
OpusDecoder::Initialize allocates shared_buffer with make_unique; decode input
and DSP output spans refer to that private allocation. The ordinary hardware
Opus service receives OutBuffer through CMIF scratch storage, and CMIF later
copies it via HLERequestContext WriteBufferB/C. Relevant sources are
`src/audio_core/opus/decoder.cpp`,
`src/core/hle/service/audio/hardware_opus_decoder.cpp` and
`src/core/hle/service/cmif_serialization.h` (OutBuffer argument preparation and
WriteOutArgument).

This narrows the observed path; it does NOT close the Opus/service lifecycle
domain. Public decoder spans, raw DSP protocol pointers, teardown dependencies,
incorrect acknowledgements and the later HLE copy still need their own proof.
No native worker stop or absence of all other aliases follows from private
scratch allocation. Similarly, renderer workbuffer is currently a private
allocation, while CircularBuffer/Aux commands make actual managed guest stores.

## Completion gate

The new local retirement protocol addresses the two destructor routes above.
P2 still cannot be completed merely by adding counters to other destructors. A
closed destructor must either run under a legal owned teardown protocol or
transfer every still-needed owner to durable deferred retirement. Counting
ordinary ioctl return cannot supply native GPU visibility, and one-time owner
snapshots cannot replace continuous publication closure.

Keep GuestBackingAdmission Unknown until all producer families have sustained
close-and-drain evidence and the necessary Unknown owner bundle is held. The
subsequent actual guest-backed Vulkan test is still a separate M4 phase; a
controlled arena or passing focused Windows compilation cannot replace it.
