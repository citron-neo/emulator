// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstring>
#include <type_traits>

#include "common/atomic_ops.h"
#include "common/pinned_physical_memory.h"

namespace Common {

enum class PhysicalStoreResult { Stored, CompareFailed, NeedSlow, Retired, Invalid };
class PhysicalPageRegistration;
class PhysicalRegistrationBundle;

/// Prepared physical target, not CPU-VA resolution/admission. Preparation may allocate; the
/// scalar methods use only this stable target, page-local atomics and stack state. Never fall
/// through to an unguarded store on failure. Ordinary Memory::Write is not routed here yet.
class PreparedPhysicalCpuPage {
public:
    [[nodiscard]] const void* GateIdentity() const noexcept { return gate.get(); }
    [[nodiscard]] u64 PhysicalOffset() const noexcept { return storage.PhysicalOffset(); }
    [[nodiscard]] const auto& Epoch() const noexcept { return storage.Epoch(); }
    [[nodiscard]] u64 PublicationSequence() const noexcept {
        return gate->published.load(std::memory_order_acquire);
    }
    /// Cumulative observation only: no consume, clear or acknowledgement semantics. A future
    /// incremental consumer needs a separate sequence/ack protocol that cannot lose writes.
    [[nodiscard]] std::optional<PhysicalPageGates::Publication> Published() const noexcept {
        return gate->Snapshot();
    }

    template <typename T> requires(std::is_trivially_copyable_v<T> && sizeof(T) <= 16)
    [[nodiscard]] PhysicalStoreResult TryStore(size_t offset, const T& value) const noexcept {
        if (!Valid(offset, sizeof(T))) return PhysicalStoreResult::Invalid;
        if (!storage.OwnerAlive()) return PhysicalStoreResult::Retired;
        if (!gate->TryEnter()) return PhysicalStoreResult::NeedSlow;
        Exit exit{gate.get()}; // no per-store shared_ptr copy or allocation
        std::memcpy(storage.Bytes().data() + offset, &value, sizeof(T));
        gate->Publish(offset, sizeof(T)); // exact successful post-store extent, before writer exit
        return PhysicalStoreResult::Stored;
    }

    template <typename T> requires(std::is_unsigned_v<T> && sizeof(T) <= 8)
    [[nodiscard]] PhysicalStoreResult TryCompareExchange(size_t offset, T value,
                                                        T expected) const noexcept {
        if (!Valid(offset, sizeof(T)) || offset % alignof(T)) return PhysicalStoreResult::Invalid;
        if (!storage.OwnerAlive()) return PhysicalStoreResult::Retired;
        if (!gate->TryEnter()) return PhysicalStoreResult::NeedSlow;
        Exit exit{gate.get()};
        if (!AtomicCompareAndSwap(reinterpret_cast<T*>(storage.Bytes().data() + offset),
                                  value, expected)) return PhysicalStoreResult::CompareFailed;
        gate->Publish(offset, sizeof(T));
        return PhysicalStoreResult::Stored;
    }

