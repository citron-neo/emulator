# M4 P2: AA helper retirement before replacement

2026-10-24 Windows checkpoint, from `bdd1977472c6095e7fb201b1ec33091b4270dafb`.
Branch `codex/issue-372-fixes`; main is untouched.

## Defect and the extra internal-Finish counterexample

RefreshResources reset anti_alias before ReleaseRawImages waited. SetAntiAliasPass
replaced its unique owner before any wait. A previous slot's recording/native work
could therefore still refer to a deleted helper's images, views, framebuffer,
descriptors or pipeline.

Simply moving reset after a slot wait is insufficient. ConfigureDraw's scope-exit
stores CurrentTick, then WindowAdaptPass records its final sampling command after
configuring the layers. A subsequent layer's preparation/internal Finish can
advance the submission between those events. The final composite can consume an
AA output at a later tick than the slot recorded. Waiting every cached slot tick
still misses this later use. A shared accelerated source packet owns its selected
cache image/view, not the AA output or helper resources.

The historical fixture extracts old RefreshResources/SetAntiAliasPass and
ConfigureDraw through AA Draw, including its real scope-exit. Eight named
witnesses (two mutation routes times ordinary, late-final, internal-Finish and
ConfigureDraw cases) require exit 91 on AA retirement with unfinished users.
The resource state is a destruction probe, not an actual dangling dereference.

## Candidate before production

`aa_retirement_candidate.json` preserves an in-memory transformation of pinned
pre-fix production bodies. The candidate passed the known normal/error/control
cases before the production edit. Expanded candidate and actual-production
suites were rerun for final acceptance. The baseline remains reproducible even
when current production has been repaired.

The candidate uses the existing FinishNativeBorrow boundary only on actual
resource replacement, with the old owners still present. This seals and completes
the complete already-recorded graphics prefix and drains host recording before
allowing retirement. It avoids deriving a lifetime certificate from per-slot ticks.
A controlled multi-slot and late-final counterexample rejects a candidate that
waits cached ticks only.

## Production change

RefreshResources first calls WaitForResourceUses. Only after it returns does it
change dimensions/format, reset anti_alias and clear old raw resources. AA mode
replacement calls the same boundary before constructing/replacing a live helper.
ClearRawImages releases raw views before images and then the staging buffer;
ReleaseRawImages shares this clear operation after its existing destructor waits.
The destructor does not introduce a new Finish or reopen a closed Scheduler.

WaitForResourceUses skips a truly cold owner set. The existing unchanged-dimensions
and unchanged-AA fast paths also return before any new synchronization. Warm
replacement makes one existing-style Finish submission, waits that full graphics
prefix, drains host recording and checks native lifetime for the saved tick.
The normal per-slot reuse policy in ConfigureDraw is unchanged. Queue ordering,
barriers, ordinary error policy and content/admission machinery are not modified.

Vulkan or host exceptions before confirmation use TerminalFailStop at
`layer.resource_retirement`; checked completion and closed admission keep their
existing native_borrow stages. No selected old AA/raw owner is destructed on the
uncertain path, no atexit or C++ unwind occurs, and partial completion is never
retried as if no effects had happened. This includes partial slot completion,
submission/recording/context failures, a host-drain failure and fallback failure.
A successful wait for one slot is not success for the remaining slots.

SetAntiAliasPass now selects the constructor using effective_aa and publishes
anti_alias_setting only after construction/assignment succeeds. A failed
constructor leaves the previous helper and previous setting together, permitting
a later retry. Actual FXAA/SMAA/TAA constructors create their objects before Draw;
this is not a general rule allowing an asynchronous constructor to unwind.
Raw-image/staging recreation after a successful old-use drain still has its
existing partial-allocation behavior; this change does not make it transactional.

## Why this boundary is sufficient for the selected mutation

The existing GPU-owner serialization must keep the old helper's consumers ahead
of its mutation. In the covered WindowAdapt route, each Layer is configured once
per draw and its previous final composite has already been recorded when the next
mutation is considered. All those helper uses share Scheduler's graphics queue.
Completing the newly sealed prefix therefore includes every slot and the later
final sampling use. This does not authorize arbitrary escaped helper handles or
concurrent direct callers.

