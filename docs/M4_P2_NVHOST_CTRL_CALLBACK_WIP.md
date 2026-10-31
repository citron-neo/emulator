# P2 nvhost_ctrl callback owner checkpoint - UNVALIDATED WIP

Historical checkpoint, superseded by the completed Windows
[callback lifetime repair](M4_P2_NVHOST_CTRL_CALLBACK_LIFETIME.md) and
[results](M4_P2_NVHOST_CTRL_CALLBACK_RESULTS.json). The statements below describe
the saved state before resumption, not the current validation status.

Saved 2026-10-25 (Asia/Taipei) at the user's explicit request to stop development
and resume later. No further implementation, compilation, tests or CI dispatch
were performed while making this checkpoint.

## Repository and accepted baseline

- Worktree: `C:\Users\BDM11\.codex\worktrees\issue-372-fixes\emulator`.
- Branch: `codex/issue-372-fixes`, origin `Splaser/emulator`, PR #375.
- Last validated production head: `33ca3eb2ad540b669deed958eedb751be9cafb20`
  (`audio: drain raw service callbacks before target retirement`).
- This checkpoint adds documentation and three unvalidated test/candidate drafts;
  it does not apply the nvhost_ctrl production patch or modify a workflow.
- Main remains `b6a2ce9488d78efdbbeb0efe14c2ec5c0a263e76`, untouched.
- No current-head CI result was queried or asserted for this checkpoint. No
  workflow was dispatched. Previous Audio validation is scoped to its own report.
- P2 remains incomplete, F2/F3 open and GuestBackingAdmission Unknown.

## Approved scope and source findings

Scope is limited to nvhost_ctrl, its Host1x syncpoint callback queue, KEvent,
Module/session and OwnedRetirementQueue ownership. Keep the local repair freeze;
do not expand into other teardown subsystems.

Source inspection, not yet runtime reproduction:

1. `nvhost_ctrl::IocCtrlEventWait` registers a Host1x host action capturing raw
   `this` and slot. `nvhost_ctrl::~nvhost_ctrl` currently frees registered KEvents
   without explicitly deregistering those actions.
2. Actual SyncpointManager stores queued actions and executes ready callbacks
   under its guard. DeregisterHostAction uses that guard to remove a queued
   action or serialize with an active callback. Immediate registration execution
   and unrelated direct/escaped callers must not be assumed covered by this alone.
3. Module owns open_files and clears devices in its destructor while providers
   still exist. Session retirement records capture shared_from_this(), retained
   Process/backing and the published session identity. Closed records can retain
   this graph in OwnedRetirementQueue; dispatch drainage is not downstream
   callback completion.
4. GPU::Impl::PushCommandBuffer currently calls CDmaPusher::ProcessEntries
   synchronously. Do not invent an asynchronous NVDEC command queue in the test.
   The real queued path selected here is Host1x syncpoint action storage.

These findings are hypotheses for deterministic owner witnesses. No new
production defect has yet been recorded as reproduced by the draft suite.

## Saved draft files

- `tools/physical_coherency_tests/run_nvhost_ctrl_owner_tests.py`: intended to
  assemble complete Module/Container/session/nvhost_ctrl declarations and relevant
  actual constructor, dispatch, retirement, callback and destructor bodies, plus
  the real SyncpointManager implementation and OwnedRetirementQueue type.
- `tools/physical_coherency_tests/nvhost_ctrl_owner_fixture.cpp.in`: composition
  fixture with controlled KEvent Signal, last facade/retirement-owner release,
  queued/active callback gates and explicit kernel/native collaborators.
- `tools/physical_coherency_tests/nvhost_ctrl_owner_candidate.json`: **unapplied**
  proposed two-pass destructor: deregister/join all registered callbacks first,
  then free KEvents; hold no events_mutex across the callback join; stop without
  unwinding if cancellation/retirement throws.

All three files are newly drafted and **have never been compiled or run**.
Do not interpret their assertions, expected messages or candidate as evidence.
They may need declaration-order, dependency, fixture and control-oracle repairs.
In particular, first inspect forward declarations for Module/EventInterface/
Devices and ContainerImpl/NvMap/SyncpointManager in the generated translation unit;
the full production class declarations are being combined with collaborators.
Do not replace real owner semantics with a prepared shared packet to make it pass.

Planned cases in the draft: queued, active, completed, closed-deferred retirement,
multiple device actions, callback failure, FD close, injected cancellation failure.
Planned historical witnesses: stale queued callback after target retirement and
KEvent free while an active callback is blocked. Planned controls currently omit
cancellation, free before cancellation, and weaken the retirement Module capture.
None of these cases or controls has an accepted result yet.

## Resume sequence

1. Inspect this checkpoint and the three drafts; preserve the validated baseline.
   Repair only the test integration needed to produce trustworthy observations.
2. Run historical source first. Require exact named owner/index assertions; a
   compile error, crash or timeout is not a valid defect witness.
3. Run candidate source and meaningful negative controls before applying any
   production change. Prove queued cancellation, active callback join, and
   Module/session/Process/backing retention through last use. Include multiple
   actions and conservative failure/partial-effect boundaries as needed.
4. Only after those tests pass, apply the bounded, reproduced production fix.
   Run the fixed suite and relevant retirement/dispatch/syncpoint regressions.
5. Compile related real production files on Windows, record exact input/object
   hashes and limits, then remove only this run's generated exe/object/fixture
   files after checked paths/hashes. Preserve logs and committed result JSON.
6. Update the authoritative coverage/handoff, commit in English and push the
   issue branch. Do not merge main or claim global P2/native Guest admission.

Starting commands (not executed at this checkpoint):

```powershell
Set-Location C:\Users\BDM11\.codex\worktrees\issue-372-fixes\emulator
git status --short
git log -3 --oneline
$env:CXX='C:/Strawberry/c/bin/g++.exe'
$env:PYTHONUTF8='1'
python tools/physical_coherency_tests/run_nvhost_ctrl_owner_tests.py --baseline --output-dir .diagnostics/m4-nvhost-owner-33c-baseline
# Continue only after the baseline witness and fixture integration are valid.
python tools/physical_coherency_tests/run_nvhost_ctrl_owner_tests.py --candidate --output-dir .diagnostics/m4-nvhost-owner-33c-candidate
```

No temporary executable was generated in this unfinished nvhost_ctrl work.
The user's repeated Daybreak warning prompted this stop/checkpoint; its cause
was not diagnosed, and no workaround or configuration change was attempted.
