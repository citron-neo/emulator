// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone candidate only. Not included by any production target.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace M3BCandidate {
using Id = std::uint64_t;
struct Progress {
    std::shared_ptr<std::atomic<Id>> owner=std::make_shared<std::atomic<Id>>(1);
    std::atomic<Id>& sequence;
    Progress():sequence(*owner) {}
    Progress(const Progress& p):owner(p.owner),sequence(*owner) {}
    Progress& operator=(const Progress&)=delete;
    void Notify() noexcept { sequence.fetch_add(1); sequence.notify_all(); }
};
struct Allocation {
    const Id epoch;
    alignas(4096) std::array<std::uint8_t, 16384> bytes{};
    explicit Allocation(Id e) : epoch(e) { bytes.fill(10); }
};
struct Range {
    Id offset{}, size{};
};
inline bool Overlap(Range a, Range b) {
    return a.size && b.size && (a.offset <= b.offset ? b.offset-a.offset<a.size : a.offset-b.offset<b.size);
}
// A plan owns actual fixture storage and an immutable footprint. No later VA
// lookup is permitted. Real Vulkan allocations/views need equivalent pins.
enum class ScopeCertificate { ControlledSingleReplica, Unsupported };
struct Plan {
    const std::shared_ptr<Allocation> allocation;
    const std::vector<Range> ranges;
    const std::shared_ptr<void> representation;
    Plan(std::shared_ptr<Allocation> a, std::vector<Range> r, std::shared_ptr<void> pin,
         ScopeCertificate certificate)
        : allocation(std::move(a)), ranges(std::move(r)), representation(std::move(pin)) {
        if (!allocation || !representation || certificate!=ScopeCertificate::ControlledSingleReplica || ranges.empty())
            throw std::invalid_argument("uncontrolled or unknown plan");
        for (std::size_t i=0;i<ranges.size();++i) {
            auto s=ranges[i];
            if (!s.size || s.offset>allocation->bytes.size() || s.size>allocation->bytes.size()-s.offset)
                throw std::invalid_argument("plan bounds");
            for (std::size_t j=0;j<i;++j)
                if (Overlap(s,ranges[j])) throw std::invalid_argument("repeated physical scope");
        }
    }
};
using PinnedPlan = std::shared_ptr<const Plan>;
enum class Phase { Pending, Running, Writing, Published, Done, Cancelled, Unknown };
enum class Start { Granted, Park, Cancelled, Reentrant };
struct Request {
    const void* const domain;
    const Id id;
    const PinnedPlan plan;
    const bool exclusive;
    Phase phase=Phase::Pending; // protected by Ranges metadata mutex
    std::vector<Range> published; // capacity prepared before any irreversible write
    Request(const void* d,Id n,PinnedPlan p,bool write):domain(d),id(n),plan(std::move(p)),exclusive(write) {
        published.reserve(plan->ranges.size());
    }
};
using Ticket = std::shared_ptr<Request>;
class Ranges {
    inline static thread_local Request* executing{}; // trivial TLS, no allocator/destructor
    struct State {
        std::mutex mutex;
        std::list<Ticket> requests;
        Id next=1;
        bool sealed=false;
        std::shared_ptr<std::atomic<bool>> epoch_closed=std::make_shared<std::atomic<bool>>(false);
        Progress progress;
        std::shared_ptr<State> retained;
    };
    std::shared_ptr<State> owner;
    std::mutex& mutex;
    std::list<Ticket>& requests;
    Id& next;
    bool& sealed;
    explicit Ranges(std::shared_ptr<State> s):owner(std::move(s)),mutex(owner->mutex),
        requests(owner->requests),next(owner->next),sealed(owner->sealed),progress(owner->progress) {}
    void Check(const Ticket& r) const {if(!r||r->domain!=owner.get()) throw std::logic_error("foreign ticket");}
    static bool Conflict(const Request& a,const Request& b) {
        if (a.plan->allocation!=b.plan->allocation) return false;
        if (a.phase==Phase::Unknown||b.phase==Phase::Unknown) return true;
        if (!a.exclusive&&!b.exclusive) return false;
        for(auto x:a.plan->ranges) for(auto y:b.plan->ranges) if(Overlap(x,y)) return true;
        return false;
    }
    static bool Active(Phase p) {return p==Phase::Running||p==Phase::Writing||p==Phase::Unknown;}
    void Remove(const Ticket& r) {requests.remove(r);}
    std::shared_ptr<State> RetireRootIfIdle() {
        for(const auto& r:requests) if(Active(r->phase)) return {};
        return std::move(owner->retained);
    }
public:
    class ExecutionScope {
        Ticket owner;
        Request* previous;
    public:
        explicit ExecutionScope(Ticket r):owner(std::move(r)),previous(std::exchange(executing,owner.get())) {}
        ~ExecutionScope() {executing=previous;}
        ExecutionScope(const ExecutionScope&)=delete;
    };
    Progress& progress;
    Ranges():Ranges(std::make_shared<State>()) {}
    Ranges(const Ranges&)=delete;
    std::shared_ptr<Ranges> RecoveryOwner() {return std::shared_ptr<Ranges>(new Ranges(owner));}
    Ticket Enqueue(PinnedPlan p,bool write) {
        Id id;
        {std::lock_guard lock{mutex};if(sealed||owner->epoch_closed->load()) return {};id=next++;}
        auto r=std::make_shared<Request>(owner.get(),id,std::move(p),write);
        {
            std::lock_guard lock{mutex};
            if(sealed||owner->epoch_closed->load()) return {};
            requests.push_back(r);progress.Notify(); // insertion is request-order linearization
        }
        return r;
    }
    Start TryStart(const Ticket& r,const Ticket& parent={}) {
        std::lock_guard lock{mutex};
        if(!r || r->domain!=owner.get() || r->phase==Phase::Cancelled || sealed || owner->epoch_closed->load()) return Start::Cancelled;
        if(parent) Check(parent);
        auto* inherited=parent ? parent.get() : executing;
        if(inherited && inherited->domain==owner.get() && Active(inherited->phase) && Conflict(*inherited,*r)) return Start::Reentrant;
        if(r->phase!=Phase::Pending) throw std::logic_error("ticket reused");
        for(const auto& older:requests) {
            if(older==r) break;
            if((older->phase==Phase::Pending||Active(older->phase)) && Conflict(*older,*r)) return Start::Park;
        }
        // A later disjoint request may already be running; range overlap, not
        // list position or thread identity, decides exclusion.
        for(const auto& other:requests)
            if(other!=r && Active(other->phase) && Conflict(*other,*r)) return Start::Park;
        r->phase=Phase::Running;owner->retained=owner;return Start::Granted;
    }
    Phase StateOf(const Ticket& r) {std::lock_guard lock{mutex};Check(r);return r->phase;}
    void BeginWrite(const Ticket& r) {
        std::lock_guard lock{mutex};
        Check(r);
        if(r->phase!=Phase::Running||!r->exclusive) throw std::logic_error("not writable");
        r->phase=Phase::Writing;
    }
    // Extents must be supplied from preallocated caller storage. Invalid
    // post-store bookkeeping quarantines the obligation instead of rolling back.
    bool Publish(const Ticket& r,std::span<const Range> done) {
        std::shared_ptr<State> retention;
        std::lock_guard lock{mutex};
        Check(r);
        if(r->phase!=Phase::Writing) throw std::logic_error("publication outside writing");
        bool valid=done.size()<=r->published.capacity();
        for(auto x:done) {
            bool contained=false;
            for(auto y:r->plan->ranges)
                contained|=x.size && x.offset>=y.offset && x.offset-y.offset<=y.size && x.size<=y.size-(x.offset-y.offset);
            valid&=contained;
        }
        for(std::size_t i=0;i<done.size();++i) for(std::size_t j=0;j<i;++j) valid&=!Overlap(done[i],done[j]);
        if(!valid) {r->phase=Phase::Unknown;owner->retained=owner;progress.Notify();return false;}
        r->published.resize(done.size());
        std::copy(done.begin(),done.end(),r->published.begin()); // prepared capacity, trivial Range
        r->phase=Phase::Published;Remove(r);retention=RetireRootIfIdle();progress.Notify();return true;
    }
    void CompleteNoWrite(const Ticket& r) {
        std::shared_ptr<State> retention;
        std::lock_guard lock{mutex};
        Check(r);
        if(r->phase!=Phase::Running) throw std::logic_error("unexecuted/modified completion");
        r->phase=Phase::Done;Remove(r);retention=RetireRootIfIdle();progress.Notify();
    }
    void CompleteFailedCompare(const Ticket& r,bool comparison_succeeded) {
        std::shared_ptr<State> retention;
        std::lock_guard lock{mutex};Check(r);
        if(comparison_succeeded||r->phase!=Phase::Writing) throw std::logic_error("no failed-compare proof");
        r->phase=Phase::Done;Remove(r);retention=RetireRootIfIdle();progress.Notify();
    }
    bool Cancel(const Ticket& r) {
        std::lock_guard lock{mutex};
        if(!r || r->domain!=owner.get() || r->phase!=Phase::Pending) return false;
        r->phase=Phase::Cancelled;Remove(r);progress.Notify();return true;
    }
    void Unknown(const Ticket& r) {
        std::lock_guard lock{mutex};
        Check(r);
        if(!Active(r->phase)) throw std::logic_error("unknown without execution");
        r->phase=Phase::Unknown;owner->retained=owner;progress.Notify();
    }
    void Seal() {
        CloseEpoch();
        std::list<Ticket> reaped;
        {
            std::lock_guard lock{mutex};sealed=true;
            for(auto it=requests.begin();it!=requests.end();) {
                if((*it)->phase==Phase::Pending) {(*it)->phase=Phase::Cancelled;auto victim=it++;reaped.splice(reaped.end(),requests,victim);} else ++it;
            }
            progress.Notify();
        }
    }
    std::shared_ptr<std::atomic<bool>> ClosedWord() const {return owner->epoch_closed;}
    void CloseEpoch() noexcept {owner->epoch_closed->store(true,std::memory_order_release);progress.Notify();}
    bool Drained() {
        std::lock_guard lock{mutex};
        for(const auto& r:requests) if(Active(r->phase)||r->phase==Phase::Pending) return false;
        return true;
    }
    void ReleaseUnknownAfterQuiescence(bool all_executors_stopped) {
        std::list<Ticket> reaped;
        std::shared_ptr<State> retention;
        {
            std::lock_guard lock{mutex};
            if(!sealed||!all_executors_stopped) throw std::logic_error("no quiescence proof");
            for(const auto& r:requests)
                if(r->phase==Phase::Running||r->phase==Phase::Writing) throw std::logic_error("executor still active");
            for(auto it=requests.begin();it!=requests.end();) {
                if((*it)->phase==Phase::Unknown) {auto victim=it++;reaped.splice(reaped.end(),requests,victim);} else ++it;
            }
            retention=std::move(owner->retained);
            progress.Notify(); // lifetime release, never data recovery/publication
        }
    }
};

