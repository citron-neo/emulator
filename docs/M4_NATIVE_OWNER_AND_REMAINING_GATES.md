# M4 remaining gates: native owners before mapping and GPU admission

Date: 2026-10-26 (Asia/Taipei). PR #375, `codex/issue-372-fixes`.
Baseline: `874bbe70c60dd2ad7984f89ae94469d5b9eada59`.
CI remains deferred. This task writes only the issue checkout, not main.
[Commands, source hashes, runtime logs and cleanup](M4_NATIVE_OWNER_RESULTS.json).

The 2026-10-27 synchronization follow-up replaces the removed Null renderer only
inside the private cold owner bootstrap. The historical results below retain
their original source context; see [the current mapping/owner checkpoint](M4_WINDOWS_MAPPING_OWNER.md)
for the refusal backend and fresh native revalidation.

## Required order

| Priority | Gate | Acceptance |
| --- | --- | --- |
| P0 | Restricted real KProcess / KEvent / System owners | Real kernel linkage and runtime; remove external roots while retained work remains. Kernel children and cohort borrows must own their foundation without test-scope System/window variables. Final native child release precedes System retirement. |
| P0 | Physical backing / mapping / alias freeze | Old pointers, remaps and raw aliases cannot bypass the continuous closure. Missing census, escaped writable capabilities, unsupported mappings or unproved producer exclusion refuse. |
| P1 | Native GPU completion with retained backing | Retain every dependent owner through checked native completion and content visibility, including failed/unknown completion. Backend acknowledgement or host callback return is insufficient. |
| P1 | First restricted guest-backed Vulkan Copy | Exact success, refusal, partial-failure and terminal-disposal evidence, using the preceding capabilities. No ordinary BufferCache routing or content authority follows from a fixture result. |

Do not add more variants of the already-passed controlled NVDRV/Host1x/cohort/GPU
composition. Those tests remain regression prerequisites. The restricted cold
native combination below is now accepted locally. Adopting existing active
processes, writable mapping/alias exclusion and native GPU completion remain
unproved. F2/F3 remain Open; GuestBackingAdmission remains Unknown.

## Restricted native owner component

`RestrictedProcessOwner` owns a private, cold headless System and its window,
real KernelCore, KProcess and KEvent. It exposes no raw System, Kernel, process,
event, backing pointer, renderer or GPU handle. Leases retain the same foundation;
the cohort wrapper destroys its actual ProcessCohortStop before releasing that
foundation. Removing the root seals new admission only when explicitly requested;
existing leases continue to own their native dependencies.

Final retirement closes the readable event and KEvent, then the original process
reference, shuts down the System, and finally destroys the window. All operations
are restricted to the creating orchestration thread. Foreign operations refuse;
the thread identity is a non-reused serial rather than a recyclable OS thread ID.
Foreign final foundation/cohort release terminates before native dependency
destruction. A host thread with native Kernel TLS history refuses creation,
including after that Kernel has retired. This records the existing TLS limitation;
it does not change the existing frontend/kernel teardown contract.

The private bootstrap uses the actual Null renderer and GPU ThreadManager with
fresh kernel scheduling, without services, guest execution, Vulkan submission or
mapping export. This is a restricted native lifetime component, not adoption of
an existing running guest. Repeated cohort cancellation refuses without invoking
the underlying cancellation a second time. Neither BackendsStopped nor successful
cancellation grants mapping or GPU admission.

The new native executable links the complete `core` target. Its positive test
removes root and lease while the actual cohort alone holds the foundation, then
requires full retirement after cohort release. It also checks real event/process
reference counts, signal/clear, missing leases, closed admission, existing TLS and
foreign operation refusal. Two isolated terminal processes require exit 86 and
the exact foreign-release stage/reason. Build errors, crashes and timeouts cannot
pass those controls. A full-link counterfactual drops the foundation after native
cohort cancellation; the positive schedule must detect early retirement with its
specific exit-91 observation before touching the potentially invalid Kernel.
The counterfactual is emitted into diagnostics and never edits production source.
All three repeats detect exactly that missing-foundation condition.

