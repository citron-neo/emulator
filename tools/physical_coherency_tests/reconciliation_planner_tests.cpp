// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <iostream>
#include "video_core/renderer_vulkan/vk_reconciliation_planner.h"

using Planner=Vulkan::BoundedReconciliationPlanner;
using Content=Vulkan::TransferContent;
using Root=Planner::RootSpan;
struct Mapping {
    std::shared_ptr<Common::GuestBackingEpoch> backing=std::make_shared<Common::GuestBackingEpoch>();
    std::shared_ptr<Common::MappingEpoch> epoch=std::make_shared<Common::MappingEpoch>();
    template<typename F> bool ReadPhysicalMappingSnapshot(F f) const {
        return f(epoch,epoch->generation.load(),backing,4096,[](u64 address)->std::optional<u64>{return address%4096;});
    }
};
struct Fixture {
    struct Byte {u32 root;u64 offset;u8 value;};
    Mapping mapping;
    u64 extent;
    std::shared_ptr<const Content> anchor;
    std::array<std::shared_ptr<const VideoCommon::PhysicalWriteIdentity>,3> identities;
    std::array<std::shared_ptr<const Vulkan::ReplicaCopyProvenance>,3> receipts;
    std::array<std::vector<Root>,3> roots;
    std::array<std::vector<Byte>,3> images;
    explicit Fixture(u64 size=64,bool mixed=false):extent{size} {
        const std::array<Content::Offset,3> sizes{{size,size,size}};
        std::vector<Content::Command> commands;
        for(u32 slot=0;slot<3;++slot) {
            const u8 value=slot?slot*10:99;
            commands.push_back({Content::Kind::Fill,slot,0,0,0,size,u32(value)*0x01010101u});
            for(u64 b=0;b<size;++b) images[slot].push_back({slot,b,value});
            auto identity=VideoCommon::PhysicalWriteIdentity::Capture(mapping,(slot+1)*4096,size,reinterpret_cast<const void*>(uintptr_t(slot+1)));
            assert(identity);identities[slot]=std::make_shared<const VideoCommon::PhysicalWriteIdentity>(*identity);
        }
        if(mixed) {
            const auto copy=[&](u32 dst,u32 src,u64 to,u64 from,u64 length) {
                commands.push_back({Content::Kind::Copy,dst,src,to,from,length});const auto before=images[src];
                for(u64 b=0;b<length;++b) images[dst][to+b]=before[from+b];
            };
            copy(0,0,8,0,8);copy(0,0,56,32,8);copy(2,1,24,4,8);copy(2,0,48,16,8);
        }
        anchor=std::make_shared<const Content>(*Content::Build(sizes,commands));
        roots[0]={{8,8,0,8},{56,8,0,56}};roots[1]={{32,8,1,32}};
        TargetPartition();
    }
    void TargetPartition() {
        roots[2].clear();
        for(u64 b=0;b<extent;++b) {
            const auto label=images[2][b];
            if(!roots[2].empty() && roots[2].back().fill==label.root && roots[2].back().root_offset+roots[2].back().size==label.offset)
                ++roots[2].back().size;
            else roots[2].push_back({b,1,label.root,label.offset});
        }
    }
    template<typename Evidence> Planner::Result Build(const Evidence& evidence,unsigned count=3) const {
        std::vector<Planner::Replica> replicas;
        for(u32 slot=0;slot<3;++slot) if(count==3||slot!=1) replicas.push_back({slot,identities[slot],receipts[slot],roots[slot]});
        return Planner::Build({anchor,{mapping.backing,0,extent},2,replicas},evidence);
    }
};
int main() {
    using State=Planner::State;using Reason=Planner::Reason;
    const auto valid=[](const auto&,const Root&){return Reason::None;};
    const auto expect=[](const Planner::Result& result,State state,Reason reason) {
        assert(result.state==state && result.reason==reason);
        assert(bool(result.content)==(state==State::Candidate));
    };
    Fixture base;expect(base.Build(valid),State::Candidate,Reason::None);
    expect(base.Build(valid,2),State::Candidate,Reason::None);
    // Fully verified different roots conflict; completing later never breaks the tie.
    base.roots[1]={{8,8,1,8}};expect(base.Build(valid),State::Refused,Reason::ConflictingSources);
    base.roots[1]={{32,8,1,32}};
    expect(base.Build([](const auto& replica,const Root&){return replica.slot==1?Reason::CurrentUnavailable:Reason::None;}),State::Unknown,Reason::CurrentUnavailable);
    base.roots[0][0].root_offset++;expect(base.Build(valid),State::Refused,Reason::RootMismatch);base.roots[0][0].root_offset--;
    base.roots[0][0].fill=Content::Unknown;expect(base.Build(valid),State::Unknown,Reason::MissingContent);base.roots[0][0].fill=0;
    base.roots[2][0].root_offset++;expect(base.Build(valid),State::Refused,Reason::RootMismatch);base.TargetPartition();
    for(const auto reason:{Reason::Remapped,Reason::Retired,Reason::SynchronizationUnknown,Reason::CrossQueueUnknown,Reason::SubmissionUnknown})
        expect(base.Build([&](const auto&,const Root&){return reason;}),State::Unknown,reason);
    expect(base.Build([](const auto&,const Root&)->Reason{throw 1;}),State::Unknown,Reason::EvidenceUnavailable);
    base.mapping.epoch->generation.store(2);expect(base.Build(valid),State::Unknown,Reason::Remapped);base.mapping.epoch->generation.store(0);
    base.mapping.backing->alive.store(false);expect(base.Build(valid),State::Unknown,Reason::Retired);base.mapping.backing->alive.store(true);
    const auto original=base.identities[1];base.identities[1]=base.identities[0];expect(base.Build(valid),State::Refused,Reason::NativeAlias);base.identities[1]=original;
    base.roots[0]={{9,4,0,9}};expect(base.Build(valid),State::Refused,Reason::Alignment);
    base.roots[0]={{8,8,0,8},{12,4,0,12}};expect(base.Build(valid),State::Refused,Reason::Partition);
    base.roots[0].clear();base.roots[1].clear();expect(base.Build(valid),State::Refused,Reason::NoWrites);
    base.roots[2][0].fill=Content::Unknown;expect(base.Build(valid),State::Unknown,Reason::MissingContent);
    {
        Fixture same;
        const std::array<Content::Offset,3> sizes{{64,64,64}};
        const std::array<Content::Command,4> history{{{Content::Kind::Fill,0,0,0,0,64,99},{Content::Kind::Fill,1,0,0,0,64,10},
            {Content::Kind::Fill,2,0,0,0,64,20},{Content::Kind::Copy,1,0,0,0,64}}};
        same.anchor=std::make_shared<const Content>(*Content::Build(sizes,history));same.roots[1]={{8,8,0,8}};
        expect(same.Build(valid),State::Unknown,Reason::AmbiguousSources);
        same.roots[1][0].root_offset=12;expect(same.Build(valid),State::Refused,Reason::RootMismatch);
    }
    {
        Fixture boundary(512);boundary.roots[0].clear();boundary.roots[1].clear();
        for(u64 b=0;b<256;b+=8) boundary.roots[0].push_back({b,4,0,b});
        const auto full=boundary.Build(valid);expect(full,State::Candidate,Reason::None);assert(full.content->Regions().size()==32);
        boundary.roots[0].push_back({256,4,0,256});expect(boundary.Build(valid),State::Refused,Reason::Budget);
        boundary.roots[0].clear();
        for(u64 b=0;b<62;++b) boundary.roots[0].push_back({b,1,0,b});
        boundary.roots[0].push_back({62,2,0,62});expect(boundary.Build(valid),State::Candidate,Reason::None); // 64 total declarations.
        boundary.roots[0].back().size=1;boundary.roots[0].push_back({63,1,0,63});expect(boundary.Build(valid),State::Refused,Reason::Budget);
    }
    {
        Fixture input;
        std::vector<Planner::Replica> replicas;
        for(u32 slot=0;slot<3;++slot) replicas.push_back({slot,input.identities[slot],{},input.roots[slot]});
        Planner::Request request{input.anchor,{input.mapping.backing,0,64},2,replicas};
        request.scope.backing=std::make_shared<Common::GuestBackingEpoch>();
        expect(Planner::Build(request,valid),State::Refused,Reason::BackingMismatch);
        request.scope.backing=input.mapping.backing;request.scope.offset=4;
        expect(Planner::Build(request,valid),State::Refused,Reason::PhysicalGeometry);
        const auto source_identity=replicas[0].physical;
        const auto same_address=VideoCommon::PhysicalWriteIdentity::Capture(input.mapping,3*4096,64,reinterpret_cast<const void*>(uintptr_t{1}));
        assert(same_address);replicas[0].physical=std::make_shared<const VideoCommon::PhysicalWriteIdentity>(*same_address);
        request.scope.offset=0;expect(Planner::Build(request,valid),State::Refused,Reason::PhysicalGeometry);replicas[0].physical=source_identity;
        request.scope.offset=std::numeric_limits<u64>::max()-31;
        expect(Planner::Build(request,valid),State::Refused,Reason::InvalidRequest);
        request.scope.offset=0;request.scope.size=0;
        expect(Planner::Build(request,valid),State::Refused,Reason::InvalidRequest);
        request.scope.size=64;request.destination=7;
        expect(Planner::Build(request,valid),State::Refused,Reason::InvalidRequest);
        request.destination=2;replicas.push_back(replicas.back());request.replicas=replicas;
        expect(Planner::Build(request,valid),State::Refused,Reason::Budget);
        expect(input.Build([](const auto& replica,const Root&){return replica.slot==2?Reason::CurrentUnavailable:Reason::None;}),State::Unknown,Reason::CurrentUnavailable);
    }
    u32 random=0x3724;const auto next=[&]{random=random*1664525u+1013904223u;return random;};
    for(unsigned trial=0;trial<128;++trial) {
        Fixture fixture(64,true);fixture.roots[0].clear();fixture.roots[1].clear();
        auto expected=fixture.images[2];std::array<int,64> selected;selected.fill(-1);
        for(u64 begin=0;begin<64;begin+=4) {
            const auto choice=next()%3;if(choice==2) continue;
            const auto label=fixture.images[choice][begin];fixture.roots[choice].push_back({begin,4,label.root,label.offset});
            for(u64 b=begin;b<begin+4;++b) {selected[b]=choice;expected[b]=fixture.images[choice][b];}
        }
        const auto plan=fixture.Build(valid);expect(plan,State::Candidate,Reason::None);
        auto actual=fixture.images[2];
        for(const auto& region:plan.content->Regions()) for(u64 b=0;b<region.size;++b) {
            assert(selected[region.destination_offset+b]==int(region.source));actual[region.destination_offset+b]=fixture.images[region.source][region.source_offset+b];
        }
        for(u64 b=0;b<64;++b) {
            assert(plan.content->HasRoot(b,1,expected[b].root,expected[b].offset));
            assert(!plan.content->HasRoot(b,1,expected[b].root,expected[b].offset+1));
            size_t covering{};for(const auto& region:plan.content->Regions())
                covering+=b>=region.destination_offset&&b-region.destination_offset<region.size;
            assert(covering==(selected[b]>=0?1u:0u));
            assert(actual[b].value==expected[b].value && actual[b].root==expected[b].root && actual[b].offset==expected[b].offset);
        }
        // Candidate owns the recipe but never updates the input history or grants a lease.
        fixture.roots[0].clear();fixture.roots[1].clear();fixture.roots[2].clear();
        for(u64 b=0;b<64;++b) assert(plan.content->HasRoot(b,1,expected[b].root,expected[b].offset));
    }
    {
        Fixture hops;
        for(unsigned hop=1;hop<=4;++hop) {
            const auto plan=hops.Build(valid);expect(plan,State::Candidate,Reason::None);assert(plan.content->Depth()==hop);
            for(const auto& region:plan.content->Regions()) for(u64 b=0;b<region.size;++b)
                hops.images[2][region.destination_offset+b]=hops.images[region.source][region.source_offset+b];
            hops.TargetPartition();hops.receipts[2]=plan.content;
        }
        expect(hops.Build(valid),State::Refused,Reason::Ancestry);
    }
    std::cout<<"PASS bounded reconciliation logic: 128 independent byte oracles, unique/conflicting/ambiguous/Unknown sources, "
        "mapping/lifetime/evidence, exact complements, 2-3 replicas, 32-command/64-declaration budgets, immutable recipes and four hops\n";
}
