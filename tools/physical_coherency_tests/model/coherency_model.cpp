// SPDX-License-Identifier: GPL-2.0-or-later
#include "coherency_model.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace Coherency {
namespace {
Result Fail(Error e) {
    Result r;
    r.error = e;
    return r;
}
bool Bounds(std::size_t p, std::size_t n, std::size_t size) {
    return p <= size && n <= size - p;
}
std::vector<Segment> Clip(const std::vector<Segment>& segments, std::size_t begin,
                          std::size_t size) {
    std::vector<Segment> out;
    for (const auto& s : segments) {
        const auto a = std::max(begin, s.logical), b = std::min(begin + size, s.logical + s.size);
        if (a < b)
            out.push_back({s.epoch, a, s.physical + a - s.logical, b - a, s.hole});
    }
    return out;
}
} // namespace
const char* Name(Error e) {
    constexpr const char* names[]{"None",
                                  "Invalid",
                                  "Hole",
                                  "StaleStamp",
                                  "MutationInProgress",
                                  "Unordered",
                                  "Pending",
                                  "UnsupportedAlias",
                                  "UnknownCoverage",
                                  "RawAccess",
                                  "Layout",
                                  "Allocation",
                                  "Budget",
                                  "NeedsPreservation",
                                  "ResourceBusy",
                                  "SyncRequired",
                                  "Retry",
                                  "Unknown",
                                  "EpochDead"};
    return names[static_cast<unsigned>(e)];
}
Model::Model(Bytes initial, Id epoch, std::size_t chunk_size, Limits budget) : limits{budget} {
    if (!epoch || initial.empty() || !chunk_size)
        throw std::invalid_argument("invalid arena");
    state.epoch = epoch;
    state.size = initial.size();
    state.chunk_size = chunk_size;
    state.cells.push_back({0, initial.size()});
    state.reps.emplace(CPU, Representation{epoch,
                                           Kind::CPU,
                                           true,
                                           initial.size(),
                                           {{epoch, 0, 0, initial.size(), false}},
                                           std::move(initial),
                                           std::vector<Id>(state.size)});
    state.chunks.resize((state.size - 1) / chunk_size + 1);
    const auto m = Measure(state);
    if (m.metadata_bytes > limits.metadata_bytes || m.storage_bytes > limits.storage_bytes ||
        !limits.cells || !limits.representations)
        throw std::invalid_argument("arena exceeds budget");
    metrics.peak_metadata_bytes = m.metadata_bytes;
}
template <class F>
Result Model::Change(F function) {
    // Planning may allocate; the single publication step must not partly throw.
    static_assert(std::is_nothrow_move_assignable_v<State>);
    const auto start = std::chrono::steady_clock::now();
    Result result;
    try {
        if (Consume(Fault::Allocation))
            result = Fail(Error::Allocation);
        else {
            State candidate = state;
            result = function(candidate);
            if (result || result.error == Error::Unknown) {
                Coalesce(candidate);
                const auto m = Measure(candidate);
                // Unknown retains existing leases; never reject quarantine publication.
                if (result && (candidate.cells.size() > limits.cells ||
                               candidate.reps.size() > limits.representations ||
                               candidate.ops.size() > limits.operations ||
                               candidate.views.size() > limits.views ||
                               m.metadata_bytes > limits.metadata_bytes ||
                               m.storage_bytes > limits.storage_bytes)) {
                    result = Fail(Error::Budget);
                } else {
                    state = std::move(candidate);
                    metrics.peak_metadata_bytes =
                        std::max(metrics.peak_metadata_bytes, m.metadata_bytes);
                }
            }
        }
    } catch (const std::bad_alloc&) {
        result = Fail(Error::Allocation);
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    ++metrics.calls;
    metrics.elapsed_ns += ns;
    metrics.max_call_ns = std::max<Id>(metrics.max_call_ns, ns);
    metrics.rejected += !result && result.error != Error::Unknown;
    metrics.quarantined += result.error == Error::Unknown;
    return result;
}
bool Model::Consume(Fault f) {
    if (fault == f) {
        fault.reset();
        return true;
    }
    return false;
}
void Model::Inject(Fault f) {
    fault = f;
}
Error Model::Geometry(const State& s, const View& view, bool holes_allowed) const {
    if (view.epoch != s.epoch)
        return Error::EpochDead;
    if (!view.size || !view.generation)
        return Error::Invalid;
    std::size_t cursor = 0;
    for (const auto& seg : view.segments) {
        if (seg.epoch != s.epoch || !seg.size || seg.logical != cursor ||
            !Bounds(seg.logical, seg.size, view.size))
            return Error::Invalid;
        if (seg.hole && !holes_allowed)
            return Error::Hole;
        if (!seg.hole && !Bounds(seg.physical, seg.size, s.size))
            return Error::Invalid;
        cursor += seg.size;
    }
    return cursor == view.size ? Error::None : Error::Hole;
}
Error Model::Validate(const State& s, Stamp stamp) const {
    if (stamp.epoch != s.epoch)
        return Error::EpochDead;
    const auto it = s.views.find(stamp.view);
    if (it == s.views.end() || !it->second.mapped || it->second.generation != stamp.generation)
        return Error::StaleStamp;
    if (it->second.mutating)
        return Error::MutationInProgress;
    return Geometry(s, it->second, false);
}
Result Model::RegisterView(View view) {
    return Change([&](State& s) {
        if (auto e = Geometry(s, view, true); e != Error::None)
            return Fail(e);
        const Id id = s.next_view++;
        s.views.emplace(id, std::move(view));
        Result r;
        r.id = id;
        return r;
    });
}
Stamp Model::GetStamp(Id id) const {
    const auto& v = state.views.at(id);
    return {id, v.epoch, v.generation};
}
Result Model::RegisterRepresentation(Stamp stamp, Kind kind, bool codec) {
    return Change([&](State& s) {
        if (auto e = Validate(s, stamp); e != Error::None)
            return Fail(e);
        if (kind == Kind::CPU)
            return Fail(Error::Invalid);
        const auto& view = s.views.at(stamp.view);
        const Id id = s.next_rep++;
        s.reps.emplace(id,
                       Representation{s.epoch, kind, codec, view.size, view.segments,
                                      Bytes(view.size, 0xcd), std::vector<Id>(view.size, ~Id{0})});
        Result r;
        r.id = id;
        return r;
    });
}
bool Model::Overlap(const Segment& a, const Segment& b) {
    return !a.hole && !b.hole && a.epoch == b.epoch && a.physical < b.physical + b.size &&
           b.physical < a.physical + a.size;
}
bool Model::Intersects(const std::vector<Segment>& a, const std::vector<Segment>& b) {
    for (const auto& x : a)
        for (const auto& y : b)
            if (Overlap(x, y))
                return true;
    return false;
}
Result Model::Preflight(const std::vector<Binding>& bindings) const {
    for (const auto& b : bindings) {
        if (!b.coverage_known || !b.footprint_known)
            return Fail(Error::UnknownCoverage);
        if (!b.layout_supported)
            return Fail(Error::Layout);
        if (auto e = Validate(state, b.stamp); e != Error::None)
            return Fail(e);
        const auto r = state.reps.find(b.representation);
        if (r == state.reps.end() || r->second.epoch != state.epoch)
            return Fail(Error::Invalid);
        if (r->second.kind == Kind::CPU)
            return Fail(Error::Invalid);
        if (!r->second.codec)
            return Fail(Error::Layout);
        const auto& segments = state.views.at(b.stamp.view).segments;
        if (segments != r->second.segments)
            return Fail(Error::Invalid);
        if (b.writable)
            for (std::size_t i = 0; i < segments.size(); ++i)
                for (std::size_t j = 0; j < i; ++j)
                    if (Overlap(segments[i], segments[j]))
                        return Fail(Error::UnsupportedAlias);
    }
    for (std::size_t i = 0; i < bindings.size(); ++i)
        for (std::size_t j = 0; j < i; ++j) {
            const auto& a = bindings[i];
            const auto& b = bindings[j];
            if ((a.writable || b.writable) && a.representation != b.representation &&
                Intersects(state.views.at(a.stamp.view).segments,
                           state.views.at(b.stamp.view).segments))
                return Fail(Error::UnsupportedAlias);
        }
    return {};
}
std::optional<std::size_t> Model::Locate(const Representation& r, std::size_t physical) {
    for (const auto& seg : r.segments)
        if (!seg.hole && physical >= seg.physical && physical - seg.physical < seg.size)
            return seg.logical + physical - seg.physical;
    return {};
}
std::vector<Model::Slice> Model::Sources(const State& s, const std::vector<Segment>& segments) {
    std::vector<Slice> out;
    for (const auto& seg : segments) {
        if (seg.hole)
            continue;
        for (const auto& c : s.cells) {
            const auto a = std::max(seg.physical, c.begin),
                       b = std::min(seg.physical + seg.size, c.end);
            if (a < b) {
                const auto offset = Locate(s.reps.at(c.holder), a);
                if (!offset)
                    throw std::logic_error("authority representation missing physical bytes");
                // Cut at the representation's physical span boundary as well as cell boundaries.
                std::size_t p = a;
                while (p < b) {
                    const auto logical = Locate(s.reps.at(c.holder), p);
                    if (!logical)
                        throw std::logic_error("authority has a hole");
                    std::size_t n = b - p;
                    for (const auto& part : s.reps.at(c.holder).segments)
                        if (!part.hole && p >= part.physical && p - part.physical < part.size) {
                            n = std::min(n, part.size - (p - part.physical));
                            break;
                        }
                    out.push_back({p, n, seg.logical + p - seg.physical, *logical, c.version,
                                   c.producer, c.holder});
                    p += n;
                }
            }
        }
    }
    return out;
}
bool Model::Terminal(Phase p) {
    return p == Phase::Completed || p == Phase::Visible || p == Phase::Cancelled;
}
Error Model::Ready(const State& s, const std::vector<Slice>& slices, bool cpu) {
    for (const auto& x : slices) {
        for (std::size_t i = x.physical / s.chunk_size;
             i <= (x.physical + x.size - 1) / s.chunk_size; ++i)
            if (s.chunks[i].access == Access::Unknown)
                return Error::Unknown;
        for (const auto& c : s.cells)
            if (c.unknown && x.physical < c.end && c.begin < x.physical + x.size)
                return Error::Unknown;
        if (x.producer) {
            const auto& op = s.ops.at(x.producer);
            if (op.phase == Phase::Unknown)
                return Error::Unknown;
            if (!Terminal(op.phase) || op.phase == Phase::Cancelled)
                return Error::Pending;
            if (cpu && op.phase != Phase::Visible)
                return Error::SyncRequired;
        }
        const auto& r = s.reps.at(x.holder);
        for (std::size_t i = 0; i < x.size; ++i)
            if (r.versions[x.source_logical + i] != x.version)
                return Error::Pending;
    }
    return Error::None;
}
bool Model::Ordered(const State& s, Id predecessor, const std::vector<Ticket>& after) {
    if (!predecessor)
        return true;
    std::vector<Id> todo;
    std::set<Id> seen;
    for (const auto& t : after) {
        if (t.epoch != s.epoch || !s.ops.contains(t.id) || s.ops.at(t.id).phase == Phase::Cancelled)
            return false;
        todo.push_back(t.id);
    }
    while (!todo.empty()) {
        const Id id = todo.back();
        todo.pop_back();
        if (id == predecessor)
            return true;
        if (!seen.insert(id).second)
            continue;
        for (const auto& dep : s.ops.at(id).after)
            todo.push_back(dep.id);
    }
    return false;
}
Error Model::Admit(const State& s, const std::vector<Segment>& segments,
                   const std::vector<Ticket>& after, Id own_lease, bool write) const {
    for (const auto& dep : after)
        if (dep.epoch != s.epoch || !s.ops.contains(dep.id) ||
            s.ops.at(dep.id).phase == Phase::Cancelled || s.ops.at(dep.id).phase == Phase::Unknown)
            return Error::Unordered;
    for (const auto& [id, lease] : s.leases)
        if (id != own_lease && (write || lease.write) && Intersects(segments, lease.segments))
            return Error::ResourceBusy;
    for (const auto& [id, op] : s.ops)
        if (write && op.phase != Phase::Cancelled && Intersects(segments, op.read_segments) &&
            !Ordered(s, id, after))
            return Error::Unordered;
    for (const auto& x : Sources(s, segments)) {
        for (std::size_t i = x.physical / s.chunk_size;
             i <= (x.physical + x.size - 1) / s.chunk_size; ++i)
            if (s.chunks[i].access == Access::Unknown)
                return Error::Unknown;
        if (!Ordered(s, x.producer, after))
            return Error::Unordered;
        for (const auto& c : s.cells)
            if (c.unknown && x.physical < c.end && c.begin < x.physical + x.size)
                return Error::Unknown;
    }
    return Error::None;
}
void Model::Split(State& s, std::size_t p) {
    for (auto it = s.cells.begin(); it != s.cells.end(); ++it)
        if (it->begin < p && p < it->end) {
            auto right = *it;
            right.begin = p;
            it->end = p;
            s.cells.insert(it + 1, right);
            ++metrics.splits;
            return;
        }
}
bool Model::SameReplicaBoundary(const State& s, std::size_t boundary) {
    for (const auto& [id, r] : s.reps) {
        (void)id;
        // Replica presence, provenance continuity and validity must agree.
        const auto left = Locate(r, boundary - 1), right = Locate(r, boundary);
        if (left.has_value() != right.has_value())
            return false;
        if (left && (*right != *left + 1 || r.versions[*left] != r.versions[*right]))
            return false;
    }
    return true;
}
void Model::Coalesce(State& s) {
    for (std::size_t i = 1; i < s.cells.size();) {
        auto a = s.cells[i - 1], b = s.cells[i];
        a.begin = b.begin;
        a.end = b.end;
        if (a == b && SameReplicaBoundary(s, b.begin)) {
            s.cells[i - 1].end = b.end;
            s.cells.erase(s.cells.begin() + i);
            ++metrics.coalesces;
        } else
            ++i;
    }
}
void Model::Assign(State& s, const std::vector<Segment>& segments, Id version, Id producer,
                   Id holder) {
    for (const auto& seg : segments) {
        if (seg.hole)
            continue;
        Split(s, seg.physical);
        Split(s, seg.physical + seg.size);
        for (auto& c : s.cells)
            if (c.begin >= seg.physical && c.end <= seg.physical + seg.size) {
                c.prior_version = c.version;
                c.version = version;
                c.producer = producer;
                c.holder = holder;
            }
    }
}
Error Model::CopySegments(State& s, Id target, const std::vector<Segment>& segments, bool cpu) {
    if (target == CPU) {
        for (const auto& [id, lease] : s.leases) {
            (void)id;
            if (lease.write && Intersects(segments, lease.segments))
                return Error::ResourceBusy;
        }
        for (const auto& seg : segments)
            for (std::size_t i = seg.physical / s.chunk_size;
                 i <= (seg.physical + seg.size - 1) / s.chunk_size; ++i)
                if (s.chunks[i].access != Access::CPUReady)
                    return Error::ResourceBusy;
    }
    auto sources = Sources(s, segments);
    if (auto e = Ready(s, sources, cpu); e != Error::None)
        return e;
    auto& r = s.reps.at(target);
    if (!r.codec)
        return Error::Layout;
    for (const auto& x : sources) {
        const auto& from = s.reps.at(x.holder);
        // Source comes from authority, never target's stale contents.
        for (std::size_t i = 0; i < x.size; ++i) {
            const auto p = x.physical + i;
            for (const auto& seg : r.segments)
                if (!seg.hole && p >= seg.physical && p - seg.physical < seg.size) {
                    const auto j = seg.logical + p - seg.physical;
                    r.bytes[j] = from.bytes[x.source_logical + i];
                    r.versions[j] = x.version;
                }
        }
    }
    return Error::None;
}
Error Model::CopyLatest(State& s, Id rep, bool cpu) {
    return CopySegments(s, rep, s.reps.at(rep).segments, cpu);
}
Result Model::AcquireCPU(Stamp stamp, bool write, const std::vector<Ticket>& after,
                         bool controlled) {
    return Change([&](State& s) {
        if (!controlled)
            return Fail(Error::RawAccess);
        if (auto e = Validate(s, stamp); e != Error::None)
            return Fail(e);
        const auto& v = s.views.at(stamp.view);
        if (auto e = Admit(s, v.segments, after, 0, write); e != Error::None)
            return Fail(e);
        for (const auto& dep : after)
            if (!Terminal(s.ops.at(dep.id).phase))
                return Fail(Error::Pending);
        for (const auto& seg : v.segments)
            for (std::size_t i = seg.physical / s.chunk_size;
                 i <= (seg.physical + seg.size - 1) / s.chunk_size; ++i)
                if (s.chunks[i].access != Access::CPUReady)
                    return Fail(Error::ResourceBusy);
        const auto sources = Sources(s, v.segments);
        if (auto e = Ready(s, sources, !write); e != Error::None)
            return Fail(e);
        // Exact-write leases don't download bytes that will be superseded.
        if (!write)
            if (auto e = CopySegments(s, CPU, v.segments, true); e != Error::None)
                return Fail(e);
        const Id id = s.next_lease++;
        s.leases.emplace(id, Lease{stamp.view, write, v.segments, v.size, after});
        Result r;
        r.ticket = {s.epoch, id};
        if (!write)
            for (const auto& seg : v.segments)
                r.bytes.insert(r.bytes.end(), s.reps.at(CPU).bytes.begin() + seg.physical,
                               s.reps.at(CPU).bytes.begin() + seg.physical + seg.size);
        return r;
    });
}
Result Model::CommitCPU(Ticket lease, std::size_t offset, const Bytes& data) {
    return Change([&](State& s) {
        if (lease.epoch != s.epoch)
            return Fail(Error::EpochDead);
        auto it = s.leases.find(lease.id);
        if (it == s.leases.end() || !it->second.write || data.empty() ||
            !Bounds(offset, data.size(), it->second.size))
            return Fail(Error::Invalid);
        const auto segments = Clip(it->second.segments, offset, data.size());
        if (auto e = Admit(s, segments, it->second.after, lease.id); e != Error::None)
            return Fail(e);
        const Id version = ++s.clock;
        Operation op;
        op.id = ++s.sequence;
        op.version = version;
        op.phase = Phase::Visible;
        op.cpu = true;
        op.executed = true;
        op.representation = CPU;
        op.segments = segments;
        op.after = it->second.after;
        s.ops.emplace(op.id, op);
        auto& cpu = s.reps.at(CPU);
        for (const auto& seg : segments)
            for (std::size_t i = 0; i < seg.size; ++i) {
                cpu.bytes[seg.physical + i] = data[seg.logical + i - offset];
                cpu.versions[seg.physical + i] = version;
            }
        Assign(s, segments, version, op.id, CPU);
        for (auto& c : s.cells)
            if (c.producer == op.id)
                c.prior_version = 0;
        s.leases.erase(it);
        Result r;
        r.ticket = {s.epoch, op.id};
        return r;
    });
}
Result Model::ReleaseCPU(Ticket lease) {
    return Change([&](State& s) {
        if (lease.epoch != s.epoch)
            return Fail(Error::EpochDead);
        if (!s.leases.erase(lease.id))
            return Fail(Error::Invalid);
        return Result{};
    });
}
bool Model::RepInUse(const State& s, Id rep) {
    for (const auto& [id, op] : s.ops) {
        (void)id;
        if (!Terminal(op.phase)) {
            if (op.representation == rep)
                return true;
            if (op.executed)
                continue; // Retire completed operands, retain authority/visibility lease.
            if (std::find(op.readers.begin(), op.readers.end(), rep) != op.readers.end())
                return true;
            for (const auto& p : op.prior)
                if (p.holder == rep)
                    return true;
        }
    }
    return false;
}
bool Model::ViewInUse(const State& s, Id view) {
    for (const auto& [id, lease] : s.leases) {
        (void)id;
        if (lease.view == view)
            return true;
    }
    return false; // GPU operations retain physical/resource values, never retranslate.
}
Result Model::ReserveGPU(Stamp stamp, Id rep, const std::vector<Ticket>& after, unsigned queue) {
    return ReserveDispatch({{stamp, rep, true}}, after, queue);
}
Result Model::ReserveDispatch(const std::vector<Binding>& bindings,
                              const std::vector<Ticket>& after, unsigned queue) {
    return Change([&](State& s) {
        if (bindings.empty())
            return Fail(Error::Invalid);
        if (auto r = Preflight(bindings); !r)
            return r;
        std::map<Id, Operation> plans;
        std::vector<Id> readers;
        std::vector<Segment> read_segments;
        for (const auto& binding : bindings) {
            const auto& v = s.views.at(binding.stamp.view);
            if (auto e = Admit(s, v.segments, after, 0, binding.writable); e != Error::None)
                return Fail(e);
            if (RepInUse(s, binding.representation))
                return Fail(Error::ResourceBusy);
            if (!binding.writable) {
                if (auto e = CopyLatest(s, binding.representation, false); e != Error::None)
                    return Fail(e);
                readers.push_back(binding.representation);
                read_segments.insert(read_segments.end(), v.segments.begin(), v.segments.end());
                continue;
            }
            Operation op;
            op.representation = binding.representation;
            op.view = binding.stamp.view;
            op.queue = queue;
            op.after = after;
            op.segments = v.segments;
            op.size = v.size;
            op.prior = Sources(s, v.segments);
            plans.emplace(binding.representation, std::move(op));
        }
        Result result;
        if (plans.empty()) {
            Operation read;
            read.id = ++s.sequence;
            read.readonly = true;
            read.representation = readers.front();
            read.readers = readers;
            read.after = after;
            read.segments = read_segments;
            read.read_segments = read_segments;
            read.queue = queue;
            s.ops.emplace(read.id, read);
            result.tickets.push_back({s.epoch, read.id});
        }
        for (auto& [rep, op] : plans) {
            op.id = ++s.sequence;
            op.version = ++s.clock;
            op.readers = readers;
            op.read_segments = read_segments;
            s.ops.emplace(op.id, op);
            Assign(s, op.segments, op.version, op.id, rep);
            result.tickets.push_back({s.epoch, op.id});
        }
        if (!result.tickets.empty())
            result.ticket = result.tickets.front();
        return result;
    });
}
Result Model::Record(Ticket t, std::size_t offset, const Bytes& data) {
    return Change([&](State& s) {
        if (t.epoch != s.epoch)
            return Fail(Error::EpochDead);
        auto it = s.ops.find(t.id);
        if (it == s.ops.end() || it->second.cpu || it->second.phase != Phase::Accepted ||
            !Bounds(offset, data.size(), it->second.size) || (!it->second.readonly && data.empty()))
            return Fail(Error::Invalid);
        it->second.write_offset = offset;
        it->second.write_data = data;
        it->second.phase = Phase::Recorded;
        return Result{};
    });
}
void Model::Quarantine(State& s, Operation& op) {
    op.phase = Phase::Unknown;
    for (auto& c : s.cells)
        for (const auto& seg : op.segments)
            if (!seg.hole && c.begin < seg.physical + seg.size && seg.physical < c.end)
                c.unknown = true;
    for (auto chunk : op.chunks)
        s.chunks[chunk].access = Access::Unknown;
}
Result Model::Submit(Ticket t) {
    return Change([&](State& s) {
        if (t.epoch != s.epoch)
            return Fail(Error::EpochDead);
        auto it = s.ops.find(t.id);
        if (it == s.ops.end() || it->second.phase != Phase::Recorded)
            return Fail(Error::Invalid);
        for (const auto& d : it->second.after) {
            const auto p = s.ops.at(d.id).phase;
            if (p == Phase::Unknown)
                return Fail(Error::Unknown);
            if (p == Phase::Accepted || p == Phase::Recorded)
                return Fail(Error::Pending);
        }
        if (Consume(Fault::Submit)) {
            Quarantine(s, it->second);
            return Fail(Error::Unknown);
        }
        it->second.phase = Phase::Submitted;
        for (auto chunk : it->second.chunks)
            s.chunks[chunk].access = Access::GPUActive;
        return Result{};
    });
}
Result Model::Complete(Ticket t) {
    return Change([&](State& s) {
        if (t.epoch != s.epoch)
            return Fail(Error::EpochDead);
        auto it = s.ops.find(t.id);
        if (it == s.ops.end())
            return Fail(Error::Invalid);
        auto& op = it->second;
        if (op.phase == Phase::Completed || op.phase == Phase::Visible)
            return Result{};
        if (op.phase != Phase::Submitted)
            return Fail(op.phase == Phase::Unknown ? Error::Unknown : Error::Invalid);
        for (const auto& d : op.after) {
            const auto p = s.ops.at(d.id).phase;
            if (p == Phase::Unknown)
                return Fail(Error::Unknown);
            if (p != Phase::Completed && p != Phase::Visible)
                return Fail(Error::Pending);
        }
        // Prior slices refer to retained operands, not current authority (which may be newer).
        for (const auto& x : op.prior) {
            if (x.producer && !Terminal(s.ops.at(x.producer).phase))
                return Fail(Error::Pending);
            const auto& from = s.reps.at(x.holder);
            for (std::size_t i = 0; i < x.size; ++i)
                if (from.versions[x.source_logical + i] != x.version)
                    return Fail(Error::Pending);
        }
        if (Consume(Fault::Complete)) {
            Quarantine(s, op);
            return Fail(Error::Unknown);
        }
        if (op.readonly) {
            op.executed = true;
            op.phase = Phase::Completed;
            return Result{};
        }
        auto& target = s.reps.at(op.representation);
        for (const auto& x : op.prior) {
            const auto& from = s.reps.at(x.holder);
            for (std::size_t i = 0; i < x.size; ++i)
                target.bytes[x.logical + i] = from.bytes[x.source_logical + i];
        }
        std::copy(op.write_data.begin(), op.write_data.end(),
                  target.bytes.begin() + op.write_offset);
        std::fill(target.versions.begin(), target.versions.end(), op.version);
        op.phase = Phase::Completed;
        op.executed = true;
        for (auto& c : s.cells)
            if (c.producer == t.id)
                c.prior_version = 0;
        return Result{};
    });
}
Result Model::Cancel(Ticket t) {
    return Change([&](State& s) {
        if (t.epoch != s.epoch)
            return Fail(Error::EpochDead);
        auto it = s.ops.find(t.id);
        if (it == s.ops.end() ||
            (it->second.phase != Phase::Accepted && it->second.phase != Phase::Recorded))
            return Fail(Error::Invalid);
        // Descendants, even unsubmitted ones, must be cancelled first.
        for (const auto& [id, other] : s.ops)
            if (id != t.id && other.phase != Phase::Cancelled && Ordered(s, t.id, other.after))
                return Fail(Error::ResourceBusy);
        for (const auto& x : it->second.prior) {
            Split(s, x.physical);
            Split(s, x.physical + x.size);
            for (auto& c : s.cells)
                if (c.begin >= x.physical && c.end <= x.physical + x.size && c.producer == t.id) {
                    c.version = x.version;
                    c.producer = x.producer;
                    c.holder = x.holder;
                    c.prior_version = 0;
                }
        }
        it->second.phase = Phase::Cancelled;
        return Result{};
    });
}
Result Model::MakeVisible(Ticket t) {
    return Change([&](State& s) {
        if (t.epoch != s.epoch)
            return Fail(Error::EpochDead);
        auto it = s.ops.find(t.id);
        if (it == s.ops.end())
            return Fail(Error::Invalid);
        if (it->second.phase == Phase::Visible)
            return Result{};
        if (it->second.phase != Phase::Completed)
            return Fail(Error::Pending);
        if (!s.reps.contains(it->second.representation))
            return Fail(Error::Invalid);
        if (Consume(Fault::Visibility)) {
            Quarantine(s, it->second);
            return Fail(Error::Unknown);
        }
        it->second.phase = Phase::Visible;
        return Result{};
    });
}
Result Model::Read(Stamp stamp, Id rep) {
    return Change([&](State& s) {
        if (auto e = Validate(s, stamp); e != Error::None)
            return Fail(e);
        if (!s.reps.contains(rep))
            return Fail(Error::Invalid);
        const auto& segments = s.views.at(stamp.view).segments;
        for (const auto& [id, lease] : s.leases) {
            (void)id;
            if (lease.write && Intersects(segments, lease.segments))
                return Fail(Error::ResourceBusy);
        }
        if (rep != CPU && RepInUse(s, rep))
            return Fail(Error::ResourceBusy);
        if (rep != CPU && s.reps.at(rep).segments != segments)
            return Fail(Error::Invalid);
        if (rep == CPU)
            for (const auto& seg : segments)
                for (std::size_t i = seg.physical / s.chunk_size;
                     i <= (seg.physical + seg.size - 1) / s.chunk_size; ++i)
                    if (s.chunks[i].access != Access::CPUReady)
                        return Fail(Error::ResourceBusy);
        if (Consume(Fault::Sync))
            return Fail(Error::SyncRequired); // proven pre-copy failure, no effects
        if (auto e = CopySegments(s, rep, segments, rep == CPU); e != Error::None)
            return Fail(e);
        Result r;
        for (const auto& seg : segments) {
            const auto offset = rep == CPU ? seg.physical : seg.logical;
            r.bytes.insert(r.bytes.end(), s.reps.at(rep).bytes.begin() + offset,
                           s.reps.at(rep).bytes.begin() + offset + seg.size);
        }
        return r;
    });
}
Result Model::Synchronize(Id rep) {
    return Change([&](State& s) {
        if (!s.reps.contains(rep))
            return Fail(Error::Invalid);
        if (RepInUse(s, rep))
            return Fail(Error::ResourceBusy);
        if (Consume(Fault::Sync))
            return Fail(Error::SyncRequired);
        return Fail(CopyLatest(s, rep, rep == CPU));
    });
}
Result Model::Gather(Id scratch) {
    if (!state.reps.contains(scratch) || state.reps.at(scratch).kind != Kind::Scratch)
        return Fail(Error::Invalid);
    return Synchronize(scratch);
}
Error Model::Preserve(State& s, Id rep) {
    std::vector<Segment> segments;
    for (const auto& c : s.cells)
        if (c.holder == rep)
            segments.push_back({s.epoch, c.begin, c.begin, c.end - c.begin, false});
    if (auto e = CopySegments(s, CPU, segments, true); e != Error::None)
        return e;
    for (auto& c : s.cells)
        if (c.holder == rep)
            c.holder = CPU;
    return Error::None;
}
Result Model::Scatter(Ticket t) {
    return Change([&](State& s) {
        if (t.epoch != s.epoch)
            return Fail(Error::EpochDead);
        auto it = s.ops.find(t.id);
        if (it == s.ops.end() || it->second.cpu)
            return Fail(Error::Invalid);
        if (it->second.phase != Phase::Visible)
            return Fail(Error::Pending);
        if (Consume(Fault::Sync))
            return Fail(Error::SyncRequired);
        // Only slices matching this producer/version; newer owners are not copied or cleared.
        std::vector<Segment> live;
        for (const auto& c : s.cells)
            if (c.producer == t.id && c.version == it->second.version)
                live.push_back({s.epoch, c.begin, c.begin, c.end - c.begin, false});
        if (auto e = CopySegments(s, CPU, live, true); e != Error::None)
            return Fail(e);
        for (auto& c : s.cells)
            if (c.producer == t.id && c.version == it->second.version)
                c.holder = CPU;
        return Result{};
    });
}
Result Model::BeginMutation(Stamp stamp) {
    return Change([&](State& s) {
        if (auto e = Validate(s, stamp); e != Error::None)
            return Fail(e);
        if (ViewInUse(s, stamp.view))
            return Fail(Error::ResourceBusy);
        s.views.at(stamp.view).mutating = true;
        return Result{};
    });
}
Result Model::EndMutation(Id view) {
    return Change([&](State& s) {
        auto it = s.views.find(view);
        if (it == s.views.end() || !it->second.mutating)
            return Fail(Error::Invalid);
        it->second.mutating = false;
        return Result{};
    });
}
Result Model::Unmap(Stamp stamp) {
    return Change([&](State& s) {
        if (auto e = Validate(s, stamp); e != Error::None)
            return Fail(e);
        if (ViewInUse(s, stamp.view))
            return Fail(Error::ResourceBusy);
        auto& view = s.views.at(stamp.view);
        view.mapped = false;
        view.generation += 2;
        return Result{}; // retained operations use captured physical segments
    });
}
Result Model::Remap(Stamp stamp, View replacement) {
    return Change([&](State& s) {
        if (auto e = Validate(s, stamp); e != Error::None)
            return Fail(e);
        if (ViewInUse(s, stamp.view))
            return Fail(Error::ResourceBusy);
        if (auto e = Geometry(s, replacement, true); e != Error::None)
            return Fail(e);
        replacement.generation = s.views.at(stamp.view).generation + 2;
        s.views.at(stamp.view) = std::move(replacement);
        return Result{};
    });
}
Result Model::Evict(Id rep) {
    return Change([&](State& s) {
        if (rep == CPU || !s.reps.contains(rep))
            return Fail(Error::Invalid);
        if (Consume(Fault::Eviction))
            return Fail(Error::NeedsPreservation);
        if (RepInUse(s, rep))
            return Fail(Error::ResourceBusy);
        for (const auto& c : s.cells)
            if (c.holder == rep)
                return Fail(Error::NeedsPreservation);
        s.reps.erase(rep);
        return Result{};
    });
}
Result Model::DrainRenderer() {
    return Change([&](State& s) {
        if (Consume(Fault::Teardown))
            return Fail(Error::NeedsPreservation);
        for (const auto& [id, op] : s.ops) {
            (void)id;
            if (!Terminal(op.phase))
                return Fail(op.phase == Phase::Unknown ? Error::Unknown : Error::Pending);
        }
        if (!s.leases.empty())
            return Fail(Error::ResourceBusy);
        for (auto& ch : s.chunks)
            if (ch.access != Access::CPUReady)
                return Fail(Error::ResourceBusy);
        for (const auto& [id, rep] : s.reps) {
            (void)rep;
            if (id != CPU)
                if (auto e = Preserve(s, id); e != Error::None)
                    return Fail(e);
        }
        for (auto it = s.reps.begin(); it != s.reps.end();)
            if (it->first != CPU)
                it = s.reps.erase(it);
            else
                ++it;
        return Result{};
    });
}
Result Model::ReplaceBacking(Bytes initial, Id epoch) {
    return Change([&](State& s) {
        if (epoch <= s.epoch || initial.empty())
            return Fail(Error::Invalid);
        if (!s.leases.empty() || s.reps.size() != 1)
            return Fail(Error::ResourceBusy);
        for (const auto& [id, op] : s.ops) {
            (void)id;
            if (!Terminal(op.phase))
                return Fail(Error::Pending);
        }
        for (const auto& ch : s.chunks)
            if (ch.access != Access::CPUReady)
                return Fail(Error::ResourceBusy);
        const auto chunk_size = s.chunk_size;
        State next;
        next.epoch = epoch;
        next.size = initial.size();
        next.chunk_size = chunk_size;
        next.cells.push_back({0, initial.size()});
        next.reps.emplace(CPU, Representation{epoch,
                                              Kind::CPU,
                                              true,
                                              initial.size(),
                                              {{epoch, 0, 0, initial.size(), false}},
                                              std::move(initial),
                                              std::vector<Id>(next.size)});
        next.chunks.resize((next.size - 1) / chunk_size + 1);
        s = std::move(next);
        return Result{};
    });
}
Result Model::DMA(Stamp source, Stamp destination, const std::vector<Ticket>& after) {
    return Change([&](State& s) {
        if (auto e = Validate(s, source); e != Error::None)
            return Fail(e);
        if (auto e = Validate(s, destination); e != Error::None)
            return Fail(e);
        const auto& src = s.views.at(source.view);
        const auto& dst = s.views.at(destination.view);
        if (src.size != dst.size)
            return Fail(Error::Invalid);
        if (auto e = Admit(s, dst.segments, after); e != Error::None)
            return Fail(e);
        for (const auto& dep : after)
            if (!Terminal(s.ops.at(dep.id).phase))
                return Fail(Error::Pending);
        for (const auto& [id, lease] : s.leases) {
            (void)id;
            if (lease.write && Intersects(src.segments, lease.segments))
                return Fail(Error::ResourceBusy);
        }
        auto sources = Sources(s, src.segments);
        for (const auto& x : sources)
            if (!Ordered(s, x.producer, after))
                return Fail(Error::Unordered);
        if (auto e = Ready(s, sources, true); e != Error::None)
            return Fail(e);
        if (auto e = Ready(s, Sources(s, dst.segments), false); e != Error::None)
            return Fail(e);
        for (const auto& seg : src.segments)
            for (std::size_t i = seg.physical / s.chunk_size;
                 i <= (seg.physical + seg.size - 1) / s.chunk_size; ++i)
                if (s.chunks[i].access != Access::CPUReady)
                    return Fail(Error::ResourceBusy);
        for (const auto& seg : dst.segments)
            for (std::size_t i = seg.physical / s.chunk_size;
                 i <= (seg.physical + seg.size - 1) / s.chunk_size; ++i)
                if (s.chunks[i].access != Access::CPUReady)
                    return Fail(Error::ResourceBusy);
        Bytes owned(src.size);
        for (const auto& x : sources)
            std::copy_n(s.reps.at(x.holder).bytes.begin() + x.source_logical, x.size,
                        owned.begin() + x.logical);
        const Id version = ++s.clock;
        Operation op;
        op.id = ++s.sequence;
        op.version = version;
        op.phase = Phase::Visible;
        op.cpu = true;
        op.executed = true;
        op.representation = CPU;
        op.segments = dst.segments;
        op.after = after;
        s.ops.emplace(op.id, op);
        auto& cpu = s.reps.at(CPU);
        // Logical destination order defines last occurrence for repeated pages.
        for (const auto& seg : dst.segments)
            for (std::size_t i = 0; i < seg.size; ++i) {
                cpu.bytes[seg.physical + i] = owned[seg.logical + i];
                cpu.versions[seg.physical + i] = version;
            }
        Assign(s, dst.segments, version, op.id, CPU);
        for (auto& c : s.cells)
            if (c.producer == op.id)
                c.prior_version = 0;
        Result r;
        r.ticket = {s.epoch, op.id};
        return r;
    });
}
Result Model::BeginGPUAccess(std::size_t chunk, bool coverage) {
    return Change([&](State& s) {
        if (!coverage)
            return Fail(Error::UnknownCoverage);
        if (chunk >= s.chunks.size())
            return Fail(Error::Invalid);
        auto& ch = s.chunks[chunk];
        if (ch.access != Access::CPUReady)
            return Fail(Error::ResourceBusy);
        const Segment range{s.epoch, 0, chunk * s.chunk_size,
                            std::min(s.chunk_size, s.size - chunk * s.chunk_size), false};
        for (const auto& [id, lease] : s.leases) {
            (void)id;
            if (Intersects({range}, lease.segments))
                return Fail(Error::ResourceBusy);
        }
        ch.access = Access::ClosingCPU;
        return Result{};
    });
}
Result Model::EndCPUAccess(std::size_t chunk, Ack ack, bool flushed) {
    return Change([&](State& s) {
        if (chunk >= s.chunks.size() || s.chunks[chunk].access != Access::ClosingCPU)
            return Fail(Error::Invalid);
        auto& ch = s.chunks[chunk];
        if (ack == Ack::Retry)
            return Fail(Error::Retry);
        if (ch.noncoherent && !flushed)
            return Fail(Error::SyncRequired);
        if (ack == Ack::Fail) {
            ch.access = Access::Unknown;
            return Fail(Error::Unknown);
        }
        ch.access = Access::GPUPending;
        return Result{};
    });
}
Result Model::UseChunk(Ticket t, std::size_t chunk) {
    return Change([&](State& s) {
        if (t.epoch != s.epoch)
            return Fail(Error::EpochDead);
        auto it = s.ops.find(t.id);
        if (chunk >= s.chunks.size() || it == s.ops.end() || it->second.cpu ||
            (it->second.phase != Phase::Accepted && it->second.phase != Phase::Recorded))
            return Fail(Error::Invalid);
        auto& ch = s.chunks[chunk];
        if (ch.access != Access::GPUPending && ch.access != Access::GPUActive)
            return Fail(Error::ResourceBusy);
        const Segment footprint{s.epoch, 0, chunk * s.chunk_size,
                                std::min(s.chunk_size, s.size - chunk * s.chunk_size), false};
        if (!Intersects({footprint}, it->second.segments))
            return Fail(Error::Invalid);
        if (std::find(ch.uses.begin(), ch.uses.end(), t.id) == ch.uses.end())
            ch.uses.push_back(t.id);
        if (std::find(it->second.chunks.begin(), it->second.chunks.end(), chunk) ==
            it->second.chunks.end())
            it->second.chunks.push_back(chunk);
        return Result{};
    });
}
Result Model::BeginCPUAccess(std::size_t chunk) {
    return Change([&](State& s) {
        if (chunk >= s.chunks.size())
            return Fail(Error::Invalid);
        auto& ch = s.chunks[chunk];
        if (ch.access != Access::GPUPending && ch.access != Access::GPUActive)
            return Fail(Error::ResourceBusy);
        for (Id id : ch.uses)
            if (!Terminal(s.ops.at(id).phase))
                return Fail(s.ops.at(id).phase == Phase::Unknown ? Error::Unknown : Error::Pending);
        ch.access = Access::VisibilityPending;
        return Result{};
    });
}
Result Model::PrepareCPUStart(std::size_t chunk, bool barrier, bool invalidated) {
    return Change([&](State& s) {
        if (chunk >= s.chunks.size() || s.chunks[chunk].access != Access::VisibilityPending)
            return Fail(Error::Invalid);
        auto& ch = s.chunks[chunk];
        if (!barrier || (ch.noncoherent && !invalidated))
            return Fail(Error::SyncRequired);
        ch.access = Access::StartingCPU;
        return Result{};
    });
}
Result Model::StartCPUAccess(std::size_t chunk, Ack ack) {
    return Change([&](State& s) {
        if (chunk >= s.chunks.size() || s.chunks[chunk].access != Access::StartingCPU)
            return Fail(Error::Invalid);
        auto& ch = s.chunks[chunk];
        if (ack == Ack::Retry)
            return Fail(Error::Retry);
        if (ack == Ack::Fail) {
            ch.access = Access::Unknown;
            return Fail(Error::Unknown);
        }
        ch.access = Access::CPUReady;
        ch.uses.clear();
        return Result{};
    });
}
void Model::SetNonCoherent(std::size_t chunk, bool value) {
    if (state.chunks.at(chunk).access != Access::CPUReady)
        throw std::logic_error("memory type changed during access");
    state.chunks[chunk].noncoherent = value;
}
Phase Model::Status(Ticket t) const {
    if (t.epoch != state.epoch || !state.ops.contains(t.id))
        throw std::invalid_argument("stale operation");
    return state.ops.at(t.id).phase;
}
Access Model::ChunkStatus(std::size_t chunk) const {
    return state.chunks.at(chunk).access;
}
Id Model::VersionClock() const {
    return state.clock;
}
Bytes Model::ReplicaBytes(Id rep) const {
    return state.reps.at(rep).bytes;
}
std::vector<Id> Model::ReplicaVersions(Id rep) const {
    return state.reps.at(rep).versions;
}
std::vector<CellInfo> Model::Cells() const {
    std::vector<CellInfo> out;
    for (const auto& c : state.cells)
        out.push_back(
            {c.begin, c.end, c.version, c.producer, c.holder, c.prior_version, c.unknown});
    return out;
}
Metrics Model::Measure(const State& s) {
    Metrics m;
    m.cells = s.cells.size();
    m.replicas = s.reps.size();
    // Structural payload estimate plus explicit map-node allowance, not allocator/RSS.
    m.metadata_bytes =
        sizeof(State) + s.cells.capacity() * sizeof(Cell) + s.chunks.capacity() * sizeof(Chunk);
    for (const auto& ch : s.chunks)
        m.metadata_bytes += ch.uses.capacity() * sizeof(Id);
    for (const auto& [id, v] : s.views) {
        (void)id;
        m.metadata_bytes += sizeof(View) + 64 + v.segments.capacity() * sizeof(Segment);
    }
    for (const auto& [id, r] : s.reps) {
        (void)id;
        m.metadata_bytes += sizeof(Representation) + 64 + r.segments.capacity() * sizeof(Segment) +
                            r.versions.capacity() * sizeof(Id);
        m.storage_bytes += r.bytes.capacity();
    }
    for (const auto& [id, op] : s.ops) {
        (void)id;
        m.metadata_bytes +=
            sizeof(Operation) + 64 + op.after.capacity() * sizeof(Ticket) +
            op.segments.capacity() * sizeof(Segment) + op.prior.capacity() * sizeof(Slice) +
            op.chunks.capacity() * sizeof(std::size_t) + op.readers.capacity() * sizeof(Id) +
            op.read_segments.capacity() * sizeof(Segment);
        m.storage_bytes += op.write_data.capacity();
        m.pending_producers += !Terminal(op.phase);
    }
    for (const auto& [id, l] : s.leases) {
        (void)id;
        m.metadata_bytes += sizeof(Lease) + 64 + l.segments.capacity() * sizeof(Segment) +
                            l.after.capacity() * sizeof(Ticket);
    }
    std::set<Id> live;
    for (const auto& c : s.cells)
        if (c.holder != CPU)
            live.insert(c.producer);
    m.producers = live.size();
    return m;
}
Metrics Model::Statistics() const {
    auto m = Measure(state);
    m.peak_metadata_bytes = metrics.peak_metadata_bytes;
    m.splits = metrics.splits;
    m.coalesces = metrics.coalesces;
    m.rejected = metrics.rejected;
    m.quarantined = metrics.quarantined;
    m.calls = metrics.calls;
    m.elapsed_ns = metrics.elapsed_ns;
    m.max_call_ns = metrics.max_call_ns;
    return m;
}
std::string Model::Fingerprint() const {
    std::ostringstream o;
    const auto list = [&](const auto& xs) {
        for (const auto& x : xs)
            o << +x << ',';
        o << ';';
    };
    const auto segments = [&](const auto& xs) {
        for (const auto& x : xs)
            o << x.epoch << ':' << x.logical << ':' << x.physical << ':' << x.size << ':' << x.hole
              << ',';
        o << ';';
    };
    o << state.epoch << ':' << state.clock << ':' << state.sequence << ':' << state.next_view << ':'
      << state.next_rep << ':' << state.next_lease << ':' << state.size << ':' << state.chunk_size
      << ';';
    for (const auto& c : state.cells)
        o << c.begin << ':' << c.end << ':' << c.version << ':' << c.producer << ':' << c.holder
          << ':' << c.prior_version << ':' << c.unknown << ';';
    for (const auto& [id, v] : state.views) {
        o << 'V' << id << ':' << v.epoch << ':' << v.manager << ':' << v.gpu_va << ':' << v.daddr
          << ':' << v.generation << ':' << v.size << ':' << v.mapped << ':' << v.mutating;
        segments(v.segments);
    }
    for (const auto& [id, r] : state.reps) {
        o << 'R' << id << ':' << r.epoch << ':' << unsigned(r.kind) << ':' << r.codec << ':'
          << r.size;
        segments(r.segments);
        list(r.bytes);
        list(r.versions);
    }
    for (const auto& [id, p] : state.ops) {
        o << 'P' << id << ':' << p.version << ':' << unsigned(p.phase) << ':' << p.representation
          << ':' << p.view << ':' << p.lease << ':' << p.queue << ':' << p.cpu << ':' << p.size
          << ':' << p.write_offset;
        for (const auto& d : p.after)
            o << d.epoch << ':' << d.id << ',';
        segments(p.segments);
        for (const auto& x : p.prior)
            o << x.physical << ':' << x.size << ':' << x.logical << ':' << x.source_logical << ':'
              << x.version << ':' << x.producer << ':' << x.holder << ',';
        list(p.write_data);
        list(p.chunks);
        list(p.readers);
        o << p.readonly << ';';
        o << p.executed << ';';
        segments(p.read_segments);
    }
    for (const auto& [id, l] : state.leases) {
        o << 'L' << id << ':' << l.view << ':' << l.write << ':' << l.size;
        segments(l.segments);
        for (const auto& d : l.after)
            o << d.epoch << ':' << d.id << ',';
    }
    for (const auto& ch : state.chunks) {
        o << 'C' << unsigned(ch.access) << ':' << ch.noncoherent;
        list(ch.uses);
    }
    return o.str();
}
void Model::CheckInvariants() const {
    const auto require = [](bool condition, const char* why) {
        if (!condition)
            throw std::logic_error(why);
    };
    std::size_t cursor = 0;
    for (const auto& c : state.cells) {
        require(c.begin == cursor && c.begin < c.end, "interval partition");
        cursor = c.end;
        require(state.reps.contains(c.holder), "authoritative resource retained");
        require(c.version <= state.clock && c.prior_version <= c.version, "version monotonicity");
        require(!c.producer || state.ops.contains(c.producer), "producer record retained");
        const bool ready = !c.producer || Terminal(state.ops.at(c.producer).phase);
        if (ready && !c.unknown)
            for (std::size_t p = c.begin; p < c.end; ++p) {
                const auto i = Locate(state.reps.at(c.holder), p);
                require(i.has_value(), "authority physical footprint");
                require(state.reps.at(c.holder).versions[*i] == c.version,
                        "completed authority version");
            }
    }
    require(cursor == state.size, "arena fully partitioned");
    for (const auto& [id, op] : state.ops) {
        require(id == op.id && id <= state.sequence && op.version <= state.clock,
                "operation identity");
        if (!Terminal(op.phase)) {
            require(state.reps.contains(op.representation), "pending producer lease");
            if (!op.executed) {
                for (const auto& p : op.prior)
                    require(state.reps.contains(p.holder), "pending predecessor operand lease");
                for (auto reader : op.readers)
                    require(state.reps.contains(reader), "pending reader lease");
            }
        }
        for (const auto& d : op.after)
            require(d.epoch == state.epoch && d.id < id && state.ops.contains(d.id),
                    "causal dependency DAG");
    }
    for (const auto& [id, l] : state.leases) {
        (void)id;
        for (const auto& x : l.segments)
            for (std::size_t i = x.physical / state.chunk_size;
                 i <= (x.physical + x.size - 1) / state.chunk_size; ++i)
                require(state.chunks[i].access == Access::CPUReady, "CPU lease exclusion");
    }
    const auto m = Measure(state);
    require(m.cells <= limits.cells && m.replicas <= limits.representations &&
                state.ops.size() <= limits.operations && state.views.size() <= limits.views &&
                m.metadata_bytes <= limits.metadata_bytes &&
                m.storage_bytes <= limits.storage_bytes,
            "metadata budget");
}
} // namespace Coherency