Separate native processes also remove the external roots from an Unknown cohort
and an unpolled cohort. These are cold lifetime tests: the closed API has not
started a guest, exported memory, issued I/O or submitted GPU work. They do not
authorize disposal of an Unknown GPU submission or an existing active guest.

The next mapping gate cannot adopt this System's existing DeviceMemory buffer:
ordinary construction is untracked, and DeviceMemory::GetPointer exposes the
canonical backing pointer. Constructor-only mapping enrollment must remain
constructor-only. Retaining the allocation or stopping the cohort does not
revoke any already-issued pointer or prove all aliases enrolled. This owner
component therefore exposes no freeze capability or guest-backed Copy path.

## Windows validation

The complete real `core` and both native executables link with MSVC
19.51.36260 on Windows 11. There are no replacement System, Kernel, KProcess or
KEvent types and no extracted production method bodies in these executables.

| Native process | Repeats | Required result |
| --- | --- | --- |
| Positive owner/cancellation/refusal schedule | 3 | Exit 0; 35 Catch assertions per repeat |
| Actual quarantined Unknown cohort | 3 | Exit 0; 8 assertions per repeat |
| Unpolled closed cohort | 3 | Exit 0; 5 assertions per repeat |
| Foreign final owner release | 3 | Exit 86; exact `restricted_owner.release / wrong_thread` |
| Foreign cohort release | 3 | Exit 86; exact `restricted_owner.cohort_release / wrong_thread` |
| Missing-foundation full-link counterfactual | 3 | Exit 91; exact early-foundation-retirement observation |

All 18 native processes meet their required results. The Unknown schedule invokes
the actual closure's Quarantine operation. Zero or oversized poll budgets are
separately refused by the actual KProcess API. The observer retained by each test
contains only an atomic retirement phase; it cannot keep the System alive.

The original controlled nvhost owner suite also passes one regression repeat:
16 normal schedules, its terminal cancellation-failure control and nine exact
assertion mutants. This retains its previous fixture scope. The final
`restricted_process_owner.cpp`, `core.cpp` and `kernel.cpp` independently compile
to three real Windows AMD64 COFF objects with g++ 13.2 and real dependency headers.

Native build setup uses the exact vcpkg baseline
`c3173be258001c60814b6adf161322b6eb3688ee` and unchanged submodule gitlinks.
ENet and SimpleIni were initialized at their existing pins. The interrupted first
dependency build was resumed; a local PowerShell 7.6.8 to 7.6.9 update invalidated
its vcpkg ABI cache, requiring a dependency rebuild.

Two existing build issues are handled only by the generated local acceptance
overlay: remove a later `/std:c++20` override to honor the root C++23 setting;
disable C4242/C4244 only in `vk_rasterizer.cpp` for the existing
`make_shared<const u8>(0)` marker. No tracked global build rule or Vulkan source
is changed. Core/owner/kernel/test diagnostics remain strict. This is native Null
owner acceptance, not strict full-application or Vulkan runtime acceptance.

The new owner header initially failed under MSVC because inline move construction
instantiated an actual closure destructor with an incomplete KProcess. Defining
that constructor in the implementation file fixes the confirmed header issue;
the standalone consumer and both complete native executables then compile.

Reproduction uses the configure/build scripts retained verbatim in the result
manifest. Generate the local overlay and counterfactual first:

```text
python tools/physical_coherency_tests/prepare_native_restricted_owner_control.py --output-dir .diagnostics/m4-native-owner/control
```

After configuring the real core with that `CMAKE_PROJECT_INCLUDE`, build
`restricted_process_owner_tests` and `restricted_owner_missing_foundation`.
Run the committed `run_native_restricted_owner_tests.py` with both executables,
the installed dependency DLL directory, an output directory and `--repeat 3`.
The result manifest contains the exact executed commands and compiler flags.

Temporary test executables, objects, PCH/PDB/static libraries and generated
fixture sources are removed after hashing. Native/controlled logs, manifests and
configuration records remain; dependency tool/download/installed caches remain
available for the next gate. CI is deferred and no Android build was run.
