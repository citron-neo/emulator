# Terminal NV channel/address-space binding ownership

Date: 2026-10-22 (Asia/Taipei).
Baseline: `72555967d162e7546eee67c631021dd5538f9a52` plus this implementation.
Scope: one terminal producer-entry family within phase two; no M4 phase completed.

## Behavior

The actual nvhost_as_gpu::BindChannel obtains GPU::AcquireChannelBinding's owned
Entry before fd lookup or service locks. A closed GPU refuses with InvalidState
before changing the channel's shared GMMU owner. An entered call may finish after
Close; GPU teardown joins it before renderer shutdown. The Entry outlives fd/device
lookup, lock waits, shared-owner replacement, old-owner retirement and method
return acknowledgement. Facade, Module and service object entry lifetime remain
external obligations, including refused callers.

The address-space mutex protects the GMMU snapshot against AllocAsEx. The target
channel's existing mutex serializes replacement against Setup/Submit. Both locks
are acquired together with scoped_lock; they are released before retiring the
previous shared owner. That retirement can acquire MappingLifetime's mutex and
must not run under the service locks. No backend/scheduler call is added here.

Healthy pre-Start binding and the existing nullable pre-initialization assignment
are preserved. This does not prove that replacing an initialized engine's GMMU is
safe: existing engines retain references to their original manager. Native engine
rebinding/alias retirement remains a separate unresolved path, not an admission
permission granted by this Entry. Ioctl output copying beyond this method and
Module concurrent-client/escaped-device lifetimes are not joined by this change.

## Windows verification

Local environment: MinGW-w64 UCRT GCC 13.2.0, Python 3.14.6.

```powershell
$env:PYTHONUTF8='1'
python tools/gpu_lifecycle_tests/run_owner_publication_tests.py --repeat 3 --output-dir .diagnostics/m4-channel-binding-windows-20261022-verified
python tools/gpu_lifecycle_tests/run_thread_tests.py --repeat 3 --handoff-controls --output-dir .diagnostics/m4-binding-thread-windows-20261022
```

PASS: publication positives repeated three times; all 24 precise controls
passed (17 retained controls plus seven binding-specific controls). The lifecycle
prerequisite also passed all ten cases three times and its cancelled CPU-read
coverage control. Independent verification matched all 15 publication and nine
ThreadManager manifest source hashes to the current working tree. ThreadManager's
normalized-text source digest was checked separately from the raw file hashes.

The new acceptance test first failed against the baseline at the precise marker
`closed channel address-space binding published owner`, before production edits.
Positive coverage includes invalid fd, cold binding/null assignment, refused
post-Close replacement, teardown paused before lookup, at final acknowledgement
and while releasing the old owner, plus both service-lock waits held across Close.
Old-owner release additionally requires an independent thread to reacquire both
service locks. The prior address-space/init/creation controls remain required.

Seven binding-specific controls must reject an unrelated Entry domain, ignored
refusal, release before lookup, release before acknowledgement, retirement under
service locks, and either missing service lock. Each requires its exact failure
marker; a compiler error, timeout, crash without that marker or an earlier unrelated
assertion is not accepted.

These execute the real GPU Entry/forwarding/close/join and NV BindChannel method
bodies. GMMU construction/destruction, renderer, worker, engines, Module/System
lifetime and native completion remain fixtures. The retirement hook models the
last shared owner's destructor; it is not native device-resource retirement proof.

The independent scope witness now calls the actual MemoryManager::BindRasterizer
body through an externally retained GMMU after GPU Close, and verifies its pointer
assignment. It still bypasses the NV Entry family. This controlled bypass does not
establish shader/device execution or repair F2/F3; ordinary admission stays Unknown.

The ThreadManager regression command passed three repeats, its terminal controls
and the blocking-handoff negative topology control. Normal CPU-read/composite
callback and cancellation coverage also remain mandatory in the publication
suite's lifecycle prerequisite.

At the initial checkpoint, full production nvhost_as_gpu.cpp Windows syntax checking was attempted but
stopped at missing boost/unordered/unordered_flat_map.hpp. No full production
translation-unit/build, new Linux CI, Android build or native Vulkan acceptance is
claimed for this source. Earlier Linux run 39729352143 is green at d8b0c3897d,
not the new source. Generated temporary outputs are removed after inspection;
reproducible test sources and this report are retained.

## Remaining boundary

All three M4 phases remain unfinished. Reversible ProcessCohortStop linkage,
independent service/raw/mapping producers, queued/native GPU completion and a
durable Unknown isolation/retirement registry are still required. Main remains
unchanged until the requested final M7 zero-copy merge.

## Dependency follow-up

The missing Boost blocker is now resolved. All five real GPU/NV/channel translation
units compiled into Windows x64 COFF objects at production baseline c6034b8e45,
with zero diagnostics and matched source/object hashes. See
[the focused production compilation record](M4_F3_WINDOWS_PRODUCTION_VALIDATION.md).
This adds object-compilation evidence, not application linking or native execution.
