// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <csignal>
#include <future>
#include <iostream>
#include <thread>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common/host_memory.h"

using Common::HostMemory;
using Perm = Common::MemoryPermission;
template <typename F> void Refused(F&& f) {
    bool refused = false;
    try { f(); } catch (const std::runtime_error&) { refused = true; }
    assert(refused);
}
int main() {
    constexpr size_t page = 4096;
    if (sysconf(_SC_PAGESIZE) != page) {
        HostMemory arena(page * 8, page * 4096, {}, 0, false, false, {}, true);
        assert(!arena.TryFreezeMappingEntries());
        std::cout << "PASS unsupported host page size refuses mapping census\n";
        return 0;
    }
    {
        HostMemory arena(page * 8, page * 4096, {}, page * 2, false, true, {}, true);
        arena.ClearBackingRegion(0, page, 10);
        arena.Map(0, 0, page * 2, Perm::ReadWrite, false);
        arena.Map(page * 4, 0, page, Perm::ReadWrite, false);
        arena.Protect(page, page, Perm::Read);
        auto* canonical = arena.BackingBasePointer();
        auto* aliases = arena.VirtualBasePointer();
        const auto before = arena.GetBackingStats().chunks_created;
        auto frozen = arena.TryFreezeMappingEntries(); assert(frozen);
        assert(frozen->MappingEntriesClosed());
        assert(frozen->CanonicalBackingSize() == page * 8);
        const auto census = frozen->Aliases();
        assert(census.size() == 3);
        assert(census[0].virtual_offset == 0 && census[0].physical_offset == 0 &&
               census[0].size == page && census[0].permissions == static_cast<u32>(Perm::ReadWrite));
        assert(census[1].virtual_offset == page && census[1].physical_offset == page &&
               census[1].permissions == static_cast<u32>(Perm::Read));
        assert(census[2].virtual_offset == page * 4 && census[2].physical_offset == 0);
        std::thread attempts([&] {
            Refused([&] { arena.Map(page * 6, 0, page, Perm::ReadWrite, false); });
            Refused([&] { arena.Unmap(0, page, false); });
            Refused([&] { arena.Protect(page, page, Perm::ReadWrite); });
            Refused([&] { arena.EnsureBackingRange(page * 6, page); });
            Refused([&] { arena.ClearBackingRegion(0, page, 77); });
            Refused([&] { (void)arena.PinPhysicalRange(0, page); });
            Refused([&] { arena.EnableDirectMappedAddress(); });
        });
        attempts.join();
        arena.EnsureBackingRange(0, page); // Ready lookup has no new mapping effect.
        assert(arena.GetBackingStats().chunks_created == before);
        assert(canonical[0] == 10 && aliases[0] == 10 && aliases[page * 4] == 10);
        // Real protection is unchanged by a refused unprotect, not just metadata.
        const auto child = fork(); assert(child >= 0);
        if (!child) { aliases[page] = 99; _exit(0); }
        int status = 0; assert(waitpid(child, &status, 0) == child);
        assert(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
        // Mapping closure cannot prevent a retained raw store. Keep this negative.
        std::thread omitted([&] { aliases[page * 4] = 99; }); omitted.join();
        assert(canonical[0] == 99 && aliases[0] == 99);
        assert(frozen->EscapedOwnerFamilies() & Common::HostMappingDomain::CanonicalPointer);
        assert(frozen->EscapedOwnerFamilies() & Common::HostMappingDomain::VirtualPointer);
        assert(!frozen->GuestBackingAdmission());
        assert(frozen->CancelNoEffects());
        arena.Unmap(page * 4, page, false);
        arena.Map(page * 4, page * 2, page, Perm::ReadWrite, false);
        arena.Protect(page, page, Perm::ReadWrite);
        auto remapped = arena.TryFreezeMappingEntries(); assert(remapped);
        assert(remapped->Aliases().back().physical_offset == page * 2);
        assert(!frozen->CancelNoEffects());
        assert(remapped->CancelNoEffects());
    }
    {
        auto arena = std::make_unique<HostMemory>(page * 8, page * 4096,
            std::span<const int>{}, 0, false, false, nullptr, true);
        auto frozen = arena->TryFreezeMappingEntries(); assert(frozen);
        arena.reset();
        assert(!frozen->MappingEntriesClosed() && !frozen->CancelNoEffects());
    }
    {
        HostMemory source(page * 8, page * 4096, {}, 0, false, false, {}, true);
        auto frozen = source.TryFreezeMappingEntries(); assert(frozen);
        HostMemory moved{std::move(source)};
        assert(!source.PinPhysicalRange(0, 1) && !source.TryFreezeMappingEntries());
        assert(frozen->MappingEntriesClosed());
        assert(frozen->CancelNoEffects());
        assert(moved.PinPhysicalRange(0, 1));
    }
    {
        HostMemory arena(page * 8, page * 4096, {}, 0, false, false, {}, true);
        { auto frozen = arena.TryFreezeMappingEntries(); assert(frozen); }
        Refused([&] { arena.Map(0, 0, page, Perm::ReadWrite, false); });
    }
    {
        HostMemory arena(page * 8, page * 4096, {}, 0, false, false, {}, true);
        arena.EnableDirectMappedAddress();
        assert(!arena.TryFreezeMappingEntries());
    }
    {
        const int fd = memfd_create("external-mapping-census", MFD_CLOEXEC);
        assert(fd >= 0 && ftruncate(fd, page * 8) == 0);
        // HostMemory takes ownership, but the original export/owner history is
        // unknowable even if it is an ordinary memfd rather than a dma-buf.
        const std::array<int, 1> fds{fd};
        HostMemory external(page * 8, page * 4096, fds, page * 8, false, false, {}, true);
        assert(!external.TryFreezeMappingEntries());
    }
    {
        HostMemory ordinary(page * 8, page * 4096);
        ordinary.Map(0, 0, page, Perm::ReadWrite, false);
        ordinary.Protect(0, page, Perm::Read);
        assert(!ordinary.TryFreezeMappingEntries());
        ordinary.Unmap(0, page, false);
    }
    std::cout << "PASS production Linux HostMemory: canonical and two physical aliases, real read-only protection, seven pre-effect mutation refusals, retained raw-writer bypass, remap, drop, retirement and default-off enrollment; guest admission Unknown\n";
}
