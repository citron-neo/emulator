# Device idle cannot retire host-pending checked writes

Date: 2026-10-23 (Asia/Singapore). Baseline: `74279fab75`.

## Ownership gap and minimal correction

Scheduler retains checked write resources in checked_writes and transfer pins /
exclusion owners in checked_transfers. RetireCheckedWritesAfterDeviceIdle checked
observed_submission and transfer host/queue state, but not write submission state.
Device idle alone cannot prove that a write waiting in the host recording worker
has been submitted or cancelled. The actual retirement body could therefore
release a pending write's resource holder and retire otherwise settled transfer
owners even though that write still had future host effects.

The failure-first extracted-body witness reproduced
`device idle retired host-pending write owners`. The fix preflights every write
before any resource release: a missing submission, unattempted queue submission
or VK_NOT_READY result refuses the entire retirement operation. Existing transfer
and observed-submission guards remain unchanged. Once host submission state is
settled and device idle has independently been established, existing cleanup of
successful or failed work remains allowed. No failure becomes content-success
authority.

| Owner state at idle retirement | Disposition |
| --- | --- |
| Recorded observed submission still present | Retain all owners |
| Write missing submission / not queue-attempted / pending result | Retain all writes and transfers, including settled prefixes |
| Transfer worker pending / not queue-attempted / pending result | Retain all owners |
| Host state settled and external device-idle precondition holds | Existing retirement may clear resources and quarantine transfer exclusion |
| Only callback Completed or present device inactivity is known | Insufficient to call this an idle/host-drained retirement proof |

The production RendererVulkan shutdown already joins the scheduler before its
native WaitIdle and calls retirement only on VK_SUCCESS. This change preserves
that path; it strengthens the callable Scheduler retirement boundary for other
explicit controlled paths. It does not acquire device idle, join producers,
replace the external idle precondition with a typed receipt, or build a global
Unknown resource registry. Destruction without proven idle and full backing /
alias/native-resource lifetime remain separate obligations.

## Local validation

```powershell
python tools/gpu_lifecycle_tests/run_checked_retirement_tests.py --repeat 3 --output-dir .diagnostics/m4-checked-retire-final
python tools/physical_coherency_tests/run_sync_provenance_tests.py --positive-only --content
python tools/physical_coherency_tests/run_windows_production_checks.py --source src/video_core/renderer_vulkan/vk_scheduler.cpp --source src/video_core/renderer_vulkan/vk_rasterizer.cpp --output-dir .diagnostics/m4-checked-retire-production
```

Three repetitions of the actual full retirement body pass. Cases cover missing
submission, no queue attempt, pending result, settled-looking error without queue
attempt, settled write prefixes, settled transfer owners, old transfer/observed
guards, retry after host settlement and failed-but-settled cleanup. Two exact
runtime controls reject omitted write admission and omitted transfer worker
pending checks. Resource holders, submission states, exclusion and asserted idle
are fixtures, so this does not demonstrate physical-device completion.

The existing actual Scheduler/synchronization/content positive regression passes,
including replica ancestry, remap, Unknown, retirement and alias fan-out cases.
Its mutation suite was not rerun (`--positive-only`); the private-domain OS fixture
explicitly skips on Windows because it requires Linux. Both complete production
units generate Windows AMD64 COFF objects without diagnostics. Input hashes,
object digests and machine signatures are checked before cleanup.

The workflow includes the new focused runner for future executions; none is
dispatched here. All THREE M4 phases remain incomplete and GuestBackingAdmission
stays Unknown. No Android build, native device test, full app link or main merge
is claimed. Temporary generated sources, logs, manifests, executables and objects
are removed; this Markdown and reproducible test sources remain.
