# M4 P2: selected Frame recreation and Blit resource publication

2026-10-25 Windows checkpoint from `7e8c2d1b8c8c4f014b17f460c239a6090862a097`.
Branch `codex/issue-372-fixes`; main is untouched.

## Findings and bounded scope

PresentManager::RecreateFrame wrote dimensions before allocation, then assigned
image, view and framebuffer directly into live members. Image failure left new
metadata on old resources. Success or a later view/framebuffer failure could destroy
the old image while an old view/framebuffer still referenced it. A same-request
retry could no longer trust that metadata represented a complete resource tuple.

BlitScreen exchanged cached count/format and cleared layers before the replacement
pass and target Frame existed. Failures lost old layer owners/settings and could
make a retry skip work. Shrinking image_count retained the old image_index until
after Draw, so a 3-to-1 change could address slot 2 in a one-slot Layer. The applet
CreateFramebuffer overload shared the same early pass/cache publication boundary.

A separate key gap appeared when exercising pooled Frames: a global Blit format
cannot describe every target Frame. Repairing one same-size Frame must not cause
another still-old Frame to skip recreation. Conversely, selecting a Frame whose
own format is already correct must still replace a differently keyed global pass.
Both directions require independent per-Frame and per-pass format checks.

Eight historical controlled witnesses reproduce old publication, parent-order,
shrink and CreateFramebuffer failures, three repetitions each. They require named
runtime assertions, not crashes/build errors/timeouts. The core candidate passes
before production mutation; the expanded candidate adds pooled-format, reverse
reuse and all six shrink combinations before those changes enter production.
This is production-body/controlled lifetime evidence, not a device/game report.

## Complete resource tuple and parent lifetime

RecreateFrame checks existing Scheduler native-borrow admission before preparation.
It builds an otherwise empty local Frame candidate: image, matching view, matching
framebuffer, dimensions and requested view format. If any factory throws, only the
unused candidate prefix unwinds. Its framebuffer/view/image destruction order is
child first; old publication, synchronization identities and old metadata remain.

After complete construction, no-throw swaps exchange only image, image_view and
framebuffer, then primitive metadata assignments publish dimensions/view format.
The candidate now owns the old resource set and destroys framebuffer before view
before image. No resource declaration is reordered. Real Windows compilation
checks std::is_nothrow_swappable for all three native types. Moving an entire Frame
onto a live destination would overwrite parents and synchronization objects and
is deliberately avoided.

cmdbuf, render_ready and present_done remain the original objects. The transaction
does not reset an outstanding fence, create a cancellation receipt or change pool
ownership. Per-Frame image_view_format defaults to VK_FORMAT_UNDEFINED and is part
of the resource publication. The applet and RenderToBuffer Frame producers record
their requested view format too, preserving the invariant for external-to-manager
capture Frames. Width/height, content and synchronization policies are otherwise
unchanged.

This is exception-atomic on the serialized owner, not a lock-free publication to
concurrent readers. Direct RecreateFrame still requires its owning caller to have
completed old host/presentation/native use. The production Blit route retains its
existing checked WaitIdle before replacement; the direct warm fixture explicitly
uses that precondition. Admission is not a new producer/use lease or an implicit
native drain for arbitrary direct callers.

## Blit preparation, commit and retry

CreateWindowAdaptPass returns an unused pass candidate for a snapshotted requested
filter/format. It does not clear layers or publish cached fields. Effective low-GPU
filter selection remains the existing logic.

For a changed DrawToFrame request:

1. Reject zero count before drains, allocations or state changes.
2. Determine selected-Frame completeness/dimensions/view-format separately from
   global pass/filter/count keys. A stale Frame or mismatched pass requires repair.
3. Run the existing WaitIdle (Finish, host recording drain, presentation drain,
   then checked native idle under submit_mutex).
4. Construct the pass candidate. If needed, recreate the selected Frame against
   that pass using the complete-tuple transaction.
5. Commit without throwing: clear completed old layers, swap the pass, publish
   filter/count/format and normalize image_index modulo the positive new count.
6. Construct missing Layers, then Draw. The normal final index advancement remains
   after a successful Draw only.

Pass or Frame factory failure preserves the old Frame/Blit publication, layers,
failed-layer flags and image index exactly. Existing idle may already have drained
old work; this is not a no-effects claim. Repeating the request rebuilds the missing
candidate rather than trusting stale cached count/format. Layer construction or
Draw failure after commit has a later boundary: the committed tuple is complete,
missing Layers can be filled on retry, and existing Layer/app/presentation error
policies remain in force. This is not an all-Draw rollback transaction.

The public layout CreateFramebuffer overload follows the same pass preparation:
create the new framebuffer against the candidate before committing pass/cache/
layers. A failure therefore keeps the old publication. On retry it may create that
unused pass again; the prior behavior of retaining the new pass after failed
framebuffer creation is intentionally replaced. The returned framebuffer's real
move construction is statically required not to throw. An unchanged-key call does
not recreate a pass or add an idle wait.

