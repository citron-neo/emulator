// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <future>
#include <iostream>
#include <thread>
#include "common/host_mapping_domain.h"

using Domain = Common::HostMappingDomain;
template <typename F> void Refused(F&& f) {
    bool refused = false;
    try { f(); } catch (const std::runtime_error&) { refused = true; }
    assert(refused);
}
int main() {
    {
        Domain legacy{false};
        legacy.Initialize(4096, true); // Cannot retrospectively enroll an owner.
        assert(!legacy.IsManaged() && !legacy.TryFreeze());
        auto outer = legacy.BeginMutation();
        auto nested = legacy.BeginMutation(); // No mutex, allocation or reentrant lock.
        outer.Map(0, 0, 4096, 3); outer.Finish(); nested.Finish();
        assert(!legacy.IsManaged() && !legacy.TryFreeze());
    }
    {
        Domain domain;
        std::array<int, 32> physical, permissions{};
        physical.fill(-1);
        unsigned random = 375;
        const auto next = [&] { random = random * 1664525u + 1013904223u; return random; };
        for (size_t step = 0; step < 256; ++step) {
            const size_t begin = next() % physical.size();
            const size_t length = std::min<size_t>(1 + next() % 8, physical.size() - begin);
            const unsigned operation = next() % 3;
            const int pa = int(next() % 8192), perm = int(1 + next() % 3);
            {
                auto m = domain.BeginMutation();
                if (operation == 0) m.Map(begin, pa, length, perm);
                else if (operation == 1) m.Unmap(begin, length);
                else m.Protect(begin, length, perm);
                m.Finish();
            }
            for (size_t byte = begin; byte < begin + length; ++byte) {
                if (operation == 0) { physical[byte] = pa + int(byte - begin); permissions[byte] = perm; }
                else if (operation == 1) physical[byte] = -1;
                else if (physical[byte] != -1) permissions[byte] = perm;
            }
            auto frozen = domain.TryFreeze(); assert(frozen);
            for (size_t byte = 0; byte < physical.size(); ++byte) {
                size_t hits = 0;
                for (const auto& a : frozen->Aliases()) {
                    if (byte < a.virtual_offset || byte >= a.virtual_offset + a.size) continue;
                    ++hits;
                    assert(int(a.physical_offset + byte - a.virtual_offset) == physical[byte]);
                    assert(int(a.permissions) == permissions[byte]);
                }
                assert(hits == (physical[byte] < 0 ? 0u : 1u));
            }
            assert(frozen->CancelNoEffects());
        }
    }
    {
        Domain domain;
        domain.Initialize(4096, true);
        { auto m = domain.BeginMutation(); m.Map(0, 8192, 64, 3); m.Finish(); }
        { auto m = domain.BeginMutation(); m.Protect(16, 32, 1); m.Finish(); }
        { auto m = domain.BeginMutation(); m.Unmap(24, 16); m.Finish(); }
        { auto m = domain.BeginMutation(); m.Map(28, 4096, 8, 3); m.Finish(); }
        auto frozen = domain.TryFreeze();
        assert(frozen && frozen->MappingEntriesClosed());
        assert(frozen->CanonicalBackingSize() == 4096);
        // Independent per-byte oracle: preserve offset and permission on both
        // complements of protect/unmap; map insertion must not fill either hole.
        for (size_t byte = 0; byte < 64; ++byte) {
            const bool hole = (byte >= 24 && byte < 28) || (byte >= 36 && byte < 40);
            size_t hits = 0;
            for (const auto& a : frozen->Aliases()) {
                if (byte < a.virtual_offset || byte >= a.virtual_offset + a.size) continue;
                ++hits;
                const auto pa = a.physical_offset + byte - a.virtual_offset;
                assert(pa == ((byte >= 28 && byte < 36) ? 4096 + byte - 28 : 8192 + byte));
                assert(a.permissions == ((byte >= 16 && byte < 48 && !(byte >= 28 && byte < 36)) ? 1u : 3u));
            }
            assert(hits == (hole ? 0u : 1u));
        }
        assert(!domain.TryFreeze());
        Refused([&] { auto m = domain.BeginMutation(); m.Finish(); });
        domain.ObserveEscape(Domain::CanonicalPointer);
        domain.ObserveEscape(Domain::PhysicalPin);
        assert(frozen->EscapedOwnerFamilies() == (Domain::CanonicalPointer | Domain::PhysicalPin));
        assert(!frozen->GuestBackingAdmission());
        assert(frozen->CancelNoEffects());
        auto next = domain.TryFreeze();
        assert(next && !frozen->CancelNoEffects());
        Refused([&] { auto m = domain.BeginMutation(); m.Finish(); });
        assert(next->CancelNoEffects());
    }
    {
        Domain domain;
        domain.Initialize(4096, true);
        std::promise<void> entered, finish;
        auto ready = finish.get_future();
        std::thread mapper([&] {
            auto m = domain.BeginMutation();
            entered.set_value();
            ready.wait();
            m.Map(0, 0, 4096, 3);
            m.Finish();
        });
        entered.get_future().wait();
        assert(!domain.TryFreeze()); // TryFreeze must not wait on an active publisher.
        finish.set_value(); mapper.join();
        auto frozen = domain.TryFreeze();
        assert(frozen && frozen->Aliases().size() == 1);
        assert(frozen->CancelNoEffects());
    }
    {
        Domain domain;
        domain.Initialize(4096, true);
        { auto frozen = domain.TryFreeze(); assert(frozen); }
        assert(!domain.TryFreeze());
        Refused([&] { auto m = domain.BeginMutation(); m.Finish(); });
    }
    {
        auto domain = std::make_unique<Domain>();
        domain->Initialize(4096, true);
        auto frozen = domain->TryFreeze(); assert(frozen);
        domain.reset();
        assert(!frozen->MappingEntriesClosed() && !frozen->CancelNoEffects());
    }
    {
        Domain destination, source;
        auto old = destination.TryFreeze(); assert(old);
        destination = std::move(source);
        assert(!old->MappingEntriesClosed() && !old->CancelNoEffects());
    }
    {
        Domain domain;
        { auto m = domain.BeginMutation(); } // Partial/failed publication poisons census.
        assert(!domain.TryFreeze());
        { auto m = domain.BeginMutation(); m.Finish(); } // Ordinary path can still run.
        assert(!domain.TryFreeze());
    }
    {
        Domain domain;
        { auto m = domain.BeginMutation(); m.Unsupported(); m.Finish(); }
        assert(!domain.TryFreeze());
    }
    {
        Domain domain;
        for (size_t i = 0; i <= Domain::Capacity; ++i) {
            auto m = domain.BeginMutation(); m.Map(i * 2, i, 1, 3); m.Finish();
        }
        assert(!domain.TryFreeze()); // Capacity never certifies a truncated census.
    }
    {
        Domain source;
        source.Initialize(4096, true);
        auto frozen = source.TryFreeze(); assert(frozen);
        Domain moved{std::move(source)};
        assert(!source.TryFreeze());
        Refused([&] { auto m = source.BeginMutation(); m.Finish(); });
        assert(frozen->MappingEntriesClosed() && frozen->CancelNoEffects());
        auto again = moved.TryFreeze(); assert(again && again->CancelNoEffects());
    }
    std::cout << "PASS mapping ledger byte oracle, active publisher refusal, closed entry, stale release, drop, retirement, move, failed publication and capacity; guest admission Unknown\n";
}
