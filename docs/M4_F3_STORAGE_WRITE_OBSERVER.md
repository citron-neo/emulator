# M4 F3: opt-in ordinary storage write observation

The subsequent [immutable physical provenance slice](M4_F3_IMMUTABLE_PHYSICAL_PROVENANCE.md)
adds allocation-origin identity and shared closed-scope coverage. The original
observer evidence below predates that extension; neither slice grants authority.

This extends the [isolated provenance slice](M4_F3_SUBMISSION_PROVENANCE.md)
with a bounded diagnostic observer on ordinary Vulkan Draw, all three indirect
Draw variants, and direct/indirect compute dispatch. Observation is disabled by
default. It does not enable a physical publisher, change Memory::Write/UMA routing,
or close F2/F3 or restricted eligibility.

## Capture and submission boundaries

On the GPU owner, SetControlledStorageWriteObservation enables the diagnostic
scope and GetControlledStorageWriteObservation retrieves its retained events.
The capture_valid flag describes coverage of writable storage buffer bindings
within this scope only. It is not a content, admission or eligibility certificate.
Earlier/later commands and other producer classes are outside that scope.

BeginStorageWriteObservation reserves bounded host metadata before the existing
cache critical section. The typed BindStorageBuffer overload captures the actual
descriptor's staged allocation identity, native byte interval, retained payload
owner and resource-local intent revision at binding time. Looking the resource up
after Configure would be unsafe: a later binding can merge or retire the earlier
representation. No physical mapping/admission or Vulkan allocation/wait is added
under the cache lock. EndStorageWriteObservation releases temporary capture owners
after that critical section; accepted Scheduler events keep their payloads.

Scratch/gather, direct/imported, raw handle and unavailable/overflowed captures
are explicitly incomplete. Read-only storage bindings produce no write event.
This observer covers storage buffers, not images, transform feedback, attachments,
uploads, scatter or all possible commands in the batch. It never selects a source
by comparing revisions from different allocations.

RecordObservedWrites reserves one CheckedWrite per captured footprint and one
CheckedSubmission for the existing execution context. Multiple resources/commands
in that context share the same submission object. It does not flush. The next
ordinary Flush/Finish, or explicit FlushChecked, submits that same context and
publishes its actual result. A new context gets a different identity. Numeric
ticks or submission identity ordering are not used to infer a latest writer.

The real worker callback marks Recorded only after emitting Draw/Dispatch. A
failed graphics pipeline returns false and eventually yields Skipped, even when
other commands in the batch completed. Recorded host commands remain Pending
until queue acceptance. Submitted requires queue success; Complete additionally
requires the existing checked timeline counter and zero-time host wait. Ordinary
submission and worker exceptions still propagate and leave evidence Unknown.
Unknown is sticky; historical Complete does not certify current content.

For shaders, the captured interval is the descriptor's conservative writable
access range, not a measurement of individual stores. A completed Draw/Dispatch
may take a branch that leaves some or all bytes untouched. Its event proves that
the declared producer command executed; it does not publish a per-byte content
version or prove that every captured resource was changed.

The observer is limited to 64 footprints per command and 256 accepted records per
Scheduler retirement interval. Capacity/metadata/unsupported capture refusal marks
coverage incomplete and still records the original rendering command exactly once.
The disabled path retains the original templated Scheduler::Record call and avoids
observer callable/vector allocations. There is no automatic event eviction.

## Ownership and exclusions

Accepted events keep actual native payloads until the existing successful-device-
idle retirement boundary. Device idle alone cannot retire an observed batch still
waiting for host submission. As before, pins do not own VkDevice/VMA lifetimes;
failed-idle whole-device teardown remains an exclusion. This bounded diagnostic
mode is not suitable for automatic continuous gameplay enrollment.

No GuestBackingEpoch/physical geometry is published here. A completed event after
remap still describes the old allocation; it cannot make the replacement mapping
authoritative. Physical latest-writer authority requires immutable backing owners,
real synchronization relations and revalidation at publication. It must not choose
the largest revision or submission ID.

## Validation

The actual-body fixture extracts ordinary binding capture, the Rasterizer recorder,
the failed-pipeline direct/three indirect Draw workers, both dispatch workers and
Scheduler submission/polling. It checks resource/range/intent capture, multiple
resources, same-batch identity, no per-command flush, skipped draws, worker versus
queue/device progress, ordinary submit exceptions, unsubmitted payload retention,
and disabled/scratch/raw/capacity refusal with single command execution. Pipeline
state, descriptor dispatch and Vulkan/device execution there are fixtures.

The native [f3-provenance] process additionally uses actual ordinary staged cache
allocations and two barrier-ordered Vulkan fills in one observed batch, ordinary
Flush/Finish, checked completion and readback. A subsequent batch has a different
identity, and the independent replica remains stale. These are real transfer
commands, not an actual shader/game execution test. The original remap, skipped
fill, delayed worker, F2/F3 negatives and isolated alias regressions remain intact.

Remote full Linux/Android validation is required for this production change.
Android compilation runs through GitHub workflow; no local Android build is used.

## Verification record

Production/test source `be3847045e` passed the complete Linux/Android
[run 38989337199](https://github.com/Splaser/emulator/actions/runs/38989337199).
Its Linux job passed all five CTest processes: 130 ordinary cases, 1708 original
scheduler assertions, 38 original F3 assertions, 88 isolated alias assertions and
70 provenance assertions. Three additional provenance processes each passed all
70 assertions, with the original F3/alias repetitions also passing. F3 fixed mode
still exits 42 with exactly two known alias failures (36 passing assertions);
F2 still reports backing 77/cache 10 and its fixed-mode negative remains required.
Every configured Linux HostMemory/import, coherency/lifecycle and mutation gate
passed, including the new actual-body storage observer fixture.

The complete Windows actual-body pipeline/mutation suite also passed on the same
source, recorded in `.diagnostics/m4-storage-observer-final.log`. This is fixture
evidence, separate from native Vulkan acceptance. Exact Linux source stamp, native
process logs, generated fixtures and extraction hashes are retained under
`.diagnostics/ci-38989337199/` alongside full job output and Android configuration/
source artifacts. Android compiled the actual arm64 core, video and shader
libraries with CPM/NDK 29, including the new ordinary capture/recording code. This
is native-library compilation, not APK or target-device/game execution. The final
documentation follow-up changes no production/test sources or workflow; it does
not require repeating the same validation.
