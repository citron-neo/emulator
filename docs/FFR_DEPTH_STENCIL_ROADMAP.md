# FFR depth/stencil buffer layout roadmap

本路线图用于 `codex/ff-resonance-boss-transition`，按 1A → 1B → 2 → 3 顺序验收。
每一步只有实际实现和对应验证通过后才能标为完成；离线模型、SPIR-V 校验和源码审查不能代替项目集成或设备运行证据。

## 当前状态

- [x] 1A：CPU helper 已实现，实际生产源码已通过优化构建及断言调试构建的独立测试。尚未接入 staging。
- [x] 1B：普通 staging 上传与独立同步回读已接入，通过组件编译和离线测试；GPU 运行待验证。
- [x] 2：GPU-side DMA 转换、基线镜像、barrier 与中间资源保活已接入并通过离线验证；数值回退在第 3 步补齐。
- [x] 3：GPU 数值回退及真实异步队列源码的受控生命周期回归已实现并通过离线验证；实际 Vulkan/GPU 和 FFR 运行待验证。

## 1A — 格式转换 helper

范围：在现有 texture conversion 目录实现独立 CPU pack/unpack，区分 guest 交错布局和实际 Vulkan 图像格式对应的 depth/stencil 平面。

验收：

- 独立编译实际 production helper，执行确定性数值及布局测试。
- 明确支持的 guest/host 格式组合；不存在或未核实的 guest 格式不得由排列模型推导为支持。
- 覆盖偏移边界、行跨度、数组层、各 mip 的实际尺寸、部分区域和未触及字节。
- 验证 D24 → D32F → D24 在指定参考舍入规则下精确往返。
- 单独验证 D24 → D16 的量化误差，不能要求无损往返。
- 明确非法布局、越界、整数溢出和不支持格式的失败行为；失败不得留下部分输出。
- 不接入 staging、DMA、GPU 同步或异步 readback。

## 1B — 接入 staging 上传与回读

范围：普通 staging 的上传拆包与回读合包，复用 1A 已验证规则。

验收：

- 完成 guest 数据 → aspect planes → guest 数据的项目路径 round-trip。
- 覆盖行跨度、部分更新、mip 和数组层；保留未更新区域及 guest 填充字节。
- 根据实际创建的 Vulkan 图像格式选择平面元素大小，禁止仅凭 guest 格式解释 host 数据。
- 合包发生在 GPU 写入完成且主机可见后；不能在录制命令时读取尚未完成的输出。
- 此步骤不引入 GPU-side DMA 转换。

## 2 — DMA GPU-side 转换

范围：在 GPU-resident buffer 上完成拆包和合包，复用相同的格式与布局规则。

验收：

- 覆盖 compute-to-transfer 与 transfer-to-compute barrier，以及转换结果后续消费者的可见性。
- 描述符的偏移、范围、对齐和 buffer usage 合法；模板字节合并没有跨线程写竞争。
- 中间缓冲保活至执行完成，复用依赖真实完成状态，不能依赖录制回调退出。
- 保留部分更新范围外的字节，不引入不必要的 CPU 回读或 GPU 全局等待。
- 与 CPU reference 做确定性数据对照，并分别记录离线与真实 GPU 验证范围。

## 3 — 格式回退及生命周期

范围：完成实际 Vulkan D24/S8、D32/S8、D16/S8 回退的集成验证，以及同步/异步回读和中间资源回收。

验收：

- 对每种真实支持的 guest/host 组合验证深度数值转换；记录 D16 等不可避免的精度损失。
- 覆盖同步和异步 readback、部分更新、mip 分配、数组层及缓冲复用边界。
- GPU 完成、主机可见、CPU 合包与数据消费完成之前，不能提前释放或复用对应资源。
- 按需参考 PR #375 的相关 WIP，显式处理所有权接口差异，不合入无关改动。

## Vulkan X padding 规则

规范的 Depth/Stencil Aspect Copy 表规定：D24/S8 的 depth aspect 在 buffer 中按 `X8_D24_UNORM_PACK32` 表示；stencil aspect 独立按 `S8_UINT` 表示。
带 X 的组件在拷贝后可能具有未定义的填充值。因此：

- `X8_D24_UNORM_PACK32` 的高 8 位不是稳定可回读数据，不能参与 D24 数值比较或严格的整字节往返断言。
- 从 host depth plane 解码 D24 时只使用低 24 位；模板值必须读取独立 stencil plane。
- 上传时可确定性初始化 X 位，但不能假设回读会保留这些位。
- 测试需主动扰动回读 depth plane 的高 8 位，确认合包结果不依赖它们。
- guest 侧需要保留的未触及字节或填充字节必须由目的缓冲保留策略处理，不能依赖 Vulkan 的 X 位还原。

