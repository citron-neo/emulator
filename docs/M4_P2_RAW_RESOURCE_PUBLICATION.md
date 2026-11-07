# M4 P2: Layer raw resource construction, publication and retry

2026-10-24 Windows checkpoint, from `3ca2af23f61d2883002087092bfd508389b5df4f`.
Branch `codex/issue-372-fixes`; main is untouched.

## Audit findings

RefreshResources updated raw dimensions/format, reset AA and cleared the old set
before staging and every slot's image/view existed. CreateRawImages resized member
vectors before populating them. A mid-slot failure could leave nonempty vectors
containing null entries; the next same-size refresh returned early and consumers
used an incomplete tuple. A staging failure also destroyed a previously useful
publication and its metadata before replacement existed.

The refresh key omitted stride even though CalculateBufferSize and GetRawImageOffset
use it. An unchanged width/height/format with larger stride could therefore reuse
an undersized staging allocation. This field is part of the same raw-resource
publication boundary and is included in the repair; ordinary guest coherency is
not changed.

Sixteen historical witnesses are retained: seven factory failure positions for
both cold and warm state, incomplete same-size retry, and undersized staging after
stride change. Each repeats three times and requires its named runtime failure,
not a crash, build error or timeout. The in-memory candidate transformation is
saved in raw_resources_candidate.json and passed before production changes.

## Complete candidate and commit

CreateStagingBuffer now returns its buffer instead of assigning the member.
CreateRawImages returns a RawResources candidate containing staging buffer,
images, views and zero-initialized slot ticks. All vectors and all image/view
pairs are prepared locally. Its declaration puts views after images, so every
view is destroyed before its image parents on prefix failure. The buffer outlives
those local vectors. The production member declaration order is unchanged.

RefreshResources checks existing Scheduler admission before allocation or the
cache fast path. This is a serialized-owner check, not an independent producer
lease. A closed direct entry must not create or publish a fresh candidate.
Unchanged width/height/stride/format still skips creation and synchronization.

For a changed request:

1. Build the entire unused candidate while old metadata, ticks, buffer, arrays and
   AA owner remain published and alive.
2. Complete the existing graphics/host-use prefix through WaitForResourceUses.
   This retains the internal-Finish and late-final-use protection from the AA fix.
3. Reset the completed old AA helper and swap the complete buffer/image/view/tick
   set. Publish width, height, stride and format after those swaps.
4. Destroy the old set now held by the local candidate, with views before images.

Static assertions require no-throw candidate move construction, Buffer swap and
all three vector swaps. Primitive metadata assignments cannot throw. The commit
therefore cannot leave a subset published after a C++ exception. These assertions
are compiled against the real vk::Buffer and real vectors in the Windows object
check. A local vector allocation error is also before publication; the controlled
fault matrix injects Vulkan/host failures at native factories, not every STL
allocator instruction.

A construction failure propagates its original error, releases only the unused
new prefix, and preserves the old publication exactly. It does not submit/drain
old work, reset AA or fabricate a successful refresh. Direct Layer retry starts
from that prior complete state and rebuilds the desired candidate; a cold retry
starts from empty state. This does not re-enable an applet Draw path already in
sticky Failed quarantine: the caller's partial-rendering policy remains separate.

Warm replacement still makes one existing FinishNativeBorrow submission only
after preparation succeeds. Cold creation makes no retirement submission. The
normal raw copy, mapping-use guard, descriptor logic and per-slot offset functions
are unchanged. New ticks are zero because the new slots have no prior uses.

The tradeoff is temporary old-plus-new native memory use. Allocation may fail
rather than first discarding the old set to reduce memory pressure; that preserves
the requested owner/retry invariant. Target-device memory/performance evidence is
still required.

## Failed waits and shutdown

The candidate has not been recorded to the GPU. Allocation failure can therefore
unwind it without native waiting. If the old-use wait fails after candidate
construction, existing terminal fail-stop retains both old owners and the new
candidate rather than unwinding uncertain use. No prefix is retried as no-effects
success.

