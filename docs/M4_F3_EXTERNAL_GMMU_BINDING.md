# M4 phase two: terminal external GMMU rasterizer binding

Date: 2026-10-22. Branch: `codex/issue-372-fixes`, based on `714635b381`.
Main is not merged. This closes one phase-two publication bypass; all three M4
phases remain unfinished and GuestBackingAdmission remains Unknown.

## Failure first

Before changing production code, the actual-body publication suite called public
MemoryManager::BindRasterizer through an externally retained GMMU after GPU
NotifyShutdown. It compiled and failed precisely with
`FAIL closed external GMMU changed rasterizer`. The old method stored the raw
pointer without any terminal admission. A callback into the GPU cannot fix the
external-owner case because the GMMU can outlive that GPU.

## Ownership and ordering

RasterizerBindingState is a shared, detached terminal state constructed with GPU
Impl, before renderer setup. Its target is non-null, one-shot and immutable.
The GPU and enrolled GMMUs retain it. Public BindRasterizer now returns a boolean
confirmation for the already enrolled, identical target. It neither stores a raw
pointer nor calls the GPU. Null, different, unenrolled and post-Close calls refuse.
This avoids introducing another pointer store racing existing GMMU readers.

Only GPU's private AttachRasterizer path installs the first pointer. It serializes
first enrollment, refuses a different existing binding and publishes the pointer
before release-publishing the shared state. GPU::InitAddressSpace retains the
existing parent publication Entry through NV's final acknowledgement. A private
initialization admitted before Close may finish; it cannot reopen public binding.

Renderer bootstrap also retains that parent Entry and enrolls Host1x's GMMU.
Repeated renderer installation refuses without closing or replacing the healthy
owner. Failure after target publication closes the terminal domains. This does
not implement renderer/native-resource replacement or restart.

Shutdown closes both parent publications and the detached binding domain. GPU
teardown stops the emulation worker, joins parent publications, joins detached
bind entries, then shuts down the renderer. A public caller paused after acquiring
an Entry delays renderer destruction. A caller paused before entry holds only a
shared detached state: GPU destruction may complete, then the resumed call safely
refuses. An externally retained GMMU likewise refuses after GPU destruction.
ChannelState no longer repeats the raw GMMU setter: production NV has already
enrolled that owner through InitAddressSpace. Four native test fixtures now use
the current retained channel allocation and address-space enrollment APIs too.

## Deterministic Windows checks

Commands (run from this issue worktree, GCC 13.2.0 / Python 3.14.6):

```powershell
python tools/gpu_lifecycle_tests/run_owner_publication_tests.py --repeat 3 --output-dir .diagnostics/m4-external-gmmu-full
# To isolate the six new controls (all positives still run):
python tools/gpu_lifecycle_tests/run_owner_publication_tests.py --repeat 3 --control-prefix external- --control-prefix renderer- --output-dir .diagnostics/m4-external-gmmu-controls
python tools/gpu_lifecycle_tests/run_channel_rebind_tests.py --repeat 3 --output-dir .diagnostics/m4-external-gmmu-channels
python tools/physical_coherency_tests/run_windows_production_checks.py --output-dir .diagnostics/m4-external-gmmu-production
```

The actual extracted public/private binding, renderer bootstrap, GPU close/join,
NV publication and forwarding bodies execute against controlled renderer/worker,
allocator, engine and service fixtures. The detached binding header and terminal
counter are real. Event gates pause at the before-entry, entered-binding and
Host1x-enrollment boundaries. Bounded waits assert teardown is blocked where
required; joins and completed futures verify progress after release.

Positive cases repeat three times, including normal same-target confirmation,
wrong/null/unenrolled refusal, exception guard release, existing-owner refusal,
post-Close/post-GPU refusal, duplicate renderer refusal and bootstrap/teardown.
The suite retains all 24 prior publication mutation controls and adds six precise
controls for missing detached Close, missing Join, early binding Entry release,
wrong-target acknowledgement, replaced enrollment and early renderer Entry
release. Compilation failures, crashes and timeouts do not count as passing controls.
The independent channel suite repeats three times with nine ownership controls;
the ten lifecycle cases and cancelled-read control remain prerequisites.

Six complete production units generate AMD64 COFF objects with real project,
Boost and fmt headers: gpu.cpp, memory_manager.cpp, nvhost_as_gpu.cpp,
nvhost_gpu.cpp, nvdrv.cpp and channel_state.cpp. This is object compilation,
not application linking. The native controlled_cpu_scheduler fixture adaptations
are source changes only; no native Vulkan test execution is claimed.

The original 24 controls passed against `m4-external-gmmu-verified/publication.cpp`.
The six new controls are isolated with `--control-prefix external- --control-prefix
renderer-` in `m4-external-gmmu-controls`; positives still run three times. The two
positive fixture sources were byte-identical (SHA256
`04d6d0ab314fc3c34927e02a411e14a3646b78a54138b57951cd6cda2f66681e`).
An initial missing-Close control terminated on an open Join before its oracle;
that attempt was rejected as evidence. The repaired control runs its refusal
oracle first. It removes only Close and must fail with the named forbidden effect.
Likewise the wrong-target mutant retains parameter usage so `-Werror` cannot
substitute an unused-parameter compiler error for the runtime oracle.

All 30 publication controls (24 prior plus six new) and nine independent channel
controls hit their named runtime failures. Positives and the ten lifecycle cases
passed three repetitions; the cancelled-read control passed. All six production
objects had zero compiler diagnostics. The 38 input-hash entries from three
manifests (19 unique inputs), object digests and AMD64 signatures were independently
verified. The fixture source adaptations have separate source-only hashes.
See the committed [result summary](M4_F3_EXTERNAL_GMMU_RESULTS.json).

Detailed final logs, manifests and verification.json are retained locally in
`.diagnostics/m4-external-gmmu-results/`. Cleanup completed: 593 temporary files were removed, including all 132 generated
executables and six production objects from this turn. Only the final evidence
bundle remains populated; earlier dependency caches and unrelated runs are unchanged.

## Remaining boundary

This terminal cold binding protocol neither stops nor certifies ordinary GMMU
Read/Write/Map operations, escaped pointers, HostMemory VMA remapping, engine
setters, recording/submission/fencing/presentation or Vulkan completion. GMMU and
System/device facade lifetimes still require their callers' coordination.
Atomic shared_ptr use is cold-path synchronization, not a fault-safe admission
primitive. No reversible ProcessCohortStop, alias census, durable Unknown native
retirement, production F2 routing, Linux/Android/device acceptance or full app
link is inferred. Continue phase two with the remaining producer/mapping owners.