规范参考：

- https://docs.vulkan.org/spec/latest/chapters/copies.html （Depth/Stencil Aspect Copy）
- https://docs.vulkan.org/spec/latest/chapters/formats.html （带 X 组件的格式定义）

## 交付约束

- 保持现有 4GB 配置和 ATOM_cas 实现。
- 不启动 GitHub Actions workflow；不进行本地 Android APK 编译。
- 各验收阶段分别报告实际实现、测试结果和剩余未验证行为。
- 有不明确的格式或同步语义时先报告阻碍，不以静默替代或未验证生命周期完成提交。
- 全部目标阶段通过后，才按用户要求同步到 main、FFR 分支和 PR #375，并保留 WIP。
- 没有 FFR 运行证据前，不声称此修复解决 FFR GPU hang。

## 1A verification (2026-10-29)

- Compiled the production depth_stencil.cpp together with its tests, using GCC 13.2 C++20. Both optimized (-O2 -Wall -Wextra -Werror) and debug (-O0 -g -D_GLIBCXX_ASSERTIONS) runs passed.
- Exhaustively checked all 16,777,216 D24 values through D32F with exact reconstruction. D16 conversion was checked against an independent integer-rational reference: maximum reconstruction error 128; 16,711,680 changed values.
- Tested three explicit guest byte contracts and three host depth representations, five mip-sized rectangles and three layers, odd offsets, independent row/layer pitches, partial destinations, padding preservation and perturbed D24 X bits.
- Known byte fixtures, floating special values, invalid enum values, overflow, zero extents, truncated spans, overlapping buffers and rejected-write preservation were checked.
- UBSan could not link because this GCC installation lacks libubsan; no sanitizer pass is claimed.
- The standalone entry point executes the same tests registered with Catch2. The full project/Catch2 target and Android APK were not built. No GPU validation was performed.
- These explicit byte contracts do not yet bind project PixelFormat values to guest layouts. Integration must select the real guest layout and the actual created VkFormat independently, checking device support. D16S8 is a host representation here, not an invented guest PixelFormat.

## 1B implementation and offline verification (2026-10-29)

- Regular depth/stencil staging uploads now unpack into separately aligned depth/stencil planes, with one pair of VkBufferImageCopy regions per copy/mip/layer range. DMA and asynchronous readback retain their existing paths.
- Image stores the format selected by the same MakeImageCreateInfo logic that creates its native allocation. Host aspect representation is selected from this VkFormat, independently of guest layout. Unsupported host formats, image types, MSAA and invalid copy ranges are rejected explicitly.
- Guest mapping was cross-checked against Nouveau nv50_formats.c and the project's TIC lookup: TIC S8Z24 maps to PIPE_FORMAT_Z24_UNORM_S8_UINT and project D24_UNORM_S8_UINT; TIC Z24S8 maps to PIPE_FORMAT_S8_UINT_Z24_UNORM and project S8_UINT_D24_UNORM. Existing blit shaders were not treated as independent byte-layout authority.
- Source: https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gallium/drivers/nouveau/nv50/nv50_formats.c (examined via the mesa-mirror copy); local texture_cache/format_lookup_table.cpp binds the TIC codes to PixelFormat.
- Synchronous readback seeds its destination using original guest data. DownloadMemorySynchronous returns an explicit completion operation owning deferred intermediate planes; callers invoke it after their existing runtime.Finish(), before swizzling back to guest. This retains D32/S8 padding and leaves other guest bytes intact. The batch GC path still waits once. The operation is scoped to the caller rather than stored in a global completion queue.
- D24 readback ignores X bits and reads stencil only from its own aspect plane. Intermediate offsets are four-byte aligned; packed source offsets and row pitches may be nonaligned because CPU packing handles them.
- Actual vk_texture_cache.cpp and explicit synchronous TextureCache template instantiations compiled to Windows GCC 13.2 C++23 object files. No Android APK/full application build was performed.
- Optimized and assertion-debug standalone runs passed for CPU conversion and the production Vulkan staging layout adapter. The adapter checks nine format pairs, five mip dimensions, partial rectangles, layer subsets, pitched buffers, padding, X-bit perturbation and invalid regions. Software buffer/image addressing checks use the production VkBufferImageCopy regions; they do not execute Vulkan commands.
- Catch2 build and execution passed: six test cases, 33,575,223 assertions, including exhaustive D24 float round-trip and separate D16 quantization checks. Fixed the existing test wrapper to parenthesize logical expressions for Catch2 decomposition.
- Staging tests are registered in src/tests/CMakeLists.txt. The offline Catch2 runner is under E:/retro/diagnostics/ffr-depth-layout-20261029/catch-project; it builds the real production helper and test files.
- No runtime GPU execution, driver cache visibility, game behavior or FFR hang resolution is claimed. Async depth/stencil readback and GPU DMA conversion remain outstanding under later stages.