After an allocation failure, shutdown keeps the unchanged old set and AA owner
through closed worker/join and successful checked idle. The existing terminal
native retirement permits ReleaseRawImages to skip cached current ticks; it never
reopens Scheduler or submits during destruction. Failed idle retains old owners;
only the unused candidate prefix was legitimately released before shutdown began.
The new admission check rejects cold and warm closed creation. This slice does not
make arbitrary concurrent Renderer/Layer access safe.

## Deterministic evidence and limits

Candidate and fixed suites each pass 33 cases in both cold and warm phases (66
paths), four terminal cases and six exact controls, all three repetitions. The
matrix includes staging, image0/view0, image1/view1 and image2/view2 Vulkan and
host failures; real semaphore gates at all seven factories; individual retries;
repeated mixed failures; stride change; complete reuse; unchanged fast path; and
shutdown after a partial candidate failed. Terminal cases cover both closed
phases, old-use wait failure and failed shutdown idle.

Warm fixtures retain pending uses in staging and every old image/view. Snapshots
compare owner identities, all metadata, ticks and AA identity before and after
failure. Gate observers inspect members only while the owner std::jthread is
paused before a factory returns/throws, so no read overlaps commit. Prefix probes
require no live candidate leak and reject image destruction before view cleanup.
Successful use checks every slot's parent pair and fills mapped staging spans
using actual Layer size/offset functions. This is a controlled size/backing
oracle, not actual guest copying or Vulkan pixel evidence.

Controls publish metadata early, resize member images during preparation, omit
view commit, omit old-use completion, omit stride from the key or retain old
ticks. Each must fail at its named runtime assertion. The old-tick control leaves
fixture setup intact and mutates only the actual replacement; an invalid setup
crash is not accepted as evidence. Terminal children require exit 86 and no owner
destruction after their terminal boundary or atexit.

Actual extracted methods: RefreshResources, CreateStagingBuffer, CreateRawImages,
the RawResources declaration, CalculateBufferSize/GetRawImageOffset,
WaitForResourceUses, ClearRawImages/ReleaseRawImages/destructor. Allocation and
native wrappers, GetFormat/bytes-per-pixel, pending-use completion and terminal
Scheduler/idle collaborators are fixtures. This suite does not execute real VMA,
native GPU queues, guest Memory/GetPointerImpl or a game.

Regressions passed: AA retirement (25 normal/24 terminal/ten controls), applet
content quarantine/actual shutdown and terminal release (21 normal/nine terminal/
six controls), mapping-use (ten protocol/seventeen raw paths/ten controls), native
source (26 paths/seven protocol/seven terminal/fourteen controls), and actual
renderer/worker shutdown (ten paths). Repetition counts and fixture boundaries
are recorded in their manifests. The AA suite uses a candidate-factory collaborator
for its replacement audit; the new raw suite executes the actual factories.

Three real Windows GCC 13.2 AMD64 COFF production units compile with zero
diagnostics using pinned real headers and cached Boost: Layer, BlitScreen and
WindowAdaptPass. A real glslang-generated presentation shader is used. No app
link, MSVC, local Android build, Linux/Android/native device/game or current-head
CI acceptance is claimed. The integration workflow includes the fixed suite but
was not manually dispatched here.

See [results, hashes, commands and cleanup](M4_P2_RAW_RESOURCE_PUBLICATION_RESULTS.json).
Current-run temporary executables/objects/generated fixtures are removed after
verification; logs/JSON remain in .diagnostics/m4-raw-results. Ignored local logs
do not travel by Git; the committed summary records inputs and resume commands.

## Continuation boundary

This repairs the selected raw tuple publication/retry and stride sizing gap.
Content initialization, raw upload/native completion and later descriptor/AA
construction failures remain separate from allocation success. Whole helper
recreation, arbitrary native escapes and all guest coherency/producer obligations
are not closed by a complete raw tuple. Preserve the failed applet quarantine.

HostMemory remap/protection remains high risk. M4 incomplete, F2/F3 open and
GuestBackingAdmission Unknown.


Follow-up: [raw upload recording eligibility and retry](M4_P2_RAW_UPLOAD_RETRY.md)
adds per-slot failed-prefix quarantine and generation-scoped reset after the old
use boundary. Allocation completeness and Recorded remain distinct from initialized
pixels or checked native completion; applet Failed isolation is preserved.
