# M4-F3 controlled outer CPU mapping publication

This is a constructor-enrolled, CPU-only component for one `Memory` object.
Ordinary process Memory instances remain `MappingPublicationMode::Untracked`.
**F2/F3 remain Open; ordinary guest admission remains Unknown.**

## Boundary implemented

The earlier HostMemory ledger closes its own Map/Unmap/Protect calls, but the
actual `Memory::Impl::MapMemoryRegion` and `UnmapRegion` publish PageTable entries
before calling HostMemory. Debug/cache permission changes have the opposite
ordering: VMA protection precedes their PageTable tag updates. Closing an inner
HostMemory entry cannot cover that complete outer operation.

`ControlledMappingPublication` counts the six actual public Memory entries:

| Entry | Range held by the outer entry |
| --- | --- |
| SetCurrentPageTable | Context epoch, active PageTable, fastmem pointer and backing-pointer publication |
| MapMemoryRegion | Backing materialization, actual MapPages, then HostMemory Map |
| UnmapRegion | Actual MapPages, then HostMemory Unmap |
| ProtectRegion | PTE classification and every selected HostMemory Protect |
| RasterizerMarkRegionCached | HostMemory Protect through final PageTable tag publication |
| MarkRegionDebug | HostMemory Protect through final PageTable tag publication |

Entry is acquired before accessing Impl or any context/PTE/VMA state. It stays
counted until the final publication and execution-lock release. A closed entry
throws before calling the existing mutator; it never silently continues or
reports a successful mutation. TryClose closes admission and polls the existing
ProducerDrain; it does not wait, invoke GPU work or hold a PageTable mutex.

The entry's shared lifetime lock is separate from metadata locks. Retire seals
admission and takes its exclusive counterpart before Impl destruction or transfer.
Reset, Finalize, Memory destruction and move invoke that join; process finalization
also calls it before its PTE teardown. Callers must invoke retirement outside
context/PTE/HostMemory/cache locks and never reentrantly from a publication.
The existing six Impl bodies do not gain a callback, GPU handoff or GPU wait.
Their own locks and publication order are preserved.

A successful operation calls Finish. Unwinding or abandoning an entered operation
seals this publication domain as Unknown, including when a PTE has changed but
the corresponding VMA operation has not finished. This does not roll back partial
effects or make the backing inaccessible. The caller must retain its external
owners and stop using a failed controlled session; no automatic recovery is issued.
Cancellation reopens only a known drained CPU subset without such a failure.

## Enrollment, lifetime and remaining blockers

Enrollment occurs only when constructing a fresh independent Memory with the
explicit Controlled mode. No API can adopt an already used ordinary instance.
Ordinary instances have no new allocated domain, counter operations or mapping
mutex; their entry scope is empty. NCE/fastmem/fault paths stay in that ordinary
mode. No process creation code opts into Controlled mode.

Retirement is terminal for an enrolled instance, including across Reset and move.
Reset may construct a new Impl, but cannot reopen the old publication identity.
A new controlled session requires a new Memory. The old tokens remain retired.
Untracked instances retain their existing Reset and move behavior.

Initial API entry still requires externally retained Memory/System/PageTable
objects. The state token is not an object-lifetime pin. This component joins only
entered mapping publications; it does not enroll ordinary reads/stores, raw
PageTable entry writes or Resize/destruction, another Memory sharing the table,
escaped pointers, service writers, frame allocation, device/GPU mappings or queued
GPU work. It does not serialize simultaneous mutators beyond their existing locks.
No all-process publication closure, ARM/NCE stop, all-alias freeze, backing/native
pin, GPU isolation obligation, content authority or guest lease is created.

In particular, retiring an enrolled instance refuses its normal mapping API.
This is not a general process-finalization protocol: normal teardown callbacks
must not be routed through a retired enrolled instance. All actual KProcess
instances remain untracked; guest integration must first design the outer
process/PTE teardown and owner protocol. Do not enable this flag on a running guest.

## Validation contract

