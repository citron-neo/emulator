# M4 physical alias coherency: WIP handoff

## 2026-10-28: physical alias/PTE bypass and exact refusal evidence

[Physical alias refusal checkpoint](M4_PHYSICAL_ALIAS_REFUSAL.md) and
[exact results](M4_PHYSICAL_ALIAS_REFUSAL_RESULTS.json): 24/24 native Windows alias
processes pass, plus three mapping-owner regression positives and two strict
production objects. Actual old-pointer writes, direct VirtualProtect and saved-VA
remaps demonstrate why metadata closure is not physical isolation. Six-entry
closure refusals and six exact negative controls are distinguished from production
Windows unsupported refusal. No production source repair or Windows enrollment.
Physical freeze remains Open, GuestBackingAdmission Unknown; CI deferred.
Synchronized KPageTable changes mean prior native-owner execution is historical,
not freshly validated at this head. Main and other worktrees remain untouched.


## 2026-10-27: closeout status verified without production refactoring

[Current gate status](M4_CLOSEOUT_STATUS.md) and [exact results](M4_CLOSEOUT_STATUS_RESULTS.json)
now distinguish retained source-identical native-owner evidence from fresh real
Windows mapping execution. A paused last-owner writer survives backing replacement;
old epoch invalidation, distinct replacement bytes and final native release pass.
The omitted-invalidation counterfactual is detected x3; total mapping processes
21/21. Metadata-ledger checks pass with 11 assertion controls. No new production
defect or source repair. Physical freeze and native GPU completion remain Open;
GuestBackingAdmission Unknown. CI deferred, issue branch only.


## 2026-10-27: native Windows mapping effects and moved-owner exports

After the FFR/upstream synchronization, this checkpoint repairs a demonstrated
post-move raw-export gap in HostMemory's four pointer getters. Real Windows
alias/remap/protection/retained-allocation tests and per-getter counterfactuals
retain the unsupported-freeze refusal boundary. The cold native owner bootstrap
uses a private backend that refuses GPU work after upstream removed RendererNull.
See [scope, diagnosis and remaining gates](M4_WINDOWS_MAPPING_OWNER.md) and
[exact local results](M4_WINDOWS_MAPPING_OWNER_RESULTS.json). CI remains deferred;
no main or FFR worktree changes. Physical freeze, active guest adoption and native
GPU completion remain open; GuestBackingAdmission remains Unknown.


## 2026-10-26: restricted real native owner foundation

The next gate now has Windows native evidence using a private owned System,
actual KProcess/KEvent and actual ProcessCohortStop, without test-scope borrowed
foundation objects. Positive, quarantined Unknown and unpolled closures pass x3;
two foreign-release fail-stop controls and the exact missing-foundation native
counterfactual also pass x3. Three final production units compile separately to
real Windows objects. Details, local build exceptions and all evidence are in
[the native owner record](M4_NATIVE_OWNER_AND_REMAINING_GATES.md) and
[results](M4_NATIVE_OWNER_RESULTS.json).

This accepts the restricted cold combination only. Existing active guest/NVDRV
adoption remains unproved; mapping/alias freeze is next, followed by native GPU
completion with retained backing, then the first restricted Vulkan Copy. Do not
expand passed controlled compositions or infer mapping/GPU authority from this
owner result. Ordinary DeviceMemory remains untracked, all unproved aliases
refuse, F2/F3 remain Open and GuestBackingAdmission remains Unknown. CI is deferred;
this task modifies only the issue branch, not main.

## 2026-10-26: one active NVDRV/Host1x/cohort/GPU owner graph

From `ae5e649630`, actual Module/session retirement, nvhost_ctrl callback,
SyncptIncrManager/SyncpointManager, ProcessCohortStop and ThreadManager now run
in one controlled active scenario. Eight schedules/eight exact controls pass x5;
nvhost regression and original Audio/Memory/GPU composition pass x3. Seven fresh
real Windows production units compile to AMD64 COFF objects. No production
defect was reproduced; only test emission/composition and docs change.
See [active graph, witnesses and limits](M4_P2_NVDRV_COHORT_GPU_COMPOSITION.md)
and [retained results](M4_P2_NVDRV_COHORT_GPU_RESULTS.json).

Actual retirement counting spans final Module destruction and raw callback join.
GPU tail/join remains independent of cohort acknowledgement. BackendsStopped with
active raw Host1x/GPU work, and a pre-resolved escaped alias write, are mandatory
negative coverage witnesses; neither grants admission. Unknown deferred owners
have no newly trusted terminal disposal. This closes the named combined-fixture
test gap, not all P2/native/service/mapping obligations. Next: restricted real
KProcess/KEvent/System-backed composition with continuous mapping/alias ownership
and native completion refusal before any Vulkan Copy acceptance. Main untouched,
CI deferred, P2 incomplete, F2/F3 Open, GuestBackingAdmission Unknown.

## 2026-10-25: nvhost_ctrl review and active final-owner composition

Reviewed `44c6fbdccc`: no new production defect found in lock order, repeated
cancellation or final-owner release. Tests now cover repeated Clear/rearm,
concurrent Clear, final facade capture release after Host1x completion/cancel,
and real retirement-entry counting through blocked final Module destruction.
Sixteen normal cases, one terminal case and nine exact controls pass x10;
the three historical failures still reproduce x3. No production source changes.
Previous seven Windows production-unit hashes remain identical; no new object
build is asserted. See [review, schedules and next boundary](M4_P2_NVHOST_CTRL_OWNER_COMPOSITION.md)
and `owner_review_44c6fbdccc` in the
[results record](M4_P2_NVHOST_CTRL_CALLBACK_RESULTS.json).

M4 P2 now has a separately active actual NVDRV/session/retirement -> Host1x
callback -> KEvent owner graph. A drained dispatch counter with a live Host1x
callback is an explicit counterexample, not admission. Next: combine this graph
with actual ProcessCohortStop and GPU ThreadManager in one restricted session;
retain downstream-callback and escaped-alias negative controls. Native KProcess/
KEvent scheduling, sustained System ownership, mapping freeze and native completion
remain open. Main untouched, CI deferred, P2 incomplete, F2/F3 Open and
GuestBackingAdmission Unknown.

## 2026-10-25: nvhost_ctrl local revalidation recorded; CI deferred

Revalidated exact source `4a66873eb6aaa01c70b86ce04baeda346d5b8f6a` on
Windows: ten normal schedules x20, one injected terminal cancellation case x20,
and four exact assertion controls x20 pass. Syncpoint, NVDRV retirement/dispatch
and OwnedRetirementQueue positive suites pass x10, with their required controls
(syncpoint controls x10; other regression controls x1). Seven real production
AMD64 objects compile. No new lifecycle defect was reproduced. The first fixture compile
failed on generated source path handling, then the unchanged command passed after
output directories existed; preserve that as infrastructure evidence only.
All 148 generated exe/object/fixture source files were removed after hash/path
checks. See [local evidence and limits](M4_P2_NVHOST_CTRL_CALLBACK_LIFETIME.md)
and `local_revalidation_4a66873eb6` in the
[results record](M4_P2_NVHOST_CTRL_CALLBACK_RESULTS.json).
This update changes documentation only. CI is deferred by explicit user request;
do not trigger a workflow or change Actions settings as part of this update.
Main remains untouched; native/Android runtime is unvalidated, P2 incomplete,
F2/F3 Open and GuestBackingAdmission Unknown.

## 2026-10-25: nvhost_ctrl callback lifetime repair validated on Windows

Resumed from `31b25c128f` on `codex/issue-372-fixes`. Three historical
assertion witnesses reproduce queued callback use after device destruction,
KEvent release during Signal, and Clear overtaking an active Signal (each x5).
Only `nvhost_ctrl.cpp` changes in production: cancel/join every registered
Host1x action before freeing any event, and join Signalling callbacks before
Clear publishes Cancelled. Existing queue guard and Module/session owners stay
unchanged. Candidate 10 normal cases/one terminal case/four controls pass x5;
fixed source passes x10. Syncpoint, NVDRV retirement and dispatch regressions
pass x5; seven real Windows x64 production objects compile successfully.
See [repair and evidence limits](M4_P2_NVHOST_CTRL_CALLBACK_LIFETIME.md) and
[retained results](M4_P2_NVHOST_CTRL_CALLBACK_RESULTS.json). This supersedes the
unvalidated status below. Main and dependency pins remain untouched; no workflow
was changed or manually dispatched. Native kernel/Android runtime and complete
P2 owner closure are not established; GuestBackingAdmission remains Unknown,
F2/F3 Open. Keep subsequent work within separately authorized scope.

## 2026-10-25: STOP checkpoint - nvhost_ctrl owner tests are unvalidated drafts

User requested saving progress and stopping after repeated Daybreak warnings.
Last validated production head remains `33ca3eb2ad540b669deed958eedb751be9cafb20`.
Only draft tests/fixture/unapplied candidate were added for Module/session/
OwnedRetirementQueue -> NVDRV dispatch -> Host1x queued raw callback lifetime.
They have not been compiled or run; no new runtime witness or fix is accepted.
No production source/workflow/main changes or new temporary exe in this work.
See [exact checkpoint and resume commands](M4_P2_NVHOST_CTRL_CALLBACK_WIP.md).
Resume by inspecting/fixing the test integration, then historical/candidate
deterministic tests before any production patch or Windows validation. Scope
stays strictly nvhost_ctrl/callback/KEvent/Module/session; global P2 and admission
remain Unknown. This checkpoint supersedes any implication that this new repair
has already been verified.

## 2026-10-25: actual Audio service raw-target terminal closure

From `7fc738baf7`, real AudioIn/Out service constructors/default destructors and
unique impl ownership reproduce target retirement during active raw callbacks
(each x3), plus late setter success after close. Cold Audio server setup now
installs a pre-owner-release drain. ServerManager requests IPC stop and joins its
workers first, keeps ports/factories/sessions alive, joins Audio through the hook,
then releases targets. Service work holding the actual target mutex demonstrates
why Audio cannot join before service work drains. No System/member reorder.
Audio close shares the registration lock, releases it before join, supports
sequential repeats, and refuses unwinding on failed join/hook. Candidate/fixed
13 normal/four terminal/five controls pass x3; nine real Windows production COFF
units compile with zero diagnostics. Opus initialized at its existing pinned
gitlink only. Audio/owner-composition/terminal regressions pass.
See [actual owner proof and limits](M4_P2_AUDIO_SERVICE_TARGET_DRAIN.md) and
[results/commands/cleanup](M4_P2_AUDIO_SERVICE_TARGET_DRAIN_RESULTS.json).

Next: bounded NVDRV Module/session/OwnedRetirementQueue owner composition through
dispatch and downstream Host1x queued/callback last use. Local repair scope stays
frozen. Audio terminal target safety does not certify native sink/DSP, escaped
service/client references, arbitrary Kernel teardown or whole-service reversible
quiescence. P2 incomplete, HostMemory high risk, admission Unknown, F2/F3 open.
Keep main untouched; cleanup current temporary binaries, retain results and push
the issue branch for continuation.

## 2026-10-25: joint P2 callback / Memory / GPU owner closure evidence

From `d7c6489ead`, local repair scope stays frozen. No production source changes.
New tests assemble actual full AudioManager and ThreadManager owners with actual
cohort/core/roster and Write8/GetPointerImpl/PageInfo bodies in one fixture System.
Nine schedules x3 cover backend UnlockThread, pre-resolved ordinary store,
active GPU stop, queued cancellation, partial callback failure, timeout and late
seal. Six exact controls x3 reject missing drain/admission/strong owner/failure
seal. Session survives active operations and worker join via registered callback;
cohort then retains backing/process owners after Session release. Real KProcess,
backend, HostMemory remap and native Vulkan remain unproven collaborators, not
certified by these green tests. Eight real Windows production COFF units compile
with zero diagnostics; related cohort/Memory/Audio/GPU regressions pass.
See [scope and next owner chain](M4_P2_OWNER_CLOSURE_COMPOSITION.md) and
[results/commands/cleanup](M4_P2_OWNER_CLOSURE_COMPOSITION_RESULTS.json).

The tested Session is a prepared shared packet. Actual AudioOut/In registered
callbacks bind raw this to service-owned unique impls; that lifetime composition
is still Unknown. The escaped-alias case deliberately stores after managed close
and requires continued global refusal. Before claiming the Audio row covered,
join its actual service target ownership with Audio worker/closure lifetime.
Next device target: actual NVDRV Module/session/retirement owners through an
actual dispatch and downstream Host1x queued device/callback lifetime. Keep direct
exports and unaccounted aliases Unknown. Do not turn idle domain drainage,
completed ioctl, shutdown wake or terminal worker join into global reversible
closure. P2 remains incomplete; HostMemory high risk, M4 admission Unknown and
F2/F3 open. No main merge; preserve logs/results and push this issue branch.

## 2026-10-25: bounded Presentation Pool complete; local repair scope frozen

From `c3fa0f0d4b`, three historical acquire/index, failed-idle and parent-surface
owner witnesses reproduce before edits. Fixed-address pool remains stable across
swapchain count changes; no pool resize is introduced. Acquisition must have a
valid success receipt before indexing; old swapchain release requires checked
idle; replacement surface outlives old child retirement. Surface loss retries
only before copy starts, never replaying an already consumed Frame semaphore.
Candidate/fixed suites: eight regular, seven terminal, five controls x3. Five
related regression suites pass; four real Windows COFF units compile with zero
diagnostics. See [bounded audit](M4_P2_PRESENTATION_POOL.md) and
[results/commands/cleanup](M4_P2_PRESENTATION_POOL_RESULTS.json).

Next: follow the [consolidated owner coverage and four-step admission ledger](M4_PRODUCER_COVERAGE_AND_ADMISSION.md).
New local repairs now require a reproduced critical owner failure. Concentrate
on actual Process/Memory/NVDRV/Host1x/Audio/GPU/Rasterizer/Presentation close,
acknowledgement and sustained retention together. Persistent WSI retry progress,
external getter synchronization, escaped aliases, independent services,
HostMemory remap/protection and native NCE remain explicit Unknown obligations.
Whole-guest quiescence stays the first route; no signal-handler waiting or new
per-access policy. No guest-backed admission, F2 authority or main merge is enabled.
The first future integration target remains one restricted real KProcess-backed
Vulkan Copy, with real native completion and safe refusal/Unknown retention.

## 2026-10-25: transactional Frame recreation, Blit publication and slot shrink

From `7e8c2d1b8c`, eight old Frame/Blit publication/parent-order/shrink witnesses
reproduce. RecreateFrame now prepares image/view/framebuffer locally and swaps
only those resources after success; old children retire before parents and
cmdbuf/render_ready/present_done identities remain unchanged. Blit prepares its
pass and requested Frame before committing filter/format/count/layers/index;
failed creation preserves the previous publication. CreateFramebuffer shares the
same boundary. New per-Frame image_view_format prevents same-size pooled slots
from skipping format repair, including reverse reuse against a differently keyed
Blit pass. Six shrink combinations normalize image_index after commit; zero count
refuses before effects. Applet construction can retry, while Failed Draw remains
sticky. Candidate/fixed 38 normal/five terminal/eleven controls pass three repeats,
with composition/helper/capture/applet/idle/shutdown/native-source regressions and
four zero-diagnostic Windows COFF units. See [scope/next audit](M4_P2_FRAME_RECREATION_PUBLICATION.md)
and [results/commands](M4_P2_FRAME_RECREATION_PUBLICATION_RESULTS.json). Current-run
artifacts are cleaned; main untouched. Next: fixed presentation pool versus
recreated swapchain count, free/in-flight bookkeeping, and CopyToSwapchain/
PresentThread failure while a Frame remains borrowed. SetImageCount currently
runs at construction; RecreateSwapchain does not resize the Frame pool. HostMemory high risk; M4
incomplete, F2/F3 open and GuestBackingAdmission Unknown.

## 2026-10-25: final composition and borrowed target Frame retirement

From `7b2225e854`, twelve low-level Frame-borrow witnesses reproduce. Final
WindowAdaptPass enqueue errors now keep Frame/pass/source scope alive through
actual checked FinishNativeBorrow before rethrowing. Failed retirement and worker
callback errors remain no-unwind terminal boundaries. Applet retains its existing
Failed quarantine even when composition completed during error retirement; no
readback or replay. A dequeued presentation Frame has a reset fence and no proven
cancel/recycle receipt: Composite terminates on draw/flush/present error instead of
continuing with an unsettled pool slot. Blit and direct Window entry preflight
closed admission before metadata/source access. Normal submissions/index behavior
are unchanged; no Frame member reordering or completion registry. Candidate/fixed
84 return paths, 114 terminal paths and eleven controls pass three repeats, with
helper/capture/applet/source/idle/shutdown regressions and three zero-diagnostic
Windows COFF units. See [scope and continuation](M4_P2_COMPOSITION_FRAME_RETIREMENT.md)
and [results/commands](M4_P2_COMPOSITION_FRAME_RETIREMENT_RESULTS.json). Current-run
artifacts are cleaned; main untouched. Next: pre-ConfigureDraw Blit resource-change
publication and PresentManager::RecreateFrame image/view/framebuffer replacement,
metadata/index shrink and allocation failure. HostMemory high risk; M4 incomplete,
F2/F3 open and GuestBackingAdmission Unknown.

## 2026-10-25: post-upload helper/descriptor failure and Layer quarantine

From `e96ea25637`, 27 controlled retry/output witnesses reproduce. Shared AA/FSR
initialization and temporal helper state prevent a slot-local recovery claim.
ConfigureDraw now closes its post-upload path across the whole Layer after a
propagated helper/boundary exception, keeps owners and latest tick through drain,
and leaves raw-generation replacement unable to reopen it. A fresh Layer can
resume; applet Failed remains separately sticky even after swapping in that
healthy Layer. Push constants/descriptor handle publish only on successful
Configure return. The noexcept void Vulkan descriptor update has no recoverable
failure receipt; synthetic boundary faults are labeled separately. Normal
submission counts are preserved. Candidate/fixed deterministic gates and controls,
raw/helper/source/applet/shutdown regressions and three real Windows COFF objects
pass; see [audit and next boundary](M4_P2_POST_UPLOAD_HELPER_QUARANTINE.md) and
[results/commands](M4_P2_POST_UPLOAD_HELPER_QUARANTINE_RESULTS.json). Main untouched;
current-run temporary artifacts are cleaned after hash verification. Next: final
WindowAdaptPass composition recording and BlitScreen destination Frame ownership
on failure after ConfigureDraw has succeeded. HostMemory high risk; M4 incomplete,
F2/F3 open and GuestBackingAdmission Unknown.

## 2026-10-25: raw upload recording eligibility and failed-prefix quarantine

From `3165ece04d`, nine uncertain first/warm upload retry witnesses reproduce.
Candidate tests pass before production mutation. Each raw slot now tracks
Empty/Recording/Recorded/Failed on the serialized GPU owner: source preparation
can retry, but Record failure quarantines the slot and propagates the exception.
Its staging cannot be rewritten or image sampled via another raw attempt. Other
slots progress; a complete new raw generation can recover only after old-use
completion. A same-key refresh does not silently clear failure. Recorded means
enqueue returned, never native/pixel completion. Worker callback errors take the
existing no-unwind terminal boundary. Applet Failed remains sticky; raw generation
replacement does not grant permission to retry that applet. New slot/shutdown
gates, existing raw tuple, AA, mapping, applet and renderer regressions plus three
real Windows COFF units are recorded in [scope/continuation](M4_P2_RAW_UPLOAD_RETRY.md)
and [results](M4_P2_RAW_UPLOAD_RETRY_RESULTS.json). Main untouched; current-run
executables, objects and generated fixtures are cleaned after hash verification.
Next: remaining helper/descriptor and output failures after raw upload returned,
without treating upload scheduling as content readiness. HostMemory high risk;
M4 incomplete, F2/F3 open, GuestBackingAdmission Unknown.

## 2026-10-24: raw resource candidate publication and retry

From `3ca2af23f6`, sixteen raw partial-publication/retry/stride witnesses reproduce.
Candidate then production retain old staging/all slot image-view pairs/ticks/AA
and metadata until the complete replacement exists and old uses finish. No-throw
swaps publish the tuple, with stride in the sizing key and zero new ticks. Failed
creation cleans only the unused prefix; closed entries refuse and uncertain wait/
idle retain owners. Three real Windows production COFF units compile with zero
diagnostics; raw, AA/content/mapping/source/shutdown regressions pass. See
[protocol and next scope](M4_P2_RAW_RESOURCE_PUBLICATION.md) and
[results/commands](M4_P2_RAW_RESOURCE_PUBLICATION_RESULTS.json). Current-run artifacts
cleaned; issue branch pushed; main untouched. Next: later raw upload/content and
helper/descriptor recreation boundaries; do not equate allocation with initialized
content or re-enable a failed applet Draw. HostMemory high risk, M4 incomplete,
F2/F3 open and admission Unknown.

## 2026-10-24: failed applet Draw readback quarantine and terminal release

From `f892213b0a`, ten failure/export/replay/release witnesses reproduce. Applet
Empty/Recording/Recorded/Failed/Closed state now distinguishes normal Draw return
from checked readback completion. A failed prefix remains quarantined and retains
owners; no retry or bytes export, even after prefix completion. Shutdown closes
capture before drains. Layer destruction uses the successful joined/checked-idle
terminal retirement rather than flushing a cached current tick after worker join.
Candidate/fixed deterministic paths and controls pass; three real Windows COFF
units compile without diagnostics, with construction/borrow/AA/source/fallback/
shutdown regressions. See [scope and next audit](M4_P2_APPLET_CONTENT_READBACK.md)
and [results/commands](M4_P2_APPLET_CONTENT_READBACK_RESULTS.json). Current-run
artifacts cleaned; issue branch pushed; main untouched. Next: partial raw resource
publication/recreation, while preserving failed applet quarantine. No target-device
pixel or whole-lifecycle acceptance; HostMemory high risk, M4 incomplete, F2/F3
open and admission Unknown.

## 2026-10-24: applet Frame publication after complete construction

From `2a3a0f4a70`, ten old partial-publication/retry/readback witnesses reproduce.
Candidate then production pass creation failure at image/view/framebuffer, host
faults, semaphore factory gates, child-first prefix cleanup, repeated failures,
retry, existing complete reuse, pass side effects and post-publication Draw error.
A local value-initialized Frame is moved into the empty member only after all
creation succeeds; real Frame nothrow move assignment is compile-time checked.
Two real Windows production COFF units compile without diagnostics; capture,
checked idle, AA and renderer shutdown regressions pass. See
[scope and next audit](M4_P2_APPLET_FRAME_PUBLICATION.md) and
[results/commands](M4_P2_APPLET_FRAME_PUBLICATION_RESULTS.json). Current-run temporary
artifacts cleaned; issue branch pushed; main untouched. Next: partial raw resource
recreation and applet first-draw content readiness. Native tuple completeness is
not pixel/content readiness. HostMemory high risk; M4 incomplete, F2/F3 open and
admission Unknown.

## 2026-10-24: AA helper replacement waits for the complete old-use prefix

From `bdd1977472`, eight old-owner retirement witnesses reproduce, including a
final composite after internal Finish that outlives all cached slot ticks.
Candidate then production pass three-slot use, paused native/host boundaries,
partial/split failed waits, closed/unknown completion and constructor retry.
Refresh/AA replacement completes the graphics prefix and host recording before
resetting old owners; cold and unchanged paths skip this new boundary. AA setting
publication follows successful construction. Three real Windows production COFF
units compile with zero diagnostics; source/mapping/capture/fallback/shutdown
regressions are recorded. See [audit and next scope](M4_P2_AA_HELPER_RETIREMENT.md)
and [results/commands](M4_P2_AA_HELPER_RETIREMENT_RESULTS.json). Current-run temporary
artifacts cleaned; issue branch pushed; main untouched. Next: partial applet_frame
construction/publication and raw recreation failure. HostMemory high risk;
M4 incomplete, F2/F3 open and admission Unknown.