// Mapping intent gates only short resolution, never queued/suspended physical
// plans. A mutator's preservation runs from Old(), outside this metadata lock.
class Mapping {
    struct State {
        std::mutex mutex;
        PinnedPlan current,next;
        std::size_t resolving=0;
        bool intent=false,sealed=false;
        Id generation=0;
        Progress progress;
        State(PinnedPlan p,Progress& signal):current(std::move(p)),progress(signal) {}
    };
    std::shared_ptr<State> state;
public:
    class Resolution {
        std::shared_ptr<State> owner;
        friend class Mapping;
        Resolution(std::shared_ptr<State> s,PinnedPlan p):owner(std::move(s)),plan(std::move(p)) {}
    public:
        const PinnedPlan plan;
        Resolution(const Resolution&)=delete;
        Resolution(Resolution&& r) noexcept:owner(std::move(r.owner)),plan(r.plan) {}
        ~Resolution() {Release();}
        void Release() noexcept {
            if(!owner) return;
            auto s=std::move(owner);
            {std::lock_guard lock{s->mutex};--s->resolving;}
            s->progress.Notify();
        }
    };
    Mapping(PinnedPlan p,Progress& signal):state(std::make_shared<State>(std::move(p),signal)) {}
    std::optional<Resolution> BeginResolve() {
        std::lock_guard lock{state->mutex};if(state->intent||state->sealed) return {};
        ++state->resolving;return Resolution(state,state->current);
    }
    bool Intent(PinnedPlan p) {
        std::lock_guard lock{state->mutex};if(state->intent||state->sealed) return false;
        state->next=std::move(p);state->intent=true;return true;
    }
    PinnedPlan Old() {std::lock_guard lock{state->mutex};if(!state->intent) throw std::logic_error("no mutation");return state->current;}
    bool ResolversDrained() {std::lock_guard lock{state->mutex};return state->resolving==0;}
    bool Apply(bool preservation_visible) {
        PinnedPlan retired;
        {
            std::lock_guard lock{state->mutex};
            if(!state->intent||state->resolving||!preservation_visible||state->sealed) return false;
            retired=std::move(state->current);state->current=std::move(state->next);++state->generation;state->intent=false;
        }
        state->progress.Notify();
        return true;
    }
    Id Generation() {std::lock_guard lock{state->mutex};return state->generation;}
    void Seal() {PinnedPlan discarded;{std::lock_guard lock{state->mutex};state->sealed=true;discarded=std::move(state->next);state->intent=false;}state->progress.Notify();}
};

