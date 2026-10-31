// SPDX-License-Identifier: GPL-2.0-or-later
#include <fstream>
#include <random>
#include <sstream>
#include "test_support.h"

using namespace Test;
struct Command {
    unsigned code{}, a{}, b{}, physical{}, value{}, queue{};
};
std::string Describe(const Command& c) {
    std::ostringstream s;
    s << c.code << ' ' << c.a << ' ' << c.b << ' ' << c.physical << ' ' << c.value << ' '
      << c.queue;
    return s.str();
}
Metrics Replay(const std::vector<Command>& commands) {
    World w(Bytes(64, 10));
    std::vector<Alias> aliases;
    std::vector<Id> reps;
    for (unsigned i = 0; i < 8; ++i) {
        aliases.push_back(
            w.ViewOf({{i * 4, 8}}, i % 3 + 1, 0x1000 + i * 0x1000, 0x8000 + i * 0x1000));
        reps.push_back(w.Rep(aliases.back(), i % 3 == 0 ? Kind::Scratch : Kind::Staged));
    }
    const auto whole = w.ViewOf({{0, 64}}, 9, 0x100000, 0x200000);
    const auto parallel_a = w.ViewOf({{0, 8}}, 10, 0x300000, 0x400000);
    const auto parallel_b = w.ViewOf({{48, 8}}, 11, 0x500000, 0x600000);
    auto parallel_ra = w.Rep(parallel_a), parallel_rb = w.Rep(parallel_b);
    std::vector<Pending> pending;
    std::vector<Ticket> frontier;
    const auto after = [&] { return frontier; };
    const auto finish_one = [&](std::size_t index) {
        const auto p = pending.at(index);
        const auto before = w.m.Fingerprint();
        auto r = w.m.Complete(p.model);
        if (!r) {
            Is(r, Error::Pending);
            Require(w.m.Fingerprint() == before, "premature completion changed state");
            Require(!w.oracle.Complete(p.oracle), "oracle/model dependency disagreement");
            return false;
        }
        Require(w.oracle.Complete(p.oracle), "oracle completion failed");
        OK(w.m.MakeVisible(p.model));
        if (!p.readonly)
            OK(w.m.Scatter(p.model));
        pending.erase(pending.begin() + index);
        return true;
    };
    const auto finish_all = [&] {
        while (!pending.empty()) {
            bool progress = false;
            for (std::size_t i = pending.size(); i > 0; --i)
                progress = finish_one(i - 1) || progress;
            Require(progress, "completion graph deadlocked");
        }
        w.Read(whole);
    };
    for (std::size_t step = 0; step < commands.size(); ++step) {
        const auto c = commands[step];
        auto& a = aliases[c.a % 8];
        auto& b = aliases[c.b % 8];
        const auto rep = reps[c.a % 8];
        try {
            switch (c.code) {
            case 0:
                finish_all();
                frontier = {w.CPUWrite(a, static_cast<std::uint8_t>(c.value), after())};
                break;
            case 1: {
                const auto check = w.m.Preflight({{a.stamp, rep, true}});
                if (!check) {
                    Reject(w.m, Error::UnsupportedAlias,
                           [&] { return w.m.ReserveGPU(a.stamp, rep, after(), c.queue); });
                    break;
                }
                const auto before = w.m.Fingerprint();
                auto r = w.m.ReserveGPU(a.stamp, rep, after(), c.queue);
                if (r.error == Error::ResourceBusy) {
                    Require(w.m.Fingerprint() == before, "busy reserve mutated");
                    finish_all();
                    r = w.m.ReserveGPU(a.stamp, rep, after(), c.queue);
                }
                auto t = OK(r).ticket;
                Bytes bytes(a.size, static_cast<std::uint8_t>(c.value));
                OK(w.m.Record(t, 0, bytes));
                const auto e = w.oracle.Accept(Stores(a, 0, bytes), w.Deps(after()));
                w.event[t.id] = e;
                OK(w.m.Submit(t));
                pending.push_back({t, e});
                frontier = {t};
                break;
            }
            case 2:
                if (!pending.empty())
                    finish_one(c.b % pending.size());
                break;
            case 3: {
                const auto before = w.m.Fingerprint();
                auto r = w.m.Read(a.stamp, rep);
                if (r) {
                    const auto expected = w.oracle.Read(a.ranges);
                    Require(expected && *expected == r.bytes, "random read oracle mismatch");
                } else {
                    Require(r.error == Error::Pending || r.error == Error::ResourceBusy,
                            "unexpected random read failure");
                    Require(w.m.Fingerprint() == before, "failed read changed state");
                }
                break;
            }
            case 4:
                finish_all();
                frontier = {w.DMA(a, b, after())};
                break;
            case 5: {
                finish_all();
                OK(w.m.Evict(rep));
                const auto stale = a.stamp;
                std::vector<Oracle::Range> ranges{{c.physical % 57, 4},
                                                  {(c.physical * 7 + 3) % 57, 4}};
                OK(w.m.Remap(a.stamp, Geometry(ranges, 1, c.a % 3 + 1, 0x1000 + c.a * 0x1000,
                                               0x8000 + c.a * 0x1000)));
                Reject(w.m, Error::StaleStamp, [&] { return w.m.Read(stale, Model::CPU); });
                a.stamp = w.m.GetStamp(stale.view);
                a.ranges = ranges;
                reps[c.a % 8] = w.Rep(a, c.b % 2 ? Kind::Scratch : Kind::Texture);
                break;
            }
            case 6:
                w.m.Inject(Fault::Allocation);
                Reject(w.m, Error::Allocation, [&] { return w.m.DMA(a.stamp, b.stamp, after()); });
                break;
            case 7:
                Reject(w.m, Error::UnknownCoverage,
                       [&] { return w.m.Preflight({{a.stamp, rep, true, true, false}}); });
                break;
            case 8: {
                finish_all();
                const auto check = w.m.Preflight({{a.stamp, rep, true}});
                if (check) {
                    auto p = w.GPU(a, rep, static_cast<std::uint8_t>(c.value), after());
                    OK(w.m.Cancel(p.model));
                    Require(w.oracle.Cancel(p.oracle), "random oracle cancellation");
                }
                break;
            }
            case 9:
                finish_all();
                OK(w.m.DrainRenderer());
                for (unsigned i = 0; i < 8; ++i)
                    reps[i] = w.Rep(aliases[i]);
                parallel_ra = w.Rep(parallel_a);
                parallel_rb = w.Rep(parallel_b);
                break;
            case 10: {
                finish_all();
                const auto deps = after();
                const auto p =
                    w.GPU(parallel_a, parallel_ra, static_cast<std::uint8_t>(c.value), deps, 0);
                const auto q =
                    w.GPU(parallel_b, parallel_rb, static_cast<std::uint8_t>(c.value + 1), deps, 1);
                w.Submit(p);
                w.Submit(q);
                pending.push_back(p);
                pending.push_back(q);
                frontier = {p.model, q.model};
                break;
            }
            case 11: {
                finish_all();
                const auto t =
                    OK(w.m.ReserveDispatch({{a.stamp, rep, false}}, after(), c.queue)).ticket;
                OK(w.m.Record(t, 0, {}));
                OK(w.m.Submit(t));
                const auto e = w.oracle.Accept({}, w.Deps(after()));
                w.event[t.id] = e;
                pending.push_back({t, e, true});
                frontier = {t};
                break;
            }
            default:
                throw std::logic_error("invalid random command");
            }
            w.m.CheckInvariants();
        } catch (const std::exception& e) {
            throw std::runtime_error("step " + std::to_string(step) + " [" + Describe(c) + "] " +
                                     e.what());
        }
    }
    finish_all();
    w.m.CheckInvariants();
    return w.m.Statistics();
}
bool Fails(const std::vector<Command>& trace) {
    try {
        Replay(trace);
        return false;
    } catch (...) {
        return true;
    }
}
std::vector<Command> Minimize(std::vector<Command> trace) {
    // Delta reduction is rerun from fresh independent model/oracle state.
    // A 1-minimal replayable subsequence, not a claim of global minimum.
    for (std::size_t span = trace.size() / 2; span; span /= 2) {
        for (std::size_t i = 0; i + span <= trace.size();) {
            auto smaller = trace;
            smaller.erase(smaller.begin() + i, smaller.begin() + i + span);
            if (!smaller.empty() && Fails(smaller)) {
                trace = std::move(smaller);
                i = 0; // Recheck earlier commands; required for 1-minimality at span=1.
            } else
                ++i;
        }
    }
    return trace;
}
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--replay") {
        try {
            std::ifstream input(argv[2]);
            Require(bool(input), "replay file missing");
            std::vector<Command> trace;
            std::string line;
            while (std::getline(input, line)) {
                if (line.empty())
                    continue;
                std::istringstream parse(line);
                Command c;
                std::string extra;
                Require(bool(parse >> c.code >> c.a >> c.b >> c.physical >> c.value >> c.queue) &&
                            !(parse >> extra) && c.code < 12,
                        "invalid replay command");
                trace.push_back(c);
            }
            Replay(trace);
            std::cout << "PASS replay commands=" << trace.size() << '\n';
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "FAIL replay: " << e.what() << '\n';
            return 1;
        }
    }
    const std::uint64_t first = argc > 1 ? std::stoull(argv[1]) : 372;
    const unsigned steps = argc > 2 ? std::stoul(argv[2]) : 400,
                   seeds = argc > 3 ? std::stoul(argv[3]) : 8;
    for (unsigned trial = 0; trial < seeds; ++trial) {
        const auto seed = first + trial;
        std::mt19937_64 rng(seed);
        std::vector<Command> trace;
        for (unsigned i = 0; i < steps; ++i)
            trace.push_back({unsigned(rng() % 12), unsigned(rng() % 8), unsigned(rng() % 8),
                             unsigned(rng() % 57), unsigned(rng() % 256), unsigned(rng() % 3)});
        try {
            const auto m = Replay(trace);
            // Failure during outstanding GPU work: independent small trial so
            // explicit quarantine is not incorrectly recovered by later commands.
            for (unsigned inject = 0; inject < 3; ++inject) {
                const auto fault_offset = unsigned(rng() % 48);
                const auto fault_value = static_cast<std::uint8_t>(rng() % 256);
                try {
                    World q(Bytes(64, 10));
                    auto a = q.ViewOf({{fault_offset, 16}});
                    const auto rep = q.Rep(a, Kind::Scratch);
                    auto p = q.GPU(a, rep, fault_value);
                    const auto f = inject == 0   ? Fault::Submit
                                   : inject == 1 ? Fault::Complete
                                                 : Fault::Visibility;
                    if (inject)
                        q.Submit(p);
                    if (inject == 2)
                        q.Complete(p, false);
                    const auto old = q.m.ReplicaBytes(Model::CPU);
                    q.m.Inject(f);
                    Is(inject == 0   ? q.m.Submit(p.model)
                       : inject == 1 ? q.m.Complete(p.model)
                                     : q.m.MakeVisible(p.model),
                       Error::Unknown);
                    Require(q.m.ReplicaBytes(Model::CPU) == old &&
                                q.m.Status(p.model) == Phase::Unknown,
                            "random fault published stale CPU bytes");
                    Reject(q.m, Error::ResourceBusy, [&] { return q.m.Evict(rep); });
                    Reject(q.m, Error::Unknown, [&] { return q.m.Read(a.stamp, Model::CPU); });
                    q.m.CheckInvariants();
                } catch (const std::exception& e) {
                    std::cerr << "FAIL bounded fault trace seed=" << seed << " stage=" << inject
                              << " offset=" << fault_offset << " size=16 value=" << +fault_value
                              << ": " << e.what() << "\nReplay --seed " << seed << " --steps "
                              << steps
                              << " --seeds 1; reserve/record, optional submit/complete, inject "
                                 "stage, verify quarantine/lease/read rejection\n";
                    return 1;
                }
            }
            std::cout << "PASS random seed=" << seed << " commands=" << steps
                      << " cells=" << m.cells << " replicas=" << m.replicas
                      << " producers=" << m.producers << " pending=" << m.pending_producers
                      << " splits=" << m.splits << " coalesces=" << m.coalesces
                      << " metadata=" << m.metadata_bytes << " peak=" << m.peak_metadata_bytes
                      << " rejects=" << m.rejected << " unknown=" << m.quarantined
                      << " isolated_quarantines=3"
                      << " calls=" << m.calls << " mean_ns=" << m.elapsed_ns / m.calls
                      << " max_ns=" << m.max_call_ns << '\n';
        } catch (const std::exception& e) {
            std::cerr << "FAIL seed=" << seed << " original: " << e.what() << "\nFULL TRACE\n";
            for (const auto& c : trace)
                std::cerr << Describe(c) << '\n';
            const auto smaller = Minimize(trace);
            std::ofstream full_file("failing-full-" + std::to_string(seed) + ".txt");
            std::ofstream minimal_file("failing-minimal-" + std::to_string(seed) + ".txt");
            for (const auto& c : trace)
                full_file << Describe(c) << '\n';
            for (const auto& c : smaller)
                minimal_file << Describe(c) << '\n';
            std::cerr << "1-MINIMAL TRACE\n";
            for (const auto& c : smaller)
                std::cerr << Describe(c) << '\n';
            return 1;
        }
    }
}