## 2026-10-24: capture output and local upload exception retention

From `ce2007b440`, fourteen historical local-owner unwind witnesses reproduce in
RenderToBuffer, GetAppletCaptureBuffer and UploadImage. Candidate then production
extraction passes queued/active worker, partial native effects, checked completion,
closed entry and safe pre/post-effect failure cases. Error handlers run before
local native owners unwind; uncertain work uses existing terminal fail-stop.
Normal completion keeps one existing Finish submission, drains host recording and
checks the saved native tick before invalidate/return or staging release. Three
Windows production COFF units compile without diagnostics; source/fallback/shutdown
regressions pass. See [audit and next scope](M4_P2_CAPTURE_NATIVE_BORROW.md) and
[results/commands](M4_P2_CAPTURE_NATIVE_BORROW_RESULTS.json). Temporary artifacts
cleaned; issue branch pushed; main untouched. Next: deterministic old helper
replacement/use-completion witness in RefreshResources/SetAntiAliasPass, then
partial applet frame construction. This is a selected local-owner repair, not
whole-helper/device/game acceptance. HostMemory high risk, M4 incomplete,
F2/F3 open and admission Unknown.

## 2026-10-24: accelerated native source owner through split submissions

From `4ee7ee0fee`, baseline bare native handles outlive CPU-ring retirement and
failed present fence waits still reset/reuse frames. Exact original/scaled/null
image allocations and selected native views now have shared, immutable ownership
pairs independent of cache metadata. A whole WindowAdapt draw scope renews them
before every later record across internal Finish, with receipts on the existing
submissions. Disposal requires accepted same-queue receipt plus actual timeline
or checked fallback fence acknowledgment; failures retain. Closed entry, worker
join and successful checked device idle gate terminal release. Presentation
output has its separate checked present_done reuse boundary. See
[protocol, evidence and next audit](M4_P2_ACCELERATED_NATIVE_SOURCE.md) and
[results](M4_P2_ACCELERATED_NATIVE_RESULTS.json). Eight Windows production COFF
units compile; deterministic source/fallback/presentation/shutdown and data
provenance regressions are recorded with their fixture limits. Temporary
artifacts cleaned; issue branch pushed; main untouched. Next: native helper and
capture-output exception ownership. HostMemory high risk, all M4 phases incomplete,
F2/F3 open and admission Unknown.

## 2026-10-24: close retired session frame-source re-admission

From `a8daa2c75a`, test-first shutdown/stale recapture and stale queued-stamp
witnesses reproduced. SessionId now carries a nonzero per-Container incarnation;
GetDisplayMapping captures its device stamp under the handle lock, releases that
lock, then validates the session identity/active state/backing/PID. Retirement
before validation refuses; retirement after validation revokes the original stamp.
Stale detection now invalidates queued stamps without reclaiming retained state.
Close/RetainSession and UnmapAllHandles distinguish reused slots from old handles.
Fifteen paths/nine exact controls pass three times, including the counterexample
which validates first then incorrectly captures a fresh epoch. Address, prepared
owner, packet and actual-copy mapping-use regressions pass; six Windows production
COFF units compile without diagnostics. See [scope, evidence and resume commands](M4_P2_SESSION_SOURCE_ADMISSION.md)
and [results](M4_P2_SESSION_SOURCE_RESULTS.json). Current-run exe/objects/generated
fixtures cleaned; main untouched. Next: accelerated image-owner handoff through
recording/submission/presentation completion. Generic raw session/GMMU admission
is not upgraded to a lease. HostMemory high risk; all M4 phases incomplete,
F2/F3 open and GuestBackingAdmission Unknown.

## 2026-10-24: bounded mapping use at the delayed frame-source read

From `96d993457d`, an actual raw-read baseline witnesses address reuse selecting
another physical source. Candidate protocol was tested before integration.
Device-wide frame source stamps now revoke before Map/Unmap callbacks and final
session/ASID retirement. A bounded guarded page copy creates owned bytes for
unswizzle; cache view selection also checks/uses the domain. Rejected layers are
omitted rather than drawing stale staging. No source lock spans GPU waits.
Ten protocol paths/six controls and seventeen actual-body paths/four controls
pass three times, including active actual copy versus Unmap. Packet, address and
prepared session-retirement regressions pass; nine Windows production COFF units
compile without diagnostics. See [protocol and precise limits](M4_P2_FRAME_MAPPING_USE.md)
and [results](M4_P2_FRAME_MAPPING_RESULTS.json). This is conservative device-wide
cancellation, not a physical content transaction or native lease. Next: permanent
session source re-admission via escaped producers (shutdown/stale-session mappings
can be retained), plus accelerated image ownership through native completion.
HostMemory stays high risk. Temporary artifacts cleaned; main untouched; M4
incomplete, F2/F3 open and admission Unknown.

## 2026-10-24: synchronize source address snapshots and refuse retired addresses

From `0412393e94`, mapping/free/session audit found GetHandleAddress read d_address
under the table lock while writers used the handle lock. Actual-body baseline
witnesses reproduced unsynchronized lookup and deferred publication of address
zero. Lookup now retains a shared table snapshot, releases the table lock, then
reads under the handle mutex; nvdisp refuses a zero translation before GPU
publication. Eight paths/five exact controls pass three times; packet, prepared
retirement and actual-ThreadManager checked-unmap regressions pass. Four Windows
production objects compile without diagnostics. See [audit and remaining epoch gap](M4_P2_SOURCE_ADDRESS_TRANSLATION.md)
and [results](M4_P2_SOURCE_ADDRESS_RESULTS.json). The explicit after-snapshot free
witness still invalidates the saved address despite an internal duplicate. No
sustained mapping lease is claimed. Next: actual-read epoch/physical plan with
remap/reuse, active-read unmap and session mapper retirement. Temporary binaries/
objects cleaned; main unchanged; M4 incomplete, F2/F3 open, admission Unknown.

## 2026-10-24: immutable per-frame source packet connected through guest fences

From `4e76547588`, actual composer/nvdisp/GPU bodies reproduced lost source
ownership after all original layer/slot/cache references dropped. GraphicBufferSource
now freezes the descriptor and retains its wrapper through HwcLayer,
FramebufferConfig and shared GPU Request/guest actions. Missing/mismatched packets
refuse before nvmap lookup; partial registration/read exceptions cancel future
access without claiming no effects or replaying reads. Active close waits the
source callback under the existing state mutex; capture retirement stays outside
the syncpoint guard. Fourteen cases/eight precise controls pass three times,
including ready-registration partial effects, shared fences, live byte borrowing
and shutdown; request/action/retirement regressions pass. Eight real Windows
production objects compile without diagnostics. See [protocol/limits](M4_P2_FRAME_SOURCE_PACKET.md)
and [results](M4_P2_FRAME_SOURCE_RESULTS.json). Temporary binaries/objects/generated
sources are cleaned; main unchanged. Next: stable source translation/mapping-use
epochs through unmap/free/session retirement and accelerated access. The packet
is not a mapping/native lease or F2 authority; M4 incomplete, admission Unknown.

## 2026-10-23: release deferred action captures outside the syncpoint guard

From `f029ee7bd3`, the per-frame audit confirmed HwcLayer/FramebufferConfig still
lose the source GraphicBuffer owner. Directly attaching it would also retire its
last captured reference under SyncpointManager guard. Two execution/cancellation
baseline witnesses reproduced before repair. Completed/cancelled function owners
now move into locals destroyed after unlocking; callback execution/order stays
under guard. Ready-prefix capacity is reserved before callbacks, with explicit
counter-advanced/actions-intact allocation-failure and throwing-prefix tests.
Eight paths/seven precise controls pass three times; GPU request, owned retirement
and blit idle regressions pass; four Windows production objects compile without
diagnostics. See [scope and next packet protocol](M4_P2_SYNCPOINT_OWNER_RETIREMENT.md)
and [results](M4_P2_SYNCPOINT_RETIREMENT_RESULTS.json). Current-run binaries/objects
are cleaned and main untouched. Next remains the immutable source-owner packet
across deferred fences/read, with partial registration and active-shutdown cases;
it is not connected by this prerequisite fix. No native/mapping/guest authority;
all M4 phases incomplete, F2/F3 open, GuestBackingAdmission Unknown.

## 2026-10-23: checked Vulkan recreation idle after final present publication

From `73a0461d43`, backing/cache/native audit found BlitScreen::WaitIdle waited
presentation before publishing final recording closures and ignored native idle
results. Two actual-body baseline witnesses reproduced before repair. Recreation
now Finish/WaitWorker/WaitPresent, then checks device idle under submit_mutex;
failures/exceptions fail-stop before resource disposal. Four healthy/eleven
failure paths and six exact controls pass three times; GPU normal synchronous
request/cancellation, whole-owner disposal and abandoned-slot regressions pass.
Six Windows production objects compile without diagnostics. See
[audit and ownership limits](M4_P2_BLIT_NATIVE_IDLE.md) and
[results](M4_P2_BLIT_IDLE_RESULTS.json). Current-run binaries/objects are cleaned,
evidence retained, main unchanged. This is a recreation boundary, not native
completion for every VI buffer. Next: per-frame ownership across deferred guest
fences and actual source access, then native resource use/reuse. All M4 phases
remain open; F2/F3 open and GuestBackingAdmission Unknown.

## 2026-10-23: escaped producer cannot resurrect abandoned warm slots

From `1c1348d777`, actual warm layer/consumer teardown plus producer bodies
reproduced two slot resurrection witnesses before repair. SetPreallocatedBuffer
now checks queue abandonment under the same mutex as Disconnect, both before
backing effects and at final publication. Refused prepared candidates retire
unlocked through existing counted/deferred/quarantined records; no slot reset,
dimension/count change or notification follows terminal refusal. Cache fixtures
retain the original GraphicBuffer independently of a refused candidate. Twelve
paths/ten precise controls pass three times, plus retirement/Conductor/session
regressions; seven real Windows production objects compile without diagnostics.
See [audit/protocol](M4_P2_ABANDONED_SLOT_PUBLICATION.md) and
[results](M4_P2_ABANDONED_SLOT_RESULTS.json). Current-run exe/objects/generated
sources are cleaned, evidence preserved; main untouched. Next: final uncached
unpin/free and cached framebuffer release versus checked native/presentation
completion. Local queue closure does not prove full producer drainage, revoke
escaped aliases or complete M4/F2/F3; GuestBackingAdmission remains Unknown.

## 2026-10-23: warm VI Conductor and event-borrow drainage

From `31d980ce84`, three actual-body witnesses reproduced warm termination
returning with a live Conductor, destructor-body return before explicit join and
terminal unlink retaining Event pointers. OnTerminate now closes entries, waits
for timing callbacks and explicitly joins the VSync worker outside m_lock, then
performs its existing cleanup. A separate owner mutex serializes repeated stop;
terminal unlink stays available and a manager mutex spans each signal borrow.
Ten deterministic paths/eleven exact controls pass three times; terminal
publication/session/composition/disposal regressions pass, and six Windows
production objects compile without diagnostics. See [audit and next boundary](M4_P2_VI_CONDUCTOR_DRAIN.md)
and [results](M4_P2_VI_CONDUCTOR_RESULTS.json). Current-run executables/objects
are cleaned after preserving logs/hashes. No main merge or native/device/CI
acceptance is claimed. Next: warm slot/GraphicBuffer and HardwareComposer cached
framebuffer owner release versus native completion, including pre-close escaped
producers. Cleanup remains before GPU drain. M4 incomplete; admission Unknown.

## 2026-10-23: VI terminal publication after OnTerminate

From `dc5d532840`, actual stop/owner/GPU-order audit found cold and paused VI lazy
initialization could publish after OnTerminate, including after GPU-drain sequence;
retained terminated facades could lend the manager again. Three original witnesses
reproduced before repair. Bool EnsureInitialized now checks terminal state under
the same mutex before lookup and publication, with under-lock caller rechecks and
all eight nullable VI/AM manager callers adapted. Existing owner records are not
discarded. Nine cases/nine exact controls pass three times, plus the session-owner
and whole-owner terminal suites; six Windows production objects compile cleanly.
See [protocol and limits](M4_P2_VI_TERMINAL_PUBLICATION.md) and
[results](M4_P2_VI_TERMINAL_RESULTS.json). Next: warm OnTerminate layer/slot effects,
Conductor and active-operation drain order. This terminal gate does not drain old
borrowers/native work or complete any M4 phase. Guest admission Unknown, main
unchanged; current-run binaries/objects cleaned after preserving evidence.

## 2026-10-23: VI partial-create and refused-Close owner retention

From `0a22468339`, actual-body negative witnesses reproduced visible failed-create
records and session owner release after refused Close. Create/destroy now retain
the existing outer entry and prepare a retaining record before effects, with
Module/Process/backing/VI anchors and pool/slot owners acquired before their use.
Only complete creation becomes Ready; failed prefixes and
refused/partial destruction become hidden Unknown tombstones, with the prepared
record moved to existing quarantine before entry exit. No uncertain cleanup replay.
Normal slot/free/session order is preserved; final owner release is unlocked but
counted, and healthy sessions have no VI self-cycle. 29 cases/ten exact controls
pass three times, plus capture/queue regressions and six real Windows production
objects. See [protocol and remaining boundaries](M4_P2_VI_SESSION_OWNERSHIP.md) and
[results](M4_P2_VI_SESSION_RESULTS.json). Nested admission still resolves uncertain
prefixes conservatively, not via a new continuation. Next: explicit VI OnTerminate
and Container publication/lifetime composition. No whole-System/native/alias lease,
F2 content completion or ordinary Guest admission; all M4 phases remain open.
Main unchanged; current-run binaries/objects are cleaned after preserving evidence.

## 2026-10-23: VI raw capture and create/destroy owner audit

From `cbf9c25990`, audited SharedBufferManager pool allocation/publication, Process
mapping, partial session records, raw capture and destruction. Test-first capture
witnesses reproduced uncounted closed/active writes and empty-result false success.
Capture now retains the existing NVDRV entry through the synchronous operation,
pairs pool publication with m_guard without holding it over GPU wait, refuses empty
capture before stores, and seals partial exceptions. Eight cases/eight controls
pass three times; ten GPU lifecycle cases/control and three Windows production
objects pass. See [owner audit, boundaries and next tests](M4_P2_VI_CAPTURE_CLOSURE.md)
and [results](M4_P2_VI_CAPTURE_RESULTS.json). Next: actual CreateSession failure
prefixes and DestroySession ignoring refused Module::Close, with prepared owner
retirement and nested-admission progress. Those methods, raw initialization and
the zero-length invalidation concern remain unchanged. Main unchanged, all M4
phases incomplete, Guest admission Unknown. Current-run binaries/objects are
cleaned after preserving evidence; no full app/native/Android acceptance claimed.

## 2026-10-23: direct SurfaceFlinger composition and raw-alias census

From `b791777696`, actual caller audit found ComposeDisplay borrowing nvdisp and
running synchronous effects outside the Kernel NVDRV closure. Test-first closed
and borrowed-device witnesses both reproduced false drain. The entry now counts
before display/device borrows through synchronous composer return/output, checks
missing device, and seals exceptions before entry exit. Six cases/five exact
controls pass three times, as do ten existing GPU lifecycle cases and their
cancelled-read control; three real Windows production objects compile cleanly.
See [scope, raw producer table and next step](M4_P2_SURFACE_COMPOSITION_CLOSURE.md)
and [results](M4_P2_SURFACE_COMPOSITION_RESULTS.json). Guest-fence delayed callbacks
and native GPU work are explicitly outside this synchronous closure. Next isolate
VI SharedBufferManager create/raw-write/map/retire ownership; HID/time/IRS/raw views
remain open. Main unchanged; all M4 phases incomplete, Guest admission Unknown.

## 2026-10-23: afternoon sync and desktop Turbo pending dispatch

Fast-forwarded 16 afternoon commits to `664136bb76`, matching PR375. A test-first
follow-up reproduced five actual Turbo Run defects: inverted VkResult handling,
pending native resource reuse, stop-time false completion and ignored submit/wait
errors. The worker now retains its one pending dispatch across timeouts and pauses,
reuses only after VK_SUCCESS, and applies existing no-unwind Fail-Stop on native
errors. Stop leaves pending work for destructor checked device idle. Five paths
and five precise controls pass three times; the whole-owner regression and actual
Windows Turbo production compilation pass. See [evidence and continuation](M4_P2_TURBO_DISPATCH_LIFETIME.md)
and [recorded results](M4_P2_TURBO_DISPATCH_RESULTS.json). Temporary executables and
objects are cleaned after recording results. Main is unchanged; P2/M4 remain open,
GuestBackingAdmission Unknown. Next: remaining producer/raw-alias closure audit;
do not promote this separate desktop device witness to Guest-backed admission.

## 2026-10-23: whole-owner terminal disposal and no-unwind failure

The owning System shutdown now drains GPU before service/Process/PageTable
retirement; System destructor has the same fallback before Impl's reverse member
destruction. GPU's existing terminal close/join becomes idempotent explicit
Shutdown. Renderer requires successful native idle and checked disposal; Turbo
checks its separate device too. Detected uncertainty records stage/reason/Vulkan
result and directly `_Exit(86)` before parent/member destruction. No queue or
global quarantine manager is added. Independent child-process tests verify five
success paths, 23 failure paths (three repetitions), five precise mutation controls,
and no destructor/atexit/static-handler escape. Windows production compilation
uses real headers and a real generated Turbo shader. See
[protocol and evidence](M4_P2_WHOLE_OWNER_TERMINAL_DISPOSAL.md).
This is a terminal Fail-Stop slice; P2 and ordinary Guest admission remain open.

## 2026-10-23: native terminal drain witness and disposal ordering

Prioritize native drain and whole-owner terminal disposal; do not grow retirement
queues as a substitute. Scheduler now reports settled-owner disposal explicitly;
Rasterizer checks it before releasing any runtime transfer pins/readback. Missing
transfer submission refuses safely. Four exact controls detect preflight bypass
and the previous cross-layer release order. Three Windows production objects and
the renderer/worker cases pass. A real RTX 4060 witness passes checked idle,
barrier/invalidate/readback and native disposal three times, with an omitted-tail
negative control. It uses dedicated native allocations, not Guest backing.
Critically, refusal still does not prevent later C++ member/allocator destruction;
whole-owner failure propagation remains the next gate. See
[evidence, disposal table and limits](M4_P2_NATIVE_TERMINAL_DRAIN.md).
P2 remains incomplete and GuestBackingAdmission stays Unknown.

## 2026-10-23: GraphicBuffer and session retained retirement

GraphicBuffer uses a shared Module provider and a preallocated unique retirement
record holding handle/Process/physical backing. NVDRV initialization prepares a
session record before OpenSession; destruction retires under admission or defers
without allocating. Unknown partial effects are retained and never replayed.
Provider final release is counted through an independently owned queue. Session
owner ordering/reuse avoids releasing Process/backing before mapper or under
session_guard; final Close failure/stale epoch preserves preallocated backing
quarantine without retrying zero-reference objects.
Three suites, 50 exact controls and eight Windows production objects pass; see
[protocol, evidence and limits](M4_P2_GRAPHIC_SESSION_RETIREMENT.md).
Global System/Host1x/native/allocator retirement and trusted quarantine disposal
remain separate. P2/M4 are not complete; GuestBackingAdmission stays Unknown.

## 2026-10-23: ordinary NVDRV dispatch and remaining producer audit

Six actual Module methods now retain one Kernel-shared dispatch entry: Open,
Ioctl1/2/3, Close and QueryEvent. Exceptions seal before count exit; ordinary
returned errors remain unchanged. Cohort drains the outer domain before inner
host1x/memory closures and atomically resumes it with the others. Three focused
suites, 46 exact controls and five Windows production objects pass; see
[protocol and validation](M4_P2_NVDRV_DISPATCH_CLOSURE.md).
NVDRV Initialize now shares this domain; it cannot publish a session after
closure or fabricate Success while an initialization exception unwinds.
The [actual remaining-route audit](M4_P2_REMAINING_PRODUCER_AUDIT.md) identifies
GraphicBuffer direct pin/unpin/free, session publication/teardown, escaped views,
NCE and native GPU/Unknown owner requirements. P2 is NOT complete and ordinary
GuestBackingAdmission remains Unknown.

## 2026-10-23: host1x MapBuffer/UnmapBuffer share the Submit closure

Both common VIC/NVDEC mapping ioctls now enter the same domain as Submit, before
pin/unpin or local output mutation. Partial pin/unpin failures seal before count
exit; even a zero first Pin is Unknown because the callee can already have
changed reservations/mappings. The expanded actual-body suite passes three
repetitions and sixteen exact controls; four Windows production units compile.
See [disposition and validation](M4_P2_HOST1X_MAPPING_CLOSURE.md).
Existing cohort/ResumeBatch code is unchanged. Direct nvmap/free/eviction paths,
complete alias freeze and durable Unknown native/backing ownership remain open.
P2 remains incomplete and GuestBackingAdmission stays Unknown.

## 2026-10-23: synchronous VIC/NVDEC Submit closure

The shared nvhost_nvdec_common Submit body now retains a Kernel-owned producer
entry through command reads, synchronous CDma dispatch and local response
serialization. Closed admission returns Busy before effects. Partial prefixes,
refused suffixes and exceptions after an effect boundary seal before count exit.
Cohort drains the domain before managed writes and composes atomic cancellation.
Three focused suites, 27 exact controls and five Windows production objects
pass; see [protocol and evidence](M4_P2_HOST1X_SUBMIT_CLOSURE.md).
This is not all public VIC/host1x routes, mapping protection, native completion
or durable Unknown owner retention. P2 remains incomplete; Guest admission Unknown.

## 2026-10-23: AudioManager input/output callback closure

AudioManager callback sweeps now enter a Kernel-shared domain outside the event
mutex. Closed admission preserves pending events; stop-token shutdown wakes and
joins a paused worker. Callback exceptions seal before counted-entry exit.
Cohort drains these callbacks before managed Memory writes and includes the
domain in its atomic ResumeBatch. Three focused suites, 24 exact controls and
five Windows production objects pass; see [evidence](M4_P2_AUDIO_CALLBACK_CLOSURE.md).
This covers that worker only: sink/backend callbacks, direct service operations,
Opus/VIC/NCE/raw/GPU producers and complete owner retention remain independent.
P2 remains incomplete and ordinary Guest admission stays Unknown.

## 2026-10-23: Audio render cycle closure before managed writes

SystemManager counts the complete command-generation/Signal/checked-response cycle.
Closed cycles pause before list locks/guest pointer use; stop cancels that wait.
AudioRenderer Wait rejects wrong responses without clearing command metadata and
the manager quarantines its domain. Cohort drains Audio cycles after backend stop
and BEFORE closing managed Memory writes, then composes all domains in one
ResumeBatch. Actual cycle/Stop/ack and cohort tests plus five Windows production
objects pass; see [protocol, evidence and limits](M4_P2_AUDIO_RENDER_CLOSURE.md).
This does not stop all Audio/input/output/Opus/HLE/VIC/raw/native producers or
provide complete owner retention. P2 remains incomplete; guest admission Unknown.

## 2026-10-23: M4 P2 shared managed-write closure and cohort composition

