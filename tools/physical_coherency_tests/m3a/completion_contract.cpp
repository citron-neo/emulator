// SPDX-License-Identifier: GPL-2.0-or-later
#include "completion_contract.h"

namespace M3A {
CompletionContract::~CompletionContract() {
    Shutdown(); // unexecuted work cancels; uncertain started work retains fail-stop pins
}
Error CompletionContract::Failure(Outcome out) {
    if (out == Outcome::Unknown)
        return Error::Unknown;
    if (out == Outcome::DeviceLost)
        return Error::DeviceLost;
    if (out == Outcome::CancelledUnexecuted || out == Outcome::ShutdownRejected)
        return Error::Shutdown;
    return Error::None;
}
bool CompletionContract::Reached(const Progress& p, Boundary b) {
    switch (b) {
    case Boundary::CallbackOnly:
        return p.callback_done;
    case Boundary::Recorded:
        return p.recorded;
    case Boundary::Submitted:
        return p.submitted;
    case Boundary::Completed:
        return p.completed;
    case Boundary::Visible:
        return p.visible;
    default:
        return false;
    }
}
CompletionContract::Accepted CompletionContract::Accept(MappingAdmission::UseLease use) {
    if (auto e = EntryCheck(false); e != Error::None)
        return {e, 0};
    if (!use.Valid())
        return {Error::Invalid, 0};
    if (auto e = use.Arm(); e != Error::None)
        return {e, 0};
    try {
        {
            std::unique_lock lock{mutex};
            MetadataMark mark;
            if (!sealed) {
                const Id id = next;
                entries.try_emplace(
                    id); // no move of armed obligation until map allocation succeeds
                auto& entry = entries.at(id);
                entry.progress.accepted = true;
                entry.use = std::move(use);
                ++next;
                cv.notify_all();
                return {Error::None, id};
            }
        }
        use.Retire(Retirement::ProvenCancelled);
        return {Error::Shutdown, 0};
    } catch (const std::bad_alloc&) {
        use.Retire(Retirement::ProvenCancelled);
        return {Error::Allocation, 0};
    }
}
Error CompletionContract::CallbackStart(Id id) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    std::lock_guard lock{mutex};
    MetadataMark mark;
    auto& p = entries.at(id).progress;
    if (auto e = Failure(p.outcome); e != Error::None)
        return e;
    if (p.callback_started)
        return Error::Invalid;
    p.callback_started = true;
    cv.notify_all();
    return Error::None;
}
Error CompletionContract::Record(Id id) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    std::lock_guard lock{mutex};
    MetadataMark mark;
    auto& p = entries.at(id).progress;
    if (auto e = Failure(p.outcome); e != Error::None)
        return e;
    if (!p.callback_started || p.callback_done || p.recorded)
        return Error::Invalid;
    p.recorded = true;
    cv.notify_all();
    return Error::None;
}
Error CompletionContract::CallbackDone(Id id) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    std::lock_guard lock{mutex};
    MetadataMark mark;
    auto& p = entries.at(id).progress;
    if (auto e = Failure(p.outcome); e != Error::None)
        return e;
    if (!p.callback_started || p.callback_done)
        return Error::Invalid;
    p.callback_done = true;
    cv.notify_all();
    return Error::None;
}
Error CompletionContract::RequestSubmit(Id id, Submission ticket) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    std::lock_guard lock{mutex};
    MetadataMark mark;
    auto& p = entries.at(id).progress;
    if (auto e = Failure(p.outcome); e != Error::None)
        return e;
    if (!p.recorded || p.submit_requested || !ticket.device || !ticket.tick)
        return Error::Invalid;
    p.submit_requested = true;
    p.ticket = ticket;
    cv.notify_all();
    return Error::None;
}
Error CompletionContract::AcknowledgeSubmission(Id id, Submission ticket) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    std::lock_guard lock{mutex};
    MetadataMark mark;
    auto& p = entries.at(id).progress;
    if (auto e = Failure(p.outcome); e != Error::None)
        return e;
    if (!p.submit_requested || p.ticket != ticket)
        return Error::Invalid;
    p.submitted = true;
    cv.notify_all();
    return Error::None;
}
Error CompletionContract::AcknowledgeCompletion(Id id, Submission ticket) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    std::lock_guard lock{mutex};
    MetadataMark mark;
    auto& p = entries.at(id).progress;
    if (auto e = Failure(p.outcome); e != Error::None)
        return e;
    if (!p.submitted || p.ticket != ticket)
        return Error::Invalid;
    p.completed = true;
    cv.notify_all();
    return Error::None;
}
Error CompletionContract::AcknowledgeVisibility(Id id) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    MappingAdmission::UseLease retired;
    {
        std::lock_guard lock{mutex};
        MetadataMark mark;
        auto& entry = entries.at(id);
        auto& p = entry.progress;
        if (auto e = Failure(p.outcome); e != Error::None)
            return e;
        if (entry.transitioning || !p.completed || !p.callback_done)
            return Error::Pending;
        p.visible = true;
        p.outcome = Outcome::Retired;
        retired = std::move(entry.use);
        cv.notify_all();
    }
    retired.Retire(Retirement::Visible);
    return Error::None;
}
Error CompletionContract::CancelQueued(Id id) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    MappingAdmission::UseLease retired;
    {
        std::lock_guard lock{mutex};
        MetadataMark mark;
        auto& entry = entries.at(id);
        auto& p = entry.progress;
        if (entry.transitioning)
            return Error::Pending;
        if (p.outcome != Outcome::Active || p.callback_started)
            return Error::Invalid;
        p.outcome = Outcome::CancelledUnexecuted;
        retired = std::move(entry.use);
        cv.notify_all();
    }
    retired.Retire(Retirement::ProvenCancelled);
    return Error::None;
}
Error CompletionContract::CancelUnsubmitted(Id id, bool proof) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    MappingAdmission::UseLease retired;
    {
        std::lock_guard lock{mutex};
        MetadataMark mark;
        auto& entry = entries.at(id);
        auto& p = entry.progress;
        if (entry.transitioning)
            return Error::Pending;
        if (!proof || p.outcome != Outcome::Active || p.submit_requested || !p.callback_done)
            return Error::Invalid;
        p.outcome = Outcome::CancelledUnexecuted;
        retired = std::move(entry.use);
        cv.notify_all();
    }
    retired.Retire(Retirement::ProvenCancelled);
    return Error::None;
}
Error CompletionContract::Fail(Id id, Outcome reason) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    MappingAdmission::UseLease* use;
    {
        std::lock_guard lock{mutex};
        MetadataMark mark;
        auto& entry = entries.at(id);
        if (entry.progress.outcome != Outcome::Active)
            return Error::Invalid;
        if (entry.transitioning)
            return Error::Pending;
        entry.transitioning = true;
        use = &entry.use;
    }
    use->Fail();
    {
        std::lock_guard lock{mutex};
        MetadataMark mark;
        auto& entry = entries.at(id);
        entry.progress.outcome = reason;
        entry.transitioning = false;
        cv.notify_all();
    }
    return Failure(reason);
}
Error CompletionContract::SubmissionFailed(Id id) {
    return Fail(id, Outcome::Unknown);
}
Error CompletionContract::CompletionFailed(Id id) {
    return Fail(id, Outcome::Unknown);
}
Error CompletionContract::VisibilityFailed(Id id) {
    return Fail(id, Outcome::Unknown);
}
Error CompletionContract::DeviceLost(Id id) {
    return Fail(id, Outcome::DeviceLost);
}
Error CompletionContract::Wait(Id id, Boundary boundary) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    std::unique_lock lock{mutex};
    auto& p = entries.at(id).progress;
    const auto ready = [&] { return Reached(p, boundary) || p.outcome != Outcome::Active; };
    if (!ready() && context.gpu)
        return Error::SelfWait;
    ++waiters;
    cv.notify_all();
    cv.wait(lock, ready);
    --waiters;
    cv.notify_all();
    if (auto e = Failure(p.outcome); e != Error::None)
        return e;
    return Reached(p, boundary) ? Error::None : Error::Invalid;
}
Progress CompletionContract::Inspect(Id id) {
    std::lock_guard lock{mutex};
    return entries.at(id).progress;
}
void CompletionContract::Shutdown() {
    {
        std::lock_guard lock{mutex};
        sealed = true;
        cv.notify_all();
    }
    Id cursor = 0;
    for (;;) {
        MappingAdmission::UseLease cancelled;
        bool started = false;
        {
            std::lock_guard lock{mutex};
            MetadataMark mark;
            auto it = entries.upper_bound(cursor);
            if (it == entries.end())
                break;
            cursor = it->first;
            auto& e = it->second;
            if (e.transitioning || e.progress.outcome != Outcome::Active)
                continue;
            started = e.progress.callback_started;
            if (!started) {
                e.progress.outcome = Outcome::ShutdownRejected;
                cancelled = std::move(e.use);
                cv.notify_all();
            }
        }
        if (started)
            Fail(cursor, Outcome::Unknown);
        else
            cancelled.Retire(Retirement::ProvenCancelled);
    }
}
Error CompletionContract::AcknowledgeDeviceQuiescent(Id id) {
    if (auto e = EntryCheck(false); e != Error::None)
        return e;
    MappingAdmission::UseLease retired;
    {
        std::lock_guard lock{mutex};
        MetadataMark mark;
        auto& entry = entries.at(id);
        if (entry.transitioning)
            return Error::Pending;
        if (entry.progress.outcome != Outcome::Unknown &&
            entry.progress.outcome != Outcome::DeviceLost)
            return Error::Invalid;
        retired = std::move(entry.use);
    }
    // Failed domain remains sealed. Safe resource destruction is not data recovery.
    retired.Retire(Retirement::DeviceQuiescent);
    return Error::None;
}
void CompletionContract::WaitForWaiters(std::size_t n) {
    std::unique_lock lock{mutex};
    cv.wait(lock, [&] { return waiters >= n; });
}
} // namespace M3A
