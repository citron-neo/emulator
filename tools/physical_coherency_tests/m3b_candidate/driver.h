// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only continuation adapter over the real ThreadManager. No nested pump.
#pragma once
#include <functional>
#include <thread>
#include "protocol.h"

namespace M3BCandidate {
enum class Work { Complete, KeepLease };
enum class JobState { Pending, Parked, Suspended, Granted, Done, Cancelled, Unknown };
struct Job {
    const Id lane;
    const PinnedPlan plan;
    const bool exclusive;
    Ticket ticket;
    std::function<Work(const Ticket&)> action;
    std::function<bool()> eligible=[] {return true;};
    std::function<bool()> guest_ready=[] {return true;};
    std::atomic<JobState> state{JobState::Pending};
    Id attempted{}; // driver thread only
    Event parked,suspended,granted,finished;
    Job(Id l,PinnedPlan p,bool w):lane(l),plan(std::move(p)),exclusive(w) {}
};
class Driver {
    Ranges& ranges;
    std::function<VideoCommon::CallbackOutcome(std::function<void()>)> dispatch;
    std::mutex mutex;
    std::list<std::shared_ptr<Job>> jobs;
    static constexpr std::size_t MaxJobs=64;
    std::atomic<bool> stopping=false;
    std::thread worker;
    void Run() {
        while(!stopping) {
            auto revision=ranges.progress.sequence.load();
            std::array<std::shared_ptr<Job>,MaxJobs> heads;
            std::array<Id,MaxJobs> lanes{};std::size_t count=0;
            std::list<std::shared_ptr<Job>> retired;
            {
                std::lock_guard lock{mutex};
                for(auto it=jobs.begin();it!=jobs.end();) {
                    auto state=(*it)->state.load();
                    if(state==JobState::Done||state==JobState::Cancelled) {auto victim=it++;retired.splice(retired.end(),jobs,victim);} else ++it;
                }
                for(auto j:jobs) {
                    auto state=j->state.load();
                    if(state==JobState::Done||state==JobState::Cancelled) continue;
                    if(std::find(lanes.begin(),lanes.begin()+count,j->lane)==lanes.begin()+count) {
                        lanes[count]=j->lane;
                        try {if(!j->ticket) j->ticket=ranges.Enqueue(j->plan,j->exclusive);}
                        catch(const std::bad_alloc&) {j->state=JobState::Cancelled;j->finished.Set();ranges.progress.Notify();continue;}
                        if(!j->ticket) {j->state=JobState::Cancelled;j->finished.Set();ranges.progress.Notify();}
                        else heads[count++]=j;
                    }
                }
            }
            retired.clear(); // arbitrary payload destruction outside driver metadata
            for(std::size_t index=0;index<count;++index) {
                auto j=heads[index];
                auto state=j->state.load();
                if((state!=JobState::Pending&&state!=JobState::Parked)||j->attempted==revision) continue;
                j->attempted=revision;
                auto outcome=dispatch([&,j] {
                    if(ranges.StateOf(j->ticket)==Phase::Cancelled) {
                        j->state=JobState::Cancelled;j->finished.Set();return;
                    }
                    if(!j->guest_ready()) {
                        if(!ranges.Cancel(j->ticket)) throw std::logic_error("suspend active work");
                        j->state=JobState::Suspended;j->suspended.Set();return;
                    }
                    if(!j->eligible()) {j->state=JobState::Parked;j->parked.Set();return;}
                    auto start=ranges.TryStart(j->ticket);
                    if(start==Start::Cancelled) {j->state=JobState::Cancelled;j->finished.Set();return;}
                    if(start!=Start::Granted) {j->state=JobState::Parked;j->parked.Set();return;}
                    j->state=JobState::Granted;
                    Work work;
                    {Ranges::ExecutionScope context(j->ticket);work=j->action(j->ticket);}
                    if(work==Work::Complete) {
                        ranges.CompleteNoWrite(j->ticket);j->state=JobState::Done;j->finished.Set();
                    } else j->granted.Set();
                });
                if(outcome!=VideoCommon::CallbackOutcome::Completed) {
                    auto phase=ranges.StateOf(j->ticket);
                    if(phase==Phase::Running||phase==Phase::Writing) {
                        ranges.Unknown(j->ticket);j->state=JobState::Unknown;
                    } else {ranges.Cancel(j->ticket);j->state=JobState::Cancelled;}
                    j->finished.Set();
                }
            }
            if(!stopping) ranges.progress.sequence.wait(revision);
        }
    }
public:
    template<class Dispatch>
    Driver(Ranges& r,Dispatch d):ranges(r),dispatch([d](auto f) mutable {
        return VideoCommon::RunAcknowledgedCallback(d,std::move(f));
    }),worker([this] {Run();}) {}
    ~Driver() {Stop();}
    std::shared_ptr<Job> Submit(Id lane,PinnedPlan plan,bool exclusive,
        std::function<Work(const Ticket&)> action,
        std::function<bool()> eligible=[] {return true;},
        std::function<bool()> guest_ready=[] {return true;},Ticket reserved={}) {
        auto j=std::make_shared<Job>(lane,std::move(plan),exclusive);
        j->action=std::move(action);j->eligible=std::move(eligible);j->guest_ready=std::move(guest_ready);
        if(reserved && (reserved->plan!=j->plan || reserved->exclusive!=exclusive)) throw std::logic_error("wrong preaccepted scope");
        j->ticket=std::move(reserved);
        {
            std::lock_guard lock{mutex};
            // Only ready lane heads obtain data tickets; tails own plan pins.
            if(ranges_sealed) {j->state=JobState::Cancelled;j->finished.Set();}
            if(jobs.size()>=MaxJobs) throw std::length_error("candidate continuation capacity");
            jobs.push_back(j);
        }
        ranges.progress.Notify();return j;
    }
    // Driver must not race requests' ticket replacement. Resume is an explicit
    // scheduler continuation command, serialized on the driver metadata mutex.
    void Resume(const std::shared_ptr<Job>& j) {
        std::lock_guard lock{mutex};
        if(j->state!=JobState::Suspended) throw std::logic_error("not suspended");
        j->ticket=ranges.Enqueue(j->plan,j->exclusive);
        if(j->ticket) j->state=JobState::Pending;
        else {j->state=JobState::Cancelled;j->finished.Set();}
        ranges.progress.Notify();
    }
    void ExternalDone(const std::shared_ptr<Job>& j) {
        auto phase=ranges.StateOf(j->ticket);
        if(phase!=Phase::Published && phase!=Phase::Done) throw std::logic_error("logical work not retired");
        j->state=JobState::Done;j->finished.Set();ranges.progress.Notify();
    }
    void Seal() {
        std::lock_guard lock{mutex};ranges_sealed=true;ranges.Seal();
        for(auto j:jobs) {
            if(!j->ticket || ranges.StateOf(j->ticket)==Phase::Cancelled) {
                j->state=JobState::Cancelled;j->finished.Set();
            }
        }
        ranges.progress.Notify();
    }
    void Stop() {stopping=true;ranges.progress.Notify();if(worker.joinable()) worker.join();}
    std::size_t RetainedJobs() {std::lock_guard lock{mutex};return jobs.size();}
private:
    bool ranges_sealed=false;
};
} // namespace M3BCandidate
