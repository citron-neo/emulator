# M4 P2: ordinary NVDRV Module dispatch closure

Date: 2026-10-23 (Asia/Singapore). Baseline: `f63ea5c87f`.

## Actual coverage

The common host1x ioctl gate alone does not stop nvhost-as-gpu or /dev/nvmap
ioctls from pinning, unpinning, freeing handles or triggering eviction.
Module Open, Ioctl1, Ioctl2, Ioctl3, Close and QueryEvent now enter one shared
Kernel-owned NvdrvDispatchDomain. Every Module constructor binds to that same
domain, which is independent of the inner Host1xSubmitDomain.

NVDRV::Initialize also enters the same domain before session publication. It
retains the entry through OpenSession, initialization-state publication and the
local success response. Closed admission returns Busy through ServiceError.
The previous unconditional success scope guard is removed: initialization which
throws now seals before entry exit and does not fabricate a Success response.
Success ResponseBuilder construction itself is deferred until initialization
returns normally, since its real constructor clears and prepares the IPC reply.
Already-initialized healthy calls still succeed without creating another session.

Admission happens before file/builder lookup or output mutation. Closed Open
returns INVALID_NVDRV_FD; the other five methods return Busy. An admitted entry
spans the full ordinary synchronous device callback, local response copying
inside device ioctl wrappers, event publication, and Close's map erasure/device
destruction when that removes the final shared owner. The caller must still
keep Module/Kernel/System alive; the count is not a replacement for those owners
or pre-existing service serialization of open_files.

An exception seals the domain via a scope guard before the producer entry
unwinds. Open exceptions retain any already-published device map entry; an
OnClose exception does not erase the file. No speculative rollback is added.
Ordinary returned device results are preserved, including NotImplemented and
other errors. They must not globally quarantine normal probing by games.
Their return is only proof that this synchronous call exited, NOT proof of no
partial/native effects. The narrower host1x transaction guards continue sealing
their known partial-effect failures. General native/Unknown ownership remains
an independent, unsatisfied gate even after all dispatch calls return.

## Composition and disposition

| State | Disposition |
| --- | --- |
| Closure wins before dispatch admission | Invalid fd/Busy; no builder/device call, output mutation or file erase |
| Already admitted builder/device callback | Keep count live through its return and local output/owner updates |
| Exception unwinds the dispatch | Seal before count exit; never grant a drained receipt |
| Ordinary validation or returned device error | Preserve result and normal count exit; no whole-device/no-effects authority |
| Closure sealed while an admitted call finishes | A normal old return cannot reopen it |
| Healthy no-effects cohort cancellation | All dispatch/inner/Audio/CPU/write closures reopen in the same ResumeBatch |

ProcessCohortStop closes and drains outer NVDRV dispatch AFTER backend stop and
BEFORE closing Audio, host1x and managed Memory domains. In particular, an
already admitted outer call can still enter its inner host1x/memory operations.
Closing the inner gate first could refuse a suffix of a previously admitted
outer operation. Poll is bounded and joins no service/native worker itself.
The new domain is rechecked on stopped Poll and included in quarantine and the
same atomic cancellation group. Four CPU cores plus all these members remain
within the primitive's sixteen-closure capacity.

## Executed local validation

```powershell
python tools/physical_coherency_tests/run_nvdrv_dispatch_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-nvdrv-dispatch-cycle
python tools/physical_coherency_tests/run_process_execution_stop_tests.py --output-dir .diagnostics/m4-nvdrv-dispatch-execution
python tools/physical_coherency_tests/run_process_cohort_stop_tests.py --prerequisite-dir .diagnostics/m4-nvdrv-dispatch-execution --output-dir .diagnostics/m4-nvdrv-dispatch-cohort
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/nvdrv/nvdrv.cpp --source src/core/hle/service/nvdrv/nvdrv_interface.cpp --source src/core/hle/kernel/kernel.cpp --source src/core/hle/kernel/process_cohort_stop.cpp --source src/core/hle/kernel/k_process.cpp --output-dir .diagnostics/m4-nvdrv-dispatch-production
```

The dispatch runner executes the complete extracted six production method bodies
with real ProducerDrain and ScopeGuard. Module member layout, device builders,
device calls, sessions and queued/native work are fixtures. Three repetitions
check all six closed/admitted/throwing paths, preserved returned errors, output
and event publication, builder drainage, final file-held device destruction,
and a Seal during each in-flight call. The actual Initialize and ServiceError
bodies additionally cover closed initialization, healthy resume/idempotence,
counted session publication and local reply, and throwing initialization without
a success response. Twenty-two exact controls include admission, early release
and exception sealing separately in each method, plus a negative control that
reintroduces fabricated Success on an initializing exception.

Execution-stop and cohort each pass three positive repetitions and five and
nineteen exact controls respectively (46 controls total across these suites).
New cohort cases verify outer-before-inner drainage, restored dispatch, late
Seal, and Seal during local resume or before atomic group commit. A dedicated
ordering mutant moves outer drainage after inner closure and must fail its
named assertion. The cohort runner now reads stubs from its actual prerequisite
directory rather than relying on old default-directory diagnostics; the final
run uses this corrected path.

Five complete production sources compile to Windows AMD64 COFF objects without
diagnostics. Source hashes, object digests and machine signatures are checked
before deleting this round's generated fixtures, binaries, objects, manifests
and logs. Markdown conclusions and runners remain. The existing Linux workflow
includes the new runner; no workflow is dispatched here.

## Limits

Follow-up: [GraphicBuffer/session retirement](M4_P2_GRAPHIC_SESSION_RETIREMENT.md)
now replaces the specific direct-destructor paths described below with retained
records and counted/deferred cleanup. Other escaped access and native/terminal
ownership gaps remain.

Module GetContainer/GetDevice return escaped access and are not lifetime-bound
by a dispatch entry. Direct Container session methods and the NVDRV destructor
remain outside normal dispatch. SurfaceFlinger/GraphicBuffer directly pin,
unpin and free nvmap handles. Module/device destructors invoked by external
teardown are also outside a normal Close. Those routes need their own sustained
lifecycle/owner protocol, not a guard that silently skips destruction when closed.

The outer HLE response write occurs after Module return. Managed Memory facade
counting covers its supported stores, but full service pause and lifetime
composition are not established by this dispatch gate. Deferred GPU callbacks,
queues and native submissions require independent drainage/visibility proof.
The domain retains bookkeeping, not a durable whole-owner Unknown resource
bundle. See the [remaining producer audit](M4_P2_REMAINING_PRODUCER_AUDIT.md).

P2 and M4 remain incomplete. Ordinary GuestBackingAdmission remains Unknown.
No Android build, full application link, native device/Vulkan acceptance, main
merge or fresh remote CI success is claimed.
