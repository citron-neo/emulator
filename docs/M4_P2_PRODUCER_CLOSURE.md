# M4 P2 producer closure: managed writes joined with the process cohort

Date: 2026-10-23 (Asia/Singapore). Baseline: `1157bf0d04`.

## Phase status and ownership boundary

The [GraphicBuffer/session retirement update](M4_P2_GRAPHIC_SESSION_RETIREMENT.md)
replaces raw-provider destructor cleanup with prepared retained owners, counted
or deferred retirement and non-replayed Unknown records. Global native device/
allocator lifetime and trusted terminal quarantine disposal remain open.

The [ordinary NVDRV dispatch closure](M4_P2_NVDRV_DISPATCH_CLOSURE.md) now spans
Open, Ioctl1/2/3, Close and QueryEvent, including ordinary nvhost-as-gpu/nvmap
calls. NVDRV session Initialize shares that domain through its local reply.
The [remaining actual-route audit](M4_P2_REMAINING_PRODUCER_AUDIT.md)
identifies escaped GraphicBuffer/session/teardown and native ownership gaps.

The [shared host1x mapping ioctl closure](M4_P2_HOST1X_MAPPING_CLOSURE.md) extends
the Submit domain to MapBuffer/UnmapBuffer, including partial-failure sealing.
Direct nvmap/device mapping routes and complete alias ownership remain open.

The [synchronous host1x Submit closure](M4_P2_HOST1X_SUBMIT_CLOSURE.md) now counts
the common VIC/NVDEC ioctl, refuses closed submissions before effects and seals
partial failures. Other public device/mapping/alias/native routes remain open.

Further follow-up: [AudioManager callback closure](M4_P2_AUDIO_CALLBACK_CLOSURE.md)
also drains its input/output callback sweeps before managed writes close. Direct
service calls, sink/backend callbacks and other independent producers remain open.

Follow-up: [Audio render-cycle closure](M4_P2_AUDIO_RENDER_CLOSURE.md) now pauses
and drains the SystemManager-owned generation/DSP-response cycle before this
managed Memory domain closes. Other Audio/Opus/HLE/service producers remain open.

P2 is not complete. This change addresses the large ordinary Memory facade gap
instead of counting another fence repair as phase completion. The table below
is the remaining acceptance inventory, not a list of capabilities all granted by
one counter.

| Producer/owner family | Current treatment | Remaining acceptance gap |
| --- | --- | --- |
| Non-NCE Memory facade scalar, exclusive, bulk/unsafe, zero/copy, prepared/cached writes | 21 actual entry methods now use one Kernel-owned managed write domain shared across Memory views | Full real-process runtime validation and hot-path overhead; direct PreparedCpuPage/HostMemory APIs are not automatically covered |
| PhysicalCore / checked Dynarmic execution | Existing real-backend stop prerequisite, retained Process roster and continuous cold publication gates; managed write closure acquired only afterward | Complete integration with all fastmem stores and outstanding host continuations; local fixture is not native acceptance |
| NCE native stores / signal interpreter | Explicitly unsupported for this managed domain in HAS_NCE builds; domain sealed and closure unavailable | Safe native halt and signal-store protocol; no new signal waiting/exception/parking is added |
| Audio/services using ordinary Memory WriteBlockUnsafe etc. | Individual Memory calls count on non-NCE builds; SystemManager render cycles and AudioManager callback sweeps now have sustained pause/drain domains | Direct service calls, sink/backend callbacks, Opus and other worker/queued routes remain independent; these subsets do not stop whole services |
| VIC/host1x/device mappings | Shared VIC/NVDEC Submit/MapBuffer/UnmapBuffer domain drains admitted ioctl bodies and seals partial effects; direct nvmap/device/mapping APIs remain independent | Whole producer stop, decoder/device lifetime and complete mapping/alias ownership |
| Raw GetPointer/GetSpan, external maps, direct prepared-page stores | Explicitly outside this facade domain | Inventory and revoke/retain every escaped borrow; closure must span its actual use |
| GPU queued/recorded transfer and shader/image writers | Existing terminal ingress, checked callback/submission and fence failure repairs | A sustained reversible GPU stop and visibility proof across every writer |
| Unknown backing/alias/native resource owners | Scheduler retains checked holders; premature host-pending retirement fixed | Complete durable owner bundle including allocator/device and backing/alias protection, not fence handles alone |

Ordinary GuestBackingAdmission remains Unknown. Terminal shutdown and a managed
subset count are not a reversible whole-guest exclusion capability. No normal
guest admission or new GPU authority is enabled.

## Shared managed Memory admission

KernelCore owns a shared ProducerDrain domain. Every Memory constructor binds it
regardless of mapping tracking mode; a moved Memory keeps that same domain, and
Reset does not replace it. This prevents a different facade or newly constructed
view from bypassing an existing shared closure.

The following complete facade methods enter before touching Impl, resolving a
pointer, performing cache callbacks or storing bytes:

- Write8/16/32/64;
- WriteExclusive8/16/32/64/128;
- WriteBlock, WriteBlockUnsafe, CopyBlock and ZeroBlock;
- WritePrepared8/16/32/64 and WritePreparedCached8/16/32/64.

Their scoped entry lasts through the actual Impl call. Ordinary-store CAS
contention retries rather than dropping a store; closure/capacity refusal throws
the typed ManagedGuestWrites::Refused before effects for those APIs. The eight
prepared/cached facades instead use one nonwaiting TryEnter and return Busy on
refusal or contention, preserving their bounded-entry contract. Neither route
returns fake success or falls back to an untracked store.
This is a cold orchestration protocol: callers must not close it while arbitrary
service/JIT exception propagation is assumed safe. Cohort closes it only after
backend stop. Per-call entries do not themselves keep a Memory facade or its
PageTable alive; existing caller/Process/publication ownership is still required.