The asynchronous presentation copy consumes the independent output Frame image,
not these AA helper resources. The helper boundary therefore needs graphics
completion, not presentation-engine/scanout retirement. Output-frame presentation
ownership retains its existing separate checked fence and shutdown boundaries.

No scheduler continuation, immutable helper transfer packet or range admission is
required for this serialized destruction-only boundary. If helper borrows start
escaping to later not-yet-recorded consumers, that premise will fail and a real
owner handoff must be added. Cached ticks cannot replace such a handoff.

The cost is a graphics-prefix stall during warm resource/AA changes. There is no
new per-frame wait on unchanged/cold paths, but device/performance validation
remains necessary. No submit mutex is held over host/native waits. Existing
Renderer closure, worker joins and checked idle still govern whole-owner teardown.

## Deterministic Windows evidence

Candidate and fixed suites each pass 25 normal/retention/selection paths and 24
terminal paths, three repetitions, plus ten exact controls three repetitions.
The 25 include two routes times twelve scenarios and one constructor-failure/retry
case. Scenarios cover three slots, an actual extracted scope-exit tick update,
helper/internal Finish and later final use, native/host-drain semaphore pauses,
unchanged/cold paths and effective FXAA/SMAA/TAA/None selection. The 24 terminal
paths cover two routes times twelve faults, including partial/split wait failures.

The two pause cases run the mutation on a real std::jthread, pause at a semaphore
boundary and inspect old helper/raw ownership before releasing the boundary. No
sleep establishes ordering. Terminal children require exit 86 with exact stage
and reason and no AA/raw destruction or atexit. Eight old witnesses require their
named exit-91 lifetime failure; compiler errors/timeouts are not accepted.

Controls omit either mutation drain, wait only slot 0, wait all stale cached
ticks, reset before waiting, ignore a failed wait, publish settings early, omit
checked completion, omit host drain or construct using the old setting. Each must
produce its named runtime failure. The host-drain/completion controls also enforce
the confirmation protocol, independently of the fixture's native progress.

Actual extracted code: Layer RefreshResources/SetAntiAliasPass/ReleaseRawImages,
the new wait/clear methods, ConfigureDraw prefix through AA Draw and SCOPE_EXIT,
Scheduler Finish/RequireNativeBorrowAdmission/FinishNativeBorrow, TerminalFailStop.
Fixtures: helper native resources/destruction probes, constructor allocation,
AA Draw, the final WindowAdapt record sequence, submission/native progress and
Wait/WaitWorker collaborators. The helper Draw fixture invokes the actual Finish
body to split submissions. Full FXAA/SMAA/TAA shader execution, native graphics,
presentation engine, game and guest memory are not executed by these tests.

Three real Windows GCC 13.2 AMD64 COFF production units compile with zero
diagnostics using pinned real headers and cached Boost: Layer, BlitScreen and
WindowAdaptPass. The latter uses a real glslang-generated presentation shader.
No application link, MSVC, local Android build, Linux/Android/device/game or
current-head workflow acceptance is claimed. The integration workflow includes
the fixed suite, but was not manually dispatched here.

Regressions: accelerated native source ownership and full SubmitExecution,
mapping-use/raw-source copy, capture/native upload borrow, native fallback failure,
and actual renderer/worker shutdown order. Their separate manifests record exact
inputs and fixture limits. There is no broad data-provenance rerun for this
Layer-only mutation change.

See [manifests, hashes, commands and cleanup](M4_P2_AA_HELPER_RETIREMENT_RESULTS.json).
Current-run exe/object/generated fixtures are removed after hash verification;
logs/JSON remain under `.diagnostics/m4-aa-results`. Ignored logs do not travel by
Git; the committed summary records exact inputs and resume commands.

## Continuation boundary

Next: partial applet_frame initialization/publication and retry, plus partial raw
resource allocation/recreation. Other native helper constructors, capture-output
state machines, arbitrary raw API escapes and all guest producer/alias coverage
remain separate. Do not upgrade this replacement boundary into reversible GPU
exclusion or content authority.

HostMemory remap/protection remains high risk. M4 phases remain incomplete,
F2/F3 open, GuestBackingAdmission Unknown.

Follow-up: [applet Frame construction publication](M4_P2_APPLET_FRAME_PUBLICATION.md)
now covers image/view/framebuffer construction failure and retry from an empty
member. Partial raw recreation and first-draw content readiness remain open.
