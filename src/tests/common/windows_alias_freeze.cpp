// SPDX-License-Identifier: GPL-2.0-or-later
// Native Windows byte/PTE witnesses. Metadata enrollment is diagnostic only.
#include <windows.h>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include "common/host_memory.h"

namespace {
using Memory = Common::HostMemory;
using Perm = Common::MemoryPermission;
void Check(bool okay, const char* reason) {
    if (!okay) throw std::runtime_error(reason);
}
template <class F> void Refused(F&& operation, const char* reason) {
    bool refused{};
    try { operation(); }
    catch (const std::runtime_error& error) {
        refused = std::string_view{error.what()} == "HostMemory mapping entries are frozen";
    }
    Check(refused, reason);
}
DWORD Protection(void* address) {
    MEMORY_BASIC_INFORMATION info{};
    Check(VirtualQuery(address, &info, sizeof(info)) == sizeof(info) &&
          info.State == MEM_COMMIT, "native-mapping-query");
    return info.Protect;
}
class PausedWriter {
public:
    explicit PausedWriter(u8* pointer) : gate{resume.get_future()}, worker{[this, pointer] {
        entered.set_value();
        gate.wait();
        *pointer = 29;
    }} {
        entered.get_future().wait();
    }
    ~PausedWriter() { Release(); }
    void Release() {
        if (worker.joinable()) { resume.set_value(); worker.join(); }
    }
private:
    std::promise<void> entered, resume;
    std::future<void> gate;
    std::thread worker;
};
void Cases(bool diagnostic) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const size_t g = info.dwAllocationGranularity;
    Check(g && g % 4096 == 0, "allocation-granularity");
    Memory arena(g * 8, g * 64, {}, 0, false, false, {}, true);
    arena.ClearBackingRegion(0, g, 17);
    arena.ClearBackingRegion(g * 2, g, 47);
    arena.Map(0, 0, g, Perm::ReadWrite, false);
    arena.Map(g * 2, 0, g, Perm::Read, false);
    auto* old_pointer = arena.BackingBasePointer();
    auto* aliases = arena.VirtualBasePointer();
    Check(old_pointer[0] == 17 && aliases[0] == 17 && aliases[g * 2] == 17,
          "physical-alias-identity");
    // Windows Map currently maps views read/write regardless of this argument.
    // Never infer actual PTE permissions from the ledger: prepare and query them.
    const auto mapped_permissions = Protection(aliases + g * 2);
    std::cout << "OBSERVE native Map(Read) protection=" << mapped_permissions << '\n';
    if (mapped_permissions != PAGE_READONLY)
        std::cout << "WITNESS Map(Read) metadata alone is not readonly PTE proof\n";
    arena.Protect(g * 2, g, Perm::Read);
    Check(Protection(aliases + g * 2) == PAGE_READONLY &&
          Protection(old_pointer) == PAGE_READWRITE, "independent-native-alias-permissions");
    // Writer has already captured a pointer and entered before closure attempts.
    PausedWriter writer{old_pointer};
    auto frozen = arena.TryFreezeMappingEntries();
    Check(bool(frozen) == diagnostic, "unsupported-windows-freeze-refusal");
    if (diagnostic) {
        Check(frozen->MappingEntriesClosed() && frozen->Aliases().size() == 2,
              "metadata-closure-census");
        Refused([&] { arena.Map(g * 4, g * 2, g, Perm::ReadWrite, false); },
                "closed-map-was-not-refused");
        Refused([&] { arena.Unmap(g * 2, g, false); }, "closed-unmap-was-not-refused");
        Refused([&] { arena.Protect(g * 2, g, Perm::ReadWrite); },
                "closed-protect-was-not-refused");
        Refused([&] { arena.ClearBackingRegion(0, g, 99); }, "closed-clear-was-not-refused");
        Refused([&] { (void)arena.PinPhysicalRange(0, g); }, "closed-pin-was-not-refused");
        Refused([&] { arena.EnableDirectMappedAddress(); }, "closed-direct-was-not-refused");
        Check(old_pointer[0] == 17 && Protection(aliases + g * 2) == PAGE_READONLY,
              "refused-entry-had-native-effects");
        // These ready/lifetime queries do not promise access exclusion.
        arena.EnsureBackingRange(0, g);
        Check(bool(arena.RetainBackingStorage()), "closed-lifetime-query");
        auto* late = arena.VirtualBasePointer();
        const auto expected = Common::HostMappingDomain::CanonicalPointer |
                              Common::HostMappingDomain::VirtualPointer;
        Check(late == aliases && (frozen->EscapedOwnerFamilies() & expected) == expected,
              "closed-escape-history");
        std::cout << "PASS metadata-only closure refuses six mutation entries; escapes remain recorded\n";
    }
    writer.Release();
    // PAGE_READONLY on one alias never excludes writes through another alias.
    Check(old_pointer[0] == 29 && aliases[0] == 29 && aliases[g * 2] == 29 &&
          Protection(aliases + g * 2) == PAGE_READONLY, "queued-old-pointer-write-witness");
    if (diagnostic) Check(frozen->MappingEntriesClosed(), "raw-store-reopened-metadata");
    std::cout << "WITNESS queued old pointer writes through readonly physical alias\n";
    DWORD previous{};
    Check(VirtualProtect(aliases + g * 2, g, PAGE_READWRITE, &previous) &&
          previous == PAGE_READONLY && Protection(aliases + g * 2) == PAGE_READWRITE,
          "raw-protection-bypass-witness");
    aliases[g * 2] = 53;
    Check(old_pointer[0] == 53 && aliases[0] == 53, "raw-alias-store-witness");
    if (diagnostic) {
        bool stale_readonly{};
        for (const auto& alias : frozen->Aliases()) {
            if (alias.virtual_offset == g * 2)
                stale_readonly = alias.permissions == static_cast<u32>(Perm::Read);
        }
        Check(frozen->MappingEntriesClosed() && stale_readonly,
              "raw-protection-ledger-mismatch-witness");
    }
    std::cout << "WITNESS raw VirtualProtect changes PTE permissions outside the ledger\n";
    Check(!Common::HostMappingDomain::Freeze::GuestBackingAdmission(),
          "unproved-physical-admission");
    if (diagnostic) {
        Check(frozen->CancelNoEffects() && !frozen->CancelNoEffects(), "duplicate-cancel");
        frozen.reset();
    } else {
        Check(!arena.TryFreezeMappingEntries(), "raw-alias-support-autoenrollment");
    }
    // After explicit metadata cancellation, saved VA resolves different physical bytes.
    arena.Unmap(g * 2, g, false);
    arena.Map(g * 2, g * 2, g, Perm::ReadWrite, false);
    Check(aliases[g * 2] == 47 && old_pointer[0] == 53, "remap-physical-identity");
    aliases[g * 2] = 61;
    Check(old_pointer[g * 2] == 61 && old_pointer[0] == 53, "saved-va-remap-store");
    auto again = arena.TryFreezeMappingEntries();
    if (diagnostic) {
        Check(again && again->MappingEntriesClosed(), "refreeze-metadata");
        bool remapped{};
        for (const auto& alias : again->Aliases()) {
            if (alias.virtual_offset == g * 2) remapped = alias.physical_offset == g * 2;
        }
        Check(remapped, "remap-census-generation");
        Check(again->CancelNoEffects(), "final-cancel");
    } else Check(!again, "remap-support-autoenrollment");
    std::cout << "WITNESS saved virtual pointer follows remap; canonical identity remains distinct\n";
    Check(!Common::HostMappingDomain::Freeze::GuestBackingAdmission(),
          "unproved-physical-admission");
}
} // namespace
int main(int argc, char** argv) {
    try {
        const bool diagnostic = argc == 2 && std::string_view{argv[1]} == "--metadata-counterfactual";
        Cases(diagnostic);
        std::cout << "COMPLETE native alias exclusion/refusal teardown\n";
        return diagnostic ? 42 : 0;
    } catch (const std::exception& error) {
        std::cerr << "ALIAS FREEZE CHECK: " << error.what() << '\n';
        return 91;
    }
}
