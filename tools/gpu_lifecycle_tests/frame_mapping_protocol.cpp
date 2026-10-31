// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include "frame_mapping_observer.h"
#include "frame_mapping_candidate.h"

void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL " << message << '\n'; std::exit(91); }
}
int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "normal";
    Common::FrameMappingDomain domain;
    const auto stamp = domain.Capture(4096, 8192);
    Check(stamp.has_value(), "initial capture refused");
    int reads{};
    auto read = [&] { ++reads; return true; };
    if (mode == "normal") {
        Check(domain.WithUse(*stamp, 4096, 8192, read) && reads == 1, "normal read refused");
    } else if (mode == "unmap" || mode == "reuse" || mode == "session" || mode == "failure") {
        {
            auto mutation = domain.BeginMutation();
            Check(!domain.WithUse(*stamp, 4096, 1, read), "pending mutation admitted read");
            Check(!domain.Capture(4096, 8192), "pending mutation admitted capture");
            if (mode != "failure") mutation.Complete();
        }
        Check(!domain.WithUse(*stamp, 4096, 1, read) && reads == 0, "retired stamp admitted read");
        Check(bool(domain.Capture(4096, 8192)) == (mode != "failure"), "failed mutation recaptured authority");
    } else if (mode == "active") {
        std::promise<void> entered, release, writer_waiting;
        auto release_wait = release.get_future();
        auto reader = std::async(std::launch::async, [&] {
            return domain.WithUse(*stamp, 4096, 1, [&] {
                entered.set_value(); release_wait.wait(); return true;
            });
        });
        entered.get_future().wait();
        auto writer = std::async(std::launch::async, [&] {
            on_wait = [&] { writer_waiting.set_value(); };
            auto mutation = domain.BeginMutation(); on_wait = {}; mutation.Complete();
        });
        writer_waiting.get_future().wait();
        release.set_value();
        Check(reader.get(), "active source read lost ownership"); writer.get();
        Check(!domain.WithUse(*stamp, 4096, 1, read), "active completion restored old stamp");
    } else if (mode == "nested") {
        auto first = domain.BeginMutation();
        { auto second = domain.BeginMutation(); second.Complete(); }
        Check(!domain.Capture(4096, 8192), "nested mutation reopened capture"); first.Complete();
    } else if (mode == "range") {
        Check(!domain.WithUse(*stamp, 4095, 1, read) &&
              !domain.WithUse(*stamp, 12287, 2, read) &&
              !domain.WithUse(*stamp, ~u64{0}, 2, read) && reads == 0,
              "out of range read admitted");
    } else if (mode == "close") {
        domain.Close();
        Check(!domain.WithUse(*stamp, 4096, 1, read) && !domain.Capture(4096, 1), "closed source admitted");
    } else if (mode == "foreign") {
        Common::FrameMappingDomain other;
        Check(!other.WithUse(*stamp, 4096, 1, read), "foreign stamp admitted");
    } else { Check(false, "unknown case"); }
    std::cout << "PASS mapping protocol " << mode << '\n';
}
