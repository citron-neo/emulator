# M4 P2: session retirement closes escaped frame-source re-admission

Follow-up: [accelerated native source ownership](M4_P2_ACCELERATED_NATIVE_SOURCE.md)
connects the selected native pair through recording/split submissions and checked
resource completion. Generic producer/native output and guest admission remain separate.

2026-10-24 (Asia/Taipei). Baseline `a8daa2c75a4dd02475970ccb8b61f92edb72a341`;
branch `codex/issue-372-fixes`. Personal main stays
`b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`. No main merge or manual CI dispatch.

## Test-first defects

The previous mapping-use domain revoked old queued stamps, but did not permanently
close a retired session's source ingress. Actual GetDisplayMapping only locked its
handle and captured the current device generation. CloseSession's shutdown branch
leaves mappings alive; OpenSession's stale-PID branch intentionally retains old
resources. A retained GraphicBuffer/HwcLayer packet could consequently ask nvdisp
to resolve the same handle again and receive a fresh stamp.

Before production edits the new deterministic fixture reproduced three exact
exit-91 witnesses: shutdown source recapture, stale-session source recapture, and
a queued source stamp surviving stale-session detection. The baseline was repeated
against the pinned revision with the final fixtures. Neither a crash, timeout nor
an arbitrary nonzero exit qualifies as a witness.

There is a second identity boundary: an inactive ordinary session's numeric slot
can be reused. Active/ref_count checks on the slot alone would authorize handles
and late close/owner-retention requests belonging to its previous incarnation.

## Production protocol and linearization

SessionId now contains a numeric slot and a nonzero generation. The per-Container
counter is allocated under session_guard before ASID registration; duplicate opens
of a live session return the same complete identity. New or reused slots receive
a fresh identity. Zero remains unpublished. The last u64 generation is usable,
but subsequent opens refuse at zero after unsigned wrap, before registering a new
ASID. Failed initialization can consume a generation; it cannot reuse one.
No guest wire structure changes: this is the emulator's internal SessionId passed
through NVDRV device/session maps. Actual Handle::Alloc copies the complete ID.

GetDisplayMapping follows this order:

1. Retain the handle from its table and release the table lock.
2. Under the handle mutex, copy its allocated session identity and capture the
   existing device mapping stamp for its address/extent.
3. Release the handle mutex, then validate that session under session_guard.
   Validation requires matching nonzero generation, active/ref_count, a live
   retained backing owner, a Process and a matching Process PID.
4. Return the original stamp only when validation succeeds. Never recapture a
   newer device generation after validation.

If retirement completes before step 3, the source is refused even if its handle
and mapping remain alive. If validation wins first, later retirement invalidates
the returned **old** stamp before actual copy/cache selection, using the existing
mapping-use domain. A read which already owns that bounded domain finishes before
retirement enters. The new entry check holds no handle/table lock while waiting
on session_guard; retirement's session -> handle order therefore gains no inverse
edge. There is no session/handle lock across the renderer or GPU waits.

Final CloseSession retains its existing source-domain mutation before both the
ordinary and shutdown branches. The stale-PID path now also begins a source
mutation before marking the old session inactive; it leaves its mapper, handles,
ASID, backing and Process retention policy intact. A stale replacement gets a
different generation. This closes the actual frame-source re-admission route
without adding a second mutex to each source copy or changing scheduler dispatch.

Related identity consumers are checked as well: CloseSession refuses an old
generation before changing a replacement's ref count; RetainSession refuses old
generations and PID mismatches before opening a Process owner; UnmapAllHandles
filters by both slot and generation so replacement retirement cannot reclaim
intentionally retained old-incarnation handles. A nonfinal CloseSession reference
drop preserves both existing stamps and new captures for the still-live session.

## Failure and effects boundaries

Source refusal does not mean native completion, cleanup, unpin or guest-memory
publication. Shutdown and stale-session resources can remain retained after their
source ingress closes. Old handles are not relabeled to a new session, and no
generation-zero fallback looks up a replacement by slot alone.

Ordinary retirement can already have dropped its last reference or performed
unmap/mapper/ASID side effects when it fails. Its existing mutation failure remains
sticky, and ref_count zero prevents a fresh source capture; this change neither
replays retirement nor asserts rollback. Stale detection can retire the old source
before a later replacement registration/allocation fails. OpenSession is not made
an atomic no-effects transaction by this patch. Retained-owner/quarantine behavior
continues to rely on the existing lifecycle protocols.

PID mismatch refuses a new source/owner borrow even before OpenSession diagnoses
it as stale. Already captured stamps are revoked when the stale transition is
actually detected. This does not invent an asynchronous notification for a Process
identity change; the Process reference/lifetime contract remains a precondition.

## Deterministic Windows evidence

