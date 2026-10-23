// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <type_traits>
#include <sys/uio.h>
#include <unistd.h>

#include "common/host_memory.h"

namespace {
constexpr size_t Page = 64 * 1024;
size_t Fds() {
    size_t count = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
        ++count;
    return count;
}
bool Read(const u8* address, u8& byte) {
    iovec local{&byte, 1}, remote{const_cast<u8*>(address), 1};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == 1;
}
}

int main() {
    using Owner = Common::RetainedBackingStorage;
    static_assert(!std::is_default_constructible_v<Owner>);
    static_assert(!std::is_constructible_v<Owner, std::shared_ptr<const void>,
                                          std::shared_ptr<const Common::GuestBackingEpoch>, size_t>);
    const auto initial_fds = Fds();
    {
        auto arena = std::make_unique<Common::HostMemory>(4 * Page, 16 * Page);
        auto* probe = arena->BackingBasePointer() + 7;
        *probe = 77;
        const auto epoch = arena->BackingEpoch();
        auto owner = arena->RetainBackingStorage();
        assert(owner && owner->OwnerAlive() && owner->Epoch() == epoch && owner->Size() == 4 * Page);
        assert(!arena->TryFreezeMappingEntries()); // Retention never enrolls an ordinary arena.
        auto relocated = std::make_unique<Common::HostMemory>(std::move(*arena));
        assert(!arena->RetainBackingStorage());
        arena.reset();
        assert(owner->OwnerAlive() && owner->Epoch() == relocated->BackingEpoch());
        auto retained = std::make_optional(std::move(*owner));
        assert(!owner->OwnerAlive() && owner->Size() == 0);
        owner.reset();
        relocated.reset();
        assert(!epoch->alive && !retained->OwnerAlive());
        u8 observed = 0;
        // Kernel-mediated probe: never dereference an address if the mutant unmapped it.
        if (!Read(probe, observed) || observed != 77) {
            std::cerr << "backing storage released while owner retained\n";
            return 42;
        }
        Common::HostMemory replacement{4 * Page, 16 * Page};
        assert(replacement.BackingEpoch() != epoch);
        auto* current = replacement.BackingBasePointer() + 7;
        *current = 99;
        assert(current != probe);
        retained.reset();
        assert(!Read(probe, observed));
        assert(Read(current, observed) && observed == 99);
    }
    assert(Fds() == initial_fds);
    {
        auto lazy = std::make_unique<Common::HostMemory>(8 * Page, 16 * Page,
            std::span<const int>{}, Page, false, true);
        const auto before = lazy->GetBackingStats();
        auto owner = lazy->RetainBackingStorage();
        assert(owner && owner->OwnerAlive());
        const auto after = lazy->GetBackingStats();
        assert(before.chunks_created == 0 && after.chunks_created == 0 && after.active_chunks == 0);
        lazy->EnsureBackingRange(3 * Page, Page);
        assert(lazy->GetBackingStats().chunks_created == 1);
        auto* probe = lazy->BackingBasePointer() + 3 * Page + 9;
        *probe = 55;
        lazy.reset();
        assert(!owner->OwnerAlive());
        u8 observed = 0;
        assert(Read(probe, observed) && observed == 55);
        owner.reset();
        assert(!Read(probe, observed));
    }
    assert(Fds() == initial_fds);
    {
        Common::HostMemory controlled{4 * Page, 16 * Page, {}, 0, false, false, {}, true};
        auto freeze = controlled.TryFreezeMappingEntries();
        assert(freeze && freeze->MappingEntriesClosed() && freeze->EscapedOwnerFamilies() == 0);
        auto owner = controlled.RetainBackingStorage();
        assert(owner && owner->OwnerAlive());
        assert(freeze->MappingEntriesClosed() && freeze->EscapedOwnerFamilies() == 0);
        owner.reset();
        assert(freeze->MappingEntriesClosed() && freeze->CancelNoEffects());
    }
    assert(Fds() == initial_fds);
    std::cout << "PASS real backing storage owner: facade/token move, moved-from refusal, "
                 "retired epoch with live allocation, replacement isolation, lazy capture without "
                 "materialization, retained later chunk and final mapping/FD release; no admission\n";
}
