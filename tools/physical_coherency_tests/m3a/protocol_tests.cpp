// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <future>
#include <iostream>
#include <latch>
#include "completion_contract.h"
#include "controlled_cpu_access.h"

using namespace M3A;
void Require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void Is(Error actual, Error expected = Error::None) {
    Require(actual == expected, "unexpected protocol error");
}
template <class T>
T Get(std::future<T>& future) {
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        throw std::runtime_error("deterministic rendezvous timed out");
    return future.get();
}
struct Ptes {
    std::mutex gpu_mutex, device_mutex;
    Id gpu_generation{}, device_generation{}, physical{64};
};
struct Fixture {
    MappingAdmission gate;
    std::shared_ptr<Ptes> ptes = std::make_shared<Ptes>();
    std::shared_ptr<OwnerToken> gpu = gate.Register({Role::GPU, 1}, 1, ptes);
    std::shared_ptr<OwnerToken> device = gate.Register({Role::Device, 1}, 1, ptes);
    CompletionContract completion;
    MappingAdmission::UseLease Reserve() {
        const auto g = gate.Snapshot(gpu), d = gate.Snapshot(device);
        auto result = gate.TryUse({d, g});
        Is(result.error);
        Is(result.lease.Validate([&] {
            PteLock a{ptes->gpu_mutex, gpu->key};
            PteLock b{ptes->device_mutex, device->key};
            return ptes->gpu_generation == g.generation && ptes->device_generation == d.generation;
        }));
        Require(result.lease.Order() == std::vector<Key>{gpu->key, device->key},
                "GPU->device ordering not canonical");
        return std::move(result.lease);
    }
    Id Accept() {
        auto result = completion.Accept(Reserve());
        Is(result.error);
        return result.id;
    }
    void RecordSubmit(Id id, Submission ticket = {1, 0, 9}) {
        Is(completion.CallbackStart(id));
        Is(completion.Record(id));
        Is(completion.CallbackDone(id));
        Is(completion.RequestSubmit(id, ticket));
        Is(completion.AcknowledgeSubmission(id, ticket));
    }
    void Finish(Id id, Submission ticket = {1, 0, 9}) {
        Is(completion.AcknowledgeCompletion(id, ticket));
        Is(completion.AcknowledgeVisibility(id));
    }
};
void ReaderThenMutation() {
    Fixture f;
    const Id work = f.Accept();
    auto mutation = f.gate.BeginMutation(f.gpu);
    Is(mutation.error);
    std::latch callback_started{1}, allow_callback{1};
    auto mutator = std::async(std::launch::async, [&] {
        Is(mutation.mutation.Wait());
        Is(mutation.mutation.Preserve([&] {
            Require(f.gate.TryUse({f.gate.Snapshot(f.gpu), f.gate.Snapshot(f.device)}).error ==
                        Error::Reentrant,
                    "preservation admitted new use");
            Require(context.metadata == 0 && context.ptes.empty(), "callback retained lock");
            callback_started.count_down();
            allow_callback.wait();
            PteLock pure{f.ptes->gpu_mutex, f.gpu->key};
            Require(f.ptes->physical == 64, "old mapping lost before callback");
            return Boundary::NoWork;
        }));
        return mutation.mutation.ApplyPtes([&] {
            PteLock lock{f.ptes->gpu_mutex, f.gpu->key};
            f.ptes->physical = 128;
            return f.ptes->gpu_generation += 2;
        });
    });
    f.gate.WaitStats([](auto s) { return s.waiters == 1; });
    Is(f.gate.TryUse({f.gate.Snapshot(f.gpu), f.gate.Snapshot(f.device)}).error, Error::Blocked);
    f.RecordSubmit(work);
    Require(f.gate.Stats().uses == 1, "submission retired mapping use");
    f.Finish(work);
    callback_started.wait();
    auto stamp = f.gate.Snapshot(f.gpu);
    auto reader = std::async(std::launch::async, [&] {
        return f.gate.WaitUse({stamp, f.gate.Snapshot(f.device)}).error;
    });
    f.gate.WaitStats([](auto s) { return s.waiters == 1; });
    allow_callback.count_down();
    Is(Get(mutator));
    Is(Get(reader), Error::Stale);
    auto next = f.Reserve();
    Require(f.ptes->physical == 128, "reader didn't observe completed mapping update");
}
void IntentAndFIFO() {
    Fixture f;
    auto first = f.gate.BeginMutation(f.device), second = f.gate.BeginMutation(f.device);
    Is(first.error);
    Is(second.error);
    Is(f.gate.TryUse({f.gate.Snapshot(f.gpu), f.gate.Snapshot(f.device)}).error, Error::Blocked);
    auto waiter = std::async(std::launch::async, [&] {
        Is(second.mutation.Wait());
        Is(second.mutation.Preserve([] { return Boundary::NoWork; }));
        return second.mutation.ApplyPtes([&] {
            PteLock lock{f.ptes->device_mutex, f.device->key};
            return f.ptes->device_generation += 2;
        });
    });
    f.gate.WaitStats([](auto s) { return s.waiters == 1 && s.pending_mutators == 2; });
    Is(first.mutation.Wait());
    Is(first.mutation.Preserve([] { return Boundary::NoWork; }));
    Is(first.mutation.ApplyPtes([&] {
        PteLock lock{f.ptes->device_mutex, f.device->key};
        return f.ptes->device_generation += 2;
    }));
    Is(Get(waiter));
    Require(f.gate.Snapshot(f.device).generation == 4, "FIFO mutators lost generation");
    std::vector<MappingAdmission::UseLease> readers;
    for (int i = 0; i < 8; ++i)
        readers.push_back(f.Reserve());
    Require(f.gate.Stats().uses == 8, "multiple readers not admitted");
    readers.clear();
    Require(f.gate.Stats().uses == 0, "unarmed readers leaked");
}
void StampsAndSealing() {
    Fixture f;
    auto stale = f.gate.Snapshot(f.gpu);
    auto mutation = f.gate.BeginMutation(f.gpu);
    Is(mutation.error);
    Is(mutation.mutation.Wait());
    Is(mutation.mutation.Preserve([] { return Boundary::NoWork; }));
    Is(mutation.mutation.ApplyPtes([&] {
        PteLock lock{f.ptes->gpu_mutex, f.gpu->key};
        return f.ptes->gpu_generation += 2;
    }));
    Is(f.gate.TryUse({stale, f.gate.Snapshot(f.device)}).error, Error::Stale);
    auto intent = f.gate.BeginMutation(f.gpu);
    Is(intent.error);
    auto reader = std::async(std::launch::async, [&] {
        return f.gate.WaitUse({f.gate.Snapshot(f.gpu), f.gate.Snapshot(f.device)}).error;
    });
    f.gate.WaitStats([](auto s) { return s.waiters == 1; });
    f.gate.Seal();
    Is(Get(reader), Error::Sealed);
    Is(intent.mutation.Wait(), Error::Sealed);
    Is(f.gate.TryUse({f.gate.Snapshot(f.gpu), f.gate.Snapshot(f.device)}).error, Error::Sealed);
}
void CallbackAndLockChecks() {
    {
        Fixture nested;
        auto first = nested.gate.BeginMutation(nested.gpu);
        Is(first.mutation.Wait());
        Is(nested.gate.WaitUse({nested.gate.Snapshot(nested.gpu)}).error, Error::SelfWait);
        auto second = nested.gate.BeginMutation(nested.gpu);
        Is(second.mutation.Wait(), Error::SelfWait);
        Is(nested.gate.DestroyOwner(nested.gpu), Error::SelfWait);
    }
    Fixture f;
    auto use = f.Reserve();
    auto intent = f.gate.BeginMutation(f.gpu);
    Is(intent.error);
    Is(intent.mutation.Wait(), Error::SelfWait);
    {
        GpuContext gpu;
        Is(f.gate.WaitUse({f.gate.Snapshot(f.gpu), f.gate.Snapshot(f.device)}).error,
           Error::SelfWait);
    }
    use = MappingAdmission::UseLease{};
    Is(intent.mutation.Wait());
    Is(intent.mutation.Preserve([&] {
        Is(f.gate.BeginMutation(f.device).error, Error::Reentrant);
        Is(f.gate.WaitUse({f.gate.Snapshot(f.gpu)}).error, Error::Reentrant);
        return Boundary::CallbackOnly;
    }),
       Error::Pending);
    bool applied = false;
    Is(intent.mutation.ApplyPtes([&] {
        applied = true;
        return Id{2};
    }),
       Error::Pending);
    Require(!applied, "callback return permitted PTE mutation before completion");
    Is(intent.mutation.ConfirmPreserved(Boundary::Visible));
    Is(intent.mutation.ApplyPtes([&] {
        PteLock lock{f.ptes->gpu_mutex, f.gpu->key};
        return f.ptes->gpu_generation += 2;
    }));
    const auto current = f.gate.Snapshot(f.gpu);
    {
        PteLock a{f.ptes->gpu_mutex, f.gpu->key};
        Is(f.gate.TryUse({current}).error, Error::LockOrder);
        Is(f.completion.Wait(0, Boundary::Completed), Error::LockOrder);
    }
    bool inverted = false;
    try {
        PteLock d{f.ptes->device_mutex, f.device->key};
        PteLock g{f.ptes->gpu_mutex, f.gpu->key};
    } catch (const std::logic_error&) {
        inverted = true;
    }
    Require(inverted, "reversed device->GPU lock accepted");
    bool metadata_to_pte = false;
    try {
        MetadataMark mark;
        PteLock lock{f.ptes->gpu_mutex, f.gpu->key};
    } catch (const std::logic_error&) {
        metadata_to_pte = true;
    }
    Require(metadata_to_pte, "metadata->PTE lock accepted");
}
void DestructionAndPins() {
    {
        MappingAdmission lifetime_gate;
        bool cleanup_outside_metadata = false;
        auto pin = std::shared_ptr<int>(new int{1}, [&](int* value) {
            cleanup_outside_metadata = context.metadata == 0 && lifetime_gate.Stats().uses == 0;
            delete value;
        });
        auto owner = lifetime_gate.Register({Role::GPU, 1}, 1, pin);
        pin.reset();
        Is(lifetime_gate.DestroyOwner(owner));
        Require(cleanup_outside_metadata, "mapping pin cleanup retained metadata lock");
    }
    Fixture f;
    const auto token = f.gpu;
    const auto work = f.Accept();
    std::weak_ptr<Ptes> lifetime = f.ptes;
    f.ptes.reset(); // pins, not a hidden fixture reference, must retain actual PTE storage
    auto destroy = std::async(std::launch::async, [&] { return f.gate.DestroyOwner(f.gpu); });
    auto destroy_device =
        std::async(std::launch::async, [&] { return f.gate.DestroyOwner(f.device); });
    f.gate.WaitStats([](auto s) { return s.waiters == 2; });
    Require(!lifetime.expired(), "manager resources died during admitted use");
    Is(f.gate.TryUse({f.gate.Snapshot(f.gpu)}).error, Error::Sealed);
    f.RecordSubmit(work);
    f.Finish(work);
    Is(Get(destroy));
    Is(Get(destroy_device));
    Require(lifetime.expired(), "drained manager resource pins not released");
    Require(!token->alive.load(), "retained identity not invalidated");
    Is(f.gate.TryUse({f.gate.Snapshot(token)}).error, Error::Dead);
    // Physical pins retain allocation/representation, not manager/PTE resources.
    Fixture p;
    auto lease = p.Reserve();
    bool cleanup_outside_metadata = false;
    auto allocation = std::shared_ptr<int>(new int{1}, [&](int* value) {
        cleanup_outside_metadata = context.metadata == 0 && p.gate.Stats().uses == 0;
        delete value;
    });
    auto rep = std::make_shared<int>(2);
    std::weak_ptr<int> allocation_life = allocation, rep_life = rep;
    Is(lease.PinPhysical({{1, 64, 16}}, allocation, rep));
    allocation.reset();
    rep.reset();
    Is(p.gate.DestroyOwner(p.gpu));
    auto accepted = p.completion.Accept(std::move(lease));
    Is(accepted.error);
    Require(p.gate.Stats().mapping_uses == 0 && p.gate.Stats().pinned_uses == 1,
            "physical use still blocked mapping");
    p.RecordSubmit(accepted.id);
    Require(!allocation_life.expired() && !rep_life.expired(), "physical pins missing");
    p.Finish(accepted.id);
    Require(allocation_life.expired() && rep_life.expired(),
            "physical pins not released at boundary");
    Require(cleanup_outside_metadata, "physical pin cleanup retained metadata lock");
}
void EpochDrain() {
    {
        Fixture mutating;
        auto intent = mutating.gate.BeginMutation(mutating.gpu);
        std::latch callback_started{1}, allow_callback{1};
        auto update = std::async(std::launch::async, [&] {
            Is(intent.mutation.Wait());
            Is(intent.mutation.Preserve([&] {
                callback_started.count_down();
                allow_callback.wait();
                return Boundary::NoWork;
            }));
            return intent.mutation.ApplyPtes([&] {
                PteLock lock{mutating.ptes->gpu_mutex, mutating.gpu->key};
                return mutating.ptes->gpu_generation += 2;
            });
        });
        callback_started.wait();
        auto drain = std::async(std::launch::async, [&] { return mutating.gate.DrainBacking(1); });
        mutating.gate.WaitStats([](auto s) { return s.waiters == 1; });
        Require(mutating.gate.Stats().pending_mutators == 1, "backing drain lost active mutation");
        allow_callback.count_down();
        Is(Get(update), Error::Sealed);
        Is(Get(drain));
        Is(mutating.gate.TryUse({mutating.gate.Snapshot(mutating.gpu)}).error, Error::Sealed);
    }
    Fixture f;
    auto lease = f.Reserve();
    auto allocation = std::make_shared<int>(1), rep = std::make_shared<int>(2);
    Is(lease.PinPhysical({{1, 64, 16}}, allocation, rep));
    auto accepted = f.completion.Accept(std::move(lease));
    Is(accepted.error);
    auto drain = std::async(std::launch::async, [&] { return f.gate.DrainBacking(1); });
    f.gate.WaitStats([](auto s) { return s.waiters == 1; });
    Is(f.gate.TryUse({f.gate.Snapshot(f.gpu)}).error, Error::Sealed);
    f.RecordSubmit(accepted.id);
    f.Finish(accepted.id);
    Is(Get(drain));
    auto new_owner = f.gate.Register({Role::GPU, 2}, 2, std::make_shared<int>(7));
    auto next = f.gate.TryUse({f.gate.Snapshot(new_owner)});
    Is(next.error);
    Is(next.lease.Validate([] { return true; }));
}
void Milestones() {
    Fixture f;
    const Id id = f.Accept();
    Is(f.completion.CallbackStart(id));
    Is(f.completion.CallbackDone(id));
    Require(f.completion.Inspect(id).callback_done && !f.completion.Inspect(id).recorded,
            "CPU callback became command recording");
    {
        GpuContext gpu;
        Is(f.completion.Wait(id, Boundary::Completed), Error::SelfWait);
    }
    Is(f.completion.CancelUnsubmitted(id, false), Error::Invalid);
    Is(f.completion.CancelUnsubmitted(id, true));
    const auto next = f.Accept();
    Is(f.completion.CallbackStart(next));
    Is(f.completion.Record(next));
    Is(f.completion.CallbackDone(next));
    auto wait =
        std::async(std::launch::async, [&] { return f.completion.Wait(next, Boundary::Visible); });
    f.completion.WaitForWaiters(1);
    const Submission ticket{4, 2, 17};
    Is(f.completion.RequestSubmit(next, ticket));
    Is(f.completion.AcknowledgeCompletion(next, ticket), Error::Invalid);
    Is(f.completion.AcknowledgeSubmission(next, ticket));
    Is(f.completion.AcknowledgeCompletion(next, {5, 2, 17}), Error::Invalid);
    Is(f.completion.AcknowledgeCompletion(next, {4, 3, 1000}), Error::Invalid);
    Is(f.completion.AcknowledgeCompletion(next, {4, 2, 18}), Error::Invalid);
    Require(!f.completion.Inspect(next).completed && f.gate.Stats().uses == 1,
            "unrelated queue tick completed use");
    Is(f.completion.AcknowledgeCompletion(next, ticket));
    Require(f.gate.Stats().uses == 1, "completion assumed CPU visibility");
    Is(f.completion.AcknowledgeVisibility(next));
    Is(Get(wait));
    Require(f.gate.Stats().uses == 0, "visibility did not retire use");
}
void Failures() {
    for (unsigned stage = 0; stage < 4; ++stage) {
        Fixture f;
        const auto id = f.Accept();
        Is(f.completion.CallbackStart(id));
        Is(f.completion.Record(id));
        Is(f.completion.CallbackDone(id));
        const Submission ticket{1, 0, 9};
        Is(f.completion.RequestSubmit(id, ticket));
        if (stage)
            Is(f.completion.AcknowledgeSubmission(id, ticket));
        if (stage == 2)
            Is(f.completion.AcknowledgeCompletion(id, ticket));
        auto expected = stage == 3 ? Error::DeviceLost : Error::Unknown;
        Is(stage == 0   ? f.completion.SubmissionFailed(id)
           : stage == 1 ? f.completion.CompletionFailed(id)
           : stage == 2 ? f.completion.VisibilityFailed(id)
                        : f.completion.DeviceLost(id),
           expected);
        Require(f.gate.Stats().uses == 1, "failure retired unsafe obligation");
        Is(f.completion.CancelQueued(id), Error::Invalid);
        Is(f.completion.Wait(id, Boundary::Visible), expected);
        Is(f.gate.TryUse({f.gate.Snapshot(f.gpu)}).error, Error::Unknown);
        Is(f.completion.AcknowledgeDeviceQuiescent(id));
        Require(f.gate.Stats().uses == 0 && !f.completion.Inspect(id).visible,
                "quiescent cleanup pretended data visible");
    }
    Fixture f;
    const auto queued = f.Accept(), started = f.Accept();
    Is(f.completion.CallbackStart(started));
    f.completion.Shutdown();
    Is(f.completion.Wait(queued, Boundary::CallbackOnly), Error::Shutdown);
    Is(f.completion.Wait(started, Boundary::Completed), Error::Unknown);
    Require(f.gate.Stats().uses == 1, "shutdown freed started work");
    Is(f.completion.AcknowledgeDeviceQuiescent(started));
}
void CpuBoundaries() {
    ControlledCPU cpu(std::vector<std::uint8_t>(64, 10));
    {
        auto result = cpu.TryAcquire({1, 0, 16}, true);
        Is(result.error);
        Is(result.lease.PreStore());
        Require(cpu.PublishedCommits() == 0, "pre-store published modification");
    }
    Require(cpu.PublishedCommits() == 0, "abandoned unexecuted write published version");
    auto writer = cpu.TryAcquire({1, 0, 16}, true);
    Is(writer.error);
    Is(writer.lease.PreStore());
    Is(writer.lease.Store(4, {77, 77, 77, 77}));
    Require(cpu.PublishedCommits() == 0, "actual store auto-committed");
    Is(cpu.TryAcquire({1, 0, 16}, false).error, Error::Blocked);
    auto commit = writer.lease.Commit();
    Is(commit.first);
    Require(commit.second.modified == std::vector<Range>{{1, 4, 4}} && cpu.PublishedCommits() == 1,
            "post-store byte extent wrong");
    auto reader = cpu.TryAcquire({1, 0, 16}, false);
    Is(reader.error);
    auto bytes = reader.lease.Read();
    Require(bytes[0] == 10 && bytes[4] == 77 && bytes[8] == 10, "controlled store bytes incorrect");
    Is(reader.lease.CloseRead());
    {
        auto bad = cpu.TryAcquire({1, 16, 16}, true);
        Is(bad.error);
        Is(bad.lease.PreStore());
        Is(bad.lease.Store(0, {99, 99, 99, 99}, 2), Error::Unknown);
        Is(bad.lease.Commit().first, Error::Unknown);
        Is(bad.lease.Cancel(), Error::Invalid);
    }
    Is(cpu.TryAcquire({1, 16, 4}, false).error, Error::Unknown);
    auto disjoint = cpu.TryAcquire({1, 48, 8}, false);
    Is(disjoint.error);
    Is(disjoint.lease.CloseRead());
    Require(cpu.PublishedCommits() == 1, "unknown partial store published version");
    Is(cpu.Drain(), Error::Unknown);
    Is(cpu.TryAcquire({1, 0, 4}, true, false).error, Error::Invalid);
}
void CpuConcurrency() {
    ControlledCPU cpu(std::vector<std::uint8_t>(64, 10));
    auto a = cpu.TryAcquire({1, 0, 16}, false), b = cpu.TryAcquire({1, 0, 16}, false);
    Is(a.error);
    Is(b.error);
    auto wait = std::async(std::launch::async, [&] {
        auto writer = cpu.WaitAcquire({1, 4, 4}, true);
        Is(writer.error);
        Is(writer.lease.PreStore());
        Is(writer.lease.Store(0, {77, 77, 77, 77}));
        return writer.lease.Commit().first;
    });
    cpu.WaitForWaiters(1);
    auto disjoint = cpu.TryAcquire({1, 32, 16}, true);
    Is(disjoint.error);
    Is(disjoint.lease.PreStore());
    Is(disjoint.lease.Store(0, std::vector<std::uint8_t>(16, 99)));
    Is(disjoint.lease.Commit().first);
    Is(a.lease.CloseRead());
    Is(b.lease.CloseRead());
    Is(Get(wait));
    Require(cpu.PublishedCommits() == 2, "disjoint/conflicting completion lost commit");
    auto held = cpu.TryAcquire({1, 0, 4}, true);
    Is(held.error);
    Is(cpu.WaitAcquire({1, 0, 4}, false).error, Error::SelfWait);
    auto shutdown =
        std::async(std::launch::async, [&] { return cpu.WaitAcquire({1, 0, 4}, true).error; });
    cpu.WaitForWaiters(1);
    cpu.Seal();
    Is(Get(shutdown), Error::Sealed);
    Is(held.lease.Cancel());
    Is(cpu.Drain());
}
int main() {
    try {
        ReaderThenMutation();
        std::cout << "PASS 1/3/4 admitted use precedes callback/PTE mutation; new readers closed\n";
        IntentAndFIFO();
        std::cout << "PASS 2/5 mutation intent and FIFO multi-mutator/multi-reader ordering\n";
        StampsAndSealing();
        std::cout << "PASS 6/8 stale stamps and pending shutdown requests\n";
        CallbackAndLockChecks();
        std::cout << "PASS 9/10 callback reentrancy, PTE lock order and self-wait prevention\n";
        DestructionAndPins();
        std::cout
            << "PASS 7 manager destruction, inert tokens and distinct physical resource pins\n";
        EpochDrain();
        std::cout << "PASS 20 backing replacement drains mapping-dependent and pinned uses\n";
        Milestones();
        std::cout << "PASS 12/13 callback/record/submit/complete/visible milestones and exact "
                     "submission identity\n";
        Failures();
        std::cout << "PASS 11/14/15 shutdown drop and unknown/device-loss retained obligations\n";
        CpuBoundaries();
        std::cout << "PASS 16/17 controlled pre/post store and partial uncertainty\n";
        CpuConcurrency();
        std::cout << "PASS 18/19 conflicting/shared/disjoint CPU ranges and shutdown\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL M3A: " << e.what() << '\n';
        return 1;
    }
}
