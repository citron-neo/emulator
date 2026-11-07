# M4 P2: post-upload helper and descriptor-boundary quarantine

2026-10-25 Windows checkpoint from `e96ea256378eadff48775cad73e1f6098b1ab370`.
Branch `codex/issue-372-fixes`; main is untouched.

## Audit result

RawUploadState closes only raw upload recording/retry. After raw upload returns,
ConfigureDraw retains accelerated owners when applicable, invokes AA, optional
FSR/FSR2, builds push constants and updates the final descriptor. An exception in
that suffix previously left the whole path available for another attempt. Waiting
one raw slot tick or rebuilding raw resources cannot prove shared helpers reusable.

FXAA, FSR and FSR2 UploadImages clear every dynamic slot, call internal Finish and
set m_images_ready only after Finish returns. SMAA also uploads shared static lookup
images; TAA changes shared parameters/frame counters and records previous-image
copies. A failing host Record/Finish/descriptor allocation can therefore leave
initialization, recording or submission effects beyond one slot. No rollback or
per-helper failure receipt establishes a safe same-object retry. This selected
repair conservatively isolates the whole Layer, preserving the current owner graph.

The callback captures were audited: FXAA/FSR/FSR2 draw callbacks hold native handles;
their initialization callbacks refer to helper members through this. SMAA retains
this and handles; TAA callbacks retain this and references to a stable member slot.
They require the existing Layer/helper owners until host/native drain. The new
handler does not replace helpers, clear raw arrays or destroy descriptors on failure.
Member declaration order is unchanged. Arbitrary concurrent helper access or
escaped helper consumers are outside this serialized-owner proof.

## Descriptor API boundary is not an error receipt

The production Device::UpdateDescriptorSets wrapper is noexcept and returns void,
as does the underlying Vulkan update. Layer::UpdateDescriptorSet uses stack
structures and has no recoverable native failure result to inspect. A void return
cannot establish descriptor validity, GPU visibility or content completion. A
native driver fault or an exception violating noexcept is not converted by the new
catch into a recoverable host error. This work adds no fake success/failure status.

Tests execute the actual Layer descriptor method with a noexcept logical-device
collaborator. Synthetic host throws immediately before and after that invocation
exercise its surrounding protocol, including already-updated descriptor state.
Those nine historical output-publication witnesses are boundary-contract evidence,
not evidence that current vkUpdateDescriptorSets throws. Actual helper descriptor
vectors can allocate/throw; their allocation boundaries are controlled separately.

## Failure and retry policy

ConfigureDraw checks existing native-borrow admission before source selection,
resource refresh or helper selection. If post_upload_failed is set, it returns
false without touching those objects, staging, descriptors or output arguments.
This remains an owner-thread preflight, not a concurrent producer lease.

The existing raw preparation and raw-only failure policy is unchanged: failures
before raw Record may retry, and raw Record failures retain their per-slot state.
After a successful raw call, the remaining suffix is enclosed by a handler:

1. Retain accelerated source ownership when selected, then run AA/FSR normally.
2. Prepare push constants locally. Perform the actual descriptor update.
3. Publish push constants and descriptor handle only after the suffix succeeds.
   A static assertion checks real PresentPushConstants copy assignment cannot throw.
4. On a propagated exception, set post_upload_failed and rethrow the original error.
   No owner is reset. The original ConfigureDraw scope-exit records CurrentTick,
   including after helper Finish split a submission.

The flag is sticky across every slot, accelerated/raw selection and raw-generation
replacement. Private raw replacement still drains old uses before freeing those raw
owners, but cannot repair shared helper state or reopen ConfigureDraw. A fresh
Layer may resume after old work is safely drained/retired. This conservative policy
can hide a layer until its owner recreates it; automatic same-object recovery is
not claimed. Errors after raw upload but before any helper effect are also isolated
because no independent effect receipt justifies a narrower recovery policy.

No extra normal submission, wait or helper initialization is introduced. Return
true still means the configure/recording suffix returned normally, not that GPU
work or output pixels completed. Internal Finish behavior, source owner renewal,
barrier ordering, selected-slot wait and readback completion policy remain intact.
The bounded policy needs no scheduler continuation or new helper packet/registry;
recoverable same-helper replay or independent escaping consumers would require
stronger effect and ownership evidence.

## Applet isolation and shutdown

AppletCaptureState is unchanged. A Layer exception passes through the actual
RenderAppletCaptureLayer handler and leaves that renderer in sticky Failed. The
connected deterministic test then substitutes a healthy fresh Layer and another
slot: the failed applet still does not Draw or export bytes. Both its Draw guard
and actual readback refusal prefix have negative controls. The full checked
readback and shutdown bodies remain separately covered by the applet regression.
A false drawable result still follows the existing skip-layer behavior; it never
grants permission to consume a failed helper image.

