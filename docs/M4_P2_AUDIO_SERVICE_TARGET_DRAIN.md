# M4 P2: actual Audio service targets, raw callback drain and worker join

Date: 2026-10-25, Asia/Taipei. Historical baseline: `7fc738baf7e27b1d1f3c7f0e7ed021fb8f81a3e3`.

## Reproduced owner failure and bounded repair

The real owner chain is ServerManager Port's handler factory (and IPC session
handlers) -> shared IAudioInManager / IAudioOutManager -> unique_ptr impl ->
AudioCore::AudioIn/Out::Manager. Manager::LinkToManager registers
`std::bind(&Manager::BufferReleaseAndRegister, this)` with AudioManager. The
registered function does not own that target. The previous prepared shared
Session test did not prove this production chain.

Two historical witnesses now reproduce three times each using the actual service
class declarations, constructors/default destructors, unique impl, raw callback
registration and callback bodies. Audio callback pauses inside its target body;
Kernel CloseServices runs actual server worker-join statements and releases the
last factory-held service. The target retirement probe detects an active callback
before any freed target/mutex is used. AudioIn and AudioOut both fail. A third
witness pauses a setter after its old running check; shutdown joins, then that
setter publishes a callback and returns success after close.

The fix installs a cold `SetBeforeServiceRelease` callback on the Audio server
before it is published. ServerManager destruction executes this order:

1. Request IPC stop, wake its main loop, wait for it and join additional workers.
2. Keep ports, handler factories and sessions alive; invoke the Audio drain hook.
3. AudioManager closes registration under the same lock used by SetIn/OutManager,
   requests stop, releases that lock, wakes and joins its worker.
4. Only after that real join may the server delete ports/sessions and release
   the service's unique raw callback target. AudioCore/ADSP/sink context remains
   alive through this interval under the owning System teardown.

System's existing GPU-before-service teardown order is unchanged. A service
operation may hold the target's recursive mutex. Its thread must exit before
Audio join: a new interleaving holds that exact mutex on a real jthread and makes
the Audio callback wait for it. The server's actual stop and m_threads.clear
release/join that operation before invoking the hook. A negative control moving
Audio join ahead of service work is rejected with the named dependency assertion.
There is no broad System shutdown reorder or member-declaration reorder.

AudioManager::Shutdown permits sequential repeats by its single owning teardown
thread: later AudioCore destruction does not join a non-joinable worker. No lock
is held across worker join. Callback registration racing close either commits
under its existing lock before close or returns OperationFailed after close.
This is a registration receipt, not a receipt that the callback completed.
Unexpected Audio join exceptions stop without unwinding at `audio.callback_join`;
an exception in the server drain hook stops at `service.owner_drain`, before
factories/targets/context can destruct. Existing partial callback failure still
seals its ProducerDrain domain; terminal joining does not reopen it.

## Actual code and fixture boundary

The new suite includes the complete production AudioManager and AudioCore class
declarations. It extracts actual AudioManager constructor, registration, Event,
worker and Shutdown bodies, AudioCore constructor/destructor/Shutdown, complete
AudioIn/Out Manager and service class declarations, service constructors/default
destructors, target constructor/LinkToManager/BufferReleaseAndRegister, actual
shared-handler RegisterNamedService overload, ServerManager worker-join/drain
destructor prefix, Audio LoopProcess server/drain setup, Kernel CloseServices
body and the relevant System shutdown ordering slice.

The class declarations keep the actual service unique_ptr impl. Test code obtains
an address only through a construction observer, calls actual LinkToManager and
then drops the external service root. The actual handler-factory capture remains
the last owner. This is not a substitute shared Session callback or an extra
shared_ptr artificially pinning the target through shutdown.

CMIF/ServiceFramework handler metadata, Kernel ports, main server-loop stop event,
sink/ADSP and System fields are collaborators. Additional service work uses a real
jthread and the real target mutex, with a controlled service body. Target sessions
are inert, so native buffer release, DSP/sink I/O and real IPC execution are not
claimed. Lifetime probes annotate implicit target destruction and callback
entry/exit; no destructor/lock/callback is skipped to obtain green evidence.
Join exceptions and callback side effects are injected; they are not OS join
failure or native audio hardware fault reproduction.

## Deterministic acceptance

