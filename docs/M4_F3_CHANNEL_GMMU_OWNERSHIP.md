# Preserve the GMMU referenced by initialized channel engines

Date: 2026-10-22 (Asia/Taipei). Baseline: `d40b0009c0`.
Scope: channel owner publication within M4 phase two; not a completed phase.

## Failure and resulting behavior

The actual ChannelState::Init constructs the DMA pusher and engines using
references to *memory_manager. Changing the channel's shared_ptr afterward does
not rebind those references. The sole NV slot publication site previously allowed
that replacement after successful or partially effected initialization. It could
remove the last shared owner while engines still referred to that manager.

BindChannel now checks initialization state and GMMU identity under the existing
address-space/channel lock pair. A different manager, including null, is rejected
with InvalidState once initialized or initialization_failed is set. The check and
replacement are serialized with actual Setup/Submit. Idempotent binding requires both the same manager address and equivalent shared
ownership (neither control block orders before the other). Equal addresses with
another control block, or equal ownership with another address, are rejected.
A permitted idempotent binding does not rebuild engines, clear failure isolation
or enable retry.
Before initialization, replacement/null binding and the normal deferred Setup ->
address-space bind sequence retain their behavior. The terminal Entry, lookup,
lock-wait and acknowledgement scope from c6034b8e45 is preserved. The old owner
still retires outside both service locks, while Entry remains retained.

This deliberately refuses an unsafe transition; no native engine stop/rebuild
protocol is introduced. Backing identity, real GPU completion, alias freeze and
resource retirement remain separate from channel-ready metadata.

## Executed Windows checks

The new actual-body test first failed against d40b0009c0's production source at
`ready channel rebound its GMMU`, before the new production guard was added.
An additional red control then exposed equal-address/different-owner replacement
before ownership equivalence was added. Both were real assertion failures after
successful fixture compilation, not accepted compiler failures.

```powershell
$env:PYTHONUTF8='1'
python tools/gpu_lifecycle_tests/run_channel_rebind_tests.py --repeat 3
python tools/gpu_lifecycle_tests/run_owner_publication_tests.py --repeat 3
python tools/physical_coherency_tests/run_windows_production_checks.py
```

Windows MinGW-w64 UCRT GCC 13.2.0 / Python 3.14.6:

- Three new positive repetitions passed. Actual ChannelState Init/BindRasterizer,
  GPU InitChannel/AcquireChannelBinding, NV BindChannel/SetupGPFIFOChannel execute.
- Fixture engine constructors retain the actual supplied MemoryManager reference.
  Mid-construction failure leaves a real partial prefix from the production Init
  body; refusal preserves its sole shared manager owner and sticky failure state.
- Ready and partial initialization each reject different and null managers.
  Same-owner binding preserves flags and construction count; partial initialization
  still refuses replay. Deferred Setup before binding remains successful.
- Latches pause the real Setup path in backend initialization. Concurrent binding
  waits on the same channel mutex and observes the completed ready/failure state.
  Terminal Close/Join waits for both entered operations to settle.
- Nine precise controls reject omitted ready/partial checks, blanket same-owner
  rejection, omitted transition refusal, ready/null bypass, cleared failure state,
  equal-address/wrong-owner and equal-owner/wrong-address aliases, and bypassed
  initialization serialization. Compiler failures and timeouts do
  not count as successful controls.

The affected publication suite also passed three positive repetitions and all
24 retained precise controls. Its ten lifecycle prerequisite cases passed three
repeats and the cancelled CPU-read coverage control passed. The independent
post-Close external GMMU publication witness still succeeds, as required.

All five complete production units from the Windows helper generated AMD64 COFF
objects after this guard was added, with zero compiler diagnostics. This is real
Windows object compilation using complete Boost 1.87 and pinned fmt headers,
not another syntax-only check or a fixture compilation. It does not link or run
the application. The earlier initial-object record remains scoped to c6034b8e45;
the new source has a separate manifest and independent verification below.

Independent verification matched 34 source-hash entries across the new owner
test (8), retained publication suite (15) and production-object helper (11),
covering 17 unique source inputs. All five object digests and AMD64 COFF machine
signatures matched; every production compilation log was empty. New owner tests
passed three repetitions and all nine precise mutation controls.

The new test is registered alongside publication tests in the existing Linux
workflow for future runs. No new remote workflow or Android build was dispatched.
The unchanged ThreadManager suite was already validated in the prior checkpoint;
this round reruns the affected publication suite and new engine-owner tests.

## Limits and next work

Engine, renderer, System, Module/fd and syncpoint providers remain fixtures. This
proves the production construction/owner-selection control flow, not execution
of real DMA/shader commands or device backing bytes. Existing channel destruction
order, escaped raw aliases, output copies, other mapping/service producers and
native retirement are not certified. The independent post-Close external GMMU
BindRasterizer scope witness remains required and succeeds.

Phase one still needs complete continuous owner/execution drainage. Phase two
still needs all independent producer close/join facts and a durable Unknown
isolation/retirement registry. Phase three remains blocked on those facts before
any real guest-backed Vulkan admission. All three phases remain unfinished;
F2/F3 remain Open and GuestBackingAdmission stays Unknown. Main is unchanged.
Automatic approval rejected the attempted current-run temporary-directory cleanup
with only "blocked by policy" and no further reason. Temporary fixtures, objects
and logs therefore remain in ignored .diagnostics directories. The shared Boost
dependency cache, reproducible helpers and this report are retained.
