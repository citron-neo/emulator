// SPDX-License-Identifier: GPL-2.0-or-later
// Real Windows HostMemory/Kernelbase mappings and real common library linkage.
#include <windows.h>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include "common/host_memory.h"

namespace {
void Check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
void NativeCases(bool expect_vulnerable) {
    using Common::HostMemory;
    using P = Common::MemoryPermission;
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const size_t g = info.dwAllocationGranularity;
    Check(g && g % 4096 == 0, "allocation-granularity");
    {
        // Move BEFORE any export: the old facade must not create an unrecorded
        // pointer after its mapping domain has transferred to the new owner.
        HostMemory source(g * 8, g * 64, {}, 0, false, false, {}, true);
        source.Map(0, 0, g, P::ReadWrite, false);
        source.ClearBackingRegion(0, g, 17);
        HostMemory moved{std::move(source)};
        const auto& old = source;
        auto* a = source.BackingBasePointer();
        auto* b = source.VirtualBasePointer();
        auto* c = old.BackingBasePointer();
        auto* d = old.VirtualBasePointer();
        if (expect_vulnerable) {
            Check(a && b && c == a && d == b, "baseline-stale-pointer-witness-absent");
            a[0] = 19;
            Check(b[0] == 19 && c[0] == 19 && d[0] == 19, "unrecorded-native-alias-write");
            std::cout << "WITNESS post-move first export bypasses the transferred owner\n";
        } else {
            Check(!a && !b && !c && !d, "moved-from-pointer");
        }
        Check(!moved.TryFreezeMappingEntries(), "unsupported-windows-freeze-refusal");
        Check(moved.BackingBasePointer()[0] == (expect_vulnerable ? 19 : 17),
              "moved-owner-native-content");
    }
    {
        HostMemory source(g * 8, g * 64, {}, 0, false, false, {}, true);
        source.Map(0, 0, g, P::ReadWrite, false);
        source.Map(g * 2, 0, g, P::ReadWrite, false);
        source.ClearBackingRegion(0, g, 10);
        source.ClearBackingRegion(g * 2, g, 20);
        auto* canonical = source.BackingBasePointer();
        auto* aliases = source.VirtualBasePointer();
        Check(canonical && aliases && aliases[0] == 10 && aliases[g * 2] == 10,
              "real-physical-alias-identity");
        auto pin = source.PinPhysicalRange(0, g);
        auto storage = source.RetainBackingStorage();
        Check(pin && storage && pin->OwnerAlive() && storage->OwnerAlive(), "actual-storage-owners");
        HostMemory moved{std::move(source)};
        auto* stale_backing = source.BackingBasePointer();
        auto* stale_virtual = source.VirtualBasePointer();
        const auto& const_source = source;
        auto* stale_const_backing = const_source.BackingBasePointer();
        auto* stale_const_virtual = const_source.VirtualBasePointer();
        if (expect_vulnerable) {
            Check(stale_backing == canonical && stale_virtual == aliases &&
                  stale_const_backing == canonical && stale_const_virtual == aliases,
                  "baseline-stale-pointer-witness-absent");
            stale_backing[0] = 11;
            Check(aliases[0] == 11 && aliases[g * 2] == 11, "baseline-native-write-not-observed");
            std::cout << "WITNESS moved-from facade exports four unowned raw pointers\n";
        } else {
            Check(!stale_backing && !stale_virtual && !stale_const_backing && !stale_const_virtual,
                  "moved-from-pointer");
        }
        Check(!source.PinPhysicalRange(0, 1) && !source.RetainBackingStorage() &&
              !source.TryFreezeMappingEntries(), "moved-from-capture-refusal");
        // Windows is deliberately unsupported by the existing ledger. A storage
        // pin, owner move and native aliases must not silently enroll this arena.
        Check(!moved.TryFreezeMappingEntries(), "unsupported-windows-freeze-refusal");
        std::promise<void> run_writer;
        auto ready = run_writer.get_future();
        std::thread writer{[&] { ready.wait(); aliases[g * 2] = 31; }};
        run_writer.set_value();
        writer.join();
        Check(canonical[0] == 31 && pin->Bytes()[0] == 31, "retained-raw-writer-bypass-witness");
        moved.Unmap(g * 2, g, false);
        moved.Map(g * 2, g * 2, g, P::ReadWrite, false);
        aliases[g * 2] = 47;
        Check(canonical[0] == 31 && canonical[g * 2] == 47 && pin->Bytes()[0] == 31,
              "remap-physical-identity");
        moved.Protect(g * 2, g, P::Read);
        MEMORY_BASIC_INFORMATION mapping{};
        Check(VirtualQuery(aliases + g * 2, &mapping, sizeof(mapping)) == sizeof(mapping) &&
              mapping.State == MEM_COMMIT && mapping.Protect == PAGE_READONLY,
              "native-readonly-protection");
        Check(!moved.TryFreezeMappingEntries(), "readonly-is-not-physical-freeze");
        moved.Protect(g * 2, g, P::ReadWrite);
        aliases[g * 2] = 53;
        Check(canonical[g * 2] == 53, "native-unprotect-byte-effect");
        Check(!Common::HostMappingDomain::Freeze::GuestBackingAdmission(), "false-guest-admission");
    }
    {
        std::optional<Common::RetainedBackingStorage> retained;
        std::optional<Common::PinnedPhysicalMemory> pin;
        u8* saved{};
        {
            HostMemory arena(g * 8, g * 64);
            saved = arena.BackingBasePointer();
            pin = arena.PinPhysicalRange(0, g);
            retained = arena.RetainBackingStorage();
            Check(pin && retained && !arena.TryFreezeMappingEntries(), "ordinary-arena-refusal");
        }
        Check(!pin->OwnerAlive() && !retained->OwnerAlive(), "retired-epoch-invalidation");
        saved[0] = 67; // Allocation is retained, but this is not access admission.
        Check(pin->Bytes()[0] == 67, "allocation-retention-is-not-pointer-revocation");
        MEMORY_BASIC_INFORMATION mapping{};
        Check(VirtualQuery(saved, &mapping, sizeof(mapping)) == sizeof(mapping) &&
              mapping.State == MEM_COMMIT, "retained-native-allocation");
        pin.reset();
        retained.reset();
        Check(VirtualQuery(saved, &mapping, sizeof(mapping)) == sizeof(mapping) &&
              mapping.State == MEM_FREE, "final-native-allocation-release");
    }
    {
        // Replace the real facade while a paused alias writer is the last
        // allocation owner. Epoch invalidation must not depend on test-scope pins.
        HostMemory destination(g * 8, g * 64, {}, 0, false, false, {}, true);
        destination.Map(0, 0, g, P::ReadWrite, false);
        destination.ClearBackingRegion(0, g, 71);
        auto* old_backing = destination.BackingBasePointer();
        auto* old_alias = destination.VirtualBasePointer();
        auto old_epoch = destination.BackingEpoch(); // Identity only, not storage.
        auto pin = destination.PinPhysicalRange(0, g);
        auto storage = destination.RetainBackingStorage();
        Check(pin && storage, "replacement-capture");
        HostMemory incoming(g * 8, g * 64, {}, 0, false, false, {}, true);
        incoming.Map(0, 0, g, P::ReadWrite, false);
        incoming.ClearBackingRegion(0, g, 83);
        const auto incoming_epoch = incoming.BackingEpoch();
        std::promise<void> entered, resume;
        auto gate = resume.get_future();
        bool writer_saw_retirement{}, writer_saw_bytes{};
        std::thread writer{[&, pin = std::move(pin), storage = std::move(storage)]() mutable {
            entered.set_value();
            gate.wait();
            writer_saw_retirement = !pin->OwnerAlive() && !storage->OwnerAlive();
            old_alias[0] = 97;
            writer_saw_bytes = pin->Bytes()[0] == 97 && old_backing[0] == 97;
            pin.reset();
            storage.reset(); // Actual last allocation owner, on this writer.
        }};
        entered.get_future().wait();
        pin.reset();
        storage.reset();
        destination = std::move(incoming);
        const bool retired = !old_epoch->alive.load(std::memory_order_acquire);
        const bool transferred = destination.BackingEpoch() == incoming_epoch &&
                                 incoming_epoch->alive.load(std::memory_order_acquire);
        MEMORY_BASIC_INFORMATION before{};
        const bool retained = VirtualQuery(old_backing, &before, sizeof(before)) == sizeof(before) &&
                              before.State == MEM_COMMIT;
        // Join before reporting any failure: no timeout-based scheduling oracle.
        resume.set_value();
        writer.join();
        Check(retired && writer_saw_retirement, "replaced-epoch-still-live");
        Check(transferred && retained && writer_saw_bytes, "replacement-last-owner-storage");
        Check(destination.BackingBasePointer()[0] == 83,
              "retired-alias-wrote-replacement-backing");
        MEMORY_BASIC_INFORMATION after{};
        Check(VirtualQuery(old_backing, &after, sizeof(after)) == sizeof(after) &&
              after.State == MEM_FREE, "replacement-final-native-release");
        Check(VirtualQuery(old_alias, &after, sizeof(after)) == sizeof(after) &&
              after.State == MEM_FREE, "replacement-final-alias-release");
        Check(!destination.TryFreezeMappingEntries() &&
              !Common::HostMappingDomain::Freeze::GuestBackingAdmission(),
              "replacement-is-not-freeze-admission");
        std::cout << "PASS replacement invalidates epoch; paused alias writer retains old storage; final native release\n";
    }
    std::cout << "PASS real Windows alias/remap/protection/retained-allocation effects; all unproved freezes refuse\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        const bool vulnerable = argc == 2 && std::string_view{argv[1]} == "--expect-vulnerable";
        NativeCases(vulnerable);
        std::cout << "COMPLETE native mapping teardown\n";
        return vulnerable ? 42 : 0;
    } catch (const std::exception& error) {
        std::cerr << "MAPPING OWNER CHECK: " << error.what() << '\n';
        return 91;
    }
}