21 ordinary/prepared Memory facade write methods now enter a shared Kernel domain
before Impl effects. Non-NCE closed calls explicitly refuse; normal contention
does not drop stores. Cohort closes the domain after backend stop, waits for
admitted calls, and resumes it in the same atomic ResumeBatch. Late Seal and
Unknown remain fail-closed. HAS_NCE seals this unsupported domain and preserves
existing signal-interpreter stores without adding signal waits/exceptions.
See [P2 coverage, protocol and acceptance](M4_P2_PRODUCER_CLOSURE.md).
P2 is NOT complete: real NCE/fastmem/service worker drains, escaped aliases and
durable complete backing/native ownership remain blockers. Guest admission Unknown.

## 2026-10-23: checked write host-pending owners block idle retirement

The actual retirement body could release checked write holders while their host
submission was still missing/unattempted/pending; only transfers had analogous
guards. It now preflights all writes before releasing any write/transfer owners,
including settled prefixes. Three actual-body repeats/two exact controls pass;
the existing synchronization/content positive regression passes (Windows private
OS fixture explicitly skipped), and Scheduler/Rasterizer production objects
compile. See [retirement ownership](M4_F3_CHECKED_WRITE_RETIREMENT.md).
Normal renderer shutdown already joins its host worker before device idle; this
strengthens the explicit retirement boundary, not whole-guest admission. All
THREE phases remain incomplete and admission Unknown.

## 2026-10-23: roadmap comparison and fence pool retirement failure

The [roadmap snapshot](M4_ROADMAP_STATUS_2026_10_23.md) maps M0–M7 and the three
unfinished M4 phases to actual evidence and remaining gates. Recent fence/NV
repairs are phase-two prerequisites, not completed Guest admission.
An actual-body witness reproduced free_queue allocation failure escaping the
completion worker. Pool insertion now shares Reset's failure boundary; bad_alloc
retains the local fence/suffix, latches host-memory failure and wakes waiters
without publishing a completion tick. Three repeats, seven exact controls and
two Windows production objects pass; see
[validation](M4_F3_FENCE_RETIREMENT_ALLOCATION.md). Guest admission remains Unknown.

## 2026-10-23: fence submission ownership reserved before acceptance

SubmitQueueFence now reserves its tick/fence record before native Submit while
holding the worker's wait_mutex. No allocating insertion follows acceptance.
Non-success keeps the record/suffix and latches failure; the worker refuses to
consume possibly unsignalled rejected fences. First-error CAS preserves newer
isolation against delayed older completions/failures. Actual Submit/Wait/worker
bodies cover allocation refusal/retry, accepted/failed submits, mutex visibility
and both older-completion races. See
[transaction and validation](M4_F3_NATIVE_SUBMISSION_RESERVATION.md).
This retains fences, not all native/backing resources. Higher-level Unknown
ownership, independent producers and visibility remain open; guest admission Unknown.

## 2026-10-23: native fence Reset exception is acknowledged failure

A successful native wait followed by a Reset exception escaped the completion
worker and stranded waiters. Reset's vk::Exception now joins the existing
terminal failure path, retaining its fence/queued suffix and waking waiters with
the original error, without tick publication or reuse. Four result variants pass
three repetitions and three exact mutation controls. See
[reset failure and remaining submit transaction](M4_F3_NATIVE_FENCE_RESET_FAILURE.md).
Queue acceptance followed by wait_queue allocation failure remains an explicit
ownership gap; no whole-GPU Unknown registry or guest admission is established.

## 2026-10-23: native fence fallback failure cannot retire resources

The fence fallback ignored vkWaitForFences results before Reset/recycle and
gpu_tick publication. It now latches failure, retains the unknown fence and
queued suffix, stops the completion worker, wakes waiters with failure and
refuses later observed fallback submissions. Confirmed older ticks remain valid.
Actual Wait/WaitThread bodies pass three repetitions with success/device-loss/
timeout and two concurrent waiters; two exact controls reject false completion
and Unknown recycling. MasterSemaphore and Scheduler production units compile
to Windows AMD64 objects. See [native failure ownership](M4_F3_NATIVE_FENCE_FAILURE.md).
This owns fences only, not a global Unknown backing/resource registry. External
idle/device teardown remains required; all THREE M4 phases remain unfinished,
ordinary admission Unknown. No Android/device/workflow run or main merge.

## 2026-10-23: FreeSpace missing-owner refusal

An actual-body witness reproduced default owner insertion from FreeSpace's
allocation_map subscript on an unknown offset. FreeSpace now uses at(), allowing
the existing pre-unmap out_of_range path to return BadValue without metadata or
allocator effects. Ordinary and zero-sized invalid requests retain an unchanged
inventory; a subsequent valid free still succeeds. See
[refusal validation](M4_F3_FREE_SPACE_LOOKUP.md). This narrow production fix does
not complete mapping transactions or any M4 phase. Guest admission remains Unknown.

## 2026-10-23: synchronized morning commits and duplicate callback proof

Fast-forwarded seven remote morning commits to b6d1f03b65 without conflicts.
The dispatch regression now also invokes a retained closure copy while its
original action is Running, then throws. Neither replay nor early completion
acknowledgement is permitted. Both successful/throwing originals pass three
repetitions; a new precise mutation rejects admission of an already Running copy.
No production change was needed for this boundary. See
[local continuation](M4_F3_OCT23_LOCAL_CONTINUATION.md) for validation and limits.
Main is unchanged; all THREE phases remain incomplete and guest admission Unknown.

## 2026-10-23: morning checkpoint — exceptional dispatch return

Continued from the clean, remotely matching 0bb6abcc71. Two new deterministic
actual-header witnesses compiled and showed that dispatch throwing after retention
left a runnable action, and throwing after action entry returned before completion.
RunAcknowledgedCallback now cancels Pending or waits for Running on exceptional
dispatch too, then returns Failed. Ordinary completion/cancellation is unchanged;
no scheduling or shutdown order changes. Completed actions/partial effects are
not rolled back or reclassified as no-effects cancellation.

Windows: both suites passed three repetitions, with three new dispatch controls
and five checked-unmap controls. Nine actual production units compiled to x64
COFF with no diagnostics; 32 recorded input hashes (26 unique), object hashes
and machine signatures matched. Evidence is `.diagnostics/m4-dispatch-results/`
and the committed JSON summary. All twelve exe files, nine objects and other
temporary files from this turn were removed (71 files total).

See [dispatch exception evidence and continuation boundary](M4_F3_DISPATCH_EXCEPTION.md).
All THREE M4 phases remain unfinished; guest admission stays Unknown. No main
merge, new remote/Android/device run or full app link is claimed.


## 2026-10-22: checked UnmapMemory execution and retained failure ownership

UnmapMemory now consumes acknowledged callback completion. Queued rejection or
cancellation cannot become completed unmap; active callbacks remain waited to
actual return. optional false is a valid result, nullopt is failure. GMMU unmap
retains remaining mappings and latches terminal quarantine on failure/partial
ranges. NV releases allocator/pin owners only after checked unmap; failed suffixes
keep their allocation records, reject further maps/binds and return InvalidState.
Post-callback cleanup faults also quarantine. No queue scheduling or teardown
order change, F2 routing, new remote/device run or main merge.

NvMap low-area FreeHandle/reclamation retains failed queue/handle/SMMU owners,
restores the failed guest duplicate and notifies shutdown outside service/handle
locks. A final pin is retained if queue allocation fails. A low-area quarantine
race cannot publish a new pin; its cold reservation remains with the terminal owner.

Windows results: core positives passed three repetitions; 36 precise controls
(five worker, nine ownership, twelve mapping and ten channel) plus cancelled-read
coverage passed. Existing alias/readback/upload and resolver regressions passed.
Nine real production files, including Vulkan rasterizer, GPU thread and NvMap,
generated x64 COFF objects without diagnostics. The 57 recorded source hashes
(29 unique inputs), object hashes and signatures were independently verified.
Final local evidence is `.diagnostics/m4-unmap-results/` with a committed JSON
summary. All 84 exe files, 28 objects and other temporary files from this turn
were removed (554 files). No full app link or native Vulkan/device run is claimed.

See [execution, cancellation, partial effects and Windows validation](M4_F3_CHECKED_UNMAP.md).
This supersedes the previous void-handoff gap below. It does not certify complete
guest physical publication, all SMMU/raw/alias users or native completion/retirement.
All THREE M4 phases remain unfinished and GuestBackingAdmission remains Unknown.


## 2026-10-22: owned NV GMMU mapping operations and remaining entry audit

Audited the remaining direct GMMU read/write, pointer, query, resolver and mapping
families. Five NV mapping operations still bypassed terminal GPU ownership after
Close; the failure-first actual-body test compiled and reproduced closed mapping
success. AllocateSpace, FreeSpace, Remap, MapBufferEx and UnmapBuffer now retain
the existing publication Entry before locks/resources and through final effects
and acknowledgement. Remap also uses the shared AS mutex. Admitted operations
may finish; new closed requests refuse without touching allocator/pins/tables.

Windows: ten mapping variants and ten prerequisite lifecycle cases passed three
repetitions, twelve precise mapping controls and the cancelled-read control passed.
Six complete production files generated AMD64 COFF objects without diagnostics;
24 input hashes (16 unique inputs), object hashes and signatures were matched.
Final evidence is `.diagnostics/m4-nv-mapping-results/` and the committed JSON
report. All 33 exe files, twelve objects and other temporary files from this turn
were removed (206 files). No full app link or native Vulkan acceptance is claimed.

See [audit, protocol and Windows results](M4_F3_GMMU_MAPPING_LIFETIME.md).
This closes the NV service mapping subset only. Direct GMMU reads/writes/maps,
escaped pointers and native completion remain open. In particular UnmapMemory's
void GPU-worker handoff can return after cancellation without proving execution;
checked cancellation/completion and partial effects are the next explicit boundary.
No scheduling/shutdown order change, F2 routing, new remote build or main merge.
All THREE phases remain unfinished; GuestBackingAdmission remains Unknown.


## 2026-10-22: external GMMU binding terminal lifetime

The public BindRasterizer bypass now uses a detached shared terminal state.
First installation is private to GPU's retained publication, including Host1x
renderer bootstrap. Public calls only confirm the enrolled immutable target
while open; they never store a pointer or call a retired GPU. Shutdown closes
the state; teardown joins entered calls before renderer retirement. Before-entry
and post-GPU callers retain the state and refuse safely. Channel initialization
uses its already enrolled GMMU. Native scheduler fixtures are adapted to the
current allocation/enrollment APIs.

Windows results: positives and ten lifecycle cases passed three repetitions;
30 publication plus nine channel ownership controls hit precise runtime failures.
Six real production units compiled to x64 COFF without diagnostics. All 38 input
hash entries (19 unique inputs), object hashes and signatures matched. Final logs
and manifests remain in `.diagnostics/m4-external-gmmu-results/`; the committed
JSON report retains the checked inputs. All 132 generated exe files, six objects
and other temporary files from this turn were removed (593 files total).
No full app link, native Vulkan runtime, Linux/Android/device or native scheduler
fixture build is claimed.

See [failure-first evidence and Windows results](M4_F3_EXTERNAL_GMMU_BINDING.md).
This supersedes historical public-BindRasterizer bypass notes below. Other GMMU,
raw/mapping/native producers remain open. All THREE phases remain unfinished,
GuestBackingAdmission remains Unknown and main remains untouched.


## 2026-10-22: refuse GMMU replacement after channel initialization effects

After closing the Boost/production-object checkpoint at d40b0009c0, the new test
reproduced a ready channel dropping its original GMMU owner while the actual
ChannelState::Init body left engines referring to it. NV binding now rejects a
different/null manager once channel initialization succeeds or has partial effects.
The state/identity check uses the existing Setup/Submit channel lock and AS lock.
Idempotent binding requires both object address and shared control-block identity;
it cannot clear failure isolation. Equal addresses with another owner, or equal
owners with another address, refuse. Cold replacement/null and deferred Setup-
before-bind remain supported.

Windows validation: new actual-body engine/GMMU ownership positives passed three
repeats and all nine precise controls; the retained publication suite passed
three repeats and all 24 controls plus its ten-case lifecycle/cancelled-read
prerequisite. Five complete production files again generated real x64 COFF
objects without diagnostics. All 34 source-hash entries across three manifests
(17 unique inputs), five object digests and machine signatures were matched.
No full application link, native Vulkan or new remote/Android build is claimed.
The existing Linux workflow will run the new test on future dispatches. Automatic
approval blocked temporary-directory cleanup with no specific reason; current
fixtures/objects/logs remain in ignored .diagnostics directories. The complete
shared Boost cache and reproducible source helpers remain available.

This is another phase-two ownership prerequisite. It does not implement native
engine rebind, certify all engine destruction, close external GMMU/raw/mapping
producers, add reversible cohort exclusion or retain durable native Unknown work.
The existing external BindRasterizer bypass remains. All THREE phases remain
unfinished and GuestBackingAdmission stays Unknown; main still waits for M7.
See [engine/GMMU ownership validation](M4_F3_CHANNEL_GMMU_OWNERSHIP.md).

## 2026-10-22: complete Boost and real Windows production objects

The user asked to resolve the local dependency blocker before further M4 work.
The incomplete benchmark Boost tree was replaced for validation by a complete,
SHA256-verified official Boost 1.87.0 cache outside the repository. Project pins
are unchanged. The new run_windows_production_checks.py helper reproduces the
check and can fetch the verified shared header cache on another Windows machine.

At production baseline c6034b8e45, gpu.cpp, nvhost_as_gpu.cpp, nvhost_gpu.cpp,
nvdrv.cpp and channel_state.cpp all generated real Windows x64 COFF objects with
zero compiler diagnostics. All 11 input hashes and five object digests/signatures
were independently verified. This closes the prior missing-header/object-compile
blocker, not an application link or native completion proof. See
[production compilation evidence](M4_F3_WINDOWS_PRODUCTION_VALIDATION.md).
No new workflow/Android build or main merge. Continue within the same three phases;
initialized-engine GMMU replacement is the next concrete ownership boundary.

## 2026-10-22: Windows continuation — retained NV channel/address-space binding

Synced the clean issue-372 worktree from f1412a1c08 to afternoon head
`72555967d162e7546eee67c631021dd5538f9a52` with a fast-forward. Existing PR #375,
other worktrees, main and dependency choices were retained. The current local
implementation follows the latest retained channel-creation checkpoint.

BindChannel now acquires the GPU's cold terminal publication Entry before fd
lookup and service locks. Closed entry refuses before owner replacement. It
serializes the GMMU snapshot with AllocAsEx and replacement with Setup/Submit,
then releases both service locks before retiring the old shared owner. Entry
remains held through that retirement and method acknowledgement. No queue wait,
completion certificate, ordinary guest admission or native engine rebind proof
is added. Healthy cold and nullable pre-initialization behavior is preserved.

Windows MinGW GCC 13.2/Python 3.14.6 publication positives passed three repeats;
all 17 prior controls plus seven new binding controls passed their exact failure
markers. The baseline first failed the closed-binding marker before production
edits. Lifecycle prerequisites passed ten cases three times plus cancelled-read
coverage. Actual ThreadManager passed three repeats, 19 terminal controls and
the blocking-handoff negative topology control. All 15 publication/nine thread
manifest source hashes matched current sources before removing temporary outputs.

The full production nvhost_as_gpu.cpp Windows syntax attempt is blocked by the
missing Boost unordered_flat_map.hpp dependency; no full production TU/build or
new Linux/Android/native Vulkan validation is claimed. Earlier Linux 39729352143
was independently observed green at d8b0c3897d, not this new source. Reproduce the
local checks using the commands in the linked validation report. Temporary outputs
are removed after inspection; retained tests and Markdown are the handoff.

The old post-Close NV binding witness is closed by the candidate. The independent
actual-body external GMMU BindRasterizer assignment still succeeds after Close.
Existing initialized engines retain references to their old manager, so safe
live-engine rebinding remains unproved and must not qualify for admission.
Facade/Module/service entry lifetime, other mappings/raw aliases, native GPU work
and durable Unknown retention remain outstanding. All THREE M4 phases remain
unfinished; ordinary GuestBackingAdmission remains Unknown. Main still waits for
M7. See [Windows validation and remaining limits](M4_F3_CHANNEL_BINDING_VALIDATION.md).

## 2026-10-22: retained channel creation through NV fd/session publication

AllocateChannel now returns its channel together with the cold publication Entry.
NV's GPU factory and Module::Open retain it through device construction, fd-map
insertion and OnOpen's session commit. Closed allocation returns an invalid fd
without channel ID, scheduler, event or syncpoint effects. Module may consume an
unused fd number. GPU IDs are serialized; declaration failure rolls back its GPU
map record. Map locks never span scheduler/backend calls, avoiding the existing
Scheduler::Push -> BindChannel lock order. Module clears its own fd map before
event providers retire; escaped references still need external lifetime control.

Windows positives pass three repeats, prior eleven controls and six new precise
creation controls. Actual allocation, scheduler declaration, NV constructor/
destructor/factory, Module Open/retirement and session bodies execute with fake
external providers. Concurrent IDs, rollback, lock inversion and five retained
teardown stages are covered. Four production units pass local WSL syntax. See
[validation](M4_F3_CHANNEL_CREATION_VALIDATION.md).

The earlier creation-after-Close witness is closed. Existing-channel address-space
binding still succeeds after Close as a separate actual-body scope counterexample.
No full NV Open exception transaction, Module client serialization, escaped-device
lifetime, native completion, reversible cohort stop or durable Unknown registry
is established. All THREE M4 phases remain unfinished; ordinary admission Unknown.
No new workflow or Android build; main still waits for M7.

## 2026-10-22: retained channel initialization and NV acknowledgement

InitChannel now returns the GPU's cold terminal publication Entry to both NV
Setup and Submit callers, including ready-channel Submit. Deferred Setup remains
supported; unbound Submit refuses. Setup is serialized with Submit's existing
channel mutex. Closed entry refuses before caller program-id/fence/queue effects.
Entry survives through final caller acknowledgement, so returning from backend
initialization alone cannot release teardown protection.

ChannelState::Init no longer sets initialized early. The GPU sets ready only after
engine construction, alias binding and backend cache initialization succeed.
Before partial effects it marks initialization_failed; any exception preserves
that state and later requests refuse rather than retrying the affected owner.
This is retry isolation, not proof of native resource retirement.

Windows publication/lifecycle tests pass three repeats, the prior controls and
six new precise channel controls. Actual GPU/NV Setup/Submit methods are executed;
engine/backend/queue/syncpoint work remains fixture code. Complete production
gpu.cpp, nvhost_gpu.cpp and channel_state.cpp pass local WSL syntax. See
[the retained validation conclusion](M4_F3_CHANNEL_INITIALIZATION_VALIDATION.md).
Temporary outputs are removed after inspection; no new remote/Android build.

CreateChannel still succeeds after Close as an actual-body positive scope
counterexample. Channel creation and address-space binding/configuration remain
unclosed. Pre-submit guest buffer reads, existing aliases, void queue-admission
acknowledgement, native writers and durable Unknown ownership are independent.
All THREE M4 phases remain unfinished and ordinary GuestBackingAdmission remains
Unknown. Main remains unchanged until M7's final zero-copy merge.

## 2026-10-22: retained GPU address-space owner publication

GPU owns a separate cold terminal address-publication counter, open before Start,
closed by NotifyShutdown and joined before renderer teardown. InitAddressSpace
returns an optional owned Entry retained by the actual NV AllocAsEx caller across
its final VM/GMMU publication. The caller stages candidates locally, returns
InvalidState on refusal, and commits by nothrow moves only after binding succeeds.
This closes the former bind-return/caller-commit gap for this entry family.

Windows publication and existing GPU lifecycle suites pass three repeats with
five new precise controls plus the cancelled-read coverage control. Candidate
allocation failure, pre-Start success, closed no-effects refusal, bind in flight
and returned Entry held at caller commit are covered. Complete production gpu.cpp
and nvhost_as_gpu.cpp translation units pass local WSL syntax. See
[the retained validation conclusion](M4_F3_ADDRESS_PUBLICATION_VALIDATION.md).
Temporary binaries/fixtures/logs are removed after inspection. The workflow adds
this early regression for future runs; no new remote or Android build dispatched.

This covers only terminal address-space publication. Candidate construction
before entry still relies on System lifetime; other channel creation/init/bind,
service routes, existing mapping/raw-alias lifetime, native writers and durable
Unknown owner retention remain open. No reversible ProcessCohortStop linkage or
Guest admission is added. All THREE M4 phases remain unfinished. Main remains
unchanged until M7's final zero-copy merge.

## 2026-10-22: terminal command tail and whole dirty gather

FlushCommands, InvalidateGPUCache and OnCommandListEnd now forward from the actual
GPU facade to ThreadManager under its terminal counter. The dirty-gather entry
precedes metadata consumption and covers all synchronous callbacks/owners, not
just each callback. Already executing GPU-owner command tails may finish after
external Close under worker join. ReleaseFences(false) then accuracy-update
ordering remains unchanged; no new queue synchronization is introduced.

Actual-body Windows ThreadManager tests pass three repeats with all earlier
controls plus six command-tail controls. Actual System gather and dirty-manager
Collect/Gather execute against two fixture owners. Both GPU modes, closed metadata
preservation, whole-gather lifetime, exception exit and active owner tails are
covered. GPU lifecycle/readback regressions also pass three repeats; production
gpu_thread.cpp Windows syntax passes. The reused existing registration fixture
also passes local WSL syntax with the current ThreadManager prefix; Windows lacks
its Boost dependency, and registration-pipeline execution was not repeated. See
[the retained Windows conclusion](M4_F3_COMMAND_TAIL_TERMINAL_VALIDATION.md).
Temporary outputs are removed after verification. No new remote/Android build.

Actual dirty Collect still works after Stop as an independent producer scope
counterexample. Backend affinity/serialization, dirty callback partial-exception
semantics, remaining raw/service/publication routes, native submissions and
Unknown retirement remain unresolved by this terminal lifetime extension. It
does not add reversible GPU stop or checked completion. All THREE M4 phases
remain unfinished and ordinary GuestBackingAdmission remains Unknown. Main stays
unchanged until M7's final zero-copy merge.

## 2026-10-22: terminal readback query and cancelled coverage

GPU::Impl::OnCPURead now gets optional geometry through
ThreadManager::TryGetFlushArea under the same terminal cache-entry counter.
External queries are joined through callback return/exception; entered GPU-owner
queries may finish under worker join. The value leaves no entry held across the
separate sync-request flush wait. Closed query returns no area, and unsuccessful
WaitForSyncOperation also returns an empty area, so Memory cannot cache the
requested interval as newly flushed coverage on shutdown cancellation.

Windows actual-body ThreadManager and GPU lifecycle suites pass three repeats,
with all original controls plus four query controls and one cancelled-read
coverage mutation. Production gpu_thread.cpp Windows syntax passes. See
[the retained Windows conclusion](M4_F3_CPU_READ_TERMINAL_VALIDATION.md).
Temporary outputs are removed after verification per user request. No new remote
workflow or Android build is dispatched for this local check.

The former successful OnCPURead query bypass is now a precise mutation rejection.
An actual-body FlushCommands bypass remains successful against a live fixture
rasterizer after Stop. Existing cached geometry is not revoked and guest loads
are not stopped by empty results. Other services/raw entry points, parked work
and native GPU execution still need owned closure/completion/Unknown retention.
All THREE M4 phases remain unfinished and ordinary GuestBackingAdmission stays
Unknown. Keep main unchanged until M7's final zero-copy merge.

## 2026-10-22: terminal CPU-write cache callback ownership