Candidate protocol tests pass before applying its production owner-drain hook.
Candidate and fixed suites each pass 13 normal schedules, four terminal schedules
and five exact negative controls, each three repetitions:

- Out/In callbacks active during last service-owner retirement.
- Out/In workers paused in the actual closed ManagedHostProducer path, then woken
  and joined without invoking the pending raw callback.
- Out/In callback failure after an injected partial effect, retaining the Unknown
  seal while terminal shutdown safely joins the failed worker.
- Repeated close/destructor calls, both slot registrations, and setter-wins /
  setter-loses interleavings under the registration lock.
- Out/In service workers holding their actual target mutex while Audio waits;
  service stop/join must precede callback join and target release.
- Out/In injected Audio join failure and Out/In injected service hook failure:
  exact exit 86/stage, no target retirement or atexit handlers.

Controls omit the Audio server drain, omit Audio worker join, release service
owners before the hook, join Audio before admitted service work can release its
target mutex, and move registration's running check outside the close lock.
Each requires exit 91 and the named assertion three times; compile failures,
timeouts, crashes or another assertion do not count as acceptance.

Event/promise gates establish actual callback entry, registration lock positions,
host pause and worker-join attempts. A target mutex try-lock observer proves the
service callback dependency before shutdown proceeds. No test sleep substitutes
for a producer acknowledgement. Failure probes run in child processes so blocked
threads are never unwound merely to manufacture a passing owner result.

## Windows and regression evidence

Nine real production Windows GCC 13.2 AMD64 COFF units compile with real headers
and zero diagnostics: core.cpp, AudioManager, AudioCore, AudioIn/Out targets,
IAudioIn/OutManager, ServerManager and Audio LoopProcess. The pinned Opus submodule
`101a71e03bbf860aaafb7090a0e440675cb27660` was initialized to provide the previously
missing real opus.h dependency; no gitlink or dependency version changed. The
Windows checker records and checks that exact pin and its real header hashes.

Existing Audio callback tests and their five controls, the nine-schedule/six-control
cohort/Memory/GPU owner composition (with its actual backend-stop prerequisite),
and whole-owner terminal disposal regression pass. The terminal fixture required
two existing renderer members to match current production; this changes only its
collaborators, not rendering behavior. Legacy Audio fixtures add the real
registration lock and link actual TerminalFailStop now used by Shutdown.

[Results, exact inputs/commands and cleanup](M4_P2_AUDIO_SERVICE_TARGET_DRAIN_RESULTS.json)
include final manifests and hashes. Full logs remain local in ignored diagnostics;
committed JSON travels with Git. Current-run temporary executables, objects and
generated fixture/shim headers are removed after resolved-boundary and hash checks.
Cleanup removed 125 generated files, including 47 executables; final result
manifests identify accepted cases separately from superseded experiment logs.
No application link, MSVC, native audio device, Vulkan, Linux/Android/game or
current-head CI acceptance is implied by these Windows object/fixture results.

## Coverage and continuation

This closes the reproduced **AudioIn/Out raw target terminal retirement** gap for
the Audio server owner path. The cold hook is installed before RunServer; its
configuration is not a concurrent runtime mutation API. Shutdown calls remain
serialized by the owning teardown thread, with concurrent registration tested.
System/AudioCore must outlive the server's drain callback; actual setup constructs
GPU, then AudioCore, then services, and terminal teardown closes services before
AudioCore destruction.

This is not whole Audio producer closure. Escaped service/client references,
independent lower-level Kernel teardown outside that owner contract, native
sink/DSP/backend work, other service writers and arbitrary raw aliases remain
separate obligations. LinkToManager's existing handling of setter receipts is
not upgraded into any guest admission authority. A stuck native wait still lacks
a bounded-progress guarantee; owners must not be freed merely because it is slow.
No reversible whole-Audio pause, new Memory policy or signal-handler waiting is
introduced. F2/F3 remain open and GuestBackingAdmission stays Unknown.

Keep the local repair freeze. Next coverage work is the existing NVDRV Module /
session / OwnedRetirementQueue -> dispatch -> Host1x queued operation / callback
chain, with actual last-use owners and a conservative Unknown boundary. Do not
expand into unrelated teardown suspicions without a reproduced critical failure.
