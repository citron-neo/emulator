# M4-F3 process Unmap reference lifetime

This follow-up addresses one exception-ordering prerequisite above the
[per-Memory mapping publication component](M4_F3_OUTER_MAPPING_PUBLICATION.md).
It does not enroll a process or establish a general guest mapping freeze.

## Reproduced source behavior

`KPageTableBase::Operate` handles Unmap/UnmapPhysical by constructing a
`KPageGroup` from the still-mapped region, then invoking `Memory::UnmapRegion`.
The original SCOPE_EXIT invokes `KPageGroup::CloseAndReset` during any exit,
including an exception from the closed controlled Memory entry before PTE effects.

This group records the mapping's existing physical references; MakePageGroup does
not acquire replacement references. CloseAndReset calls the actual memory manager
Close on every recorded block. Closing those references while their PTE remains
published can remove the backing ownership on which that mapping relies.
The ordinary KPageGroup destructor only frees bookkeeping descriptors; it does
not close physical references.

The repair moves CloseAndReset after a successful UnmapRegion return.
A refusal before effects leaves the existing mapping references intact. An
exception after possible effects retains the references rather than releasing
potentially accessible backing. Success still closes once, after unmapping.
The partial-effects case is not rolled back or certified safe to resume.

MakePageGroup's existing best-effort result handling remains unchanged here.
It refuses non-heap physical ranges, which have no heap allocator references;
Map/Unmap also support those paths. Treating every MakePageGroup refusal as a new
fatal unmap error would change that behavior. This repair does not certify a
complete group under bookkeeping allocation failure or mixed ownership.

## Focused evidence and limits

`run_process_unmap_tests.py` executes the extracted actual Operate overload,
Memory::UnmapRegion wrapper, controlled entry and KPageGroup cleanup bodies.
Mapping effects, kernel, descriptor allocation and physical reference counters
are injected collaborators. It does not execute a native frame allocator or a
process teardown and does not claim actual page reuse was observed.

The original production body reproduces the intended assertion with
`--expect-defect`: the Memory entry refuses, its mapping collaborator remains
mapped, and the physical-reference collaborator still receives Close. A separately
generated candidate fixture first passed three repeats without changing production
source. After implementation, the actual fixed-source fixture passed three local
Windows repeats and both assertion mutants. Linux production acceptance remains
pending; local fixture results are not a native allocator or process witness.

The fixed-source suite requires three positive repeats over both Unmap modes,
covering a closed actual entry, injected pre-effect and uncertain exceptions,
successful cleanup ordering and a non-heap compatibility control. Assertion
mutants restore the original unconditional SCOPE_EXIT or omit successful cleanup. Compiler failures,
timeouts and unrelated failures cannot count as a successful mutant rejection.

## Roadmap boundary

KProcess still constructs an untracked Memory. Its Finalize deletes TLS, finalizes
the process PageTable and finally releases Memory state. KPageTableBase::Finalize
also unmaps through System DeviceMemory directly and resets its Common::PageTable;
those paths are not covered by a single Memory publication entry.

MapGroup additionally spans several Memory calls under KScopedPageGroup reference
ownership. Closing entry between those calls needs a whole-operation protocol,
not a successful first-node mapping. Raw GetImpl references and independent
Memory/service/device users also remain outside the cohort. Neither moving this
Unmap cleanup nor the preceding component permits enabling Controlled mode on
ordinary processes or treating a drained Memory as a complete table freeze.

F2/F3 remain Open. Ordinary routing, physical content authority, all-process
coverage and retained guest-wide Unknown isolation are not enabled. Validation
for this iteration remains Linux-only; Android is not requested.

Linux-only validation is pending in
[39384043830](https://github.com/Splaser/emulator/actions/runs/39384043830), exact
source `74aea98af7a49f4098f6772328b0271c5d904f9f`. The earlier publication-component
run `39380436500` passed at `3576ea32bd` and does not accept this new Unmap change.