## Stage 2 — GPU DMA implementation (2026-10-29)

- Native GPU DMA supports both D24 guest orders with host D24/S8, and D32/S8 with host D32/S8. D24-to-float, float-to-D24 and D16 numeric fallbacks explicitly decline acceleration with a diagnostic and use the existing guest DMA engine; they are not bit-reinterpreted. GPU numerical fallback support remains stage 3.
- Both directions first GPU-copy the packed buffer into a device-local scratch allocation. Upload computes separate aspect planes and records buffer-to-image copies; download reads image aspects, packs into the baseline scratch, then GPU-copies the result to the canonical buffer and the existing DMA staging mirror. The mirror receives identical untouched bytes, including D32/S8 padding. No CPU readback or additional Finish/Wait is introduced on this accelerated path.
- DMA admission now queries layout without preparing or marking the image, before obtaining/marking the buffer. Invalid size, alignment, pitch, partial texel, format or descriptor ranges refuse acceleration before those side effects.
- The compute pass consumes the CPU staging planner's offsets and format mapping. Each shader invocation owns an output uint word; byte-preservation reads are confined to its own destination word. D24 X bytes are zeroed when unpacking and ignored when packing.
- GPU baseline copies: ALL_COMMANDS/MEMORY_WRITE to TRANSFER/TRANSFER_READ|WRITE, followed by transfer-write visibility to subsequent commands. Compute: buffer-range barriers on the dedicated packed/plane buffers from TRANSFER|COMPUTE writes to COMPUTE read/write, then an output-buffer barrier from COMPUTE writes to TRANSFER|COMPUTE read/write. Final mirror copies additionally publish TRANSFER_WRITE to HOST/HOST_READ. Existing image-layout/readback barriers remain in use; device-local DMA plane readbacks omit the HOST stage.
- Dedicated DeviceLocal staging pool entries hold allocations and are stamped with CurrentTick. Pool reuse/deletion requires scheduler.IsFree(entry.tick), so destroying a local StagingBufferRef does not recycle the buffer on command recording completion. The compute descriptors use zero buffer offsets and bounded ranges; dispatches split at the Vulkan minimum guaranteed X-group-count limit without submitting or waiting.
- GLSL compiles to SPIR-V 1.3 and spirv-val passes for Vulkan 1.1. tools/depth_stencil_dma/check_shader.py extracts the actual uint shader functions and parameter fields, executes them as C++, and compares them against the production CPU helper: 96 native-layout cases per optimization level, including odd offsets/pitches, layers, X-bit perturbation, padding, tail invocations and forward/reverse invocation ordering. This is an algorithm comparison, not Vulkan execution or a concurrency proof.
- GCC 13.2 C++23 compiled vk_compute_pass.cpp, vk_texture_cache.cpp, vk_rasterizer.cpp and the explicit DMA mirror TextureCache template instantiation into object files. Shader headers were generated outside the repository. Catch2 CPU/staging regressions passed (6 cases, 33,575,223 assertions).
- Ordinary asynchronous image readback remains on its original entry point. Only the DMA-specific staging mirror path invokes DownloadMemoryDma. No memory configuration, ATOM_cas or workflow change was made.
- Unverified: actual device compute/copy ordering, driver cache behavior, allocation/recycling under GPU pressure, numerical-format GPU fallbacks and FFR runtime. No FFR hang-resolution claim is made. Do not synchronize other branches until the requested remaining stages have passed.

## Stage 3 — numerical fallbacks and asynchronous ownership (2026-10-29)