`run_mapping_publication_tests.py` extracts the actual six public wrappers and
their actual Impl bodies. HostMemory, PageTable and syscalls are real. System and
KProcess collaborators are fixtures; injected observations pause only the test.

- Three positive Linux repeats exercise all six closed-entry refusals before
  effects, then an in-flight pause across closure with early-resume refusal.
- Map's pause sees the actual new physical PTE while its actual old shared VMA
  still reads 10; after release the VMA reads the new backing value 55. This
  specifically detects a readiness probe released before outer publication ends.
- An injected exception after actual PTE publication seals the domain and rejects
  subsequent mutation. Retirement, a retained actual entry lock and default-off
  behavior have separate controls.
- An omitted-owner counterexample uses a second untracked Memory wrapper to
  change the same real PageTable/VMA while the first domain is drained. Its
  guest admission still returns Unknown. Per-Memory drainage is not table coverage.
- Eleven assertion mutants omit individual wrapper guards, shorten the scope,
  remove partial-publication sealing, remove retirement sealing/joining, or force
  ordinary enrollment. Compiler failures, timeout and unrelated crashes do not
  count as successful mutant rejection.

The native scheduler provenance process separately constructs a real controlled
Memory against real System/process objects, verifies the six public pre-effect
refusals and default-off process behavior, then holds its actual entry through
the actual Memory::Reset join. Holding that entry is privileged test orchestration,
not a game mapping or ARM/NCE witness. The original F2/F3 controls and full existing
Linux regression suite remain required.

Local Windows compilation of the production fixture succeeded. Windows runtime
does not permit this Linux remap sequence; that failed local exploratory run is
not acceptance. The script now explicitly requires Linux for runtime checks and
provides --compile-only for a compiler probe. Linux remote acceptance is pending.
This iteration requests linux-only scope; no Android build or device test is run.

Pending remote run:
[39380436500](https://github.com/Splaser/emulator/actions/runs/39380436500) at
`3576ea32bd`. Acceptance has not yet been recorded; do not duplicate the run.

The next roadmap gate remains process/PTE ownership and all relevant host/service
publication closure with retained lifetimes, followed by device/GPU enrollment
and retained Unknown isolation. This component alone cannot satisfy that gate.

## Verified Linux acceptance

[39380436500](https://github.com/Splaser/emulator/actions/runs/39380436500) passed
the complete Linux job `118488091047` at exact source
`3576ea32bdea1513b2afdbd47226a712a3bf768a`. Android was skipped.

Independent verification downloaded artifact `12246651887` and matched its
published ZIP SHA-256
`8d809c7c9f3c90e161c84d2eb27b158b468f3f1c912bde033ddf8d2a21c62b19`.
Its source stamp and all **139** source-hash entries across **11** manifests
match Git at that exact source and current HEAD for the files those manifests
cover. The later Unmap change is outside this acceptance; it has a separate run.

- Eleven new publication assertion mutants reached their intended assertions;
  all three real Linux PTE/VMA fixture repeats passed, including the split-map,
  partial-publication sealing, actual entry-lock retirement and omitted-owner controls.
- All five CTest processes passed. Three additional native provenance processes
  each passed **499** assertions and the actual constructor/six-refusal/Reset-join
  marker, as well as the previous CPU-preparation lifetime marker.
- Complete native production builds, existing mapping/owner/synchronization/content
  controls and all configured backing/coherency/lifecycle regressions passed.
- The original F2 still prints `F2 expected=77 observed=10 backing=77`; its fixed
  mode remains the required failure. F3's fixed mode retains exactly two alias
  failures and exit 42. No ordinary F2/F3 repair is claimed.

Downloaded metadata, ZIP, artifact, full Linux job log, verifier and acceptance
index are retained under ignored `.diagnostics/ci-39380436500/`. This accepts
only the constructor-enrolled per-Memory CPU publication component.

The next [Unmap reference-lifetime follow-up](M4_F3_PROCESS_UNMAP_REFERENCE_LIFETIME.md)
addresses an exception cleanup hazard in the higher-level process mapping caller.
Its source `74aea98af7` requires independent Linux acceptance. Neither component
enables an ordinary process freeze or guest admission.
