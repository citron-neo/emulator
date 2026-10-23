// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include "protocol_common.h"

namespace M3A {
struct OwnerToken {
    const Key key;
    const Id backing;
    std::atomic<bool> alive{true};
    std::atomic<Id> generation{};
    OwnerToken(Key k, Id epoch) : key{k}, backing{epoch} {}
};
struct Stamp {
    std::shared_ptr<OwnerToken> token;
    Id generation{};
};
struct AdmissionStats {
    std::size_t uses{}, mapping_uses{}, pending_mutators{}, waiters{}, pinned_uses{};
    bool sealed{};
};
class CompletionContract;
class MappingAdmission {
    struct Owner {
        std::shared_ptr<OwnerToken> token;
        std::shared_ptr<void> mapping_pin;
        bool sealed{}, unknown{}, active_mutator{};
        std::thread::id mutator_thread;
        std::size_t uses{};
        std::deque<Id> mutations;
    };
    struct Use {
        std::vector<Key> keys;
        std::vector<Stamp> stamps;
        std::vector<std::shared_ptr<void>> mapping_pins;
        std::shared_ptr<void> allocation_pin, representation_pin;
        std::vector<Range> physical;
        Id backing{};
        bool armed{}, pinned{}, validated{};
        std::thread::id creator;
    };
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        std::map<Key, Owner> owners;
        std::map<Id, Use> uses;
        std::map<Id, bool> backing_sealed;
        std::map<Id, std::size_t> backing_uses;
        std::shared_ptr<State> retained; // armed obligations outlive facade/RAII destruction
        Id next_use{1}, next_mutation{1};
        std::size_t waiters{};
        bool sealed{};
    };
    std::shared_ptr<State> state = std::make_shared<State>();
    static void Release(const std::shared_ptr<State>& state, Id id);
    static Error CheckOwners(const State&, const std::vector<Stamp>&, bool admission);

public:
    MappingAdmission() = default;
    MappingAdmission(const MappingAdmission&) = delete;
    MappingAdmission& operator=(const MappingAdmission&) = delete;
    class UseLease {
        std::shared_ptr<State> state;
        Id id{};
        friend class MappingAdmission;
        friend class CompletionContract;
        UseLease(std::shared_ptr<State> s, Id value) : state{std::move(s)}, id{value} {}
        Error Arm();
        void Retire(Retirement proof);
        void Fail();

    public:
        UseLease() = default;
        UseLease(const UseLease&) = delete;
        UseLease& operator=(const UseLease&) = delete;
        UseLease(UseLease&& other) noexcept;
        UseLease& operator=(UseLease&& other) noexcept;
        ~UseLease();
        Error Validate(const std::function<bool()>& resolver);
        Error PinPhysical(std::vector<Range>, std::shared_ptr<void> allocation,
                          std::shared_ptr<void> representation);
        std::vector<Key> Order() const;
        bool Valid() const {
            return id != 0;
        }
    };
    struct UseResult {
        Error error{};
        UseLease lease;
    };
    class Mutation {
        std::shared_ptr<State> state;
        Key key;
        Id id{};
        bool acquired{}, started{}, preserved{};
        friend class MappingAdmission;
        Mutation(std::shared_ptr<State> s, Key k, Id value)
            : state{std::move(s)}, key{k}, id{value} {}
        void Abandon();

    public:
        Mutation() = default;
        Mutation(const Mutation&) = delete;
        Mutation& operator=(const Mutation&) = delete;
        Mutation(Mutation&&) noexcept;
        ~Mutation();
        Error Wait();
        Error Preserve(const std::function<Boundary()>& callback);
        Error ConfirmPreserved(Boundary proof);
        Error ApplyPtes(const std::function<Id()>& mutation);
    };
    struct MutationResult {
        Error error{};
        Mutation mutation;
    };
    ~MappingAdmission();
    std::shared_ptr<OwnerToken> Register(Key key, Id backing, std::shared_ptr<void> mapping_pin);
    Stamp Snapshot(const std::shared_ptr<OwnerToken>& token) const;
    UseResult TryUse(std::vector<Stamp> owners);
    UseResult WaitUse(std::vector<Stamp> owners);
    MutationResult BeginMutation(const std::shared_ptr<OwnerToken>& owner);
    void SealOwner(const std::shared_ptr<OwnerToken>& owner);
    Error DestroyOwner(const std::shared_ptr<OwnerToken>& owner);
    void SealBacking(Id backing);
    Error DrainBacking(Id backing);
    void Seal();
    AdmissionStats Stats() const;
    void WaitStats(const std::function<bool(AdmissionStats)>& predicate);
};
} // namespace M3A
