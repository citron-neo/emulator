# M4 P2: applet Frame construction publication and retry

2026-10-24 Windows checkpoint, from `2a3a0f4a70eb28f3e5c20c29b8511b12ffeb19dc`.
Branch `codex/issue-372-fixes`; main remains untouched.

## Selected defect

RenderAppletCaptureLayer wrote dimensions and each native object directly into
applet_frame. A view creation failure left image published; a framebuffer failure
left image/view published. The next call tested only image, skipped creation and
passed an incomplete tuple to DrawToFrame. GetAppletCaptureBuffer also tested
only image and could schedule a download from that unrendered partial frame.
Even an image allocation failure had already changed the member dimensions.

Ten historical witnesses are retained: failure at each of image/view/framebuffer,
three factory observation gates, view/framebuffer retry skipping creation, and
view/framebuffer failure followed by premature readback. Each requires a named
exit-91 state/lifetime witness; crash, build failure and timeout are not accepted.

## Candidate and repair

The candidate is preserved in `applet_frame_candidate.json`, applied in memory to
pinned old production source. Candidate cases and controls passed before editing
production; the expanded candidate and actual-production suites were rerun for
final acceptance.

The function now creates a local value-initialized Frame, assigns its dimensions,
creates image, then matching view, then framebuffer. A creation exception unwinds
that local candidate; the member remains entirely empty, including dimensions.
The actual Frame declaration destroys framebuffer before view before image. No
DrawToFrame or download is issued for an incomplete candidate.

Only after every native creation call returns does the GPU owner move the complete
Frame to the member. A static_assert requires nothrow move assignment of the real
Frame type, so commit itself cannot throw after a subset of fields transfers.
The empty-destination precondition is essential: memberwise move assignment is
not a general safe replacement procedure for a live parent/child bundle. A control
which recreates and overwrites a live frame must fail at the parent/child oracle.
No member declaration order has been changed to implement this repair.

This is a logical publication point under the existing serialized GPU owner and
sync-capture request protocol. It is not an atomic move for arbitrary concurrent
readers, an independent producer lease, or a new shutdown admission mechanism.
The initial member is value-initialized by the existing Renderer constructor;
only this private path creates it. Existing complete frames take the same reuse
path, with no extra allocation, native submission, wait or scheduling change.

A failed allocation retains the old empty member and propagates the original
Vulkan/host exception. If the caller continues, the next call retries all necessary
creation steps from empty. There is no internal retry loop, fabricated success,
assumption that a whole application survives a Vulkan error, or replay of already
submitted work. The local candidate has not been used by GPU commands and can
unwind normally; the existing checked idle path handles uncertainty while creating
or replacing a BlitScreen pass.

Framebuffer creation can have an independent helper side effect: the BlitScreen
window-adapt pass may already exist when the native framebuffer call fails. That
helper is kept and reused on retry. The selected transaction covers applet_frame,
not every BlitScreen field. Tests verify that the existing pass is not needlessly
recreated after a framebuffer driver failure and that a failed pass constructor
is retried. The existing checked WaitIdle behavior is regression-tested separately.

## End of the construction boundary

The complete frame is published before DrawToFrame. If Draw later throws, complete
native owners remain published. They must not be rolled back or freed merely
because drawing returned an error: real drawing may already have partial recording
or native effects. The fixture checks this owner-retention distinction.

Complete image/view/framebuffer ownership is not proof that valid pixels have
been produced. First-draw failure/content readiness, capture state after partial
rendering and recovery of asynchronous native effects remain a separate audit.
The fixture's normal readback bytes are simulated, not a native pixel-content
certificate. Existing readback completion/invalidation and Renderer shutdown
closure/join/checked-idle protocols are unchanged.

## Deterministic Windows evidence

Candidate and fixed suites each pass 24 paths and seven exact controls, all
repeated three times. The paths include normal complete reuse and input filtering,
image/view/framebuffer Vulkan and host failures, factory gates for all three
objects and pass creation, repeated mixed failures, failed-creation readback,
retry, pass/helper creation failure, and failure after complete publication at Draw.

Gate cases run the creation attempt on a real std::jthread and block at a
semaphore immediately before a factory returns or throws. The observing thread
reads member state only while that owner is blocked, before any commit can run.
No sleep establishes ordering and no concurrent read overlaps fieldwise commit.
The member must remain empty at every gate. Failed prefixes must release every
native object with children before parents; subsequent retries must create a
matching complete tuple. Success must reuse exactly the same image and issue
normal synchronized readback once. Empty/filter-only inputs must allocate and
record nothing.

The seven controls construct directly in the member, publish dimensions early,
publish image early, omit framebuffer creation, use the wrong view parent, omit
publication or overwrite a live frame. Each must fail at its named runtime oracle.
The ten baseline witnesses also repeat three times. No target-device behavior is
inferred from their exit codes.

Actual extracted code: RenderAppletCaptureLayer, GetAppletCaptureBuffer, Frame
declaration and both BlitScreen CreateFramebuffer overloads; real terminal helper.
Fixtures: image/view/framebuffer wrappers, native parent-child destruction probes,
driver allocation faults, window-adapt pass creation/idle collaborator, Draw,
Scheduler recording/completion, output bytes and the factory gates. The wrappers
model ownership and exception points; they do not execute actual Vulkan factories,
VMA, shaders, native queues, a game or guest Memory/GetPointerImpl.

Two real Windows GCC 13.2 AMD64 COFF production units compile with zero
diagnostics using real pinned headers and cached Boost: renderer_vulkan.cpp and
vk_blit_screen.cpp. This compiles the static_assert against the actual native
Frame members. No application link, MSVC, local Android build, Linux/Android/device/
game or current-head CI acceptance is claimed. The integration workflow includes
the fixed suite but was not manually dispatched here.

Regressions passed: capture/local upload borrow (55 paths three repetitions plus
seven controls), BlitScreen checked idle (four success/eleven failure paths and six
controls, three repetitions), AA retirement (25 normal/24 terminal paths and ten
controls, three repetitions), and actual renderer/worker shutdown order (ten paths,
three repetitions). Their manifests record source hashes and fixture limits.
There is no broad guest coherency or native-content validation in this change.

See [results, hashes, manifests and commands](M4_P2_APPLET_FRAME_PUBLICATION_RESULTS.json).
Current-run temporary exe/objects/generated fixtures are removed after hash
verification; logs/JSON remain under `.diagnostics/m4-applet-results`. Ignored
local logs do not travel by Git; the committed summary records exact inputs and
resume commands.

## Continuation boundary

Next: partial raw resource allocation/recreation, and applet capture's first-draw
content readiness after a fully constructed frame. Other helper constructors,
arbitrary raw native escapes and all guest producer/alias obligations remain
separate. Do not promote a complete Frame tuple into native/content completion
or whole-renderer lifecycle acceptance.

HostMemory remap/protection remains high risk. M4 phases remain incomplete,
F2/F3 open, GuestBackingAdmission Unknown.

Follow-up: [failed-Draw content/readback and terminal release](M4_P2_APPLET_CONTENT_READBACK.md)
now quarantines partial applet recording/submission, refuses export/retry and closes
before shutdown drains. Normal Recorded state still needs checked readback; it is
not target-device pixel validation. Partial raw recreation remains open.
