# Command source ownership and QMD bounds regression tests

Run from the repository root with Python and a C++20 GCC-compatible compiler:

```sh
python tools/gpu_command_tests/run_tests.py --output-dir /tmp/gpu-command-tests
```

The runner extracts the actual segment-read lambdas, command decoder, compute
CallMethod/ProcessLaunch, and MemoryManager address-range predicate. It uses the
production CommandHeader, GuestMemory and ScratchBuffer implementations. Memory
mapping, engine callbacks, renderer dispatch and scheduling are simulated; this
is not Android, Vulkan completion, or FFR runtime acceptance.

Tests cover mutable contiguous and copied non-contiguous sources, safe/unsafe
read policy, increasing packets crossing segments, NI/IncreaseOnce packets,
owned semaphore/admission continuations, delayed next-segment reads, and the
control where corruption already exists before fetching. Main/WIP admission
hooks are exercised when present in the extracted decoder.

QMD cases cover valid descriptors including the final 256 bytes of a 37-bit
address space, the captured invalid address 0x910e000200, other invalid addresses,
and rejection before descriptor reads, indirect checks, dispatch or releases.
Range-predicate tests cover crossing the end and integer wraparound.

Historical negative controls can be run independently:

```sh
python tools/gpu_command_tests/run_tests.py --revision 04edd6b57c --suite commands --output-dir /tmp/old-commands
python tools/gpu_command_tests/run_tests.py --revision 04edd6b57c --suite qmd --output-dir /tmp/old-qmd
```

Both commands must compile, then fail their respective runtime oracle. To verify
the historical defect observations themselves, use `--expect-vulnerable` with
that revision; it must pass. Compile failures do not count as negative controls.

The snapshot is per segment when Step reaches it. It does not prefetch the whole
submission or fix bytes already corrupted at fetch. The QMD check validates the
configured address-space bounds; it does not establish mapping, descriptor
content validity, or authoritative physical-alias ownership. Rejected QMDs use
the existing graphics failure shutdown path instead of fabricated completion.