Layer owners survive uncertain queued or submitted prefixes. Shutdown retains its
existing close-ingress, recording/presentation worker join, checked idle and native
retirement boundary. Layer release uses existing terminal retirement instead of
submitting after closure. New semaphore cases pause actual helper recording while
shutdown closes admission and blocks joining the worker, then permit completion
before modeled checked idle and destruction. Failed idle or old-use retirement
requires terminal exit with no owner destruction/atexit. Actual Renderer worker,
retirement and owner-order regressions complement the controlled new queue model.

The handler catches host-visible failures of helper Draw/Record/Finish and boundary
work. It does not intercept arbitrary later worker callback exceptions. Existing
worker/driver fatal handling is not redesigned. Partial submission injections model
host-observed uncertainty; they are not a new native submission receipt and do not
prove that every asynchronous driver error is returned to ConfigureDraw.

## Deterministic evidence

Twenty-seven old witnesses reproduce, three repetitions each: first and warm
helper-draw enqueue failure on three slots for AA/FSR/FSR2 (18), plus synthetic
post-descriptor output failure on those combinations (nine). The saved core
candidate passed before production mutation; the final matrix additionally connects
actual applet failure/refusal to the actual Layer exception.

Candidate and production suites each pass 288 regular paths, 36 terminal paths
and ten exact controls, all repeated three times. AA has 26 cases, FSR and FSR2
35 each; every case runs against three selected slots. Each helper has four terminal
cases per slot: closed entry, failed idle, failed retirement and active-worker
failed idle. Regular cases include shared first initialization, warm helper paths,
internal Finish before/partial/after submission, enqueue before/after and draw
prefix failures, helper descriptor allocation, descriptor boundary before/after,
all-slot and accelerated refusal, raw-generation refusal, fresh Layer recovery,
local output publication, applet substitution and owner/descriptor/worker gates.

Controls omit entry isolation, fail to publish failure, isolate only one slot,
reset on raw-generation replacement, destroy an owner inside catch, publish output
early, drop the latest tick, remove either applet refusal guard or add a normal
Finish. Each must fail at its named runtime assertion with exit 91. Terminal
children require their exact fail-stop stage and exit 86; crashes/timeouts/build
failures are never accepted as witnesses.

Actual extracted methods: complete Layer ConfigureDraw, UpdateRawImage callback,
UpdateDescriptorSet, refresh/wait/release/destructor, raw declarations and size
offset; complete FXAA/FSR/FSR2 Draw and UploadImages; complete applet render handler
and actual readback refusal prefix. Native helper allocations/handles, command
queues, device execution/pixels, source copy/unswizzle, helper descriptor vectors,
math and applet allocation/blit/successful-readback tail are fixtures. TAA/SMAA
source is audited, not executed as those helper types in this suite. There is no
real Vulkan queue, full application runtime, actual guest Memory/GetPointerImpl or
target-device/game pixel acceptance claim.

Regressions pass, each three repetitions: raw upload (75 regular/27 terminal/eight
controls), raw resource publication (66/four/six), AA retirement (25/24/ten), applet
content (21/nine/six), native source ownership/completion (26 regular/seven protocol/
seven terminal/fourteen controls), and ten actual renderer/worker shutdown paths.
The prefix-specific regression extraction now preserves the actual post-upload
handler instead of dropping its catch when cutting the function after AA.

Three real Windows GCC 13.2 AMD64 COFF production objects compile with zero
diagnostics: Layer, BlitScreen and WindowAdaptPass, using real pinned headers and a
real glslang-generated presentation shader. No application link, MSVC, Linux or
Android build, device/game run or current-head CI acceptance is claimed. The
integration workflow runs the fixed suite; none was manually dispatched locally.

[Results, input hashes, commands and cleanup](M4_P2_POST_UPLOAD_HELPER_QUARANTINE_RESULTS.json)
retain exact manifests and current-run log/JSON evidence. Temporary executables,
objects and generated sources/headers are removed after scope/hash verification.
Ignored local logs do not travel through Git; committed results identify rerun inputs.

## Next boundary

Audit WindowAdaptPass final composition Record and BlitScreen destination Frame
ownership when failure happens after every ConfigureDraw succeeded. The Layer
handler cannot govern that later output stage. Preserve independent applet
quarantine, raw state and native completion contracts. HostMemory remap/protection
remains high risk; M4 incomplete, F2/F3 open and GuestBackingAdmission Unknown.


Follow-up: [final composition and target Frame](M4_P2_COMPOSITION_FRAME_RETIREMENT.md)
adds checked retirement for final enqueue errors and closes the reset-fence
presentation error boundary. Helper isolation above remains independently sticky;
a successful error drain never turns failed applet content into readable pixels.
