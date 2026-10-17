// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <optional>
#include "mapping_admission.h"

namespace M3A {
struct Submission {
    Id device{}, queue{}, tick{};
    bool operator==(const Submission&) const = default;
};
enum class Outcome { Active, CancelledUnexecuted, ShutdownRejected, Unknown, DeviceLost, Retired };
struct Progress {
    bool accepted{}, callback_started{}, callback_done{}, recorded{}, submit_requested{},
        submitted{}, completed{}, visible{};
    Outcome outcome{Outcome::Active};
    std::optional<Submission> ticket;
};
class CompletionContract {
    struct Entry {
        Progress progress;
        MappingAdmission::UseLease use;
        bool transitioning{};
    };
    std::mutex mutex;
    std::condition_variable cv;
    std::map<Id, Entry> entries;
    Id next{1};
    bool sealed{};
    std::size_t waiters{};
    static bool Reached(const Progress&, Boundary);
    static Error Failure(Outcome);
    Error Fail(Id, Outcome);

public:
    ~CompletionContract();
    struct Accepted {
        Error error{};
        Id id{};
    };
    Accepted Accept(MappingAdmission::UseLease use);
    Error CallbackStart(Id);
    Error Record(Id);
    Error CallbackDone(Id);
    Error RequestSubmit(Id, Submission);
    Error AcknowledgeSubmission(Id, Submission);
    Error AcknowledgeCompletion(Id, Submission);
    Error AcknowledgeVisibility(Id);
    Error CancelQueued(Id);
    Error CancelUnsubmitted(Id, bool no_future_submission_proven);
    Error SubmissionFailed(Id);
    Error CompletionFailed(Id);
    Error VisibilityFailed(Id);
    Error DeviceLost(Id);
    Error Wait(Id, Boundary);
    Progress Inspect(Id);
    void Shutdown();
    // Trusted fixture/device adapter evidence; never implies data visibility.
    Error AcknowledgeDeviceQuiescent(Id);
    void WaitForWaiters(std::size_t minimum);
};
} // namespace M3A
