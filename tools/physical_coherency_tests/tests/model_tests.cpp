// SPDX-License-Identifier: GPL-2.0-or-later
#include <functional>
#include "test_support.h"

using namespace Test;
void Partial() {
    for (bool cpu_first : {false, true}) {
        World w;
        auto whole = w.ViewOf({{0, 256}}), prefix = w.ViewOf({{0, 64}}),
             middle = w.ViewOf({{64, 64}});
        auto p = w.GPU(middle, w.Rep(middle), 99);
        w.Submit(p);
        if (cpu_first)
            w.CPUWrite(prefix, 77);
        w.Complete(p);
        if (!cpu_first)
            w.CPUWrite(prefix, 77);
        w.Read(whole);
        const auto bytes = w.m.ReplicaBytes(Model::CPU);
        Require(bytes[0] == 77 && bytes[64] == 99 && bytes[128] == 10,
                "partial write preservation");
    }
    World w;
    auto all = w.ViewOf({{0, 128}}), prefix = w.ViewOf({{0, 64}});
    auto p = w.GPU(all, w.Rep(all), 99);
    w.Submit(p);
    w.Complete(p);
    const auto cpu = w.CPUWrite(prefix, 77, {p.model});
    const Id version = w.m.VersionClock();
    OK(w.m.Complete(p.model)); // late duplicate completion cannot resurrect GPU99 over CPU77
    OK(w.m.Scatter(p.model));
    Require(w.m.VersionClock() == version, "scatter creates no version");
    w.Read(all);
    Require(w.m.ReplicaBytes(Model::CPU)[0] == 77, "stale full readback overwrote CPU77");
    auto later = w.GPU(prefix, w.Rep(prefix), 99, {cpu});
    w.Submit(later);
    w.Complete(later);
    w.Read(prefix);
}
void CrossAliases() {
    for (bool same_manager : {false, true})
        for (bool same_daddr : {false, true}) {
            World w;
            auto a = w.ViewOf({{0, 32}}, 1, 0x1000, 0x2000),
                 b = w.ViewOf({{0, 32}}, same_manager ? 1 : 2, 0x9000,
                              same_daddr ? 0x2000 : 0xa000);
            const auto ra = w.Rep(a), rb = w.Rep(b);
            OK(w.m.Synchronize(ra));
            OK(w.m.Synchronize(rb));
            auto p = w.GPU(a, ra, 99);
            w.Submit(p);
            w.Complete(p);
            Require(w.m.ReplicaBytes(rb) == Bytes(32, 10), "replica should remain stale");
            Require(*w.oracle.Read(b.ranges) != w.m.ReplicaBytes(Model::CPU),
                    "independent oracle detects stale CPU10");
            w.Read(b, rb);
        }
    World w;
    auto a = w.ViewOf({{16, 32}}), b = w.ViewOf({{32, 32}}, 2, 0x8000, 0x9000);
    auto p = w.GPU(a, w.Rep(a), 99);
    w.Submit(p);
    w.Complete(p);
    w.Read(b, w.Rep(b));
}
void GeometryCases() {
    World w;
    auto repeated = w.ViewOf({{0, 8}, {32, 8}, {0, 8}});
    const auto scratch = w.Rep(repeated, Kind::Scratch);
    OK(w.m.Gather(scratch));
    w.Read(repeated, scratch);
    Require(w.m.ReplicaBytes(scratch) == Bytes(24, 10), "ordered repeated gather");
    Reject(w.m, Error::UnsupportedAlias,
           [&] { return w.m.ReserveGPU(repeated.stamp, scratch, {}); });
    View hole = Geometry({{0, 8}, {16, 8}});
    hole.segments[1].hole = true;
    auto id = OK(w.m.RegisterView(hole)).id;
    const auto cells = w.m.Cells();
    Reject(w.m, Error::Hole,
           [&] { return w.m.RegisterRepresentation(w.m.GetStamp(id), Kind::Staged); });
    Require(w.m.Cells() == cells, "hole created authority cell");
    auto noncontig = w.ViewOf({{40, 8}, {8, 8}});
    auto p = w.GPU(noncontig, w.Rep(noncontig), 55);
    w.Submit(p);
    w.Complete(p);
    w.Read(noncontig);
}
void ScratchImage() {
    World w;
    auto a = w.ViewOf({{0, 32}}), b = w.ViewOf({{0, 32}}, 2, 0x9000, 0xa000);
    auto ra = w.Rep(a, Kind::Scratch), rb = w.Rep(b, Kind::Texture);
    auto p = w.GPU(a, ra, 99);
    w.Submit(p);
    w.Complete(p);
    auto q = w.GPU(b, rb, 77, {p.model});
    w.Submit(q);
    w.Complete(q);
    OK(w.m.Scatter(p.model));
    w.Read(b);
    Require(w.m.ReplicaBytes(Model::CPU)[0] == 77, "SSBO99/image77 overwritten");
    Reject(w.m, Error::UnsupportedAlias,
           [&] { return w.m.Preflight({{a.stamp, ra, true}, {b.stamp, rb, true}}); });
    Reject(w.m, Error::UnsupportedAlias, [&] {
        return w.m.ReserveDispatch({{a.stamp, ra, true}, {b.stamp, rb, true}}, {q.model});
    });
    Reject(w.m, Error::UnsupportedAlias,
           [&] { return w.m.Preflight({{a.stamp, ra, false}, {b.stamp, rb, true}}); });
    OK(w.m.Preflight({{a.stamp, ra, false},
                      {b.stamp, rb, false}})); // stable readonly copies, no executing writer
    OK(w.m.Preflight(
        {{a.stamp, ra, true}, {a.stamp, ra, false}})); // same actual linear storage positions
    auto far = w.ViewOf({{64, 32}});
    OK(w.m.Preflight({{a.stamp, ra, true}, {far.stamp, w.Rep(far), true}}));
    const auto batch =
        OK(w.m.ReserveDispatch({{a.stamp, ra, true}, {far.stamp, w.Rep(far), true}}, {q.model}));
    Require(batch.tickets.size() == 2, "non-overlapping dispatch producers not admitted");
    for (auto t : batch.tickets)
        OK(w.m.Cancel(t));
}
void Overfetch() {
    {
        World exact;
        auto a = exact.ViewOf({{0, 32}}), b = exact.ViewOf({{64, 32}});
        auto p = exact.GPU(a, exact.Rep(a), 99);
        exact.Submit(p);
        exact.Complete(p);
        auto q = exact.GPU(b, exact.Rep(b), 77);
        exact.Submit(q);
        exact.Read(a);
        const auto versions = exact.m.ReplicaVersions(Model::CPU);
        Require(versions[0] == p.model.id && versions[64] == 0,
                "partial readback cleared undownloaded GPU bytes");
        Require(exact.m.Statistics().pending_producers == 1 && exact.m.Statistics().producers == 2,
                "producer accounting lost pending range");
        exact.Complete(q);
        exact.Read(b);
    }
    World w;
    auto page = w.ViewOf({{0, 128}}), prefix = w.ViewOf({{0, 64}}), mid = w.ViewOf({{64, 32}});
    const auto image = w.Rep(page, Kind::Texture);
    auto p = w.GPU(page, image, 99);
    w.Submit(p);
    w.Complete(p);
    const auto cpu = w.CPUWrite(prefix, 77, {p.model});
    auto q = w.GPU(mid, w.Rep(mid), 66, {cpu});
    w.Submit(q);
    OK(w.m.Scatter(p.model)); // only old version's remaining [96,128), not pending q or CPU77
    const auto tags = w.m.ReplicaVersions(Model::CPU);
    Require(tags[0] == cpu.id && tags[64] != q.model.id && tags[96] == p.model.id,
            "dirty-clear extent mismatch");
    w.Complete(q);
    w.Read(page);
    Require(w.m.ReplicaBytes(Model::CPU)[64] == 66, "partial reconciliation");
}
void Lifetime() {
    World w;
    auto a = w.ViewOf({{0, 64}}), alias = w.ViewOf({{0, 64}}, 2, 0x9000, 0xa000);
    auto rep = w.Rep(a, Kind::Scratch);
    auto p = w.GPU(a, rep, 99);
    w.Submit(p);
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.Evict(rep); });
    OK(w.m.Unmap(a.stamp));
    Reject(w.m, Error::StaleStamp, [&] { return w.m.Read(a.stamp, rep); });
    auto resized = w.ViewOf({{0, 128}});
    const auto replacement = w.Rep(resized);
    Reject(w.m, Error::Pending, [&] { return w.m.Read(resized.stamp, replacement); });
    w.Complete(p);
    w.Read(resized, replacement);
    w.Read(alias);
    Reject(w.m, Error::NeedsPreservation, [&] { return w.m.Evict(rep); });
    OK(w.m.Scatter(p.model));
    OK(w.m.Evict(rep));
    w.Read(alias);
    const auto clock = w.m.VersionClock();
    OK(w.m.Remap(alias.stamp, Geometry({{128, 64}}, 1, 2, 0x9000, 0xa000)));
    Require(w.m.VersionClock() == clock, "remap is not CPU write");
    alias.stamp = w.m.GetStamp(alias.stamp.view);
    alias.ranges = {{128, 64}};
    w.Read(alias);
}
void AsyncOrdering() {
    World w;
    auto a = w.ViewOf({{0, 32}}), b = w.ViewOf({{32, 32}});
    auto p = w.GPU(a, w.Rep(a), 99, {}, 0);
    Reject(w.m, Error::Pending, [&] { return w.m.Read(a.stamp, Model::CPU); });
    w.Submit(p);
    auto q = w.GPU(b, w.Rep(b), 77, {}, 1);
    w.Submit(q);
    w.Complete(q);
    w.Read(b);
    Reject(w.m, Error::Pending, [&] { return w.m.Read(a.stamp, Model::CPU); });
    w.Complete(p, false);
    Reject(w.m, Error::SyncRequired, [&] { return w.m.Read(a.stamp, Model::CPU); });
    OK(w.m.MakeVisible(p.model));
    w.Read(a);
    auto alias = w.ViewOf({{0, 32}}, 2, 0x9000, 0xa000);
    const auto rep = w.Rep(alias);
    Reject(w.m, Error::Unordered, [&] { return w.m.ReserveGPU(alias.stamp, rep, {}, 1); });
    auto old = w.GPU(a, w.Rep(a), 55, {p.model});
    w.Submit(old);
    auto newer = w.GPU(alias, rep, 66, {old.model}, 1);
    w.Submit(newer);
    Reject(w.m, Error::Pending, [&] { return w.m.Complete(newer.model); });
    Require(!w.oracle.Complete(newer.oracle), "oracle rejects premature dependent completion");
    w.Complete(old);
    w.Complete(newer);
    OK(w.m.Complete(old.model));
    w.Read(alias);
}
void CPUAdmission() {
    World w;
    auto a = w.ViewOf({{0, 32}}), b = w.ViewOf({{32, 32}});
    const auto ra = w.Rep(a);
    const auto lease = OK(w.m.AcquireCPU(a.stamp, true, {})).ticket;
    const auto clock = w.m.VersionClock();
    Require(clock == 0, "pre-store notification created version");
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.Read(a.stamp, Model::CPU); });
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.ReserveGPU(a.stamp, ra, {}); });
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.Unmap(a.stamp); });
    w.Read(b);
    OK(w.m.ReleaseCPU(lease));
    w.Read(a);
    Reject(w.m, Error::RawAccess, [&] { return w.m.AcquireCPU(a.stamp, true, {}, false); });
    Reject(w.m, Error::RawAccess, [&] { return w.m.AcquireCPU(a.stamp, false, {}, false); });
    auto cross = w.ViewOf({{28, 8}});
    Reject(w.m, Error::RawAccess, [&] { return w.m.AcquireCPU(cross.stamp, true, {}, false); });
}
void ReadLeases() {
    World w;
    auto a = w.ViewOf({{0, 16}}), b = w.ViewOf({{0, 16}}, 2, 0x9000, 0xa000),
         far = w.ViewOf({{32, 16}});
    const auto ra = w.Rep(a), rb = w.Rep(b);
    const auto read =
        OK(w.m.ReserveDispatch({{a.stamp, ra, false}, {b.stamp, rb, false}}, {})).ticket;
    Require(w.m.VersionClock() == 0, "readonly dispatch created data version");
    const auto e = w.oracle.Accept({});
    w.event[read.id] = e;
    OK(w.m.Record(read, 0, {}));
    OK(w.m.Submit(read));
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.Evict(ra); });
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.Evict(rb); });
    Reject(w.m, Error::Unordered, [&] { return w.m.AcquireCPU(a.stamp, true, {}); });
    Reject(w.m, Error::Pending, [&] { return w.m.AcquireCPU(a.stamp, true, {read}); });
    // An explicit dependency must be honored even on a disjoint footprint.
    Reject(w.m, Error::Pending, [&] { return w.m.AcquireCPU(far.stamp, true, {read}); });
    OK(w.m.Complete(read));
    Require(w.oracle.Complete(e), "oracle readonly dependency");
    OK(w.m.MakeVisible(read));
    Reject(w.m, Error::Unordered, [&] { return w.m.AcquireCPU(a.stamp, true, {}); });
    w.CPUWrite(a, 77, {read});
    Require(w.m.VersionClock() == 1, "operation IDs confused with versions");
    w.Read(a);
    OK(w.m.Evict(rb));
    const auto rr = w.Rep(b), rw = w.Rep(far);
    const auto deps = Ticket{1, 2};
    const auto mixed =
        OK(w.m.ReserveDispatch({{b.stamp, rr, false}, {far.stamp, rw, true}}, {deps})).ticket;
    Reject(w.m, Error::Unordered, [&] { return w.m.AcquireCPU(a.stamp, true, {deps}); });
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.Evict(rr); });
    OK(w.m.Record(mixed, 0, Bytes(16, 88)));
    OK(w.m.Submit(mixed));
    OK(w.m.Complete(mixed));
    OK(w.m.MakeVisible(mixed));
    w.m.CheckInvariants();
    Require(w.m.VersionClock() == 2, "mixed dispatch version count");
}
void MappingAdmission() {
    World w;
    auto a = w.ViewOf({{0, 32}});
    auto rep = w.Rep(a);
    const auto stamp = a.stamp;
    OK(w.m.BeginMutation(stamp));
    Reject(w.m, Error::MutationInProgress, [&] { return w.m.ReserveGPU(stamp, rep, {}); });
    OK(w.m.EndMutation(stamp.view));
    OK(w.m.Remap(stamp, Geometry({{32, 32}})));
    Reject(w.m, Error::StaleStamp, [&] { return w.m.ReserveGPU(stamp, rep, {}); });
    a.stamp = w.m.GetStamp(stamp.view);
    a.ranges = {{32, 32}};
    auto fresh = w.Rep(a);
    w.Read(a, fresh);
}
void BackingAndRenderer() {
    World w;
    auto a = w.ViewOf({{0, 32}});
    auto rep = w.Rep(a);
    auto p = w.GPU(a, rep, 99);
    w.Submit(p);
    Reject(w.m, Error::Pending, [&] { return w.m.DrainRenderer(); });
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.ReplaceBacking(Bytes(256, 0), 2); });
    w.Complete(p);
    OK(w.m.DrainRenderer());
    w.Read(a);
    Require(w.m.Statistics().replicas == 1, "renderer leases remain");
    auto new_renderer = w.Rep(a);
    w.Read(a, new_renderer);
    OK(w.m.Evict(new_renderer));
    OK(w.m.ReplaceBacking(Bytes(256, 42), 2));
    w.oracle.Reset(Bytes(256, 42));
    Reject(w.m, Error::EpochDead, [&] { return w.m.Complete(p.model); });
    Reject(w.m, Error::EpochDead, [&] { return w.m.Read(a.stamp, Model::CPU); });
    auto id = OK(w.m.RegisterView(Geometry({{0, 32}}, 2))).id;
    Require(OK(w.m.Read(w.m.GetStamp(id), Model::CPU)).bytes == Bytes(32, 42),
            "backing epoch reused stale data");
}
void Coverage() {
    World w;
    auto a = w.ViewOf({{0, 32}});
    auto r = w.Rep(a);
    Reject(w.m, Error::UnknownCoverage,
           [&] { return w.m.Preflight({{a.stamp, r, true, true, false}}); });
    Reject(w.m, Error::UnknownCoverage,
           [&] { return w.m.ReserveDispatch({{a.stamp, r, true, true, false}}, {}); });
    Reject(w.m, Error::UnknownCoverage,
           [&] { return w.m.Preflight({{a.stamp, r, true, false, true}}); });
    Reject(w.m, Error::Layout,
           [&] { return w.m.Preflight({{a.stamp, r, true, true, true, false}}); });
    auto image = w.Rep(a, Kind::Texture, false);
    Reject(w.m, Error::Layout, [&] { return w.m.ReserveGPU(a.stamp, image, {}); });
    auto opaque = w.Rep(a, Kind::Opaque, false);
    Reject(w.m, Error::Layout, [&] { return w.m.Synchronize(opaque); });
}
void Chunks() {
    World w;
    auto a = w.ViewOf({{0, 8}}), other = w.ViewOf({{16, 8}});
    w.m.SetNonCoherent(0, true);
    const auto clock = w.m.VersionClock();
    OK(w.m.BeginGPUAccess(0, true));
    Reject(w.m, Error::Retry, [&] { return w.m.EndCPUAccess(0, Ack::Retry, true); });
    Reject(w.m, Error::SyncRequired, [&] { return w.m.EndCPUAccess(0, Ack::Success, false); });
    OK(w.m.EndCPUAccess(0, Ack::Success, true));
    auto p = w.GPU(a, w.Rep(a), 99);
    OK(w.m.UseChunk(p.model, 0));
    w.Submit(p);
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.AcquireCPU(other.stamp, false, {}); });
    Reject(w.m, Error::Pending, [&] { return w.m.BeginCPUAccess(0); });
    w.Complete(p);
    auto outside = w.ViewOf({{40, 8}});
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.DMA(a.stamp, outside.stamp, {p.model}); });
    // Consecutive views of the same chunk reuse access; no second END.
    auto q = w.GPU(other, w.Rep(other), 77, {p.model});
    OK(w.m.UseChunk(q.model, 0));
    w.Submit(q);
    w.Complete(q);
    OK(w.m.BeginCPUAccess(0));
    Reject(w.m, Error::SyncRequired, [&] { return w.m.PrepareCPUStart(0, true, false); });
    Reject(w.m, Error::SyncRequired, [&] { return w.m.PrepareCPUStart(0, false, true); });
    OK(w.m.PrepareCPUStart(0, true, true));
    Reject(w.m, Error::Retry, [&] { return w.m.StartCPUAccess(0, Ack::Retry); });
    OK(w.m.StartCPUAccess(0, Ack::Success));
    w.Read(a);
    w.Read(other);
    Require(w.m.VersionClock() == clock + 2, "chunk flips changed versions");
    const auto lease = OK(w.m.AcquireCPU(a.stamp, false, {p.model, q.model})).ticket;
    Reject(w.m, Error::ResourceBusy, [&] { return w.m.BeginGPUAccess(0, true); });
    OK(w.m.ReleaseCPU(lease));
    Reject(w.m, Error::UnknownCoverage, [&] { return w.m.BeginGPUAccess(0, false); });
    World failed;
    OK(failed.m.BeginGPUAccess(0, true));
    Is(failed.m.EndCPUAccess(0, Ack::Fail, true), Error::Unknown);
    Require(failed.m.ChunkStatus(0) == Access::Unknown, "failed END opened GPU");
    World start;
    OK(start.m.BeginGPUAccess(0, true));
    OK(start.m.EndCPUAccess(0, Ack::Success, true));
    OK(start.m.BeginCPUAccess(0));
    OK(start.m.PrepareCPUStart(0, true, true));
    Is(start.m.StartCPUAccess(0, Ack::Fail), Error::Unknown);
    Require(start.m.ChunkStatus(0) == Access::Unknown, "failed START opened CPU");
}
void Failures() {
    for (const bool cancel : {false, true}) {
        World w;
        auto a = w.ViewOf({{0, 16}}), b = w.ViewOf({{0, 16}}, 2, 0x9000, 0xa000);
        const auto ra = w.Rep(a), rb = w.Rep(b);
        auto old = w.GPU(a, ra, 99);
        w.Submit(old);
        w.Complete(old);
        const auto old_bytes = w.m.ReplicaBytes(ra), cpu_bytes = w.m.ReplicaBytes(Model::CPU);
        const auto old_tags = w.m.ReplicaVersions(ra);
        auto newer = w.GPU(b, rb, 77, {old.model});
        Reject(w.m, Error::ResourceBusy, [&] { return w.m.Evict(ra); });
        if (cancel) {
            OK(w.m.Cancel(newer.model));
            Require(w.oracle.Cancel(newer.oracle), "oracle successor cancel");
            Require(w.m.ReplicaBytes(ra) == old_bytes && w.m.ReplicaVersions(ra) == old_tags,
                    "cancel lost old producer bytes");
            w.Read(a);
            Require(w.m.ReplicaBytes(Model::CPU)[0] == 99, "cancel restored stale CPU10");
            OK(w.m.Scatter(old.model));
            OK(w.m.Evict(ra));
        } else {
            w.m.Inject(Fault::Submit);
            Is(w.m.Submit(newer.model), Error::Unknown);
            Require(w.m.ReplicaBytes(ra) == old_bytes && old_bytes == Bytes(16, 99) &&
                        w.m.ReplicaVersions(ra) == old_tags &&
                        w.m.ReplicaBytes(Model::CPU) == cpu_bytes,
                    "submission uncertainty destroyed prior GPU99 or published CPU10");
            Require(!w.oracle.Read(a.ranges),
                    "pending successor unexpectedly observable in oracle");
            Reject(w.m, Error::ResourceBusy, [&] { return w.m.Evict(ra); });
            Reject(w.m, Error::ResourceBusy, [&] { return w.m.Evict(rb); });
            Reject(w.m, Error::Invalid, [&] { return w.m.Cancel(newer.model); });
            Reject(w.m, Error::Unknown, [&] { return w.m.Read(a.stamp, Model::CPU); });
            w.m.CheckInvariants();
        }
    }
    {
        World w;
        auto a = w.ViewOf({{0, 16}}), b = w.ViewOf({{0, 16}}, 2, 0x9000, 0xa000);
        const auto ra = w.Rep(a), rb = w.Rep(b);
        auto p = w.GPU(a, ra, 99);
        w.Submit(p);
        w.Complete(p, false);
        auto q = w.GPU(b, rb, 77, {p.model});
        w.Submit(q);
        w.Complete(q, false);
        OK(w.m.Evict(ra)); // completed operands retire independently of host visibility
        Reject(w.m, Error::Invalid, [&] { return w.m.MakeVisible(p.model); });
        w.m.Inject(Fault::Visibility);
        Is(w.m.MakeVisible(q.model), Error::Unknown);
        w.m.CheckInvariants();
        Reject(w.m, Error::ResourceBusy, [&] { return w.m.Evict(rb); });
        Reject(w.m, Error::Unknown, [&] { return w.m.Read(b.stamp, Model::CPU); });
    }
    for (auto f : {Fault::Submit, Fault::Complete, Fault::Visibility}) {
        World w;
        auto a = w.ViewOf({{0, 32}});
        auto rep = w.Rep(a);
        auto p = w.GPU(a, rep, 99);
        if (f != Fault::Submit)
            w.Submit(p);
        if (f == Fault::Visibility)
            w.Complete(p, false);
        const auto stale = w.m.ReplicaBytes(Model::CPU);
        w.m.Inject(f);
        Is(f == Fault::Submit     ? w.m.Submit(p.model)
           : f == Fault::Complete ? w.m.Complete(p.model)
                                  : w.m.MakeVisible(p.model),
           Error::Unknown);
        Require(w.m.Status(p.model) == Phase::Unknown && w.m.ReplicaBytes(Model::CPU) == stale,
                "failure lost state");
        Reject(w.m, Error::Unknown, [&] { return w.m.Read(a.stamp, Model::CPU); });
        Reject(w.m, Error::ResourceBusy, [&] { return w.m.Evict(rep); });
        Reject(w.m, Error::Invalid, [&] { return w.m.Cancel(p.model); });
        w.m.CheckInvariants();
    }
    World w;
    auto a = w.ViewOf({{0, 32}});
    auto rep = w.Rep(a);
    w.m.Inject(Fault::Allocation);
    Reject(w.m, Error::Allocation, [&] { return w.m.ReserveGPU(a.stamp, rep, {}); });
    auto p = w.GPU(a, rep, 99);
    OK(w.m.Cancel(p.model));
    Require(w.oracle.Cancel(p.oracle), "oracle cancel");
    w.Read(a);
    auto q = w.GPU(a, rep, 77);
    w.Submit(q);
    Reject(w.m, Error::Invalid, [&] { return w.m.Cancel(q.model); });
    w.Complete(q);
    w.m.Inject(Fault::Sync);
    Reject(w.m, Error::SyncRequired, [&] { return w.m.Scatter(q.model); });
    w.m.Inject(Fault::Eviction);
    Reject(w.m, Error::NeedsPreservation, [&] { return w.m.Evict(rep); });
    w.m.Inject(Fault::Teardown);
    Reject(w.m, Error::NeedsPreservation, [&] { return w.m.DrainRenderer(); });
    OK(w.m.Scatter(q.model));
    OK(w.m.Evict(rep));
    w.Read(a);
}
void DMACases() {
    for (bool forward : {false, true}) {
        World w({10, 20, 30});
        auto src = w.ViewOf(forward ? std::vector<Oracle::Range>{{0, 1}, {1, 1}}
                                    : std::vector<Oracle::Range>{{1, 1}, {2, 1}});
        auto dst = w.ViewOf(forward ? std::vector<Oracle::Range>{{1, 1}, {2, 1}}
                                    : std::vector<Oracle::Range>{{0, 1}, {1, 1}},
                            2, 0x9000, 0xa000);
        w.DMA(src, dst);
        auto full = w.ViewOf({{0, 3}});
        w.Read(full);
        Require(w.m.ReplicaBytes(Model::CPU) == (forward ? Bytes{10, 10, 20} : Bytes{20, 30, 30}),
                "DMA whole owned source");
    }
    World w({10, 20, 30, 40});
    auto src = w.ViewOf({{0, 1}, {0, 1}, {1, 1}}),
         dst = w.ViewOf({{2, 1}, {3, 1}, {2, 1}}, 2, 0x9000, 0xa000);
    w.m.Inject(Fault::Allocation);
    Reject(w.m, Error::Allocation, [&] { return w.m.DMA(src.stamp, dst.stamp, {}); });
    const auto t = w.DMA(src, dst);
    w.Read(dst);
    Require(w.m.ReplicaBytes(Model::CPU) == Bytes({10, 20, 20, 10}),
            "repeated destination logical order");
    auto a = w.ViewOf({{0, 1}});
    auto p = w.GPU(a, w.Rep(a), 99, {t});
    w.Submit(p);
    w.Complete(p);
    auto to = w.ViewOf({{3, 1}});
    w.DMA(a, to, {p.model});
    w.Read(to);
    View hole = Geometry({{0, 1}});
    hole.segments[0].hole = true;
    auto id = OK(w.m.RegisterView(hole)).id;
    Reject(w.m, Error::Hole, [&] { return w.m.DMA(w.m.GetStamp(id), to.stamp, {p.model}); });
}
void Budget() {
    Limits l;
    l.cells = 2;
    World w(Bytes(128, 10), l);
    auto a = w.ViewOf({{32, 32}});
    auto r = w.Rep(a);
    Reject(w.m, Error::Budget, [&] { return w.m.ReserveGPU(a.stamp, r, {}); });
    w.Read(a);
    Limits reps;
    reps.representations = 2;
    World x(Bytes(32, 10), reps);
    auto b = x.ViewOf({{0, 32}});
    x.Rep(b);
    Reject(x.m, Error::Budget, [&] { return x.m.RegisterRepresentation(b.stamp, Kind::Scratch); });
    Limits ops;
    ops.operations = 1;
    World y(Bytes(32, 10), ops);
    auto c = y.ViewOf({{0, 32}});
    auto t = y.CPUWrite(c, 77);
    const auto lease = OK(y.m.AcquireCPU(c.stamp, true, {t})).ticket;
    Reject(y.m, Error::Budget, [&] { return y.m.CommitCPU(lease, 0, Bytes(32, 99)); });
    OK(y.m.ReleaseCPU(lease));
    y.Read(c);
    World z(Bytes(32, 10));
    auto d = z.ViewOf({{0, 16}}), e = z.ViewOf({{16, 16}});
    const auto t1 = z.CPUWrite(d, 77);
    z.CPUWrite(e, 77, {t1});
    const auto before = z.m.Statistics().cells;
    auto full = z.ViewOf({{0, 32}});
    z.CPUWrite(full, 88, {Ticket{1, 2}});
    Require(z.m.Statistics().cells < before && z.m.Statistics().coalesces > 0,
            "equivalent intervals not coalesced");
    z.Read(full);
    const auto m = z.m.Statistics();
    std::cout << "STRUCTURE cells=" << m.cells << " replicas=" << m.replicas
              << " splits=" << m.splits << " coalesces=" << m.coalesces
              << " metadata=" << m.metadata_bytes << " peak_metadata=" << m.peak_metadata_bytes
              << " pending=" << m.pending_producers << " mean_ns=" << m.elapsed_ns / m.calls
              << " max_ns=" << m.max_call_ns << '\n';
}
int main() {
    const std::pair<const char*, std::function<void()>> tests[]{
        {"T1 partial CPU77/GPU99 + superseding writes", Partial},
        {"T2 cross-DAddr/manager byte authority", CrossAliases},
        {"T3 geometry/repeats/holes", GeometryCases},
        {"T4 ordered scratch99/image77 and preflight", ScratchImage},
        {"T5 precise overfetch/scatter + partial pending readback", Overfetch},
        {"T6 resize/unmap/remap/eviction leases", Lifetime},
        {"T7/T8 recorded/submitted/visible and causal completion", AsyncOrdering},
        {"T9/T13 controlled pre/post CPU access", CPUAdmission},
        {"T9/I9 readonly and mixed dispatch operand leases", ReadLeases},
        {"T10 protected mapping admission", MappingAdmission},
        {"T11/T21 backing replacement/renderer drain", BackingAndRenderer},
        {"T12 producer coverage/layout rejection", Coverage},
        {"T14/T15 chunk leases/noncoherence/retry", Chunks},
        {"T15/T17 failure/quarantine/cancel", Failures},
        {"T18 independent owned DMA semantics", DMACases},
        {"T16 interval/replica/history budgets", Budget}};
    try {
        for (const auto& [name, run] : tests) {
            run();
            std::cout << "PASS " << name << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "FAIL deterministic: " << e.what() << '\n';
        return 1;
    }
}
