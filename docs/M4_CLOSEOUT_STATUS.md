# M4 closeout status and replacement-owner verification

Date: 2026-10-27 (Asia/Taipei). PR #375, `codex/issue-372-fixes`.
Validation base: `4fa1ad8e0d9f36340bcfe3486e875afb60a1f1cd`.
[Exact evidence and cleanup](M4_CLOSEOUT_STATUS_RESULTS.json).
CI remains deferred; main and the FFR worktree are not changed.

The 2026-10-28 [alias/PTE refusal checkpoint](M4_PHYSICAL_ALIAS_REFUSAL.md) adds
fresh physical-bypass witnesses and distinguishes the synced KPageTable dependency
from the historical native-owner evidence below. This table retains its dated
2026-10-27 validation context; it is not a current-head full owner rerun.

## Current gates

| Gate | Verified status | Remaining acceptance |
| --- | --- | --- |
| Restricted real KProcess / KEvent / System owner foundation | Prior native 21-process result retains identical source inputs, including positive/Unknown/unpolled, foreign final-release and missing-foundation controls. This round audits identity; it does not rerun that binary. | Restricted cold owner only; active guest adoption remains unproved. No backing or GPU authority follows. |
| Physical backing / mapping / alias freeze | New real Windows replacement-owner schedule passes x3; all six exact counterfactuals refuse x3. Existing ledger unit checks pass x3 and 11 assertion controls are detected. | Open. Windows ledger remains unsupported. Saved raw aliases still write retained storage; metadata closure is not physical isolation or writer exclusion. |
| Native GPU completion with retained backing | Cold owner backend refuses GPU submissions/fences. | Open. Need actual native submission identity, completion, content visibility and retained dependencies through success/failure/Unknown. |
| First restricted guest-backed Vulkan Copy | Not admitted. | Blocked by physical freeze/producer exclusion and native GPU completion proof. No success/partial-failure/terminal claim. |

M4 is not complete. F2/F3 remain Open and GuestBackingAdmission remains Unknown.
Do not expand already-passed controlled owner compositions. Do not enable ordinary
DeviceMemory, Windows freeze or guest copy from these results.

## Narrow new deterministic schedule

The real HostMemory facade owns an old Windows allocation and mapped alias. Its
physical pin and RetainedBackingStorage are moved into a writer thread, which
signals entry and waits on a promise. Test-scope moved-from captures are reset;
the parent keeps only a raw pointer and epoch identity, neither retaining storage.

While the writer is paused, move-assignment replaces the facade with a different
real backing. The old epoch must immediately become dead; the incoming epoch must
transfer unchanged and remain live. VirtualQuery must still report the old
backing committed, due only to the paused writer's captured owners.

After a promise releases it, the writer observes the retired epoch and writes the
old alias. Both the old backing and physical pin see the byte. The incoming
backing keeps its distinct value. The writer drops the last pin/storage owner;
after join, VirtualQuery reports both the old backing and virtual reservation free. No sleeps or scheduler
speed assumption determine this ordering. Failure checks occur after joining.

The new full-production counterfactual removes only GuestBackingLifetime's
move-assignment Invalidate call. The same complete HostMemory.cpp and real common
library/native mappings must then fail with exit 91 and
`MAPPING OWNER CHECK: replaced-epoch-still-live`. It does so on all three runs.
Five preceding controls still detect each omitted moved-from getter guard and
false Windows enrollment. Fresh native common build: 67 steps, followed by a source-only test rebuild to
check virtual-reservation release as well. No stale header objects,
no Vulkan source copy, no logger/assert substitutes and no kernel-type stubs.

This schedule verifies allocation retention, retirement identity and refusal.
It also demonstrates why an old pointer is not revoked by owner replacement.
No production defect was demonstrated, so this round changes no production code.

## Evidence freshness and limits

The preceding result record's 74 source inputs were checked: 71 remain identical;
three are the mapping fixture/runner/control generator intentionally advanced
here. All recorded native owner inputs are identical. The old native results are
retained evidence, not new executions. The only additional tool edit provides
an isolated output directory for the existing mapping-ledger runner.

Fresh executions are 21 native Windows mapping processes (three positive and six
counterfactuals x3), plus three existing ledger unit runs and eleven assertion
counterfactuals. The latter exercise the real metadata header only; Linux
HostMemory/VMA/wrapper cases explicitly did not run on Windows. New results do
not establish Vulkan, Android, target device/game behavior or CI success.

The next P0 acceptance still needs a complete physical alias and producer closure
covering canonical pointers, saved virtual aliases, remap/protect entry points
and final owner replacement. Missing ownership/census or any escaping writable
capability must refuse. Keep work at this gate; do not broaden renderer/teardown
refactoring or proceed to guest-backed copy ahead of its dependencies.

Cleanup: 115 temporary binary/fixture files (266,195,010 bytes)
were hash-verified and removed. No such leaves remain in this task directory;
logs, commands and manifests are retained. Earlier evidence and dependency caches
were not removed. No CI run or main change.