- The DMA kernel now converts all nine explicit guest/host pairs: two D24/S8 guest byte orders or D32F/S8/X24, against actual Vulkan D24/S8, D32F/S8 or D16/S8 aspect storage. D16/S8 is a host format only; there is no invented guest PixelFormat. Unknown formats, MSAA, unsupported image types and invalid layout/ranges are rejected. The planner validates each mip's dimensions and array-layer bounds independently.
- Conversions use uint32 arithmetic, exact 48-bit product/division and the CPU helper's rounding policy. UNORM-to-UNORM and float-to-UNORM round to nearest with halves upward; UNORM-to-float produces correctly rounded binary32. Float-to-UNORM clamps negative/NaN to zero and values >=1 to maximum. Native float-to-float preserves the bit pattern. D24 X bits are ignored; guest X24 and untouched bytes come from the GPU/guest baseline.
- D24 -> D32F -> D24 is exact under this reference policy. D24 -> D16 is intentionally lossy (maximum reconstructed D24 error 128). D32F -> UNORM24/16 clamps to [0,1] and quantizes; it cannot preserve out-of-range values, NaNs or the complete float precision/range. UNORM16->24 uses numerical rescaling, not bit shifting. Native D32/S8 is preferred; its missing-format alternatives now prefer D24/S8 before D16/S8, with the complete requested Vulkan feature mask checked for each candidate.
- Pending asynchronous image downloads snapshot guest address, ImageInfo and byte count at enqueue time. DeleteImage cancels obsolete writes and moves native image ownership into a reserved owner before erasing the slot. Destruction requires both queue-consumer release and the deletion GPU tick (which may be later than the readback fence), plus the existing view destruction grace period. Reusing an ImageId cannot redirect an old download to its replacement.
- Asynchronous depth/stencil completion seeds from current guest data after its fence, then runs the same CPU pack operation as synchronous readback. A production completion guard rejects an unsignaled scheduler tick. Tracked CPU writes and unmaps advance an image epoch; an older GPU snapshot is skipped even if a subsequent upload has cleared CpuModified. This covers existing texture-cache invalidation hooks, not arbitrary untracked physical/NCE producers.
- Intermediate plane ownership survives recording, GPU execution and CPU consumption. Only the recording thread returns deferred buffers to the pool; fence-thread callbacks drop shared references. Pool reuse still checks the GPU retirement tick. Exception paths retain paired batch queues, release uncommitted staging, roll back partial retirement publication and avoid replaying writes that already completed. Synchronous dirty state is cleared only after checked completion and guest publication.

### Offline acceptance

- The real GLSL kernel compiles to SPIR-V 1.3 and passes spirv-val for Vulkan 1.1. tools/depth_stencil_dma/check_shader.py executes the extracted shader arithmetic and actual host dispatch-validation prefix against the production CPU helper: 288 native/fallback layout cases per optimization level; odd offsets/pitches, array layers, partial baselines, perturbed D24 X bits, descriptor rejection and near-UINT_MAX tail guards.
- --full-depth24 checks every one of 16,777,216 D24 values and all 65,536 D16 values against independent numerical references. The default optimized/debug runs also check 524,288 deterministic float bit patterns plus NaN, infinity and rounding boundaries.
- tools/depth_stencil_dma/check_async_queue.py compiles the actual CommitAsyncFlushes, PopAsyncFlushes, WriteMemory, synchronous completion, deletion ownership hook, retirement and staging-pool selection source against a controlled backend and real SlotVector/CPU packing. It tests pre/post-record cancellation, deletion and slot reuse, reverse mixed DMA/image offsets, empty batches, CPU overwrite epochs, deletion ticks later than readback completion, allocator thread ownership, buffer reuse, allocation/record/publication failure, partial consumer failure/retry and retirement rollback. Fourteen counterfactual negative controls must fail. This is source-path execution, not merely a separate lifetime model, but it does not execute the complete HLE deletion routine or a Vulkan driver.
- tools/depth_stencil_dma/check_format_selection.py executes the production fallback selector with controlled format capabilities, testing native preference, fallback order and missing transfer-feature rejection. Capability fixtures do not assert that every test combination exists on a real device.
- Catch2 CPU/staging tests pass: six cases, 33,575,223 assertions. Actual compute-pass, texture-cache, rasterizer and device-selector translation units, plus explicit synchronous/asynchronous/deletion/bind/DMA TextureCache template paths, compile as Windows C++23 object files. No Android APK build or workflow is used.

### Remaining runtime limits

- Real device shader execution, compute/copy cache visibility, Vulkan validation, memory pressure and renderer teardown remain unverified. These controlled tests do not establish that the FFR hang is fixed.
- Legacy cross-format image-to-image ReinterpretImage is a separate existing path; its combined depth/stencil buffer copies and packed-size assumptions are not converted by this change. This stage covers regular staging and buffer/image DMA, not that reinterpretation path. No support or correctness claim is made for depth/stencil reinterpretation, MSAA or 3D conversions.
- PR #375 retains its existing shared native-image/view ownership WIP. Its native VkImage accesses must retain the extra shared_ptr dereference when applying these commits; unrelated WIP is not replaced.
