# M1 pre-device static review

Reviewed baseline `44217d0be6` with the fixes in this follow-up. The review follows
draw/compute configuration, all observed buffer categories, texture preparation/
producer marks, resolver/provenance locking, metadata cleanup and shutdown.

## Closed telemetry gaps

1. Baseline entries forgot access intent and aggregate overlap counts could not
   distinguish read-only from potentially writable aliases or descriptor repeats
   from distinct addresses. Entries now retain conservative potential-write intent;
   bounded sampled physical subintervals count distinct GPU/device token+address
   references. Sample boundaries exclude changes in physical membership or DAddr
   provenance. Pair/observation frequency remains event weighted and historical.
2. Unknown global coverage was documented but not reported. Every report now
   prints `coverage=unknown` and uninstrumented categories. Unresolved attempted
   registrations remain explicit. Texture read preparation is observed; color/
   depth render-target preparation passes a separate diagnostic tag. Staged,
   scratch, texel, texture, render-target and direct-buffer categories are separate.
3. Sum/max timings hid tails and mutex costs. Fixed log2 histograms report P50,
   P95 and P99 upper bounds for resolve, resolver lock acquisition, locked work,
   and total dispatch-observation calls. Resolver-only metrics separate GPU/device
   lock acquisition from page walking/stamp/allocation. Cached stamp/provenance
   lock time and provenance work have separate sums. Generation invalidations,
   requery attempts, lifetime invalidations and metadata peaks are explicit.
4. Inline optional shadow reserved a large metadata payload even when disabled.
   The renderer now allocates a unique_ptr payload only with the setting enabled;
   startup diagnostic allocation failure does not stop guest execution.

## Disabled and lifetime paths

- Setting defaults false; only its true construction branch allocates the shadow
  and installs the texture hook. Empty unique_ptr/null texture hook bypass Observe,
  Begin/EndDispatch, binding traversal, logging and shadow mapping locks.
- Optional resolver/validation timers have null default sinks. Null timers read
  no clocks, allocate nothing and acquire no locks. Scoped-lock versus unique-lock
  forms retain the same existing single-mutex acquisition; GPU->device paired
  order is unchanged. They do not add a mapping lock to ordinary call chains.
- Render-target tags and null-pointer checks add scalar CPU overhead when off;
  no zero-overhead claim. No binding/scatter/readback/import decision changes.
- GPU-thread/cache-lock confinement remains in force. Final shutdown reads
  counters under both existing cache mutexes and detaches the borrowed texture
  pointer. Shadow destruction follows cache destruction. Entries retain inert
  mapping/backing tokens, never driver handles or allocation/producer leases.
- Observation exceptions remain contained; immutable geometry and access-intent
  metadata cannot authorize execution, clear dirtiness or transfer data ownership.

## Interpretation limits for the device run

No static/runtime geometry-only observation proves the actual A=99/B=10 overwrite
frequency. Potential writable overlap frequency is evidence to guide the next
coherency stage, not evidence of executed writes, completion or latest data.

CPU/NCE raw aliases, query producers, DMA/inline producers, upload staging and
internal unmarked texture copy/scale producers are still uninstrumented. Their
counts are unknown, not zero. Full producer coverage and live interval membership
are not claimed. Histories can include removed mappings; sampled maxima are lower
bounds when windows evict or scan budgets drop. Don't infer global no-conflict
from zero witnesses. Read-only and potential-write reference classes are defined
by observed access intent, not Vulkan/guest byte inspection.

Metadata has a fixed 128-entry directory and fixed-size histograms/sampling arrays.
Only bounded query vectors allocate dynamically. Observed payload peaks exclude
allocator bookkeeping, process RSS, stack arrays and instantaneous vector
reallocation overlap. Use this metric together with caps/evictions/errors and
external process measurements; no unbounded registry was introduced.

Lock acquisition timers include uncontended overhead and host scheduling. Locked
work includes metadata allocation/validation as well as page table traversal.
Approximate histogram quantiles are bucket upper bounds, not exact latency
samples. Whole-AS generation can invalidate unrelated ranges; cache invalidation
rate is `invalidations / (hits + invalidations)`. Actual contention/latencies and
alias distributions require workflow APK and controlled off/on SMU/SMO sampling.

Windows/Linux regression suites, real-fmt report extraction, Linux sanitizer/cold
backing tests and isolated production TU compilation validate the local changes;
they do not substitute for Android link/device validation. No new correctness
issue requiring a guest-data/ownership change was found in this review.
