// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

#include "common/physical_cpu_page.h"

namespace Common {

/// Explicit, bounded first-registration boundary. Call on its owner thread, outside cache/PTE
/// locks. All physical pages in the *complete* coarse footprint must be supplied cold. Ordinary
/// CPU/cache callers are not enrolled. This class supplies neither Vulkan completion evidence
/// nor CPU-VA permission; those remain obligations of the controlled adapter.
class PhysicalRegistrationBundle {
    struct Resources {
        std::vector<PreparedPhysicalCpuPage> pages;
        std::shared_ptr<const void> representations;
    };

public:
    enum class Start { Granted, Draining, Park, Reentrant, Cancelled, Invalid };
    PhysicalRegistrationBundle(const PhysicalRegistrationBundle&) = delete;
    PhysicalRegistrationBundle& operator=(const PhysicalRegistrationBundle&) = delete;
    PhysicalRegistrationBundle(PhysicalRegistrationBundle&&) noexcept = default;
    PhysicalRegistrationBundle& operator=(PhysicalRegistrationBundle&&) = delete;
    ~PhysicalRegistrationBundle() {
        if (!ticket) return;
        if (!domain->Cancel(ticket)) (void)domain->MarkUnknown(ticket);
    }

    [[nodiscard]] static std::optional<PhysicalRegistrationBundle>
    Begin(std::span<const PreparedPhysicalCpuPage> pages,
          std::shared_ptr<const void> representations) {
        if (pages.empty() || pages.size() > PhysicalRangeAdmission::MaxAccesses || !representations)
            return std::nullopt;
        auto resources = std::make_shared<Resources>();
        resources->representations = std::move(representations);
        for (const auto& page : pages) {
            if (!page.Valid(0, PhysicalPageGates::PageSize) || !page.storage.OwnerAlive() ||
                page.Epoch() != pages.front().Epoch())
                return std::nullopt;
            if (std::ranges::none_of(resources->pages, [&](const auto& old) {
                    return old.GateIdentity() == page.GateIdentity();
                }))
                resources->pages.push_back(page);
        }
        std::vector<PhysicalRangeAdmission::Access> accesses;
        for (const auto& page : resources->pages)
            accesses.push_back({page.PhysicalOffset(), PhysicalPageGates::PageSize,
                                PhysicalRangeAdmission::Mode::Write});
        const auto domain = pages.front().Epoch()->ranges;
        // ONE request precedes closure of ANY gate. Never park with partial range admission.
        auto ticket = domain->Enqueue(accesses, resources);
        if (!ticket) return std::nullopt;
        for (const auto& page : resources->pages) {
            if (!page.gate->CloseForRegistration()) {
                (void)domain->Cancel(ticket);
                // Already closed pages remain fail-closed; cancellation is not recovery.
                return std::nullopt;
            }
        }
        return PhysicalRegistrationBundle{std::move(resources), domain, std::move(ticket)};
    }

    [[nodiscard]] Start TryStart() {
        const auto phase = domain->StateOf(ticket);
        if (phase == PhysicalRangeAdmission::Phase::Cancelled) return Start::Cancelled;
        if (phase != PhysicalRangeAdmission::Phase::Pending) return Start::Invalid;
        for (const auto& page : resources->pages) {
            if (!page.storage.OwnerAlive()) {
                (void)domain->Cancel(ticket);
                return Start::Cancelled;
            }
            if (!page.gate->Drained()) return Start::Draining;
        }
        switch (domain->TryStart(ticket)) {
        case PhysicalRangeAdmission::Start::Granted:
            for (const auto& page : resources->pages) {
                if (!page.storage.OwnerAlive() || !page.gate->Drained()) {
                    (void)domain->Complete(ticket); // certified no effects at this boundary
                    return Start::Cancelled;
                }
            }
            return Start::Granted;
        case PhysicalRangeAdmission::Start::Park:
            return Start::Park;
        case PhysicalRangeAdmission::Start::Reentrant:
            return Start::Reentrant;
        case PhysicalRangeAdmission::Start::Cancelled:
            return Start::Cancelled;
        default:
            return Start::Invalid;
        }
    }

    [[nodiscard]] bool Granted() const {
        return domain->StateOf(ticket) == PhysicalRangeAdmission::Phase::Running;
    }
    [[nodiscard]] bool MarkUnknown() { return domain->MarkUnknown(ticket); }
    [[nodiscard]] bool CancelBeforeEffects() {
        return domain->Cancel(ticket) || (Granted() && domain->Complete(ticket));
    }

    /// External adapter must first certify real transfer completion/visibility and keep invalid
    /// content inaccessible on failure. Callback return is not such evidence. Gates become
    /// Tracked only here, after the successful upload, and never reopen implicitly.
    [[nodiscard]] bool CompleteWithExternalEvidence() {
        if (!Granted()) return false;
        for (const auto& page : resources->pages) {
            if (!page.gate->PublishTracked()) {
                (void)domain->MarkUnknown(ticket);
                return false;
            }
        }
        ready = domain->Complete(ticket);
        return ready;
    }

