// SPDX-License-Identifier: GPL-2.0-or-later
// Compiled after exact production method extraction by run_m3a_tests.py.
void ProductionCases() {
    using namespace M3A;
    MappingAdmission gate;
    auto owner = gate.Register({Role::GPU, 1}, 1, std::make_shared<int>(7));
    CompletionContract completion;
    const auto accept = [&] {
        auto result = gate.TryUse({gate.Snapshot(owner)});
        Is(result.error);
        Is(result.lease.Validate([] { return true; }));
        auto accepted = completion.Accept(std::move(result.lease));
        Is(accepted.error);
        return accepted.id;
    };
    LegacyFixture::ThreadManager worker;
    worker.Start();
    const Id work = accept();
    LegacyFixture::Scheduler scheduler;
    scheduler.submission_request = [&](std::uint64_t tick) {
        Is(completion.RequestSubmit(work, {1, 0, tick}));
    };
    bool nested = false;
    worker.RunOnGpuThread([&] {
        Is(completion.CallbackStart(work));
        Is(completion.Record(work));
        worker.RunOnGpuThread([&] { nested = true; }); // exact production inline branch
        const auto tick = scheduler.Flush();
        Require(tick == 9, "Flush lost submission tick");
        Is(completion.CallbackDone(work));
        Is(completion.Wait(work, Boundary::Completed), Error::SelfWait);
    });
    auto status = completion.Inspect(work);
    Require(nested && status.callback_done && status.recorded && status.submit_requested &&
                !status.submitted && !status.completed && !status.visible,
            "production CPU callback/Flush falsely completed GPU");
    Is(completion.AcknowledgeSubmission(work, {1, 0, 9}));
    Is(completion.AcknowledgeCompletion(work, {1, 0, 9}));
    Is(completion.AcknowledgeVisibility(work));
    Require(gate.Stats().uses == 0, "production-backed milestone fixture didn't retire");

    // RunThread failure behavior is a fixture: queue fence wakes while callback is skipped.
    const Id dropped = accept();
    worker.drop_callbacks = true;
    bool executed = false;
    worker.RunOnGpuThread([&] {
        executed = true;
        Is(completion.CallbackStart(dropped));
    });
    Require(!executed && !completion.Inspect(dropped).callback_started,
            "dropped callback acknowledged execution");
    Is(completion.CancelQueued(dropped));
    worker.Stop();
    const Id stopped = accept();
    worker.RunOnGpuThread([&] { executed = true; }); // exact production stop early return
    Require(!executed, "stopped RunOnGpuThread ran callback");
    completion.Shutdown();
    Is(completion.Wait(stopped, Boundary::CallbackOnly), Error::Shutdown);

    LegacyFixture::Scheduler finish;
    finish.Finish();
    Require(finish.waited_tick == 9 && finish.wait_calls == 1,
            "Finish didn't request actual tick wait");
    Require(!finish.actual_gpu_complete, "a spy Wait request proved real GPU completion");
    std::cout << "PASS extracted production RunOnGpuThread + Scheduler Flush/Finish with fixture "
                 "queue/driver: CPU callback != submission/completion\n";
}