`run_session_source_tests.py` runs fifteen paths three times: normal source and
nvdisp publication; shutdown; stale replacement and source recapture; queued stale
stamp; duplicate-open/nonfinal/final close; reused slot with live replacement;
late old close; retirement before validation; retirement after validation before
return; active bounded use versus shutdown and stale detection; partial retirement
failure; mismatched PID; missing/unpublished/out-of-range sessions; generation
exhaustion after the last unique identity. The live replacement remains accepted.

Nine exact controls each run three times: move capture after validation; omit
validation; omit source incarnation; omit stale stamp revocation; omit close
incarnation; omit PID; lose the published generation; omit counter exhaustion;
hold the handle lock across validation. The last detects the lock-order defect
before a deadlock. Gates and observed real mutex waits choose the interleavings;
there are no sleeps used to decide whether an operation has completed.

Actual extracted bodies/layouts: SessionId, Handle and its constructor/Alloc,
GetHandle/GetDisplayMapping, OpenSession's admission/reuse prefix with its actual
final return and actual generation-counter initializer, complete CloseSession and
IsDisplaySessionActive, and complete nvdisp Composite conversion. Real packet,
HwcLayer, FramebufferConfig and FrameMappingDomain headers are used. Only test
observation is added around the validation/return and underlying std::mutex waits.

The escaped producer is represented by a retained valid HwcLayer/GraphicBuffer
packet reaching actual nvdisp. Session owner storage, Process ref primitives,
backing, mapper/device operations and GPU publication are explicit fixtures;
GraphicBuffer public construction/destruction is a stand-in in this suite. The
application heap preallocation optimization and real BufferQueue dispatch threads
are not executed here. No real Memory/GetPointerImpl, HostMemory remap, native
driver or game runs in this suite.

Regressions: source address eleven paths/seven controls three times (including
actual generation-filtered UnmapAllHandles); actual GraphicBuffer/session owner
retirement three times plus 24 exact controls once, with actual RetainSession old
generation/PID rejection; frame packet fourteen paths/eight controls three times;
mapping-use ten protocol paths/six controls and seventeen actual-body paths/four
controls three times, including actual CPU copy versus Unmap. These suites keep
their documented kernel/device/native fixture limits.

Six real Windows AMD64 COFF production units compile with GCC 13.2, pinned headers
and zero diagnostics: Container, core NvMap, ioctl nvmap, NVDRV Module/interface
and nvdisp. This covers generation publication, propagation through the ioctl
allocation caller, source capture and retirement caller APIs. It is object
compilation, not an application link or Windows Vulkan runtime test. No
Linux/Android/game/current-head CI acceptance is claimed. See
[manifests, hashes and cleanup](M4_P2_SESSION_SOURCE_RESULTS.json).

## Remaining scope / continuation

The queued stamp plus incarnation validation now closes this display-source route
through shutdown, stale-session diagnosis and numeric slot reuse. Source ownership,
mapping continuity, guest content coherence and native image use remain distinct.
The renderer's accelerated view still needs ownership through recording,
submission and presentation completion. Audit that handoff next; do not hold
session/handle/mapping/source locks across GPU waits or infer completion from
callback registration, ticks, Flush or a retained wrapper.

Generic raw GetSession and other GMMU/producer admission APIs are not converted to
incarnation-checked leases here. Production renderer publication uses nvdisp's
GetDisplayMapping path; the legacy numeric GetHandleAddress has no production
caller in the current inventory. A retained identity value is not a capability
to map, read arbitrary guest memory or dispose native resources. HostMemory
remap/protection remains a dedicated high-risk module. All M4 phases are still
incomplete; F2/F3 remain open and GuestBackingAdmission stays Unknown.

```powershell
$env:PYTHONUTF8='1'
$env:CXX='C:/Strawberry/c/bin/g++.exe'
python tools/gpu_lifecycle_tests/run_session_source_tests.py --baseline --revision a8daa2c75a --output-dir .diagnostics/m4-session-baseline
python tools/gpu_lifecycle_tests/run_session_source_tests.py --repeat 3 --output-dir .diagnostics/m4-session-fixed
python tools/gpu_lifecycle_tests/run_frame_address_tests.py --repeat 3 --output-dir .diagnostics/m4-session-address
python tools/physical_coherency_tests/run_nvdrv_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-session-retirement
python tools/gpu_lifecycle_tests/run_frame_source_tests.py --repeat 3 --output-dir .diagnostics/m4-session-packet
python tools/gpu_lifecycle_tests/run_frame_mapping_tests.py --repeat 3 --output-dir .diagnostics/m4-session-mapping
```

Results record the six-source Windows compile command. Current-run generated exe,
objects, fixture sources and headers are removed after input/COFF/output hashes
are checked. JSON/log copies remain under `.diagnostics/m4-session-results`.