Linux-only run [39729352143](https://github.com/Splaser/emulator/actions/runs/39729352143)
completed successfully at `d8b0c3897de6cbd57f10c9d02bcfa27a35086637`, including
production build/unit tests, native Vulkan/F2/F3 controls and integration regressions.
Android was skipped. Independent downloaded artifact verification is not complete.
The subsequent clean upstream merge `e5a1ffe362` adds only the ScarletNX game-list
feature from upstream `f942c35679`; it was not the source of that CI run.

GPU::Impl::OnCPUWrite now forwards through ThreadManager and shares the terminal
direct-invalidation entry counter. External callbacks retain the entry until return
or exception; Stop closes entry, joins the worker, retires queued captures, then
joins external callbacks before the renderer may shut down. Already executing GPU
owner callbacks remain covered by worker join. True/false rasterizer results pass
through unchanged. Before start/after close, false requests no dirty collection;
it is not successful cache work and does not stop or authorize the CPU store.

Windows MinGW GCC 15.2 local actual-body ThreadManager tests pass three repeats,
the five previous direct-ingress controls, four new CPU-write controls, and the
blocking-handoff dependency control. GPU lifecycle cases also pass three repeats;
the production gpu_thread.cpp translation unit passes Windows syntax checking.
See [the retained validation conclusion](M4_F3_CPU_WRITE_TERMINAL_VALIDATION.md).
Per user request, temporary Windows fixture executables and related outputs are
removed after verification, retaining the Markdown conclusion and reproducible
test sources. No Android or new remote build is needed for this local check.

The previous successful OnCPUWrite bypass is now an explicit failing mutation
control. The actual preemptive OnCPURead/GetFlushArea route remains a successful
independent bypass against a live fixture rasterizer after Stop. Native CPU stores,
raw pointers, other services, suspended channels and native device work remain
outside this terminal counter. It adds no reversible stop, device completion,
backing/alias freeze, or durable Unknown owner. All THREE M4 phases remain
unfinished and ordinary GuestBackingAdmission remains Unknown. Do not merge
intermediate work into main: the user now wants the final M7 zero-copy version.

## 2026-10-22: terminal direct invalidation ingress (within phase two)

Implementation source: `d8b0c3897de6cbd57f10c9d02bcfa27a35086637`, pushed to
`codex/issue-372-fixes`. Exact-source Linux-only run
[39729352143](https://github.com/Splaser/emulator/actions/runs/39729352143)
completed successfully, including production build/unit tests, native Vulkan
controls and integration regressions. Android was excluded; independent artifact
verification remains incomplete.

The previous Linux-only run
[39717929077](https://github.com/Splaser/emulator/actions/runs/39717929077)
completed successfully at `4786d2de77def3e0a721def442b8c2cb91ce0108`, including
production compilation, native CTests, Vulkan/F2/F3 controls and final regressions.
Android was skipped. This is GitHub job success; independent artifact recovery is
not yet complete.

ThreadManager::Stop previously joined command-worker execution but its direct
InvalidateRegion/FlushAndInvalidateRegion methods could still touch the rasterizer
outside queue admission. They now acquire a TerminalIngress entry for external
calls, retained until the actual callback returns or throws. Entry is lock-free
load/fetch-add/fetch-sub with no CAS contention refusal, allocation, wait, mutex,
or waiter notification. Delayed prefix reads are checked at the admission RMW.
Only cold Join sleeps, after direct/queue closure, worker cancellation/join and
unexecuted queue capture destruction. Cancel captures before waiting for an
entered caller that may depend on their retirement. Already entered GPU-owner
invalidation may finish inline under worker-join ownership. Pre-start/post-stop
external calls refuse before touching rasterizer. Facade lifetime, serialized
start and terminal owner-thread teardown remain caller obligations.

Actual ThreadManager/RunThread/MPSC queue tests pass three normal repeats, with
five precise direct controls (premature guard release, missing Stop ingress,
join-before-capture-cancel, late entry bypass, rejected entered-owner work), plus
the existing blocking-handoff negative. Tests include both methods, 8,000 healthy
concurrent calls, exception exit, late RMW and cancellation dependency. GPU,
renderer and FenceManager lifecycle regressions also pass three repeats; actual
gpu_thread.cpp syntax passes. Logs:
`.diagnostics/m4-direct-invalidation-{thread,gpu,renderer,fence,syntax}.log`.
First-registration positives/three original controls and the actual registration
pipeline pass three repeats, including its original ownership/Unknown/coverage
negative controls. Logs:
`.diagnostics/m4-direct-invalidation-{first-registration,registration-pipeline}.log`.
The workflow now runs direct ingress tests early and uploads their exact-source
manifest, logs and actual-body fixtures.

Scope is deliberately terminal: no new live/reversible GPU freeze, device
completion, Guest-backed admission or durable Unknown resource owner. The actual
GPU::Impl::OnCPUWrite sibling route is an independent successful bypass control
in the test fixture. Other rasterizer/service/raw calls, suspended channels,
parked continuations and native GPU work still need their own owned closure/join/
completion protocol. Cache/fault publication still uses its independent path.
All THREE M4 phases remain unfinished; ordinary GuestBackingAdmission stays Unknown.
Do not count this producer-family lifetime fix as a separate M4 milestone.

## 2026-10-22: repair CI negative control and cache ASID owner routing

Implementation source: `4786d2de77def3e0a721def442b8c2cb91ce0108`, pushed to
`codex/issue-372-fixes` with the preceding negative-control fix. Exact-source
Linux-only run [39717929077](https://github.com/Splaser/emulator/actions/runs/39717929077)
completed successfully; Android was excluded. Actual production
`gpu_device_memory_manager.cpp` syntax also passes locally
(`.diagnostics/m4-device-cache-owner-production-syntax.log`). Artifact recovery
must require the new cache owner manifest, three repeats and three precise mutants;
the ignored recovery helper is updated. The existing heartbeat remains PAUSED.

The atomic-cancellation run
[39713592657](https://github.com/Splaser/emulator/actions/runs/39713592657)
FAILED at controlled arena verification. All early batch/stop/cohort/mapping/owner
checks passed, but native production compilation and later validation were skipped.
Do not report that run as a successful build. The closure-drop mutant stored zero
into a word containing an active entry count. The next entry exit underflowed it,
invented Grouped membership with no gate owner, and crashed (-11) before its intended
assertion. Commit `aafe57079b` fixes the negative control by clearing only Closed/
Unknown and preserving count/membership; the arena runner now requires the precise
unfinished-closure ownership assertion. Full real HostMemory arena positives/eleven
mutants and ProducerDrain regressions pass locally in
`.diagnostics/m4-arena-drop{,-primitive}-regression.log`.

Cache ingress audit found a real production owner-routing bug, not merely an
unclosed route: UpdatePagesCachedCount changed memory_device_inter on an ASID
transition but left the discriminator at the first ASID. A contiguous A-B-A range
therefore routed its final A span to B. It now flushes to the previous owner, then
advances both ASID and Memory. The new run_device_cache_owner_tests.py reuses the
generated actual Memory mapping/cache bodies and links real HostMemory/PageTable,
extracting the actual device counter body and Maxwell forwarding method. Injected
translation/counter layout plus a recording-only callback annotation test A-B-A,
B-A-B, A-B-B-A, A-B-C-A, cache and uncache, exact callback owner/address and untouched
PTEs in other processes. Three positives/three precise mutants (old discriminator,
switch-before-flush, missing boundary flush) pass locally:
`.diagnostics/m4-device-cache-owner-regression.log`. The workflow runs this directly
after mapping verification and uploads its extraction manifest/logs.

This is routing evidence, not process lifetime retention or cache/fault closure.
Actual cache producers mutate tracking/counters under cache/range locks BEFORE
the Memory wrapper. NCE fault invalidation and direct ThreadManager rasterizer
calls bypass command enqueue; Synchronize can leave suspended channels/CPU-write
continuations and proves no Vulkan completion. Do not replace Begin with BeginCold
here or claim the GPU drained from a queue barrier. Detailed producer/effect table
is in M4_F3_PROCESS_GUEST_EXCLUSION.md. Ordinary Guest admission stays Unknown;
all three M4 phases remain unfinished and the cache bypass negative remains intact.

## 2026-10-22: atomic no-effects release across managed domains (phase one)

Implementation source: `e64b7f2b28e6672e7f20aeb65145f4c925084663`, pushed to
`codex/issue-372-fixes`. Exact-source Linux-only validation
[39713592657](https://github.com/Splaser/emulator/actions/runs/39713592657)
failed at the controlled-arena negative control described above. Android was excluded. Independent
artifact verification must require the new batch manifest/four mutants, cohort
eight mutants, and stop five mutants, selected from the exact committed runners.
The ignored artifact recovery helper has been updated for these source variants.

The previous exact-source Linux run
[39705537466](https://github.com/Splaser/emulator/actions/runs/39705537466)
completed successfully at `233cf67b6d58660f4e556eebf26c4c9e561eaabd`, including
the final lifecycle integration regressions. Android was skipped. This confirms
GitHub job success, not independent artifact download/hash recovery.

ProducerDrain::ResumeBatch now validates actual closed/drained closures, publishes
immutable shared-gate membership, reopens local words behind that closed gate,
then opens the shared gate with a final CAS. Seal propagates from a joined member
to the gate, so a failed later local resume/final commit cannot expose an already
resumed CPU. Domains retain the gate after a failed batch is destroyed. A delayed
entry CAS rechecks the gate and rolls back before returning an entry. Ungrouped
entry retains the single CAS path; grouped entry adds shared-gate checks. No raw
coordinator, atomic shared_ptr, callback, wait or retry is added. First group
formation allocates before reopening; failed allocation refuses. Membership is
permanent and reused, can extend to fresh domains, and cannot merge distinct groups.
Member retirement conservatively seals the group. A subset batch never proves
that other group members drained. See M4_F3_PROCESS_GUEST_EXCLUSION.md for the
release table and publication/ABA ordering argument.

GuestExecutionDrain cancels all cores in one batch. ProcessExecutionStop adds its
initialization closure; ProcessCohortStop adds cold publication to that SAME batch,
then commits creation under the previously reserved/validated roster mutex.
No later fallible step follows gate commit. The stopped backend latch rechecks
initialization/core uncertainty instead of hiding a later Seal. APIs remain
single-orchestration-thread, no-effects precursors with no asynchronous adoption.

Local Linux checks passed:
- Resume batch: three normal and three scheduled production repeats, four precise
  mutants (early group open, missing Seal propagation, destructive batch drop,
  omitted post-entry check); reused/extended groups, subset/stale cancellation,
  member retirement and pending/abandoned/incompatible-group refusal.
- Actual cohort bodies: three repeats/eight mutants, including omitted publication
  membership and the former sequential release; Seal after the first local resume
  and after all local resumes keeps CPU/init/publication/creation closed.
- Actual stop bodies: three repeats/five mutants, adding stopped-forgets-seal.
- Existing ProducerDrain, mapping publication, teardown, cross-call ownership and
  roster suites, plus actual stop/cohort/roster production TU syntax.
Logs: `.diagnostics/m4-batch-{transaction,cohort,stop,primitive,mapping,teardown,
ownership,roster}-regression.log`, `m4-batch-production-syntax.log`.
TSan did NOT run: the WSL runtime exits immediately with `unexpected memory mapping`
before executing tests (`m4-batch-tsan.log`). Do not report a TSan pass. The workflow
now runs the batch suite early and uploads `.diagnostics/m4-resume-batch/*`.

This closes the identified managed-domain no-effects cancellation race. It does
not finish phase one or create effect-bearing release/admission. Cache/fault PTE,
manual lifecycle/facade boundaries, independent CPU/native/service/raw-pointer/GPU
producers and durable resource ownership remain outstanding. All THREE M4 phases
remain unfinished, ordinary GuestBackingAdmission stays Unknown, and the F2/F3
negative controls remain mandatory. EPUB/CBZ tools belong only in E:\Books\epub2cbz;
their accidental emulator commit was reverted, preserving M4 code.

## 2026-10-22: reserve roster cancellation before reopening (phase one)

Implementation source: `233cf67b6d58660f4e556eebf26c4c9e561eaabd`, pushed to
`codex/issue-372-fixes`. Linux-only validation
[39705537466](https://github.com/Splaser/emulator/actions/runs/39705537466)
completed successfully; Android was skipped. ProducerDrain regressions also passed locally in
`.diagnostics/m4-cancel-primitive-regression.log`.

The previous exact-source Linux run
[39562678761](https://github.com/Splaser/emulator/actions/runs/39562678761)
completed successfully at `f9e39ecaa4a1f042b02fcb9ad6b9de3b275d6996`,
including the native cohort test. Android was excluded. This is GitHub job/test
success; independent artifact download/hash recovery has not been completed here.

Cohort cancellation previously reopened execution and cold publication before
trying the roster mutex to reopen creation. Legitimate constructor contention
could therefore cause a false return after CPUs had already resumed. Cancellation
now obtains a private, move-only roster cancellation reservation FIRST. The held
mutex also validates the captured cohort revision, preventing retirement between
the final Poll and reservation from being accepted as the same cohort. Committing
creation then needs no further lock acquisition. No backend/lifecycle callback,
owner Close, or roster re-entry occurs while this reservation is held; the inner
stop is already latched, and only validation/atomic gate operations run there.

The extracted production fixture deterministically inserts contention/retirement
immediately after the final successful cancellation Poll. It checks that refused
cancellation leaves CPU, initialization and cold publication closed. Three normal
repeats and SIX precise mutants pass, including moving reservation after reopening
and removing the reserved revision check. Existing stop positives/four mutants,
roster positives/six mutants and actual roster/cohort production TU syntax pass.
Logs: `.diagnostics/m4-cancel-{cohort,stop,roster}-regression.log` and
`.diagnostics/m4-cancel-production-syntax.log`.

This fixes roster-related partial reopening, not atomic cancellation across all
domains. ProducerDrain Seal can still race the sequential execution/initialization/
publication resumes; a later failure cannot revoke an earlier successful resume.
Before any effect-bearing adoption, a genuine cross-domain release protocol and
durable Unknown owner retention are required. The current types cannot adopt GPU
work, and ordinary GuestBackingAdmission remains Unknown. All three M4 phases
remain unfinished; do not count this lifecycle fix as an additional milestone.

## 2026-10-21: phase-one sustained cohort integration (not a separate milestone)

The user consolidated remaining M4 work into THREE phases: (1) retained process
cohort plus sustained creation/initialization/publication closure and actual
execution drainage; (2) every independent CPU/native/service/raw-alias/GPU
producer closed and joined; (3) actual ordinary Guest-backed Vulkan admission,
checked completion and safe release/durable Unknown retirement. Do not count
individual lifecycle fixes as new milestones. All three phases remain unfinished.

Implementation source: `f9e39ecaa4a1f042b02fcb9ad6b9de3b275d6996`.
Exact-source Linux validation:
[run 39562678761](https://github.com/Splaser/emulator/actions/runs/39562678761),
completed successfully, including the native process-cohort test.
Android is excluded. This workflow now has NINE CTests, including the new native
process-cohort case; do not duplicate dispatch. The ignored artifact verifier
detects the ninth test/new cohort manifest and requires its four precise mutants.

TryStopProcessCohort is a new prerequisite API; the old backend-only API remains.
The new API owns an atomic-with-Register creation closure, all initialization
(including is_real=false service setup) and a shared cold publication closure.
It drains initialization/publication BEFORE closing CPU entry, captures actual
roster references BEFORE issuing halt, then drives real ProcessExecutionStop.
It continues to hold the gates, cohort and real backing through move/poll/Unknown.
No snapshot IsCurrent observation is converted into permission without those
continuously owned closures. No asynchronous GPU work can adopt this handle.

Every ordinary Memory binds the shared cold publication domain at construction:
context/map/unmap/protect/debug use BeginCold, reset/move use parent-only entry
before joining local state. Run, metadata/module/interface setup, OS loading,
process-list and application-process publication are also gated. Cold count
contention yields/retries rather than spuriously failing normal publishers;
that API is never used in a fault handler/backend entry/stop poll. Constructor
refusal rolls back kernel bookkeeping and its popped slab slot. The OS wrapper
propagates blocked load/run instead of reporting success. Requesters without a
real Guest PageTable refuse. Existing owner teardown capabilities remain distinct.

Local Linux: three cohort positives/four mutants (late capture, omitted publication
drain, omitted creation gate, unrelated publication gate); concurrent cold-entry
stress; move/stale cancel, zero refs, timeout/late exit and post-stop sealing.
Existing stop positives/four mutants, roster positives/six mutants, full mapping
wrapper controls, teardown, cross-call ownership and ProducerDrain regressions pass.
Actual cohort/process/kernel/Memory/OS-loader production TUs and the expanded native
test TU pass syntax. Logs: `.diagnostics/m4-cohort-{integration,stop,roster,mapping,
teardown,ownership,primitive}-regression.log`, `m4-cohort-{native-,}syntax.log`.
Native runtime passed in that run: its real checked Dynarmic loop also checks creation/
slab rollback, cold publication/reset/move/interface/module/OS refusal and mapping
after known cancellation. Keep the original unchecked-loop negative unchanged.

IMPORTANT: phase one is NOT complete. Cache/fault-related RasterizerMarkRegionCached
still uses the original independent Begin path; the fixture intentionally proves
this bypass. No NCE fault wait/parking was added. Ordinary scalar/bulk/atomic writes,
host callbacks, escaped pointers, unmanaged alias/HostMemory paths, GPU work and
its cache/DAddr publication remain independent producers. Scheduler/teardown and
partial-initialization owner protocols still need integration/audit. BackendsStopped
is not Guest Exclusion. GuestBackingAdmission remains Unknown. There is no durable
async Unknown GPU registry or successful ordinary Guest Vulkan admission yet.

Previous cleanup run 39556788293 at 9c816d1c9f completed successfully (confirmed
live this turn). Post-upstream source 5956cbe934/run 39550694175 also passed, but
its separate artifact recovery still has no verification.json; do not call that
archive independently verified. The known earlier a09bf27bb4 acceptance is below.

## 2026-10-21: clarify direct C++ versus Close unregister ownership

Implementation source: `9c816d1c9f9230e485ba43fe2b88400e706ae2a0`.
Linux-only validation is
[run 39556788293](https://github.com/Splaser/emulator/actions/runs/39556788293),
in progress at this checkpoint; Android is skipped. Do not duplicate dispatch.

KProcess's C++ destructor now unregisters kernel bookkeeping only inside the
live-registration branch. Slab Destroy consumes the registration and leaves
unregister to KAutoObject::Close. Production slab Free never calls the C++
destructor, so the earlier normal Close path did not actually double-unregister;
this is explicit ownership cleanup rather than a claim of that production bug.
The source-extraction witness now compiles the actual inherited slab Destroy
body, actual Close/UnregisterWithKernel and both KProcess lifecycle methods,
while injecting allocator/Finalize/kernel registry counters. It proves exactly
one unregister for Close and direct C++ destruction separately, including later
fixture storage cleanup after a consumed registration.

Local Linux checks passed: production KProcess syntax; three roster positives;
the previous four mutants and two new precise unregister mutants (unconditional
destructor unregister and omitted direct-destructor unregister). The roster suite
now has six negatives. The ignored artifact verifier selects four or six from the
exact committed runner, and requires the expanded source hashes. No ordinary
Guest admission or roster-to-ProcessExecutionStop adoption was added: IsCurrent
remains an instantaneous revision check, requiring continuously held creation,
initialization/publication closures before any future cohort authorization.

Post-upstream-merge run 39550694175 at 5956cbe934 completed successfully. Its artifact
12325147067 has expected archive digest
`81e09009acfa75ef769b0e751513c74e0a7c10806b5481474659e8301af28223`, but independent
download/hash verification is still pending: `.diagnostics/ci-39550694175/` has a
partial archive and no verification.json. Recovery tool session 14972 was lost;
OS python/gh processes were still active (PIDs 25608/30584/27216 when checked).
Inspect them/output before restarting `.diagnostics/recover_linux_run.py` to avoid
a duplicate download. Do not describe this archive as independently verified yet.

## 2026-10-21: merge upstream PRs 404, 405 and 406

Upstream main `5b543e71769bdd4c116c8b66a8974224d0c1e1e8` was merged into
`codex/issue-372-fixes` as `5956cbe934` and pushed, without conflicts. This brings
the Android max-clock thread fix, mobile GPU CPU-side Z-pass counting and Thor
dual-screen support. The automatic vk_turbo_mode merge preserves the existing
branch code and adds the upstream thread naming/timed wait. Diff whitespace checks
pass; no Android build was requested.

Post-merge Linux validation is
[run 39550694175](https://github.com/Splaser/emulator/actions/runs/39550694175).
Use this exact merged source for integration acceptance; preceding roster-only
run 39550288914 predates the upstream merge and remains separate evidence.
The lifecycle roster/ordinary-write closure roadmap below remains unchanged.

Fork main was inspected but not changed: it has a separate older feature history
(263 commits absent from upstream), and a read-only merge-tree probe reports
historical content/modify-delete conflicts involving upstream reverts and the
Dynarmic storage-layout transition. No conflicted tree was committed, no branch
history was reset and no files/submodule contents were removed to force checkout.
Current working branch remains the PR375 branch.

## 2026-10-21: retain a lifecycle inventory of real KProcess owners

Implementation source: `9068f9c3d867d2882ec801dfe7ee5a125afe20f5`.
New Linux-only acceptance is
[run 39550288914](https://github.com/Splaser/emulator/actions/runs/39550288914),
queued at dispatch; do not duplicate it. It still has eight CTests, now with the
native roster witness in checked/cross-process/unchecked execution tests, and a
new dedicated three-repeat roster suite with four precise mutants. Android is
excluded. Recover this exact source before claiming native/full-workflow acceptance.

ProcessOwnerRoster is separate from the OS wrapper list. KProcess registers after
Memory construction and before any Initialize; slab Destroy marks the registration
retiring before inherited Finalize/Free. A thrown Finalize leaves that record
retiring and future snapshots refuse. Exact monotonically assigned IDs prevent
old completion from erasing a new owner at a reused address. C++ destruction also
marks retiring; a first-declared registration member completes retirement after
all PageTable/Memory/member destruction. Its destructor unregisters the object's
kernel bookkeeping as well (the previous native stack candidate left an entry).

TryRetainProcessOwners captures real scoped references using checked TryOpen;
zero-reference construction/final-Close losers refuse without resurrection.
Capture/current validation try-lock once. Partial pins release after unlocking,
even when the caller drops the last other reference mid-capture and finalization
reenters the roster. Stale or moved snapshots cannot validate; stale snapshots
keep their pins. This API is cold retention only, not a bounded halt, alias freeze
or admission certificate. Kernel/System must outlive retained process references.

Local Linux validation passed: three dedicated repeats; ignore-retiring-owner,
omit-registration-revision, erase-reused-address and omit-owner-pin mutants;
the existing three stop-body repeats/four stop mutants; actual production
roster/process/kernel and expanded native test TU syntax. Logs are
`.diagnostics/m4-process-owner-roster-{regression,stop-regression,syntax,native-syntax}.log`.
Actual source-bound suite outputs are `.diagnostics/m4-process-owner-roster/`.
Native code retains an actual unpublished process through Terminate/final Close,
rejects a real zero-ref constructor and reacquires the roster after finalization.
Runtime result remains pending. The ignored artifact verifier requires the new
manifest, four mutants and native marker when its exact source contains the roster.

Previous initialization/storage source a09bf27bb4 passed the entire Linux workflow
39540116589. Artifact 12320539901 is independently verified: 180 source hash entries,
103 unique files, 16 manifests, eight CTests, archive SHA256
`6267209c50becfca1166b8f15b3025fca0e7386157ed79f0bb61e45a3f3fb5a7`.
Evidence: `.diagnostics/ci-39540116589/verification.json`; recovery is finished.

Next: check/recover the new CI, then design adoption of the retained cohort under
continuous initialization/creation/publication closure and ordinary write/teardown
exclusion. ProcessExecutionStop DOES NOT yet adopt the roster. A one-time IsCurrent
check cannot exclude future service constructors or cover arbitrary Memory views,
failed-initialization remnants, raw aliases or GPU/device owners. Guest admission
and all four full M4 gates remain Open; preserve the original independent negatives.

## 2026-10-21: close and drain initial guest process PageTable construction

Implementation source: `a09bf27bb4a737604479f38e5b0f3dfe28b20881`.
Linux-only acceptance dispatched as
[run 39540116589](https://github.com/Splaser/emulator/actions/runs/39540116589),
queued at this checkpoint. Android remains excluded. Do not duplicate this run.
The independent preceding storage run 39537228855 at 64d67e2c30 is still running:
full build, all eight native CTests and the storage-owner suite passed; the
unsupported synchronization regression step is still active. Neither pending
result is full-workflow acceptance yet. SHA-specific concurrency preserves both.

KernelCore now owns a session-specific guest process initialization domain.
Both PageTable-building Initialize overloads enter before allocation/mapping and
retain the entry through initial TLS, initialized publication and local rollback.
A private InitializeCommon avoids nested entry after closure. The public real
common initializer also refuses before effects; is_real=false service creation
is deliberately outside this domain and still requires separate closure/join.
ProcessExecutionStop closes initialization before cores and drains it before
requesting halt. Unknown seals both domains; cancellation reopens initialization
last. Existing Memory writes/mappings, metadata/interfaces/module setup, service
creation/callbacks, teardown and a complete retained process roster remain Open.
This is not Process-wide Guest Exclusion or ordinary guest admission.

Local Linux validation passed: three actual stop-body repeats and four precise
mutants, including bypassed initialization drainage before halt; active initializer,
timeout/late exit, occupied-closure refusal, move and stale cancellation controls.
Actual process/kernel/stop production TUs and the expanded native test TU pass
syntax checks. Native witnesses now require all three real-process Initialize
overloads to return ResultBusy without initialized publication/resource-limit
reference effects while closed, and a mapped process to initialize/retire after
known cancellation. Their runtime result is pending the exact new-source CI.
Logs: `.diagnostics/m4-process-initialization-{regression,syntax,native-syntax}.log`;
source-bound outputs: `.diagnostics/m4-process-execution-stop/`.

The earlier running-owner run 39533283269 at d98272e36a is fully accepted and its
artifact recovery has finished: artifact 12317661714, 170 source hash entries,
98 unique source files, 15 manifests, eight CTests, archive SHA256
`9d5cfe1feb2ccd7f3b3d5119d68474d67c29cc519a21aca5eb14cab082eea65b`.
Evidence is `.diagnostics/ci-39533283269/verification.json`. No recovery download
is still active. The ignored verifier now detects the initialization API in each
exact source and requires its fourth mutant plus native refusal marker.
Next resume: recover pending CI/artifacts, then advance the actual owner roster
and ordinary writer/publication closure without treating the OS wrapper list as
an exhaustive census. Keep unchecked-loop/NCE/SVC/raw-alias/GPU negatives intact.

## 2026-10-21: retain the real backing allocation through process stop

Implementation source: `64d67e2c309b185e9b91eac22725c7f774522259`.
Current acceptance attempt is
[Linux run 39537228855](https://github.com/Splaser/emulator/actions/runs/39537228855),
still in progress with Android skipped. Recover this exact run before a duplicate
dispatch; expect eight CTests plus the dedicated three-repeat storage owner
witness and epoch-only-owner negative. The ignored verifier now requires that
manifest/log set when the committed common CMake list contains the new header.

HostMemory now provides an opaque RetainedBackingStorage holding its actual Impl
and epoch. Capture neither materializes lazy chunks nor exposes bytes, FDs or
mapping authority. KProcess captures it before process references/core closure;
ProcessExecutionStop retains it after closure destruction and process Close.
Moved-from facades refuse capture. Epoch retirement/replacement keeps Poll Unknown
while the old allocation stays alive for the handle lifetime. This does not make
System/Kernel/facade destruction concurrent with API entry legal or retain a GPU
device; normal writer/alias/native-resource closure is still required.

Local Linux acceptance: three real HostMemory/OS lifetime repeats plus the precise
epoch-only fake-owner negative; facade/token move, epoch retirement, replacement,
final unmap/FD cleanup, zero-chunk lazy capture/later chunk retention, and capture
under mapping freeze without escape/reopening. The actual stop fixture passes
three repeats/three existing mutants, now also proving failed capture before core
closure and allocation retention through process finalization. Full existing
HostMemory mapping-freeze controls and production/native-test syntax checks pass.
Logs/manifests are in ignored `.diagnostics/m4-backing-storage-owner/`,
`.diagnostics/m4-process-execution-stop/`, `.diagnostics/m4-host-mapping-freeze/`,
and `.diagnostics/m4-backing-storage-*-syntax.log`.

The preceding cross-process run 39533283269 at d98272e36a has completed successfully;
its independent artifact recovery is in progress. That result predates this new
storage owner and cannot accept it. A fresh Linux-only source run is required.
The new workflow includes the dedicated storage witness and its artifact files.
Ordinary GuestBackingAdmission and F2/F3 remain Open; no Android build is requested.

Local recovery for that preceding run is still active in terminal session 86386
at this checkpoint. Poll it before starting a second download on this machine.
The recovery script now checks an existing ZIP's digest and uses a partial file
for future downloads, so an interrupted zero-byte archive is not accepted/reused.

## 2026-10-21: retain the actual running backend owners

Implementation source: `d98272e36abd40dd63dc4367c14b50194ebe4fb5` on
`origin/codex/issue-372-fixes`. Current Linux-only validation is
[39533283269](https://github.com/Splaser/emulator/actions/runs/39533283269);
it is in progress and Android is skipped. Resume by recovering this run before
another dispatch or broader M4 changes. Verify eight CTest processes, including
the new cross-process marker, and three stop assertion mutants against that exact
source. The ignored recovery verifier now distinguishes the seven-test baseline
from this eight-test source by reading its committed CMake configuration.

The global stop previously retained only its requesting KProcess. It now captures
each actual running process under that core's try-locked context before halt,
retains its PageTable/Memory lifetime with the requester and validates identity
before acknowledgement. Occupied owner slots refuse before effects. Moving a
request transfers all references; closure destruction precedes owner release.
Captured owners remain held after unsupported requests and late backend exits,
for the handle lifetime. This does not create a durable Unknown GPU registry.

Linux actual-body tests pass three repeats and three assertion mutants, including
removing the pre-halt foreign-owner reference. Cases cover two running owners and
a third requester, delayed UnlockThread, move and exact final release, invalid
output slots and unsupported/late-exit retention. Existing ProducerDrain tests
and all fifteen assertion mutants pass. Real production stop/core and native
test translation units pass local Linux syntax checks with actual project types.
The new cross-process Dynarmic CTest is pending remote runtime acceptance; it is
an eighth CTest process, preserving the original unchecked-loop refusal separately.

See the running-owner resource table in
[the implementation record](M4_F3_PROCESS_GUEST_EXCLUSION.md). This retains only
the observed backend cohort. Full process birth/publication/retirement closure,
ordinary Memory and service writes, escaped pointers, storage pins, all aliases
and GPU owners remain Open. Do not use the OS wrapper's process list as a complete
roster or hold its mutex across drain. GuestBackingAdmission remains Unknown.

## 2026-10-21: complete Linux baseline accepted

Run [39528346917](https://github.com/Splaser/emulator/actions/runs/39528346917)
passed in full at `9fdec2afa12775caf402c1dd7b59d41bb3cc403b`; Android was skipped.
This includes the full production build, all seven CTest processes, focused
ownership/mutation checks, repeated real Vulkan witnesses, original F2/F3
negatives, real HostMemory checks and the complete final regression batch.

Artifact `12316026001` was downloaded and independently verified against its API
digest: SHA-256 `16ecf784de58ff1431a3fa365eb570183c882d37ca0786606ac779cc8ad28dbc`.
All 170 hash entries / 98 unique files / 15 manifests match the exact run source;
the Dynarmic gitlink is unchanged. All seven CTest markers, checked-loop stop and
unchecked-loop refusal, three actual-body positives/two stop assertion mutants,
and the original F2/F3 negatives are present. Evidence is under ignored
`.diagnostics/ci-39528346917/`, with `.diagnostics/recover_linux_run.py` as verifier.

This accepts the existing backend-stop prerequisite and ownership fixes only.
It does not certify arbitrary Dynarmic/NCE stop, all processes/services/raw aliases,
mapping closure, backing storage pinning, GPU drainage or ordinary guest admission.
Any following code change requires its own exact-source Linux validation.

## 2026-10-21: office continuation recovered the final regression failure

Run [39518766603](https://github.com/Splaser/emulator/actions/runs/39518766603)
at `89f034d20b9b3e956bb5570ec198ddc9a9fd64f1` completed with a failure in
`run_gpu_plan_tests.py`: its shared resolver fixture still modeled the small-page
table as a vector without the production `ReserveRange` API. The complete build,
all seven CTests, synchronization/mutation controls, repeated Vulkan witnesses,
F2/F3 negative controls and real Linux HostMemory checks passed before this failure.
This is partial evidence, not full-workflow acceptance.

The shared resolver fixture now models reservation success/refusal; both the
resolver and real-HostMemory GPU-plan tests check multi-page refusal before any
PTE publication, even generation restoration and successful retry. GPU plans
also refuse to pin the unpublished range. The production reservation guard stays
unchanged. Local Linux GPU-plan positives and existing paired-remap mutation,
resolver and physical-shadow checks pass. The remaining regression batch and
fresh exact-source remote Linux validation must complete before broader M4 work.
No Android validation or ordinary guest admission is enabled.

The local continuation also found missing `DEBUG_ASSERT` definitions in the
standalone representation-pin, shared HostMemory and first-registration assertion stubs after the
upstream SlotVector restore. Both now retain active assertion checks. The real
payload/pin lifetime controls, cache representation, physical admission and
page-writer suites pass after that fixture correction. Run `39527273477` at
`c160ad1fc9` was cancelled before full validation because it lacks this correction;
do not use it as acceptance.

The preceding failed run's artifact `12310269482` is independently recovered:
ZIP SHA-256 `74b73e4803a867a7a6208b736898f19b1ac2bd27b58ec84a120da028b72a5834`;
109 hash entries / 78 unique files / 12 manifests match its exact Git tree, and
the Dynarmic gitlink remains `e40fa8ba6d739807b0965c5e3a60e69f2353f209`.
All seven CTest markers, three actual-body stop positives/two intended assertion
mutants and the original F2/F3 negative evidence are present. The verifier and
download live under ignored `.diagnostics/ci-39518766603/` and
`.diagnostics/recover_linux_run.py`; recover remote artifacts on another machine.
This verified partial evidence still does not make the failed workflow a pass.

The local final-regression sweep completed all 33 commands: 28 passed directly,
and all five affected commands passed after the assertion-stub repairs. The
first-registration controls and repaired registration pipeline passed ten positive
repeats and all their required negative controls. The final observer, Unknown
retention and physical-coverage controls also passed. Local logs are retained in
ignored `.diagnostics/resume-linux-regressions/` and
`.diagnostics/resume-*-fixed.log`; the initial sweep's five failures remain in its
original results.json, with repair reruns recorded separately.

Current remote acceptance attempt:
[Linux run 39528346917](https://github.com/Splaser/emulator/actions/runs/39528346917),
exact code/test source `9fdec2afa12775caf402c1dd7b59d41bb3cc403b` (fixture fixes
`c160ad1fc9` and `9fdec2afa1`). It is still in progress; Android is skipped.
Recover that run and independently verify its exact-source artifact before
claiming complete acceptance or extending M4. The local sweep is standalone
fixture evidence, not another native Vulkan run or ordinary guest admission.

## 2026-10-21 08:20 Asia/Taipei: paused for office-machine continuation

User requested a progress marker and no further development. This is a
**documentation-only pause**, not full acceptance. Continue on
`origin/codex/issue-372-fixes` (Splaser/emulator; upstream PR #375).
Latest code/test commit is **89f034d20b9b3e956bb5570ec198ddc9a9fd64f1**;
the checkpoint commit adds only documentation. Personal main and the CI helper
checkout remain separate; do not merge/promote this M4 work merely to resume.

### First action on the next machine

Inspect local modifications, remotes, worktrees and the PR head before syncing.
Use the existing issue-372 checkout, then fetch and fast-forward only if safe.
Read this section before starting new F2/F3 work.

```powershell
git status --short --branch
git remote -v
git worktree list
git fetch origin
git pull --ff-only origin codex/issue-372-fixes
gh run view 39518766603 --repo Splaser/emulator
gh run view 39518766603 --repo Splaser/emulator --log-failed
```

Run the pull command only while already on the intended branch. Recover the
existing run before dispatching any duplicate native validation:
[Linux run 39518766603](https://github.com/Splaser/emulator/actions/runs/39518766603)
is testing exact source **89f034d20b9b3e956bb5570ec198ddc9a9fd64f1**.
At pause: production CLI/shader/tests build and all seven CTest processes passed;
`Validate unsupported synchronization evidence and mutation controls` is still
running. Later repeated native witnesses, HostMemory and the final regression
batch have **not yet been accepted on this source**. Android is intentionally
skipped. Leave this run running when changing machines.

### Changes since the previous checkpoint

- `ac3fa60a68`, `883e7f904b`, `10e58d6545`: restored pinned Dynarmic include
  compatibility and tuple/pair default hashing without modifying its sources or
  pin. Descriptor/std::hash and explicit caller hashers remain intact.
- `b10519eea5`: selectively restored stable syncpoint waiter handles after the
  upstream revert; the retained cancellation regression now passes.
- `51266b64ef`: checked TBNZ native stop witness plus the original unchecked
  fast-link continued-write refusal witness in a separate OS process. This does
  not repair arbitrary unchecked-loop stop or enable guest admission.
- `f14d696718`: free-area fixture extracts actual ASSERT as well as R_ASSERT;
  validates report-and-continue behavior and failed-query guard sensitivity.
- `1acf6cbf9b`, corrected by `89f034d20b`: remap fixture models the existing
  ReserveRange API, refusal without PTE publication and successful retry. Its
  negative control keeps the reserve call and ignores only its result so it
  fails at the intended refusal assertion, not an earlier call-count check.

Today's changes after `51266b64ef` are tests/documentation only; no production
source, dependency pin, optimization or scheduler behavior changed.
Windows fixture positives and both new negative controls pass. The 24-case
candidate passed three repeats with actual ThreadManager. ThreadManager,
scheduler cancellation, presentation, fencing and completion lifecycle fixtures
passed ten repeats. A broader Windows sweep was not a full pass: Boost/Vulkan/fmt
headers are missing here and dependent fixtures then refuse stale prerequisites.
The Linux workflow is the remaining acceptance boundary; do not label those
local dependency failures as successful Windows validation.

### Evidence already recovered, and what remains

Run `39432258674` at **51266b64efd77033d9bd1abac4d8c3c270c1e544** compiled all
requested production targets and passed all seven CTests (119 ordinary cases),
synchronization/mutation controls, repeated native witnesses and real Linux
HostMemory/backing import checks. It failed only when the last regression batch
reached the stale free-area ASSERT fixture; the remap fixture drift was then
found locally. Runs `39518428253` and `39518727858` were cancelled as superseded;
use `39518766603` for current-source acceptance.

The old run's artifact **12272700625** was downloaded and independently checked:
ZIP digest **sha256:eb4e23135035065af3edde8c6805e0ff7d522df57ae23bb2ff353bc9aef2d395**;
109 source hash entries / 78 unique source files / 12 manifests matched its exact
Git tree. All seven CTest results and five process-stop positive/mutant logs were
present. This is partial-run evidence, not a successful full workflow.

On successful completion of the current run, download its artifact, verify the
archive digest against the GitHub artifact API, source.txt against the exact
run SHA, the unchanged Dynarmic gitlink, every extraction manifest source hash,
all seven CTest markers and the process-stop positive/negative logs. Also inspect
the final regression log for both new fixture controls and retained F2/F3 defect
witnesses. Record artifact ID/digest and actual hash counts, then update this
handoff and M4_F3_PROCESS_GUEST_EXCLUSION.md with qualified acceptance. Do not
reuse the old artifact's counts or digest for the new run. If it fails, inspect
the first actual diagnostic before changing code or dispatching another run.

Local downloaded ZIPs, generated binaries, logs and verifier under `.diagnostics/`
are ignored and are **not pushed**; use the remote workflow logs/artifacts on the
next machine. The tracked handoff contains all identifiers needed to recover them.

### Scope still blocked

F2/F3 remain Open. GuestBackingAdmission remains Unknown. The checked-loop native
stop success does not close the unchecked LinkBlockFast counterexample, NCE,
independent service/raw-pointer writers, mapping/export aliases or GPU producers.
The original unchecked loop continues stores after budget exhaustion, refuses
cancel/re-entry and retains owners until OS termination via the isolated test
process; no successful in-process join/cleanup is claimed. HostMemory remains a
high-risk module and lazy VMA merging remains disabled pending mapping-use and
target evidence. Do not enable ordinary guest admission or broaden producer
routing from these controlled results.

## 2026-10-21: final regression fixture recovery

Linux run `39432258674` at `51266b64ef` compiled all requested production
and test targets and passed all seven CTest processes, the unsupported-evidence
mutation batch, repeated native F3/transfer/provenance witnesses and real Linux
HostMemory/backing import checks. The F3 fixed-mode exit 42 is the intended
negative control, not a new coherency fix.

The last regression batch stopped because the extracted R_ASSERT now expands
to ASSERT, while its standalone FindFreeArea fixture did not define ASSERT.
The fixture now extracts the actual ASSERT macro too, mocks only logging and
the runtime assertion handler, and checks failed-query assertion counts when
that handler continues (use_debug_asserts disabled). Windows Release compilation,
retry/fallback behavior and the guard-removal negative control pass. Production
code and scheduling behavior are unchanged. Complete Linux acceptance remains
pending until the resumed batch and exact-source artifact are verified.

A local sweep also found a second stale extraction fixture: the remap page-table
array lacked the existing ReserveRange API. It now models reservation success
and refusal, checks both pages remain unpublished on refusal, even generation
restoration and a successful retry. Removing the production reservation guard
must fail that specific assertion. Both fixtures pass locally; Windows checks
that need absent Boost/Vulkan dependencies remain unverified locally. The earlier
replacement run was cancelled before its long test stages and is superseded by
a run containing both fixture repairs.

## 2026-10-20: compiled native run exposes unchecked-loop stop limit

Run `39427948982` at `10e58d6545` compiled/linked the real CLI, shader tool,
ordinary tests and native scheduler executable. All ownership and synchronization
fixtures/mutants passed. CTest's four existing native scheduler/F3 processes passed,
but the ordinary test process failed the retained syncpoint cancellation case,
and the new unconditional Dynarmic store-loop returned Unknown before cleanup
hung at its 45-second deadline. There is no full-run/native-stop acceptance.

Syncpoint cancellation was a genuine upstream-revert integration regression: its
retained test expected stable handles, but the reverted manager returned movable
vector iterators. `b10519eea5` selectively restores #380's numeric handle repair.
All three existing case bodies pass locally against actual production manager code.
No unrelated #380 translation changes are restored by that commit.

The original store loop uses B -8. In the current pin, B_uncond selects
LinkBlockFast, whose linked path jumps without inspecting halt_reason. Ordinary
HaltExecution/Interrupt therefore cannot stop this hot loop. Increasing budget or
changing ordinary JIT settings is not a repair and is not done here.

The original unchecked loop is retained as a separate native negative process:
actual stores must continue after Poll returns sticky Unknown, cancellation and
new execution admission must refuse, and all real owners stay alive until OS
process termination. It deliberately uses _Exit only after those assertions; no
in-process cleanup, reuse or successful join is inferred. CTest bounds the child.
A separate checked TBNZ loop (odd counter incremented by two) tests actual halt,
backend return, stable guest bytes, closed re-entry and stale-cancel behavior under
the same ordinary JIT settings. This is a supported-subset witness, not satisfaction
of the original unconditional-loop success expectation. Arbitrary guest execution
closure remains blocked on the unchecked-link issue, as well as NCE/service/GPU gates.

CTest is now before the long unchanged sensitivity batch so runtime failures
surface promptly; both still run in the complete Linux workflow. Native execution
of these revised witnesses and remaining regressions is pending. No production
stop algorithm or optimization behavior is changed; F2/F3 remain Open.

## 2026-10-20: compatibility include-order follow-up

Run `39424847488` at `883e7f904b` exposed a missing global namespace qualifier in
our new adapter: after backend headers declare Dynarmic::Common, Common::TupleHash
resolves there instead of the embedder. It now explicitly uses ::Common::TupleHash.
The real container test declares the nested namespace before including the adapter;
a Windows strict-warning smoke passes in that order and the unqualified-name
counterfactual fails. This closes the local smoke's original include-order gap.

The same run found a pre-existing -Wshadow warning in the pinned dependency's
LocationDescriptor constructor when included by the new strict test. Only that
third-party header directory is marked SYSTEM for the test target. Project and
compatibility headers remain non-system under the existing warning policy; no
-Wno-shadow or global warning downgrade is added. The pin/source remain unchanged.
Complete Linux native/runtime acceptance is still pending; the failed run did not
execute CTest. Resume the exact new Linux run before advancing M4.

## 2026-10-20: pinned Dynarmic tuple hashing after include recovery

Linux run `39421826300` at `ac3fa60a6841d345a53e1fbf37fc5fefdd808b54`
passed the early native translation units, all owner/process-stop prerequisites
and synchronization/mutation controls. Complete production compilation then exposed
two restored Dynarmic default-hash key shapes: tuple<bool, unsigned long, int, int>
and tuple<IR::LocationDescriptor, unsigned>. std::hash is unavailable for tuples;
no native execution-stop CTest ran. The include bridge repaired the first blocker,
not full build acceptance.

Static build-only compatibility headers now select Common::TupleHash only for
standard tuple/pair keys and preserve std::hash for ordinary keys and explicit
caller hashers. Their include priority propagates from the actual pinned target
so backend and consumer TUs use one alias definition. The submodule sources and
pin are unchanged, and CPM's independent provider is unaffected. Do not globally
switch to boost::hash: LocationDescriptor has its own std::hash specialization.

A production Catch2 case checks actual Boost map/set aliases with descriptor,
callback tuple, descriptor tuple/pair and explicit colliding hasher keys. It is
compiled only for this compatibility target. A Windows smoke using the actual
hash selector and real pinned LocationDescriptor passed against standard test
containers; this is not native Boost-container or Linux runtime acceptance.

The complete native build now precedes the long synchronization-mutant batch,
but that batch still precedes native execution. This changes validation order
only, permitting another compiler failure to surface before repeating those
unchanged sensitivity checks. Recover the new exact-source Linux run before
extending M4. Android remains skipped; F2/F3 and guest admission remain Open.

## 2026-10-20: home-machine resume and restored Dynarmic include boundary

Both paused Linux runs have completed unsuccessfully: `39391213895` at
`5a6eee7b58` and `39394434521` at `a2365189ec`. Their first native build failure
is the same missing `common/x64/xbyak.h` include in the restored pinned Dynarmic
`backend/x64/block_of_code.h`. The newer run's complete failed-build log contains
no other distinct compiler diagnostic. Neither run accepts process execution stop.

A forwarding compatibility header restores that legacy include path while reusing
`dynarmic_xbyak_abi.h` and its existing ABI/util split. It introduces no second
Common::X64 definitions, changes no container macros, and leaves both dependency
provider choices and the Dynarmic gitlink unchanged. The native store-loop CTest
and the existing full Linux gate are the required validation. A local Windows
syntax probe was stopped by unavailable Boost development headers; it is not a
successful compiler check. No local Android build is requested.

Continue recovering the exact new Linux source/run before extending the four
M4 gates below. NCE, service/raw/GPU exclusion, guest-backed admission and F2/F3
remain Open; this is build recovery only. Local main was separately fast-forwarded
to `b6a2ce9488`; the active PR worktree was fast-forwarded to `c41887b314` before
this repair. No newer upstream changes are merged by this resumption.

## 2026-10-20: pause checkpoint for quota reset

Source and workflow changes are committed and pushed on
`origin/codex/issue-372-fixes` through `a2365189ec17139ed20f86e0002012bad85863f0`.
The execution-stop implementation is `9ad0852457`; upstream interface recovery is
`5a6eee7b58`. This checkpoint adds documentation only and starts no new build.

Last observed remote validation state (pending, not acceptance):

- [Merge recovery 39391213895](https://github.com/Splaser/emulator/actions/runs/39391213895),
  exact source `5a6eee7b58f32bd5f38eadbea87344bdb93ed127`: early native translation
  units passed; CLI/shader-tool/unit-test build was in progress.
- [Execution stop 39394434521](https://github.com/Splaser/emulator/actions/runs/39394434521),
  exact source `a2365189ec17139ed20f86e0002012bad85863f0`: native build dependency
  installation was in progress. The real Dynarmic store-loop CTest remains pending.

Local Linux checks passed: execution-stop actual-body fixtures (three positive
repeats and two assertion mutants), ProducerDrain fixtures and fifteen mutants,
process teardown, process unmap and cross-call ownership audit fixtures and their
mutants. Production syntax checks passed for the new stop implementation,
KProcess, PhysicalCore and both Dynarmic interfaces. These results do not certify
native runtime behavior. The optional local native-test syntax probe initially
stopped at missing development headers; X11/Wayland/XCB headers were subsequently
installed, but that probe has not been rerun.

Resume by recovering both workflow results and artifacts against their exact
source commits. Repair any native compile/runtime failures before extending M4.
Then continue the four gates in the process guest exclusion implementation record:
process/API/mapping/service close-and-join; actual NCE and independent producer
exit; GPU/device drain with retained Unknown isolation; ordinary guest-backed
Vulkan admission. Current stop acknowledgement covers backend execution only.
SVC/service writes, escaped aliases, real storage pinning and GPU ownership are
still Open; NCE active stop is unsupported and GuestBackingAdmission stays Unknown.
Keep original F2/F3 negative controls. Validation remains Linux-only; no Android
build or usage-reset redemption was requested for this pause.

## 2026-10-20: process-owned execution-stop prerequisite

Continue the user's four-step M4 sequence in the
[process guest exclusion implementation record](M4_F3_PROCESS_GUEST_EXCLUSION.md).
The first component retains a real KProcess and binds its PageTable/backing
identity while closing all real backend entries and requesting actual halt.
The physical execution scope ends after backend/context unlock, before potentially
suspended SVC fibers; the successful omitted-SVC-writer control prevents confusing
backend acknowledgement with complete guest exclusion. NCE remains unsupported.
Process/API/service closure, GPU drainage/retained isolation and the actual
guest-backed Vulkan admission witness are still Open, with no ordinary activation.

Linux actual-body repeats and assertion mutants pass locally. A native Dynarmic
store-loop CTest is added and awaits remote compilation/execution. The predecessor
merge-recovery run 39391213895 at 5a6eee7b58 has passed early native translation
units and is still running; it does not accept this new execution-stop source.

## 2026-10-20: upstream #403 conflict resolution

Upstream e672babee8 reverts #376/#380/#401 for re-landing as individual subjects.
The merge follows the restored Dynarmic submodule and upstream unrelated code,
while retaining PR #375's backing, owner/admission, buffer-transfer and lifecycle
interfaces and the support they require. C++23 remains a current PR prerequisite;
upstream Common::Barrier and assertion helpers remain available. This is not a
wholesale restoration of #376 or its Dynarmic/shader flat-IR migration. The roadmap
below records #376 as a selective future reference at the user's request.

The pre-merge three-commit Linux run 39388275267 found one native API mismatch:
the real intrusive TLS tree erases an iterator, not a node reference. The cleanup
now obtains and checks that iterator; the injected tree no longer supplies the
unsupported overload. Focused Linux TLS/group/unmap checks pass after correction.
Full merged-source compilation and regression acceptance must be recorded
separately; conflict-marker removal alone is not build acceptance.

The previous Unmap-only run 39384043830 is independently artifact-verified at
74aea98af7: artifact 12248117179, SHA-256
1c12ac11f46234bd324165c035a3683d1c4f8f0c003822d19cccf909d62f225f;
146 source hash entries in 12 manifests, five CTest processes, three native
provenance repeats of 499 assertions, 11 publication mutants and two Unmap mutants.
Android was skipped and F2/F3 negative controls remain unchanged. Current source
has drifted and is not certified by that old run.

## 2026-10-20: explicit process teardown entry

The third independent change implements the previously written owner order.
Normal mapping admission remains closed; a private process-owner factory issues
an exclusive, domain/thread-bound entry for explicit cleanup unmaps only. TLS
cleanup carries it through UnmapPages. Failed TLS cleanup retains a descriptor in
a non-reusable finalizing tree and propagates failure. PageTable cleanup detaches
pending groups before the ordinary block walk. The entry is destroyed before
Memory finalization, preventing recursive retirement deadlock.

Linux extracted-body tests pass three repeats for owner sequencing, failed TLS
retention, foreign/moved/thread entry refusal, closed admission and lifetime join;
ignored-Result and unguarded-unmap mutants fail their intended assertions. Group
cleanup failure retains all references and its recovery record. Full native
acceptance is separate; no ordinary controlled-process enrollment is enabled.
See the [transaction design and limits](M4_F3_MAPPING_OWNERSHIP_TRANSACTIONS.md).

The old Unmap run 39384043830 has completed successfully at 74aea98af7. Its
artifact is being recovered independently and cannot accept the three new commits.

## 2026-10-20: MapGroup ownership transaction

See the [resource disposition table and teardown design](M4_F3_MAPPING_OWNERSHIP_TRANSACTIONS.md).
Failed group publication now has a PageTable-owned copied descriptor record and
retained operation references. A known refusal before any effects still closes
references. Prefix success or Unknown quarantines further Operate calls; cleanup
detaches the full range before closing references and normal block finalization.
Linux extracted-body repeats, descriptor-failure control, pending-cleanup control
and reference-release mutant pass. Controlled cleanup is the next separate commit;
ordinary process enrollment and full owner completeness remain disabled.

## 2026-10-20: Finalize bookkeeping repair

KMemoryBlockManager now commits record removal only after its cleanup callback
returns. The cross-call fixture checks first/second-block throws both before and
after effects, exact remaining records, descriptor counts and an explicitly
idempotent retry. Linux WSL passes three repeats and rejects restored premature
erase. Callback effect rollback is not inferred from retained bookkeeping.

## 2026-10-20: cross-call ownership audit

The [cross-call ownership audit](M4_F3_CROSS_CALL_OWNERSHIP_AUDIT.md) identifies
three remaining OPEN prerequisites: MapGroup unwinding closes references for a
published prefix; controlled KProcess retirement precedes its own TLS unmap;
and block-manager Finalize removes a record before a potentially throwing cleanup
callback. MapPageGroupImpl's Result rollback and KScopedPageTableUpdater do not
provide exception rollback. All actual processes remain untracked.

The actual-body audit fixture reproduces the group and block-manager failures
with injected collaborators and normal/refusal/untracked controls. Passing this
fixture records existing defects, not safe ownership or fixed acceptance. The
Linux WSL fixture passed three reproductions and two assertion-specific
counterfactual sensitivity controls. The
full-process/TLS path is source-audited only. No runtime change or new dispatch
is part of this audit; recover the pending Unmap Linux run below separately.

## 2026-10-20: process Unmap exception-ordering follow-up

The preceding Linux run `39380436500` has passed every configured step, including
the eleven new publication mutants, complete native/CTest processes and full
original Linux regressions. Android was skipped. Artifact `12246651887` has now
been independently verified: published ZIP SHA-256 matches, all 139 hash entries
in eleven manifests match exact source `3576ea32bd`, five CTest processes pass and
the three extra native provenance processes each pass 499 assertions. The eleven
new publication mutants and three PTE/VMA repeats pass; original F2/F3 negatives
remain unchanged. See the [publication acceptance record](M4_F3_OUTER_MAPPING_PUBLICATION.md#verified-linux-acceptance)
and ignored `.diagnostics/ci-39380436500/` for full evidence and scope. This closes
the preceding component only and does not accept the new Unmap source.

Source audit above the per-Memory boundary found unconditional reference cleanup
in KPageTableBase::Operate Unmap/UnmapPhysical: an inner controlled-entry refusal
can leave the mapping intact while SCOPE_EXIT calls physical Close. The original
body reproduces that assertion in an actual-body fixture with injected kernel,
mapping and refcount collaborators. The new implementation closes mapping
references only after UnmapRegion returns successfully. Non-heap compatibility
is preserved; no new MakePageGroup error classification is introduced.

The [Unmap reference lifetime record](M4_F3_PROCESS_UNMAP_REFERENCE_LIFETIME.md)
documents local three-repeat acceptance, both assertion controls and precise
limits. No native allocator/page reuse or process-wide closure is claimed.
Full Linux validation for the new fix remains pending. Ordinary guest mode,
producer/alias completeness, content authority and F2/F3 status are unchanged.

New Linux-only validation:
[39384043830](https://github.com/Splaser/emulator/actions/runs/39384043830), exact
source `74aea98af7a49f4098f6772328b0271c5d904f9f`. Recover its result before further
runtime changes or another dispatch. Android is not requested. Earlier run
`39380436500` cannot accept the new Unmap source.

## 2026-10-20: controlled outer CPU mapping publication component

The [outer mapping publication contract](M4_F3_OUTER_MAPPING_PUBLICATION.md)
adds constructor-only opt-in entry closure around the six actual Memory mapping,
permission and context-publication wrappers. Each counted scope precedes Impl/PTE
access and lasts through actual HostMemory effects and final tag publication.
Retirement joins entered scopes before Impl lifetime ends; partial-publication
exceptions seal the domain. Ordinary Memory/NCE instances remain untracked with
no new allocated domain, counter operations or mapping mutex. No KProcess adopts
the mode, and controlled retirement is terminal, including across Reset/move.

The new Linux fixture uses actual wrapper/Impl bodies and real HostMemory/PTEs,
with three repeats and eleven assertion mutants. It includes a real split PTE/VMA
pause and an omitted-second-owner counterexample. Native provenance additionally
checks actual constructor/default behavior, six public refusals and the real
Memory::Reset lifetime join; the paused entry is privileged test orchestration.
Local Windows fixture compilation succeeded; the Linux-specific runtime checks
and full Linux acceptance remain pending. Android is explicitly not requested.

Current Linux-only acceptance run:
[39380436500](https://github.com/Splaser/emulator/actions/runs/39380436500), exact
production/test source `3576ea32bd`. Android is skipped. Recover this run's first
actual result and artifact before another dispatch or further runtime changes;
the local compile-only probe is not runtime or mutation acceptance.

That pending run has now passed and its artifact/source evidence is verified in
the [component acceptance record](M4_F3_OUTER_MAPPING_PUBLICATION.md#verified-linux-acceptance).
No duplicate dispatch for `3576ea32bd` is needed. The Unmap follow-up above is
separately tracked at source `74aea98af7`.

This closes no all-owner table freeze or general guest exclusion gate. Raw PTE
writes, another Memory, services, frame/device/GPU owners and retained Unknown
isolation remain unclosed. No ordinary routing or physical content authority is
enabled; F2/F3 remain Open. Continue validation on the PR branch only.

## 2026-10-20: cross-platform compilation recovery

After syncing upstream `598b700884`, source `d67743a8c7` fixes a missing direct
`vulkan_device.h` dependency in `vk_scheduler.h`. The inline reconciliation
template calls `Device::GetGraphicsQueue()` on a non-dependent `Device` object;
a forward declaration is insufficient even when that template is not instantiated.
`present/window_adapt_pass.cpp` reaches this header before the Device definition.

Historical PR build logs at `857e8eadc9` confirm this exact compiler failure in
Android run `39274857043` (both variants), Linux run `39274852319` (three variants)
and the Clangtron job of Windows run `39274847896`. These are compile failures,
not F2/F3 runtime failures. The earlier Linux GCC-only acceptance did not certify
these Clang translation units. The fix supplies the definition directly without
changing the planner's queue check, admission or content semantics.

The integration workflow now compiles that presentation translation unit early
in both jobs. The Android configuration explicitly disables PCH, so the Clang
check cannot rely on a precompiled Device definition. Full Linux/Android run
[39352517047](https://github.com/Splaser/emulator/actions/runs/39352517047) targets
exact source `d67743a8c7`; acceptance is pending. Recover its actual results before
another dispatch or further runtime implementation. No local full build was run.

That historical run subsequently passed its complete Linux job and failed Android
at a separate shadowed lambda capture in `vk_scheduler.cpp`. Source `96d81e5aaf`
renames that capture; Android-only
[39357500270](https://github.com/Splaser/emulator/actions/runs/39357500270) passed
NCE/core, HostMemory, Vulkan and shader library compilation. This is not APK
packaging or device/runtime acceptance. The Linux-only iteration above builds on
those recovered results; no duplicate Android dispatch is requested.

Other historical failures need separate disposition: the native Windows clang-cl
job in `39274847896` rejects an unused `/std:c++latest` alongside
`/clang:-std=c++23`; macOS run `39274861951` fails during Qt setup because its
selected Python has no `aqt` module. This header repair does not certify either
of those jobs. Raw failed-job logs are retained under ignored
`.diagnostics/pr375-build-repair/`.

The next ordinary-guest gate remains outer PTE/process/host-service publication
closure and lifetime retention, then device/GPU owner completeness, as described
in `M4_F3_CPU_PREPARATION_LIFETIME.md`. No ordinary routing, production guest
admission or physical latest-writer authority is enabled; F2/F3 remain Open.

Initial checkpoint date: 2026-10-16, at the user's requested 5-hour usage
threshold. Evening continuation established a stable native F3 reproduction.
This is a continuation checkpoint, not M4 acceptance or a production F3 fix.

## Status and constraints

- Submission-provenance source `102b0486f7` (initial implementation `64c59e9208`)
  introduces only an explicit isolated
  staged fill linked to actual native owner/footprint, local intent and checked
  submission. Pending, Submitted, Skipped, Complete and Unknown are distinct;
  historical completion does not certify a still-current physical representation.
  See the [provenance boundary and tests](M4_F3_SUBMISSION_PROVENANCE.md). Ordinary
  shaders/alias consumers are not switched to this API and no latest-writer
  authority is published. Verification results are recorded in that document.
  Full [Linux/Android run 38984914456](https://github.com/Splaser/emulator/actions/runs/38984914456)
  passed at `102b0486f7`: five CTest processes, 53 provenance assertions in CTest
  and each of three extra processes, unchanged original F2/F3 negative controls,
  all configured integration/lifecycle/fault gates and actual Android arm64
  core/video/shader compilation. The failed-pipeline Draw guard remains an
  actual-body fixture, not native shader/binding execution. Evidence is retained
  under `.diagnostics/ci-38984914456/`. No physical content authority, general
  draw/dispatch provenance or ordinary F3 repair is certified.

- Write-intent follow-up `d8764c0cf0` records resource-local GPU write revisions at
  ordinary write marking, staged DMA copy/clear and scatter destinations. The
  isolated adapter rejects pre-submit source/target rewrites even when dirty
  geometry is unchanged; post-submit rewrites retain Unknown and quarantine.
  This starts producer metadata but is not submission completion, historical
  physical provenance or latest-writer subrange authority. See the
  [revision record](M4_F3_CONTROLLED_ALIAS_ADAPTER.md#resource-write-intent-revisions).
  Full Linux/Android run [38907387312](https://github.com/Splaser/emulator/actions/runs/38907387312)
  passed at that source: 88 native adapter assertions in CTest and each of three
  extra processes, all configured integration/fault/lifecycle gates and actual
  Android arm64 core/video/shader compilation. Original F3/F2 negative controls
  remain unchanged. Baseline run `38906918308` separately confirms the repeated
  source write was accepted by the old adapter. Both runs' evidence is retained
  under `.diagnostics/ci-<run>/`; no Android/device execution is claimed.

- M3B-F2 remains **Open**. Restricted eligibility remains **blocked**; see
  [eligibility blockers](M3B_F2_RESTRICTED_ELIGIBILITY.md) and
  [producer inventory](M3B_F2_PRODUCER_INVENTORY.md).
- Ordinary `Memory::Write` routing and selective UMA zero-copy remain inactive.
- No new coordinator or broad BufferCache rewrite has been introduced.
- The native F3 witness and test observation helpers have compiled remotely. The
  standalone `[f3]` processes reproduce stale full/partial aliases with whole-byte
  controls, remap rejection and physically equivalent representation qualification.
  Final remote validation is tracked below. **F3 is reproduced, not fixed.**
- No local Android build was attempted; validation continues through remote workflow.

## Initial verified baseline

The checkpoint starts from `3462bbadcf`, previously pushed to both `main` and
`codex/issue-372-fixes` (PR 375 source branch).

- Production source at `3ebb655b97`: Android job passed in run `38870993595`.
- Linux workflow `38873324462` at `46687f1bfa`: passed, including 1708 native
  assertions, 130 production unit cases, integration/mutation checks, renderer
  and FenceManager fixtures. These are baseline results, not validation of this WIP.
- Original F2 witness was reproduced three times: expected CPU value 77,
  observed cache value 10, backing 77. Its `--require-fixed` invocation failed as
  expected. This defect remains open and must remain a regression witness.

## Source audit and current hypothesis

| Area | Existing source and finding |
| --- | --- |
| Physical backing lifetime | `src/common/published_guest_chunk.h`: backing epoch owns generation/alive state, range admission and page gates; lifetime invalidation closes authority even while storage remains pinned. |
| Immutable physical plans | `src/video_core/physical_access_plan.h`: snapshots device mappings, pins physical storage outside mapping locks, then revalidates. `MatchesCurrentDevice` checks captured epoch/generation and physical pages. A storage pin is not content authority. |
| DAddr aliases | `src/core/device_memory_manager.inc`: odd/even mapping generation, reverse alias collection, bounded alias enumeration and inverse-translation verification. Staged-mode Map itself does not perform the UMA unmap path. Higher-level mapping callers still require audit. |
| Staged ownership | `src/video_core/buffer_cache/buffer_cache.h`: ordinary lookup, overlap joining, upload tracking and GPU dirty publication are keyed by DAddr. Physically overlapping but disjoint DAddr windows can own independent staged buffers. |
| Native resource pin | `src/video_core/renderer_vulkan/vk_buffer_payload.h`: pins retain allocation and byte bounds. They do not prove cache membership, current physical association, authoritative content, admission or completion. |
| Representation qualification | `PinExistingDmaRepresentation` validates physical geometry and takes/revalidates a fresh mapping snapshot around pin acquisition. It does not simply reject every historical plan generation change; existing physical-equivalence behavior must be preserved. |

Observed F3 sequence: warm two independent staged buffers A/B from physical
backing 10, then complete a GPU write of 99 through A. Ordinary synchronization
through B can still consult B's DAddr-local tracking and serve its old staged
value 10. Completing physical range admission establishes ordering but does not
itself transfer authoritative content between staged aliases. This explanation
matches the standalone native result. See
[source cause and bounded authority plan](M4_F3_ALIAS_AUTHORITY_PLAN.md).

## Native witness and process-isolation correction

`src/tests/core/controlled_cpu_scheduler.cpp` adds the `[f3]` case to the existing
`controlled_cpu_scheduler_tests` target. `src/core/scheduler_test_access.h` adds
test helpers for ordinary buffer acquisition, Vulkan fill/readback, retained
staged pins and cache retirement.

| Case | Required observation |
| --- | --- |
| Full-page alias | A/B map identical physical pages; GPU writes A=99; same A reads 99; B currently expected to reproduce stale 10. |
| Partial alias | C starts one physical page into A; a 256-byte GPU fill within that page reads 99 through A, currently expected stale 10 through C. Adjacent untouched bytes stay 10. |
| Disjoint control | Separate physical backing D remains 10. |
| Admission control | Existing physical range admission parks the B reader behind A's writer and grants it after completion; no new coordinator is used. |
| Retirement/remap | Retire B before remapping it to backing 55; a retained old pin remains an old allocation, the old physical plan and representation query reject the new mapping, and a fresh representation has a new identity and reads 55. A's unchanged physical association still qualifies despite the global mapping-generation change. |

The driver uses actual System/Kernel/process objects, device mappings, ordinary
BufferCache code and native Vulkan transfer execution through the existing
software-device test renderer. The sequence and manual admission enrollment are
test drivers. This is not a game image, an ARM scalar store, a shader draw, a
GPUVAddr translation witness, or proof that ordinary producers participate in
production admission. Existing extracted cache/pin and renderer fixtures remain
fixtures; do not report their results as this native F3 reproduction.

Commands on the remote Linux build:

```sh
build-integration/bin/controlled_cpu_scheduler_tests '[f3]'
CITRON_REQUIRE_F3_FIXED=1 build-integration/bin/controlled_cpu_scheduler_tests '[f3]'
```

The F3 case is hidden from an unfiltered Catch2 invocation and explicitly selected
by its own CTest process. KernelCore's host dummy KThread is thread-local and
retains the first Kernel; creating two complete System instances sequentially
in the same host test thread reused stale kernel state. Run `ctest`, the original
unfiltered scheduler executable, or the explicit `[f3]` process; do not select both
System lifetimes in one executable invocation.

Default mode intentionally requires stale alias values 10 and prints
`F3 REPRODUCED (not fixed)` only after positive controls pass. The second command
requires coherent values 99 and should fail until F3 is fixed. The repeat driver
`python tools/physical_coherency_tests/run_native_f3_tests.py --repeat 3` retains
each normal log and explicitly rejects crashes, timeouts, failed positive controls
and unexpected negative-mode failures. Catch assertions outside the GPU callback
receive captured setup/observation exceptions rather than terminating the GPU
worker on an uncaught test exception.

## Resume roadmap

### Follow-up design reference: PR #376

Per the user's direction, [PR #376](https://github.com/citron-neo/emulator/pull/376)
is a reference for overlapping ideas, not a request to restore its entire patch.
Upstream [PR #403](https://github.com/citron-neo/emulator/pull/403) reverted that
series for re-landing as separate subjects. Track the individual replacements
before implementing overlapping work, and compare them against PR #375's current
ownership and validation contracts.

- Reuse the idea of stopping only the channel at a semaphore acquire and retaining
  its continuation; preserve PR #375's explicit producer/admission boundaries.
- Compare UMA upload decisions and driver-exported backing capability checks with
  the current lazy-backing/alias ownership plan. Performance observations do not
  establish physical content authority or close F2/F3.
- Evaluate in-tree Dynarmic, index-linked IR and C++23/std-library migration as
  separate future refactors, with their own dependency/build and semantic tests.
  Keep upstream's restored Dynarmic submodule during this conflict resolution;
  do not repeat the flat-IR migration as part of ownership fixes.
- Deduplicate the Common::Barrier build repair, which both branches addressed and
  upstream retained. Preserve focused tests and compare actual re-landed commits
  before taking any other implementation from the series.

### Active ownership work

1. Extend the [bounded authority implementation plan](M4_F3_ALIAS_AUTHORITY_PLAN.md)
   with persistent write-site physical/provenance metadata and latest-writer
   subrange ownership. Reuse the now verified
   [isolated alias adapter](M4_F3_CONTROLLED_ALIAS_ADAPTER.md) for qualified exact
   copies. Its per-operation snapshots are not a general write-site version issuer.
   Preserve DAddr lookup and GPU dirty complements; do not add a coordinator.
2. Integrate overlapping alternating writers (currently refused), merge/GC
   authority transfer and frame-reuse exclusion. The adapter already covers
   disjoint reverse writes, source/target retirement and submitted remap Unknown;
   native adapter success is not ordinary-path or allocator-lifetime acceptance.
3. Preserve the original ordinary F3 negative witness and F2 defect. A manually
   driven copy adapter cannot count as an ordinary-path coherency fix. Only after
   the implementation exists may fixed-mode passing controls replace this gate.
4. Run the complete `pr375-pr380-integration.yml` gates after source changes.
   Preserve M3B native/integration/mutation regressions; Android production source
   changes require the remote full scope, without a local Android build.

## Continuation evidence: 2026-10-16 evening

- Initial run [38891421101](https://github.com/Splaser/emulator/actions/runs/38891421101)
  compiled production and native targets, passed the 130-case ordinary test suite,
  and printed full/partial stale 10, same-DAddr 99 and remap 55. Its combined native
  executable timed out; this was not clean witness acceptance.
- Diagnostic run [38896969297](https://github.com/Splaser/emulator/actions/runs/38896969297)
  confirmed standalone F3 normal exit under GDB (28 assertions). Both native
  cases in one process failed in whichever complete System lifetime ran second.
  Test process isolation is implemented at `0fd0f17c9e`.
- Run [38897769357](https://github.com/Splaser/emulator/actions/runs/38897769357)
  at `f6ab101715` passed all three CTest processes (130 ordinary cases, 1708
  scheduler assertions, 36 F3 assertions) and three additional standalone F3
  repetitions. Fixed mode produced precisely the two expected alias failures;
  the wrapper incorrectly assumed the older Catch2 exit convention. `62259c5dbd`
  recognizes Catch2 3.16's test-failure status 42 and records process exit codes.
- Final validation of `48d95a8ba3`, with two additional native mapping qualification
  controls, passed in run
  [38898475861](https://github.com/Splaser/emulator/actions/runs/38898475861).
  All three CTest processes passed: 130 ordinary cases, 1708 scheduler assertions,
  and 38 F3 assertions. Three additional F3 processes each passed 38 assertions
  and exited 0. Fixed mode exited 42 with exactly the two expected alias failures
  (36 passing/2 failing assertions), after normal guest teardown. F2 reproduced
  three times and its negative mode exited 1 as required. Real Linux HostMemory/
  import lifetime and every configured coherency/lifecycle/mutation regression
  passed. Android was intentionally skipped because production code is unchanged.
  Downloaded source stamp, CTest logs, each F3 process log, F2 negative log and
  complete job output are retained in `.diagnostics/ci-38898475861/`.
- Local Windows extracted F2 witness ran three repetitions: backing 77, cache 10;
  invoking its compiled `f2 require-fixed` returned 1 as expected. This is fixture
  regression evidence, not native Vulkan or Android acceptance.

The witness-only continuation through `425de3055b` did not change production code.

## Subsequent continuation: isolated staged alias copy verified

Production source `642a71fb07` implements the explicit admitted alias-copy adapter;
full run [38903097142](https://github.com/Splaser/emulator/actions/runs/38903097142)
passed Linux and Android compilation. Linux passed the original 130 ordinary cases,
1708 scheduler assertions, 38 F3 assertions and 67 new native adapter assertions,
plus three extra adapter repetitions. Test-only weak-payload-owner checks at
`a415dea618` passed full Linux run
[38903908981](https://github.com/Splaser/emulator/actions/runs/38903908981), including
the new exact-copy/failure/retention fixture and every existing integration/mutation
regression. Production source/workflow differences between those heads are empty.

See [adapter contract and evidence](M4_F3_CONTROLLED_ALIAS_ADAPTER.md) for exact
scope, commands/cases, retained resources and Unknown behavior. The original ordinary
F3/F2 negatives still reproduce; automatic ordinary routing, latest-writer authority
and general producer eligibility remain blocked. Next development starts at the
resume roadmap above, not by redoing the verified adapter.

## Subsequent continuation: ordinary storage submission observation

Source `be3847045e` adds an opt-in ordinary Draw/indirect Draw/Dispatch storage
observer. It captures the actual descriptor allocation/range and write intent,
then joins the existing batch's checked submission without a per-draw flush.
Default rendering remains unobserved; unsupported capture is incomplete and does
not suppress/replay the command. Shader footprints are conservative access ranges,
not proof of per-byte stores or physical content versions.

See the [observer contract and current validation record](M4_F3_STORAGE_WRITE_OBSERVER.md)
and [initial checked provenance slice](M4_F3_SUBMISSION_PROVENANCE.md). Native
provenance now has 70 assertions, including two real fills sharing an ordinary
submission and a subsequent batch with a different identity. The Linux job of
[38989337199](https://github.com/Splaser/emulator/actions/runs/38989337199) passed
all five CTest processes, repeated native controls, original F2/F3 negatives and
all configured integration/mutation regressions. Full platform status is recorded
in the observer document.

Next gate: retain immutable GuestBackingEpoch/physical geometry and prove actual
representation association plus synchronization relationships before any physical
latest-writer publisher. Do not infer authority from Complete, maximum revision or
submission identity. Actual shader/game execution, merge/GC and mapping/frame reuse
remain separate acceptance gates. F2/F3 remain Open, restricted eligibility remains
blocked, and ordinary Memory::Write/UMA routing remains inactive.

## Subsequent continuation: immutable physical identity before authority

The [immutable physical provenance contract](M4_F3_IMMUTABLE_PHYSICAL_PROVENANCE.md)
supersedes the earlier observer's lack of physical identity. Allocation origins
are captured once from real device PTE snapshots; exact binding slices retain
GuestBackingEpoch/physical geometry and native identity. Existing, merged,
ambiguous or mapping-generation-changed resources remain incomplete. Completion
never re-resolves a current DAddr or chooses a writer by revision/submission ID.

Shared coverage is Open until the GPU owner explicitly closes the observation
scope. Any capture failure/overflow invalidates every retained event in the scope;
sealing cannot repair it. CompletedPhysicalIdentity requires checked Complete,
closed complete coverage and live identity epochs. It exposes historical identity
only, with no backing pointer/storage ownership or content authority.

Production source is `84d004b270`; the native test setup follow-up `52560e5ec9`
changes only pre-enqueue retry behavior in the existing scheduler fixture. Native
physical provenance now has 105 assertions. Validation/failures and exact source
relationships are recorded in the contract; use those results rather than inferring
acceptance from an intermediate run's partial success.

Next stage remains real synchronization relationships: same-queue execution/memory
dependencies, cross-queue semaphore relationships, transfer/shader dependencies and
partial intervals. Do not start latest-writer publication from these identity
records. Real storage pins/admission, complete producer enrollment, mapping/frame
reuse and merge/GC ownership are still required before content authority. F2/F3
remain Open; restricted eligibility and ordinary Memory::Write/UMA stay blocked.

### M4 F3 synchronization provenance extension

See [the synchronization contract](M4_F3_SYNCHRONIZATION_PROVENANCE.md). Direct
same-submission graphics-queue WFI memory-barrier dependencies are now recorded
alongside actual Vulkan emission and gated by checked completion, closed coverage,
stage/access scopes and complete same-native-byte physical projection. Independent
replicas, different submissions and cross-queue relations remain Unknown. No data
copy or latest-writer publication is implemented. Source `a5dba62536` passed full
[workflow 39004334780](https://github.com/Splaser/emulator/actions/runs/39004334780):
all five Linux CTest processes, native provenance 119 assertions plus three repeat
processes, focused actual-body controls and five expected mutation failures, full
coherency/lifecycle regressions and Android arm64 production library compilation.
F3 require-fixed still fails exactly two aliases (exit 42); F2 remains backing
77/cache 10. Detailed boundaries and retained artifacts are in the contract.

### M4 F3 exact transfer content extension

See [the transfer content contract](M4_F3_TRANSFER_CONTENT_PROVENANCE.md). Explicit
bounded Scheduler Fill/Copy requests now retain exact command coverage, immutable
copy ancestry and both untouched complements inside one checked callback. Initial
bytes are Unknown; previous submissions and shader bindings cannot seed roots.
The mapping gate refuses remap/dead origins. No ordinary caller routing, automatic
replica synchronization or physical latest-writer publication is added.

Final source `4a3f9a6aba` passed complete
[workflow 39012415113](https://github.com/Splaser/emulator/actions/runs/39012415113):
all five Linux CTest processes, native provenance 149 assertions plus three repeat
processes, 23 actual-body content cases, nine content/five sync mutation failures,
512 byte-oracle prefixes, full coherency/lifecycle regressions and actual Android
arm64 production library compilation. No APK/device/game acceptance is claimed.

The pre-submit negative reproduced early pin retirement after command recording
but before its queued Submit callback. The final source guards queue attempt/result
as well as host callback completion; unattempted host failures remain quarantined.
The initial `f682e086e9` run was cancelled/superseded, not accepted. Artifacts and
source-hash correspondence are recorded in the transfer contract. Original F3
require-fixed still fails exactly two aliases (exit 42); F2 remains backing 77/cache
10. Cross-submission/queue roots stay Unknown; no latest-writer authority is added.

### Current-content cancellation follow-up (2026-10-17)

See [M4_F3_CURRENT_CONTENT_GATE.md](M4_F3_CURRENT_CONTENT_GATE.md). Historical
ancestry remains immutable. A separate current query requires checked completion,
strict physical mapping, live cache membership and no subsequent unclassified
Scheduler recording. Ordinary Fill/Copy, observer-off shader recording and upload
commands cancel before device execution; reads/disjoint commands conservatively
cancel too. Unregister cancels staged payload membership without releasing pins.
Worker completion cannot restore eligibility. Cross-submission/queue ancestry
remains Unknown and no authority or replica synchronization is published.

The actual-body red control is retained in `.diagnostics/m4-current-gate-red.log`.
Final source `95ab388bd9` passed complete
[workflow 39015687982](https://github.com/Splaser/emulator/actions/runs/39015687982):
all five Linux CTest processes, native provenance 159 assertions plus three repeat
processes, 31 actual-body content cases, 17 expected mutation failures, full
coherency/lifecycle regressions, and actual Android arm64 production library
compilation. Eleven extraction input hashes match committed Git blobs. Logs and
artifacts are retained in `.diagnostics/ci-39015687982/`.

The first run at `e7ee346913` exposed a native `-Werror=shadow` diagnostic and was
superseded after the local variable rename, without changing warning policy.
No APK/device/game acceptance is claimed. Next work must preserve this separate
cancellation contract: returned graph pointers are historical metadata across
subsequent work, read/disjoint commands conservatively cancel, and no current
query constitutes physical authority or a concurrent data-admission lease.

### Staged byte-range eligibility follow-up (2026-10-17)

See [M4_F3_RANGE_CURRENT_CONTENT_GATE.md](M4_F3_RANGE_CURRENT_CONTENT_GATE.md).
The new range query checks a pin-relative nonempty byte interval. Typed actual
Fill/Copy remove only overlapping destination ranges on the same live staged
allocation; source ranges, untouched complements and independent allocations
remain eligible. Ordinary runtime clear/copy/upload/readback use the typed
emitters and preserve their prior barriers. Unknown/general recording still
rejects the whole snapshot. Native offsets, bounded fragmentation, device-loss
failure domains, mapping and retirement must all fail closed.

Historical roots and origins remain immutable; preserved current ranges do not
import any cross-submission content source. No latest-writer authority, replica
reconciliation, or ordinary Memory::Write/UMA activation is added. The three
development stages are recorded explicitly: close proven content provenance,
then real replica reconciliation with complement preservation, then the original
ordinary F3 witness subject to M3B producer-exclusion eligibility. Controlled M4
acceptance and production F3 acceptance remain distinct.

The old whole-snapshot behavior failed the new untouched-range control as expected
(`.diagnostics/m4-range-gate-red.log`). The typed-barrier control additionally
exposed copy pre/post and WFI barriers cancelling complements; their typed enqueue
now preserves content eligibility while retaining the actual barriers and scopes.
The source-only current-root query also failed a stale-interval control
(`.diagnostics/m4-current-root-red.log`); `CurrentTransferHasRoot` now combines
current byte eligibility and the exact historical root/projection without
promoting other bytes in a retained graph pointer.

Final source `3bf13ca261` passed
[Linux-only workflow 39019720456](https://github.com/Splaser/emulator/actions/runs/39019720456):
all five CTest processes, native provenance 177 assertions plus three repeat
processes, 46 actual-body transfer-content cases, 27 expected mutation failures,
512 content/2,048 eligibility oracle prefixes and full coherency/lifecycle tests.
All 12 extraction input hashes match committed blobs; artifacts are retained in
`.diagnostics/ci-39019720456/`. Original F3 still rejects two aliases (exit 42),
and F2 remains backing 77/cache 10. No authority or reconciliation is published.

Validation policy: default to Linux compilation, unit tests and relevant native
regressions. Run Android builds only for Android-specific changes or risks Linux
cannot cover; manual workflow dispatch now defaults to `linux-only`, with `full`
available when needed. Android was intentionally skipped for `3bf13ca261`; do not
claim an unrun Android or device/game acceptance. The previous `cc055d3426` full
Linux job passed independently (170 native assertions). Its already running
Android job did not block this follow-up. This documentation-only handoff does
not repeat the same source validation.

### Controlled same-queue submission handoff (2026-10-17)

See [M4_F3_SAME_QUEUE_REPLICA_COPY.md](M4_F3_SAME_QUEUE_REPLICA_COPY.md).
This follow-up copies one exact current interval between different private staged
allocations over the same saved physical alias, preserving explicit parent roots
for copied bytes and untouched target complements. Actual accepted graphics-queue
predecessor identity, entry/release barriers and checked completion are required;
ticks/revisions do not rank producers. Writable host pointer exposure disqualifies
the payload. Missing synchronization, stale source/target intervals, failures,
remap, unknown recording and retirement fail closed. Source writes after a
completed copy do not rewrite its historical ancestry.

Scope remains one hop from one controlled parent batch, with no intervening
unchecked submission. Cross-queue and unsupported chains remain Unknown. There
is no physical latest-writer publication, ordinary game selection or F3/F2 closure.
Use Linux-only CI and relevant unit/native tests; Android remains on demand.

Source `d783250b0d` passed
[Linux-only workflow 39022879191](https://github.com/Splaser/emulator/actions/runs/39022879191):
all five CTest processes, native Vulkan provenance 211 assertions plus three
independent repeats, 49 new replica modes alongside the existing 46 transfer
modes, 42 expected assertion-failure mutations, byte oracles and the full
registration/coherency/lifecycle regression suite. Seventeen extracted
input hashes match committed blobs; all 43 fixture logs are retained under
`.diagnostics/ci-39022879191/`. Original F3 fixed-required mode still fails two
alias checks (exit 42); F2 remains backing 77/cache 10. Android was intentionally
skipped. This evidence update changes documentation only and does not rerun CI.

### Bounded multi-hop same-queue handoff (2026-10-17)

The follow-up in [M4_F3_SAME_QUEUE_REPLICA_COPY.md](M4_F3_SAME_QUEUE_REPLICA_COPY.md)
extends explicit receipt ancestry to at most four copies over fresh target slots
from one root anchor. Admission combines current source bytes, immutable root
projection, exact saved binding and known whole-target complements. Completed
qualification verifies every actual submission/queue/barrier edge, not just the
last copy. Ancestor storage/remap/host-exposure cancellation fails current
qualification; later intermediate byte writes do not rewrite already captured
downstream content. Cross-queue remains Unknown;
ordinary automatic selection, latest-writer publication, F3 and F2 closure remain
outside this slice. Android remains on demand.

Source `715d1097b9` passed
[Linux-only workflow 39025579635](https://github.com/Splaser/emulator/actions/runs/39025579635):
all five CTest processes, native provenance 233 assertions plus three independent
repeats, 44 multi-hop modes, 46 expected assertion-failure mutations, 256 new
multi-hop oracle prefixes and full existing content/registration/coherency/backing/
lifecycle regressions. All 17 extracted input hashes match committed blobs;
47 fixture logs and downloaded artifacts are retained in
`.diagnostics/ci-39025579635/`. Original F3 fixed-required mode still fails two
aliases (exit 42); F2 remains backing 77/cache 10. Android was intentionally
skipped. This documentation-only evidence update does not repeat source CI.

### Mixed-source copy intervals (2026-10-17)

The follow-up in [M4_F3_SAME_QUEUE_REPLICA_COPY.md](M4_F3_SAME_QUEUE_REPLICA_COPY.md)
accepts a complete explicit source partition with at most 64 root spans. Every
piece must prove its historical root/projection and current byte qualification;
holes, overlaps, Unknown and a merely valid prefix cannot qualify a copy. The
receipt owns immutable declarations and preserves anchor root IDs through up to
four hops. A target still needs a known whole-pin anchor root for its complements.
Actual same-queue submission/barrier/completion and storage/remap gates are
unchanged. The new native case copies B's 10/99/10 source into C while preserving
C's outer 20 intervals; unit controls cover partial cancellation, failure, remap,
retirement and mixed four-hop ancestry. Cross-queue
Unknown, ordinary selection disabled, no latest-writer authority; F3/F2 stay Open.
Android remains on demand.

Source `c3cb37cfb1` passed
[Linux-only workflow 39093574539](https://github.com/Splaser/emulator/actions/runs/39093574539)
on 2026-10-18: all five CTest processes, native Vulkan provenance 241 assertions
plus three independent repeats, 46 new mixed-source modes, 51 expected assertion
failures, 256 new mixed-source byte-oracle prefixes and the full existing
content/registration/coherency/backing/lifecycle regression suite. All 18 extracted
input hashes match committed blobs; all 52 fixture logs and downloaded artifacts
are retained in `.diagnostics/ci-39093574539/`. F3 still fails two fixed-required
aliases (exit 42); F2 remains backing 77/cache 10. Android was intentionally
skipped. This documentation-only evidence update does not rerun source CI.

### Mixed-target partial-copy follow-up (2026-10-18)

See [M4_F3_MIXED_TARGET_COPY.md](M4_F3_MIXED_TARGET_COPY.md). New receipts compose
the exact source interval with an explicit complete untouched-target partition.
Current root checks, saved native/pin/physical binding and mapping qualification
apply to each retained piece; only covered bytes are replaced. Old ancestry stays
immutable, and later writes revoke only overlap in current target qualification.
Both dependency histories must be on one verified bounded submission spine, with
actual queue adjacency, barriers and checked completion; four hops remain the
limit. No cross-queue proof, ordinary automatic selection or latest-writer system
is added. The new native test compares mixed-target C and fourth-hop E readback
with an independent byte oracle, plus later writes and async target retirement.
Source `0777f7f327` passed
[Linux-only workflow 39095898124](https://github.com/Splaser/emulator/actions/runs/39095898124):
all five CTest processes, native Vulkan provenance 280 assertions plus three
independent repeats, 51 new deterministic mixed-target modes, all 59 expected
assertion mutations and 256 new mixed-target byte-oracle prefixes. All 19
extracted input hashes match committed blobs; all 60 fixture logs, native
readback/repeat evidence and the full registration/coherency/backing/lifecycle
regressions are retained in `.diagnostics/ci-39095898124/`. Original ordinary F3
still fails two fixed-required aliases (exit 42); F2 remains backing 77/cache 10.
Android was intentionally skipped. F2/F3 remain Open. This evidence-only
documentation update does not repeat source CI.

### Simultaneously mixed source/target acceptance (2026-10-18)

The existing native mixed-target case now uses mixed B and mixed C together on
its fourth actual copy. An anchor disjoint copy makes root offsets nonidentical
to allocation offsets. The independent byte oracle checks all C roots,
projections, untouched complements and actual readback; later source/target
writes check immutable history versus current overlap qualification. A fifth
hop refuses. No production implementation or queue/admission rules change.
Forty new deterministic simultaneous-mixed modes cover exact partitions,
stale/Unknown pieces, failure/pending submission, barriers/queue linkage,
remap and retirement; two added mutations target projection and target ancestry.
See [M4_F3_MIXED_TARGET_COPY.md](M4_F3_MIXED_TARGET_COPY.md) for the sequence and
evidence boundaries. Source `9bf69055a7` passed
[Linux-only workflow 39101068176](https://github.com/Splaser/emulator/actions/runs/39101068176):
all five CTest processes, native Vulkan provenance 285 assertions plus three
independent repeats, all 40 new deterministic simultaneous-mixed modes and all
61 expected assertion mutations. All 20 extracted input hashes match committed
blobs, with all 62 fixture logs, actual readback/repeat evidence, complete job
output and registration/coherency/backing/lifecycle regressions retained in
`.diagnostics/ci-39101068176/`. The native asynchronous retirement case now also
uses both mixed receipts. Ordinary F3 still fails two fixed-required aliases
(exit 42); F2 remains backing 77/cache 10. Android was intentionally skipped;
cross-queue Unknown, no ordinary automatic replica selection or latest-writer
authority, F2/F3 Open. This documentation-only evidence update does not rerun
unchanged source CI.

### Explicit-source multi-region controlled Copy (2026-10-18)

See [M4_F3_MULTI_REGION_COPY.md](M4_F3_MULTI_REGION_COPY.md). A bounded ordered
region list now imports explicitly qualified roots from several staged sources
into one fully covered target window, preserving known untouched complements.
It reuses existing receipts, actual same-queue submission spine, checked
completion, pins and Current Content Gate. All possible planned writes cancel
old target qualification before recording; partial recording does not roll back
GPU writes, and unknown submission outcomes cannot publish completion. Four-hop,
16-receipt, eight-resource and partition budgets remain. New deterministic
faults allow prefix execution and writes despite a failed queue result.
The native test composes batch/single/batch/batch with byte oracle and readback.
Implementation `bb6b845784` plus native test-name fix `4bb3231040` passed
[Linux-only workflow 39104473255](https://github.com/Splaser/emulator/actions/runs/39104473255):
all five CTest processes, native Vulkan provenance 315 assertions plus three
independent repeats, all 35 new deterministic modes plus three fixture repeats,
all 67 expected assertion mutations, budget boundaries and the full existing
registration/coherency/backing/lifecycle regressions. All 21 extracted input
hashes match committed blobs; all 68 fixture logs, exact source, readback/repeat
evidence and complete Linux output are retained in `.diagnostics/ci-39104473255/`.
The initial native variable-name conflict and failed run are preserved in
`.diagnostics/ci-39103686983/`; only test locals changed for the successful rerun.
Early real Scheduler/BufferCache compilation was added to the Linux workflow.
F3 still fails two fixed-required aliases (exit 42); F2 remains backing 77/cache
10. F2/F3 remain Open, cross-queue Unknown, no ordinary automatic selection or
physical authority. Android was intentionally skipped. This documentation-only
evidence update does not rerun unchanged source CI.


### Non-contiguous explicit-source controlled Copy (2026-10-18)

Source `d223b5ca33` extends the existing bounded batch provenance partition to
allow internal holes as independently qualified untouched complement. It changes
only the metadata merge, reusing per-command planned cancellation, actual checked
submission spine, barriers, immutable roots, mapping/native identity and retained
pins. Partial recording leaves all possible write intervals unqualified, including
unrecorded suffixes; old hole evidence can survive independently. A separately
flushed hole Fill cancels its overlap alone. Submit-domain uncertainty remains
conservatively global Unknown; there is no rollback or fabricated completion.

[Linux-only run 39111978882](https://github.com/Splaser/emulator/actions/runs/39111978882)
passed real Scheduler/BufferCache compilation, CLI/shader/tests, all five CTest
processes and the full registration/coherency/backing/lifecycle regressions.
Native Vulkan provenance now has 351 assertions plus three repeats; both
contiguous and disjoint four-hop chains compare complete readback and all root
projections with independent oracles, reject a fifth hop and retain local hole
cancellation/retirement rules. The extracted fixture has 35 contiguous and 41
non-contiguous modes plus three repeats; all 70 assertion mutations and standalone
512 new disjoint oracle prefixes pass their expected checks. Exact source and all
21 extracted input hashes match committed blobs. Complete Linux/artifact output,
all 71 fixture transcripts, local red/green/repeat evidence and a verified index
are retained in `.diagnostics/ci-39111978882/`.

See [M4_F3_DISJOINT_COPY.md](M4_F3_DISJOINT_COPY.md) for the contract, changed files,
fault-model boundaries and complete acceptance. Android was skipped. F3 fixed
required still has two expected 99/observed 10 failures (exit 42); F2 remains
backing 77/cache 10. F2/F3 stay Open, restricted eligibility blocked, cross queue
Unknown; no ordinary automatic replica selection or latest-writer authority.
This evidence-only follow-up does not rerun unchanged source CI.


### Mixed-root internal holes: Copy combination stage ceiling (2026-10-18)

Source `8089ed6553` adds acceptance only; production BuildBatch/HasRoot, current
eligibility, checked submission and receipt mechanisms are unchanged from
`d223b5ca33`. Three writes [8,16), [32,40), [56,64) leave two holes, each retaining
two different historical roots and nonidentity projections. Both roots in both
holes are independently qualified; every possible write byte is revoked before
worker execution. Simulated one/two-command recording prefixes can execute on
successful submission without qualifying the new Unknown receipt. A separate
hole Fill revokes its intersection alone; submission-domain uncertainty refuses
all current qualification, including holes, without rolling back possible writes.

[Linux-only run 39115398414](https://github.com/Splaser/emulator/actions/runs/39115398414)
passed all five CTest processes, actual Scheduler/BufferCache compilation,
CLI/shader/test builds and the full registration/coherency/backing/lifecycle
regressions. Native Vulkan has 392 assertions plus three repeats with all three
layouts and complete four-hop readback/root/current oracle checks. New extracted
fixtures have 31 modes / 34 trials plus three repeats; all 74 CI assertion mutants
are detected. Standalone mixed-hole metadata accepts exactly 64 complement pieces
and refuses 65. A separate local-only capacity relaxation mutant is detected by
the new unit assertion; do not count it as a CI or GPU result. Exact source and
all 22 extracted input hashes match committed blobs. All 75 fixture transcripts,
complete artifacts/Linux output, local red/repeat witnesses and a verified index
are retained in `.diagnostics/ci-39115398414/`.

See [M4_F3_MIXED_HOLES_ACCEPTANCE.md](M4_F3_MIXED_HOLES_ACCEPTANCE.md) for changed
files and evidence boundaries. **Controlled Copy provenance combination acceptance
is stage-complete. Next: bounded reconciliation planner, beginning with contract,
budgets, source/current qualification and failure invalidation rules. Do not add
more Copy combination scenarios unless an actual defect is found.** No planner
implementation was started in this acceptance round. F2/F3 stay Open, restricted
eligibility blocked; original F3 still has two expected 99/observed 10 failures
(exit 42), F2 backing 77/cache 10. Cross queue Unknown, no ordinary automatic
replica selection, no latest-writer authority or Draw/Dispatch expansion. Android
was skipped. Evidence-only documentation does not retrigger unchanged source CI.


### Bounded Reconciliation Planner: minimal candidate generation (2026-10-18)

Runtime implementation `374977121d`, receipt/mutant acceptance fix `6a652ed561`
and naming/fail-fast fix `4b856d0921` add a stateless spatial candidate generator
and read-only actual-evidence Scheduler adapter. One known GuestBackingEpoch,
one contiguous complete checked view and 2-3 explicit staged replicas are allowed.
Source RootSpans are explicitly offered patch intervals, not a physical producer
registry. A unique verified offer emits a Copy region; differing overlapping
roots/projections refuse, equivalent overlapping offers remain Unknown/ambiguous,
and unverifiable sources or target complements remain Unknown. No root/revision/
tick/completion-order election or physical content authority is inferred. Output
is owned existing BuildBatch metadata; query success is not an admission lease.

[Linux-only run 39123951599](https://github.com/Splaser/emulator/actions/runs/39123951599)
passed actual production/native compilation, CLI/shader/tests, all five CTest
processes and the complete registration/coherency/backing/lifecycle regressions.
The existing Vulkan layout uses generated first/fourth-hop candidates through
original admission, including actual legacy source / batch target receipt inputs:
398 assertions plus three repeats with full readback and root/current oracles.
Pure logic has 128 byte oracles, three repeats and 12 expected assertion mutants;
24 extracted-body query/admission modes plus three full fixture repeats and all
78 mutations pass. Failure/prefix/uncertainty injection remains simulated evidence.
All 24+6 input hashes and exact source match Git blobs. Complete job/artifact,
generated-source/mutation/repeat logs and an independently verified index are in
`.diagnostics/ci-39123951599/`.

The first run `39122121531` failed because a budget mutant left a counter unused
under Linux GCC; its explicit discard now preserves assertion-based mutation
sensitivity without weakening warnings. Run `39122571156` passed all 90 mutants
then found Scheduler State/state name shadowing; only names changed. Both failed
artifacts/job logs are retained in their respective `.diagnostics/ci-.../` folders.
Real translation units now compile before the slow mutation loop for faster failure.
See [M4_F3_BOUNDED_RECONCILIATION_PLANNER.md](M4_F3_BOUNDED_RECONCILIATION_PLANNER.md)
for the API, deterministic reasons, changed files, scope and full acceptance.

Copy combination expression remains stage-capped; no new Copy forms were added.
This is the first bounded candidate-planner step, not full replica reconciliation.
It does not auto-select ordinary BufferCache replicas, publish latest-writer
authority, enable cross queue or add Draw/Dispatch producers. Cross queue remains
Unknown, restricted production eligibility blocked and F2/F3 Open. Original F3
fixed-required still fails two aliases (99 versus 10, exit 42); F2 backing77/cache10.
Android was skipped. Evidence-only documentation does not rerun unchanged source CI.

### Producer Coverage / Exclusion Contract (2026-10-18)

Source `3f7a3440e9` freezes the existing Planner/Copy/Scheduler implementation and
adds `ProducerCoverageContract`, a bounded synchronous **assertion verifier** and
physical pre-effect invalidation policy. Transfer is the only managed producer;
Shader, CPU, NCE and External require independently enforced exclusion/drainage.
Unknown/missing entry paths, notification gaps, active writers, overflow,
insufficient sync, stale admission, remap and retirement reject closure.

This is not an exclusion provider, certificate issuer, new history registry or
authority. There is no production call site. Its `Eligible` result means supplied
assertions are consistent, not that guest producer exclusion is true or retained.
The existing restricted-eligibility blockers remain unchanged: booleans, fixture
locks, notification counts and retained pins cannot replace owned enforcement.
Actual physical alias invalidation fan-out and exclusion through async completion
or quarantine remain required future work. Ordinary production closure is Unknown.

New pure tests use 400 per-byte oracle cases, three repeats, a toy two-thread
exclusion provider, a weakened-contract red control and 20 assertion mutants.
The failure/partial-recording policy checks are simulations, not hardware fault
injection or real CPU/NCE execution proof. Original native tests and F2/F3 witnesses
continue unchanged. See [contract and audit](M4_F3_PRODUCER_COVERAGE_CONTRACT.md)
for trust boundaries, producer inventory links, admission and invalidation rules.

Cross queue remains Unknown; no ordinary automatic selection, Draw/Dispatch
expansion, content authority or F2/F3 repair is claimed. Android is skipped.

Accepted exact source `3f7a3440e9a35afaf8fc9f22df201f7953133ee9` in
[Linux run 39127457695](https://github.com/Splaser/emulator/actions/runs/39127457695):
new 20 assertion mutants and 400 byte cases plus three repeats; existing 78+12
mutants and full repeat groups; real native provenance 398 assertions plus three
repeats; all five CTest processes and complete coherency/lifecycle suites. All
24+6+4 input hashes match Git. Full artifacts, job output and independently checked
acceptance index are in `.diagnostics/ci-39127457695/`. Native Copy evidence does
not establish real guest producer exclusion. Original F2 cache10/backing77 and
F3 two expected alias failures/exit42 remain; restricted eligibility stays blocked.

### Physical alias Current Content Gate fan-out (2026-10-18)

Source `fecb02c9a9` adds `Scheduler::InvalidatePhysicalWrite`: an owner-thread,
pre-effect bounded scan over existing retained transfer resources, saved backing
identity and physical segments. Every intersecting representation loses the
projected old current interval; untouched/disjoint/different-backing intervals
retain qualification where independently valid. Remapped/retired/unknown bindings
reject conservatively. No immutable receipt ancestry is changed.

An explicit `RecordControlledPhysicalFill` endpoint qualifies a saved resource,
performs fan-out before recording, and reuses RecordTransferBatch for the new Fill's
own retained pins/worker/checked submission/retirement. It does NOT call the pure
coverage verifier or grant an exclusion lease. Native regression now exercises
three allocations under the same physical interval, a 192-byte gate oracle, actual
Fill/readback and unchanged peers/history. Extracted production methods add 14
failure/lifecycle/projection modes, 24,576 reordered/duplicate-page byte checks,
strict pre-enqueue revocation assertions and nine assertion mutants.

This is precise invalidation of existing tracked gates for explicit writes, not
complete physical producer enrollment. Ordinary native-only hooks are unchanged;
real CPU/NCE/raw-pointer/mapping exclusion through asynchronous effects is still
absent. An owned enforcement provider remains the next gate. Cross queue stays
Unknown, production eligibility blocked and F2/F3 Open. Planner/Copy expression,
ordinary automatic source selection and content authority are not expanded.
See [implementation and evidence boundaries](M4_F3_PHYSICAL_ALIAS_INVALIDATION.md).

Exact source `fecb02c9a98c6441bf58356a06035b29135f5c0e` passed
[Linux run 39134590517](https://github.com/Splaser/emulator/actions/runs/39134590517):
87 extracted-body assertion mutants (nine new), existing 12 Planner/20 coverage
mutants, 14 fan-out fixture modes plus 24,576 page-projection byte checks and three
full repeats; native Vulkan 407 assertions plus three repeats with the new 192-byte
fan-out oracle and prior four-hop Copy/Planner; all five CTest processes and complete
coherency/lifecycle/backing regressions. All 25+6+4 hashes match Git. Complete logs,
artifacts and an independently verified index are in `.diagnostics/ci-39134590517/`.
Android skipped; original F2 cache10/backing77 and F3 aliases99/10/exit42 retained.
Only controlled invalidation is proved; ordinary producer enrollment and an owned
exclusion provider remain unimplemented, so production closure stays Unknown.

## 2026-10-19: bounded private allocation provider accepted

Source `3d271fc313322403a451cf6e4fd2409504530ff9` implements the first real
exclusion provider only for fresh private Linux allocations: one unexported CPU
mapping and 2-3 fresh staged replicas, no existing guest/native-resource adoption.
Actual OS read-only protection precedes admission, existing physical alias fan-out
precedes recording, and the existing checked transfer retains the guard through
real completion. Unknown/epoch loss/retirement/early drop seals the domain and
keeps CPU protection and native retention through trusted-idle retirement. Old
receipt polling cannot release a newer guard. Weak domain references cannot keep
VMA pins alive past renderer lifetime. Private CPU writes only invalidate; no
reconciliation or content authority is published.

[Linux run 39140867733](https://github.com/Splaser/emulator/actions/runs/39140867733)
passed: real kernel store exclusion with six assertion mutants and three repeats;
twelve extracted-body provider modes, four new mutants within 91 total, and three
fixture repeats; actual native Vulkan 430 assertions in each of three runs; existing
12 Planner/20 coverage mutants, all five CTest processes and complete backing,
coherency/lifecycle regressions. Kernel-unit completion and fixture GPU failures
are injected; native checked completion/readback is real. 28+6+4+4 hash entries
match the exact source. Full logs and an independent acceptance verifier/index
are archived in `.diagnostics/ci-39140867733/`. Android skipped. This completed
run was recovered after workstation shutdown without duplicate CI dispatch.

Original F2 cache10/backing77 and F3 aliases99/10/fixed-required exit42 remain.
Ordinary guest/NCE/fastmem/raw-pointer exclusion and cross queue stay Unknown;
restricted production admission is blocked, F2/F3 Open. No ordinary automatic
replica selection, Draw/Dispatch expansion or physical latest-writer authority.
Do not relabel private Linux native-store protection as an ARM NCE runtime hook.
Planner/Copy expression stays frozen. Future guest adoption requires enforcement
of all writable aliases across asynchronous effects, not a coverage-query result.
See [provider contract and acceptance](M4_F3_PRIVATE_PRODUCER_PROVIDER.md).

## 2026-10-19: guest-backed exclusion feasibility audit

[Static audit at 0ea2b9a5c8](M4_F3_GUEST_EXCLUSION_FEASIBILITY_AUDIT.md) selects
acknowledged whole-guest quiescence plus host/device producer close-and-drain and
an all-alias mapping freeze as the first guest-backed mechanism. NCE/JIT stay
stopped through checked GPU completion; no live fault-parking implementation is
selected. PhysicalPageGates only drain enrolled stores. The current NCE access
fault chain can enter cache locks/GPU handoff and cannot acquire or wait for an
exclusion lease safely; access/alignment fallback must not emulate a conflicting
store or skip it as success. Existing SuspendEmulation really waits for guest
execution to stop, but does not supply complete host/device producer drainage.

General guest admission remains Unknown. Missing prerequisites are exhaustive
alias/owner/export coverage (including canonical pointers and audio/VIC writers),
host/device close-and-join, mapping/permission freeze, bounded safe quiescence and
retained guest-wide Unknown isolation. A single VMA mprotect, generation check,
gate counter or successful private-domain test cannot substitute. No runtime code
changed and no new CI/build/device tests were dispatched for this static audit.
Planner and ordinary BufferCache remain frozen; authority absent; F2/F3 Open.

## 2026-10-19: managed close-and-drain implementation; historical pending checkpoint

User requested immediate push/handoff while GitHub workflow runs, to continue on
another workstation. All implementation is pushed on `codex/issue-372-fixes`:
`827073eab4` plus Memory test-peer access fix
`7e6996daa317835d76f0b5d4d5ce6a3d884d88bb`.

New bounded ProducerDrain closes actual PhysicalCore::RunThread entry and existing
CpuWriteOwner::TryVisit. Kernel factory always collects all four core gates;
Memory endpoint closes only its own controlled owner. A counted call/callback must
return before DrainedSubset; early resume is refused. Polls have a fixed call budget,
no writer wait, forced NCE interrupt, GPU callback or signal-handler integration.
Drop/timeout/retirement seals further entry. Existing in-flight code can still be
running after Unknown: this is NOT safe physical backing isolation. No backing or
native pin, alias census, mapping freeze or asynchronous GPU lease is supplied.
GuestBackingAdmission always Unknown. Do not claim full guest suspension or resume
after possible GPU effects via these CPU-only no-effects cancellation endpoints.

Local Windows g++ units and extracted-body integration passed three repeats and
15 assertion mutants. Extracted backend execution is mocked; native integration
uses privileged held-entry injection, not actual ARM/NCE execution. Linux has a
real two-MAP_SHARED-alias counterexample; it has not run locally. Native tests add
actual Kernel/PhysicalCore/Memory-owner closure/drainage to the existing Vulkan
case. Linux workflow now compiles kernel.cpp/physical_core.cpp early and preserves
`.diagnostics/m4-producer-drain/*`. Original F2/F3 witnesses are unchanged.

Current [Linux run 39179992784](https://github.com/Splaser/emulator/actions/runs/39179992784)
is for exact source `7e6996daa317835d76f0b5d4d5ce6a3d884d88bb`, pending at handoff.
Do not treat local results or the superseded 39179956771 run as its acceptance.
Next read this run's first real error/result; avoid duplicate dispatch. Download
artifacts, verify new mutant/repeat/native markers and original witnesses, match
hashes, then record acceptance separately. Do not extend implementation while
unresolved compile/test failures remain. Android skipped by linux-only scope.
See [implementation, scope and remaining checks](M4_F3_MANAGED_PRODUCER_DRAIN.md).
Ordinary host/raw aliases, audio/VIC/I/O, mapping and GPU drainage remain outside
the covered subset. No BufferCache auto-selection or authority; F2/F3 Open.

## 2026-10-19: resumed workstation; managed drainage Linux acceptance closed

Resumed `codex/issue-372-fixes` at `62209d4816`, matching the PR 375 head.
Origin and upstream remote references were refreshed. No upstream merge or new
workflow was required for the pending acceptance gate.

[Run 39179992784](https://github.com/Splaser/emulator/actions/runs/39179992784)
passed at exact implementation/test source
`7e6996daa317835d76f0b5d4d5ce6a3d884d88bb`; Android was skipped. Artifact
`12157085513` was downloaded, and all 108 source hash entries in seven manifests
matched both that Git source and the resumed checkout. Fifteen new assertion
mutants plus the baseline red control failed as intended; both new fixtures and
the real Linux omitted-alias counterexample passed three additional repeats.
Each of three native provenance repeats passed 447 assertions with the four-core
and controlled Memory-owner drainage marker. All five CTest processes and the
configured regression suite passed. F2 remains backing77/cache10; F3 still has
two expected alias failures and fixed-mode exit42. Detailed evidence and scope
are in [the acceptance record](M4_F3_MANAGED_PRODUCER_DRAIN.md#2026-10-19-linux-acceptance-recovered-and-verified);
local logs, artifacts and verifier are in `.diagnostics/ci-39179992784/`.

The pending Linux gate above is now closed for this managed subset only. The
next gate is exhaustive owner/alias enrollment and mapping/permission closure
from the feasibility audit, followed by retained guest isolation through checked
GPU completion. No live ARM/NCE stop/resume was proved, no full guest provider
exists, and no production admission/authority or ordinary routing is enabled.
Planner/Copy remains frozen; F2/F3 remain Open. No Android compilation was run.

## 2026-10-19: HostMemory mapping enrollment/freeze component

The [HostMemory mapping contract](M4_F3_HOST_MAPPING_FREEZE.md) adds an owner-created
bounded actual alias ledger and a continuous mapping-entry closure around real
Map/Unmap/Protect, backing materialization/clear/pin and direct-address mutation.
The final design is explicitly enrolled at construction for fresh controlled
arenas only; ordinary Core::DeviceMemory/NCE arenas remain default-off, allocate
no new ledger state and enter no new mapping mutex. No later adoption is supported.
Partial interval complements, canonical backing and irreversible raw export-family
history are retained. External/driver/direct-address/unsupported arenas and census
overflow refuse. A token can explicitly cancel CPU-only closure; unfinished drop
seals entry and owner retirement invalidates metadata. No backing pin or GPU lease
is issued, and guest backing admission always refuses.

Initial default-on checkpoint (historical): local macOS checks passed three repeats and ten assertion mutants, including
an independent 256-prefix physical-offset/permission byte oracle. Real Linux VMA
tests, seven actual-wrapper guard mutants and complete regression acceptance are
pending. The Linux workflow now includes these tests and preserves their evidence.
No Android build is requested. Do not enable this on a running guest: outer CPU
PTE changes, services, raw stores and GPU mappings/publications are still unclosed.
An inner HostMemory refusal cannot undo earlier outer publication. Next compose
outer entry closure and real owner lifetimes before any guest admission attempt.

Implementation is pushed through `465f626f7b55372eade5896fc28276a8239d4c33`.
It preserves moved-from PinPhysicalRange queries and keeps already-ready backing
lookups free of the new cold mapping mutex. Current Linux-only acceptance run is
[39187373524](https://github.com/Splaser/emulator/actions/runs/39187373524), with
the real mapping suite before native integration compilation. Android is skipped;
superseded runs `39187093671`/`39187016054` are cancelled. Acceptance remains pending.

Run `39187373524` subsequently failed in the standalone production fixture link:
the harness's source include path preceded its logging/assert stubs. The ten
primitive mutants passed, but no production/native acceptance is inferred. The
test include order is corrected for the next Linux-only run. Production source
is unchanged by this harness repair; complete Linux acceptance remains pending.

Latest acceptance attempt:
[39201797569](https://github.com/Splaser/emulator/actions/runs/39201797569), exact
source `cc409319a18286f0c602ae47fab5b2dfdd57ca71`. It includes the harness include
order correction. The focused real Linux mapping suite passed all three positive
repeats, the injected NDEBUG protection-failure control and ten primitive/seven
actual-wrapper assertion mutants. Native compilation/full regressions are still
in progress; Android is skipped. Recover this run's result and artifact before
further implementation or another dispatch.

### Constructor-only enrollment correction

Run `39201797569` is now cancelled after source review found that default-on
HostMemory::Protect tracking could add a mutex to the existing NCE fault chain.
Its early native compilation and old synchronization/content boundary gates passed,
but full build/regression acceptance was not completed. Do not use its focused
results as acceptance for the revised constructor-only opt-in component.

Ordinary guest arenas now stay untracked and cannot freeze, with no new ledger
allocation or mapping mutex. Only fresh explicitly controlled arenas enroll.
Local macOS checks pass three repeats and eleven assertion mutants, adding a forced
legacy-domain-enrollment negative. The real Linux suite additionally mutates the
HostMemory public constructor default and expects ordinary-arena refusal; it still
has seven actual wrapper guard mutants. At that checkpoint final Linux acceptance was pending;
Android remains unrequested. No NCE runtime stop/resume or fault recovery is claimed.

Current final-source acceptance attempt:
[39205308506](https://github.com/Splaser/emulator/actions/runs/39205308506), exact
source `a080cc5a2ac61263c5014b94cf25836bd87ddb50`. It verifies constructor-only
enrollment: eleven primitive + seven wrapper + one public-constructor assertion
mutants, three positive repeats and full original Linux regressions. The Linux
job has now passed in full; Android was skipped. No duplicate dispatch is needed.

### Final constructor-only component Linux acceptance

Source `a080cc5a2ac61263c5014b94cf25836bd87ddb50` passed
[run 39205308506](https://github.com/Splaser/emulator/actions/runs/39205308506).
Artifact `12166179272` was downloaded and its ZIP digest verified. All 114 source
hash entries across eight manifests match that Git source and the resumed checkout;
later commits change only documentation. Three primitive/production/protection-
failure repeats and all nineteen assertion mutants passed their required controls.
All five CTest processes, native Vulkan provenance 447 assertions in each of three
extra repeats, complete backing/coherency/lifecycle regressions and original F2/F3
negative controls passed. F2 remains backing77/cache10; F3 require-fixed still has
two alias failures and exit42. Evidence and independent verifier/index are retained
in `.diagnostics/ci-39205308506/`; details and evidence boundaries are in the
[component contract](M4_F3_HOST_MAPPING_FREEZE.md).

This closes only the fresh explicitly enrolled HostMemory mapping component.
Ordinary guest/NCE arenas remain default-off with no new ledger allocation/mutex.
Complete owner/borrow enrollment, outer CPU PTE/device/GPU mapping/publication
closure, host/device drainage and retained GPU-lifetime isolation remain next
gates. No ordinary routing, content authority or GPU admission is enabled; F2/F3
remain Open. No Android compilation was run.
