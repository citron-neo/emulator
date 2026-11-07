// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include "common/producer_drain.h"

namespace Common {
// Cold owner retirement only. Prepare records/owner captures BEFORE effects.
// Closed retirement is deferred without allocating; uncertain effects are never
// retried. The intrusive queue owns each record and its captures until drained.
// Unknown deliberately retains that graph; no native idle/teardown is implied.
class OwnedRetirementQueue {
public:
    struct Record {
        explicit Record(std::function<bool()> retire_) : retire{std::move(retire_)} {}
        Record(const Record&) = delete;
    private:
        friend class OwnedRetirementQueue;
        std::function<bool()> retire;
        Record* next{};
    };
    using Owner = std::unique_ptr<Record>;
private:
    const std::shared_ptr<ProducerDrain> domain;
    std::mutex mutex;
    Record* pending{};
    Record* unknown{};
    std::atomic<bool> failed{false};

    void KeepLocked(Owner record, bool uncertain) noexcept {
        auto*& head = uncertain ? unknown : pending;
        record->next = head;
        head = record.release();
    }
    void Keep(Owner record, bool uncertain) noexcept {
        std::scoped_lock lock{mutex};
        KeepLocked(std::move(record), uncertain);
    }
    Owner Pop() noexcept {
        std::scoped_lock lock{mutex};
        if (!pending) return {};
        auto* record = pending;
        pending = record->next;
        record->next = nullptr;
        return Owner{record};
    }
    bool Execute(Owner& record) noexcept {
        {
            std::scoped_lock lock{mutex};
            if (failed.load(std::memory_order_acquire)) {
                KeepLocked(std::move(record), true);
                return false;
            }
        }
        try {
            if (record->retire()) {
                // A newer failure may precede this older callback's return.
                // Serialize the release decision with quarantine publication.
                std::scoped_lock lock{mutex};
                if (!failed.load(std::memory_order_acquire)) return true;
                KeepLocked(std::move(record), true);
                return false;
            }
        } catch (...) {
            // A callback may have partially retired owners. Never replay it.
        }
        Quarantine(std::move(record));
        return false;
    }
public:
    explicit OwnedRetirementQueue(std::shared_ptr<ProducerDrain> domain_)
        : domain{std::move(domain_)} {}
    static Owner Prepare(std::function<bool()> retire) {
        return std::make_unique<Record>(std::move(retire));
    }
    void Retire(Owner record) noexcept {
        if (!record) return;
        auto entry = domain->EnterCold();
        if (!entry || failed.load(std::memory_order_acquire)) {
            Keep(std::move(record), failed.load(std::memory_order_acquire));
            return;
        }
        Execute(record);
        record.reset(); // Release captures before the counted entry leaves.
    }
    // Construction failed after possible effects: retain BEFORE the caller's
    // operation entry exits, without attempting speculative cleanup.
    void Quarantine(Owner record) noexcept {
        std::scoped_lock lock{mutex};
        failed.store(true, std::memory_order_release);
        domain->Seal();
        if (record) KeepLocked(std::move(record), true);
    }
    // Call only before effects of an admitted cold dispatch. Each queued callback
    // needs fresh admission; an old dispatch cannot replay retirement after Seal.
    bool Drain() noexcept {
        for (unsigned i = 0; i < 32; ++i) {
            if (failed.load(std::memory_order_acquire)) return false;
            auto record = Pop();
            if (!record) return true;
            auto entry = domain->EnterCold();
            if (!entry) { Keep(std::move(record), false); return false; }
            if (!Execute(record)) return false;
            record.reset(); // Final owner destruction is part of this entry.
        }
        return true; // Remaining queued records still retain their owners.
    }
};
} // namespace Common
