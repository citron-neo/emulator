// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <new>
#include <utility>
#include "mapping_admission.h"

namespace M3A {
Error MappingAdmission::CheckOwners(const State& s, const std::vector<Stamp>& stamps,
                                    bool admission) {
    if (s.sealed)
        return Error::Sealed;
    for (const auto& stamp : stamps) {
        if (!stamp.token)
            return Error::Invalid;
        auto it = s.owners.find(stamp.token->key);
        if (it == s.owners.end() || it->second.token != stamp.token || !stamp.token->alive)
            return Error::Dead;
        const auto& owner = it->second;
        if (owner.unknown)
            return Error::Unknown;
        if (owner.sealed || s.backing_sealed.at(stamp.token->backing))
            return Error::Sealed;
        if (stamp.generation != stamp.token->generation || (stamp.generation & 1))
            return Error::Stale;
        if (admission && !owner.mutations.empty())
            return Error::Blocked;
    }
    return Error::None;
}
MappingAdmission::~MappingAdmission() {
    Seal();
    std::lock_guard lock{state->mutex};
    for (auto& [key, owner] : state->owners) {
        (void)key;
        owner.token->alive = false;
    }
}
std::shared_ptr<OwnerToken> MappingAdmission::Register(Key key, Id backing,
                                                       std::shared_ptr<void> pin) {
    if (EntryCheck() != Error::None || !pin || !backing)
        throw std::invalid_argument("owner requires controlled context/pin");
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    if (state->sealed || state->owners.contains(key))
        throw std::invalid_argument("sealed/duplicate owner");
    auto token = std::make_shared<OwnerToken>(key, backing);
    Owner owner;
    owner.token = token;
    owner.mapping_pin = std::move(pin);
    state->owners.emplace(key, std::move(owner));
    state->backing_sealed.try_emplace(backing, false);
    state->backing_uses.try_emplace(backing, 0);
    return token;
}
Stamp MappingAdmission::Snapshot(const std::shared_ptr<OwnerToken>& token) const {
    if (EntryCheck(false) != Error::None)
        throw std::logic_error("snapshot under PTE/metadata lock");
    std::lock_guard lock{state->mutex};
    return {token, token->generation};
}
MappingAdmission::UseResult MappingAdmission::TryUse(std::vector<Stamp> stamps) {
    if (auto e = EntryCheck(); e != Error::None)
        return {e, {}};
    if (stamps.empty())
        return {Error::Invalid, {}};
    if (std::any_of(stamps.begin(), stamps.end(), [](const auto& x) { return !x.token; }))
        return {Error::Invalid, {}};
    std::sort(stamps.begin(), stamps.end(),
              [](const auto& a, const auto& b) { return a.token->key < b.token->key; });
    for (std::size_t i = 1; i < stamps.size(); ++i)
        if (stamps[i - 1].token == stamps[i].token)
            return {Error::Invalid, {}};
    try {
        std::lock_guard lock{state->mutex};
        MetadataMark mark;
        if (auto e = CheckOwners(*state, stamps, true); e != Error::None)
            return {e, {}};
        const Id backing = stamps.front().token->backing;
        Use use;
        use.stamps = stamps;
        use.backing = backing;
        use.creator = std::this_thread::get_id();
        for (const auto& stamp : stamps) {
            if (stamp.token->backing != backing)
                return {Error::Invalid, {}};
            use.keys.push_back(stamp.token->key);
            use.mapping_pins.push_back(state->owners.at(stamp.token->key).mapping_pin);
        }
        const Id id = state->next_use;
        state->uses.emplace(id, std::move(use)); // allocate BEFORE incrementing reservations
        ++state->next_use;
        ++state->backing_uses.at(backing);
        for (const auto& stamp : stamps)
            ++state->owners.at(stamp.token->key).uses;
        state->cv.notify_all();
        return {Error::None, UseLease{state, id}};
    } catch (const std::bad_alloc&) {
        return {Error::Allocation, {}};
    }
}
MappingAdmission::UseResult MappingAdmission::WaitUse(std::vector<Stamp> stamps) {
    if (auto e = EntryCheck(); e != Error::None)
        return {e, {}};
    for (;;) {
        auto result = TryUse(stamps);
        if (result.error != Error::Blocked)
            return result;
        if (context.gpu)
            return {Error::SelfWait, {}};
        std::unique_lock lock{state->mutex};
        MetadataMark mark;
        for (const auto& stamp : stamps) {
            const auto& owner = state->owners.at(stamp.token->key);
            if (owner.active_mutator && owner.mutator_thread == std::this_thread::get_id())
                return {Error::SelfWait, {}};
        }
        for (const auto& [id, use] : state->uses) {
            (void)id;
            if (!use.armed && use.creator == std::this_thread::get_id())
                for (const auto& stamp : stamps)
                    if (std::find(use.keys.begin(), use.keys.end(), stamp.token->key) !=
                        use.keys.end())
                        return {Error::SelfWait, {}};
        }
        ++state->waiters;
        state->cv.notify_all();
        state->cv.wait(lock, [&] { return CheckOwners(*state, stamps, true) != Error::Blocked; });
        --state->waiters;
        state->cv.notify_all();
    }
}
MappingAdmission::UseLease::UseLease(UseLease&& other) noexcept
    : state{std::move(other.state)}, id{std::exchange(other.id, 0)} {}
MappingAdmission::UseLease& MappingAdmission::UseLease::operator=(UseLease&& other) noexcept {
    if (this != &other) {
        this->~UseLease();
        new (this) UseLease(std::move(other));
    }
    return *this;
}
MappingAdmission::UseLease::~UseLease() {
    if (!id)
        return;
    bool unarmed;
    {
        std::lock_guard lock{state->mutex};
        unarmed = !state->uses.at(id).armed;
    }
    if (unarmed)
        Release(state, id); // an armed token is NEVER silently retired by RAII
}
void MappingAdmission::Release(const std::shared_ptr<State>& s, Id id) {
    Use retired;
    {
        std::lock_guard lock{s->mutex};
        MetadataMark mark;
        auto it = s->uses.find(id);
        if (it == s->uses.end())
            return;
        if (!it->second.pinned)
            for (auto key : it->second.keys)
                --s->owners.at(key).uses;
        --s->backing_uses.at(it->second.backing);
        retired = std::move(it->second);
        s->uses.erase(it); // release resource pins only after metadata unlock
        if (std::none_of(s->uses.begin(), s->uses.end(),
                         [](const auto& x) { return x.second.armed; }))
            s->retained.reset();
        s->cv.notify_all();
    }
}
Error MappingAdmission::UseLease::Validate(const std::function<bool()>& resolver) {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    if (!id)
        return Error::Invalid;
    {
        std::lock_guard lock{state->mutex};
        MetadataMark mark;
        const auto& use = state->uses.at(id);
        if (use.pinned || use.armed || use.validated)
            return Error::Invalid;
        if (auto e = CheckOwners(*state, use.stamps, false); e != Error::None)
            return e;
    }
    // Existing use counts close the TOCTOU window; no metadata->PTE nesting.
    bool valid = false;
    try {
        valid = resolver();
    } catch (...) {
        return Error::Unknown;
    }
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    auto& use = state->uses.at(id);
    if (auto e = CheckOwners(*state, use.stamps, false); e != Error::None)
        return e;
    if (!valid)
        return Error::Stale;
    use.validated = true;
    return Error::None;
}
Error MappingAdmission::UseLease::Arm() {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    auto& use = state->uses.at(id);
    if (!use.validated)
        return Error::Stale;
    if (use.pinned) {
        if (state->sealed || state->backing_sealed.at(use.backing))
            return Error::Sealed;
    } else if (auto e = CheckOwners(*state, use.stamps, false); e != Error::None)
        return e;
    use.armed = true;
    state->retained = state;
    return Error::None;
}
void MappingAdmission::UseLease::Retire(Retirement) {
    if (id) {
        Release(state, id);
        id = 0;
    }
}
void MappingAdmission::UseLease::Fail() {
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    const auto& use = state->uses.at(id);
    state->backing_sealed.at(use.backing) = true;
    for (auto key : use.keys) {
        auto& owner = state->owners.at(key);
        owner.sealed = true;
        owner.unknown = true;
    }
    state->cv.notify_all();
}
Error MappingAdmission::UseLease::PinPhysical(std::vector<Range> physical,
                                              std::shared_ptr<void> allocation,
                                              std::shared_ptr<void> representation) {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    if (!id || !allocation || !representation || physical.empty())
        return Error::Invalid;
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    auto& use = state->uses.at(id);
    if (!use.validated || use.pinned || use.armed)
        return Error::Invalid;
    for (auto r : physical)
        if (r.epoch != use.backing || !r.size || r.size > ~Id{0} - r.offset)
            return Error::Invalid;
    use.physical = std::move(physical);
    use.allocation_pin = std::move(allocation);
    use.representation_pin = std::move(representation);
    for (auto key : use.keys)
        --state->owners.at(key).uses;
    use.mapping_pins.clear();
    use.pinned = true;
    state->cv.notify_all();
    return Error::None;
}
std::vector<Key> MappingAdmission::UseLease::Order() const {
    std::lock_guard lock{state->mutex};
    return state->uses.at(id).keys;
}
MappingAdmission::MutationResult MappingAdmission::BeginMutation(
    const std::shared_ptr<OwnerToken>& token) {
    if (auto e = EntryCheck(); e != Error::None)
        return {e, {}};
    if (!token)
        return {Error::Invalid, {}};
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    auto it = state->owners.find(token->key);
    if (it == state->owners.end() || it->second.token != token || !token->alive)
        return {Error::Dead, {}};
    auto& owner = it->second;
    if (owner.unknown)
        return {Error::Unknown, {}};
    if (state->sealed || owner.sealed || state->backing_sealed.at(token->backing))
        return {Error::Sealed, {}};
    const Id id = state->next_mutation;
    owner.mutations.push_back(id);
    ++state->next_mutation; // Intent closes admission immediately.
    state->cv.notify_all();
    return {Error::None, Mutation{state, token->key, id}};
}
MappingAdmission::Mutation::Mutation(Mutation&& other) noexcept
    : state{std::move(other.state)}, key{other.key}, id{std::exchange(other.id, 0)},
      acquired{other.acquired}, started{other.started}, preserved{other.preserved} {}
MappingAdmission::Mutation::~Mutation() {
    Abandon();
}
void MappingAdmission::Mutation::Abandon() {
    if (!id)
        return;
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    auto& owner = state->owners.at(key);
    if (started) {
        owner.sealed = true;
        owner.unknown = true;
    }
    if (acquired)
        owner.active_mutator = false;
    auto it = std::find(owner.mutations.begin(), owner.mutations.end(), id);
    if (it != owner.mutations.end())
        owner.mutations.erase(it);
    id = 0;
    state->cv.notify_all();
}
Error MappingAdmission::Mutation::Wait() {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    if (!id || acquired)
        return Error::Invalid;
    std::unique_lock lock{state->mutex};
    MetadataMark mark;
    auto& owner = state->owners.at(key);
    const auto ready = [&] {
        return state->sealed || owner.sealed || owner.unknown ||
               (!owner.mutations.empty() && owner.mutations.front() == id && !owner.uses);
    };
    if (!ready()) {
        if (owner.active_mutator && owner.mutator_thread == std::this_thread::get_id())
            return Error::SelfWait;
        if (context.gpu)
            return Error::SelfWait;
        for (const auto& [use_id, use] : state->uses) {
            (void)use_id;
            if (!use.armed && !use.pinned && use.creator == std::this_thread::get_id() &&
                std::find(use.keys.begin(), use.keys.end(), key) != use.keys.end())
                return Error::SelfWait;
        }
        ++state->waiters;
        state->cv.notify_all();
        state->cv.wait(lock, ready);
        --state->waiters;
    }
    if (owner.unknown)
        return Error::Unknown;
    if (state->sealed || owner.sealed)
        return Error::Sealed;
    acquired = true;
    owner.active_mutator = true;
    owner.mutator_thread = std::this_thread::get_id();
    state->cv.notify_all();
    return Error::None;
}
Error MappingAdmission::Mutation::Preserve(const std::function<Boundary()>& callback) {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    if (!acquired || started)
        return Error::Invalid;
    started = true;
    Boundary proof = Boundary::Unknown;
    try {
        CallbackScope scope;
        proof = callback();
    } catch (...) {
        proof = Boundary::Unknown;
    }
    return ConfirmPreserved(proof);
}
Error MappingAdmission::Mutation::ConfirmPreserved(Boundary proof) {
    if (!acquired || !started)
        return Error::Invalid;
    if (proof == Boundary::Visible || proof == Boundary::NoWork) {
        preserved = true;
        return Error::None;
    }
    if (proof != Boundary::Unknown)
        return Error::Pending;
    std::lock_guard lock{state->mutex};
    auto& owner = state->owners.at(key);
    owner.sealed = true;
    owner.unknown = true;
    state->cv.notify_all();
    return Error::Unknown;
}
Error MappingAdmission::Mutation::ApplyPtes(const std::function<Id()>& function) {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    if (!preserved)
        return Error::Pending;
    Id generation{};
    try {
        generation = function();
    } catch (...) {
        return ConfirmPreserved(Boundary::Unknown);
    }
    std::lock_guard lock{state->mutex};
    MetadataMark mark;
    auto& owner = state->owners.at(key);
    if (generation <= owner.token->generation || (generation & 1)) {
        owner.unknown = true;
        owner.sealed = true;
        state->cv.notify_all();
        return Error::Unknown;
    }
    owner.token->generation = generation;
    owner.active_mutator = false;
    owner.mutations.pop_front();
    id = 0;
    state->cv.notify_all();
    return state->sealed || owner.sealed ? Error::Sealed : Error::None;
}
void MappingAdmission::SealOwner(const std::shared_ptr<OwnerToken>& token) {
    std::lock_guard lock{state->mutex};
    state->owners.at(token->key).sealed = true;
    state->cv.notify_all();
}
Error MappingAdmission::DestroyOwner(const std::shared_ptr<OwnerToken>& token) {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    SealOwner(token);
    std::shared_ptr<void> retired_pin;
    {
        std::unique_lock lock{state->mutex};
        MetadataMark mark;
        auto& owner = state->owners.at(token->key);
        if (owner.active_mutator && owner.mutator_thread == std::this_thread::get_id())
            return Error::SelfWait;
        for (const auto& [id, use] : state->uses) {
            (void)id;
            if (!use.armed && !use.pinned && use.creator == std::this_thread::get_id() &&
                std::find(use.keys.begin(), use.keys.end(), token->key) != use.keys.end())
                return Error::SelfWait;
        }
        if (context.gpu && (owner.uses || owner.active_mutator))
            return Error::SelfWait;
        ++state->waiters;
        state->cv.notify_all();
        state->cv.wait(lock, [&] { return !owner.uses && !owner.active_mutator; });
        --state->waiters;
        token->alive = false;
        retired_pin = std::move(owner.mapping_pin);
        state->cv.notify_all();
    }
    // A real pin's destructor may schedule cleanup; never run it under metadata.
    return Error::None;
}
void MappingAdmission::SealBacking(Id backing) {
    std::lock_guard lock{state->mutex};
    state->backing_sealed.at(backing) = true;
    for (auto& [key, owner] : state->owners) {
        (void)key;
        if (owner.token->backing == backing)
            owner.sealed = true;
    }
    state->cv.notify_all();
}
Error MappingAdmission::DrainBacking(Id backing) {
    if (auto e = EntryCheck(); e != Error::None)
        return e;
    SealBacking(backing);
    std::unique_lock lock{state->mutex};
    MetadataMark mark;
    const auto active_mutations = [&] {
        return std::any_of(state->owners.begin(), state->owners.end(), [&](const auto& entry) {
            return entry.second.token->backing == backing && entry.second.active_mutator;
        });
    };
    for (const auto& [key, owner] : state->owners) {
        (void)key;
        if (owner.token->backing == backing && owner.active_mutator &&
            owner.mutator_thread == std::this_thread::get_id())
            return Error::SelfWait;
    }
    for (const auto& [id, use] : state->uses) {
        (void)id;
        if (use.backing == backing && !use.armed && use.creator == std::this_thread::get_id())
            return Error::SelfWait;
    }
    if (context.gpu && (state->backing_uses.at(backing) || active_mutations()))
        return Error::SelfWait;
    ++state->waiters;
    state->cv.notify_all();
    state->cv.wait(lock, [&] { return !state->backing_uses.at(backing) && !active_mutations(); });
    --state->waiters;
    return Error::None;
}
void MappingAdmission::Seal() {
    std::lock_guard lock{state->mutex};
    state->sealed = true;
    state->cv.notify_all();
}
AdmissionStats MappingAdmission::Stats() const {
    std::lock_guard lock{state->mutex};
    AdmissionStats out;
    out.uses = state->uses.size();
    out.waiters = state->waiters;
    out.sealed = state->sealed;
    for (const auto& [key, owner] : state->owners) {
        (void)key;
        out.mapping_uses += owner.uses;
        out.pending_mutators += owner.mutations.size();
    }
    for (const auto& [id, use] : state->uses) {
        (void)id;
        out.pinned_uses += use.pinned;
    }
    return out;
}
void MappingAdmission::WaitStats(const std::function<bool(AdmissionStats)>& predicate) {
    // Test-only rendezvous; predicate receives a value and must not call back into gate.
    std::unique_lock lock{state->mutex};
    state->cv.wait(lock, [&] {
        AdmissionStats out;
        out.uses = state->uses.size();
        out.waiters = state->waiters;
        out.sealed = state->sealed;
        for (const auto& [key, owner] : state->owners) {
            (void)key;
            out.pending_mutators += owner.mutations.size();
            out.mapping_uses += owner.uses;
        }
        return predicate(out);
    });
}
} // namespace M3A