    /// Reserve slow-path priority BEFORE closing fast entry. Call outside cache/PTE locks.
    [[nodiscard]] std::optional<PhysicalPageRegistration> BeginRegistration(
        std::shared_ptr<const void> representations) const;

private:
    friend class HostMemory;
    friend class PhysicalPageRegistration;
    friend class PhysicalRegistrationBundle;
    PreparedPhysicalCpuPage(PinnedPhysicalMemory pin, std::shared_ptr<PhysicalPageGates::Node> node)
        : storage{std::move(pin)}, gate{std::move(node)} {}
    struct Exit {
        PhysicalPageGates::Node* node;
        ~Exit() { node->Exit(); }
    };
    bool Valid(size_t offset, size_t count) const noexcept {
        return storage && gate && storage.Bytes().size() == PhysicalPageGates::PageSize &&
               offset < PhysicalPageGates::PageSize && count <= PhysicalPageGates::PageSize - offset;
    }
    PinnedPhysicalMemory storage;
    std::shared_ptr<PhysicalPageGates::Node> gate;
};

class PhysicalPageRegistration {
public:
    enum class Start { Granted, Draining, Park, Reentrant, Cancelled, Invalid };
    PhysicalPageRegistration(const PhysicalPageRegistration&) = delete;
    PhysicalPageRegistration& operator=(const PhysicalPageRegistration&) = delete;
    PhysicalPageRegistration(PhysicalPageRegistration&&) noexcept = default;
    PhysicalPageRegistration& operator=(PhysicalPageRegistration&&) = delete;
    ~PhysicalPageRegistration() {
        if (!ticket) return;
        if (!domain->Cancel(ticket)) (void)domain->MarkUnknown(ticket);
        // Never reopen a closing gate implicitly. A failed registration stays fail-closed.
    }
    [[nodiscard]] Start TryStart() {
        const auto phase = domain->StateOf(ticket);
        if (phase == PhysicalRangeAdmission::Phase::Cancelled) return Start::Cancelled;
        if (phase != PhysicalRangeAdmission::Phase::Pending) return Start::Invalid;
        if (gate->closed->load(std::memory_order_acquire)) {
            (void)domain->Cancel(ticket);return Start::Cancelled;
        }
        if (!gate->Drained()) return Start::Draining;
        switch (domain->TryStart(ticket)) {
        case PhysicalRangeAdmission::Start::Granted:
            if (gate->closed->load(std::memory_order_acquire)) {
                (void)domain->Complete(ticket); // no effects have occurred in this boundary
                return Start::Cancelled;
            }
            return Start::Granted;
        case PhysicalRangeAdmission::Start::Park: return Start::Park;
        case PhysicalRangeAdmission::Start::Reentrant: return Start::Reentrant;
        case PhysicalRangeAdmission::Start::Cancelled: return Start::Cancelled;
        default: return Start::Invalid;
        }
    }
    /// Marks tracking only; does not complete the range obligation or certify an upload.
    /// Tracked is NOT registration-ready/content-valid. An uncertain upload after this call
    /// must retain the ticket/resources as Unknown, never complete as a successful registration.
    [[nodiscard]] bool PublishTracked() {
        return domain->StateOf(ticket) == PhysicalRangeAdmission::Phase::Running &&
               gate->PublishTracked();
    }
    /// Requires REAL visibility/completion or no-effect evidence from the production adapter.
    /// Callback return alone is insufficient. This helper cannot validate that evidence.
    /// No-effect release does not make a tracked representation valid; the adapter must keep
    /// failed/uninitialized content inaccessible. Partial/uncertain effects require Unknown.
    [[nodiscard]] bool CompleteWithExternalEvidence() { return domain->Complete(ticket); }
    [[nodiscard]] bool MarkUnknown() { return domain->MarkUnknown(ticket); }

private:
    friend class PreparedPhysicalCpuPage;
    PhysicalPageRegistration(std::shared_ptr<PhysicalPageGates::Node> node,
                             std::shared_ptr<PhysicalRangeAdmission> owner,
                             PhysicalRangeAdmission::Ticket request)
        : gate{std::move(node)}, domain{std::move(owner)}, ticket{std::move(request)} {}
    std::shared_ptr<PhysicalPageGates::Node> gate;
    std::shared_ptr<PhysicalRangeAdmission> domain;
    PhysicalRangeAdmission::Ticket ticket;
};

inline std::optional<PhysicalPageRegistration> PreparedPhysicalCpuPage::BeginRegistration(
    std::shared_ptr<const void> representations) const {
    if (!Valid(0, PhysicalPageGates::PageSize) || !storage.OwnerAlive() || !representations)
        return std::nullopt;
    struct Resources { PreparedPhysicalCpuPage physical; std::shared_ptr<const void> gpu; };
    auto resources = std::make_shared<const Resources>(Resources{*this, std::move(representations)});
    const std::array accesses{PhysicalRangeAdmission::Access{
        storage.PhysicalOffset(), PhysicalPageGates::PageSize, PhysicalRangeAdmission::Mode::Write}};
    const auto domain = storage.Epoch()->ranges;
    auto ticket = domain->Enqueue(accesses, std::move(resources));
    if (!ticket) return std::nullopt;
    if (!gate->CloseForRegistration()) {
        (void)domain->Cancel(ticket);return std::nullopt;
    }
    return PhysicalPageRegistration{gate, domain, std::move(ticket)};
}

} // namespace Common
