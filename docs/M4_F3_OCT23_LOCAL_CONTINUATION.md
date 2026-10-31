# October 23 local continuation

Synchronized `codex/issue-372-fixes` by fast-forward from `72555967d1` to
`b6d1f03b65`, incorporating seven morning commits without conflicts. Main remains
`b6a2ce9488`; no intermediate zero-copy merge is made.

The next focused callback boundary is duplicate retained-closure admission. A
dispatcher starts an action, invokes another copy while the original remains
Running, then throws. The second copy must not execute the action or acknowledge
the original's completion. The checked caller must still wait for the original,
then report Failed. Both successful and throwing original actions are covered.
This extends the actual-header regression; no production behavior change was
needed for this boundary.

Windows validation commands:

```powershell
python tools/gpu_lifecycle_tests/run_dispatch_exception_tests.py --repeat 3 --output-dir .diagnostics/m4-oct23-dispatch
python tools/gpu_lifecycle_tests/run_checked_unmap_tests.py --repeat 3 --output-dir .diagnostics/m4-oct23-unmap
python tools/physical_coherency_tests/run_windows_production_checks.py --fetch-boost --output-dir .diagnostics/m4-oct23-production
```

The first production attempt stopped at missing Boost headers, before compilation;
it is not accepted as evidence. The helper fetched SHA256-pinned official Boost
1.87.0 to the current user's external cache before retrying. Dependency pins and
submodule gitlinks were unchanged.

Dispatch positives passed three repetitions; four precise mutation controls
passed, including `duplicate-running-admission`, rejected with
`duplicate dispatch replayed running action`. The original three controls retain
their exact runtime failure checks. Fixture dispatchers/workers do not establish
native GPU execution or full System lifetime safety.

Checked-unmap positives also passed three repetitions, with all five exact
mutation controls rejected as expected. Nine complete production units generated
Windows AMD64 COFF objects without compiler diagnostics. All 32 input-hash records
across the three final manifests, plus nine object digests and machine signatures,
were independently matched before cleanup. This is focused object compilation,
not an application link or device execution.

All THREE M4 phases remain incomplete. Ordinary GuestBackingAdmission remains
Unknown. Further work must address independent producers, raw/escaped aliases,
physical backing publication and native completion/Unknown ownership. This
callback proof is not a new M4 milestone. No Android build, remote workflow,
full-application link or native Vulkan execution is claimed. Only the Markdown
conclusion and test source are retained from this round; temporary binaries,
objects, generated fixtures, manifests and logs are removed after inspection.