// Per-canonical-physical-page fast handshake. No global metadata lock is used
// for untracked entry/exit. Resolution/pinning must precede this handshake.
class Page {
    static constexpr Id Tracked=1,Closing=2,Closed=4,One=8;
    struct Node {
        std::atomic<Id> state{0};
        Progress progress;
        const std::shared_ptr<Allocation> allocation;
        const Id offset;
        const std::shared_ptr<std::atomic<bool>> epoch_closed;
        Node(Progress& p,std::shared_ptr<Allocation> a,Id o,std::shared_ptr<std::atomic<bool>> closed)
            :progress(p),allocation(std::move(a)),offset(o),epoch_closed(std::move(closed)) {}
    };
    std::shared_ptr<Node> node;
public:
    explicit Page(Progress& p,std::shared_ptr<Allocation> a={},Id offset=0,
        std::shared_ptr<std::atomic<bool>> closed=std::make_shared<std::atomic<bool>>(false))
        :node(std::make_shared<Node>(p,std::move(a),offset,std::move(closed))) {}
    class Fast {
        std::shared_ptr<Node> page;
        friend class Page;
        explicit Fast(std::shared_ptr<Node> p):page(std::move(p)) {}
    public:
        Fast()=default;Fast(const Fast&)=delete;
        Fast(Fast&& f) noexcept:page(std::move(f.page)) {}
        ~Fast() {Release();}
        explicit operator bool() const {return page!=nullptr;}
        void Release() noexcept {
            if(!page) return;
            auto p=std::move(page);
            auto old=p->state.fetch_sub(One,std::memory_order_release);
            if(((old&(Closing|Closed))||p->epoch_closed->load()) && (old/One)==1) p->progress.Notify();
        }
    };
    Fast TryFast() {
        if(node->epoch_closed->load(std::memory_order_acquire)) return {};
        auto& state=node->state;
        auto v=state.load(std::memory_order_acquire);
        while(!(v&(Tracked|Closing|Closed))) {
            if(v>UINT64_MAX-One) return {};
            if(state.compare_exchange_weak(v,v+One,std::memory_order_acquire)) {
                Fast fast(node);
                if(node->epoch_closed->load(std::memory_order_acquire)) {fast.Release();return {};}
                return fast;
            }
        }
        return {};
    }
    bool CloseForRegistration() {
        if(node->epoch_closed->load(std::memory_order_acquire)) return false;
        auto& state=node->state;
        auto v=state.load(std::memory_order_acquire);
        while(!(v&(Tracked|Closing|Closed)))
            if(state.compare_exchange_weak(v,v|Closing,std::memory_order_acq_rel)) return true;
        return false;
    }
    bool Drained() const {return node->state.load(std::memory_order_acquire)==Closing;}
    bool PublishTracked() {
        Id v=Closing;
        bool ok=!node->epoch_closed->load() && node->state.compare_exchange_strong(v,Tracked,std::memory_order_release);
        if(ok) node->progress.Notify();
        return ok;
    }
    bool IsTracked() const {return node->state.load(std::memory_order_acquire)&Tracked;}
    bool NoFastUsers() const {return node->state.load(std::memory_order_acquire)/One==0;}
    bool Matches(const Plan& plan) const {
        return node->allocation && node->allocation==plan.allocation && plan.ranges.size()==1 &&
            plan.ranges[0].offset==node->offset && plan.ranges[0].size==4096;
    }
    void Seal() {node->state.fetch_or(Closed,std::memory_order_acq_rel);node->progress.Notify();}
};
inline Ticket RegistrationIntent(Ranges& ranges,Page& page,PinnedPlan plan) {
    // Reserve slow-path priority BEFORE closing fast entry. A writer that sees
    // Closing cannot overtake an as-yet-unqueued registration callback.
    if(!plan||!page.Matches(*plan)) throw std::invalid_argument("registration gate/physical plan mismatch");
    auto ticket=ranges.Enqueue(std::move(plan),true);
    if(ticket && !page.CloseForRegistration()) {ranges.Cancel(ticket);return {};}
    return ticket;
}
class PhysicalDomain {
    std::mutex registry_mutex;
    bool closed=false;
    std::map<std::pair<Allocation*,Id>,std::shared_ptr<Page>> pages;
public:
    Ranges ranges;
    std::shared_ptr<Page> PageFor(std::shared_ptr<Allocation> allocation,Id offset) {
        std::lock_guard lock{registry_mutex};
        if(closed||ranges.ClosedWord()->load()) return {};
        if(!allocation||offset%4096||offset>allocation->bytes.size()||4096>allocation->bytes.size()-offset)
            throw std::invalid_argument("physical page geometry");
        auto key=std::pair{allocation.get(),offset};
        auto it=pages.find(key);
        if(it==pages.end()) it=pages.emplace(key,std::make_shared<Page>(ranges.progress,allocation,offset,ranges.ClosedWord())).first;
        return it->second;
    }
    void Seal() {
        ranges.CloseEpoch();
        {
            std::lock_guard lock{registry_mutex};closed=true;
            for(auto& [key,page]:pages) { (void)key;page->Seal(); }
        }
        ranges.Seal();
    }
    bool Drained() {
        std::lock_guard lock{registry_mutex};
        if(!closed) return false;
        for(auto& [key,page]:pages) {(void)key;if(!page->NoFastUsers()) return false;}
        return ranges.Drained();
    }
};
class FastSet {
    std::array<std::optional<Page::Fast>,2> leases;
public:
    FastSet()=default;FastSet(const FastSet&)=delete;FastSet(FastSet&&)=default;
    static std::optional<FastSet> Try(std::span<const std::shared_ptr<Page>> pages) {
        if(pages.empty()||pages.size()>2) return {}; // bounded scalar path, no allocation
        FastSet result;
        for(std::size_t i=0;i<pages.size();++i) {
            auto fast=pages[i]->TryFast();
            if(!fast) return {}; // only provisional counters unwind, no store occurred
            result.leases[i].emplace(std::move(fast));
        }
        return result;
    }
    void Release() noexcept {for(auto& lease:leases) lease.reset();}
};
} // namespace M3BCandidate
