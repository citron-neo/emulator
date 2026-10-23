# M4-F3 HostMemory alias ledger and mapping-entry freeze

This is the next bounded component after
[managed producer drainage](M4_F3_MANAGED_PRODUCER_DRAIN.md), implementing part of
the [guest exclusion plan](M4_F3_GUEST_EXCLUSION_FEASIBILITY_AUDIT.md). It is not a
guest-backed exclusion provider. No caller currently combines these two components
into GPU admission; guest backing remains Unknown and F2/F3 remain Open.

## Actual enrollment and ownership boundary

Fresh controlled arenas explicitly opt in with `managed_mapping_entries=true` at
HostMemory construction, rather than adopting a caller's alias list. The default
is **off**: ordinary guest arenas allocate no mapping-domain state, enter no new
mapping mutex, record no escape history and always refuse freezing. They cannot
be retroactively enrolled. Core::DeviceMemory constructors leave the flag off,
including the NCE path; the current legacy fault chain must not acquire this lock.

An enrolled arena owns one HostMappingDomain. Successful Map, Unmap and Protect calls
maintain ordered physical-offset/virtual-offset/requested-permission intervals, including
both complements of partial changes. Canonical backing is a separate whole-arena
alias in the snapshot; lazy reserved bytes are not a claim of materialized storage.
An interval snapshot contains no mutable address, borrowed fd or backing pin.
Permission flags retain the request, not an OS-effective permission query or a
restore certificate: Execute handling is platform/build dependent. The separate
real read-only store-fault control checks OS enforcement. A future protection
lease must establish saved effective intersections independently before restoration.

Raw canonical/virtual pointer access, physical pin creation, chunk-lease queries
and driver-fd publication record permanent escape-family history on this owner.
These bits are diagnostic ownership facts, not a count of live borrowers, an
exhaustive owner census, or proof that an owner joined a write-use protocol. In
particular, escaped writers are not enrolled or revoked, and pointer/fd/lease
queries can still occur during a mapping freeze. Escape history is queried live;
the frozen alias table must never certify ownership based on a past zero mask.
GuestBackingAdmission always refuses, even with no observed escape history.

The first snapshot is restricted to ordinary internally owned Linux backing and
4096-byte host pages. External fds, exporters/driver backing, direct-address mode
and unsupported platforms refuse. A fixed 64-interval capacity is a refusal
limit: overflow permanently invalidates the census rather than publishing a
truncated alias list. Ordinary operations can continue after census invalidation.
Even this complete HostMemory table does not enumerate CPU PTEs in other processes,
GMMU/DAddr aliases, external OS mappings, services, plugins or renderer imports.

## Real mapping closure

The cold mutation transaction spans each actual HostMemory syscall/publication:
Map, Unmap, Protect, ClearBackingRegion, EnsureBackingRange, PinPhysicalRange and
EnableDirectMappedAddress. Nested materialization uses Impl directly within that
same transaction. Default-off ordinary arenas use a no-lock/no-ledger transaction;
there is no new mapping lock in their legacy fault chain. Explicit enrollment is
only for fresh controlled arenas, never for a live NCE/JIT guest. Protection
failure propagates even in assertion-disabled builds
and leaves the census incomplete instead of publishing successful permissions.

Already-published ready chunks retain the existing lock-free materialization-query
path (and cache-hit telemetry); they cannot be replaced within a live arena.
EnsureBackingRange joins the mapping transaction only for cold publication, so an
ordinary ready-range pointer lookup does not acquire a new global mutex or copy a
shared owner on every access. A ready-range query can still succeed while frozen;
like raw pointer getters, that grants no owner/write exclusion.

TryFreezeMappingEntries uses a single try-lock and refuses an in-progress operation,
an incomplete census, another freeze or a retired owner. On success it closes new
mapping entry under the same mutex that covers the complete actual mutation and
captures the table. Subsequent managed mutations throw before any mapping or
backing effect. The freeze retains metadata only, not the OS arena or GPU resources.
It changes no page permissions itself and supplies no CPU write exclusion.

The freeze is move-only and used by one orchestration thread with stable HostMemory
lifecycle. Explicit CancelNoEffects releases a known CPU-only mapping closure.
An unfinished drop seals mutation entry; stale cancellation cannot release a newer
freeze. Destruction/move replacement invalidates old-owner queries. Object move
construction transfers the same domain. There is no submission attachment or
release-after-GPU API; this token must not be used for asynchronous effects.

Do not freeze an ordinary running guest's HostMemory: its higher-level CPU PTE
updates can occur before the HostMemory wrapper, and other mapping domains are
unclosed. A refused inner call cannot roll back those earlier publications.
The next integration must close those outer entry points before their first
effect, retain all relevant lifetimes and drain writers without holding PTE/cache
locks. This component is exercised through dedicated test arenas only.

## Verification and current evidence

Run the focused checks with:

```sh
python tools/physical_coherency_tests/run_host_mapping_freeze_tests.py
```

On non-Linux hosts, use `--unit-only --cxx clang++`. The actual-domain tests cover
an independent 8192-byte-observation oracle over 256 alternating Map/Unmap/Protect
prefixes, physical offsets and permission complements, active-publication refusal,
stale release, early drop, retirement, moves, incomplete publication and capacity.
Eleven assertion mutants challenge those boundaries, including forced legacy
enrollment. These are primitive tests, not
native guest execution or OS mapping acceptance.

