# M4 P2: shared host1x ioctl mapping closure

Date: 2026-10-23 (Asia/Singapore). Baseline: `f140fd8390`.

## Resource disposition (before implementation)

MapBuffer and UnmapBuffer are independent of Submit. They must participate in
the SAME Kernel domain so a stopped Submit receipt cannot coexist with a new
mapping/pin ioctl. None of these three methods calls another of the three;
there is no nested admission that could reject a suffix already admitted by
an outer operation. Direct NvMap callers remain separate.

| State | Disposition |
| --- | --- |
| Closure before MapBuffer/UnmapBuffer admission | Busy before pin/unpin or local output mutation |
| Admitted operation | Count through all pin/unpin calls and local output updates |
| Pin prefix succeeds, later Pin returns zero | Preserve prefix state/output; seal before entry exit; InsufficientMemory does not authorize drainage |
| First Pin returns zero | Also seal: PinHandle may have changed mappings/reservations before refusal |
| Pin/Unpin throws before or after partial effects | Seal before entry exit; no speculative rollback or fabricated completion |
| Empty or normally completed batch | Normal entry exit; shared closure may drain this ioctl subset |
| Healthy no-effects cancellation | Existing cohort ResumeBatch reopens all three ioctl methods together |

This is an ioctl publication/drain subset, not complete mapping freeze or
durable backing/resource ownership. NvMap FreeHandle/PinHandle/UnpinHandle
called elsewhere, direct GMMU operations and outer HLE response writes remain
independent; ordinary GuestBackingAdmission remains Unknown.

## Implementation

Both mapping ioctl methods enter Host1xSubmitDomain before inspecting or changing
their batch. This intentionally reuses the existing shared Submit closure and
cohort ResumeBatch; it adds no new domain or nested gate inside NvMap. Closed
admission returns Busy before local output updates. Ordinary entry contention
uses EnterCold's retry behavior rather than silently dropping work.

Each method marks possible effects BEFORE PinHandle/UnpinHandle and retains
admission through all local map-address/entry/params updates. An exit guard seals
the shared domain before the counted entry unwinds unless the whole batch has
returned normally. Prefix pin/unpin state is not speculatively rolled back.
An empty batch completes without sealing. Existing batch truncation to the
available entry span and existing successful-return semantics are unchanged.

The zero-Pin case is deliberately conservative, including the first element:
the current PinHandle can reserve an address, remove an unmap-queue entry or
change device mappings before a later low-area mapping fails. Its zero return
cannot distinguish those effects from a clean refusal. This can quarantine an
address space for a failure which might eventually be recoverable; no local
recovery protocol is asserted here.

## Executed local validation

```powershell
python tools/physical_coherency_tests/run_host1x_submit_drain_tests.py --repeat 3 --output-dir .diagnostics/m4-host1x-map-cycle
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/core/hle/service/nvdrv/devices/nvhost_nvdec_common.cpp --source src/core/hle/service/nvdrv/devices/nvhost_vic.cpp --source src/core/hle/service/nvdrv/devices/nvhost_nvdec.cpp --source src/core/hle/service/nvdrv/core/nvmap.cpp --output-dir .diagnostics/m4-host1x-map-production
```

The existing runner now extracts complete actual MapBuffer and UnmapBuffer
bodies and ioctl structs alongside Submit, SliceVectors and WriteVectors. It
uses the real ProducerDrain and ScopeGuard; nvmap pin/unpin and GPU/Memory
collaborators are fixtures, not actual mappings or VIC execution.

Three positive repetitions cover shared closed admission for all three methods;
unchanged output on Busy; normal map/unmap and empty batches; counted in-flight
Pin/Unpin; local output completion; partial prefix/suffix preservation; zero on
the first or second Pin; and exceptions on the first or second Pin/Unpin. A
closure sealed during an admitted operation remains Unknown after its successful
return, and a concurrent Submit/Map/Unmap remains refused by the same closure.

All sixteen exact controls hit their intended runtime assertions: the previous
six Submit controls plus ten for missing Map/Unmap admission, early release,
missing seals, effect markers after Pin/Unpin, and falsely completing after a
successful prefix. The existing cohort/ResumeBatch code is unchanged and its
previous recorded run is not counted as a fresh execution in this round.

Four full Windows production units (common ioctl, VIC, NVDEC and nvmap) compile
to AMD64 COFF objects without diagnostics. Source hashes, object digests and
machine signatures are checked before cleanup. This round's generated fixtures,
manifests, binaries, objects and logs are deleted. Reproducible test source and
Markdown conclusions remain; the existing workflow already calls the expanded
runner. No workflow is dispatched here.

## Limits and next boundary

The underlying nvmap methods are still callable by FreeHandle, other pinning
paths, eviction or teardown outside these ioctl methods. Closing this subset
does not freeze every SMMU/GMMU mapping or retain every alias/backing owner.
It also does not retain session/nvmap/device objects after partial failure;
complete durable Unknown owner retention remains a separate prerequisite.

In particular, normal UnpinHandle return may only enqueue a retained handle for
later unmap. Neither callback return nor an empty producer count proves native
GPU completion, visibility, or permission to free backing. Outer ioctl/HLE
response copying and direct public GPU/CDma/Host1x entry points remain separate.
Actual malformed-input validation, device Busy/recovery behavior and runtime
performance require their own work and are not inferred from fixtures.

P2 and M4 remain incomplete. NCE/fastmem, raw aliases, independent Audio/Opus
workers, GPU writers and the final real Guest-backed admission still require
closure/ownership proofs. No Android build, full application link, native
VIC/NVDEC/Vulkan acceptance, main merge or remote CI pass is claimed.