Successful replacement temporarily needs old plus new native memory and, on some
changes, both passes. OOM may remain an error rather than freeing a still-published
old set to make room. No automatic memory-pressure fallback is introduced. Normal
unchanged draws retain their existing scheduling; no new normal completion registry
or per-frame native submission is added.

## Slot, format and applet boundaries

All index 0/1/2 to count 1/2 combinations are tested. The normalized index is valid
before a new Layer is used, and count zero changes nothing. This is the Blit source
slot count, not proof about every presentation-pool bookkeeping transition.

Three targets with the same dimensions retain distinct view-format metadata. Each
stale target is repaired even after the global key changed; reverse reuse repairs
the pass when the chosen Frame already has the desired format. The global-format-
only and omitted-global-key controls each fail a different case.

Actual RenderAppletCaptureLayer remains empty/retryable for creation failure and
publishes only a complete Frame. Once Draw failed, its sticky Failed guard still
prevents retry and readback after recreation succeeds. Existing failed helper
quarantine, composition retirement and dequeued presentation fail-stop are not
reopened. Allocation success/view-format match is not initialized pixels, GPU
completion or permission to export failed applet bytes.

## Deterministic tests and limits

Candidate and fixed suites each pass 38 regular paths, five terminal paths and
eleven exact controls, each three repetitions. Regular paths include cold/warm
image/view/framebuffer failures, host/native factory faults, pass failure,
semaphore observations at every factory, direct and Blit retries, child-first
replacement, unchanged requests, zero count, six shrink combinations, pooled and
reverse format reuse, layout CreateFramebuffer failures, shutdown after failure,
and real applet construction/failure isolation.

Snapshots compare metadata, Frame/pass/layer owner identities, failed-layer flags,
count/index and synchronization identities. Real std::jthread/semaphore observers
read old publication only while the owner is paused in a factory. Native probes
reject parent release with children, release before old native use completes and
unused candidate leaks. Direct warm replacement first executes actual checked
Blit WaitIdle against controlled host/presentation/device collaborators.

Terminal paths cover closed Blit, closed direct RecreateFrame, closed layout
CreateFramebuffer, failed existing idle and failed shutdown idle. Exact exit 86
and stage are required; old owners cannot destruct after terminal failure.
Controls publish Frame metadata/image early, omit view commit, replace fence
identity, publish count early, omit index normalization, clear layers before
successful Frame preparation, omit old-use idle, remove applet refusal, use only
global Frame format or omit the global pass format key. Each requires its named
exit-91 assertion.

Actual bodies: complete PresentManager::RecreateFrame and Frame declaration,
complete Blit pass factory/DrawToFrame/both CreateFramebuffer overloads/WaitIdle,
complete applet render handler and actual readback refusal prefix. Native factories,
parent/use probes, source Layer/Window Draw, queue/device idle and successful
readback tail are fixtures. Factory host faults represent allocation boundaries,
not every STL allocator instruction or every WindowAdaptPass shader constructor.
No real Vulkan queue, GPU pixels, guest Memory/GetPointerImpl or application/game
runtime acceptance is claimed.

Current regressions pass three repetitions: final composition (84 returning/120
terminal/eleven controls; augmented closed fast-path admission witness), post-upload
helpers (288/36/ten), capture/native borrows, applet Frame (24/seven controls), applet
content (21/nine/six), Blit idle (four/eleven/six), ten renderer/worker shutdown paths,
and native source/fence fallback (26 regular/seven protocol/seven terminal/fourteen
controls). Legacy facades were adapted to the new pass factory and Frame format;
the actual tested bodies and named negative controls remain extracted.

Four real Windows GCC 13.2 AMD64 COFF production objects compile with zero
diagnostics: PresentManager, BlitScreen, RendererVulkan and WindowAdaptPass. Real
headers/dependencies and a glslang-generated presentation shader are used. No app
link, MSVC, Linux/Android build, target-device/game run or current-head CI acceptance
is claimed. The workflow includes the fixed suite; no manual dispatch was made.

[Results, commands, hashes and cleanup](M4_P2_FRAME_RECREATION_PUBLICATION_RESULTS.json)
retain exact final-suite manifests and local log/JSON evidence. Current-run
executables, objects and generated fixtures/headers are removed after exact path
and hash checks. Ignored logs do not travel by Git; the committed results identify
inputs and rerun commands.

## Continuation

Audit fixed presentation-pool ownership versus recreated swapchain count,
free_queue/frames_in_flight updates, CopyToSwapchain surface-loss retry and active
PresentThread failure. SetImageCount currently executes at construction, and only
the constructor resizes frames; RecreateSwapchain does not dynamically resize that
pool. Do not infer a pool-resize fix or cancellation protocol from Blit count
normalization. HostMemory remap/protection remains high risk; M4 incomplete,
F2/F3 open and GuestBackingAdmission Unknown.