The Linux-only suite compiles production HostMemory with real memfd/mmap/mprotect
and threads. Only unused logging/assert formatting is stubbed. It checks canonical
backing plus two actual physical aliases, refuses seven managed entry families
before effects, verifies a refused unprotect leaves a real store fault in a child
process, then demonstrates that a retained writable alias can still store 99.
The latter is an ownership counterexample and must keep guest admission Unknown.
Remap/cancel/drop/retirement/direct-address/external-fd controls are included.
Seven further assertion mutants bypass the actual production wrapper guards;
another changes the real HostMemory constructor default to enroll ordinary arenas.
Both positive executables repeat three times; compiler failures/timeouts/signals
without assertion diagnostics do not count as killed mutants.

Local macOS: three unit repeats and all eleven primitive assertion mutants passed.
Final production Linux syscalls, seven wrapper/one constructor mutants and full
regression acceptance passed in the exact-source run recorded below.
The existing Linux workflow runs the new suite and archives its
logs/source hashes under `.diagnostics/m4-host-mapping-freeze/`. No Android
compilation or ARM/NCE execution is requested or claimed.

The first final-source Linux-only run
[39187373524](https://github.com/Splaser/emulator/actions/runs/39187373524) targets
exact source `465f626f7b55372eade5896fc28276a8239d4c33`, comprising initial component
`56676e3bd9`, moved-from compatibility/early validation `05acd60221` and ready-query
hot-path preservation `465f626f7b`. Its primitive checks passed, but the production
fixture failed to link because the harness put the production include directory
before its logging/assert stubs. This is a harness failure, not runtime acceptance.
The include order is corrected for the next run; Android was skipped.
Superseded runs `39187093671` and `39187016054` were cancelled and must
not supply acceptance for this source. Read the first actual failure/result and
verify the artifact hashes, three positive repeats, ten primitive/seven wrapper
assertion mutants and original F2/F3 controls before closing this gate.

Historical Linux-only run
[39201797569](https://github.com/Splaser/emulator/actions/runs/39201797569) targets
`cc409319a18286f0c602ae47fab5b2dfdd57ca71`, including the corrected harness include
order. Its focused mapping suite has passed: three real-domain/production repeats,
the injected NDEBUG protection-failure control and all ten primitive/seven actual
wrapper assertion mutants, early native integration compilation and existing
synchronization/content boundary checks. It was cancelled before complete build/
regression acceptance when source review identified the legacy NCE fault-chain
risk from default-on tracking. The final component instead requires constructor-
only explicit enrollment; its new defaults/negatives need a fresh Linux-only run.
Android was skipped. These earlier focused results do not accept the final source
or certify full native guest exclusion.

Current Linux-only run
[39205308506](https://github.com/Splaser/emulator/actions/runs/39205308506) targets
exact constructor-only enrollment source
`a080cc5a2ac61263c5014b94cf25836bd87ddb50`. Its Linux production job passed in full;
Android was skipped. Independent artifact verification confirms:

- All eleven primitive, seven wrapper and one public-constructor assertion mutants
  failed assertions as required (nineteen total); no compiler failures/timeouts
  were counted as successful negatives.
- Primitive and real production mapping tests each passed three repeats. The
  injected NDEBUG protection-failure control passed three repeats. Real read-only
  protection, refused mutation effects and the retained raw-store counterexample
  are exercised; guest admission stays Unknown.
- Native integration translation units and CLI/shader/tests compiled. All five
  CTest processes, three additional native provenance repeats (447 assertions
  each), backing/import and all configured coherency/lifecycle regressions passed.
- F3 still reproduces stale aliases; require-fixed has exactly two alias failures,
  36 passing assertions and exit 42. F2 remains backing77/cache10 with the expected
  failing fixed-mode control. Neither defect is claimed fixed.
- Artifact `12166179272` matches its published ZIP digest. All 114 SHA-256 entries
  in eight manifests match Git at this source and the resumed checkout's source
  files (subsequent changes are documentation only).

Artifact, source stamp, full job log, independent verifier and acceptance index are
retained locally under `.diagnostics/ci-39205308506/`. This closes acceptance for
the fresh controlled HostMemory component only. Do not reuse the cancelled
default-on run's results or infer full guest owner enrollment, NCE execution,
asynchronous GPU isolation or content authority from this acceptance.

## Remaining gates

The subsequent [controlled owner lifecycle](M4_F3_CONTROLLED_OWNER_LIFECYCLE.md)
substage creates a closed CPU-only backing cohort and composes its owned entry
drain with this same HostMappingDomain. Its Linux-only acceptance is verified at
`a36bae493c` in [39212903431](https://github.com/Splaser/emulator/actions/runs/39212903431):
eleven owner and nineteen mapping assertion mutants, three real owner/mapping
repeats, all five CTest processes (467 provenance assertions, also in each of three
extra native processes), native GPU-progress/metrics controls and all configured
regressions passed. Android was skipped. Artifact `12170850326` has a verified ZIP
digest and 125 source hashes across nine manifests; exact metrics and scope are
recorded in the linked owner document. F2/F3 defect controls remain unchanged.
This accepts that fresh CPU-only owner component only. It deliberately
stops before outer CPU PTE publication: real PageTable/process/backend/service/GPU
owners are not enrolled, and ordinary guest admission remains Unknown.

1. Enroll lifetime-scoped owner/borrow use, or stop/join the complete audited owner
   set. Raw-pointer observation alone cannot account for outstanding uses.
2. Close CPU PTE, device/GPU mapping, permission restoration and import/export
   publication before their first effect, composing them with this HostMemory
   closure. No higher-level rollback is inferred from an inner refusal.
3. Join audio/VIC/I/O, all-process CPU execution and old GPU work; prove the
   lock/dependency graph and retain physical/native lifetimes.
4. Freeze every alias and preserve saved permission intersections through checked
   GPU completion and retained Unknown isolation. Neither this token nor the
   managed drain offers a guest-wide recovery/resume endpoint.

Planner/Copy, ordinary BufferCache routing and physical content authority remain
unchanged. No new automatic rendering path is enabled.
