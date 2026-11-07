// SPDX-License-Identifier: GPL-2.0-or-later
#include <atomic>
#include <cassert>
#include <functional>
#include <future>
#include <iostream>
#include <thread>
namespace ResumeBatchTest {
inline std::function<void(size_t)> after_resume;
inline std::function<void()> before_commit, before_entry_cas;
}
#include "common/producer_drain.h"
using Gate = Common::ProducerDrain;
using R = Gate::Result;
int main() {
    { // Permanent group membership supports repeated release and subset cancellation.
        Gate a,b,c;
        for (unsigned n=0;n<128;++n) {
            auto ca=a.TryClose(),cb=b.TryClose(),cc=c.TryClose();assert(ca&&cb&&cc);
            Gate::ResumeBatch batch;
            assert(batch.Add(*ca)&&batch.Add(*cb)&&batch.Add(*cc)&&batch.Commit());
            assert(a.TryEnter()&&b.TryEnter()&&c.TryEnter());
            assert(!batch.Commit()&&!ca->ResumeNoEffects());
        }
        auto ca=a.TryClose();assert(ca&&ca->ResumeNoEffects());
        auto next=a.TryClose();assert(next);
        ca->Quarantine();assert(!ca->ResumeNoEffects()&&!a.TryEnter());
        assert(next->ResumeNoEffects()&&a.TryEnter());
    }
    { // A pending grouped member refuses an early individual release without poisoning it.
        Gate a,b;auto ca=a.TryClose(),cb=b.TryClose();
        {Gate::ResumeBatch batch;assert(batch.Add(*ca)&&batch.Add(*cb)&&batch.Commit());}
        auto active=a.TryEnter();auto pending=a.TryClose(8);assert(active&&pending);
        assert(!pending->ResumeNoEffects()&&pending->Poll()==R::Pending);
        active.reset();assert(pending->ResumeNoEffects());
    }
    { // Uncertain preparation, abandoned batches and incompatible groups fail closed.
        Gate a,b;auto ca=a.TryClose(),cb=b.TryClose();b.Seal();
        {Gate::ResumeBatch batch;assert(batch.Add(*ca)&&batch.Add(*cb)&&!batch.Commit());}
        assert(!a.TryEnter()&&!b.TryEnter());
    }
    {Gate a;auto ca=a.TryClose();{Gate::ResumeBatch batch;assert(batch.Add(*ca));}
     assert(ca->Poll()==R::Unknown&&!a.TryEnter());}
    {Gate a,b;auto ca=a.TryClose(),cb=b.TryClose();
     {Gate::ResumeBatch batch;assert(batch.Add(*ca)&&batch.Commit());}
     {Gate::ResumeBatch batch;assert(batch.Add(*cb)&&batch.Commit());}
     auto na=a.TryClose(),nb=b.TryClose();assert(na&&nb);
     {Gate::ResumeBatch batch;assert(batch.Add(*na)&&batch.Add(*nb)&&!batch.Commit());}
     assert(!a.TryEnter()&&!b.TryEnter());}
    { // No missing publisher/alias certificate can be inferred from batch success.
        assert(Gate::GuestBackingAdmission()==R::Unknown);
    }
#ifdef RESUME_BATCH_INTERLEAVING
    { // Observe every local release while a real producer attempts entry.
        Gate a,b,c;auto ca=a.TryClose(),cb=b.TryClose(),cc=c.TryClose();unsigned visits=0;
        ResumeBatchTest::after_resume=[&](size_t) {
            std::thread producer{[&] {
                assert(!a.TryEnter()&&!b.TryEnter()&&!c.TryEnter()&&
                       "batch must hide each locally resumed member");
            }};producer.join();++visits;
        };
        {Gate::ResumeBatch batch;assert(batch.Add(*ca)&&batch.Add(*cb)&&batch.Add(*cc)&&batch.Commit());}
        ResumeBatchTest::after_resume={};assert(visits==3&&a.TryEnter()&&b.TryEnter()&&c.TryEnter());
    }
    { // A seal after the FIRST local release must block that already released member too.
        Gate a,b;auto ca=a.TryClose(),cb=b.TryClose();
        ResumeBatchTest::after_resume=[&](size_t i) {if(i==0)b.Seal();};
        {Gate::ResumeBatch batch;assert(batch.Add(*ca)&&batch.Add(*cb)&&!batch.Commit());}
        ResumeBatchTest::after_resume={};
        assert(!a.TryEnter()&&!b.TryEnter()&&"failed batch must retain already released members");
    }
    { // A seal after ALL local releases, immediately before group commit.
        Gate a,b;auto ca=a.TryClose(),cb=b.TryClose();
        ResumeBatchTest::before_commit=[&] {b.Seal();};
        {Gate::ResumeBatch batch;assert(batch.Add(*ca)&&batch.Add(*cb));
         assert(!batch.Commit()&&"Seal before final commit must reject the entire release");}
        ResumeBatchTest::before_commit={};
        assert(!a.TryEnter()&&!b.TryEnter()&&"failed batch must retain already released members");
    }
    { // Entry passed its initial group read before a subset started cancellation.
        Gate a,b;auto ca=a.TryClose(),cb=b.TryClose();
        {Gate::ResumeBatch batch;assert(batch.Add(*ca)&&batch.Add(*cb)&&batch.Commit());}
        auto next=b.TryClose();assert(next);
        std::promise<void> loaded,continue_entry,finished;
        auto ready=loaded.get_future(),proceed=continue_entry.get_future(),done=finished.get_future();
        ResumeBatchTest::before_entry_cas=[&] {loaded.set_value();proceed.wait();};
        std::thread producer{[&] {
            assert(!a.TryEnter()&&"late entry CAS must recheck the common barrier");
            finished.set_value();
        }};
        ready.wait();
        ResumeBatchTest::after_resume=[&](size_t) {continue_entry.set_value();done.wait();};
        {Gate::ResumeBatch batch;assert(batch.Add(*next)&&batch.Commit());}
        producer.join();ResumeBatchTest::after_resume={};ResumeBatchTest::before_entry_cas={};
        auto verify=a.TryClose();assert(verify&&verify->Poll()==R::DrainedSubset&&verify->ResumeNoEffects());
    }
#endif
    { // Extend an existing group without replacing its shared owner; member retirement seals it.
        Gate a,c;auto b=std::make_unique<Gate>();auto ca=a.TryClose(),cb=b->TryClose();
        {Gate::ResumeBatch batch;assert(batch.Add(*ca)&&batch.Add(*cb)&&batch.Commit());}
        auto na=a.TryClose(),nb=b->TryClose(),nc=c.TryClose();
        {Gate::ResumeBatch batch;assert(batch.Add(*na)&&batch.Add(*nb)&&batch.Add(*nc)&&batch.Commit());}
        b.reset();assert(!a.TryEnter()&&!c.TryEnter());
    }
    std::cout<<"PASS resume batch: repeated groups, stale/subset cancellation, pending/Unknown isolation";
#ifdef RESUME_BATCH_INTERLEAVING
    std::cout<<", local release and Seal interleavings, late entry rollback";
#endif
    std::cout<<"; Guest admission Unknown\n";
}
