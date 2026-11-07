# FFR GPU hang investigation handoff

Updated: 2026-11-04 (Asia/Taipei)

## Goal and constraints

Investigate the repeatable FF Resonance GPU hang/crash around random encounter transitions and settlement screens. Use saved captures and current project source first; patch only when runtime/source evidence identifies a concrete defect.

Keep the game's 4 GB memory setting and leave `ATOM_cas` and Phi unchanged. Do not start GitHub Actions. The user builds APKs with their own workflow and tests them on device. Do not claim the game hang is fixed without target-device evidence.

## Current checkout and branch state

- This document is in the FFR worktree: `C:\Users\BDM11\.codex\worktrees\ff-resonance-fix\emulator`.
- Branch: `codex/ff-resonance-boss-transition`.
- Branch HEAD at handoff: `be02e625a6e3b2db207834cfbc81ea0e6ad62e3d`.
- Main source checkout: `E:\retro\emulator`, branch `main`, HEAD `1375c6c0cdce9ebd3a9c6a06dfe4c7018f726f61`.
- The most recent source investigation was explicitly against that `main` HEAD. Recheck both refs before doing work because they can advance independently.
- Both worktrees were clean before this handoff document was added. No source code, device configuration, or branch refs were changed in this turn.
- No workflow was started.

## Symptom and reproducibility

The user repeatedly reaches a random encounter from the FFR overworld and then sees a frozen picture followed by a crash. In recent runs, music continued for about 1–2 seconds after the picture froze. Failures have occurred around encounter loading and battle settlement. User observations have varied between a hard freeze and crash; native fault snapshots report `VK_ERROR_DEVICE_LOST` and termination in the GPU semaphore wait path.

The failure has persisted across multiple APKs and prior fixes. One comparison with asynchronous GPU simulation disabled still hung/crashed, so that toggle alone does not explain the failure. A prior depth/stencil layout investigation and UBO upload/ownership fixes also did not resolve it. Avoid treating the temporal correlation with any one previous patch as proof of cause.

## Latest saved incident: fault 77

Authoritative capture and analysis directory:

