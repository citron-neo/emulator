// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>
#include <stdexcept>
#include <utility>
#include "common/producer_drain.h"

namespace Core::Memory {
// Managed Memory facade writes only. Native stores, escaped pointers, device
// writers and mapping publication remain independent; never Guest admission.
class ManagedGuestWrites {
    std::shared_ptr<Common::ProducerDrain> domain;
public:
    static constexpr bool TrackingSupported() noexcept {
#ifdef HAS_NCE
        return false;
#else
        return true;
#endif
    }
    class Refused final : public std::runtime_error {
    public:
        Refused() : std::runtime_error("managed guest writes closed") {}
    };
    explicit ManagedGuestWrites(std::shared_ptr<Common::ProducerDrain> domain_)
        : domain{std::move(domain_)} {
#ifdef HAS_NCE
        // NCE's signal interpreter calls Memory facades. Do not introduce
        // yielding, exceptions or new TLS policy in a signal handler. This
        // build cannot certify the domain; closure must remain Unknown.
        domain->Seal();
#endif
    }
    [[nodiscard]] std::optional<Common::ProducerDrain::Entry> Enter() const {
#ifdef HAS_NCE
        return std::nullopt; // Unsupported domain, NOT a tracked/drained entry.
#else
        // Normal contention must not drop a store. This is not a signal/fault
        // handler API; closed/capacity refuses before any Impl access.
        auto entry = domain->EnterCold();
        if (!entry) throw Refused{};
        return entry;
#endif
    }
    [[nodiscard]] std::optional<Common::ProducerDrain::Entry> TryEnter() const noexcept {
#ifdef HAS_NCE
        return std::nullopt;
#else
        return domain->TryEnter();
#endif
    }
};
} // namespace Core::Memory