HAS_NCE requires different handling: its signal interpreter invokes these Memory
facades. The helper constructor seals the shared domain on that build and Enter
does no tracking/yield/exception work. Existing stores retain their behavior,
but no managed closure can be acquired. This is explicit unsupported-domain
refusal, not evidence that NCE writes are drained. The test exercises this compile
branch; no ARM/NCE runtime is claimed.

## Cohort close, drain, cancellation and failure

ProcessCohortStop first retains its existing creation/init/publication closures
and obtains real backend-stop acknowledgement. Only then does it close the managed
write domain. Active admitted calls keep Poll Pending. It returns BackendsStopped
only after this additional managed subset drains; that result still grants no
Guest admission. Poll is bounded and invokes no new wait, GPU callback or service
join. A late Seal is rechecked even after a successful stop.

No-effects cancellation adds managed writes to the SAME ResumeBatch as execution,
initialization and publication; local reopening remains invisible until the shared
commit. Quarantine seals an acquired managed write closure too. A timeout/Unknown cannot later become
a successful resume merely because a delayed writer exits. The existing process
creation cancellation reservation remains in force.

Outstanding service writes can depend on stopped execution or other owners. This
patch does not assert their bounded completion: pending dependencies exhaust the
budget and fail closed. Service-specific close-and-join is still required before
claiming P2, as is a caller protocol for any refused service operation.

## Executed Windows validation

```powershell
python tools/physical_coherency_tests/run_managed_guest_write_tests.py --repeat 3 --output-dir .diagnostics/m4-managed-writes-final
python tools/physical_coherency_tests/run_process_execution_stop_tests.py --output-dir .diagnostics/m4-managed-execution
python tools/physical_coherency_tests/run_process_cohort_stop_tests.py --prerequisite-dir .diagnostics/m4-managed-execution --output-dir .diagnostics/m4-managed-cohort
python tools/physical_coherency_tests/run_first_registration_tests.py --repeat 3 --boost-include C:/Users/N/.cache/citron-deps/boost_1_87_0 --output-dir .diagnostics/m4-managed-first-registration
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/memory.cpp --source src/core/hle/kernel/kernel.cpp --source src/core/hle/kernel/process_cohort_stop.cpp --source src/core/hle/kernel/k_process.cpp --output-dir .diagnostics/m4-managed-writes-production
```

The Memory suite executes all 21 full facade bodies plus the real managed-domain
and ProducerDrain code, with fixture Impl effects. Three repetitions cover healthy
effects; closed refusal across existing and late views; retained entry at every
method; exception unwind in fixture Impl; retry/resume; and 8,000 contended stores
without drops. All 21 individually omitted-admission mutants hit the exact
closed-before-Impl assertion. The HAS_NCE compile branch preserves stores while
refusing closure. Fixture construction shares the domain explicitly; actual Kernel
wiring is compiled and source-audited, not executed against a complete System.

The actual process/core stop and cohort body suites pass their three positive
repetitions and precise mutants. New cases hold a managed writer across backend
stop, refuse new entries, drain after exit, resume with the group, and reject a
late Seal. New controls omit managed drainage or batch membership. Existing
cross-domain Seal/cancellation controls remain present. Their sequential-release
mutant was extended to reopen managed writes sequentially too, preserving its
intended atomicity witness rather than failing only on a missing new member.

The existing real HostMemory/PageTable first-registration suite also passes three
positive repetitions and its three exact negative controls. Its extraction fixture
now gives the actual ordinary Memory facades an explicitly open managed domain;
it does not certify Kernel wiring. Original disabled-handshake F2 still reports
backing77/cache10, so enrollment has not erased the independent counterexample.

Four complete production units generate Windows AMD64 COFF objects without
diagnostics. The first attempt to compile KProcess stopped at a missing Dynarmic
include path; the helper now includes the pinned dynarmic/src headers, and the
complete rerun passes. Initial fixture indentation warnings and earlier unrelated
mutant failures were corrected, not counted as evidence. Final manifests/input
hashes and object digests/signatures are verified before cleanup.

Final verification matched 76 input-hash records across five manifests and all
four AMD64 object digests/signatures. There are 39 exact negative controls across
the managed Memory (21), execution (5), cohort (10) and first-registration (3)
suites, in addition to the explicit NCE unsupported-domain branch check.

These are actual-body simulations and focused production compilation. Real
Dynarmic/NCE execution, Android/runtime overhead and whole-service/native device
drainage require separate acceptance. No remote workflow, Android local build,
native Vulkan run, full application link or main merge is claimed. Only Markdown
conclusions and source remain; owned temporary executables, objects, generated
fixtures, manifests and logs are removed. Earlier diagnostic directories remain.

## What must happen before P2 can be called complete

1. Real stop/join facts for NCE/unchecked fastmem, Audio/VIC and every other
   independent worker; safe rejection/acknowledgement and dependency ordering.
2. Complete raw/escaped alias inventory and actual borrow lifetime closure.
3. Sustained GPU producer exclusion with checked native completion/visibility and
   a durable backing/alias/native owner bundle on Unknown.
4. Composition with the Process cohort that preserves pause state and cannot
   reopen any domain through a failed/old cancellation or completion.

The first legal ordinary Guest-backed Copy remains P3 and stays blocked until
those P2 facts exist. The remaining work is cross-domain architecture and runtime
proof, not a list of additional bookkeeping patches.