`E:\retro\diagnostics\ffr-compute-binding-20261031\`

Start with:

- `FINDINGS.md` — fault77 command/UBO and shader notes.
- `IMAGE_VISIBILITY_AUDIT.md` — latest saved-data and source audit for sampled input image ownership and barriers.
- `image-descriptor77-audit.json` and `verify_image_descriptor77.py` — descriptor decode and deterministic comparisons. The verifier passed on saved bytes.
- `image-probe-plan78.json` — four planned runtime probe sites; this is a plan, not evidence that probes ran.
- `data/` — captured kernel/GPU data and hash manifest; `data/commands/` contains decoded command allocations.
- `data/trace_graphics.py` and `data/graphics-producer-states.json` — reconstructed graphics producer candidates.
- `compute-resource.json`, `fault-ubo-ownership.json`, `ubo-buffer-timeline.txt` — faulting compute UBO and buffer history.

Capture facts recorded for fault77:

- GPU context 26, KGSL timestamp 99141, Vulkan submit host tick 94667; the latest known completed tick at submit was 94665.
- Faulting command root `0x4084540000`, 895 dwords. First compute dispatch at `0x4084540da4`, grid `128x64x1`, using native compute shader `0x401a0ff200`.
- The 32-byte UBO maps guest address `0x84653100` to a native buffer and GPU address `0x4029896100`. The binding metadata says 32 bytes (`SIZE >> 17 * 16`), not 64. The same native object/slot mapping is seen in the retained pre-fault buffer timeline.
- A mapped upload of 4096 bytes to guest range `[0x84653000, 0x84654000)` is recorded before the UBO bind. The command tracer reports no loss/overrun for its retained interval. This is evidence for the upload path and identity, not a GPU-time proof of data visibility.
- A normal-world 32-byte UBO read was captured as a control, but it occurred outside the faulting submission and cannot prove the exact bytes consumed by the GPU at fault time.

## Input depth image: established facts and limits

The first 64 bytes of the captured A8xx texture descriptor decode as a 2D, single-sample `Z24_UNORM_S8_UINT` view, 1384x780:

- Data base `0x402a00a000`.
- UBWC enabled; flag base `0x402a003000`.
- Encoded data array pitch 4,415,488 bytes; UBWC row pitch 128 bytes.
- The last depth-write-enabled draw candidate has matching depth/flag bases, D24S8 format, array pitch and flag row pitch. Its raw RB row pitch is 5632 bytes and the texture line offset is 45056 (8 × 5632). Texture and RB pitch fields have different units/semantics; do not compare raw encodings as if they were the same quantity.
- One mip is present in the sampled descriptor. This does not identify the original Vulkan view's base mip or image generation.
- The descriptor's all-X swizzle is compatible with the current depth sampling conversion. It is not, by itself, evidence of a stencil swizzle or format-fallback bug.

The saved data **does not** identify the bound project's cache `ImageId`, native `VkImageView`/`VkImage` generation, or the exact image/subresource range passed to the actual native barrier callback. Matching physical data and UBWC addresses strongly ties the descriptor to the depth attachment layout, but does not distinguish a valid current image from a retired/replaced alias. Candidate output descriptors beyond reflected/used bindings may be stale heap data, so do not claim all outputs are disjoint based only on the 512-byte block dump.

The graphics reconstruction has 136 candidate draws and no missing captured state blocks. It is a candidate reconstruction, not proof that conditional draws executed or that their writes completed and became visible. A bounded vertex evaluator reached all modeled fetches and passes its offline tests; that does not establish native shader translation, raster execution or GPU completion.

## Current-main source audit

The last static audit checked main `1375c6c0cdce9ebd3a9c6a06dfe4c7018f726f61` and the installed ELF corresponding to that build.

Existing paths found in source:

1. `VisitImageView -> PrepareImageView -> PrepareImage` refreshes image contents and synchronizes aliases before sampling. Alias synchronization orders newer aliases and uses image copies with pre/post barriers.
2. `ImageView` retains its image ID, guest address, format/aspect, subresource range and native image handle. The sampled view path applies depth/stencil aspect selection and channel conversion.
3. Image deletion/view invalidation clears both compute and graphics descriptor references, framebuffer references and retires native resources through existing queues. No stale image-descriptor slot witness was found in fault77.
4. Framebuffer construction retains attachment image handles/ranges. `Scheduler::EndRenderPass` copies them into its queued command before ending the active-pass state.
5. The queued EndRenderPass callback ends the native render pass and issues per-image `GENERAL -> GENERAL` barriers. Source access includes color and depth/stencil attachment writes; destination access includes shader and attachment reads/writes. Source stages include early/late fragment tests and color output; destination is all commands. Dispatch obtains an outside-render-pass context. A deferred guest WFI also emits a memory barrier.
6. Initialization state is exchanged in the image layout transition path. The earlier missing-initialization hypothesis is not supported by this code.
7. The direct cached unified-buffer path requires host coherent memory. The earlier speculative missing-flush explanation is not supported for that path.

Installed ELF disassembly/vtable checks corroborated the relevant callback and constants. Key VA locations, before applying the module's runtime load bias:

- `PushImageDescriptors` sampled-image publication: `0x193c88c`; compute caller return address `0x1943844` can distinguish this use from graphics.
- `ImageView` constructor: `0x195b218`; stored image handle at view offset `0x280`.
- Scheduler EndRenderPass: `0x194739c`; execution callback: `0x19490cc`.
- Per-image barrier construction: `0x1949140`.
- Actual native `vkCmdPipelineBarrier` dispatch: `0x19491a8`; callback return `0x19491ac` is CPU callback completion, not GPU completion.
- `SubmitQueue`: `0x18ac690`.

Use the exact binary and validate ASLR/module base before using these addresses. The RX file-offset mapping used in the prior audit was VA minus `0x4000`; do not apply that as a runtime address adjustment. Probe points are not yet installed or validated.

### Conclusion so far

No concrete production-source defect has been demonstrated by fault77. The code contains a depth-attachment-to-shader barrier path, but fault77 lacks the native image identity and emitted barrier target/range needed to establish that the path covered the same image generation consumed by the compute descriptor. Do not add a generic barrier or change alias/lifetime code without that evidence.

## Next diagnostic step

The next useful experiment is narrow: validate four read-only runtime records during ordinary FFR rendering, then trigger a single encounter only after the normal-render records are internally consistent.

1. At compute `PushImageDescriptors`, record project view pointer, native `VkImageView`, associated `ImageId`, guest address, native `VkImage`, format/aspect, mip/layer range, and the real caller. Filter to the compute call site or D24S8 view so the capture remains small.
2. In the actual EndRenderPass worker callback, record each native barrier's image handle, aspect mask and subresource range.
3. At the actual `vkCmdPipelineBarrier` dispatch, record source/destination stages, image-barrier count and barrier-array contents. Also record callback return, but label it as command-recording return, not GPU completion.
4. Correlate these records with `SubmitQueue` and KGSL timestamp for the faulting submission. Do not equate CP pointer movement, WFI, callback return or a semaphore request with GPU completion.
5. Verify probe addresses/register assumptions against the exact installed ELF and current module base before enabling capture. The saved `image-probe-plan78.json` is only a register/address plan. Reuse the previously validated bounded read-only fault collector if available; avoid frequent `su` shell polling and avoid persistent diagnostic files on the Android device. Transfer/hash the bounded capture to the host and remove task-owned temporary device files afterward.
6. A normal draw can validate that the probes and field interpretation work, but it does not prove the failing dispatch's resource state. After normal validation, capture one failure. Compare descriptor image/view identity against the barrier's native image and range, and inspect intervening alias copy, replacement, invalidation and retirement events.

Interpretation:

- If the compute descriptor's native image is absent from the preceding barrier set or the range does not cover the sampled subresource, trace the exact image replacement/alias path and prove why the barrier state was lost before patching.
- If the descriptor and barrier refer to the same live image and covered range, this specific missing/wrong-image-barrier theory is falsified; move to the next consumer/resource path rather than broadening barriers speculatively.
- If image identity agrees but the generation/lifetime is ambiguous, the capture is still inconclusive; obtain the creation/retirement event for that exact native handle.

## Device and diagnostics state at handoff

- Device previously used: Odin3, adb serial `15e05ed4`; adb executable `E:\retro\platform-tools\adb.exe`; package `org.citron.citron_emu`.
- Fault77 build was reported as version `1375c6c0c`, ELF build ID `f556d63afff190af7bebe7c91982a32c6e2da55d`.
- Last known device process state in the prior investigation: Citron was not running after fault77. A request was pending for the user to reopen Citron and stop at the game list; no reply was recorded before this handoff. Recheck device, app version and module path before doing anything.
- The prior diagnostic host root contains substantial retained data. Keep it as evidence; do not recapture or delete it by default. Android-side task files were reported cleaned after transfer. Recheck before creating any new capture.
- Main fault77 diagnosis: `E:\retro\diagnostics\ffr-compute-binding-20261031\IMAGE_VISIBILITY_AUDIT.md`.
- Broader fault77 findings: `E:\retro\diagnostics\ffr-compute-binding-20261031\FINDINGS.md`.

## Working rules for the next agent

- Start by checking branch heads, dirty state, current APK/native build ID, device connection and any live capture process. Do not assume values above remain current.
- Keep investigation against the APK's corresponding `main` source revision; if it differs from the recorded head, identify the delta first.
- Use saved fault77 data before asking the user to reproduce. Ask for a new run only for the specific native image/barrier identity gap described above.
- Do not edit `ATOM_cas`, Phi, 4 GB settings, or unrelated FFR code. Do not launch workflows or claim a runtime fix.
- Preserve prior WIP on FFR and PR #375. This handoff alone is not a request to sync or push branches.
- Any patch needs a falsifiable witness and a targeted offline check; device testing remains the user's workflow/build step.