    /// A cached store is a distinct request over the same bounded footprint, not a fast-entry
    /// retry. It stays admitted through post-store tracker publication by the cache adapter.
    class CachedWrite {
    public:
        CachedWrite(const CachedWrite&) = delete;
        CachedWrite& operator=(const CachedWrite&) = delete;
        CachedWrite(CachedWrite&&) noexcept = default;
        ~CachedWrite() {
            if (!ticket) return;
            if (!domain->Cancel(ticket)) (void)domain->MarkUnknown(ticket);
        }
        [[nodiscard]] PhysicalRangeAdmission::Start TryStart() {
            const auto result = domain->TryStart(ticket);
            if (result == PhysicalRangeAdmission::Start::Granted) active = true;
            return result;
        }
        // Owner-thread capability. Ticket is private and only this object can complete it;
        // Seal does not revoke running requests. No range-domain lock inside CPU PTE guards.
        [[nodiscard]] bool Granted() const noexcept { return active && bool(ticket); }
        [[nodiscard]] bool CompleteAfterTrackerPublication() {
            if (!active || !domain->Complete(ticket)) return false;
            active = false;
            return true;
        }
        [[nodiscard]] bool TryCompleteAfterTrackerPublication() {
            if (!active || !domain->TryComplete(ticket)) return false;
            active = false;
            return true;
        }
        [[nodiscard]] bool MarkUnknown() {
            if (!domain->Quarantine(ticket)) return false;
            active = false;
            return true;
        }

    private:
        friend class PhysicalRegistrationBundle;
        CachedWrite(std::shared_ptr<const Resources> resources_,
                    std::shared_ptr<PhysicalRangeAdmission> domain_,
                    PhysicalRangeAdmission::Ticket ticket_)
            : resources{std::move(resources_)}, domain{std::move(domain_)},
              ticket{std::move(ticket_)} {}
        std::shared_ptr<const Resources> resources;
        std::shared_ptr<PhysicalRangeAdmission> domain;
        PhysicalRangeAdmission::Ticket ticket;
        bool active{};
    };

    [[nodiscard]] std::optional<CachedWrite> BeginCachedWrite(
        std::shared_ptr<const void> extra_owner = {}) const {
        if (!ready) return std::nullopt;
        std::vector<PhysicalRangeAdmission::Access> accesses;
        for (const auto& page : resources->pages) {
            if (!page.storage.OwnerAlive()) return std::nullopt;
            accesses.push_back({page.PhysicalOffset(), PhysicalPageGates::PageSize,
                                PhysicalRangeAdmission::Mode::Write});
        }
        struct Owners {
            std::shared_ptr<const Resources> pages;
            std::shared_ptr<const void> extra;
        };
        auto owners = std::make_shared<const Owners>(Owners{resources, std::move(extra_owner)});
        auto write = domain->Enqueue(accesses, std::move(owners));
        if (!write) return std::nullopt;
        return CachedWrite{resources, domain, std::move(write)};
    }

    [[nodiscard]] bool OwnsCachedWrite(const CachedWrite& write) const noexcept {
        return ready && resources == write.resources && domain == write.domain;
    }

    template <typename T>
        requires(std::is_trivially_copyable_v<T> && sizeof(T) <= 16)
    [[nodiscard]] static PhysicalStoreResult StoreCached(CachedWrite& write,
                                                         const PreparedPhysicalCpuPage& page,
                                                         size_t offset, const T& value) {
        if (!page.Valid(offset, sizeof(T))) return PhysicalStoreResult::Invalid;
        if (!page.storage.OwnerAlive()) return PhysicalStoreResult::Retired;
        if (!write.Granted() || std::ranges::none_of(write.resources->pages, [&](const auto& old) {
                return old.GateIdentity() == page.GateIdentity() && old.Epoch() == page.Epoch();
            }))
            return PhysicalStoreResult::NeedSlow;
        if (page.gate->observation.test_and_set(std::memory_order_acquire))
            return PhysicalStoreResult::NeedSlow;
        PhysicalPageGates::Node::ObservationExit observation_exit{&page.gate->observation};
        u64 expected = PhysicalPageGates::Node::Tracked;
        if (!page.gate->state.compare_exchange_strong(
                expected, PhysicalPageGates::Node::Tracked + PhysicalPageGates::Node::One,
                std::memory_order_acquire))
            return PhysicalStoreResult::NeedSlow;
        PreparedPhysicalCpuPage::Exit exit{page.gate.get()};
        std::memcpy(page.storage.Bytes().data() + offset, &value, sizeof(T));
        page.gate->Publish(offset, sizeof(T));
        return PhysicalStoreResult::Stored;
    }

private:
    PhysicalRegistrationBundle(std::shared_ptr<const Resources> resources_,
                               std::shared_ptr<PhysicalRangeAdmission> domain_,
                               PhysicalRangeAdmission::Ticket ticket_)
        : resources{std::move(resources_)}, domain{std::move(domain_)}, ticket{std::move(ticket_)} {
    }
    std::shared_ptr<const Resources> resources;
    std::shared_ptr<PhysicalRangeAdmission> domain;
    PhysicalRangeAdmission::Ticket ticket;
    bool ready{};
};

} // namespace Common
