# M4 P2: synchronous host1x Submit closure

Date: 2026-10-23 (Asia/Singapore). Baseline: `11a92e5a09`.

## Transaction disposition (defined before implementation)

Follow-up: [shared mapping ioctl closure](M4_P2_HOST1X_MAPPING_CLOSURE.md) now
also counts MapBuffer and UnmapBuffer on this same domain. Direct nvmap/device
operations and complete alias freeze remain independent.

The actual VIC/NVDEC ioctl shares nvhost_nvdec_common::Submit. It reads guest
command buffers, advances syncpoint maxima and invokes GPU::PushCommandBuffer,
which currently calls CDmaPusher::ProcessEntries synchronously. The commented
asynchronous path is not active. A producer entry must span the whole Submit,
not only the final VIC WriteBlock calls.

| State | Disposition |
| --- | --- |
| Domain closed before admission | Return NvResult::Busy before Submit allocations, syncpoints, reads or dispatch; do not report accepted commands |
| Admitted; no effect boundary crossed; allocation/validation fails | Leave count normally; no new submission authority is granted |
| Syncpoint mutation or guest read/dispatch may have begun; suffix fails/throws | Seal domain before counted entry exits; retain Unknown; do not invent syncpoint completion or roll back partial work |
| All command buffers and local response serialization return normally | Leave count; closure may drain this synchronous Submit subset |
| Healthy no-effects cohort cancellation | Restore this domain in the same ResumeBatch as execution, Audio and managed writes |
| Seal after drainage or during cancellation | Reject stale stopped receipt; keep all grouped domains closed |

This boundary covers the shared ioctl Submit body only. Direct public GPU/CDma/
Host1x methods, outer ioctl response copies, mapping/alias changes, decoder worker
lifetimes and native GPU queues remain separate. It is not ordinary Guest
admission or a native device completion receipt.

## Implementation and composition

Kernel owns a shared Host1xSubmitDomain, which the common Submit body enters
before any local allocation, syncpoint advancement, command read or dispatch.
EnterCold retries ordinary count contention and explicitly returns Busy on
closed/unsupported admission. It does not pause a service while it may hold
unrelated locks. No refused submission is silently reported as successful.

The entry remains live through local WriteVectors calls. A scope guard, declared
after that entry, seals the domain before entry destruction whenever an effect
boundary has been crossed but Submit has not completed. The effect marker is
set BEFORE syncpoint mutation and BEFORE guest ReadBlock, since either can
throw after partial effects or invoke other code. A bad first handle with no
syncpoint increments leaves the domain usable; a bad suffix after a successful
command or failed syncpoint increment is Unknown. Allocation failures before
any marked effect do not unnecessarily seal the domain.

ProcessCohortStop now closes/drains this domain after the Audio subsets and
before managed Memory writes. Already admitted submissions can still finish
their managed writes. The closure is retained and rechecked after stopped Poll,
quarantined on Unknown, and included in the same ResumeBatch as execution,
initialization/publication, Audio and managed writes. Poll adds no blocking
decoder/device join. The primitive's fixed batch capacity still accommodates
the four CPU cores and all these closures.

## Executed local validation

```powershell
python tools/physical_coherency_tests/run_host1x_submit_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-host1x-cycle
python tools/physical_coherency_tests/run_process_execution_stop_tests.py --output-dir .diagnostics/m4-host1x-execution
python tools/physical_coherency_tests/run_process_cohort_stop_tests.py --prerequisite-dir .diagnostics/m4-host1x-execution --output-dir .diagnostics/m4-host1x-cohort
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/nvdrv/devices/nvhost_nvdec_common.cpp --source src/core/hle/service/nvdrv/devices/nvhost_vic.cpp --source src/core/hle/kernel/kernel.cpp --source src/core/hle/kernel/process_cohort_stop.cpp --source src/core/hle/kernel/k_process.cpp --output-dir .diagnostics/m4-host1x-production
```

The Submit suite runs extracted complete Submit, SliceVectors and WriteVectors
bodies, actual ioctl structs and the real ProducerDrain/ScopeGuard. A scheduling
annotation before the real response memcpy permits controlled drainage timing.
Kernel/Memory/nvmap/syncpoint/GPU collaborators are fixtures; no actual VIC pixel
conversion, decoder, ordinary guest backing or device runtime is executed.

Three positive repetitions check Busy with unchanged data and no dispatch or
syncpoint effect; healthy resume; counted guest read, dispatch and local response
serialization; no-effects validation failure; successful prefix/refused suffix;
and throwing syncpoint, guest-read and dispatch collaborators. Six exact mutants
hit their intended assertions: omitted admission, early entry release, absent
partial-effect seal, effect marking after syncpoint mutation or read, and early
completion marking before dispatch.

Execution-stop and cohort each pass three positive repetitions with five and
sixteen exact controls respectively (27 controls total across the three suites).
Cohort cases verify Submit drainage before Memory closure, restored admission,
late Seal and Seal during local resume or just before atomic group commit.

Five complete Windows production units compile to AMD64 COFF objects without
diagnostics. Source hashes, object digests and machine signatures are checked
before cleanup. This round's generated fixtures, manifests, logs, objects and
executables are removed; runners and Markdown evidence remain. The existing
Linux workflow includes the new test; no workflow is dispatched here.

## Remaining P2 acceptance gaps

This removes one actual service Submit gap, not every VIC/host1x entry. Mapping,
unmapping, pinning and external aliases remain independent. Public
GPU::PushCommandBuffer, CDmaPusher::ProcessEntries and Host1x::PushEntries are not
automatically gated when called outside the common ioctl. Outer WrapGeneric/HLE
response copies are outside this entry and need their own managed write/lifetime
composition. Converting the currently synchronous path to asynchronous dispatch
would require carrying ownership to checked completion instead of method return.

The domain retains bookkeeping, not nvmap objects, device/session lifetimes,
backing or mapping protection after a partial failure. Complete Unknown owner
retention still needs a separate owner bundle. No native GPU completion,
visibility or decoder-worker join follows from Submit return. NCE/fastmem,
escaped aliases, Opus and other Audio/service workers and queued GPU writers
remain unclosed. Busy retry/device behavior still needs actual runtime testing.

P2 and M4 remain incomplete; GuestBackingAdmission remains Unknown. No Android
build, full application link, native VIC/NVDEC/Vulkan acceptance, main merge or
fresh remote CI pass is claimed.
