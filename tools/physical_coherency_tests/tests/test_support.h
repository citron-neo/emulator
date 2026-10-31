// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include "coherency_model.h"
#include "reference_memory.h"

namespace Test {
using namespace Coherency;
inline void Require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
inline Result OK(Result r) {
    Require(bool(r), std::string("unexpected ") + Name(r.error));
    return r;
}
inline void Is(Result r, Error e) {
    Require(r.error == e, std::string("expected ") + Name(e) + ", got " + Name(r.error));
}
template <class F>
void Reject(Model& m, Error e, F call) {
    const auto before = m.Fingerprint();
    Is(call(), e);
    Require(m.Fingerprint() == before, "rejection changed semantic state");
    m.CheckInvariants();
}
struct Alias {
    Stamp stamp;
    std::vector<Oracle::Range> ranges;
    std::size_t size{};
};
inline View Geometry(std::vector<Oracle::Range> ranges, Id epoch = 1, Id manager = 1,
                     Id va = 0x1000, Id daddr = 0x2000) {
    View v;
    v.epoch = epoch;
    v.manager = manager;
    v.gpu_va = va;
    v.daddr = daddr;
    for (const auto& r : ranges) {
        v.segments.push_back({epoch, v.size, r.offset, r.size, false});
        v.size += r.size;
    }
    return v;
}
inline std::vector<Oracle::Store> Stores(const Alias& a, std::size_t offset, const Bytes& data) {
    std::vector<Oracle::Store> result;
    std::size_t logical = 0;
    for (const auto& range : a.ranges) {
        const auto begin = std::max(logical, offset),
                   end = std::min(logical + range.size, offset + data.size());
        if (begin < end)
            result.push_back({range.offset + begin - logical,
                              Bytes(data.begin() + begin - offset, data.begin() + end - offset)});
        logical += range.size;
    }
    return result;
}
struct Pending {
    Ticket model;
    Oracle::EventId oracle;
    bool readonly{};
};
struct World {
    Model m;
    Oracle::ReferenceMemory oracle;
    std::map<Id, Oracle::EventId> event;
    explicit World(Bytes initial = Bytes(256, 10), Limits limits = {})
        : m(initial, 1, 32, limits), oracle(initial) {}
    Alias ViewOf(std::vector<Oracle::Range> ranges, Id manager = 1, Id va = 0x1000,
                 Id daddr = 0x2000) {
        const auto id = OK(m.RegisterView(Geometry(ranges, 1, manager, va, daddr))).id;
        std::size_t n = 0;
        for (const auto& r : ranges)
            n += r.size;
        return {m.GetStamp(id), std::move(ranges), n};
    }
    Id Rep(const Alias& a, Kind k = Kind::Staged, bool codec = true) {
        return OK(m.RegisterRepresentation(a.stamp, k, codec)).id;
    }
    std::vector<Oracle::EventId> Deps(const std::vector<Ticket>& after) const {
        std::vector<Oracle::EventId> out;
        for (const auto& t : after)
            out.push_back(event.at(t.id));
        return out;
    }
    Pending GPU(const Alias& a, Id rep, std::uint8_t value, const std::vector<Ticket>& after = {},
                unsigned queue = 0, std::size_t offset = 0, std::optional<std::size_t> size = {}) {
        Bytes bytes(size.value_or(a.size - offset), value);
        const auto t = OK(m.ReserveGPU(a.stamp, rep, after, queue)).ticket;
        OK(m.Record(t, offset, bytes));
        const auto e = oracle.Accept(Stores(a, offset, bytes), Deps(after));
        event[t.id] = e;
        m.CheckInvariants();
        return {t, e};
    }
    void Submit(Pending p) {
        OK(m.Submit(p.model));
        m.CheckInvariants();
    }
    void Complete(Pending p, bool visible = true) {
        OK(m.Complete(p.model));
        Require(oracle.Complete(p.oracle), "oracle completion dependency");
        if (visible)
            OK(m.MakeVisible(p.model));
        m.CheckInvariants();
    }
    Ticket CPUWrite(const Alias& a, std::uint8_t value, const std::vector<Ticket>& after = {},
                    std::size_t offset = 0, std::optional<std::size_t> size = {}) {
        const auto lease = OK(m.AcquireCPU(a.stamp, true, after)).ticket;
        Bytes bytes(size.value_or(a.size - offset), value);
        const auto t = OK(m.CommitCPU(lease, offset, bytes)).ticket;
        auto e = oracle.Accept(Stores(a, offset, bytes), Deps(after));
        Require(oracle.Complete(e), "oracle CPU order");
        event[t.id] = e;
        m.CheckInvariants();
        return t;
    }
    void Read(const Alias& a, Id rep = Model::CPU) {
        const auto expected = oracle.Read(a.ranges);
        Require(expected.has_value(), "oracle latest store pending");
        const auto actual = OK(m.Read(a.stamp, rep)).bytes;
        Require(actual == *expected, "byte oracle mismatch");
        m.CheckInvariants();
    }
    Ticket DMA(const Alias& src, const Alias& dst, const std::vector<Ticket>& after = {}) {
        const auto snapshot =
            oracle.DMASnapshot(src.ranges, dst.ranges); // independent, before model writes
        const auto t = OK(m.DMA(src.stamp, dst.stamp, after)).ticket;
        auto e = oracle.Accept(snapshot, Deps(after));
        Require(oracle.Complete(e), "oracle DMA order");
        event[t.id] = e;
        m.CheckInvariants();
        return t;
    }
};
} // namespace Test
