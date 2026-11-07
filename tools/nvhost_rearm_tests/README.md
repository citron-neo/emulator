# Post-wake NV event rearm

Run from the repository root with Python and a GCC-compatible C++20 compiler:

```sh
python tools/nvhost_rearm_tests/run_tests.py --output-dir /tmp/nvhost-rearm
```

The runner extracts actual NVCTRL Wait/Clear and NVDRV counter-cache functions
and links the actual Host1x SyncpointManager implementation. System facilities
and the kernel wake/scheduling boundary are controlled collaborators. The event
gate pauses the real callback after a simulated guest wake but before Signal
returns and the callback publishes Signalled. This is not an Android KEvent,
driver, or FFR runtime test.

The default 100 repetitions verify that same-slot rearm joins the old callback,
then arms the new pending target without a false BadParameter. Completed targets
and zero-timeout polls bypass that join. A genuinely live Waiting event still
rejects overlapping waits, the old callback cannot overwrite the new Waiting
state, and the new target signals exactly once after its real host increment.
Completed and duplicate cancellation also retain their previous behavior.

Historical defect observations:

```sh
python tools/nvhost_rearm_tests/run_tests.py --revision 2c8dba3768 --expect-vulnerable --output-dir /tmp/old-main
python tools/nvhost_rearm_tests/run_tests.py --revision ff12bb169b --expect-vulnerable --output-dir /tmp/old-pr375
```

Both must pass the historical oracle: a new pending target returns BadParameter
while the old callback is Signalling; the same target is accepted after callback
publication. Omitting `--expect-vulnerable` against either old revision must
compile and fail with `rearm rejected or bypassed active callback join`.
Compiler errors, timeouts and crashes are not accepted as negative controls.

The production fix joins by the retained action handle only when rearm observes
Signalling, then rechecks the existing busy predicate. It does not wait for the
new GPU target, publish completion early, relax live-wait rejection, or clear
the guest's readable event on its behalf. EventState alone does not authorize
slot reuse. This repairs a reachable failure path; it does not prove which
underlying error produced the captured FFR NVN FAILED result.

QueryEvent slot decoding and ATOM_cas are outside this change.
