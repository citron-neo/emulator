// SPDX-License-Identifier: GPL-2.0-or-later
// Appended after the real ThreadManager TU and generated Memory fixture.
#include "protocol.h"
#include "driver.h"
using namespace M3BCandidate;
struct Replica {
    std::array<u8,16384> bytes{};
    std::array<bool,16384> gpu{},cpu{};
    Replica() {bytes.fill(10);}
    void GpuWrite(Range r,u8 x) {for(Id i=r.offset;i<r.offset+r.size;++i) {bytes[i]=x;gpu[i]=true;cpu[i]=false;}}
    void Preserve(Allocation& a,Range r) {for(Id i=r.offset;i<r.offset+r.size;++i) if(gpu[i]&&!cpu[i]) {a.bytes[i]=bytes[i];gpu[i]=false;}}
    void Publish(Range r) {for(Id i=r.offset;i<r.offset+r.size;++i) {cpu[i]=true;gpu[i]=false;}}
    void Upload(Allocation& a,Range r) {for(Id i=r.offset;i<r.offset+r.size;++i) if(cpu[i]) {bytes[i]=a.bytes[i];cpu[i]=false;}}
};
PinnedPlan PlanFor(std::shared_ptr<Allocation> a,std::vector<Range> ranges={{0,4}},std::shared_ptr<void> replica={}) {
    if(!replica) replica=std::make_shared<Replica>();
    return std::make_shared<Plan>(std::move(a),std::move(ranges),std::move(replica),ScopeCertificate::ControlledSingleReplica);
}
struct Environment {
    Core::System system;VideoCore::RendererBase renderer;
    Core::Frontend::GraphicsContext context;Tegra::Control::Scheduler scheduler;
    PhysicalDomain domain;Ranges& ranges=domain.ranges;
    VideoCommon::GPUThread::ThreadManager manager{system,true};
    std::unique_ptr<Driver> driver;
    Environment() {
        manager.StartThread(renderer,context,scheduler);
        driver=std::make_unique<Driver>(ranges,[this](auto callback) {manager.RunOnGpuThread(std::move(callback));});
    }
    ~Environment() {driver->Stop();manager.Stop();driver.reset();}
};
void Commit(Environment& e,const std::shared_ptr<Job>& j,std::span<const Range> done) {
    Require(e.ranges.Publish(j->ticket,done),"CPU publication rejected");e.driver->ExternalDone(j);
}
void old_work_mutation_preserve() {
    Environment e;auto a=std::make_shared<Allocation>(1),b=std::make_shared<Allocation>(2);
    auto replica=std::make_shared<Replica>();auto p=PlanFor(a,{{0,4}},replica);Mapping mapping(p,e.ranges.progress);
    auto resolved=mapping.BeginResolve();Require(resolved.has_value(),"initial resolution");resolved->Release();
    Event old_entered,old_release;
    auto older=e.driver->Submit(1,p,true,[&](auto) {old_entered.Set();old_release.Wait();replica->GpuWrite({0,4},99);return Work::Complete;});
    old_entered.Wait();Require(mapping.Intent(PlanFor(b)),"mutation intent");
    Require(!mapping.BeginResolve(),"intent admitted new resolver");
    auto cpu=e.driver->Submit(2,p,true,[&](auto) {replica->Preserve(*a,{0,4});return Work::KeepLease;});
    auto mutation=e.driver->Submit(3,mapping.Old(),true,[&](auto) {
        replica->Preserve(*a,{0,4});Require(mapping.Apply(true),"old-use preservation/apply");return Work::Complete;
    });
    old_release.Set();cpu->granted.Wait();mutation->parked.Wait();
    CpuMemory view(a);e.ranges.BeginWrite(cpu->ticket);view.Write<u8>(4097,77);
    replica->Publish({1,1});std::array<Range,1> done{{{1,1}}};Commit(e,cpu,done);
    mutation->finished.Wait();older->finished.Wait();
    Require(a->bytes[0]==99&&a->bytes[1]==77&&a->bytes[2]==99,"complement lost");
    Require(mapping.Generation()==1,"mutation did not progress");
    auto now=mapping.BeginResolve();Require(now&&now->plan->allocation==b,"new mapping not published");now->Release();
}
void suspended_channel() {
    Environment e;auto a=std::make_shared<Allocation>(3),b=std::make_shared<Allocation>(4);
    auto p=PlanFor(a);Mapping mapping(p,e.ranges.progress);std::atomic<bool> semaphore=false;std::atomic<int> order=0;
    auto wait=e.driver->Submit(7,p,false,[&](auto) {Require(semaphore&&order++==0,"resume order");return Work::Complete;},
        [] {return true;},[&] {return semaphore.load();});
    wait->suspended.Wait();
    auto tail=e.driver->Submit(7,p,false,[&](auto) {Require(order++==1,"tail passed suspended head");return Work::Complete;});
    Require(mapping.Intent(PlanFor(b)),"suspended mutation intent");
    auto mutate=e.driver->Submit(8,mapping.Old(),true,[&](auto) {Require(mapping.Apply(true),"suspended plan blocked mutation");return Work::Complete;});
    mutate->finished.Wait();
    auto cpu=e.driver->Submit(9,p,true,[&](auto) {return Work::KeepLease;});cpu->granted.Wait();
    CpuMemory view(a);e.ranges.BeginWrite(cpu->ticket);view.Write<u8>(4096,77);semaphore=true;
    std::array<Range,1> done{{{0,1}}};Commit(e,cpu,done);e.driver->Resume(wait);
    wait->finished.Wait();tail->finished.Wait();Require(order==2,"lane stalled");
    Require(a->bytes[0]==77&&b->bytes[0]==10,"suspended immutable address retargeted");
}
void new_cpu_producer_during_unrelated_intent() {
    Environment e;auto a=std::make_shared<Allocation>(26),b=std::make_shared<Allocation>(27),c=std::make_shared<Allocation>(28);
    auto p=PlanFor(a);Mapping semaphore(p,e.ranges.progress),unrelated(PlanFor(b),e.ranges.progress);
    std::atomic<bool> released=false;
    auto channel=e.driver->Submit(1,p,false,[&](auto) {Require(a->bytes[0]==77,"new producer not visible to resumed channel");return Work::Complete;},
        [] {return true;},[&] {return released.load();});channel->suspended.Wait();
    auto resolving=unrelated.BeginResolve();Require(bool(resolving)&&unrelated.Intent(PlanFor(c)),"unrelated intent");
    auto mutation=e.driver->Submit(2,unrelated.Old(),true,[&](auto) {Require(unrelated.Apply(true),"unrelated mutation");return Work::Complete;},[&] {return unrelated.ResolversDrained();});
    mutation->parked.Wait();
    auto fresh=semaphore.BeginResolve();Require(bool(fresh),"unrelated intent closed new CPU producer admission");
    auto physical=fresh->plan;fresh.reset();
    auto cpu=e.driver->Submit(3,physical,true,[](auto) {return Work::KeepLease;});cpu->granted.Wait();
    CpuMemory view(a);e.ranges.BeginWrite(cpu->ticket);view.Write<u8>(4096,77);
    std::array<Range,1> done{{{0,1}}};Commit(e,cpu,done);released=true;e.driver->Resume(channel);channel->finished.Wait();
    Require(!unrelated.BeginResolve(),"mutation intent was dropped to make guest progress");
    resolving.reset();mutation->finished.Wait();
}
void upload_during_store() {
    Environment e;auto a=std::make_shared<Allocation>(5);auto replica=std::make_shared<Replica>();auto p=PlanFor(a,{{0,4}},replica);
    auto cpu=e.driver->Submit(1,p,true,[](auto) {return Work::KeepLease;});cpu->granted.Wait();
    auto gpu=e.driver->Submit(2,p,false,[&](auto) {replica->Upload(*a,{0,4});return Work::Complete;});gpu->parked.Wait();
    auto other=PlanFor(a,{{8192,1}});std::atomic<bool> independent=false;
    auto preservation=e.driver->Submit(3,other,true,[&](auto) {independent=true;return Work::Complete;});preservation->finished.Wait();
    Require(independent&&replica->bytes[0]==10,"parked GPU blocked worker or consumed dirty");
    CpuMemory view(a);e.ranges.BeginWrite(cpu->ticket);view.Write<u8>(4096,77);
    Require(replica->bytes[0]==10,"store became an upload before publication");
    replica->Publish({0,1});std::array<Range,1> done{{{0,1}}};Commit(e,cpu,done);gpu->finished.Wait();
    Require(replica->bytes[0]==77,"later GPU did not see completed store");
}
void cas_and_exact_publication() {
    Environment e;auto a=std::make_shared<Allocation>(6);auto replica=std::make_shared<Replica>();auto p=PlanFor(a,{{0,4}},replica);
    replica->GpuWrite({0,4},99);
    auto failed=e.driver->Submit(1,p,true,[&](auto) {replica->Preserve(*a,{0,4});return Work::KeepLease;});failed->granted.Wait();
    CpuMemory view(a);view.RasterizerMarkRegionCached(4096,4096,true);
    e.ranges.BeginWrite(failed->ticket);const bool compared=view.WriteExclusive<u8>(4097,77,88);
    Require(!compared,"failed CAS succeeded");
    e.ranges.CompleteFailedCompare(failed->ticket,compared);e.driver->ExternalDone(failed);
    Require(failed->ticket->published.empty()&&!replica->cpu[1]&&a->bytes[1]==99,"failed CAS published");
    auto success=e.driver->Submit(1,p,true,[](auto) {return Work::KeepLease;});success->granted.Wait();
    e.ranges.BeginWrite(success->ticket);Require(view.WriteExclusive<u8>(4097,77,99),"CAS expected value lost");
    replica->Publish({1,1});std::array<Range,1> done{{{1,1}}};Commit(e,success,done);
    Require(success->ticket->published.size()==1&&success->ticket->published[0].offset==1,"CAS extent");
    Require(!replica->cpu[0]&&replica->cpu[1]&&!replica->cpu[2]&&a->bytes[0]==99&&a->bytes[2]==99,"CAS complement");
}
void split_and_partial_block() {
    Environment e;auto a=std::make_shared<Allocation>(7);auto p=PlanFor(a,{{4095,1},{4096,7}});
    auto cpu=e.driver->Submit(1,p,true,[](auto) {return Work::KeepLease;});cpu->granted.Wait();
    CpuMemory view(a);view.RasterizerMarkRegionCached(4096,8192,true);
    e.ranges.BeginWrite(cpu->ticket);view.Write<u64>(8191,0x8877665544332211ULL);
    std::array<Range,2> done{{{4095,1},{4096,7}}};Commit(e,cpu,done);
    Require(a->bytes[4095]==0x11&&a->bytes[4102]==0x88,"split scalar bytes");
    auto block=PlanFor(a,{{0,8192}});auto j=e.driver->Submit(1,block,true,[](auto) {return Work::KeepLease;});j->granted.Wait();
    view.table.entries[2].pointer.Store(0,Common::PageType::Unmapped);
    std::array<u8,8192> source;source.fill(42);e.ranges.BeginWrite(j->ticket);
    Require(!view.WriteBlockImpl<false>(4096,source.data(),source.size()),"block hole reported success");
    // Candidate observer records real memcpy extents in the generated fixture,
    // not a synthetic all-or-nothing return from WalkBlock.
    Require(view.completed[0]==4096&&view.completed[1]==0,"block completed extent observer");
    std::array<Range,1> prefix{{{0,4096}}};Commit(e,j,prefix);
    Require(j->ticket->published[0].size==4096&&a->bytes[4096]==0x22,"partial overwrite/extent");
}
void conflicting_and_disjoint_writers() {
    Environment e;auto a=std::make_shared<Allocation>(8);auto p=PlanFor(a,{{0,1}}),q=PlanFor(a,{{4096,1}});
    auto first=e.driver->Submit(1,p,true,[](auto) {return Work::KeepLease;});first->granted.Wait();
    Event entered,resume;CpuMemory first_view(a);
    first_view.table.entries[1].pointer.after_load=[&] {entered.Set();resume.Wait();};
    e.ranges.BeginWrite(first->ticket);
    auto writing=std::async(std::launch::async,[&] {
        first_view.Write<u8>(4096,77);std::array<Range,1> done{{{0,1}}};Commit(e,first,done);
    });
    entered.Wait();
    auto same=e.driver->Submit(2,p,true,[](auto) {return Work::KeepLease;});same->parked.Wait();
    auto other=e.driver->Submit(3,q,true,[](auto) {return Work::KeepLease;});other->granted.Wait();
    CpuMemory view(a);e.ranges.BeginWrite(other->ticket);view.Write<u8>(8192,88);
    std::array<Range,1> d2{{{4096,1}}};Commit(e,other,d2);
    resume.Set();Ready(writing);std::array<Range,1> d1{{{0,1}}};
    same->granted.Wait();e.ranges.BeginWrite(same->ticket);view.Write<u8>(4096,66);Commit(e,same,d1);
    Require(a->bytes[0]==66&&a->bytes[4096]==88,"writer order/independence");
}
void registration_cpu_first() {
    Environment e;auto a=std::make_shared<Allocation>(9);auto p=PlanFor(a,{{0,4096}});auto gate=e.domain.PageFor(a,0);Page& page=*gate;CpuMemory view(a);
    Event pointer_read,continue_store;view.table.entries[1].pointer.after_load=[&] {pointer_read.Set();continue_store.Wait();};
    auto writer=std::async(std::launch::async,[&] {auto fast=page.TryFast();Require(bool(fast),"untracked fast admission");view.Write<u8>(4096,77);});
    pointer_read.Wait();u8 uploaded=0;
    auto intent=RegistrationIntent(e.ranges,page,p);Require(bool(intent),"registration claim");
    auto registration=e.driver->Submit(1,p,true,[&](auto) {
        view.RasterizerMarkRegionCached(4096,4096,true);uploaded=a->bytes[0];Require(page.PublishTracked(),"publish tracked");return Work::Complete;
    },[&] {return page.Drained();},[] {return true;},intent);
    registration->parked.Wait();Require(!page.TryFast(),"closing accepted untracked writer");
    continue_store.Set();Ready(writer);registration->finished.Wait();
    Require(uploaded==77&&view.notifications==0&&page.IsTracked(),"first registration missed old uncached store");
}
void registration_gpu_first() {
    Environment e;auto a=std::make_shared<Allocation>(10);auto p=PlanFor(a,{{0,4096}});auto gate=e.domain.PageFor(a,0);Page& page=*gate;CpuMemory view(a);
    auto intent=RegistrationIntent(e.ranges,page,p);Require(bool(intent),"registration claim");Require(!page.TryFast(),"new untracked writer bypassed registrar");
    auto cpu=e.driver->Submit(2,p,true,[](auto) {return Work::KeepLease;});cpu->parked.Wait();
    auto registration=e.driver->Submit(1,p,true,[&](auto) {view.RasterizerMarkRegionCached(4096,4096,true);Require(page.PublishTracked(),"tracked release");return Work::Complete;},[] {return true;},[] {return true;},intent);
    registration->finished.Wait();cpu->granted.Wait();
    e.ranges.BeginWrite(cpu->ticket);view.Write<u8>(4096,77);std::array<Range,1> done{{{0,1}}};Commit(e,cpu,done);
    Require(view.notifications==1&&a->bytes[0]==77&&cpu->ticket->published.size()==1,"registrar-first protected write");
}
void registration_two_fast_writers() {
    PhysicalDomain domain;auto allocation=std::make_shared<Allocation>(19);
    auto node=domain.PageFor(allocation,0),alias=domain.PageFor(allocation,0);
    Require(node==alias,"physical aliases created distinct handshake nodes");
    Page& page=*node;auto first=page.TryFast(),second=alias->TryFast();
    Require(bool(first)&&bool(second)&&page.CloseForRegistration(),"multiple untracked admission");
    CpuMemory a(allocation),b(allocation);
    a.Write<u8>(4096,77);b.Write<u8>(4097,88);
    first.Release();Require(!page.Drained()&&!page.PublishTracked(),"did not drain all old writers");
    second.Release();Require(page.Drained()&&page.PublishTracked(),"last writer did not release registration");
    Require(allocation->bytes[0]==77&&allocation->bytes[1]==88,"physical aliases used different handshake state");
}
void pinned_remap_and_resolver_drain() {
    Environment e;auto a=std::make_shared<Allocation>(11),b=std::make_shared<Allocation>(12);auto p=PlanFor(a);Mapping map(p,e.ranges.progress);
    auto resolving=map.BeginResolve();Require(resolving.has_value(),"resolver");
    Require(map.Intent(PlanFor(b))&&!map.Apply(true),"PTE changed across unresolved snapshot");
    resolving->Release();Require(!map.Apply(false),"host-only/cancelled preservation accepted");
    auto old=e.driver->Submit(1,p,true,[](auto) {return Work::KeepLease;});old->granted.Wait();
    Require(map.Apply(true),"physical pin unnecessarily held mapping use");
    CpuMemory view(p->allocation);e.ranges.BeginWrite(old->ticket);view.Write<u8>(4096,77);
    std::array<Range,1> done{{{0,1}}};Commit(e,old,done);
    Require(a->bytes[0]==77&&b->bytes[0]==10,"retained plan retranslated after remap");
}
void shutdown_pending_and_running() {
    Environment e;auto a=std::make_shared<Allocation>(13);auto p=PlanFor(a);
    auto running=e.driver->Submit(1,p,true,[](auto) {return Work::KeepLease;});running->granted.Wait();
    std::atomic<int> cancelled_stores=0;
    auto pending=e.driver->Submit(2,p,true,[&](auto) {++cancelled_stores;return Work::Complete;});pending->parked.Wait();
    e.domain.Seal();e.driver->Seal();pending->finished.Wait();Require(cancelled_stores==0,"sealed queued action ran");
    auto rejected=e.driver->Submit(3,p,true,[&](auto) {++cancelled_stores;return Work::Complete;});rejected->finished.Wait();
    CpuMemory view(a);e.ranges.BeginWrite(running->ticket);view.Write<u8>(4096,77);
    std::array<Range,1> done{{{0,1}}};Commit(e,running,done);
    Require(a->bytes[0]==77&&cancelled_stores==0,"running store falsely cancelled");
    e.manager.Stop();
    Require(VideoCommon::RunAcknowledgedCallback([&](auto f) {e.manager.RunOnGpuThread(std::move(f));},[] {})==VideoCommon::CallbackOutcome::Cancelled,"closed manager success");
}
void reentry_and_domain() {
    Ranges r,other;auto a=std::make_shared<Allocation>(14);auto p=PlanFor(a);auto parent=r.Enqueue(p,true),child=r.Enqueue(p,true);
    Require(r.TryStart(parent)==Start::Granted,"parent grant");
    Require(r.TryStart(child,parent)==Start::Reentrant,"callback recursively acquired own exclusion");
    Require(other.TryStart(child)==Start::Cancelled,"foreign range ticket accepted");
    Require(!r.Cancel(parent),"running operation cancelled as unexecuted");r.CompleteNoWrite(parent);Require(r.TryStart(child)==Start::Granted,"child did not progress");r.CompleteNoWrite(child);
}
void unknown_and_backing_replacement() {
    auto ranges=std::make_unique<Ranges>();auto recovery=ranges->RecoveryOwner();
    auto a=std::make_shared<Allocation>(15);std::weak_ptr<Allocation> weak=a;
    auto p=PlanFor(a);auto request=ranges->Enqueue(p,true);Require(ranges->TryStart(request)==Start::Granted,"unknown start");
    ranges->BeginWrite(request);CpuMemory view(a);view.Write<u8>(4096,77);ranges->Unknown(request);
    Require(!ranges->Cancel(request),"unknown claimed unexecuted");ranges->Seal();request.reset();p.reset();a.reset();view.storage.reset();ranges.reset();
    Require(!weak.expired(),"unknown backing released with facade");
    recovery->ReleaseUnknownAfterQuiescence(true);Require(weak.expired(),"quiescence failed to release unknown backing");
    Ranges replacement;auto fresh=std::make_shared<Allocation>(16);auto next=replacement.Enqueue(PlanFor(fresh),true);
    Require(next&&replacement.TryStart(next)==Start::Granted,"new backing incarnation sealed by old owner");replacement.CompleteNoWrite(next);
}
void partial_unknown_and_unsupported() {
    Ranges r;auto a=std::make_shared<Allocation>(17);auto p=PlanFor(a);auto request=r.Enqueue(p,true);
    Require(r.TryStart(request)==Start::Granted,"partial grant");r.BeginWrite(request);a->bytes[0]=77;
    std::array<Range,1> outside{{{8192,1}}};Require(!r.Publish(request,outside)&&r.StateOf(request)==Phase::Unknown,"uncertain store published");
    auto unrelated=r.Enqueue(PlanFor(a,{{8192,1}}),true);
    Require(r.TryStart(unrelated)==Start::Park,"unknown did not quarantine allocation");
    bool rejected=false;try {(void)std::make_shared<Plan>(a,std::vector<Range>{{0,4}},std::make_shared<int>(),ScopeCertificate::Unsupported);} catch(const std::invalid_argument&) {rejected=true;}
    Require(rejected,"unknown alias accepted");
    rejected=false;try {(void)PlanFor(a,{{0,4},{1,1}});} catch(const std::invalid_argument&) {rejected=true;}
    Require(rejected,"repeated physical write scope accepted");r.Seal();r.ReleaseUnknownAfterQuiescence(true);
}
void registration_seal() {
    PhysicalDomain domain;auto allocation=std::make_shared<Allocation>(25);auto node=domain.PageFor(allocation,0);
    Page& page=*node;auto fast=page.TryFast();Require(bool(fast)&&page.CloseForRegistration(),"registration fast entry");
    domain.Seal();Require(!domain.Drained()&&!domain.PageFor(allocation,4096),"backing seal admitted new page or lost fast use");
    fast.Release();Require(!page.TryFast()&&!page.PublishTracked()&&domain.Drained(),"registration published after shutdown");
}
void callback_failure() {
    Environment e;auto p=PlanFor(std::make_shared<Allocation>(18));
    auto failed=e.driver->Submit(1,p,true,[](auto)->Work {throw VideoCommon::UnsupportedGatherMapping("candidate failure");});
    failed->finished.Wait();Require(failed->state==JobState::Unknown,"started GPU failure freed reservation");
    auto skipped=e.driver->Submit(2,p,false,[](auto) {throw std::logic_error("failed GPU worker executed new work");return Work::Complete;});
    skipped->finished.Wait();Require(skipped->state==JobState::Cancelled,"failed-mode queue acknowledgement was success");
    e.driver->Seal();e.driver->Stop();e.manager.Stop();e.ranges.ReleaseUnknownAfterQuiescence(true);
}
void explicit_device_completion() {
    Environment e;auto a=std::make_shared<Allocation>(20);auto replica=std::make_shared<Replica>();auto p=PlanFor(a,{{0,4}},replica);
    auto device=e.driver->Submit(1,p,true,[](auto) {return Work::KeepLease;});device->granted.Wait();
    auto cpu=e.driver->Submit(2,p,true,[&](auto) {replica->Preserve(*a,{0,4});return Work::KeepLease;});cpu->parked.Wait();
    Require(e.ranges.StateOf(device->ticket)==Phase::Running&&a->bytes[0]==10,"host callback treated as device visibility");
    // Explicit fixture device/visibility event; not a real Vulkan fence.
    replica->GpuWrite({0,4},99);e.ranges.CompleteNoWrite(device->ticket);e.driver->ExternalDone(device);
    cpu->granted.Wait();Require(a->bytes[0]==99,"preserved before declared device completion");
    CpuMemory view(a);e.ranges.BeginWrite(cpu->ticket);view.Write<u8>(4097,77);
    std::array<Range,1> done{{{1,1}}};Commit(e,cpu,done);
}
void resolver_continuation_and_exception() {
    Environment e;auto a=std::make_shared<Allocation>(21),b=std::make_shared<Allocation>(22);auto p=PlanFor(a);Mapping map(p,e.ranges.progress);
    auto old_ready=e.ranges.Enqueue(p,false);auto resolution=map.BeginResolve();Require(bool(resolution),"resolver admission");
    Require(map.Intent(PlanFor(b)),"resolver intent");
    auto mutation=e.driver->Submit(1,map.Old(),true,[&](auto) {Require(map.Apply(true),"resolver continuation apply");return Work::Complete;},[&] {return map.ResolversDrained();});
    mutation->parked.Wait();std::atomic<bool> old_ran=false;
    auto old=e.driver->Submit(2,p,false,[&](auto) {old_ran=true;return Work::Complete;},[] {return true;},[] {return true;},old_ready);
    old->finished.Wait();Require(old_ran,"old ready work trapped by mutation intent");
    resolution.reset();mutation->finished.Wait(); // RAII drain emits the retry notification
    try {auto throwing=map.BeginResolve();Require(bool(throwing),"exception resolver");throw std::runtime_error("resolution failed");}
    catch(const std::runtime_error&) {}
    Require(map.ResolversDrained(),"exception leaked resolver use");
    Require(map.Intent(p)&&!map.Intent(p),"recursive mutation accepted");map.Seal();Require(!map.BeginResolve(),"sealed mapping resolved");
}
void cleanup_reentrancy_and_fast_lifetime() {
    Ranges ranges;auto a=std::make_shared<Allocation>(23);int destroyed=0;
    auto fallback=PlanFor(std::make_shared<Allocation>(24));
    auto pin=std::shared_ptr<int>(new int(1),[&](int* p) {
        ++destroyed;Require(!ranges.Enqueue(fallback,false),"reentrant deleter ran before seal");delete p;
    });
    auto p=PlanFor(a,{{0,4}},pin);auto pending=ranges.Enqueue(p,true);pending.reset();pin.reset();p.reset();
    ranges.Seal();Require(destroyed==1,"resource deleter ran under metadata lock or leaked");
    auto page=std::make_unique<Page>(ranges.progress);auto fast=page->TryFast();Require(bool(fast),"fast lifetime entry");
    page.reset();fast.Release(); // handshake state is owned until the last accessor exits
}
void fast_entry_seal_race() {
    PhysicalDomain domain;auto allocation=std::make_shared<Allocation>(29);auto node=domain.PageFor(allocation,0);
    CpuMemory view(allocation);Event entered,resume;fast_entered=&entered;fast_resume=&resume;
    auto cpu=std::async(std::launch::async,[&] {auto fast=node->TryFast();if(fast) view.Write<u8>(4096,77);return bool(fast);});
    entered.Wait();domain.Seal();Require(!domain.Drained(),"provisional fast entry disappeared at seal");
    resume.Set();Require(!Ready(cpu),"fast entry ignored backing epoch seal");
    Require(allocation->bytes[0]==10&&domain.Drained(),"rejected fast entry stored bytes or leaked obligation");
    fast_entered=nullptr;fast_resume=nullptr;
}
void multi_page_fast_handshake() {
    Environment e;auto a=std::make_shared<Allocation>(30);
    std::array<std::shared_ptr<Page>,2> pages{{e.domain.PageFor(a,0),e.domain.PageFor(a,4096)}};
    auto fast=FastSet::Try(pages);Require(bool(fast),"multi-page fast admission");
    auto p0=PlanFor(a,{{0,4096}}),p1=PlanFor(a,{{4096,4096}});
    auto t0=RegistrationIntent(e.ranges,*pages[0],p0),t1=RegistrationIntent(e.ranges,*pages[1],p1);
    CpuMemory view(a);u8 uploaded_first=0,uploaded_second=0;
    auto r0=e.driver->Submit(1,p0,true,[&](auto) {view.RasterizerMarkRegionCached(4096,4096,true);uploaded_first=a->bytes[4095];Require(pages[0]->PublishTracked(),"first tracked");return Work::Complete;},[&] {return pages[0]->Drained();},[] {return true;},t0);
    auto r1=e.driver->Submit(2,p1,true,[&](auto) {view.RasterizerMarkRegionCached(8192,4096,true);uploaded_second=a->bytes[4096];Require(pages[1]->PublishTracked(),"second tracked");return Work::Complete;},[&] {return pages[1]->Drained();},[] {return true;},t1);
    r0->parked.Wait();r1->parked.Wait();view.Write<u64>(8191,0x8877665544332211ULL);fast->Release();
    r0->finished.Wait();r1->finished.Wait();Require(uploaded_first==0x11&&uploaded_second==0x22&&a->bytes[4102]==0x88,"split first upload consumed partial scalar");
    PhysicalDomain rejected;auto b=std::make_shared<Allocation>(31);
    std::array<std::shared_ptr<Page>,2> blocked{{rejected.PageFor(b,0),rejected.PageFor(b,4096)}};
    auto intent=RegistrationIntent(rejected.ranges,*blocked[1],PlanFor(b,{{4096,4096}}));Require(bool(intent),"rollback registration");
    Require(!FastSet::Try(blocked)&&blocked[0]->NoFastUsers()&&b->bytes[0]==10,"partial fast admission modified bytes or leaked count");
    rejected.Seal();
}
void bounded_continuation_metadata() {
    Environment e;auto p=PlanFor(std::make_shared<Allocation>(32));
    for(int i=0;i<256;++i) {
        auto job=e.driver->Submit(1,p,false,[](auto) {return Work::Complete;});job->finished.Wait();
    }
    e.driver->Stop();Require(e.driver->RetainedJobs()<=2&&e.ranges.Drained(),"completed continuations accumulated indefinitely");
}
void actual_inline_reentry() {
    Environment e;auto p=PlanFor(std::make_shared<Allocation>(33));int nested=0;
    auto job=e.driver->Submit(1,p,true,[&](auto) {
        auto child=e.ranges.Enqueue(p,true);
        e.manager.RunOnGpuThread([&] {
            ++nested;Require(e.manager.IsGpuThread(),"nested callback escaped GPU thread");
            Require(e.ranges.TryStart(child)==Start::Reentrant,"inline callback blocked on its own inherited range");
            Require(e.ranges.Cancel(child),"rejected child retained range priority");
        });
        return Work::Complete;
    });
    job->finished.Wait();Require(nested==1,"inline callback failed to finish");
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    std::string name=argv[1];
    try {
        if(name=="old_mutation") old_work_mutation_preserve();
        else if(name=="suspended") suspended_channel();
        else if(name=="new_cpu_suspended") new_cpu_producer_during_unrelated_intent();
        else if(name=="upload") upload_during_store();
        else if(name=="cas") cas_and_exact_publication();
        else if(name=="split_partial") split_and_partial_block();
        else if(name=="writers") conflicting_and_disjoint_writers();
        else if(name=="cpu_first") registration_cpu_first();
        else if(name=="gpu_first") registration_gpu_first();
        else if(name=="two_fast") registration_two_fast_writers();
        else if(name=="remap") pinned_remap_and_resolver_drain();
        else if(name=="shutdown") shutdown_pending_and_running();
        else if(name=="reentry") reentry_and_domain();
        else if(name=="unknown") unknown_and_backing_replacement();
        else if(name=="unsupported") partial_unknown_and_unsupported();
        else if(name=="registration_seal") registration_seal();
        else if(name=="callback_failure") callback_failure();
        else if(name=="device_completion") explicit_device_completion();
        else if(name=="resolver") resolver_continuation_and_exception();
        else if(name=="cleanup") cleanup_reentrancy_and_fast_lifetime();
        else if(name=="fast_seal_race") fast_entry_seal_race();
        else if(name=="multi_fast") multi_page_fast_handshake();
        else if(name=="bounded") bounded_continuation_metadata();
        else if(name=="inline_reentry") actual_inline_reentry();
        else return 2;
        std::cout<<"PASS candidate "<<name<<"\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<name<<": "<<e.what()<<"\n";return 1;}
}
