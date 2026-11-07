# M4 physical mapping / alias exclusion and refusal checkpoint

Date: 2026-10-28 (Asia/Taipei). PR #375, `codex/issue-372-fixes`.
Initial base: `b6de86d4efcb70dff966cd920275751822e16ae8`.
Audited head: `9e4b3f101d99fe865efa097c9809f0ecca3f158b`. The incoming texture-cache-only commit
is preserved; all validation inputs remain hash-identical.
[Commands, binary/source hashes, logs and cleanup](M4_PHYSICAL_ALIAS_REFUSAL_RESULTS.json).
CI deferred. Main and other worktrees are untouched. No production code changes.

## What is now demonstrated

The new test links the complete actual HostMemory/common implementation against
native Windows mapping APIs. It creates two virtual mappings of the same physical
bytes plus the canonical view. A promise-controlled writer captures the old
canonical pointer and enters before the freeze attempt. It cannot write until
explicitly released, and an RAII join releases it even if a check throws.

| Path | Actual evidence | Result |
| --- | --- | --- |
| Production Windows freeze | Fresh explicitly managed arena still returns no mapping Freeze, before and after raw alias effects/remap. | Required refusal; no support auto-enrollment. |
| Queued old pointer | Explicit Protect(Read) and VirtualQuery establish a readonly alias. The paused writer then changes canonical bytes; both virtual aliases observe the value while the readonly alias stays PAGE_READONLY. | Readonly on one VA does not exclude writes through another VA. |
| Direct raw alias permission change | Direct VirtualProtect makes the readonly alias writable, then a raw store changes canonical bytes. | HostMemory entry guards cannot stop an external PTE/protection operation. |
| Saved VA after remap | After explicit metadata cancellation, Unmap/Map redirects the saved VA to a different physical offset. Byte writes affect that offset and leave original canonical bytes unchanged. | Saved virtual address identity is not immutable physical identity. |
| Physical admission | GuestBackingAdmission remains false throughout all these effects. | No physical/guest/GPU authority. |

## Metadata counterfactual: diagnostic only

Windows production does not enable the mapping ledger. A whole-translation-unit
test copy changes only its constructor support flag to true, solely to exercise
the wrapper guards on real OS mappings. It returns exit 42, distinctly from the
production refusal test's exit 0. It is not Windows freeze acceptance and no
such source change is committed.

While this metadata closure is closed, Map, Unmap, Protect, ClearBackingRegion,
PinPhysicalRange and EnableDirectMappedAddress must throw the exact closure
refusal before their implementation runs. Actual bytes and readonly protection
remain unchanged by these refused requests. Ready-range and retained-lifetime
queries remain allowed, and pointer escapes remain recorded. This is intentional
metadata behavior, not physical access isolation.

The queued raw writer and direct VirtualProtect still succeed while
MappingEntriesClosed remains true. The immutable metadata permission snapshot
still says Read while VirtualQuery reports PAGE_READWRITE. This demonstrates why
metadata closure cannot be promoted to physical freeze, even when its mutation
entry guards work. Duplicate cancellation refuses. Re-freeze after explicit
cancellation records the new remapped physical offset, but still grants no guest
admission.

Five whole-HostMemory counterfactuals independently bypass the Map, Unmap,
Protect, clear and physical-pin entry guard. Each must fail with exit 91 and its
specific `ALIAS FREEZE CHECK: closed-...-was-not-refused` marker. The Map control
bypasses only a nonzero-physical-offset request, preserving initial census
construction so it tests the closed ingress itself. A sixth control changes only
the physical-admission claim to true on the otherwise unsupported production
path; the real raw-write/PTE witnesses must precede exit 91 with
`unproved-physical-admission`. No timeout, signal or generic nonzero exit is a kill.
The unimplemented Windows direct-mapping body is never executed or crash-counted
as a negative control; its actual closed-entry refusal is checked in the
unmodified diagnostic copy.

## Failures diagnosed before acceptance

The first native run failed its assumed readonly preparation: Windows
HostMemory::Impl::Map currently uses PAGE_READWRITE independently of the requested
Read argument. This is confirmed with VirtualQuery (protection 4) and the complete
implementation. The test now records this observation and explicitly calls
Protect(Read), then verifies actual PAGE_READONLY before testing alias exclusion.
It does not silently treat a requested permission as a PTE proof. Production Map
semantics are left unchanged; the unsupported Windows ledger already refuses and
no physical capability is being added. Any future Windows admission would need
to prove actual mapping/protection behavior as well as alias exclusion.

An initial Map counterfactual removed census construction too, and therefore
failed at `metadata-closure-census` rather than the intended closed-entry marker.
That is retained as a failed test-design attempt, not an accepted control. The
final control targets the later nonzero-offset request and produces the exact
closed-entry failure. The result record retains both attempts.

## Windows results and remaining gate

Fresh alias suite: 24/24 processes across three repeats: production refusal (3),
metadata-only diagnostics (3), five entry-guard counterfactuals (15) and false
admission (3). Every production/metadata diagnostic run requires all three byte/
PTE/remap witnesses and complete native teardown. Existing real Windows mapping
owner positives also pass x3, including moved-from getters, retained allocation,
replacement epoch invalidation and last-owner backing/VA release. Two related
production files (HostMemory and DeviceMemory) compile independently with strict
Windows g++ flags into AMD64 COFF objects. The new native common build starts
fresh; no kernel-type/logger stubs or Vulkan compatibility copy are used.

The synchronized KPageTable reservation and Vulkan buffer/texture changes are
preserved unchanged. Full native System/Kernel owner execution is not rerun here;
its prior result remains historical, and the synchronized KPageTable dependency
means it must not be advertised as a new current-head owner acceptance. No Linux
VMA, Android, Vulkan/GPU runtime, target game/device or CI result is claimed.

P0 physical freeze remains Open: old pointers and raw alias/PTE operations are
not excluded. The safe result is refusal/Unknown, not a successful physical lease.
Ordinary DeviceMemory remains untracked. Native GPU completion and the first
restricted guest-backed Vulkan Copy remain blocked by their prerequisites. The
next implementation must prove a complete alias/producer closure or refuse; this
checkpoint does not expand controlled owner combinations or global teardown.

Cleanup: 108 temporary binary/fixture files (295,219,541 bytes)
were verified by size/SHA256 and removed. No such leaves remain in this task
directory. Logs, manifests and commands remain; existing dependencies and earlier
checkpoint evidence were preserved.
