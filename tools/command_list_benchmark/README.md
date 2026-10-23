# Command-list allocation and submission benchmark

This standalone benchmark addresses PR #335's allocation/latency review without
changing production behavior. It needs C++20, Python 3, CMake, and Boost 1.87
headers; it does not build the emulator or Android application.

## Reproduce

From the repository root, using an existing Boost source/header tree:

```sh
cmake -S tools/command_list_benchmark -B build-command-list-benchmark \
  -DCMAKE_BUILD_TYPE=Release -DBOOST_SOURCE_DIR=/path/to/boost-1.87.0
cmake --build build-command-list-benchmark --config Release
./build-command-list-benchmark/command_list_benchmark > results.csv
```

For a multi-config Windows generator, run
`build-command-list-benchmark/Release/command_list_benchmark.exe`. For MinGW,
add `-G "MinGW Makefiles"`; CMake's Python3_EXECUTABLE can select a specific Python.
Keep optimization enabled and compare both storage types on the same machine.

## What is measured

- Current `Tegra::CommandList` is included directly from `dma_pusher.h`.
  `generate.py` extracts its definition and the three current fence builders
  from `nvhost_gpu.cpp`. The baseline changes only the storage to the original
  `boost::container::small_vector<T, 512>`. The builder bodies were checked against
  `94ba76c0a2^`; they are unchanged apart from storage and formatting.
- Builder-only rows measure construction, reading the words, and destruction.
  The WFI builder executes its intermediate increment builder and range insert:
  neither vector is reserved or simplified for the benchmark.
- Submission sizes are 0, 1, 4, 16, 64, 128, 256, and 512 GPFIFO entries. Each size
  covers no fence, an unsignalled wait, increment without WFI, increment with WFI,
  wait + increment, and wait + WFI. Entries are sized and copied as in
  `SubmitGPFIFOBase1/2`; deterministic input generation is also inside the timing.
- The submission lifecycle includes fence building, SubmitListCommand/variant/
  container move boundaries, the actual `Common::MPSCQueue` (4096 slots), dequeue,
  the `std::queue<CommandList>` DMA push, reading all payload words, DMA pop, and
  destruction. The harness uses a reduced command variant containing only
  monostate and submit, not unrelated GPU command alternatives. Scheduler lookup,
  its lock, actual DMA decoding, guest-memory access, syncpoint evaluation,
  rendering, and device execution are excluded.
- `serial` measures that lifecycle on one thread, with nonblocking dequeue.
  `threaded` measures producer-to-consumer completion on two threads with a
  condition-variable acknowledgement after each whole submission. It is an
  end-to-end **CPU submission harness** latency, not full ioctl-to-GPU-fence
  completion or asynchronous game throughput. It includes OS scheduling costs.
- Allocation counting is a separate serial pass, not the timing pass, using
  thread-local counters in ordinary new/new[]. It includes vector growth and
  amortized DMA deque allocations. Queue/thread/fixture startup is excluded.
  Threaded count columns are blank: they are not zero-allocation claims.
  No measured payload uses over-aligned allocation. Allocated bytes are the sum
  of allocation requests, not peak/live/RSS memory.
- Each submission row warms up 200 submissions, counts 1000 serial submissions,
  and reports median/min/max batch-average ns/submission across seven batches:
  20,000 serial or 2,000 threaded submissions per batch. Builder timings use
  seven batches of 100,000 calls. Storage order alternates across paths.
  Min/max are batch averages, not individual-operation latency percentiles.
- Validation checks 4/6/8-word fence output equality and compares every
  submission case's accumulated payload checksum between implementations.
  These checks remain active in Release builds.

## Recorded result (2026-10-10)

Source: PR branch `6bdd825a078f80e11f01ed27eef13a613577bc73`.
Host: Windows x64, Intel Core i9-11900K; GCC 13.2.0 MinGW-w64/UCRT,
Boost 1.87.0, Release `-O3 -DNDEBUG -std=gnu++20`.
The full matrix is in `results-windows-gcc.csv`; a second run is in
`results-windows-gcc-repeat.csv`. Sizes/layouts/allocator policies and timings
are toolchain-specific. No Android or game-performance claim is made.

`sizeof(CommandList)` was 48 bytes with vector and 6192 bytes with small_vector.

| Builder | Vector allocations / requested bytes | Small-vector allocations | Vector median ns | Small-vector median ns |
| --- | ---: | ---: | ---: | ---: |
| Wait | 1 / 16 | 0 | 32.5 | 9.8 |
| Increment | 3 / 56 | 0 | 94.4 | 9.8 |
| WFI (including intermediate increment) | 5 / 96 | 0 | 160.2 | 15.7 |

For WFI, this libstdc++ implementation allocates result sizes 2 then 8 words,
and intermediate increment capacities 2, 4, then 8 words: five requests in total.
Different STL growth policies may produce different counts.

Selected serial submission lifecycle medians, including allocation and moves:

| Entries | Path | Vector ns | Small-vector ns |
| ---: | --- | ---: | ---: |
| 1 | None | 162.9 | 181.6 |
| 1 | Wait | 315.0 | 364.8 |
| 1 | Increment | 369.5 | 356.7 |
| 1 | WFI | 432.3 | 373.8 |
| 64 | None | 202.8 | 237.7 |
| 64 | WFI | 482.7 | 430.9 |
| 512 | None | 465.5 | 749.8 |
| 512 | Wait | 634.0 | 853.5 |
| 512 | Increment | 695.6 | 884.1 |
| 512 | WFI | 785.7 | 872.9 |
| 512 | Wait + WFI | 924.0 | 1168.1 |

The allocation cost **can outweigh the removed inline-storage move cost for
small submissions**, especially WFI. At 512 entries every tested serial fence
combination favors vector in both runs. The repeat run measured one-entry WFI
at 466.8 ns vs 367.8 ns, and 512-entry WFI at 734.3 ns vs 802.3 ns. Marginal
increment results at smaller sizes change order between runs. This is not a
uniform win at every size.

The DMA queue also matters: with this libstdc++ deque, a 6192-byte small-vector
list needs a new deque block per pushed list, while vector-backed lists amortize
block allocations. For a nonempty unfenced submission, the serial counts were
1.100 allocations/submission with vector versus 1.000 with small_vector. For
WFI they were 6.200 versus 2.000; for wait + WFI, 7.300 versus 3.000. Vector's
extra requests are real, but the baseline's queue requests are much larger.

Threaded measurements include scheduler/condition-variable noise. In the first
recorded run, 512-entry unfenced completion was 8.72 us (vector) vs 9.17 us
(small_vector), while one-entry wait + WFI was 10.69 us vs 10.70 us. Fine-grained
differences at those scales do not establish a game-performance improvement.
The raw runs should be considered together, not used as a blanket speedup claim.

Production storage and fence builders remain unchanged. Any follow-up optimizing
small fence allocations should be a separate measured change with Android
validation; this benchmark alone does not justify reverting all vector storage.
